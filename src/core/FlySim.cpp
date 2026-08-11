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

namespace {
// Cheap smooth value noise. Enough for a flow field — the point is
// spatial coherence, not spectral quality.
float hash2(int x, int y, int s)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u
               + uint32_t(s) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0x00FFFFFFu) / float(0x01000000u);
}
float smoothNoise(float x, float y, int seed)
{
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float fx = x - xi, fy = y - yi;
    // Quintic smoothstep: continuous first and second derivatives, so the
    // curl (a derivative of this) doesn't visibly kink.
    const float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
    const float v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    const float a  = hash2(xi,     yi,     seed), b = hash2(xi + 1, yi,     seed);
    const float c2 = hash2(xi,     yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    const float top = a + (b - a) * u;
    const float bot = c2 + (d - c2) * u;
    return top + (bot - top) * v;
}
} // namespace
QPointF FlySim::curlAt(float x, float y) const
{
    // curl of a scalar potential in 2D: (dPsi/dy, -dPsi/dx). Divergence
    // free by construction, so the flow swirls instead of converging on
    // sinks — flies never pile into one corner of the field.
    const float s = params.noiseScale;
    const float t = m_time * params.noiseDrift;
    const float e = 0.75f;
    const float px = x * s + t, py = y * s - t * 0.6f;
    const float dpdy = smoothNoise(px, py + e, 1) - smoothNoise(px, py - e, 1);
    const float dpdx = smoothNoise(px + e, py, 1) - smoothNoise(px - e, py, 1);
    return QPointF(dpdy, -dpdx) / (2.0f * e);
}
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
    const float halfW = m_bin.width()  > 2.0 ? float(m_bin.width())  * 0.5f : 20.0f * k;
    const float halfH = m_bin.height() > 2.0 ? float(m_bin.height()) * 0.5f : 14.0f * k;
    Fly f;
    // Appear clear of the icon, for the same reason flies fade out clear
    // of it: materialising on top of the bin doesn't read as arriving.
    //
    // This can't be done by pushing the radius outward. The roam ellipse
    // is only a little larger than the tile, so "outside the bin" and
    // "inside the containment boundary" barely overlap — growing the
    // radius until the point cleared the bin put it PAST the boundary,
    // where the containment shove immediately drove it back over the
    // icon, mid-fade. Instead the radius is fixed just inside the
    // boundary and the ANGLE is re-rolled. The horizontal half-axis
    // (roamX) is narrower than the tile and the vertical one (roamUp) is
    // taller, so what survives is arrivals from above and below — which
    // is the natural direction for a fly to come from anyway.
    float ang = rndRange(0.0f, float(2 * M_PI));
    QPointF at;
    for (int tries = 0; tries < 12; ++tries) {
        at = c + QPointF(std::cos(ang) * halfW * 2.0f * params.roamX * 0.95f,
                         std::sin(ang) * halfH * 2.0f * params.roamUp * 0.95f);
        // Also off-limits: directly below the bin. Occluded flies are
        // held above its bottom edge, so a fly born there would be shoved
        // up into the icon and fade in on top of it.
        const bool underBin = at.y() > m_bin.bottom()
            && std::abs(at.x() - c.x()) < m_bin.width() * 0.5 + 2.0 * k;
        if (!m_bin.contains(at) && !underBin)
            break;
        ang = rndRange(0.0f, float(2 * M_PI));
    }
    f.pos = at;
    f.vel = QPointF(std::cos(ang), std::sin(ang)) * params.flySpeed * k * 0.4f;
    // Seed the smoothed direction, or the fly spends its first moments
    // easing out of a zero vector instead of flying.
    f.dir = QPointF(std::cos(ang), std::sin(ang));
    f.wanderAngle = ang;
    f.phase       = rndRange(0.0f, float(2 * M_PI));
    // Mild size variation only — enough to break up uniformity,
    // not enough to read as flies being at different depths.
    f.scale       = rndRange(0.9f, 1.1f);
    f.life        = rndRange(params.lifeMin, params.lifeMax);
    f.fade        = 0.0f;
    f.leaving     = false;
    f.inFront     = rnd() < params.frontShare;
    enterMode(f, FlyMode::Flying);
    // Decide about landing at birth, not only when the first flying leg
    // expires. Lifetimes (2-6s) overlap the flying-leg length, so most
    // flies used to die before ever getting asked, and the swarm went
    // almost entirely airborne.
    f.seekingLand = rnd() < params.landChance;
    if (f.seekingLand)
        f.modeLeft = rndRange(params.flyMin, params.flyMax) * 0.5f;
    m_flies.push_back(f);
}

void FlySim::step(float dt)
{
    // Guard against the first frame after a wake, where dt can be huge.
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (dt <= 0.0f)
        return;

    m_time += dt;
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
            // Fading out on top of the bin looks like a fly winking out of
            // existence, because the bin gives the eye a fixed reference
            // right behind it. Expired flies take off and only begin to
            // fade once they're clear of the icon — the disappearance then
            // reads as one flying off. The -2s floor stops a fly that
            // can't get clear (a shrinking Dock tile, say) from living on.
            if (f.life <= 0.0f) {
                f.seekingLand = false;
                if (f.mode == FlyMode::Crawling)
                    enterMode(f, FlyMode::Flying); // always leaves on the wing
                if (!m_bin.contains(f.pos) || f.life <= -2.0f) {
                    f.leaving = true;
                    enterMode(f, FlyMode::Flying);
                }
            }
        }
        // Fade in on arrival, out on departure — but only while clear of
        // the icon. Setting `leaving` off the bin isn't enough on its own:
        // a departing fly can cross back over the bin well within the
        // fade, and then vanishes against it anyway. Freezing the fade
        // over the tile means every visible appearance and disappearance
        // happens against the desktop.
        // Freezing the fade over the icon was tried and is worse: flies
        // then loiter half-transparent for seconds at a time. Instead the
        // fade always runs, and a fading fly is held off the tile by the
        // outward steer below — `leaving` is only ever set off the bin,
        // and 0.28s of outward flight is not enough to get back on it.
        const bool fading = f.leaving || f.fade < 0.995f;
        // Linear, not exponential. An exponential approach never really
        // finishes: at 0.28s it is still visibly translucent, and the tail
        // is long enough for the fly to drift back over the bin while
        // still fading. A linear ramp is over when it says it is.
        const float target = f.leaving ? 0.0f : 1.0f;
        const float step   = dt / params.fadeTime;
        f.fade = (target > f.fade) ? std::min(target, f.fade + step)
                                   : std::max(target, f.fade - step);

        // Mode follows POSITION, not just a timer: a fly can only crawl
        // where there is something to crawl on. Off the bin it must be
        // flying — a timer alone could park one mid-air at crawl speed,
        // which reads as hovering rather than landing.
        f.modeLeft -= dt;
        const bool overBin = m_bin.contains(f.pos);
        if (!f.leaving) {
            if (f.mode == FlyMode::Crawling && !overBin) {
                enterMode(f, FlyMode::Flying);      // drifted off: take off
            } else if (f.seekingLand && overBin) {
                enterMode(f, FlyMode::Crawling);     // arrived: settle
                f.seekingLand = false;
            } else if (f.modeLeft <= 0.0f) {
                if (f.mode == FlyMode::Crawling) {
                    enterMode(f, FlyMode::Flying);   // done crawling
                } else {
                    enterMode(f, FlyMode::Flying);   // another circuit
                    f.seekingLand = rnd() < params.landChance;
                }
            }
        }

        const bool crawling = !f.leaving && f.mode == FlyMode::Crawling && overBin;
        f.onSurface = crawling;
        // Crawling pace wanders, and spends real time near a standstill.
        // A crawler at one constant speed reads as a machine tracking a
        // path; the stop-and-go is most of what sells it as an insect.
        f.paceLeft -= dt;
        if (f.paceLeft <= 0.0f) {
            f.paceLeft = rndRange(0.35f, 1.1f);
            // Weighted low: two thirds of the time it's dawdling or
            // stopped, the rest is a short scurry.
            f.pace = rnd() < 0.66f ? rndRange(0.05f, 0.45f)
                                   : rndRange(0.9f, 1.6f);
        }
        float speedMax =
            (crawling ? params.crawlSpeed
                      : params.flySpeed * (0.65f + 0.35f * m_fullness)) * k;
        // Kept separately: the paced speed can drop to near zero, and the
        // heading threshold below must not scale with it or a dawdling
        // crawler would start spinning again.
        const float cruise = speedMax;
        if (crawling)
            speedMax *= f.pace;
        // A flier passing over the bin throttles back — hovering over the
        // thing it cares about, rather than treating it as scenery.
        if (!crawling && !f.leaving && overBin)
            speedMax *= params.overBinSlow;

        // --- steering --------------------------------------------------
        // Curl noise gives the DIRECTION; speed is set explicitly and the
        // fly accelerates toward that target velocity.
        //
        // Using the curl as a raw force was wrong: it's a difference of
        // two noise samples, so its magnitude is only ~0.1, and against
        // the friction term that settled at about 6px/s — flies that
        // barely moved and, with almost no velocity, spun on the spot
        // because the sprite's heading comes from the velocity vector.
        // A crawler samples the field at a slightly finer scale, but
        // nothing like the 3x used before — that turned the curl into
        // noise at body scale and made them jitter constantly.
        const float freq = crawling ? 1.6f : 1.0f;
        QPointF raw = curlAt(float(f.pos.x()) * freq, float(f.pos.y()) * freq);
        const float dlen = float(std::hypot(raw.x(), raw.y()));
        raw = dlen > 0.001f ? raw / dlen
                            : QPointF(std::cos(f.wanderAngle), std::sin(f.wanderAngle));
        // Low-pass the DIRECTION before it becomes a target velocity. The
        // curl field is sampled at the fly's own position, so it changes
        // as fast as the fly travels — fed in raw it read as twitching
        // rather than as flying. Smoothing the direction and not the
        // velocity leaves the speed untouched, so this costs no liveliness.
        const float blend = std::min(1.0f, (crawling ? 6.0f : 2.6f) * dt);
        f.dir += (raw - f.dir) * blend;
        const float sl = float(std::hypot(f.dir.x(), f.dir.y()));
        QPointF dir = sl > 0.001f ? f.dir / sl : raw;
        QPointF desired = dir * speedMax;
        // A fly on approach steers for the bin rather than drifting until
        // it happens to cross it.
        // A fly that is still fading holds away from the icon, so the
        // fade has somewhere to run to completion instead of stalling
        // frozen and half-transparent over the tile.
        // How far out the fly is, as a fraction of its roam ellipse. This
        // is needed BEFORE the steering terms, not after: a fading fly
        // pushing outward and the boundary shoving inward used to be
        // applied one after the other, and near the edge they cancelled
        // and then reversed frame to frame. The velocity flipped, the
        // heading chased it, and the fly span on the spot.
        const QPointF rel(f.pos.x() - c.x(), f.pos.y() - c.y());
        const float rs0 = crawling ? 1.0f : params.flyRoamScale;
        const float ry0 = (rel.y() < 0 ? roamUp : roamDown) * rs0;
        const float nx0 = float(rel.x()) / (roamX * rs0);
        const float ny0 = float(rel.y()) / ry0;
        const float nd = std::sqrt(nx0 * nx0 + ny0 * ny0);
        // Outward push, ramped to nothing as the boundary approaches so
        // it never fights the containment term head-on. The ramp has to be
        // narrow: flies spawn close to the boundary, and a wide ramp left
        // them with no push at all where they most need it.
        if (fading && nd < 1.0f) {
            const QPointF out = f.pos - c;
            const float od = float(std::hypot(out.x(), out.y()));
            if (od > 0.001f) {
                const float w = std::min(1.0f, (1.0f - nd) / 0.12f);
                desired = desired * (1.0f - 0.88f * w)
                          + (out / od) * speedMax * 1.25f * w;
            }
        }
        if (f.seekingLand && !f.leaving) {
            const QPointF toBin = c - f.pos;
            const float db = float(std::hypot(toBin.x(), toBin.y()));
            if (db > 0.001f)
                desired = desired * 0.35f + (toBin / db) * speedMax * 0.9f;
        }
        {
            // Containment: an ellipse matching the icon's proportions —
            // tight left/right so they don't stray onto neighbouring Dock
            // icons, taller upward so they can rise off the bin.
            // Fliers get a wider berth; crawlers stay on the bin.
            const float d = float(std::hypot(rel.x(), rel.y()));
            if (d > 0.001f) {
                // Always a gentle pull inward, growing with distance, so
                // the swarm clusters on the bin instead of spreading out
                // to fill the region uniformly...
                if (!fading)
                desired -= (rel / d) * speedMax * params.centrePull * qMin(nd, 1.0f);
                // ...and a hard shove once past the boundary.
                // Capped. Unbounded, this reached several times speedMax
                // and did not merely turn a fly around, it fired it back
                // across the bin; overshoot then re-triggered it from the
                // far side and the fly oscillated in place.
                if (nd > 1.0f)
                    desired -= (rel / d) * speedMax
                               * std::min(1.6f, 1.0f + (nd - 1.0f) * 3.0f);
            }
        }
        // The bin sits on the floor of the Dock: there is no space behind
        // its bottom edge for anything to pass through. A fly that is
        // occluded — drawn behind the icon, or crawling on it — is
        // therefore steered up and away from that edge, so it can only
        // enter or leave the icon's silhouette at the left, top or right.
        // Flies drawn IN FRONT are unconstrained: they're over the Dock,
        // not behind the bin, and nothing about that reads as impossible.
        const bool occluded = !f.inFront || crawling;
        if (occluded && !m_bin.isEmpty()) {
            const float floorY = float(m_bin.bottom());
            const float band   = 6.0f * k;   // start lifting before the edge
            const float overX  = float(m_bin.width()) * 0.5f + 2.0f * k;
            if (std::abs(rel.x()) < overX && f.pos.y() > floorY - band) {
                const float t = std::min(1.0f, (float(f.pos.y()) - (floorY - band)) / band);
                desired.ry() = desired.y() * (1.0f - t) - speedMax * 1.2f * t;
            }
        }
        // Separation — cheap O(n^2), fine at n<=6.
        QPointF push(0, 0);
        for (int j = 0; j < m_flies.size(); ++j) {
            if (i == j) continue;
            QPointF diff = f.pos - m_flies[j].pos;
            const float dd = float(std::hypot(diff.x(), diff.y()));
            if (dd > 0.001f && dd < separation)
                push += (diff / dd) * (separation - dd);
        }
        desired += push * speedMax * 0.35f;
        // Seek the target velocity. Crawlers respond sluggishly, fliers
        // sharply — that difference is most of what separates the two.
        // Fliers were at 11, which tracked the steering target almost
        // instantly and turned every wobble in it into a visible jolt.
        const float respond = crawling ? 3.0f : 5.0f;
        f.vel += (desired - f.vel) * std::min(1.0f, respond * dt);
        // Twitches: a crawler makes quick sideways corrections, a flier
        // occasionally darts.
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
        f.vel = limit(f.vel, speedMax * 1.6f);
        // Fliers always cruise. Steering terms can cancel — an outward
        // push against the containment shove, a seek against the curl —
        // and a flier whose speed collapses to nothing spins on the spot,
        // because the sprite's heading is read off the velocity vector.
        // Real flies don't hover in place; enforcing a floor along the
        // current heading removes the whole failure mode rather than
        // chasing the individual pairs of terms that cause it.
        if (!crawling) {
            const float sp = float(std::hypot(f.vel.x(), f.vel.y()));
            const float floorSp = speedMax * 0.45f;
            if (sp < floorSp) {
                const QPointF headed = sp > 0.001f
                    ? f.vel / sp
                    : QPointF(std::cos(f.wanderAngle), std::sin(f.wanderAngle));
                f.vel = headed * floorSp;
            }
        }
        f.pos += f.vel * dt;
        // Hard backstop for the same rule. The steer above handles the
        // normal case; a dart impulse or a separation shove can still
        // punch a fly through in one step, and one frame of a fly inside
        // the Dock floor is one frame too many.
        if (occluded && !m_bin.isEmpty()) {
            const float floorY = float(m_bin.bottom());
            const float overX  = float(m_bin.width()) * 0.5f + 2.0f * k;
            if (std::abs(f.pos.x() - c.x()) < overX && f.pos.y() > floorY) {
                f.pos.setY(floorY);
                if (f.vel.y() > 0)
                    f.vel.setY(0);
            }
        }
        // Heading follows travel, but ROTATES toward it at a limited
        // rate rather than snapping. Snapping is what made a slow fly
        // spin: tiny velocity changes swing atan2 wildly, and the sprite
        // followed every one of them.
        const float sp2 = float(std::hypot(f.vel.x(), f.vel.y()));
        if (sp2 > cruise * 0.12f) {
            const float want = std::atan2(float(f.vel.y()), float(f.vel.x()));
            float d = want - f.wanderAngle;
            while (d >  float(M_PI)) d -= float(2 * M_PI);
            while (d < -float(M_PI)) d += float(2 * M_PI);
            const float maxTurn =
                (crawling ? params.turnRateCrawl : params.turnRateFly) * dt;
            f.wanderAngle += std::clamp(d, -maxTurn, maxTurn);
        }
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
