#include "Settings.h"

#include <algorithm>
#include <utility>

namespace hyperbin {

namespace {
constexpr qint64 kMB = 1024LL * 1024LL;
constexpr qint64 kGB = 1024LL * kMB;

template <typename E>
[[maybe_unused]] E readEnum(const QSettings &s, const char *key, E def, int maxValue)
{
    const int v = s.value(QString::fromLatin1(key), int(def)).toInt();
    return (v >= 0 && v <= maxValue) ? E(v) : def;
}
} // namespace

Settings::Settings(QObject *parent, const QString &appName)
    : QObject(parent)
    , m_store(QStringLiteral("Hypernuclear"), appName)
{
    m_enabled   = m_store.value(QStringLiteral("enabled"), true).toBool();
    // Migration: the first build stored an enum ordinal under "type", and
    // only one value was ever written. Drop it; an unset infestation means
    // "whatever the registry considers default", which is the same thing.
    m_store.remove(QStringLiteral("type"));
    // Amount and threshold were briefly namespaced under the effect. They
    // describe how much of WHATEVER is running, so they moved up a level;
    // carry the old values across rather than resetting people.
    for (const auto &pair : {std::pair{"flies/density", "density"},
                             std::pair{"flies/threshold", "threshold"}}) {
        const QString from = QString::fromLatin1(pair.first);
        const QString to   = QString::fromLatin1(pair.second);
        if (m_store.contains(from) && !m_store.contains(to))
            m_store.setValue(to, m_store.value(from));
        m_store.remove(from);
    }
    // Stored as an opaque id. Settings deliberately does NOT know which
    // effects exist — that list lives in EffectRegistry, which pulls in
    // the renderer, and dragging that into a value store would drag it
    // into the headless tests too. An unknown or empty id is resolved by
    // the registry at construction time.
    m_infestation = m_store.value(QStringLiteral("infestation")).toString();
    // Relative by default: tying the swarm to how much rubbish is
    // actually there is the whole idea, and the fixed steps are for
    // people who want it to stop being clever.
    m_density   = readEnum(m_store, "density", Density::Relative, 3);
    m_threshold = readEnum(m_store, "threshold", Threshold::HundredMB, 3);
}

void Settings::flush()
{
    // Push each change to disk as it is made rather than waiting for
    // QSettings to flush on its own schedule or on destruction.
    //
    // Belt and braces: on macOS the value reaches cfprefsd on setValue
    // anyway, and a cross-process test (see simtest) shows a killed
    // process still persists its settings without this. It matters on the
    // kill paths where that is not guaranteed — a debugger stop, a
    // logout — and it keeps the on-disk plist current for anything
    // reading it from outside. Four settings, changed by hand from a
    // menu; the cost is irrelevant.
    m_store.sync();
}
void Settings::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    m_store.setValue(QStringLiteral("enabled"), on);
    flush();
    emit enabledChanged(on);
}

void Settings::setInfestation(const QString &id)
{
    if (id == m_infestation || id.isEmpty())
        return;
    m_infestation = id;
    m_store.setValue(QStringLiteral("infestation"), id);
    flush();
    emit infestationChanged(id);
    emit appearanceChanged();
}

void Settings::setDensity(Density d)
{
    if (d == m_density)
        return;
    m_density = d;
    m_store.setValue(QStringLiteral("density"), int(d));
    flush();
    emit appearanceChanged();
}

void Settings::setThreshold(Threshold t)
{
    if (t == m_threshold)
        return;
    m_threshold = t;
    m_store.setValue(QStringLiteral("threshold"), int(t));
    flush();
    emit appearanceChanged();
}

qint64 Settings::thresholdBytes(Threshold t)
{
    switch (t) {
    case Threshold::OneMB:     return kMB;
    case Threshold::HundredMB: return 100 * kMB;
    case Threshold::OneGB:     return kGB;
    case Threshold::FiveGB:    return 5 * kGB;
    }
    return 100 * kMB;
}

qint64 Settings::thresholdBytes() const
{
    return thresholdBytes(m_threshold);
}

double Settings::fullnessFor(qint64 trashBytes, int itemCount) const
{
    switch (m_density) {
    case Density::Few:     return 1.0 / 3.0;
    case Density::Lots:    return 2.0 / 3.0;
    case Density::TooMany: return 1.0;
    case Density::Relative:
        break;
    }

    // Relative, and measured in BYTES. One 4GB video is more rubbish than
    // forty screenshots, so the count is a poor signal and is no longer
    // used for this — the platform layer either has Full Disk Access and
    // knows the size, or reports nothing and the effect does not run.
    //
    // There used to be a count-based fallback here for the case where
    // only Finder could be asked. Finder can size files but returns
    // "missing value" for folders, so a trashed app bundle or project
    // directory weighed nothing at all; and the fallback ignored the
    // threshold entirely, so the whole Trash Threshold menu silently did
    // nothing on a stock Mac.
    Q_UNUSED(itemCount);
    if (trashBytes < 0)
        return 0.0;
    if (trashBytes == 0)
        return 0.0;
    return std::clamp(double(trashBytes) / double(thresholdBytes()), 0.0, 1.0);
}

bool Settings::splashSeen() const
{
    return m_store.value(QStringLiteral("splashSeen"), false).toBool();
}
void Settings::setSplashSeen()
{
    m_store.setValue(QStringLiteral("splashSeen"), true);
    // Flushed now rather than on destruction: a splash that reappears
    // because the app was killed before its preferences were written is
    // exactly the kind of thing nobody would ever manage to reproduce.
    m_store.sync();
}
} // namespace hyperbin
