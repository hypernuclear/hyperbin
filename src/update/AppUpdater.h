// The seam between the app and whichever updater the platform has.
//
// macOS gets Sparkle, Windows gets WinSparkle, and everywhere else gets
// nothing at all — `create()` returns null and the menu entry stays
// hidden. Both real backends read the same appcast format and verify the
// same EdDSA signature, so one feed generator serves both.
#pragma once

#include <QObject>
#include <QString>

namespace hyperbin {

class AppUpdater : public QObject
{
    Q_OBJECT
public:
    explicit AppUpdater(QObject *parent = nullptr) : QObject(parent) {}
    ~AppUpdater() override = default;

    /// The updater for this platform, or null where there isn't one.
    ///
    /// Null is a normal answer, not a failure: it is what a Linux build
    /// returns, and what any build with HYPERBIN_AUTO_UPDATE=OFF
    /// returns. Callers hide their update UI rather than reporting an
    /// error.
    static AppUpdater *create(QObject *parent = nullptr);

    /// Whether a check can start right now. False while one is already
    /// running, or while an update is being installed.
    virtual bool canCheckForUpdates() const = 0;

public slots:
    /// A check the user asked for, with the platform's own progress and
    /// "you're up to date" UI. Distinct from the silent background check
    /// the updater runs on its own schedule, which says nothing unless
    /// there is something to say.
    virtual void checkForUpdates() = 0;

    /// Turn the background schedule on or off.
    virtual void setAutomaticChecksEnabled(bool enabled) = 0;

signals:
    void canCheckForUpdatesChanged(bool canCheck);
};

} // namespace hyperbin
