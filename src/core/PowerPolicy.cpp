#include "PowerPolicy.h"

namespace hyperbin {

namespace {
// 60fps on mains. 20fps was the original budget-driven guess and it read
// as choppy rather than twitchy once there was something to look at
// (Leo, 2026-08-09), so smoothness wins and we pay for it in frames.
// Frame cadence is still the dominant power cost (docs/battery.md), so
// this is the number to revisit if measurement comes back bad — and the
// idle rules matter more than ever now that active frames cost 3x.
constexpr int kActiveMs  = 16;
constexpr int kBatteryMs = 33; // halve the rate off mains
} // namespace

PowerPolicy::PowerPolicy(QObject *parent)
    : QObject(parent)
{
}

void PowerPolicy::setBinEmpty(bool v)      { if (m_binEmpty      != v) { m_binEmpty      = v; bump(); } }
void PowerPolicy::setSwarmIdle(bool v)     { if (m_swarmIdle     != v) { m_swarmIdle     = v; bump(); } }
void PowerPolicy::setTargetVisible(bool v) { if (m_targetVisible != v) { m_targetVisible = v; bump(); } }
void PowerPolicy::setDisplayAwake(bool v)  { if (m_displayAwake  != v) { m_displayAwake  = v; bump(); } }
void PowerPolicy::setOnBattery(bool v)     { if (m_onBattery     != v) { m_onBattery     = v; bump(); } }
void PowerPolicy::setLowPowerMode(bool v)  { if (m_lowPower      != v) { m_lowPower      = v; bump(); } }

bool PowerPolicy::shouldRender() const
{
    // Hard stops: nothing to draw, or nobody to see it.
    if (!m_displayAwake || !m_targetVisible)
        return false;
    if (m_lowPower)
        return false;
    // An empty bin costs zero — but let an emptying swarm fly away first.
    if (m_binEmpty && m_swarmIdle)
        return false;
    return true;
}

int PowerPolicy::frameIntervalMs() const
{
    if (!shouldRender())
        return 0;
    return m_onBattery ? kBatteryMs : kActiveMs;
}

void PowerPolicy::bump()
{
    // Only signal when the observable outcome actually moved, so callers
    // don't restart timers on every unrelated input change.
    const bool r = shouldRender();
    const int  i = frameIntervalMs();
    if (r == m_lastRender && i == m_lastInterval)
        return;
    m_lastRender   = r;
    m_lastInterval = i;
    emit changed();
}

} // namespace hyperbin
