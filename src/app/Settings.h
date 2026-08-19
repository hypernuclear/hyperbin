// User-visible configuration, and the only place defaults live.
//
// Deliberately split into "what kind of animation" and "settings for that
// animation": flies are the first type, not the only planned one, so
// anything fly-specific is namespaced under its type rather than sitting
// at the top level where a second type would have to fight it.
#pragma once

#include <QObject>
#include <QSettings>

namespace hyperbin {

class Settings : public QObject
{
    Q_OBJECT
public:
    /// `appName` selects the preference store. It exists so tests can
    /// use a throwaway one — a test that writes to the real store would
    /// silently rewrite the user's menu choices every time it runs.
    explicit Settings(QObject *parent = nullptr,
                      const QString &appName = QStringLiteral("hyperbin"));
    /// Delete everything in this store. Tests only.
    void clearStore() { m_store.clear(); }


    /// How many flies, independent of what's actually in the bin. The
    /// three fixed steps are thirds, so they line up with the relative
    /// mode's scale rather than being an unrelated set of numbers.
    enum class Density {
        Few     = 0,  // 33%
        Lots    = 1,  // 66%
        TooMany = 2,  // 100%
        Relative= 3,  // scaled by trash size against the threshold below
    };
    Q_ENUM(Density)

    /// The trash size that counts as "full" in Relative density.
    enum class Threshold {
        OneMB      = 0,
        HundredMB  = 1,
        OneGB      = 2,
        FiveGB     = 3,
    };
    Q_ENUM(Threshold)

    /// Low Power Mode, as a THREE-way choice rather than a reading of the
    /// battery.
    ///
    /// "On battery" was the wrong question. Somebody on mains may want the
    /// thing calm, and somebody unplugged may not care — the two are only
    /// correlated. So this asks what the user wants, with Auto for the
    /// common case of "do whatever the OS is doing".
    enum class LowPower {
        Auto = 0,   ///< follow the OS's own Low Power Mode / Battery Saver
        On   = 1,
        Off  = 2,
    };
    Q_ENUM(LowPower)
    bool      enabled() const   { return m_enabled; }
    LowPower  lowPower() const  { return m_lowPower; }
    /// Which effect is running, by id — an opaque string, not an enum
    /// ordinal: ordinals silently change meaning when the list is
    /// reordered, and this list is expected to grow. Empty means "not
    /// chosen"; EffectRegistry resolves that to its default. Settings
    /// knows nothing about which effects exist, on purpose.
    QString   infestation() const { return m_infestation; }
    Density   density() const   { return m_density; }
    Threshold threshold() const { return m_threshold; }

    void setEnabled(bool on);
    void setLowPower(LowPower m);
    void setInfestation(const QString &id);
    void setDensity(Density d);
    void setThreshold(Threshold t);

    /// Threshold in bytes.
    qint64 thresholdBytes() const;
    static qint64 thresholdBytes(Threshold t);

    /// Fullness 0-1 for the current density, given the bin's contents.
    /// Fixed densities ignore both arguments — that's the point of them.
    double fullnessFor(qint64 trashBytes, int itemCount) const;

    /// Whether first-run setup has already happened. Guards anything the
    /// app does to the user's machine ONCE — see main.cpp, where it is
    /// what stops the login item being re-registered on every launch
    /// after someone has deliberately turned it off.
    bool firstRunDone() const { return m_firstRunDone; }
    void setFirstRunDone();
signals:
    void enabledChanged(bool on);
    void lowPowerChanged(LowPower m);
    void infestationChanged(const QString &id);
    /// Anything that changes how the swarm should look.
    void appearanceChanged();
    /// The two appearance settings, reported individually as well.
    ///
    /// appearanceChanged() says "redraw"; these say WHICH knob moved, and
    /// the difference matters to the one caller that wants to know —
    /// analytics. Nine event names were declared in Analytics.h and only
    /// four were ever fired, and two of the dead ones were dead precisely
    /// because there was no signal here to hang them on.
    void densityChanged(Density d);
    void thresholdChanged(Threshold t);

private:
    /// Push the store to disk now. See the implementation for why every
    /// setter does this.
    void flush();
    QSettings m_store;
    bool m_firstRunDone = false;
    bool      m_enabled;
    LowPower  m_lowPower;
    QString   m_infestation;
    Density   m_density;
    Threshold m_threshold;
};

} // namespace hyperbin
