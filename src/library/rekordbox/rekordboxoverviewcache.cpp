#include "library/rekordbox/rekordboxoverviewcache.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPixmapCache>
#include <QtConcurrentRun>

#include "library/rekordbox/rekordboxoverviewrenderer.h"
#include "moc_rekordboxoverviewcache.cpp"
#include "util/logger.h"

namespace mixxx {
namespace rekordbox {

namespace {

const mixxx::Logger kLogger("RekordboxOverviewCache");

QString siblingAnalyzePath(const QString& analyzePath, const QString& extension) {
    if (analyzePath.size() < 3) {
        return QString();
    }

    QString sibling = analyzePath;
    sibling.chop(3);
    sibling.append(extension);
    return sibling;
}

QString fileIdentityToken(const QString& path) {
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return QStringLiteral("%1|missing").arg(path);
    }

    return QStringLiteral("%1|%2|%3")
            .arg(path,
                    QString::number(fileInfo.size()),
                    QString::number(fileInfo.lastModified().toMSecsSinceEpoch()));
}

} // anonymous namespace

OverviewCache::OverviewCache(
        QObject* pParent,
        LoadPreviewFn loadPreviewFn)
        : QObject(pParent),
          m_loadPreviewFn(std::move(loadPreviewFn)) {
    if (!m_loadPreviewFn) {
        m_loadPreviewFn = &loadWaveformPreview;
    }
    m_threadPool.setMaxThreadCount(2);
}

OverviewCache* OverviewCache::instance() {
    static OverviewCache s_instance;
    return &s_instance;
}

OverviewCache::PreviewIdentity OverviewCache::currentPreviewIdentity(
        const QString& analyzePath) {
    const bool isExtPath = analyzePath.endsWith(QStringLiteral("EXT"), Qt::CaseInsensitive);
    const QString extPath =
            isExtPath ? analyzePath : siblingAnalyzePath(analyzePath, QStringLiteral("EXT"));
    const QString datPath =
            isExtPath ? siblingAnalyzePath(analyzePath, QStringLiteral("DAT")) : analyzePath;

    const QString identityData = QStringLiteral("%1\n%2").arg(
            fileIdentityToken(extPath), fileIdentityToken(datPath));
    const QString previewKey = QStringLiteral("RekordboxPreview_%1")
                                       .arg(QString::fromLatin1(
                                               QCryptographicHash::hash(
                                                       identityData.toUtf8(),
                                                       QCryptographicHash::Sha1)
                                                       .toHex()));
    return {analyzePath, previewKey};
}

QString OverviewCache::pixmapCacheKey(
        const QString& previewKey,
        QSize desiredSize) const {
    return QStringLiteral("RekordboxOverview_%1_%2_%3")
            .arg(previewKey,
                    QString::number(desiredSize.width()),
                    QString::number(desiredSize.height()));
}

QPixmap OverviewCache::requestCachedOverview(
        const QString& analyzePath,
        const QObject* pRequester,
        QSize desiredSize) {
    Q_UNUSED(pRequester);
    if (analyzePath.isEmpty() || desiredSize.isEmpty()) {
        return QPixmap();
    }

    const PreviewIdentity previewIdentity = currentPreviewIdentity(analyzePath);
    const QString key = pixmapCacheKey(previewIdentity.previewKey, desiredSize);
    if (m_loadingPreviewKeys.contains(previewIdentity.previewKey) ||
            m_loadingPixmapKeys.contains(key)) {
        return QPixmap();
    }
    const auto previewIt = m_previewCache.constFind(previewIdentity.previewKey);
    if (previewIt != m_previewCache.constEnd() && !previewIt->preview) {
        return QPixmap();
    }

    QPixmap pixmap;
    QPixmapCache::find(key, &pixmap);
    return pixmap;
}

QPixmap OverviewCache::requestUncachedOverview(
        const QString& analyzePath,
        const QObject* pRequester,
        QSize desiredSize) {
    if (analyzePath.isEmpty() || desiredSize.isEmpty()) {
        return QPixmap();
    }

    const PreviewIdentity previewIdentity = currentPreviewIdentity(analyzePath);
    const QString key = pixmapCacheKey(previewIdentity.previewKey, desiredSize);
    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap)) {
        return pixmap;
    }

    const auto previewIt = m_previewCache.constFind(previewIdentity.previewKey);
    if (previewIt != m_previewCache.constEnd() && !previewIt->preview) {
        return QPixmap();
    }

    PendingPixmapRequest& request = m_pendingRequestsByPixmapKey[key];
    if (request.analyzePath.isEmpty()) {
        request.analyzePath = analyzePath;
        request.previewKey = previewIdentity.previewKey;
        request.desiredSize = desiredSize;
        m_pixmapKeysByPreviewKey.insert(previewIdentity.previewKey, key);
    }
    request.requesters.insert(pRequester);

    if (m_loadingPixmapKeys.contains(key)) {
        return QPixmap();
    }

    if (previewIt != m_previewCache.constEnd()) {
        scheduleRender(key);
        return QPixmap();
    }

    if (!m_loadingPreviewKeys.contains(previewIdentity.previewKey)) {
        schedulePreviewLoad(previewIdentity);
    }
    return QPixmap();
}

void OverviewCache::schedulePreviewLoad(
        const PreviewIdentity& previewIdentity) {
    if (m_loadingPreviewKeys.contains(previewIdentity.previewKey)) {
        return;
    }

    m_loadingPreviewKeys.insert(previewIdentity.previewKey);

    auto* pWatcher = new QFutureWatcher<FutureResult>(this);
    const auto loadPreviewFn = m_loadPreviewFn;
    const QString analyzePath = previewIdentity.analyzePath;
    const QString previewKey = previewIdentity.previewKey;
    QFuture<FutureResult> future = QtConcurrent::run(
            &m_threadPool,
            [loadPreviewFn, analyzePath, previewKey]() {
                FutureResult result;
                result.jobKind = JobKind::LoadPreview;
                result.analyzePath = analyzePath;
                result.previewKey = previewKey;
                result.loadResult = loadPreviewFn(analyzePath);
                return result;
            });
    connect(pWatcher,
            &QFutureWatcher<FutureResult>::finished,
            this,
            &OverviewCache::overviewPrepared);
    pWatcher->setFuture(future);
}

void OverviewCache::scheduleRender(
        const QString& pixmapKey) {
    if (m_loadingPixmapKeys.contains(pixmapKey)) {
        return;
    }

    const auto requestIt = m_pendingRequestsByPixmapKey.constFind(pixmapKey);
    if (requestIt == m_pendingRequestsByPixmapKey.constEnd()) {
        return;
    }
    const auto previewIt = m_previewCache.constFind(requestIt->previewKey);
    if (previewIt == m_previewCache.constEnd() || !previewIt->preview) {
        finishPendingPixmapRequest(pixmapKey, false);
        return;
    }

    const WaveformPreview preview = *previewIt->preview;
    const QString analyzePath = requestIt->analyzePath;
    const QString previewKey = requestIt->previewKey;
    const QSize desiredSize = requestIt->desiredSize;
    m_loadingPixmapKeys.insert(pixmapKey);

    auto* pWatcher = new QFutureWatcher<FutureResult>(this);
    QFuture<FutureResult> future = QtConcurrent::run(
            &m_threadPool,
            [preview, analyzePath, previewKey, pixmapKey, desiredSize]() {
                FutureResult result;
                result.jobKind = JobKind::RenderPixmap;
                result.analyzePath = analyzePath;
                result.previewKey = previewKey;
                result.pixmapKey = pixmapKey;
                result.resizedToSize = desiredSize;
                result.image = renderOverview(preview, desiredSize);
                return result;
            });
    connect(pWatcher,
            &QFutureWatcher<FutureResult>::finished,
            this,
            &OverviewCache::overviewPrepared);
    pWatcher->setFuture(future);
}

void OverviewCache::finishPendingPixmapRequest(
        const QString& pixmapKey,
        bool pixmapValid) {
    const auto requestIt = m_pendingRequestsByPixmapKey.find(pixmapKey);
    if (requestIt == m_pendingRequestsByPixmapKey.end()) {
        return;
    }

    const PendingPixmapRequest request = requestIt.value();
    m_pendingRequestsByPixmapKey.erase(requestIt);
    m_pixmapKeysByPreviewKey.remove(request.previewKey, pixmapKey);

    for (const QObject* pRequester : request.requesters) {
        emit overviewReady(pRequester, request.analyzePath, pixmapValid);
    }
}

void OverviewCache::overviewPrepared() {
    auto* pWatcher = static_cast<QFutureWatcher<FutureResult>*>(sender());
    const FutureResult result = pWatcher->result();
    pWatcher->deleteLater();

    if (result.jobKind == JobKind::LoadPreview) {
        m_loadingPreviewKeys.remove(result.previewKey);
        m_previewCache.insert(result.previewKey, result.loadResult);

        if (result.loadResult.error == WaveformPreviewLoadError::ParseFailure &&
                !result.loadResult.errorPath.isEmpty() &&
                !m_loggedParseFailures.contains(result.loadResult.errorPath)) {
            m_loggedParseFailures.insert(result.loadResult.errorPath);
            kLogger.warning() << "Failed to parse Rekordbox ANLZ preview:"
                              << result.loadResult.errorPath;
        }

        const QList<QString> pendingPixmapKeys =
                m_pixmapKeysByPreviewKey.values(result.previewKey);
        for (const QString& pixmapKey : pendingPixmapKeys) {
            if (result.loadResult.preview) {
                scheduleRender(pixmapKey);
            } else {
                finishPendingPixmapRequest(pixmapKey, false);
            }
        }
        return;
    }

    m_loadingPixmapKeys.remove(result.pixmapKey);

    QPixmap pixmap = QPixmap::fromImage(result.image);
    if (!pixmap.isNull()) {
        QPixmapCache::insert(result.pixmapKey, pixmap);
    }
    finishPendingPixmapRequest(result.pixmapKey, !pixmap.isNull());
}

} // namespace rekordbox
} // namespace mixxx
