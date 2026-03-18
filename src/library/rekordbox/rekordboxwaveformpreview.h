#pragma once

#include <QColor>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>

namespace mixxx {
namespace rekordbox {

/// Identifies the preview data variant extracted from Rekordbox ANLZ files.
enum class WaveformPreviewStyle {
    Monochrome,
    Color,
};

/// A single vertical column of a Rekordbox overview preview.
struct WaveformPreviewColumn {
    int backHeight = 0;
    int frontHeight = 0;
    QRgb backColor = 0;
    QRgb frontColor = 0;
};

/// Waveform preview data extracted from Rekordbox ANLZ files.
struct WaveformPreview {
    WaveformPreviewStyle style = WaveformPreviewStyle::Monochrome;
    int maxHeight = 0;
    QVector<WaveformPreviewColumn> columns;

    bool isEmpty() const {
        return columns.isEmpty() || maxHeight <= 0;
    }
};

/// Describes why preview extraction failed.
enum class WaveformPreviewLoadError {
    None,
    MissingFile,
    ParseFailure,
    UnsupportedData,
};

/// The result of extracting preview data from Rekordbox ANLZ files.
struct WaveformPreviewLoadResult {
    std::optional<WaveformPreview> preview;
    WaveformPreviewLoadError error = WaveformPreviewLoadError::None;
    QString errorPath;
};

/// Load preview-quality waveform data from Rekordbox ANLZ files.
WaveformPreviewLoadResult loadWaveformPreview(const QString& analyzePath);

} // namespace rekordbox
} // namespace mixxx
