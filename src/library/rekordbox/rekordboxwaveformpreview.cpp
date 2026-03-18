#include "library/rekordbox/rekordboxwaveformpreview.h"

#include <rekordbox_anlz.h>

#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace mixxx {
namespace rekordbox {

namespace {

constexpr int kMonochromeHeightMask = 0x1f;
constexpr int kMonochromeWhitenessShift = 5;
constexpr int kMonochromeWhitenessMax = 0x07;

QString siblingAnalyzePath(const QString& analyzePath, const QString& extension) {
    if (analyzePath.size() < 3) {
        return QString();
    }

    QString sibling = analyzePath;
    sibling.chop(3);
    sibling.append(extension);
    return sibling;
}

int errorPriority(WaveformPreviewLoadError error) {
    switch (error) {
    case WaveformPreviewLoadError::ParseFailure:
        return 3;
    case WaveformPreviewLoadError::UnsupportedData:
        return 2;
    case WaveformPreviewLoadError::MissingFile:
        return 1;
    case WaveformPreviewLoadError::None:
        return 0;
    }
    return 0;
}

void updateError(
        WaveformPreviewLoadResult* pResult,
        WaveformPreviewLoadError error,
        const QString& errorPath) {
    if (errorPriority(error) <= errorPriority(pResult->error)) {
        return;
    }
    pResult->error = error;
    pResult->errorPath = errorPath;
}

QRgb monochromeColorForWhiteness(int whiteness) {
    const double whitenessRatio =
            static_cast<double>(qBound(0, whiteness, kMonochromeWhitenessMax)) /
            static_cast<double>(kMonochromeWhitenessMax);
    const QColor baseColor(0x2d, 0x6a, 0xff);
    const QColor highlightColor(Qt::white);
    const int red = static_cast<int>(std::lround(
            baseColor.red() + (highlightColor.red() - baseColor.red()) * whitenessRatio));
    const int green = static_cast<int>(std::lround(
            baseColor.green() + (highlightColor.green() - baseColor.green()) * whitenessRatio));
    const int blue = static_cast<int>(std::lround(
            baseColor.blue() + (highlightColor.blue() - baseColor.blue()) * whitenessRatio));
    return qRgb(red, green, blue);
}

QRgb colorPreviewColor(
        int redLevel,
        int greenLevel,
        int blueLevel,
        int backHeight,
        int maxLevel) {
    if (backHeight <= 0) {
        return qRgb(0, 0, 0);
    }

    return qRgb(
            qBound(0, (redLevel * maxLevel) / backHeight, 255),
            qBound(0, (greenLevel * maxLevel) / backHeight, 255),
            qBound(0, (blueLevel * maxLevel) / backHeight, 255));
}

std::optional<WaveformPreview> parseMonochromePreview(
        const rekordbox_anlz_t::wave_preview_tag_t* pPreviewTag) {
    if (!pPreviewTag || pPreviewTag->data().empty()) {
        return std::nullopt;
    }

    WaveformPreview preview;
    preview.style = WaveformPreviewStyle::Monochrome;
    preview.maxHeight = kMonochromeHeightMask;
    preview.columns.reserve(static_cast<int>(pPreviewTag->data().size()));

    for (const unsigned char previewByte : pPreviewTag->data()) {
        WaveformPreviewColumn column;
        column.frontHeight = previewByte & kMonochromeHeightMask;
        column.frontColor =
                monochromeColorForWhiteness(previewByte >> kMonochromeWhitenessShift);
        preview.columns.append(column);
    }

    if (preview.isEmpty()) {
        return std::nullopt;
    }
    return preview;
}

std::optional<WaveformPreview> parseColorPreview(
        const rekordbox_anlz_t::wave_color_preview_tag_t* pPreviewTag) {
    if (!pPreviewTag || pPreviewTag->len_entry_bytes() != 6) {
        return std::nullopt;
    }

    const auto expectedSize =
            static_cast<std::size_t>(pPreviewTag->len_entry_bytes()) *
            static_cast<std::size_t>(pPreviewTag->len_entries());
    const std::string entries = pPreviewTag->entries();
    if (entries.size() != expectedSize) {
        return std::nullopt;
    }

    WaveformPreview preview;
    preview.style = WaveformPreviewStyle::Color;
    preview.columns.reserve(static_cast<int>(pPreviewTag->len_entries()));

    for (std::size_t offset = 0; offset < entries.size(); offset += 6) {
        // The first three bytes carry metadata used for other waveform views.
        // Rekordbox's color preview is derived from bytes 3/4/5, where byte 5 is
        // also the brighter foreground height. Mirror Beat Link's decoding here.
        const auto redLevel = static_cast<unsigned char>(entries[offset + 3]);
        const auto greenLevel = static_cast<unsigned char>(entries[offset + 4]);
        const auto blueLevel = static_cast<unsigned char>(entries[offset + 5]);

        WaveformPreviewColumn column;
        column.backHeight = std::max({
                static_cast<int>(blueLevel),
                static_cast<int>(redLevel),
                static_cast<int>(greenLevel),
        });
        column.frontHeight = static_cast<int>(blueLevel);
        column.backColor = colorPreviewColor(
                redLevel,
                greenLevel,
                blueLevel,
                column.backHeight,
                191);
        column.frontColor = colorPreviewColor(
                redLevel,
                greenLevel,
                blueLevel,
                column.backHeight,
                255);
        preview.columns.append(column);
        preview.maxHeight = std::max(preview.maxHeight, column.backHeight);
    }

    if (preview.isEmpty()) {
        return std::nullopt;
    }
    return preview;
}

std::optional<WaveformPreview> extractPreview(
        const rekordbox_anlz_t& anlz) {
    std::optional<WaveformPreview> monochromePreview;

    for (const auto& section : *anlz.sections()) {
        switch (section->fourcc()) {
        case rekordbox_anlz_t::SECTION_TAGS_WAVE_COLOR_PREVIEW: {
            if (auto colorPreview = parseColorPreview(
                        static_cast<const rekordbox_anlz_t::wave_color_preview_tag_t*>(
                                section->body()))) {
                return colorPreview;
            }
            break;
        }
        case rekordbox_anlz_t::SECTION_TAGS_WAVE_PREVIEW: {
            if (!monochromePreview) {
                monochromePreview = parseMonochromePreview(
                        static_cast<const rekordbox_anlz_t::wave_preview_tag_t*>(
                                section->body()));
            }
            break;
        }
        default:
            break;
        }
    }

    return monochromePreview;
}

WaveformPreviewLoadResult loadPreviewFromFile(const QString& analyzePath) {
    WaveformPreviewLoadResult result;
    if (analyzePath.isEmpty() || !QFileInfo(analyzePath).exists()) {
        result.error = WaveformPreviewLoadError::MissingFile;
        result.errorPath = analyzePath;
        return result;
    }

    try {
        std::ifstream ifs(analyzePath.toStdString(), std::ifstream::binary);
        if (!ifs.is_open()) {
            result.error = WaveformPreviewLoadError::ParseFailure;
            result.errorPath = analyzePath;
            return result;
        }

        kaitai::kstream stream(&ifs);
        const rekordbox_anlz_t anlz(&stream);
        result.preview = extractPreview(anlz);
        if (!result.preview) {
            result.error = WaveformPreviewLoadError::UnsupportedData;
            result.errorPath = analyzePath;
        }
        return result;
    } catch (const std::exception&) {
        result.error = WaveformPreviewLoadError::ParseFailure;
        result.errorPath = analyzePath;
        return result;
    }
}

} // anonymous namespace

WaveformPreviewLoadResult loadWaveformPreview(const QString& analyzePath) {
    WaveformPreviewLoadResult result;

    const bool isExtPath = analyzePath.endsWith(QStringLiteral("EXT"), Qt::CaseInsensitive);
    const QString extPath = isExtPath
            ? analyzePath
            : siblingAnalyzePath(analyzePath, QStringLiteral("EXT"));
    const QString datPath = isExtPath
            ? siblingAnalyzePath(analyzePath, QStringLiteral("DAT"))
            : analyzePath;

    const WaveformPreviewLoadResult extResult = loadPreviewFromFile(extPath);
    if (extResult.preview) {
        return extResult;
    }
    updateError(&result, extResult.error, extResult.errorPath);

    if (datPath == extPath) {
        return result;
    }

    const WaveformPreviewLoadResult datResult = loadPreviewFromFile(datPath);
    if (datResult.preview) {
        return datResult;
    }
    updateError(&result, datResult.error, datResult.errorPath);

    return result;
}

} // namespace rekordbox
} // namespace mixxx
