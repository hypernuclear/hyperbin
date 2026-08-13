#include "OozeSim.h"

#include <algorithm>
#include <cmath>

namespace hyperbin {

int OozeSim::marginX(qreal iconSize)
{
    // Drips hang straight down, so sideways room is only for the coating
    // itself plus its own thickness.
    return int(qMax(8.0, 0.14 * iconSize));
}

int OozeSim::marginTop(qreal iconSize)
{
    // The coating starts at the rim and works down; nothing rises above.
    return int(qMax(4.0, 0.06 * iconSize));
}

int OozeSim::marginBottom(qreal iconSize)
{
    // Where the drips hang and the drops fall away — the one direction
    // that matters, and the one with least room on a bottom Dock.
    // Derived from the drip's own reach rather than guessed, plus a
    // little for the puddle underneath.
    return int(qMax(20.0, iconSize * (kDripReach * kDripStretch + 0.14)));
}

void OozeSim::setFullness(float f)
{
    m_target = std::clamp(f, 0.0f, 1.0f);
}

void OozeSim::step(float dt)
{
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (dt <= 0.0f)
        return;

    // Below startAt the bin is clean. Squared above it, so a nearly-empty
    // bin stays clean and a full one is unmistakable.
    float want = 0.0f;
    if (m_target > params.startAt) {
        const float t = (m_target - params.startAt) / (1.0f - params.startAt);
        const float shape = std::pow(t, params.ramp);
        want = params.maxLevel
               * (params.minShare + (1.0f - params.minShare) * shape);
    }

    // Critically damped, so it eases at both ends and never overshoots.
    //
    // This used to be a constant rate, which is a straight line: the
    // level left at full speed, held it the whole way and stopped dead
    // the instant it arrived. On something the size of a Dock icon that
    // reads as a bar filling rather than as a substance rising. A spring
    // starts at nothing, gathers pace and settles in, which is what the
    // eye expects of anything with mass.
    //
    // Velocity first, then position — semi-implicit, which stays stable
    // at the frame intervals this runs at where the explicit form would
    // not.
    const float omega = want > m_level ? params.creepEase : params.recedeEase;
    m_vel += (omega * omega * (want - m_level) - 2.0f * omega * m_vel) * dt;
    m_level += m_vel * dt;
    // A spring approaches its target asymptotically and would rebuild the
    // mesh forever chasing the last thousandth. Close and slow is
    // arrived.
    if (std::abs(want - m_level) < 0.002f && std::abs(m_vel) < 0.02f) {
        m_level = want;
        m_vel = 0.0f;
    }
    m_level = std::clamp(m_level, 0.0f, 1.0f);
    if (m_level < 0.0005f)
        m_level = 0.0f;

    // Time only advances while there is something to animate, so a clean
    // bin leaves it where it was rather than accumulating a number that
    // does nothing.
    if (!isEmpty())
        m_time += dt;
}

} // namespace hyperbin
