// Opt-in usage analytics.
//
// Off until the user turns it on, and off is the real thing: the SDK is
// not initialised, no device ID is computed, and no request is made. The
// switch is in the menu bar next to everything else the app can be told
// to stop doing.
//
// What gets sent is deliberately thin — which effect is running, how
// often the bin gets emptied, what platform this is. There is nothing
// here about file names, paths, or what the user actually threw away,
// and there is no code path that could send them.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace hyperbin {

class Analytics : public QObject
{
    Q_OBJECT
public:
    explicit Analytics(QObject *parent = nullptr) : QObject(parent) {}
    ~Analytics() override = default;

    /// The analytics backend for this build, or null when there isn't
    /// one — no app key was compiled in, or analytics was configured
    /// out. Null is normal: it is what every local developer build
    /// returns, and callers hide the menu entry rather than showing a
    /// switch that does nothing.
    static Analytics *create(QObject *parent = nullptr);

    /// Whether the user has opted in. Persisted.
    virtual bool enabled() const = 0;

    /// Turn collection on or off. Turning it on initialises the SDK on
    /// first use and starts a session; turning it off ends the session
    /// and stops everything.
    virtual void setEnabled(bool on) = 0;

    /// Record a named event. A no-op while disabled.
    virtual void event(const QString &name,
                       const QVariantMap &segmentation = {}) = 0;

    /// Set a property describing this install (platform, version, and
    /// so on). A no-op while disabled.
    virtual void property(const QString &key, const QString &value) = 0;

signals:
    void enabledChanged(bool on);
};

/// The event names, in one place.
///
/// Spelled out as constants rather than string literals at each call
/// site: a typo in an event name does not fail, it silently creates a
/// second event that looks almost like the first, and the mistake only
/// surfaces weeks later in a dashboard nobody can reconcile.
namespace Ev {
inline constexpr auto kLaunch      = "launch";
inline constexpr auto kEffect      = "effect_changed";
inline constexpr auto kDensity     = "density_changed";
inline constexpr auto kThreshold   = "threshold_changed";
inline constexpr auto kEnabled     = "enabled_toggled";
inline constexpr auto kLoginItem   = "login_item_toggled";
inline constexpr auto kEmptied     = "bin_emptied";
inline constexpr auto kPermission  = "permission_granted";
inline constexpr auto kSplashShown = "splash_shown";
} // namespace Ev

/// User-property keys, same reasoning as the event names.
namespace Prop {
inline constexpr auto kVersion   = "app_version";
inline constexpr auto kOs        = "os";
inline constexpr auto kOsVersion = "os_version";
inline constexpr auto kEffect    = "effect";
inline constexpr auto kLoginItem = "login_item";
} // namespace Prop

} // namespace hyperbin
