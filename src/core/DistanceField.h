// Signed distance field of the bin, built from its own artwork.
//
// The alternative was hand-modelling a low-poly bin, which would be our
// bin rather than the shell's: the Dock's trash is a translucent mesh
// cylinder, the Windows Recycle Bin is a different shape again, and both
// change with theme and magnification. Anything that hugs an approximated
// silhouette visibly fails to hug the real one, and the bigger the icon
// the worse it reads.
//
// Deriving the field from the icon's alpha means the shape is always
// exactly what the shell drew, on both platforms, at any size — the same
// reasoning that made fly landing use the silhouette instead of the
// bounding box.
#pragma once

#include <QImage>
#include <QVector>

namespace hyperbin {

/// Signed distance field from a coverage grid, as an 8-bit greyscale
/// image: 0.5 is the surface, below is inside, above is outside.
///
/// `rangeCells` is the distance, in output cells, that maps to the full
/// 0..1 range. Distances beyond it clamp — a field only has to be
/// accurate near the surface, and clamping keeps the 8 bits where they
/// are worth spending.
QImage buildSignedDistanceField(const QVector<quint8> &coverage, int w, int h,
                                int outSize, float rangeCells);

} // namespace hyperbin
