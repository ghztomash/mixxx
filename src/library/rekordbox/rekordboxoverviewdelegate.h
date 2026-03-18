#pragma once

#include <QSet>

#include "library/tabledelegates/tableitemdelegate.h"

class QTableView;

class RekordboxPlaylistModel;

namespace mixxx {
namespace rekordbox {

/// Paints Rekordbox playlist overview cells directly from ANLZ preview data.
class OverviewDelegate : public TableItemDelegate {
    Q_OBJECT

  public:
    /// Create a delegate for a Rekordbox playlist table view.
    explicit OverviewDelegate(
            QTableView* pParent,
            RekordboxPlaylistModel* pTrackModel);
    ~OverviewDelegate() override = default;

    /// Paint the overview pixmap for a Rekordbox playlist row.
    void paintItem(
            QPainter* painter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;

  signals:
    /// Request repaints for rows whose cached overview became available.
    void overviewRowsChanged(const QList<int>& rows);

  public slots:
    /// Temporarily suppress uncached background loading while the view scrolls.
    void slotInhibitLazyLoading(bool inhibitLazyLoading);

  private slots:
    void slotOverviewReady(
            const QObject* pRequester,
            const QString& analyzePath,
            bool pixmapValid);

  private:
    void emitOverviewRowsChanged(QSet<QString>&& analyzePaths);

    RekordboxPlaylistModel* const m_pTrackModel;
    bool m_inhibitLazyLoading;

    mutable QSet<QString> m_cacheMissAnalyzePaths;
};

} // namespace rekordbox
} // namespace mixxx
