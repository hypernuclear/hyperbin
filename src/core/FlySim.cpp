#include "FlySim.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace hyperbin {

namespace {
// Clamp a vector's length without a sqrt when it's already short enough.
QPointF limit(const QPointF &v, float max)
{
    const float l2 = float(v.x() * v.x() + v.y() * v.y());
    if (l2 <= max * max || l2 == 0.0f)
        return v;
    const float l = std::sqrt(l2);
    return QPointF(v.x() / l * max, v.y() / l * max);
}
} // namespace

FlySim::FlySim(uint32_t seed)
    : m_rngState(seed ? seed : 1u)
{
}

// xorshift32 — deterministic across platforms, unlike std::mt19937 whose
// distributions differ between libstdc++ and libc++. Determinism is what
// makes the visual regression test reproducible on CI.
float FlySim::rnd()
{
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return float(m_rngState & 0x00FFFFFFu) / float(0x01000000u);
}

void FlySim::setBinRect(const QRectF &r)
{
    // Carry the swarm with the icon. Without this the flies keep their
    // window-local coordinates while the window itself moves and grows
    // (the margin scales with the icon), so magnifying the Dock throws
    // the whole swarm off to one side.
    if (!m_bin.isNull() && !r.isNull() && !m_flies.isEmpty()) {
        const QPointF oldC = m_bin.center();
        const QPointF newC = r.center();
        const qreal oldD = qMax(m_bin.width(), m_bin.height());
        const qreal newD = qMax(r.width(), r.height());
        const float s = (oldD > 1.0 && newD > 1.0) ? float(newD / oldD) : 1.0f;

        for (Fly &f : m_flies) {
            f.pos = newC + (f.pos - oldC) * s;
            f.vel *= s;
        }
    }
    m_bin = r;
}

void FlySim::setFullness(float f)
{
    m_fullnessTarget = std::clamp(f, 0.0f, 1.0f);
}

int FlySim::desiredCount() const
{
    if (m_fullness < 0.02f)
        return 0;
    // Ramp in fast at the low end so the first item thrown away is
    // visible immediately — one lonely fly reads as intentional.
    const float curved = std::pow(m_fullness, 0.7f);
    return std::max(1, int(std::lround(curved * params.maxFlies)));
}

void FlySim::spawnFly()
{
    // Enter from off-screen so flies arrive rather than pop into being.
    const QPointF c = m_bin.center();
    const float ang = rndRange(0.0f, float(2 * M_PI));
    const float r   = params.roamRadius * sizeScale() * rndRange(2.0f, 2.8f);

    Fly f;
    f.pos         = c + QPointF(std::cos(ang) * r, std::sin(ang) * r);
    f.vel         = QPointF(-std::cos(ang), -std::sin(ang)) * params.minSpeed;
    f.wanderAngle = ang;
    f.phase       = rndRange(0.0f, float(2 * M_PI));
    f.scale       = rndRange(0.8f, 1.2f);
    m_flies.push_back(f);
}

void FlySim::step(float dt)
{
    // Guard against the first frame after a wake, where dt can be huge.
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (dt <= 0.0f)
        return;

    // Ease fullness so a bulk delete swells the swarm instead of snapping.
    m_fullness += (m_fullnessTarget - m_fullness)
                  * std::min(1.0f, params.easeRate * dt);

    const int want = desiredCount();
    if (m_flies.size() < want)
        spawnFly();               // one per step: the swarm gathers visibly

    // Scale everything spatial off the icon so a magnified or resized
    // Dock tile keeps the swarm in proportion instead of leaving the
    // flies orbiting at a fixed radius round a much bigger bin.
    const float k         = sizeScale();
    const QPointF c       = m_bin.center();
    // Roam region follows the icon's own shape. Falls back to the radius
    // tunable when there's no sensible rect (headless tests, first frame).
    const float roamR     = params.roamRadius * k;
    const float roamX     = m_bin.width()  > 2.0 ? float(m_bin.width())  * 0.58f : roamR;
    const float roamY     = m_bin.height() > 2.0 ? float(m_bin.height()) * 0.58f : roamR;
    const float separation= params.separation * k;
    const float speedMax  = (params.minSpeed
                          + (params.maxSpeed - params.minSpeed) * m_fullness) * k;
    const bool  shrinking = m_flies.size() > want;

    for (int i = 0; i < m_flies.size(); ++i) {
        Fly &f = m_flies[i];

        // 1. Wander — heading drifts by a random walk. This is what makes
        //    the motion read as alive rather than mechanical.
        f.wanderAngle += rndRange(-1.0f, 1.0f) * params.wanderStrength * dt
                         * (0.5f + m_fullness);
        QPointF steer(std::cos(f.wanderAngle), std::sin(f.wanderAngle));
        steer *= speedMax * 0.6f;

        // 2. Stay over the bin. Not a ring to orbit — a region to remain
        //    inside, so they crawl across the icon instead of circling
        //    it at a fixed distance. The region is an ellipse matching
        //    the icon's own proportions: a circle would let them spill
        //    above and below a Dock tile that's wider than it is tall,
        //    onto the neighbouring icons.
        QPointF toC   = c - f.pos;
        const float d = float(std::hypot(toC.x(), toC.y()));
        if (d > 0.001f) {
            const QPointF dir = toC / d;
            if (shrinking && i >= want) {
                steer -= dir * speedMax * 2.0f;   // leaving: head out
            } else {
                const QPointF rel(f.pos.x() - c.x(), f.pos.y() - c.y());
                const float nx = float(rel.x()) / roamX;
                const float ny = float(rel.y()) / roamY;
                const float nd = std::sqrt(nx * nx + ny * ny);
                if (nd > 1.0f)
                    steer += dir * ((nd - 1.0f) * roamX * 6.0f + speedMax);
            }
        }

        // 3. Separation — cheap O(n^2), fine at n<=12.
        QPointF push(0, 0);
        for (int j = 0; j < m_flies.size(); ++j) {
            if (i == j) continue;
            QPointF diff = f.pos - m_flies[j].pos;
            const float dd = float(std::hypot(diff.x(), diff.y()));
            if (dd > 0.001f && dd < separation)
                push += (diff / dd) * (separation - dd);
        }
        steer += push * 6.0f;

        // 4. Occasional dart — the sharp direction changes that make
        //    something read as an insect rather than a drifting mote.
        if (rnd() < params.dartChance * (0.4f + m_fullness))
            f.vel += QPointF(rndRange(-1, 1), rndRange(-1, 1))
                     * params.dartImpulse * k;

        f.vel = limit(f.vel + steer * dt, speedMax);
        f.pos += f.vel * dt;
        f.phase += dt * 40.0f; // wingbeat
    }

    // Retire flies that have wandered far enough out to be invisible.
    if (shrinking) {
        const float cull = roamR * 3.5f;
        for (int i = m_flies.size() - 1; i >= want; --i) {
            const QPointF d = m_flies[i].pos - c;
            if (std::hypot(d.x(), d.y()) > cull)
                m_flies.removeAt(i);
        }
    }
}

} // namespace hyperbin
