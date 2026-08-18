// The bin's OPENING, measured out of the shell's artwork.
//
// The silhouette says where the bin is; it does not say where you could
// reach into it. Anything that comes out of the bin needs that second
// thing — the mouth's near lip is the line an emerging shape has to pass
// BEHIND, and without it a tentacle rising out of the trash is a tentacle
// standing in front of it.
//
// This is the one part of the bin the alpha channel cannot answer.
// Measured on macOS against both trash states; see measure() for what the
// signal actually is and how far it was checked.
#pragma once

#include <QImage>
#include <QPointF>

namespace hyperbin {

/// The opening, as an ellipse in the bin rect's own 0..1 coordinates.
///
/// Normalised rather than in pixels, because the icon is measured at
/// whatever size the shell hands over and used at whatever size the
/// overlay happens to be — the Dock alone changes it as the user drags
/// the size slider.
struct BinMouth
{
    /// Centre of the opening, 0..1 across and down the bin's rect. This
    /// is the height at which the mouth is at its WIDEST, not the top of
    /// the rim.
    QPointF centre {0.5, 0.17};
    /// Half the opening's width, as a fraction of the rect's width.
    float halfWidth = 0.42f;
    /// Half the opening's height on screen, as a fraction of the rect's
    /// HEIGHT. This is the tilt made visible: a mouth seen from directly
    /// above would be as tall as it is wide, and one seen edge-on would
    /// be nothing at all.
    float depth = 0.075f;
    /// False when nothing was measured and the numbers above are the
    /// generic fallback. Worth knowing before drawing something that
    /// would look wrong in the wrong place.
    bool measured = false;

    /// The lowest point of the near lip: the line below which the bin's
    /// own front wall stands between the viewer and anything inside.
    float nearArc() const { return float(centre.y()) + depth; }
    /// The highest point of the far lip.
    float farArc() const { return float(centre.y()) - depth; }
};

/// Find the opening in a bin icon. Returns a fallback with measured
/// false if the image is unusable.
BinMouth measureBinMouth(const QImage &icon);

} // namespace hyperbin
