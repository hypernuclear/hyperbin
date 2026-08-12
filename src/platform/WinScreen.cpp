#include "WinScreen.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellscalingapi.h>

#include <QList>

namespace hyperbin {

namespace {

struct Monitor
{
    QRect physical;      // real pixels, as Windows lays them out
    qreal scale = 1.0;   // 1.5 at 150%
};

BOOL CALLBACK collect(HMONITOR mon, HDC, LPRECT, LPARAM lp)
{
    auto *out = reinterpret_cast<QList<Monitor> *>(lp);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return TRUE;

    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    out->append(Monitor{
        QRect(mi.rcMonitor.left, mi.rcMonitor.top,
              mi.rcMonitor.right - mi.rcMonitor.left,
              mi.rcMonitor.bottom - mi.rcMonitor.top),
        dpiX / 96.0});
    return TRUE;
}

/// Every monitor, re-read each call.
///
/// Not cached: a display can be unplugged, rearranged or rescaled between
/// two polls, and a stale cache turns that into an overlay parked at
/// coordinates that no longer exist. There are two or three of these and
/// the callers run at 1Hz.
QList<Monitor> monitors()
{
    QList<Monitor> list;
    EnumDisplayMonitors(nullptr, nullptr, collect, reinterpret_cast<LPARAM>(&list));
    return list;
}

// Why the monitor's own DPI, rather than the matching QScreen's
// devicePixelRatio:
//
// Qt's Windows backend keeps each screen's NATIVE origin as its
// device-independent origin and divides only the size by that screen's
// scale factor. Verified on this mixed-DPI pair — monitor \\.\DISPLAY2 at
// (0,0) 2560x1440 @150% is Qt's (0,0 1707x960) dpr 1.5, and \\.\DISPLAY1
// at (2560,0) 2560x1440 @100% is Qt's (2560,0 2560x1440) dpr 1.0. So
// anchoring on the native origin and scaling by the monitor's own DPI
// reproduces Qt's mapping exactly, with no need to pair a HMONITOR up
// with a QScreen at all.
//
// That pairing was the first attempt and it was wrong: it matched on
// QScreen::name(), which on Windows is the friendly monitor name
// ("InnoView (2)"), not the GDI device name ("\\.\DISPLAY2"). Nothing
// ever matched, every lookup fell through to the primary screen, and the
// bug stayed invisible precisely because the Recycle Bin is usually ON
// the primary — it would have surfaced as a swarm on the wrong monitor,
// at the wrong size, for the one user with the bin on a second display.

} // namespace

QPoint physicalToDip(const QPoint &physical)
{
    const QList<Monitor> all = monitors();
    for (const Monitor &m : all) {
        if (!m.physical.contains(physical))
            continue;
        const QPoint offset = physical - m.physical.topLeft();
        return m.physical.topLeft()
             + QPoint(qRound(offset.x() / m.scale), qRound(offset.y() / m.scale));
    }
    // Off every display (an icon mid-rearrange, or a monitor just
    // unplugged). Fall back to the nearest monitor rather than inventing
    // a scale of 1.
    if (const POINT pt{physical.x(), physical.y()};
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        UINT dpiX = 96, dpiY = 96;
        if (GetMonitorInfoW(mon, &mi)) {
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            const QPoint origin(mi.rcMonitor.left, mi.rcMonitor.top);
            const QPoint offset = physical - origin;
            const qreal scale = dpiX / 96.0;
            return origin + QPoint(qRound(offset.x() / scale),
                                   qRound(offset.y() / scale));
        }
    }
    return physical;
}

QPoint dipToPhysical(const QPoint &dip)
{
    for (const Monitor &m : monitors()) {
        // The monitor's extent in the same space the argument is in.
        const QRect asDip(m.physical.topLeft(),
                          QSize(qRound(m.physical.width() / m.scale),
                                qRound(m.physical.height() / m.scale)));
        if (!asDip.contains(dip))
            continue;
        const QPoint offset = dip - asDip.topLeft();
        return m.physical.topLeft()
             + QPoint(qRound(offset.x() * m.scale), qRound(offset.y() * m.scale));
    }
    return dip;
}

qreal scaleAtPhysical(const QPoint &physical)
{
    for (const Monitor &m : monitors())
        if (m.physical.contains(physical))
            return m.scale;

    const POINT pt{physical.x(), physical.y()};
    if (HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)) {
        UINT dpiX = 96, dpiY = 96;
        if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
            return dpiX / 96.0;
    }
    return 1.0;
}

} // namespace hyperbin
