#include "PowerPolicy.h"

namespace hyperbin {

namespace {
// Conserving runs at 30fps. NOT conserving runs at whatever the display
// does — see refreshMs — because a 120Hz screen should get 120Hz.
//
// 20fps was the original budget-driven guess and it read as choppy rather
// than twitchy once there was something to look at (Leo, 2026-08-09).
// Frame cadence is still the dominant power cost (docs/battery.md), which
// is exactly why the conserving path halves it rather than stopping: a
// paused novelty is forgiven, a drained battery is not.
constexpr int kLowPowerMs = 33;
} // namespace

PowerPolicy::PowerPolicy(QObject *parent)
    : QObject(parent)
{
}

void PowerPolicy::setBinEmpty(bool v)      { if (m_binEmpty      != v) { m_binEmpty      = v; bump(); } }
void PowerPolicy::setEffectIdle(bool v)    { if (m_effectIdle    != v) { m_effectIdle    = v; bump(); } }
void PowerPolicy::setEffectAtRest(bool v)  { if (m_effectAtRest  != v) { m_effectAtRest  = v; bump(); } }
void PowerPolicy::setTargetVisible(bool v) { if (m_targetVisible != v) { m_targetVisible = v; bump(); } }
void PowerPolicy::setDisplayAwake(bool v)  { if (m_displayAwake  != v) { m_displayAwake  = v; bump(); } }
void PowerPolicy::setLowPower(bool v)      { if (m_lowPower      != v) { m_lowPower      = v; bump(); } }
void PowerPolicy::setRefreshHz(qreal v)
{
    if (qFuzzyCompare(m_refreshHz, v) || v < 20.0)
        return;
    m_refreshHz = v;
    bump();
}
void PowerPolicy::setEnabled(bool v)       { if (m_enabled       != v) { m_enabled       = v; bump(); } }
void PowerPolicy::setDismissed(bool v)     { if (m_dismissed     != v) { m_dismissed     = v; bump(); } }

bool PowerPolicy::shouldRender() const
{
    // Hard stops: nothing to draw, or nobody to see it.
    if (!m_enabled)
        return false;
    // Pointer on the bin. The flies have already flown off, so there is
    // genuinely nothing to draw — this is not a throttle, it's a stop.
    if (m_dismissed)
        return false;
    if (!m_displayAwake || !m_targetVisible)
        return false;
    // An empty bin costs zero — but let a departing effect finish first.
    if (m_binEmpty && m_effectIdle)
        return false;
    return true;
}

int PowerPolicy::frameIntervalMs() const
{
    if (!shouldRender())
        return 0;
    // At rest is NOT the same as nothing to draw. The surface stays up
    // and the last frame stays on it; we simply stop producing new ones.
    if (m_effectAtRest)
        return 0;
    if (m_lowPower)
        return kLowPowerMs;
    // One frame per refresh. Floored at 4ms so a bogus rate cannot turn
    // the clock into a spin, and never SLOWER than the conserving rate,
    // which would make Low Power Mode speed things up on a 24Hz display.
    const int ms = int(qRound(1000.0 / m_refreshHz));
    return qBound(4, ms, kLowPowerMs);
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
