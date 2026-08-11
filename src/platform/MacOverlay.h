// Native window configuration Qt has no portable equivalent for: sitting
// above the Dock, ignoring every click, and following the user across
// Spaces without stealing focus.
#pragma once

class QWindow;

namespace hyperbin {

/// Must be called after the window has a native handle (i.e. after
/// create() or first show), or there is no NSWindow to configure.
void configureOverlayWindow(QWindow *w);

/// Hold/release an App Nap exemption.
///
/// A background LSUIElement agent is exactly what App Nap targets: once
/// another app takes focus, macOS throttles our timers and the swarm
/// quietly stops. Held only while actually animating, so an idle
/// hyperbin still naps like a good citizen. Idle *system* sleep is still
/// allowed — this stops throttling, it does not keep the Mac awake.
void setAnimationActivity(bool active);

} // namespace hyperbin
