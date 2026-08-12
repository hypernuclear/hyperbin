// Native window configuration Qt has no portable equivalent for, and the
// occlusion test that decides whether drawing is worth anything at all.
//
// The macOS counterpart (MacOverlay) only has to sit above the Dock, which
// is always visible. Here the bin sits ON the desktop, underneath every
// window, so an overlay that simply stayed topmost would scribble flies
// over whatever the user is actually working in.
#pragma once

#include <QObject>
#include <QPoint>

class QWindow;

namespace hyperbin {

/// Topmost, click-through, and never focused. Must be called after the
/// window has a native handle (i.e. after create() or first show).
void configureOverlayWindow(QWindow *w);

/// Watches whether the desktop is actually the thing on screen at a point.
///
/// This is the Windows answer to "is the target visible", and it feeds the
/// same PowerPolicy::setTargetVisible() path that an auto-hidden Dock
/// drives on macOS — which means a covered desktop costs nothing, and on
/// Windows a covered desktop is most of the time.
///
/// Full-screen apps and games fall out of this for free, which is the main
/// reason to test what is on top rather than who has focus.
class DesktopWatcher : public QObject
{
    Q_OBJECT
public:
    explicit DesktopWatcher(QObject *parent = nullptr);
    ~DesktopWatcher() override;

    /// Our own overlay, so the test can ignore it. Without this the
    /// overlay would find itself and always report "covered".
    void setOverlay(QWindow *w);
    /// Where to test, in device-independent pixels — the icon's centre.
    void setPoint(const QPoint &dip);
    void setEnabled(bool on);
    /// Test again now. Called from the foreground hook, which has no
    /// signal of its own to connect.
    void recheck();

    bool desktopVisible() const { return m_visible; }

signals:
    void desktopVisibleChanged(bool visible);

private:
    void sample();

    struct Impl;
    Impl  *d = nullptr;
    QPoint m_point;
    bool   m_visible = true;
    bool   m_enabled = false;
};

} // namespace hyperbin
