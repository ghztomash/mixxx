#include "library/rekordbox/rekordboxoverviewdelegate.h"

#include <QAbstractProxyModel>
#include <QPainter>
#include <algorithm>

#include "library/rekordbox/rekordboxfeature.h"
#include "library/rekordbox/rekordboxoverviewcache.h"
#include "moc_rekordboxoverviewdelegate.cpp"

namespace mixxx {
namespace rekordbox {

namespace {

bool useSelectedBackground(const QStyleOptionViewItem& option) {
    return option.showDecorationSelected &&
            (option.state & QStyle::State_Selected);
}

} // anonymous namespace

OverviewDelegate::OverviewDelegate(
        QTableView* pTableView,
        RekordboxPlaylistModel* pTrackModel)
        : TableItemDelegate(pTableView),
          m_pTrackModel(pTrackModel),
          m_inhibitLazyLoading(false) {
    DEBUG_ASSERT(m_pTrackModel);

    connect(OverviewCache::instance(),
            &OverviewCache::overviewReady,
            this,
            &OverviewDelegate::slotOverviewReady);
}

void OverviewDelegate::emitOverviewRowsChanged(QSet<QString>&& analyzePaths) {
    if (analyzePaths.isEmpty()) {
        return;
    }

    QList<int> rows;
    for (const auto& analyzePath : analyzePaths) {
        const auto analyzePathRows = m_pTrackModel->getAnalyzePathRows(analyzePath);
        for (int row : analyzePathRows) {
            rows.append(row);
        }
    }
    if (rows.isEmpty()) {
        return;
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    emit overviewRowsChanged(std::move(rows));
}

void OverviewDelegate::slotInhibitLazyLoading(bool inhibitLazyLoading) {
    m_inhibitLazyLoading = inhibitLazyLoading;
    if (m_inhibitLazyLoading || m_cacheMissAnalyzePaths.isEmpty()) {
        return;
    }

    auto staleAnalyzePaths = std::move(m_cacheMissAnalyzePaths);
    DEBUG_ASSERT(m_cacheMissAnalyzePaths.isEmpty());
    emitOverviewRowsChanged(std::move(staleAnalyzePaths));
}

void OverviewDelegate::slotOverviewReady(
        const QObject* pRequester,
        const QString& analyzePath,
        bool pixmapValid) {
    if (pRequester == this && pixmapValid) {
        emitOverviewRowsChanged(QSet<QString>{analyzePath});
    }
}

void OverviewDelegate::paintItem(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const {
    if (option.rect.isEmpty() || m_pTableView->isColumnHidden(index.column())) {
        paintItemBackground(painter, option, index);
        if (option.state & QStyle::State_HasFocus) {
            drawBorder(painter, m_focusBorderColor, option.rect);
        }
        return;
    }

    QModelIndex sourceIndex = index;
    const auto* pModel = index.model();
    while (auto* pProxyModel = qobject_cast<const QAbstractProxyModel*>(pModel)) {
        sourceIndex = pProxyModel->mapToSource(sourceIndex);
        pModel = pProxyModel->sourceModel();
    }

    const QString analyzePath = m_pTrackModel->getAnalyzePath(sourceIndex);
    if (analyzePath.isEmpty()) {
        paintItemBackground(painter, option, index);
        if (option.state & QStyle::State_HasFocus) {
            drawBorder(painter, m_focusBorderColor, option.rect);
        }
        return;
    }

    const double scaleFactor = m_pTableView->devicePixelRatioF();
    QPixmap pixmap = OverviewCache::instance()->requestCachedOverview(
            analyzePath,
            this,
            option.rect.size() * scaleFactor);
    if (pixmap.isNull()) {
        if (m_inhibitLazyLoading) {
            m_cacheMissAnalyzePaths.insert(analyzePath);
        } else {
            pixmap = OverviewCache::instance()->requestUncachedOverview(
                    analyzePath,
                    this,
                    option.rect.size() * scaleFactor);
        }
    }

    paintItemBackground(painter, option, index);
    if (!useSelectedBackground(option)) {
        painter->fillRect(option.rect, Qt::black);
    }
    if (!pixmap.isNull()) {
        pixmap.setDevicePixelRatio(scaleFactor);
        painter->drawPixmap(option.rect, pixmap);
    }

    if (option.state & QStyle::State_HasFocus) {
        drawBorder(painter, m_focusBorderColor, option.rect);
    }
}

} // namespace rekordbox
} // namespace mixxx
