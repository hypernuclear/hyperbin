#include "DockTrashTarget.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <QDir>
#include <QDirIterator>
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

/// The Accessibility rect for a dock item is its hit area, not the
/// artwork's bounds — it's larger and offset. Everything we draw needs
/// the artwork's real rect. See the calibration note inside.
QRect visualIconRect(const QRect &ax)
{
    if (ax.isEmpty())
        return ax;

    const int minDim = qMin(ax.width(), ax.height());
    const int majDim = qMax(ax.width(), ax.height());

    // The Dock draws each item as a SQUARE tile, and the artwork carries
    // its own transparent padding inside that square. So the thing to
    // locate is the tile, not the visible bin: one square, one size, one
    // centre. Mapping the uncropped artwork onto it reproduces whatever
    // inset the artwork has, for free.
    //
    // Evidence the tile is square and the AX rect is tile + fixed padding:
    //
    //   state      AX rect     minDim   majDim - minDim
    //   resting    40 x 28     28       12
    //   magnified  109 x 97    97       12
    //
    // sizeK is therefore expected to be near 1.0, and nearPt/crossPt near
    // 0 — small residuals, not the large fudges they were. A constant
    // offset in POINTS is also why alignment looked fine magnified and
    // badly wrong small: 8pt is a third of a 25pt icon and under a tenth
    // of an 87pt one. Verify with HYPERBIN_CALIBRATE=s|x|y.
    // Residuals against the square model, confirmed by eye through the
    // calibration sweeps rather than by an automated fit — the fitter
    // could never segment a semi-transparent bin reliably.
    //
    // sizeK is the one that matters: 0.957 is close to the 1.0 the model
    // predicts, and the small shortfall is the tile's own inset. Crucially
    // it holds across magnification, which the previous visible-bin model
    // never managed — its constants traded off against tile size, so it
    // could be right small or right large but never both.
    double sizeK  = 0.957; // tile side / min(AX.w, AX.h)
    double nearPt = 5.0;   // residual toward the Dock's edge, in pt
    double crossPt = 3.0;  // residual along the Dock, in pt
    // The artwork is not quite centred in the AX tile: it sits a couple of
    // points right of centre, so the mask's left edge lands inside the
    // bin while the right edge is exact. This is applied to the left edge
    // ALONE — shifting the centre instead would drag the right edge in
    // with it, and the right edge is correct. Constant in points, not
    // scaled with the tile: alignment currently holds across the full
    // magnification range, and a scaled term would break that at the top
    // end to fix two pixels at the bottom.
    const double leftBleedPt = 2.0;
    double dxExtra = 0.0, dyExtra = 0.0, scaleExtra = 1.0;
    if (qEnvironmentVariableIsSet("HYPERBIN_BIN_ADJUST")) {
        const QStringList p = qEnvironmentVariable("HYPERBIN_BIN_ADJUST").split(',');
        if (p.size() >= 1) dxExtra = p[0].toDouble();
        if (p.size() >= 2) dyExtra = p[1].toDouble();
        if (p.size() >= 3 && p[2].toDouble() > 0) scaleExtra = p[2].toDouble();
    }
    // Square: the artwork is square, so width and height match. Kept as a
    // double until the very end: rounding the side first and then placing
    // it with `centre - w / 2` puts the whole rounding error on one edge,
    // because integer division truncates. That is a hard 1px overhang on
    // the right (and bottom) whenever the side is odd.
    const double side = sizeK * minDim * scaleExtra;
    const double shift = nearPt;
    Q_UNUSED(majDim);
    // Which edge is the Dock on? Infer from geometry rather than reading
    // com.apple.dock, so a per-display arrangement can't disagree.
    QRect screen;
    const CGFloat primaryMaxY = NSMaxY(NSScreen.screens.firstObject.frame);
    for (NSScreen *sc in NSScreen.screens) {
        const NSRect f = sc.frame;
        const QRect s(int(f.origin.x), int(primaryMaxY - NSMaxY(f)),
                      int(f.size.width), int(f.size.height));
        if (s.intersects(ax)) { screen = s; break; }
    }
    QPointF c = QRectF(ax).center();
    if (ax.width() >= ax.height()) {
        const bool dockOnLeft = screen.isNull()
            || (c.x() - screen.left()) <= (screen.right() - c.x());
        c.rx() += dockOnLeft ? -shift : shift;
        c.ry() += crossPt;
    } else {
        const bool dockOnTop = !screen.isNull()
            && (c.y() - screen.top()) < (screen.bottom() - c.y());
        c.ry() += dockOnTop ? -shift : shift;
        c.rx() += crossPt;
    }
    // Each edge is derived independently from the same real-valued
    // centre, so the rounding error can't all land on one side — that was
    // a hard 1px overhang on the right whenever the side was odd.
    //
    // And each edge rounds OUTWARD rather than to nearest. The two errors
    // are not equally bad: a mask a hair too large hides a fly a pixel
    // early, which is invisible, while one a hair too small lets a fly
    // that should be behind the bin show through along the edge. At a
    // small Dock size the side is ~17.2px inside an 18px tile, so
    // round-to-nearest reliably ate a pixel off one edge.
    const int x0 = int(std::floor(c.x() - side / 2.0 + dxExtra - leftBleedPt));
    const int y0 = int(std::floor(c.y() - side / 2.0 + dyExtra));
    const int x1 = int(std::ceil (c.x() + side / 2.0 + dxExtra));
    const int y1 = int(std::ceil (c.y() + side / 2.0 + dyExtra));
    return QRect(x0, y0, x1 - x0, y1 - y0);
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
        if (++m_trashTick >= (m_poll.interval() >= kSlowPollMs ? 1 : 60)) {
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

void DockTrashTarget::setAnimating(bool animating)
{
    if (animating == m_animating)
        return;
    m_animating = animating;
    if (!m_poll.isActive())
        return;
    if (animating) {
        // Resume at frame rate AND take a reading immediately: the Dock
        // may have moved or magnified while we were idle, and waiting up
        // to a second for the next slow tick would show as the swarm
        // starting up in the wrong place.
        m_poll.setInterval(kFastPollMs);
        pollIconRect();
    } else {
        m_poll.setInterval(kSlowPollMs);
    }
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

    // Size, when we're allowed to read the directory at all. Only
    // recomputed on an mtime change, same as the count, so an idle bin
    // costs one stat() per poll and nothing else.
    qint64 bytes = -1;
    if (m_directRead) {
        bytes = 0;
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden
                                  | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            bytes += it.fileInfo().size();
        }
    }
    if (bytes != m_bytes) {
        m_bytes = bytes;
        emit byteSizeChanged(bytes);
    }
    if (n == m_count)
        return;
    qInfo("hyperbin: trash -> %d item(s), %lld byte(s)", n, (long long)m_bytes);
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
    const QRect axRect(int(pos.x), int(pos.y), int(size.width), int(size.height));
    const QRect r = visualIconRect(axRect);

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
        if (inAxSpace.intersects(axRect))
            onScreen = true;
    }

    // Drop to a lazy poll when there's nothing to follow.
    // Frame-rate tracking only while something is actually being drawn.
    // Off screen, or with nothing to draw, once a second is plenty.
    const int want = (onScreen && m_animating) ? kFastPollMs : kSlowPollMs;
    if (qEnvironmentVariableIsSet("HYPERBIN_DEBUG") && want != m_poll.interval())
        qInfo("hyperbin: icon poll -> %dms (onScreen=%d animating=%d)",
              want, onScreen, m_animating);
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

QImage DockTrashTarget::iconImage(int px) const
{
    // The shell's own Trash artwork, drawn over the swarm so flies pass
    // behind the bin. Must be the named system images: iconForFile: on
    // ~/.Trash returns a generic document icon, not the trash.
    NSImage *icon = [NSImage imageNamed:(m_count > 0 ? NSImageNameTrashFull
                                                     : NSImageNameTrashEmpty)];
    if (!icon)
        return {};

    QImage out(px, px, QImage::Format_RGBA8888_Premultiplied);
    out.fill(Qt::transparent);

    // Render straight into the QImage's buffer through a CGBitmapContext.
    //
    // sRGB specifically: NSCalibratedRGBColorSpace renders noticeably
    // darker than the Dock's own drawing, which showed up as a tonal
    // mismatch between our copy and the icon underneath it. Measured with
    // an off/on screen diff over the icon region.
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef cg = CGBitmapContextCreate(
        out.bits(), px, px, 8, out.bytesPerLine(), cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    if (!cg)
        return {};

    NSGraphicsContext *ctx = [NSGraphicsContext graphicsContextWithCGContext:cg
                                                                     flipped:NO];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:ctx];
    [icon drawInRect:NSMakeRect(0, 0, px, px)
            fromRect:NSZeroRect
           operation:NSCompositingOperationSourceOver
            fraction:1.0];
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(cg);

    // No flip. AppKit's drawInRect: through a non-flipped NSGraphicsContext
    // already lands the pixels top-down in the buffer, matching QImage.
    // Verified by dumping this image and comparing against the system
    // artwork — flipping the CTM *or* mirroring afterwards each produced
    // an upside-down bin.
    //
    // NOT cropped. The artwork's own transparent padding is part of how
    // the Dock lays the icon out, so the image and the rect it's mapped
    // onto must describe the same square. Cropping to the alpha bounds
    // made the mask's apparent size depend on the crop threshold, which
    // then had to be cancelled out by a fudge in sizeK — two constants
    // fighting, and the reason the size never settled.
    return out;
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
