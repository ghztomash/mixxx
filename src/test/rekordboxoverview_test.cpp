#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <array>
#include <atomic>

#include "library/rekordbox/rekordboxoverviewcache.h"
#include "library/rekordbox/rekordboxoverviewrenderer.h"
#include "library/rekordbox/rekordboxwaveformpreview.h"
#include "mixxxtest.h"

namespace {

constexpr quint32 kWavePreviewTag = 0x50574156;      // PWAV
constexpr quint32 kWaveColorPreviewTag = 0x50575634; // PWV4

void appendU32Be(QByteArray* pData, quint32 value) {
    pData->append(static_cast<char>((value >> 24) & 0xff));
    pData->append(static_cast<char>((value >> 16) & 0xff));
    pData->append(static_cast<char>((value >> 8) & 0xff));
    pData->append(static_cast<char>(value & 0xff));
}

QByteArray wrapSection(quint32 fourcc, const QByteArray& body) {
    QByteArray section;
    appendU32Be(&section, fourcc);
    appendU32Be(&section, 12);
    appendU32Be(&section, 12 + body.size());
    section.append(body);
    return section;
}

QByteArray wavePreviewBody(std::initializer_list<quint8> bytes) {
    QByteArray body;
    appendU32Be(&body, bytes.size());
    appendU32Be(&body, 0);
    for (quint8 byte : bytes) {
        body.append(static_cast<char>(byte));
    }
    return body;
}

QByteArray waveColorPreviewBody(
        std::initializer_list<std::array<quint8, 6>> entries) {
    QByteArray body;
    appendU32Be(&body, 6);
    appendU32Be(&body, entries.size());
    appendU32Be(&body, 0);
    for (const auto& entry : entries) {
        for (quint8 byte : entry) {
            body.append(static_cast<char>(byte));
        }
    }
    return body;
}

QByteArray anlzFile(std::initializer_list<QByteArray> sections) {
    QByteArray data("PMAI", 4);
    int totalSize = 12;
    for (const QByteArray& section : sections) {
        totalSize += section.size();
    }
    appendU32Be(&data, 12);
    appendU32Be(&data, totalSize);
    for (const QByteArray& section : sections) {
        data.append(section);
    }
    return data;
}

bool writeBinaryFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(data) == data.size();
}

int countOpaquePixels(const QImage& image) {
    int opaquePixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                ++opaquePixels;
            }
        }
    }
    return opaquePixels;
}

class RekordboxOverviewTest : public MixxxTest {
};

TEST_F(RekordboxOverviewTest, LoadWaveformPreviewPrefersColorPreview) {
    const QString dirPath = getTestDataDir().filePath(QStringLiteral("rekordboxoverview"));
    ASSERT_TRUE(QDir().mkpath(dirPath));

    const QString analyzePath = dirPath + QStringLiteral("/ANLZ0000.DAT");
    const QString extPath = dirPath + QStringLiteral("/ANLZ0000.EXT");

    ASSERT_TRUE(writeBinaryFile(
            analyzePath,
            anlzFile({
                    wrapSection(
                            kWavePreviewTag,
                            wavePreviewBody({0x1f})),
            })));
    ASSERT_TRUE(writeBinaryFile(
            extPath,
            anlzFile({
                    wrapSection(
                            kWaveColorPreviewTag,
                            waveColorPreviewBody({
                                    std::array<quint8, 6>{0, 127, 10, 20, 30, 40},
                            })),
            })));

    const auto loadResult = mixxx::rekordbox::loadWaveformPreview(analyzePath);
    ASSERT_TRUE(loadResult.preview.has_value());
    EXPECT_EQ(mixxx::rekordbox::WaveformPreviewStyle::Color, loadResult.preview->style);
    ASSERT_EQ(1, loadResult.preview->columns.size());
    EXPECT_EQ(40, loadResult.preview->columns[0].backHeight);
    EXPECT_EQ(40, loadResult.preview->columns[0].frontHeight);
    EXPECT_EQ(qRgb(95, 143, 191), loadResult.preview->columns[0].backColor);
    EXPECT_EQ(qRgb(127, 191, 255), loadResult.preview->columns[0].frontColor);
}

TEST_F(RekordboxOverviewTest, LoadWaveformPreviewUsesBeatLinkColorTransform) {
    const QString dirPath = getTestDataDir().filePath(QStringLiteral("rekordboxoverview"));
    ASSERT_TRUE(QDir().mkpath(dirPath));

    const QString analyzePath = dirPath + QStringLiteral("/ANLZ0001.EXT");
    ASSERT_TRUE(writeBinaryFile(
            analyzePath,
            anlzFile({
                    wrapSection(
                            kWaveColorPreviewTag,
                            waveColorPreviewBody({
                                    std::array<quint8, 6>{200, 180, 120, 60, 30, 15},
                            })),
            })));

    const auto loadResult = mixxx::rekordbox::loadWaveformPreview(analyzePath);
    ASSERT_TRUE(loadResult.preview.has_value());
    ASSERT_EQ(1, loadResult.preview->columns.size());

    const auto& column = loadResult.preview->columns[0];
    EXPECT_EQ(60, column.backHeight);
    EXPECT_EQ(15, column.frontHeight);
    EXPECT_EQ(qRgb(191, 95, 47), column.backColor);
    EXPECT_EQ(qRgb(255, 127, 63), column.frontColor);
    EXPECT_EQ(60, loadResult.preview->maxHeight);
}

TEST_F(RekordboxOverviewTest, RenderOverviewProducesVisibleImage) {
    mixxx::rekordbox::WaveformPreview preview;
    preview.style = mixxx::rekordbox::WaveformPreviewStyle::Monochrome;
    preview.maxHeight = 31;
    preview.columns = {
            {0, 8, 0, qRgb(0x40, 0x80, 0xff)},
            {0, 16, 0, qRgb(0xff, 0xff, 0xff)},
            {0, 24, 0, qRgb(0x20, 0x60, 0xff)},
    };

    const QImage image =
            mixxx::rekordbox::renderOverview(preview, QSize(12, 6));
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(QSize(12, 6), image.size());
    EXPECT_GT(countOpaquePixels(image), 0);
    EXPECT_LT(countOpaquePixels(image), image.width() * image.height());
}

TEST_F(RekordboxOverviewTest, OverviewCacheCoalescesDuplicateRequests) {
    std::atomic_int loadCount{0};
    mixxx::rekordbox::OverviewCache cache(
            nullptr,
            [&loadCount](const QString& analyzePath) {
                ++loadCount;
                mixxx::rekordbox::WaveformPreviewLoadResult result;
                mixxx::rekordbox::WaveformPreview preview;
                preview.style = mixxx::rekordbox::WaveformPreviewStyle::Monochrome;
                preview.maxHeight = 31;
                preview.columns = {
                        {0, 8, 0, qRgb(0x40, 0x80, 0xff)},
                        {0, 16, 0, qRgb(0xff, 0xff, 0xff)},
                };
                result.preview = preview;
                result.errorPath = analyzePath;
                return result;
            });

    QSignalSpy readySpy(
            &cache,
            &mixxx::rekordbox::OverviewCache::overviewReady);
    ASSERT_TRUE(readySpy.isValid());

    QObject requester1;
    QObject requester2;
    const QString analyzePath =
            getTestDataDir().filePath(QStringLiteral("cache-coalesce.dat"));
    cache.requestUncachedOverview(
            analyzePath,
            &requester1,
            QSize(24, 6));
    cache.requestUncachedOverview(
            analyzePath,
            &requester2,
            QSize(24, 6));

    QElapsedTimer timeout;
    timeout.start();
    while (readySpy.count() < 2 && timeout.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    EXPECT_EQ(1, loadCount.load());
    EXPECT_EQ(2, readySpy.count());
}

TEST_F(RekordboxOverviewTest, OverviewCacheReusesPreviewAcrossSizes) {
    std::atomic_int loadCount{0};
    mixxx::rekordbox::OverviewCache cache(
            nullptr,
            [&loadCount](const QString& analyzePath) {
                ++loadCount;
                mixxx::rekordbox::WaveformPreviewLoadResult result;
                mixxx::rekordbox::WaveformPreview preview;
                preview.style = mixxx::rekordbox::WaveformPreviewStyle::Color;
                preview.maxHeight = 60;
                preview.columns = {
                        {60, 15, qRgb(191, 95, 47), qRgb(255, 127, 63)},
                        {40, 10, qRgb(95, 143, 191), qRgb(127, 191, 255)},
                };
                result.preview = preview;
                result.errorPath = analyzePath;
                return result;
            });

    QSignalSpy readySpy(
            &cache,
            &mixxx::rekordbox::OverviewCache::overviewReady);
    ASSERT_TRUE(readySpy.isValid());

    QObject requester1;
    QObject requester2;
    const QString analyzePath =
            getTestDataDir().filePath(QStringLiteral("cache-two-sizes.dat"));

    cache.requestUncachedOverview(
            analyzePath,
            &requester1,
            QSize(24, 6));
    cache.requestUncachedOverview(
            analyzePath,
            &requester2,
            QSize(48, 6));

    QElapsedTimer timeout;
    timeout.start();
    while (readySpy.count() < 2 && timeout.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    EXPECT_EQ(1, loadCount.load());
    EXPECT_EQ(2, readySpy.count());
}

TEST_F(RekordboxOverviewTest, OverviewCacheReloadsWhenAnalyzeFileChanges) {
    std::atomic_int loadCount{0};
    mixxx::rekordbox::OverviewCache cache(
            nullptr,
            [&loadCount](const QString& analyzePath) {
                ++loadCount;
                mixxx::rekordbox::WaveformPreviewLoadResult result;
                result.errorPath = analyzePath;
                if (loadCount.load() == 1) {
                    result.error = mixxx::rekordbox::WaveformPreviewLoadError::UnsupportedData;
                    return result;
                }

                mixxx::rekordbox::WaveformPreview preview;
                preview.style = mixxx::rekordbox::WaveformPreviewStyle::Monochrome;
                preview.maxHeight = 31;
                preview.columns = {
                        {0, 8, 0, qRgb(0x40, 0x80, 0xff)},
                        {0, 16, 0, qRgb(0xff, 0xff, 0xff)},
                };
                result.preview = preview;
                return result;
            });

    QSignalSpy readySpy(
            &cache,
            &mixxx::rekordbox::OverviewCache::overviewReady);
    ASSERT_TRUE(readySpy.isValid());

    QObject requester1;
    QObject requester2;
    const QString analyzePath =
            getTestDataDir().filePath(QStringLiteral("cache-reload.dat"));
    ASSERT_TRUE(writeBinaryFile(analyzePath, QByteArray("first")));

    cache.requestUncachedOverview(
            analyzePath,
            &requester1,
            QSize(24, 6));

    QElapsedTimer timeout;
    timeout.start();
    while (readySpy.count() < 1 && timeout.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    EXPECT_EQ(1, loadCount.load());
    ASSERT_EQ(1, readySpy.count());
    EXPECT_FALSE(readySpy.takeFirst().at(2).toBool());

    ASSERT_TRUE(writeBinaryFile(analyzePath, QByteArray("second-version")));

    cache.requestUncachedOverview(
            analyzePath,
            &requester2,
            QSize(24, 6));

    timeout.restart();
    while (readySpy.count() < 1 && timeout.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    EXPECT_EQ(2, loadCount.load());
    ASSERT_EQ(1, readySpy.count());
    EXPECT_TRUE(readySpy.takeFirst().at(2).toBool());
}

} // anonymous namespace
