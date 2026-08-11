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
    // An occupied bin always has at least one fly; past that the count
    // ramps to maxFlies. The curve is steep at the low end so throwing
    // away a single item is immediately visible.
    const float curved = std::pow(m_fullness, 0.7f);
    const int span = params.maxFlies - params.minFlies;
    return std::clamp(params.minFlies + int(std::lround(curved * span)),
                      params.minFlies, params.maxFlies);
}

void FlySim::enterMode(Fly &f, FlyMode m)
{
    f.mode = m;
    f.modeLeft = (m == FlyMode::Crawling) ? rndRange(params.crawlMin, params.crawlMax)
                                          : rndRange(params.flyMin, params.flyMax);
}

void FlySim::spawnFly()
{
    // Materialise inside the roam region and fade in. Flying in from
    // outside looks better in principle, but the approach flight pushed
    // flies beyond the overlay and clipped them at the window edge.
    const QPointF c  = m_bin.center();
    const float k    = sizeScale();
    const float ang  = rndRange(0.0f, float(2 * M_PI));
    const float halfW = m_bin.width()  > 2.0 ? float(m_bin.width())  * 0.5f : 20.0f * k;
    const float halfH = m_bin.height() > 2.0 ? float(m_bin.height()) * 0.5f : 14.0f * k;

    Fly f;
    f.pos = c + QPointF(std::cos(ang) * halfW * 2.0f * params.roamX * rndRange(0.2f, 0.9f),
                        std::sin(ang) * halfH * 2.0f * params.roamUp * rndRange(0.2f, 0.8f));
    f.vel = QPointF(std::cos(ang), std::sin(ang)) * params.flySpeed * k * 0.4f;
    f.wanderAngle = ang;
    f.phase       = rndRange(0.0f, float(2 * M_PI));
    f.scale       = rndRange(0.8f, 1.2f);
    f.life        = rndRange(params.lifeMin, params.lifeMax);
    f.fade        = 0.0f;
    f.leaving     = false;
    enterMode(f, FlyMode::Flying);
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

    // Count only the flies that are staying — a departing one has already
    // given up its slot, so its replacement overlaps with it and the
    // swarm never visibly dips.
    int staying = 0;
    for (const Fly &f : m_flies)
        if (!f.leaving)
            ++staying;
    // Cap on the TOTAL list, not just the ones staying: a departing fly
    // is still on screen, and counting only 'staying' let the visible
    // count overshoot maxFlies while replacements overlapped leavers.
    if (staying < want && m_flies.size() < params.maxFlies)
        spawnFly();

    // Everything spatial scales off the icon, so Dock magnification and
    // the user's Dock-size setting both come out right for free.
    const float k          = sizeScale();
    const QPointF c        = m_bin.center();
    const float separation = params.separation * k;
    const float halfW      = m_bin.width()  > 2.0 ? float(m_bin.width())  * 0.5f : 20.0f * k;
    const float halfH      = m_bin.height() > 2.0 ? float(m_bin.height()) * 0.5f : 14.0f * k;
    const float roamX      = halfW * 2.0f * params.roamX;
    const float roamUp     = halfH * 2.0f * params.roamUp;
    const float roamDown   = halfH * 2.0f * params.roamDown;

    for (int i = 0; i < m_flies.size(); ++i) {
        Fly &f = m_flies[i];

        // --- lifecycle -------------------------------------------------
        if (!f.leaving) {
            f.life -= dt;
            if (f.life <= 0.0f) {
                f.leaving = true;
                enterMode(f, FlyMode::Flying); // always leaves on the wing
            }
        }
        // Fade in on arrival, out on departure.
        const float target = f.leaving ? 0.0f : 1.0f;
        f.fade += (target - f.fade) * std::min(1.0f, dt / params.fadeTime);

        f.modeLeft -= dt;
        if (!f.leaving && f.modeLeft <= 0.0f)
            enterMode(f, f.mode == FlyMode::Crawling ? FlyMode::Flying
                                                     : FlyMode::Crawling);

        const bool crawling = !f.leaving && f.mode == FlyMode::Crawling;
        const float speedMax =
            (crawling ? params.crawlSpeed
                      : params.flySpeed * (0.65f + 0.35f * m_fullness)) * k;

        // --- steering --------------------------------------------------
        // 1. Wander. A crawling fly turns much more sharply than a flying
        //    one, which is what sells the difference at these speeds.
        f.wanderAngle += rndRange(-1.0f, 1.0f) * params.wanderStrength * dt
                         * (crawling ? 3.0f : 1.0f) * (0.5f + m_fullness);
        QPointF steer(std::cos(f.wanderAngle), std::sin(f.wanderAngle));
        steer *= speedMax * 0.6f;

        const QPointF rel(f.pos.x() - c.x(), f.pos.y() - c.y());

        {
            // 2. Stay over the bin. The region is an ellipse matching the
            //    icon's proportions — tight left/right so they don't
            //    stray onto neighbouring Dock icons, taller upward so
            //    they can rise off the bin.
            const float ry = rel.y() < 0 ? roamUp : roamDown;
            const float nx = float(rel.x()) / roamX;
            const float ny = float(rel.y()) / ry;
            const float nd = std::sqrt(nx * nx + ny * ny);
            if (nd > 1.0f) {
                const float d = float(std::hypot(rel.x(), rel.y()));
                if (d > 0.001f)
                    steer -= (rel / d) * ((nd - 1.0f) * roamX * 7.0f + speedMax);
            }
        }

        // 3. Separation — cheap O(n^2), fine at n<=6.
        QPointF push(0, 0);
        for (int j = 0; j < m_flies.size(); ++j) {
            if (i == j) continue;
            QPointF diff = f.pos - m_flies[j].pos;
            const float dd = float(std::hypot(diff.x(), diff.y()));
            if (dd > 0.001f && dd < separation)
                push += (diff / dd) * (separation - dd);
        }
        steer += push * 6.0f;

        // 4. Twitches. A crawling fly makes quick sideways corrections;
        //    a flying one occasionally darts. Both are what read as
        //    "insect" rather than "drifting particle".
        if (crawling) {
            if (rnd() < params.jitterChance) {
                const float side = rnd() < 0.5f ? -1.0f : 1.0f;
                f.vel += QPointF(side, rndRange(-0.25f, 0.25f))
                         * params.jitterImpulse * k;
            }
        } else if (rnd() < params.dartChance * (0.4f + m_fullness)) {
            f.vel += QPointF(rndRange(-1, 1), rndRange(-1, 1))
                     * params.dartImpulse * k;
        }

        f.vel = limit(f.vel + steer * dt, speedMax);
        f.pos += f.vel * dt;
        // Crawling flies beat their wings far less.
        f.phase += dt * (crawling ? 9.0f : 40.0f);
    }

    // Retire faded-out flies. Culling on fade alone (rather than on
    // distance travelled) keeps every fly inside the roam region for its
    // whole life, which is what lets the overlay margins stay tight.
    for (int i = m_flies.size() - 1; i >= 0; --i)
        if (m_flies[i].leaving && m_flies[i].fade < 0.03f)
            m_flies.removeAt(i);

    // An emptied bin sends everyone home.
    if (want == 0)
        for (Fly &f : m_flies)
            f.leaving = true;
}

} // namespace hyperbin
