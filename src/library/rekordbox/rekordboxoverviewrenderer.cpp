#include "library/rekordbox/rekordboxoverviewrenderer.h"

#include <QPainter>
#include <algorithm>

namespace mixxx {
namespace rekordbox {

namespace {

struct AggregatedColumn {
    int backHeight = 0;
    int frontHeight = 0;
    int backRed = 0;
    int backGreen = 0;
    int backBlue = 0;
    int backColorCount = 0;
    int frontRed = 0;
    int frontGreen = 0;
    int frontBlue = 0;
    int frontColorCount = 0;
};

AggregatedColumn aggregateColumn(
        const WaveformPreview& preview,
        int x,
        int width) {
    AggregatedColumn column;

    const int sourceCount = preview.columns.size();
    const int start = (x * sourceCount) / width;
    const int end = std::max(start + 1, ((x + 1) * sourceCount) / width);

    for (int sourceIndex = start; sourceIndex < end; ++sourceIndex) {
        const auto& sourceColumn = preview.columns[sourceIndex];
        column.backHeight = std::max(column.backHeight, sourceColumn.backHeight);
        column.frontHeight = std::max(column.frontHeight, sourceColumn.frontHeight);

        if (sourceColumn.backHeight > 0) {
            column.backRed += qRed(sourceColumn.backColor);
            column.backGreen += qGreen(sourceColumn.backColor);
            column.backBlue += qBlue(sourceColumn.backColor);
            ++column.backColorCount;
        }
        if (sourceColumn.frontHeight > 0) {
            column.frontRed += qRed(sourceColumn.frontColor);
            column.frontGreen += qGreen(sourceColumn.frontColor);
            column.frontBlue += qBlue(sourceColumn.frontColor);
            ++column.frontColorCount;
        }
    }

    return column;
}

QColor averagedColor(
        int red,
        int green,
        int blue,
        int count) {
    if (count <= 0) {
        return QColor(Qt::transparent);
    }
    return QColor(red / count, green / count, blue / count);
}

int scaledHeight(
        int sourceHeight,
        int maxHeight,
        int targetHeight) {
    if (sourceHeight <= 0 || maxHeight <= 0 || targetHeight <= 0) {
        return 0;
    }

    return std::max(1, (sourceHeight * targetHeight) / maxHeight);
}

} // anonymous namespace

QImage renderOverview(
        const WaveformPreview& preview,
        QSize size) {
    if (preview.isEmpty() || size.isEmpty()) {
        return QImage();
    }

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int bottom = size.height() - 1;
    for (int x = 0; x < size.width(); ++x) {
        const AggregatedColumn column = aggregateColumn(preview, x, size.width());
        const int backHeight = scaledHeight(column.backHeight, preview.maxHeight, size.height());
        const int frontHeight = scaledHeight(column.frontHeight, preview.maxHeight, size.height());

        if (backHeight > 0) {
            painter.setPen(averagedColor(
                    column.backRed,
                    column.backGreen,
                    column.backBlue,
                    column.backColorCount));
            painter.drawLine(x, bottom, x, bottom - backHeight + 1);
        }
        if (frontHeight > 0) {
            painter.setPen(averagedColor(
                    column.frontRed,
                    column.frontGreen,
                    column.frontBlue,
                    column.frontColorCount));
            painter.drawLine(x, bottom, x, bottom - frontHeight + 1);
        }
    }

    return image;
}

} // namespace rekordbox
} // namespace mixxx
