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

    const float rate = want > m_level ? params.creepRate : params.recedeRate;
    const float delta = want - m_level;
    const float stepBy = rate * dt;
    m_level = std::abs(delta) <= stepBy ? want
                                        : m_level + (delta > 0 ? stepBy : -stepBy);
    if (m_level < 0.0005f)
        m_level = 0.0f;

    // Time only advances while there is something to animate, so a clean
    // bin leaves it where it was rather than accumulating a number that
    // does nothing.
    if (!isEmpty())
        m_time += dt;
}

} // namespace hyperbin
