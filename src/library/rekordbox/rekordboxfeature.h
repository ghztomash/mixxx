// This feature reads tracks, playlists and folders from removable Recordbox
// prepared devices (USB drives, etc), by parsing the binary *.PDB files
// stored on each removable device. It does not read the locally stored
// Rekordbox database (Collection).

// It draws heavily from the hard work completed here:

//      https://github.com/Deep-Symmetry/crate-digger

// And uses the C++ Kaitai Struct binary parsing libraries:

//      http://kaitai.io
//      https://github.com/kaitai-io/kaitai_struct
//      https://github.com/kaitai-io/kaitai_struct_cpp_stl_runtime

// The *.PDB C++ files:

//      rekordbox_pdb.h
//      rekordbox_pdb.cpp

// Were generated from the following structure definition file:

//      https://github.com/Deep-Symmetry/crate-digger/blob/master/src/main/kaitai/rekordbox_pdb.ksy

#pragma once

#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QStringListModel>
#include <QVector>
#include <QtConcurrentRun>
#include <fstream>

#include "library/baseexternallibraryfeature.h"
#include "library/baseexternalplaylistmodel.h"
#include "library/baseexternaltrackmodel.h"
#include "library/treeitemmodel.h"
#include "track/trackid.h"
#include "util/parented_ptr.h"

class TrackCollectionManager;
class BaseExternalPlaylistModel;
class QAbstractItemDelegate;
class WTrackTableView;

class RekordboxPlaylistModel : public BaseExternalPlaylistModel {
    Q_OBJECT
  public:
    RekordboxPlaylistModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            QSharedPointer<BaseTrackCache> trackSource);
    /// Return the Rekordbox ANLZ path for a table row.
    QString getAnalyzePath(const QModelIndex& index) const;
    /// Return all visible rows that reference the same Rekordbox ANLZ path.
    QVector<int> getAnalyzePathRows(const QString& analyzePath) const;
    TrackPointer getTrack(const QModelIndex& index) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    CoverInfo getCoverInfo(const QModelIndex& index) const override;
    bool isColumnHiddenByDefault(int column) override;
    bool isColumnInternal(int column) override;
    void select() override;

  protected:
    QAbstractItemDelegate* delegateForSpecialColumn(
            int column,
            WTrackTableView* pTableView) override;
    void initSortColumnMapping() override;

  private:
    void rebuildAnalyzePathRowIndex();

    QHash<QString, QVector<int>> m_rowsByAnalyzePath;
};

class RekordboxFeature : public BaseExternalLibraryFeature {
    Q_OBJECT
  public:
    RekordboxFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~RekordboxFeature() override;

    QVariant title() override;
    static bool isSupported();
    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;

    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void refreshLibraryModels();
    void onRekordboxDevicesFound();
    void onTracksFound();

  private slots:
    void htmlLinkClicked(const QUrl& link);

  private:
    QSet<TrackId> getAllRekordboxTrackIds() const;
    QSet<TrackId> getRekordboxTrackIdsForLocations(const QSet<QString>& locations) const;
    QSet<QString> getRekordboxLocations(const QSet<QString>& locations) const;
    bool hasLoadedRekordboxTracks() const;
    QSet<TrackId> syncPlayedStateForAllTracks();
    QSet<TrackId> syncPlayedStateForTracks(const QSet<TrackId>& trackIds);
    QSet<TrackId> resetPlayedStateForLocations(const QSet<QString>& locations);
    void refreshPlayedState(const QSet<TrackId>& rekordboxTrackIds);
    void refreshMatchedTrackLocations();
    void updatePlayedStateSyncConnections();

    void slotTracksAddedOrChanged(const QSet<TrackId>& trackIds);
    void slotTracksRemoved(const QSet<TrackId>& trackIds);
    void slotCurrentPlayingTrackChanged(const TrackPointer& pTrack);

    QString formatRootViewHtml() const;
    std::unique_ptr<BaseSqlTableModel> createPlaylistModelForPlaylist(
            const QVariant& data) override;

    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<RekordboxPlaylistModel> m_pRekordboxPlaylistModel;

    QFutureWatcher<QList<TreeItem*>> m_devicesFutureWatcher;
    QFuture<QList<TreeItem*>> m_devicesFuture;
    QFutureWatcher<QString> m_tracksFutureWatcher;
    QFuture<QString> m_tracksFuture;
    QString m_title;

    QSharedPointer<BaseTrackCache> m_trackSource;
    bool m_playedStateSyncConnected{false};
    QHash<TrackId, QString> m_rekordboxLocationByLibraryTrackId;
};
