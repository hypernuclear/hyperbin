// Physical pixels <-> Qt's device-independent pixels, on Windows.
//
// Every shell API here speaks PHYSICAL pixels — IFolderView2 positions,
// WindowFromPoint, GetWindowRect — while QRect/QWindow speak DIP. On a
// single 100% display the two are the same number, which is exactly why
// getting this wrong is invisible until a second monitor or a scaled
// display appears. Both backends need the conversion, so it lives here
// rather than in whichever one grew it first.
#pragma once

#include <QPoint>
#include <QRect>

namespace hyperbin {

/// DIP for a physical screen point, using the monitor that point is on.
///
/// Not a single global scale factor: with mixed-DPI displays each monitor
/// has its own, so the conversion has to be anchored to the monitor under
/// the point rather than to the primary screen's ratio.
QPoint physicalToDip(const QPoint &physical);

/// Physical screen point for a DIP point. Inverse of the above.
QPoint dipToPhysical(const QPoint &dip);

/// Scale factor of the monitor under a physical point (1.0 at 100%).
/// Use it to convert sizes, which have no position to anchor to.
qreal scaleAtPhysical(const QPoint &physical);

} // namespace hyperbin
