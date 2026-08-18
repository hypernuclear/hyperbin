// macOS trash target: the Dock's Trash item.
//
// Position comes from the Accessibility API — there is no public API for
// Dock item geometry. Count comes from ~/.Trash directly, which needs no
// permission at all; only the position does.
#pragma once

#include "TrashTarget.h"

#include <QDateTime>
#include <QRect>
#include <QTimer>

class QImage;

namespace hyperbin {

/// The shell's own Trash artwork, at a given size. Free-standing because
/// the preview harness needs it without a live Dock target behind it:
/// tuning an effect against a stand-in glyph and then finding it looks
/// wrong over the real icon is how several of these rounds were spent.
QImage macTrashIcon(int px, bool full);

class DockTrashTarget : public TrashTarget
{
    Q_OBJECT
public:
    explicit DockTrashTarget(QObject *parent = nullptr);
    ~DockTrashTarget() override;

    Status status() const override { return m_status; }
    QRect  iconRect() const override { return m_rect; }
    int    itemCount() const override { return m_count; }
    qint64 byteSize() const override { return m_bytes; }
    void   start() override;
    void   stop() override;
    void   setAnimating(bool animating) override;
    void   openRemediation() override;
    QList<Permission> permissions() const override;
    void   openPermission(const QString &id) override;
    void   refreshPermissions() override;
    QImage iconImage(int px) const override;

    /// True once the user has granted Accessibility. Prompts on first call
    /// if `prompt` is set — only do that from onboarding, never silently.
    static bool accessibilityGranted(bool prompt = false);
    /// True once the user has granted Full Disk Access, tested by
    /// ATTEMPTING to list the trash.
    ///
    /// The attempt matters as much as the answer. There is no API to
    /// prompt for Full Disk Access — the most an app can do is send the
    /// user to the settings pane — and an app only appears in that pane
    /// once it has actually tried to touch something protected. So this
    /// call is what puts hyperbin in the list for the user to switch on;
    /// without it they would have to add the bundle by hand with the
    /// "+" button.
    static bool fullDiskAccessGranted();

private:
    void beginTracking();  // the real start, once permission exists
    void pollIconRect();   // one AX round-trip; cheap enough at 1Hz
    void pollTrash();      // stat is free; the real count only on a change
    void setStatus(Status s);


    struct Impl;           // holds the AXUIElementRef, kept out of the header
    Impl  *d = nullptr;

    QRect     m_rect;
    int       m_count  = 0;
    qint64    m_bytes  = -1;  // -1 = ~/.Trash not readable
    Status    m_status = Status::NotFound;
    // Frame rate, not a compromise: one AX position+size round-trip was
    // measured at 0.039ms, so 60Hz costs ~0.2% of a core. Polling slower
    // than the render clock is what made the overlay visibly lag the
    // Dock during magnification.
    static constexpr int kFastPollMs = 16;
    static constexpr int kSlowPollMs = 1000;
    /// How far the icon must move before the overlay follows it, pixels.
    ///
    /// One. The Dock does not keep its items on integer pixels and hovering
    /// nudges the layout, so the rounded rect flips between two neighbouring
    /// values — and each flip moves the overlay window, which reads as the
    /// effect shivering against an icon that is standing still. A pixel of
    /// lag cannot be seen; a pixel of shimmer at 60Hz is the first thing
    /// you see. Position only: see the emit in pollIconRect.
    static constexpr int kRectDeadbandPx = 1;

    QTimer    m_poll;
    QTimer    m_permissionWatch; // runs only while waiting to be granted
    int       m_trashTick = 0;   // divides the icon poll down for the count
    QDateTime m_trashMTime;
    QTimer    m_diskWatch;   // runs only while waiting for Full Disk Access
    /// Cached, because asking is not free: the honest test for disk
    /// access is a directory listing and the honest test for
    /// Accessibility is a Dock round-trip. The watchers above own these.
    bool      m_hasAx   = false;
    bool      m_hasDisk = false;
    bool      m_animating  = true;  // drives the fast/slow poll choice
};

} // namespace hyperbin
