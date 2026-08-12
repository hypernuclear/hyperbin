#include "Settings.h"

#include <algorithm>

namespace hyperbin {

namespace {
constexpr qint64 kMB = 1024LL * 1024LL;
constexpr qint64 kGB = 1024LL * kMB;

template <typename E>
E readEnum(const QSettings &s, const char *key, E def, int maxValue)
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
    m_type      = readEnum(m_store, "type", Type::Flies, 0);
    // Relative by default: tying the swarm to how much rubbish is
    // actually there is the whole idea, and the fixed steps are for
    // people who want it to stop being clever.
    m_density   = readEnum(m_store, "flies/density", Density::Relative, 3);
    m_threshold = readEnum(m_store, "flies/threshold", Threshold::HundredMB, 3);
}

void Settings::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    m_store.setValue(QStringLiteral("enabled"), on);
    emit enabledChanged(on);
}

void Settings::setType(Type t)
{
    if (t == m_type)
        return;
    m_type = t;
    m_store.setValue(QStringLiteral("type"), int(t));
    emit appearanceChanged();
}

void Settings::setDensity(Density d)
{
    if (d == m_density)
        return;
    m_density = d;
    m_store.setValue(QStringLiteral("flies/density"), int(d));
    emit appearanceChanged();
}

void Settings::setThreshold(Threshold t)
{
    if (t == m_threshold)
        return;
    m_threshold = t;
    m_store.setValue(QStringLiteral("flies/threshold"), int(t));
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

    // Relative. Size is the better signal — one 4GB video is more rubbish
    // than forty screenshots — but it isn't always available: reading
    // ~/.Trash needs Full Disk Access, and without it the platform layer
    // can only get a count out of Finder. Fall back rather than showing
    // nothing.
    if (trashBytes < 0)
        return std::clamp(itemCount / 40.0, 0.0, 1.0);
    if (trashBytes == 0)
        return 0.0;
    return std::clamp(double(trashBytes) / double(thresholdBytes()), 0.0, 1.0);
}

} // namespace hyperbin
