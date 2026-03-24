#include "library/rekordbox/rekordboxcoverartutils.h"

#include <gtest/gtest.h>

#include <QDir>

namespace {

TEST(RekordboxCoverArtUtilsTest, ImportCoverArtInfoFromExportedAbsolutePath) {
    const QString deviceRoot = QStringLiteral("device-root");
    const QString exportedCoverPath =
            QStringLiteral("/PIONEER/Artwork/00001/cover.jpg");

    const auto coverArtInfo =
            mixxx::rekordbox::importCoverArtInfo(deviceRoot, exportedCoverPath);

    ASSERT_TRUE(coverArtInfo.has_value());
    EXPECT_EQ(QDir::cleanPath(QStringLiteral("device-root/PIONEER/Artwork/00001/cover.jpg")),
            coverArtInfo->absoluteLocation);
    EXPECT_FALSE(coverArtInfo->cacheDigest.isEmpty());
    EXPECT_EQ(mixxx::rekordbox::calculateCoverArtDigestFromLocation(
                      coverArtInfo->absoluteLocation),
            coverArtInfo->cacheDigest);
}

TEST(RekordboxCoverArtUtilsTest, ImportCoverArtInfoFromRelativePath) {
    const auto coverArtInfo = mixxx::rekordbox::importCoverArtInfo(
            QStringLiteral("device-root/"),
            QStringLiteral("Artwork/cover.jpg"));

    ASSERT_TRUE(coverArtInfo.has_value());
    EXPECT_EQ(QDir::cleanPath(QStringLiteral("device-root/Artwork/cover.jpg")),
            coverArtInfo->absoluteLocation);
}

TEST(RekordboxCoverArtUtilsTest, EmptyExportedCoverPathReturnsNoCoverArt) {
    const auto coverArtInfo = mixxx::rekordbox::importCoverArtInfo(
            QStringLiteral("device-root"),
            QString());

    EXPECT_FALSE(coverArtInfo.has_value());
}

TEST(RekordboxCoverArtUtilsTest, CoverArtDigestDependsOnNormalizedLocation) {
    const QByteArray digest = mixxx::rekordbox::calculateCoverArtDigestFromLocation(
            QDir::cleanPath(QStringLiteral("device-root/Artwork/../Artwork/cover.jpg")));

    EXPECT_FALSE(digest.isEmpty());
    EXPECT_EQ(digest,
            mixxx::rekordbox::calculateCoverArtDigestFromLocation(
                    QStringLiteral("device-root/Artwork/cover.jpg")));
    EXPECT_NE(digest,
            mixxx::rekordbox::calculateCoverArtDigestFromLocation(
                    QStringLiteral("device-root/Artwork/other-cover.jpg")));
}

} // namespace
