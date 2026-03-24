#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QString>
#include <optional>

namespace mixxx::rekordbox {

/// Imported cover-art metadata for a Rekordbox artwork entry.
struct ImportedCoverArtInfo {
    QByteArray cacheDigest;
    QString absoluteLocation;
};

/// Calculate a stable cache digest for an imported cover-art location.
inline QByteArray calculateCoverArtDigestFromLocation(
        const QString& absoluteLocation) {
    if (absoluteLocation.isEmpty()) {
        return {};
    }
    return QCryptographicHash::hash(
            absoluteLocation.toUtf8(),
            QCryptographicHash::Sha256);
}

/// Convert a Rekordbox-exported artwork path into the absolute file location
/// and cache digest used by Mixxx's generic cover-art pipeline.
inline std::optional<ImportedCoverArtInfo> importCoverArtInfo(
        const QString& deviceRootPath,
        const QString& exportedCoverPath) {
    if (exportedCoverPath.isEmpty()) {
        return std::nullopt;
    }

    const QString normalizedDeviceRoot =
            QDir::cleanPath(QDir::fromNativeSeparators(deviceRootPath));
    const QString normalizedExportedPath =
            QDir::fromNativeSeparators(exportedCoverPath);
    QString absoluteLocation;
    if (normalizedExportedPath.startsWith(QLatin1Char('/'))) {
        absoluteLocation = normalizedDeviceRoot + normalizedExportedPath;
    } else {
        absoluteLocation =
                normalizedDeviceRoot + QLatin1Char('/') + normalizedExportedPath;
    }
    absoluteLocation = QDir::cleanPath(absoluteLocation);
    if (absoluteLocation.isEmpty()) {
        return std::nullopt;
    }

    return ImportedCoverArtInfo{
            calculateCoverArtDigestFromLocation(absoluteLocation),
            absoluteLocation};
}

} // namespace mixxx::rekordbox
