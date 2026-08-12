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

    /// The animation shown over the bin. Only one for now; the enum
    /// exists so adding a second doesn't mean reworking storage.
    enum class Type {
        Flies = 0,
    };
    Q_ENUM(Type)

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

    bool      enabled() const   { return m_enabled; }
    Type      type() const      { return m_type; }
    Density   density() const   { return m_density; }
    Threshold threshold() const { return m_threshold; }

    void setEnabled(bool on);
    void setType(Type t);
    void setDensity(Density d);
    void setThreshold(Threshold t);

    /// Threshold in bytes.
    qint64 thresholdBytes() const;
    static qint64 thresholdBytes(Threshold t);

    /// Fullness 0-1 for the current density, given the bin's contents.
    /// Fixed densities ignore both arguments — that's the point of them.
    double fullnessFor(qint64 trashBytes, int itemCount) const;

signals:
    void enabledChanged(bool on);
    /// Anything that changes how the swarm should look.
    void appearanceChanged();

private:
    QSettings m_store;
    bool      m_enabled;
    Type      m_type;
    Density   m_density;
    Threshold m_threshold;
};

} // namespace hyperbin
