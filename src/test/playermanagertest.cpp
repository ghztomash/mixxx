#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryFile>
#include <QTest>
#include <gsl/pointers>

#include "control/controlindicatortimer.h"
#include "database/mixxxdb.h"
#include "effects/effectsmanager.h"
#include "engine/channels/enginedeck.h"
#include "engine/enginebuffer.h"
#include "engine/enginemixer.h"
#include "library/coverartcache.h"
#include "library/library.h"
#include "library/librarytablemodel.h"
#include "library/parsercsv.h"
#include "library/trackcollectionmanager.h"
#include "mixer/basetrackplayer.h"
#include "mixer/deck.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "sources/soundsourceproxy.h"
#include "test/mixxxdbtest.h"
#include "test/soundsourceproviderregistration.h"
#include "track/track.h"
#include "util/cmdlineargs.h"
#ifdef __RUBBERBAND__
#include "engine/bufferscalers/rubberbandworkerpool.h"
#endif

namespace {

const QString kTrackLocationTest1 = QStringLiteral("id3-test-data/cover-test-png.mp3");
const QString kTrackLocationTest2 = QStringLiteral("id3-test-data/cover-test-vbr.mp3");

void deleteTrack(Track* pTrack) {
    // Delete track objects directly in unit tests with
    // no main event loop
    delete pTrack;
};

void waitForTrackToBeLoaded(Deck* pDeck) {
    while (!pDeck->getEngineDeck()->getEngineBuffer()->isTrackLoaded()) {
        QTest::qSleep(100); // millis
    }
}

bool waitForTrackToBeLoadedWithTimeout(
        Deck* pDeck,
        int timeoutMillis = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!pDeck->getEngineDeck()->getEngineBuffer()->isTrackLoaded()) {
        if (timer.elapsed() >= timeoutMillis) {
            return false;
        }
        QCoreApplication::processEvents();
        QTest::qSleep(100); // millis
    }
    return true;
}

QString deckMarkerForTrack(
        LibraryTableModel* pModel,
        TrackId trackId,
        int decksColumn) {
    const auto rows = pModel->getTrackRows(trackId);
    if (rows.isEmpty()) {
        return {};
    }
    return pModel->data(pModel->index(rows.first(), decksColumn)).toString();
}

QString deckToolTipForTrack(
        LibraryTableModel* pModel,
        TrackId trackId,
        int decksColumn) {
    const auto rows = pModel->getTrackRows(trackId);
    if (rows.isEmpty()) {
        return {};
    }
    return pModel->data(pModel->index(rows.first(), decksColumn), Qt::ToolTipRole).toString();
}

QColor trackColorForTrack(
        LibraryTableModel* pModel,
        TrackId trackId,
        int column) {
    const auto rows = pModel->getTrackRows(trackId);
    if (rows.isEmpty()) {
        return QColor();
    }
    return pModel->data(pModel->index(rows.first(), column), Qt::ForegroundRole)
            .value<QColor>();
}

bool waitForDeckMarker(
        LibraryTableModel* pModel,
        TrackId trackId,
        int decksColumn,
        const QString& expected) {
    for (int i = 0; i < 50; ++i) {
        QCoreApplication::processEvents();
        if (deckMarkerForTrack(pModel, trackId, decksColumn) == expected) {
            return true;
        }
        QTest::qSleep(20);
    }
    return false;
}

bool waitForTrackRow(
        LibraryTableModel* pModel,
        TrackId trackId) {
    for (int i = 0; i < 50; ++i) {
        QCoreApplication::processEvents();
        if (!pModel->getTrackRows(trackId).isEmpty()) {
            return true;
        }
        QTest::qSleep(20);
    }
    return false;
}

} // namespace

// We can't inherit from LibraryTest because that creates a key_notation control object that is also
// created by the Library object itself. The duplicated CO creation causes a debug assert.
class PlayerManagerTest : public MixxxDbTest, SoundSourceProviderRegistration {
  public:
    PlayerManagerTest()
            : MixxxDbTest(true) {
    }

    void SetUp() override {
        // This setup mirrors coreservices -- it would be nice if we could use coreservices instead
        // but it does a lot of local disk / settings setup.
        auto pChannelHandleFactory = std::make_shared<ChannelHandleFactory>();
        m_pEffectsManager = std::make_shared<EffectsManager>(m_pConfig, pChannelHandleFactory);
        m_pEngine = std::make_shared<EngineMixer>(
                m_pConfig,
                "[Master]",
                m_pEffectsManager.get(),
                pChannelHandleFactory,
                true);
        m_pSoundManager = std::make_shared<SoundManager>(m_pConfig, m_pEngine.get());
        m_pControlIndicatorTimer = std::make_shared<mixxx::ControlIndicatorTimer>(nullptr);
        m_pEngine->registerNonEngineChannelSoundIO(gsl::make_not_null(m_pSoundManager.get()));

        CoverArtCache::createInstance();

        m_pPlayerManager = std::make_shared<PlayerManager>(m_pConfig,
                m_pSoundManager.get(),
                m_pEffectsManager.get(),
                m_pEngine.get());

        m_pPlayerManager->addConfiguredDecks();
        m_pPlayerManager->addSampler();
        PlayerInfo::create();
        m_pEffectsManager->setup();

        const auto dbConnection = mixxx::DbConnectionPooled(dbConnectionPooler());
        if (!MixxxDb::initDatabaseSchema(dbConnection)) {
            exit(1);
        }
        m_pTrackCollectionManager = std::make_unique<TrackCollectionManager>(
                nullptr,
                m_pConfig,
                dbConnectionPooler(),
                deleteTrack);

        m_pRecordingManager = std::make_shared<RecordingManager>(m_pConfig, m_pEngine.get());
        m_pLibrary = std::make_shared<Library>(
                nullptr,
                m_pConfig,
                dbConnectionPooler(),
                m_pTrackCollectionManager.get(),
                m_pPlayerManager.get(),
                m_pRecordingManager.get());

        m_pPlayerManager->bindToLibrary(m_pLibrary.get());
        RubberBandWorkerPool::createInstance();
    }

    void TearDown() override {
        CoverArtCache::destroy();
#ifdef __RUBBERBAND__
        RubberBandWorkerPool::destroy();
#endif
    }

    ~PlayerManagerTest() {
        m_pSoundManager.reset();
        m_pPlayerManager.reset();
        PlayerInfo::destroy();
        m_pLibrary.reset();
        m_pRecordingManager.reset();
        m_pEngine.reset();
        m_pEffectsManager.reset();
        m_pTrackCollectionManager.reset();
        m_pControlIndicatorTimer.reset();
    }

  protected:
    TrackPointer getOrAddTrackByLocation(
            const QString& trackLocation) const {
        return m_pTrackCollectionManager->getOrAddTrack(
                TrackRef::fromFilePath(trackLocation));
    }

    std::shared_ptr<EffectsManager> m_pEffectsManager;
    std::shared_ptr<mixxx::ControlIndicatorTimer> m_pControlIndicatorTimer;
    std::shared_ptr<EngineMixer> m_pEngine;
    std::shared_ptr<SoundManager> m_pSoundManager;
    std::shared_ptr<PlayerManager> m_pPlayerManager;
    std::unique_ptr<TrackCollectionManager> m_pTrackCollectionManager;
    std::shared_ptr<RecordingManager> m_pRecordingManager;
    std::shared_ptr<Library> m_pLibrary;
};

TEST_F(PlayerManagerTest, UnEjectTest) {
    // Ejecting an empty deck with no previously-recorded ejected track has no effect.
    auto deck1 = m_pPlayerManager->getDeck(0);
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());

    // Load a track and eject it
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    // make sure eject does not trigger 'unreplace':
    // sleep for longer than 500 ms 'unreplace' period so this is not registered as double-click
    QTest::qSleep(kUnreplaceDelay); // millis
    deck1->slotEjectTrack(1.0);

    // Load another track.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);

    // Ejecting in an empty deck loads the last-ejected track.
    auto deck2 = m_pPlayerManager->getDeck(1);
    ASSERT_EQ(nullptr, deck2->getLoadedTrack());
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay); // millis
    deck2->slotEjectTrack(2.0);
    ASSERT_NE(nullptr, deck2->getLoadedTrack());
    ASSERT_EQ(testId1, deck2->getLoadedTrack()->getId());
}

// Loading a new track in a deck causes the old one to be ejected.
// That old track can be unejected into a different deck.
TEST_F(PlayerManagerTest, UnEjectReplaceTrackTest) {
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Load a track and the load another one
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);

    // Load another track, replacing the first, causing it to be unloaded.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);

    // Ejecting in an empty deck loads the last-ejected track.
    auto deck2 = m_pPlayerManager->getDeck(1);
    ASSERT_EQ(nullptr, deck2->getLoadedTrack());
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay);
    deck2->slotEjectTrack(1.0);
    ASSERT_NE(nullptr, deck2->getLoadedTrack());
    ASSERT_EQ(testId1, deck2->getLoadedTrack()->getId());
}

TEST_F(PlayerManagerTest, UnEjectInvalidTrackIdTest) {
    // Save an invalid trackid in playermanager.
    auto pTrack = Track::newDummy(
            getTestDir().filePath(kTrackLocationTest1), TrackId(QVariant(10)));
    ASSERT_NE(nullptr, pTrack);
    m_pPlayerManager->slotSaveEjectedTrack(pTrack);
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Does nothing -- no crash.
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay);
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());
}

TEST_F(PlayerManagerTest, UnReplaceTest) {
    // Trigger eject twice within 500 ms to undo track replacement
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Load a track
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    // Load another track.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    // Eject. Make sure eject does not trigger 'unreplace':
    // sleep for longer than 500 ms 'unreplace' period so this is not registered as double-click
    QTest::qSleep(kUnreplaceDelay); // millis
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());

    // Eject again, assume this is reached faster than 500 ms after first eject
    deck1->slotEjectTrack(1.0);
    // First track should be reloaded
    ASSERT_NE(nullptr, deck1->getLoadedTrack());
    ASSERT_EQ(testId1, deck1->getLoadedTrack()->getId());
}

TEST_F(PlayerManagerTest, DecksColumnReflectsLoadedDecks) {
    auto* pModel = m_pLibrary->trackTableModel();
    ASSERT_NE(nullptr, pModel);

    TrackPointer pTrack = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack);
    const TrackId trackId = pTrack->getId();
    ASSERT_TRUE(trackId.isValid());

    pModel->select();
    const int titleColumn = pModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TITLE);
    const int decksColumn = pModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_LOADED_DECK);
    ASSERT_GE(titleColumn, 0);
    ASSERT_GE(decksColumn, 0);
    EXPECT_EQ(TrackModel::SortColumnId::Invalid,
            pModel->sortColumnIdFromColumnIndex(decksColumn));
    ASSERT_FALSE(pModel->getTrackRows(trackId).isEmpty());
    EXPECT_EQ(QString(), deckMarkerForTrack(pModel, trackId, decksColumn));
    EXPECT_FALSE(trackColorForTrack(pModel, trackId, titleColumn).isValid());

    auto* pDeck1 = m_pPlayerManager->getDeck(0);
    ASSERT_NE(nullptr, pDeck1);
    pDeck1->slotLoadTrack(pTrack,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    ASSERT_TRUE(waitForTrackToBeLoadedWithTimeout(pDeck1));

    // Rebuilding the model should preserve the loaded deck marker.
    pModel->select();
    EXPECT_TRUE(waitForDeckMarker(pModel, trackId, decksColumn, QStringLiteral("1")));
    EXPECT_EQ(QColor(QStringLiteral("#518f00")),
            trackColorForTrack(pModel, trackId, titleColumn));

    auto* pDeck2 = m_pPlayerManager->getDeck(1);
    ASSERT_NE(nullptr, pDeck2);
    pDeck2->slotLoadTrack(pTrack,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    ASSERT_TRUE(waitForTrackToBeLoadedWithTimeout(pDeck2));

    EXPECT_TRUE(waitForDeckMarker(pModel, trackId, decksColumn, QStringLiteral("1,2")));
    EXPECT_EQ(QStringLiteral("Deck 1, Deck 2"),
            deckToolTipForTrack(pModel, trackId, decksColumn));

    pDeck1->slotEjectTrack(1.0);
    EXPECT_TRUE(waitForDeckMarker(pModel, trackId, decksColumn, QStringLiteral("2")));

    pDeck2->slotEjectTrack(1.0);
    EXPECT_TRUE(waitForTrackRow(pModel, trackId));
    EXPECT_TRUE(waitForDeckMarker(pModel, trackId, decksColumn, QString()));
    EXPECT_FALSE(trackColorForTrack(pModel, trackId, titleColumn).isValid());
}

TEST_F(PlayerManagerTest, DecksColumnIsOmittedFromCsvExport) {
    auto* pModel = m_pLibrary->trackTableModel();
    ASSERT_NE(nullptr, pModel);
    pModel->select();

    const int decksColumn = pModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_LOADED_DECK);
    ASSERT_GE(decksColumn, 0);

    QTemporaryFile csvFile;
    ASSERT_TRUE(csvFile.open());
    const QString csvPath = csvFile.fileName();
    csvFile.close();

    ASSERT_TRUE(ParserCsv::writeCSVFile(csvPath, pModel, false));

    QFile file(csvPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString headerLine = QString::fromUtf8(file.readLine());

    QString decksHeader = pModel->headerData(decksColumn, Qt::Horizontal, Qt::DisplayRole).toString();
    decksHeader.replace('"', QStringLiteral("\"\""));
    EXPECT_FALSE(headerLine.contains(QStringLiteral("\"%1\"").arg(decksHeader)));
}
