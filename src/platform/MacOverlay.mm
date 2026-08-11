#include "MacOverlay.h"

#import <AppKit/AppKit.h>

#include <QWindow>

namespace hyperbin {

namespace {
// These .mm files are manual reference counted (no -fobjc-arc), so the
// autoreleased activity token has to be retained for as long as we hold
// it and released when we let go.
id<NSObject> g_activity = nil;
} // namespace

void setAnimationActivity(bool active)
{
    if (active == (g_activity != nil))
        return;

    if (active) {
        g_activity = [[[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                              reason:@"animating the trash overlay"] retain];
    } else {
        [[NSProcessInfo processInfo] endActivity:g_activity];
        [g_activity release];
        g_activity = nil;
    }
}

void configureOverlayWindow(QWindow *w)
{
    if (!w)
        return;
    w->create(); // ensure there's a native handle to reach through

    NSView *view = reinterpret_cast<NSView *>(w->winId());
    NSWindow *win = view.window;
    if (!win)
        return;

    // One level above the Dock. kCGDockWindowLevelKey is 20; going higher
    // than +1 would start covering menus and the screen saver.
    win.level = CGWindowLevelForKey(kCGDockWindowLevelKey) + 1;

    win.opaque          = NO;
    win.backgroundColor = NSColor.clearColor;
    win.hasShadow       = NO;
    win.ignoresMouseEvents = YES;   // clicks pass through to the Dock below
    win.animationBehavior  = NSWindowAnimationBehaviorNone;

    // Qt::Tool becomes an AppKit utility panel, and those hide themselves
    // whenever their app is not frontmost. For a normal tool palette that
    // is right; for an overlay it means the swarm vanishes the moment the
    // user clicks anything else, which is exactly what it looked like.
    win.hidesOnDeactivate = NO;

    // Follow the user across Spaces, don't move with Mission Control, stay
    // out of Cmd-Tab, and remain visible alongside fullscreen apps.
    win.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                           | NSWindowCollectionBehaviorStationary
                           | NSWindowCollectionBehaviorIgnoresCycle
                           | NSWindowCollectionBehaviorFullScreenAuxiliary;
}

} // namespace hyperbin
