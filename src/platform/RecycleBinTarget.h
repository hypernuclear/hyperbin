// Windows trash target: the Recycle Bin on the desktop.
//
// Deliberately the same shape as DockTrashTarget, but almost nothing is
// shared underneath. Two differences drive the design:
//
//   - Fullness is FREE and complete. SHQueryRecycleBin hands over the item
//     count and the byte size together, with no permission at all, so
//     byteSize() returns a real number here — unlike macOS, where it stays
//     -1 without Full Disk Access.
//   - The icon barely moves. There is no Dock magnification to chase, so
//     this polls at 1Hz rather than at frame rate, and leans on shell
//     change notifications for the things that DO move.
#pragma once

#include "TrashTarget.h"

#include <QRect>
#include <QTimer>

namespace hyperbin {

class RecycleBinTarget : public TrashTarget
{
    Q_OBJECT
public:
    explicit RecycleBinTarget(QObject *parent = nullptr);
    ~RecycleBinTarget() override;

    Status status() const override { return m_status; }
    QRect  iconRect() const override { return m_rect; }
    int    itemCount() const override { return m_count; }
    qint64 byteSize() const override { return m_bytes; }
    void   start() override;
    void   stop() override;
    void   setAnimating(bool animating) override;
    void   openRemediation() override;
    QImage iconImage(int px) const override;

    /// Elevated processes cannot talk to explorer.exe over COM — the
    /// integrity levels differ, so the desktop view simply isn't found and
    /// every symptom looks like "the Recycle Bin has vanished". Checked
    /// once at start() so that failure reports itself instead of being
    /// debugged.
    static bool runningElevated();

    /// Re-read the bin. Public because the shell's change notification
    /// arrives at a plain WndProc, which has no other way back in.
    void refreshFullness();

private:
    void pollIconRect();     // one COM round-trip; ~1Hz is plenty
    void queryFullness();    // SHQueryRecycleBin; emits only on a change
    void setStatus(Status s);
    /// Recycle Bin specifically switched off in Desktop Icon Settings.
    static bool hiddenByDesktopIconSetting();
    void watchHiddenSetting();

    struct Impl;             // COM interfaces and HWNDs, kept out of the header
    Impl  *d = nullptr;

    QRect  m_rect;
    int    m_count  = 0;
    qint64 m_bytes  = -1;
    Status m_status = Status::NotFound;

    // Desktop icons move on an icon-size change, a rearrange, or a DPI
    // change — never per frame. Copying the Dock's 16ms poll here would
    // burn a COM round-trip 60 times a second to watch something that is
    // still. The screen-geometry signals do the real work; this is the
    // safety net under them.
    static constexpr int kPollMs     = 1000;
    static constexpr int kIdlePollMs = 4000;

    QTimer m_poll;
    bool   m_animating = true;
};

} // namespace hyperbin
