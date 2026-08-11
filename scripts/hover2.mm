// Hover the Dock's trash using synthetic mouse-move EVENTS (not a cursor
// warp — the Dock ignores warps for magnification purposes), hold it
// there, and report the AX rect each half second so the magnified
// geometry can be measured.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

static AXUIElementRef findTrash()
{
    pid_t pid = 0;
    for (NSRunningApplication *a in NSWorkspace.sharedWorkspace.runningApplications)
        if ([a.bundleIdentifier isEqualToString:@"com.apple.dock"])
            pid = a.processIdentifier;
    AXUIElementRef dock = AXUIElementCreateApplication(pid);
    CFTypeRef lists = nullptr;
    AXUIElementCopyAttributeValue(dock, kAXChildrenAttribute, &lists);
    AXUIElementRef found = nullptr;
    for (id L in (__bridge NSArray *)lists) {
        CFTypeRef items = nullptr;
        AXUIElementCopyAttributeValue((__bridge AXUIElementRef)L, kAXChildrenAttribute, &items);
        for (id I in (__bridge NSArray *)items) {
            CFTypeRef sub = nullptr;
            AXUIElementCopyAttributeValue((__bridge AXUIElementRef)I, kAXSubroleAttribute, &sub);
            if (sub && [(__bridge NSString *)sub isEqualToString:@"AXTrashDockItem"])
                found = (AXUIElementRef)CFRetain((__bridge AXUIElementRef)I);
            if (sub) CFRelease(sub);
        }
        if (items) CFRelease(items);
    }
    if (lists) CFRelease(lists);
    CFRelease(dock);
    return found;
}

static bool rectOf(AXUIElementRef el, CGPoint *p, CGSize *s)
{
    CFTypeRef pv = nullptr, sv = nullptr;
    if (AXUIElementCopyAttributeValue(el, kAXPositionAttribute, &pv) != kAXErrorSuccess) return false;
    if (AXUIElementCopyAttributeValue(el, kAXSizeAttribute, &sv) != kAXErrorSuccess) { CFRelease(pv); return false; }
    AXValueGetValue((AXValueRef)pv, kAXValueTypeCGPoint, p);
    AXValueGetValue((AXValueRef)sv, kAXValueTypeCGSize, s);
    CFRelease(pv); CFRelease(sv);
    return true;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const int holdMs = argc > 1 ? atoi(argv[1]) : 6000;

        AXUIElementRef trash = findTrash();
        if (!trash) { std::printf("trash not found\n"); return 1; }
        CGPoint p{}; CGSize s{};
        rectOf(trash, &p, &s);
        const CGPoint target = CGPointMake(p.x + s.width / 2, p.y + s.height / 2);

        CGEventRef cur = CGEventCreate(nullptr);
        const CGPoint was = CGEventGetLocation(cur);
        CFRelease(cur);
        std::printf("resting AX rect: (%.0f,%.0f) %.0fx%.0f\n", p.x, p.y, s.width, s.height);
        std::printf("hovering (%.0f,%.0f); cursor returns to (%.0f,%.0f)\n",
                    target.x, target.y, was.x, was.y);
        fflush(stdout);

        // Keep posting moves: one event may not settle the magnification,
        // and the Dock animates toward the hovered size.
        const int steps = holdMs / 100;
        for (int i = 0; i < steps; ++i) {
            CGEventRef mv = CGEventCreateMouseEvent(
                nullptr, kCGEventMouseMoved,
                CGPointMake(target.x + (i % 2 ? 0.5 : -0.5), target.y), kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap, mv);
            CFRelease(mv);
            usleep(100000);
            if (i % 5 == 4 && rectOf(trash, &p, &s))
                std::printf("  t=%.1fs AX (%.0f,%.0f) %.0fx%.0f\n",
                            (i + 1) * 0.1, p.x, p.y, s.width, s.height), fflush(stdout);
        }

        CGWarpMouseCursorPosition(was);
        std::printf("cursor restored\n");
    }
    return 0;
}
