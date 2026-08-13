#include "WinOverlay.h"

#include "WinScreen.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwmapi.h>

#include <QDebug>
#include <QTimer>
#include <QWindow>

namespace hyperbin {

namespace {

/// Shell classes that ARE the desktop. Progman owns it; WorkerW is the
/// window the wallpaper is drawn into when a slideshow or a third-party
/// wallpaper app is running; SHELLDLL_DefView is the icon host inside
/// either of them.
bool isDesktopClass(HWND hwnd)
{
    wchar_t cls[64]{};
    if (!GetClassNameW(hwnd, cls, 64))
        return false;
    return wcscmp(cls, L"Progman") == 0
        || wcscmp(cls, L"WorkerW") == 0
        || wcscmp(cls, L"SHELLDLL_DefView") == 0;
}

bool isCloaked(HWND hwnd)
{
    // A window on another virtual desktop is still "visible" by
    // IsWindowVisible — it is cloaked, not hidden. Treating cloaked
    // windows as covering the bin is how the swarm would vanish because
    // of a window the user cannot even see.
    DWORD cloaked = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        return false;
    return cloaked != 0;
}

struct HitTest
{
    POINT point{};
    HWND  ignore = nullptr;   // our own overlay
    HWND  hit    = nullptr;
};

/// A HUD rather than something the user is working in.
///
/// Topmost AND (a tool window or click-through) is the signature of an
/// overlay: screen-capture selection layers, magnifiers, on-screen
/// keyboards, notification HUDs. Treating those as "the desktop is
/// covered" is what made the swarm vanish the moment Hypershot's capture
/// overlay went up — so every screenshot of hyperbin came out empty, and
/// so would any user's screenshot of their own desktop.
///
/// The failure modes are not symmetric, which is what decides this. Get
/// it wrong this way and we keep rendering underneath something opaque:
/// a little wasted GPU, nothing visibly wrong. Get it wrong the other way
/// and the effect is missing from the picture the user was trying to
/// take. An "always on top" media player is topmost but NOT a tool
/// window, so it still counts as covering — which is right, it is a real
/// window in the way.
bool isOverlayWindow(HWND hwnd)
{
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_TOPMOST))
        return false;
    return (ex & WS_EX_TOOLWINDOW) || (ex & WS_EX_TRANSPARENT);
}

BOOL CALLBACK topmostAt(HWND hwnd, LPARAM lp)
{
    auto *t = reinterpret_cast<HitTest *>(lp);
    if (hwnd == t->ignore || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    if (isCloaked(hwnd) || isOverlayWindow(hwnd))
        return TRUE;

    RECT r{};
    if (!GetWindowRect(hwnd, &r))
        return TRUE;
    if (!PtInRect(&r, t->point))
        return TRUE;

    t->hit = hwnd;
    return FALSE;   // EnumWindows walks front to back, so the first hit wins
}

DesktopWatcher *g_watcher = nullptr;

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    // Runs on our own thread (WINEVENT_OUTOFCONTEXT delivers through the
    // message loop), but queue it anyway: the hook fires mid-transition,
    // and testing z-order before the new foreground window has actually
    // been raised reads the state we are about to leave.
    if (g_watcher)
        QMetaObject::invokeMethod(g_watcher, [] { if (g_watcher) g_watcher->recheck(); },
                                  Qt::QueuedConnection);
}

} // namespace

struct DesktopWatcher::Impl
{
    HWINEVENTHOOK hook    = nullptr;
    HWND          overlay = nullptr;
    QTimer        poll;
};

void configureOverlayWindow(QWindow *w)
{
    if (!w)
        return;
    w->create();   // ensure there's a native handle to reach through

    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd)
        return;

    // WS_EX_TRANSPARENT is doing two jobs: clicks fall through to the
    // desktop underneath, and — because it also takes the window out of
    // hit-testing — WindowFromPoint and EnumWindows-based tests never find
    // the overlay when asking what is on top of the bin.
    //
    // WS_EX_NOACTIVATE keeps a click near the bin from stealing focus, and
    // WS_EX_TOOLWINDOW keeps the overlay out of Alt-Tab and the taskbar.
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE)
                      | WS_EX_LAYERED | WS_EX_TRANSPARENT
                      | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

DesktopWatcher::DesktopWatcher(QObject *parent)
    : QObject(parent), d(new Impl)
{
    g_watcher = this;

    // Foreground changes are the event that matters — a window being
    // raised or closed over the bin. The poll underneath it catches the
    // rest (a window dragged, resized, or scrolled away) without needing a
    // hook for each. Coarse and slow: this decides whether to draw, not
    // what to draw.
    d->poll.setInterval(1000);
    d->poll.setTimerType(Qt::CoarseTimer);
    connect(&d->poll, &QTimer::timeout, this, [this] { sample(); });
}

DesktopWatcher::~DesktopWatcher()
{
    if (d->hook)
        UnhookWinEvent(d->hook);
    g_watcher = nullptr;
    delete d;
}

void DesktopWatcher::setOverlay(QWindow *w)
{
    d->overlay = w ? reinterpret_cast<HWND>(w->winId()) : nullptr;
}

void DesktopWatcher::setPoint(const QPoint &dip)
{
    if (dip == m_point)
        return;
    m_point = dip;
    if (m_enabled)
        sample();
}

void DesktopWatcher::recheck()
{
    if (m_enabled)
        sample();
}

void DesktopWatcher::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on) {
        if (!d->hook)
            d->hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                      nullptr, foregroundChanged, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        d->poll.start();
        sample();
    } else {
        d->poll.stop();
        if (d->hook) {
            UnhookWinEvent(d->hook);
            d->hook = nullptr;
        }
    }
}

void DesktopWatcher::sample()
{
    if (m_point.isNull())
        return;

    HitTest test;
    const QPoint physical = dipToPhysical(m_point);
    test.point  = POINT{physical.x(), physical.y()};
    test.ignore = d->overlay;
    EnumWindows(topmostAt, reinterpret_cast<LPARAM>(&test));

    // Nothing above the desktop at that point, or the thing above it IS
    // the desktop.
    const bool visible = !test.hit || isDesktopClass(test.hit);
    if (visible == m_visible)
        return;
    m_visible = visible;
    if (qEnvironmentVariableIsSet("HYPERBIN_DEBUG")) {
        wchar_t cls[64]{};
        if (test.hit)
            GetClassNameW(test.hit, cls, 64);
        qInfo("hyperbin: desktop %s at the bin (top window class '%s')",
              visible ? "visible" : "covered",
              qPrintable(QString::fromWCharArray(cls)));
    }
    emit desktopVisibleChanged(visible);
}

} // namespace hyperbin
