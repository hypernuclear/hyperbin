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

namespace hyperbin {

class DockTrashTarget : public TrashTarget
{
    Q_OBJECT
public:
    explicit DockTrashTarget(QObject *parent = nullptr);
    ~DockTrashTarget() override;

    Status status() const override { return m_status; }
    QRect  iconRect() const override { return m_rect; }
    int    itemCount() const override { return m_count; }
    void   start() override;
    void   stop() override;
    void   openRemediation() override;
    QImage iconImage(int px) const override;

    /// True once the user has granted Accessibility. Prompts on first call
    /// if `prompt` is set — only do that from onboarding, never silently.
    static bool accessibilityGranted(bool prompt = false);

private:
    void beginTracking();  // the real start, once permission exists
    void pollIconRect();   // one AX round-trip; cheap enough at 1Hz
    void pollTrash();      // stat is free; the real count only on a change
    void setStatus(Status s);

    /// Item count via Finder Apple Events. Needs Automation permission —
    /// a prompt the user can say yes to — rather than Full Disk Access,
    /// which is what reading ~/.Trash directly would require. Returns -1
    /// if Finder refused or wasn't reachable.
    static int countViaFinder();

    struct Impl;           // holds the AXUIElementRef, kept out of the header
    Impl  *d = nullptr;

    QRect     m_rect;
    int       m_count  = 0;
    Status    m_status = Status::NotFound;
    // Fast enough to follow Dock magnification, which resizes the tile
    // continuously while the pointer is over it.
    static constexpr int kFastPollMs = 50;
    static constexpr int kSlowPollMs = 1000;

    QTimer    m_poll;
    QTimer    m_permissionWatch; // runs only while waiting to be granted
    int       m_trashTick = 0;   // divides the icon poll down for the count
    QDateTime m_trashMTime;
    bool      m_directRead = false; // true when ~/.Trash is readable outright
};

} // namespace hyperbin
