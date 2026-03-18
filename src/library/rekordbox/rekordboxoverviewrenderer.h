#pragma once

#include <QImage>
#include <QSize>

#include "library/rekordbox/rekordboxwaveformpreview.h"

namespace mixxx {
namespace rekordbox {

/// Render Rekordbox preview samples into a table overview image.
QImage renderOverview(
        const WaveformPreview& preview,
        QSize size);

} // namespace rekordbox
} // namespace mixxx
