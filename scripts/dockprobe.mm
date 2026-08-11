// Dump the Dock's accessibility tree so DockTrashTarget can key off what
// is actually there instead of guessed subroles.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>
#include <cstdio>

static NSString *attr(AXUIElementRef el, CFStringRef name)
{
    CFTypeRef v = nullptr;
    if (AXUIElementCopyAttributeValue(el, name, &v) != kAXErrorSuccess || !v)
        return nil;
    NSString *s = [NSString stringWithFormat:@"%@", (__bridge id)v];
    CFRelease(v);
    return s;
}

static bool rectOf(AXUIElementRef el, CGPoint *pos, CGSize *size)
{
    CFTypeRef p = nullptr, s = nullptr;
    bool ok = AXUIElementCopyAttributeValue(el, kAXPositionAttribute, &p) == kAXErrorSuccess
           && AXUIElementCopyAttributeValue(el, kAXSizeAttribute, &s) == kAXErrorSuccess;
    if (ok) {
        AXValueGetValue((AXValueRef)p, kAXValueTypeCGPoint, pos);
        AXValueGetValue((AXValueRef)s, kAXValueTypeCGSize, size);
    }
    if (p) CFRelease(p);
    if (s) CFRelease(s);
    return ok;
}

int main()
{
    @autoreleasepool {
        std::printf("AXIsProcessTrusted = %d\n", AXIsProcessTrusted());

        pid_t dockPid = 0;
        for (NSRunningApplication *a in [[NSWorkspace sharedWorkspace] runningApplications]) {
            if ([a.bundleIdentifier isEqualToString:@"com.apple.dock"]) {
                dockPid = a.processIdentifier;
                break;
            }
        }
        std::printf("dock pid = %d\n", dockPid);
        if (!dockPid) return 1;

        AXUIElementRef dock = AXUIElementCreateApplication(dockPid);
        CFTypeRef kids = nullptr;
        if (AXUIElementCopyAttributeValue(dock, kAXChildrenAttribute, &kids) != kAXErrorSuccess) {
            std::printf("no children — accessibility not granted?\n");
            return 2;
        }

        NSArray *top = (__bridge NSArray *)kids;
        std::printf("dock top-level children: %lu\n", (unsigned long)top.count);
        for (id c in top) {
            AXUIElementRef el = (__bridge AXUIElementRef)c;
            std::printf("  role=%s subrole=%s title=%s\n",
                        [attr(el, kAXRoleAttribute) ?: @"-" UTF8String],
                        [attr(el, kAXSubroleAttribute) ?: @"-" UTF8String],
                        [attr(el, kAXTitleAttribute) ?: @"-" UTF8String]);

            CFTypeRef sub = nullptr;
            if (AXUIElementCopyAttributeValue(el, kAXChildrenAttribute, &sub) != kAXErrorSuccess)
                continue;
            NSArray *items = (__bridge NSArray *)sub;
            std::printf("    %lu items; last 6 and anything trash-ish:\n",
                        (unsigned long)items.count);
            for (NSUInteger i = 0; i < items.count; ++i) {
                AXUIElementRef it = (__bridge AXUIElementRef)items[i];
                NSString *subrole = attr(it, kAXSubroleAttribute) ?: @"-";
                NSString *title   = attr(it, kAXTitleAttribute) ?: @"-";
                const bool tail   = i + 6 >= items.count;
                const bool trashy = [subrole localizedCaseInsensitiveContainsString:@"trash"]
                                 || [title localizedCaseInsensitiveContainsString:@"trash"];
                if (!tail && !trashy) continue;
                CGPoint p{}; CGSize s{};
                const bool ok = rectOf(it, &p, &s);
                std::printf("      [%2lu] subrole=%-26s title=%-22s rect=%s",
                            (unsigned long)i, subrole.UTF8String, title.UTF8String,
                            ok ? "" : "(none)");
                if (ok) std::printf("(%.0f,%.0f %.0fx%.0f)", p.x, p.y, s.width, s.height);
                std::printf("%s\n", trashy ? "   <-- TRASH" : "");
            }
            CFRelease(sub);
        }
        CFRelease(kids);

        // Screen geometry, to work out which origin AX is reporting in.
        for (NSScreen *sc in [NSScreen screens]) {
            NSRect f = sc.frame, v = sc.visibleFrame;
            std::printf("screen frame=(%.0f,%.0f %.0fx%.0f) visible=(%.0f,%.0f %.0fx%.0f) scale=%.1f\n",
                        f.origin.x, f.origin.y, f.size.width, f.size.height,
                        v.origin.x, v.origin.y, v.size.width, v.size.height,
                        sc.backingScaleFactor);
        }
    }
    return 0;
}
