// The whole platform surface, in one interface.
//
// macOS and Windows share nothing here: the Dock's trash is found via the
// Accessibility API, the Recycle Bin via the shell's desktop list view.
// Everything above this line is portable; everything below it is not.
#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <memory>

namespace hyperbin {

/// Where the trash icon is on screen, and how full it is.
class TrashTarget : public QObject
{
    Q_OBJECT
public:
    ~TrashTarget() override = default;

    /// Native impl for this platform. Never null — returns a stub that
    /// reports Unavailable when the real thing can't be built.
    static std::unique_ptr<TrashTarget> create(QObject *parent = nullptr);

    enum class Status {
        Ok,
        PermissionRequired, // macOS: Accessibility not granted
        IconHidden,         // Windows: Recycle Bin not shown on desktop
        NotFound,           // couldn't locate it; transient or unsupported shell
    };
    Q_ENUM(Status)

    virtual Status status() const = 0;

    /// Screen rect of the icon, in device-independent pixels. Only
    /// meaningful when status() == Ok.
    virtual QRect iconRect() const = 0;

    /// Item count in the trash. Drives fullness.
    virtual int itemCount() const = 0;
    /// Total bytes in the trash, or -1 when it can't be read. Size is a
    /// better measure of "how much rubbish" than a count — one 4GB video
    /// outweighs forty screenshots — but on macOS reading ~/.Trash needs
    /// Full Disk Access, so callers must handle -1 rather than assume it.
    virtual qint64 byteSize() const { return -1; }

    /// Begin/stop watching. Nothing is polled until start() is called —
    /// an idle hyperbin must cost nothing (see docs/battery.md).
    virtual void start() = 0;
    virtual void stop()  = 0;

    /// Platform-specific remediation for a non-Ok status: opens the
    /// Accessibility pane, or Windows' desktop-icon settings.
    virtual void openRemediation() {}

    /// The bin's own artwork, cropped to its alpha bounds, at most `px`
    /// on a side. Used as an occlusion MASK — its alpha decides which fly
    /// pixels are erased, so flies pass behind the bin while the shell
    /// keeps drawing the icon itself. Null if unavailable.
    virtual QImage iconImage(int px) const { Q_UNUSED(px); return {}; }

signals:
    /// Icon moved or resized — the overlay must follow.
    void iconRectChanged(const QRect &r);
    /// Item count changed.
    void itemCountChanged(int count);
    /// Trash size changed, in bytes; -1 when unreadable.
    void byteSizeChanged(qint64 bytes);
    void statusChanged(Status s);

protected:
    using QObject::QObject;
};

} // namespace hyperbin
