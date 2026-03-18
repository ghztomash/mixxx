#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThreadPool>
#include <functional>

#include "library/rekordbox/rekordboxwaveformpreview.h"

class QObject;

namespace mixxx {
namespace rekordbox {

/// Caches Rekordbox playlist overview pixmaps and decoded ANLZ previews.
class OverviewCache : public QObject {
    Q_OBJECT

  public:
    /// Identifies the asynchronous stage that produced a cache result.
    enum class JobKind {
        LoadPreview,
        RenderPixmap,
    };

    /// Describes the result of one asynchronous preview-load or pixmap-render job.
    struct FutureResult {
        JobKind jobKind = JobKind::LoadPreview;
        QString analyzePath;
        QString previewKey;
        QString pixmapKey;
        QImage image;
        QSize resizedToSize;
        WaveformPreviewLoadResult loadResult;
    };

    /// Loads and decodes one Rekordbox ANLZ preview from disk.
    using LoadPreviewFn =
            std::function<WaveformPreviewLoadResult(const QString& analyzePath)>;

    explicit OverviewCache(
            QObject* pParent = nullptr,
            LoadPreviewFn loadPreviewFn = LoadPreviewFn());
    ~OverviewCache() override = default;

    /// Return the shared cache used by Rekordbox playlist overview delegates.
    static OverviewCache* instance();

    /// Request an overview image without scheduling any new background work.
    QPixmap requestCachedOverview(
            const QString& analyzePath,
            const QObject* pRequester,
            QSize desiredSize);
    /// Request an overview image and schedule background rendering on cache miss.
    QPixmap requestUncachedOverview(
            const QString& analyzePath,
            const QObject* pRequester,
            QSize desiredSize);

  signals:
    /// Emitted when a requested overview image has been prepared.
    void overviewReady(
            const QObject* pRequester,
            const QString& analyzePath,
            bool pixmapValid);

  private slots:
    void overviewPrepared();

  private:
    struct PendingPixmapRequest {
        QString analyzePath;
        QString previewKey;
        QSize desiredSize;
        QSet<const QObject*> requesters;
    };

    struct PreviewIdentity {
        QString analyzePath;
        QString previewKey;
    };

    static PreviewIdentity currentPreviewIdentity(const QString& analyzePath);
    QString pixmapCacheKey(const QString& previewKey, QSize desiredSize) const;
    void schedulePreviewLoad(const PreviewIdentity& previewIdentity);
    void scheduleRender(const QString& pixmapKey);
    void finishPendingPixmapRequest(const QString& pixmapKey, bool pixmapValid);

    LoadPreviewFn m_loadPreviewFn;
    QThreadPool m_threadPool;
    QHash<QString, WaveformPreviewLoadResult> m_previewCache;
    QSet<QString> m_loadingPreviewKeys;
    QSet<QString> m_loadingPixmapKeys;
    QHash<QString, PendingPixmapRequest> m_pendingRequestsByPixmapKey;
    QMultiHash<QString, QString> m_pixmapKeysByPreviewKey;
    QSet<QString> m_loggedParseFailures;
};

} // namespace rekordbox
} // namespace mixxx
