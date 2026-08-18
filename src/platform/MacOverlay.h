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

/// Keep a dev-preview window out of the user's way.
///
/// The preview harness opens a normal window, renders, grabs and exits.
/// Left alone that window activates the app, takes keyboard focus and
/// lands on top of whatever the user was doing — which for a harness that
/// gets run dozens of times in a row is genuinely disruptive. This puts it
/// below everything and stops it accepting focus, so a grab can happen
/// while somebody else is working.
void configurePreviewWindow(QWindow *w);
/// Bring the app forward so a window it has just opened is actually seen.
///
/// An LSUIElement agent has no Dock icon and is not in the activation
/// order, so showing a window puts it behind whatever the user was
/// doing. Qt's requestActivate() cannot fix that on its own — it asks the
/// window manager to focus a window belonging to an app that is not
/// front.
void activateApp();

} // namespace hyperbin
