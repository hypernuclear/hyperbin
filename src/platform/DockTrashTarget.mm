#include "DockTrashTarget.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace hyperbin {

namespace {

// Verified against the live Dock (2026-08-09): the Dock exposes one
// top-level AXList whose children are the dock items, and the trash
// carries subrole AXTrashDockItem. Keying on the subrole rather than the
// title avoids every localisation problem.
NSString *const kTrashSubrole = @"AXTrashDockItem";

pid_t dockPid()
{
    for (NSRunningApplication *a in NSWorkspace.sharedWorkspace.runningApplications)
        if ([a.bundleIdentifier isEqualToString:@"com.apple.dock"])
            return a.processIdentifier;
    return 0;
}

CFTypeRef copyAttr(AXUIElementRef el, CFStringRef name)
{
    CFTypeRef v = nullptr;
    if (AXUIElementCopyAttributeValue(el, name, &v) != kAXErrorSuccess)
        return nullptr;
    return v;
}

/// Walks the Dock for the trash item. Returns a +1 reference, or null.
AXUIElementRef findTrashItem()
{
    const pid_t pid = dockPid();
    if (!pid)
        return nullptr;

    AXUIElementRef dock = AXUIElementCreateApplication(pid);
    if (!dock)
        return nullptr;

    AXUIElementRef found = nullptr;
    CFTypeRef lists = copyAttr(dock, kAXChildrenAttribute);
    for (id listObj in (__bridge NSArray *)lists) {
        CFTypeRef items = copyAttr((__bridge AXUIElementRef)listObj, kAXChildrenAttribute);
        for (id itemObj in (__bridge NSArray *)items) {
            AXUIElementRef item = (__bridge AXUIElementRef)itemObj;
            CFTypeRef sub = copyAttr(item, kAXSubroleAttribute);
            const bool hit = sub && [(__bridge NSString *)sub isEqualToString:kTrashSubrole];
            if (sub) CFRelease(sub);
            if (hit) {
                found = (AXUIElementRef)CFRetain(item);
                break;
            }
        }
        if (items) CFRelease(items);
        if (found) break;
    }
    if (lists) CFRelease(lists);
    CFRelease(dock);
    return found;
}

} // namespace

struct DockTrashTarget::Impl
{
    AXUIElementRef trash = nullptr;
    ~Impl() { if (trash) CFRelease(trash); }
};

bool DockTrashTarget::accessibilityGranted(bool prompt)
{
    // Launching from a terminal that already has Accessibility makes the
    // terminal the responsible process, so the app inherits the grant and
    // the ungranted path can't be reached in development. This forces it.
    if (qEnvironmentVariableIsSet("HYPERBIN_FORCE_NO_AX")) {
        if (prompt)
            AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)
                @{ (__bridge id)kAXTrustedCheckOptionPrompt: @YES });
        return false;
    }

    if (!prompt)
        return AXIsProcessTrusted();
    NSDictionary *opts = @{ (__bridge id)kAXTrustedCheckOptionPrompt: @YES };
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
}

DockTrashTarget::DockTrashTarget(QObject *parent)
    : TrashTarget(parent), d(new Impl)
{
    // Two rates. Dock magnification resizes and moves the tile every
    // frame while the pointer is over it, so 1Hz leaves the swarm
    // visibly lagging behind the icon; but polling that fast forever
    // wastes power when nothing is happening. Track fast while the icon
    // is on screen, slowly when it isn't.
    m_poll.setInterval(kFastPollMs);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        pollIconRect();
        if (++m_trashTick >= (m_poll.interval() >= kSlowPollMs ? 1 : 20)) {
            m_trashTick = 0;
            pollTrash();  // the Finder round-trip stays at ~1Hz regardless
        }
    });

    // Granting Accessibility doesn't notify the app, so watch for it and
    // start working the moment it appears — requiring a restart after
    // granting is a miserable first run.
    m_permissionWatch.setInterval(1000);
    connect(&m_permissionWatch, &QTimer::timeout, this, [this] {
        // Do NOT gate this on AXIsProcessTrusted(): it caches per process,
        // so an app that started untrusted can keep reporting untrusted
        // long after the user has granted it — which looks exactly like
        // "I granted it and the app didn't pick it up". Attempting the
        // real call is the only honest test of whether we have access.
        AXUIElementRef probe = findTrashItem();
        if (!probe)
            return;
        CFRelease(probe);

        m_permissionWatch.stop();
        qInfo("hyperbin: Accessibility is live — tracking the Dock.");
        beginTracking();
    });
}

DockTrashTarget::~DockTrashTarget()
{
    delete d;
}

void DockTrashTarget::start()
{
    // Can we actually reach the Dock? That's the question that matters,
    // and it's not the same as what AXIsProcessTrusted() reports (which
    // caches). Probe for real, and only fall back to asking.
    if (!qEnvironmentVariableIsSet("HYPERBIN_FORCE_NO_AX")) {
        if (AXUIElementRef probe = findTrashItem()) {
            CFRelease(probe);
            beginTracking();
            return;
        }
    }

    setStatus(Status::PermissionRequired);
    // Show the system prompt rather than failing silently to a log line.
    // It carries an "Open System Settings" button, so it's both the
    // explanation and the way to act on it. Only prompt when we're
    // genuinely untrusted — a missing Dock would otherwise nag for a
    // permission that isn't the problem.
    if (!accessibilityGranted())
        accessibilityGranted(/*prompt=*/true);
    m_permissionWatch.start();
}

void DockTrashTarget::beginTracking()
{
    // Reading ~/.Trash outright needs Full Disk Access. If we happen to
    // have it, use it — it's cheaper and needs nobody else running.
    m_directRead = QDir(QDir::homePath() + QStringLiteral("/.Trash")).isReadable();
    qInfo("hyperbin: trash count via %s",
          m_directRead ? "direct read (Full Disk Access)" : "Finder (Automation)");

    pollIconRect();
    pollTrash();
    m_poll.start();
}

void DockTrashTarget::stop()
{
    m_poll.stop();
    m_permissionWatch.stop();
}

void DockTrashTarget::setStatus(Status s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged(s);
}

int DockTrashTarget::countViaFinder()
{
    // Measured: ~/.Trash listing is TCC-protected (Full Disk Access), but
    // asking Finder for the count only needs Automation, which is a prompt
    // the user can reasonably accept for a novelty app.
    NSAppleScript *s = [[NSAppleScript alloc]
        initWithSource:@"tell application \"Finder\" to count items in trash"];
    NSDictionary *err = nil;
    NSAppleEventDescriptor *r = [s executeAndReturnError:&err];
    if (!r || err) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning("hyperbin: Finder refused the trash count (%s). Grant Automation "
                     "in System Settings > Privacy & Security > Automation.",
                     [[err description] UTF8String] ?: "no detail");
        }
        return -1;
    }
    return int(r.int32Value);
}

void DockTrashTarget::pollTrash()
{
    // Only ~/.Trash. Items trashed from other volumes live in
    // /Volumes/<x>/.Trashes/<uid> and are not counted yet.
    const QString path = QDir::homePath() + QStringLiteral("/.Trash");

    // stat() is not TCC-protected even when listing is, so this is a free
    // change check — the expensive count only runs when it actually moved.
    const QDateTime mtime = QFileInfo(path).lastModified();
    if (mtime == m_trashMTime)
        return;
    m_trashMTime = mtime;

    int n = -1;
    if (m_directRead) {
        const auto entries = QDir(path).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        n = 0;
        for (const QString &e : entries)
            if (e != QStringLiteral(".DS_Store"))
                ++n;
    } else {
        n = countViaFinder();
    }
    if (n < 0)
        return; // couldn't tell; leave the last known count alone

    if (n == m_count)
        return;
    qInfo("hyperbin: trash -> %d item(s)", n);
    m_count = n;
    emit itemCountChanged(n);
}

void DockTrashTarget::pollIconRect()
{
    if (!d->trash)
        d->trash = findTrashItem();
    if (!d->trash) {
        setStatus(Status::NotFound);
        return;
    }

    CFTypeRef p = copyAttr(d->trash, kAXPositionAttribute);
    CFTypeRef s = copyAttr(d->trash, kAXSizeAttribute);
    if (!p || !s) {
        // Stale element — the Dock restarted or the item was rebuilt.
        if (p) CFRelease(p);
        if (s) CFRelease(s);
        CFRelease(d->trash);
        d->trash = nullptr;
        setStatus(Status::NotFound);
        return;
    }

    CGPoint pos{};
    CGSize  size{};
    AXValueGetValue((AXValueRef)p, kAXValueTypeCGPoint, &pos);
    AXValueGetValue((AXValueRef)s, kAXValueTypeCGSize, &size);
    CFRelease(p);
    CFRelease(s);

    // AX reports top-left-origin points, same convention and units as Qt
    // screen coordinates, so this needs no conversion.
    const QRect r(int(pos.x), int(pos.y), int(size.width), int(size.height));

    // An auto-hidden Dock parks its items just outside the screen it
    // belongs to, so "is it visible" is exactly "does it land on any
    // display". Both measured on a 3-display layout with a left-side
    // Dock: visible at x=-1910 (inside the -1920 display), hidden at
    // x=-1960 (outside every display).
    //
    // NSScreen.frame is bottom-left origin and AX is top-left, so the
    // screens have to be flipped into AX space before comparing — an
    // earlier version compared them raw and also assumed x<0 meant
    // off-screen, which wrongly rejected every left-hand display.
    const CGFloat primaryMaxY = NSMaxY(NSScreen.screens.firstObject.frame);
    bool onScreen = false;
    for (NSScreen *sc in NSScreen.screens) {
        const NSRect f = sc.frame;
        const QRect inAxSpace(int(f.origin.x), int(primaryMaxY - NSMaxY(f)),
                              int(f.size.width), int(f.size.height));
        if (inAxSpace.intersects(r))
            onScreen = true;
    }

    // Drop to a lazy poll when there's nothing to follow.
    const int want = onScreen ? kFastPollMs : kSlowPollMs;
    if (m_poll.interval() != want)
        m_poll.setInterval(want);

    const Status was = m_status;
    setStatus(onScreen ? Status::Ok : Status::IconHidden);
    if (was != m_status)
        qInfo("hyperbin: trash icon at (%d,%d %dx%d) onScreen=%d",
              r.x(), r.y(), r.width(), r.height(), onScreen);
    if (r != m_rect) {
        m_rect = r;
        emit iconRectChanged(r);
    }
}

void DockTrashTarget::openRemediation()
{
    // The system prompt only appears once; after that the user has to be
    // sent to the right settings pane by hand.
    NSString *pane = accessibilityGranted()
        ? @"x-apple.systempreferences:com.apple.preference.security?Privacy_Automation"
        : @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:pane]];
}

} // namespace hyperbin
