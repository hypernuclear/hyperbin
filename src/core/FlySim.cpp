#include "FlySim.h"

#include <QtEnvironmentVariables>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace hyperbin {

qreal FlySim::spriteBase()
{
    // Visible length -> quad half-size. A quad of width W shows a fly of
    // about kInkFraction x W; halved again because this returns a
    // HALF-size.
    static const qreal base = [] {
        qreal gain = 1.0;
        if (qEnvironmentVariableIsSet("HYPERBIN_FLY_SIZE")) {
            bool ok = false;
            const qreal v = qEnvironmentVariable("HYPERBIN_FLY_SIZE").toDouble(&ok);
            // Reject rather than clamp. Clamping quietly turned "225%" —
            // which does not parse as a number — into 0.1, i.e. flies ten
            // times SMALLER than default, while looking like the knob had
            // been set. A knob that silently does the opposite of what was
            // asked is worse than no knob.
            if (ok && v > 0.0)
                gain = v;
            else
                qWarning("hyperbin: HYPERBIN_FLY_SIZE='%s' is not a positive "
                         "number (try 1.5, not 150%%); ignoring it",
                         qPrintable(qEnvironmentVariable("HYPERBIN_FLY_SIZE")));
        }
        return kFlyLengthAt40 * gain / (2.0 * kInkFraction);
    }();
    return base;
}

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

void FlySim::trimSwarm(int want)
{
    // With no lifespan, nothing else ever removes a fly. When the bin
    // gets emptier the swarm has to shrink, so the surplus is retired
    // here — one at a time, so the swarm thins out rather than vanishing
    // in a block.
    int staying = 0;
    for (const Fly &f : m_flies)
        if (!f.leaving && !f.retiring)
            ++staying;
    if (staying <= want)
        return;

    // Retire a flier before a crawler: a fly leaving the bin is more
    // conspicuous than one already in the air, and the landing floor
    // wants the bin occupied anyway.
    Fly *pick = nullptr;
    for (Fly &f : m_flies) {
        if (f.leaving || f.retiring)
            continue;
        if (!pick || (pick->mode == FlyMode::Crawling
                      && f.mode == FlyMode::Flying))
            pick = &f;
    }
    if (pick) {
        pick->retiring  = true;
        pick->retireFor = 0.0f;
    }
}

void FlySim::manageLandings()
{
    // Hold a floor of flies ON the bin. Per-fly timers can't express this
    // on their own: each fly decides independently, so the count drifts
    // and the bin goes bare for seconds at a stretch purely by chance.
    // This is the one genuinely population-level rule in the sim.
    if (m_bin.isEmpty() || m_cursorOnBin)
        return;

    int landed = 0, inbound = 0;
    for (const Fly &f : m_flies) {
        if (f.leaving)
            continue;
        if (f.mode == FlyMode::Crawling)
            ++landed;
        else if (f.seekingLand)
            ++inbound;
    }

    const int target = qMin(params.preferLanded, int(m_flies.size()));
    if (landed + inbound >= target)
        return;

    // Send whoever is closest — it arrives soonest, and a fly turning
    // toward the bin from nearby reads as intent rather than teleportation.
    Fly *best = nullptr;
    double bestD = 1e18;
    for (Fly &f : m_flies) {
        if (f.leaving || f.seekingLand || f.mode == FlyMode::Crawling
            || f.bolting || f.scatterLeft > 0.0f)
            continue;
        // Not one that is behind the bin AND over it: it would have to
        // change depth where the change is visible. Off the bin, the swap
        // below is free.
        if (!f.inFront && onSurfaceAt(f.pos))
            continue;
        const QPointF d = f.pos - m_bin.center();
        const double dist = d.x() * d.x() + d.y() * d.y();
        if (dist < bestD) {
            bestD = dist;
            best  = &f;
        }
    }
    if (best) {
        best->seekingLand = true;
        best->landTarget  = surfaceTarget();
        // Come round to the front on the way in. Safe here precisely
        // because the fly is clear of the bin, so nothing pops.
        best->inFront   = true;
        best->wasToward = true;
    }
}

void FlySim::setSurface(const QVector<quint8> &coverage, int w, int h)
{
    if (w <= 0 || h <= 0 || coverage.size() < w * h) {
        m_cov.clear();
        m_covSolid.clear();
        m_covW = m_covH = 0;
        return;
    }
    m_cov  = coverage;
    m_covW = w;
    m_covH = h;
    m_covSolid.clear();
    m_covSolid.reserve(w * h / 2);
    m_covInner.clear();
    for (int i = 0; i < w * h; ++i) {
        if (!m_cov[i])
            continue;
        m_covSolid.push_back(i);
        // One-cell erosion. A cell on the rim is a legal place to stand
        // but a bad place to land, because the fly is wider than the cell
        // and arrives with its nose already over the edge.
        const int x = i % w, y = i / w;
        if (x > 0 && x < w - 1 && y > 0 && y < h - 1
            && m_cov[i - 1] && m_cov[i + 1] && m_cov[i - w] && m_cov[i + w])
            m_covInner.push_back(i);
    }
    // No solid cells at all means the grid is useless; fall back rather
    // than stranding every fly with nowhere to land.
    if (m_covSolid.isEmpty()) {
        m_cov.clear();
        m_covW = m_covH = 0;
    }
}

bool FlySim::onSurfaceAt(const QPointF &p) const
{
    if (m_bin.isEmpty())
        return false;
    if (m_cov.isEmpty())
        return m_bin.contains(p);   // no silhouette known; box is all we have
    const double u = (p.x() - m_bin.left()) / m_bin.width();
    const double v = (p.y() - m_bin.top()) / m_bin.height();
    if (u < 0.0 || u >= 1.0 || v < 0.0 || v >= 1.0)
        return false;
    const int x = qBound(0, int(u * m_covW), m_covW - 1);
    const int y = qBound(0, int(v * m_covH), m_covH - 1);
    return m_cov[y * m_covW + x] != 0;
}

QPointF FlySim::surfaceTarget()
{
    if (m_covSolid.isEmpty() || m_bin.isEmpty())
        return m_bin.center();
    // Aim at the eroded silhouette when there is one. A thin or spindly
    // icon can erode to nothing, so fall back to any solid cell rather
    // than leaving flies with nowhere to go.
    const QVector<int> &pool = m_covInner.isEmpty() ? m_covSolid : m_covInner;

    // One grid cell of erosion is not a fly's width — the grid is 24x24
    // whatever the icon's size, so a cell is ~2px on a 48px bin while the
    // fly is twice that. Sample a few candidates and prefer one with room
    // for the whole sprite; a fly that touches down on the rim is clipped
    // from the instant it lands, before the crawl steering can help.
    const float clear = float(spriteHalf(qMax(m_bin.width(), m_bin.height()), 1.0f));
    QPointF best;
    for (int attempt = 0; attempt < 6; ++attempt) {
        const int idx = pool[int(rnd() * pool.size()) % pool.size()];
        const int x = idx % m_covW, y = idx / m_covW;
        // Aim at the middle of the cell, jittered inside it so successive
        // landings don't stack on the same handful of points.
        const double u = (x + 0.2 + 0.6 * rnd()) / m_covW;
        const double v = (y + 0.2 + 0.6 * rnd()) / m_covH;
        const QPointF p(m_bin.left() + u * m_bin.width(),
                        m_bin.top()  + v * m_bin.height());
        if (attempt == 0)
            best = p;   // always have an answer, even if none has clearance
        if (onSurfaceAt(p + QPointF(clear, 0)) && onSurfaceAt(p - QPointF(clear, 0))
            && onSurfaceAt(p + QPointF(0, clear)) && onSurfaceAt(p - QPointF(0, clear)))
            return p;
    }
    return best;
}

void FlySim::setCursor(const QPointF &p, bool present)
{
    m_cursor        = p;
    m_cursorPresent = present;
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
    // Taking off: start the speed ramp. Entering Flying from Flying (a
    // fresh circuit) doesn't re-ramp — it is already moving.
    if (m == FlyMode::Flying && f.mode == FlyMode::Crawling)
        f.takeoffLeft = params.takeoffRamp;
    f.mode = m;
    if (m == FlyMode::Crawling) {
        f.modeLeft   = rndRange(params.crawlMin, params.crawlMax);
        f.longFlight = false;
    } else {
        f.longFlight = rnd() < params.longFlightChance;
        f.modeLeft   = f.longFlight
            ? rndRange(params.longFlyMin, params.longFlyMax)
            : rndRange(params.flyMin, params.flyMax);
    }
    if (m == FlyMode::Crawling) {
        // Land, then hold still for a beat before walking anywhere.
        f.crawlStage = 0;
        f.pauseLeft  = rndRange(params.settleMin, params.settleMax);
    } else {
        f.crawlStage = 0;
        f.pauseLeft  = 0.0f;
    }
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
    f.id = m_nextId++;
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
    f.fade        = 0.0f;
    f.leaving     = false;
    // Spawns start outside the icon heading away from it, so they begin
    // in front; the first turn back toward the bin is what decides
    // whether this one passes behind it.
    f.inFront     = true;
    f.wasToward   = false;
    enterMode(f, FlyMode::Flying);
    // Decide about landing at birth, not only when the first flying leg
    // expires. Lifetimes (2-6s) overlap the flying-leg length, so most
    // flies used to die before ever getting asked, and the swarm went
    // almost entirely airborne.
    f.seekingLand = rnd() < params.landChance;
    if (f.seekingLand) {
        f.landTarget = surfaceTarget();
        f.modeLeft = rndRange(params.flyMin, params.flyMax) * 0.5f;
    }
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

    // Pointer on the bin: the swarm clears off and does not come back
    // until it leaves. Scattering alone wasn't enough — flies would circle
    // just outside the cursor and crowd straight back in.
    m_cursorOnBin = m_cursorPresent && !m_bin.isEmpty()
                    && m_bin.contains(m_cursor);
    if (m_cursorOnBin) {
        const float ks = sizeScale();
        for (Fly &f : m_flies) {
            if (f.leaving || f.scatterLeft > 0.0f)
                continue;
            // Scatter FIRST, fade second. Marking them leaving on the spot
            // made them dissolve where they sat, which looked like the
            // pointer killing flies instead of scaring them off. They get
            // a burst away from the cursor, and only once that has played
            // out are they allowed to start fading.
            f.freezeLeft  = 0.0f;   // a startled fly unfreezes and goes
            f.seekingLand = false;
            f.scatterLeft = rndRange(params.scatterMin, params.scatterMax);
            enterMode(f, FlyMode::Flying);
            QPointF away = f.pos - m_cursor;
            const float ad = float(std::hypot(away.x(), away.y()));
            away = ad > 0.001f ? away / ad
                               : QPointF(std::cos(f.wanderAngle),
                                         std::sin(f.wanderAngle));
            away.ry() -= 0.55f;     // bias up and out, away from the Dock
            const float al = float(std::hypot(away.x(), away.y()));
            if (al > 0.001f) away /= al;
            f.dir      = away;
            f.vel      = away * params.flySpeed * ks * params.scatterSpeed;
            f.bolting  = true;
            f.boltLeft = params.boltFor;
        }
    }
    manageLandings();

    const int want = desiredCount();
    trimSwarm(want);

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
    if (!m_cursorOnBin && staying < want && m_flies.size() < params.maxFlies)
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
        // Flies do not expire. There is no lifespan any more: rubbish
        // doesn't stop attracting flies after six seconds, and the
        // constant churn of arrivals and departures was motion the eye
        // kept getting drawn to for no reason. A fly leaves for exactly
        // three reasons, all of them things that actually happened —
        // the pointer arrived (scatter), the bin emptied, or the swarm
        // has to shrink because there is less rubbish (see trimSwarm).
        if (f.scatterLeft > 0.0f) {
            f.scatterLeft -= dt;
            if (f.scatterLeft <= 0.0f)
                f.leaving = true;
        }
        // A fly told to go finishes on the wing and only starts fading
        // once it is clear of the icon, so it reads as flying off rather
        // than winking out against the bin.
        if (f.retiring && !f.leaving) {
            f.seekingLand = false;
            if (f.mode == FlyMode::Crawling)
                enterMode(f, FlyMode::Flying);
            f.retireFor += dt;
            if (!m_bin.contains(f.pos) || f.retireFor > 2.0f) {
                f.leaving = true;
                enterMode(f, FlyMode::Flying);
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
        // The SILHOUETTE, not the bounding box. A fly standing on the
        // empty part of the Dock tile is clipped away by the renderer, so
        // treating that as "landed" produced a sim full of flies and a
        // screen with none.
        const bool overBin = onSurfaceAt(f.pos);
        if (!f.leaving) {
            if (f.mode == FlyMode::Crawling && !overBin) {
                enterMode(f, FlyMode::Flying);      // drifted off: take off
            } else if (f.seekingLand && overBin && f.inFront) {
                // Only a fly that is IN FRONT may land. A fly behind the
                // bin is masked out entirely, so landing there flipped it
                // from the hidden batch to the clipped-to-surface batch
                // and it appeared out of nowhere on the front of the bin.
                // Depth is only re-decided off the bin, so a fly that
                // arrives behind has to leave and come back round.
                enterMode(f, FlyMode::Crawling);     // arrived: settle
                f.seekingLand = false;
                f.landTarget  = f.pos;
                f.walkAngle   = rndRange(0.0f, float(2 * M_PI));
            } else if (f.mode == FlyMode::Crawling && f.modeLeft <= 0.0f
                       && f.crawlStage < 2) {
                // Done walking: stop again before leaving, rather than
                // launching mid-stride.
                f.crawlStage = 2;
                f.pauseLeft  = rndRange(params.preFlightMin, params.preFlightMax);
            } else if (f.modeLeft <= 0.0f) {
                if (f.mode == FlyMode::Crawling) {
                    enterMode(f, FlyMode::Flying);   // done crawling
                } else {
                    enterMode(f, FlyMode::Flying);   // another circuit
                    f.seekingLand = !f.longFlight
                                    && rnd() < params.landChance;
                    if (f.seekingLand) {
                        f.landTarget = surfaceTarget();
                        // Committing to land means committing to the
                        // front. Doing it here, off the bin, is what
                        // keeps the depth change invisible.
                        if (!onSurfaceAt(f.pos)) {
                            f.inFront   = true;
                            f.wasToward = true;
                        }
                    }
                }
            }
        }

        const bool crawling = !f.leaving && f.mode == FlyMode::Crawling && overBin;
        f.onSurface = crawling;
        // Depth. A fly may only be behind the bin if it is heading toward
        // it: one flying outward from the middle has nothing left to be
        // occluded by, and masking it on the way out read as it being
        // swallowed. Re-decided only while clear of the icon, and only on
        // the toward/away transition, so nothing flickers or pops.
        if (!overBin && !crawling) {
            const QPointF toC = c - f.pos;
            const bool toward = QPointF::dotProduct(f.vel, toC) > 0.0;
            if (toward != f.wasToward) {
                f.wasToward = toward;
                // A fly coming in to LAND is always in front. The whole
                // sequence — touch down, sit still, walk, go — is invisible
                // behind the bin, and it was landing behind it most of the
                // time that made the swarm look like it never landed.
                // A fly coming in to LAND is always in front: the whole
                // sequence — touch down, sit still, walk, go — is
                // invisible behind the bin. Everything else can pass
                // behind, and a fly on a long circuit does so more often
                // still, since a lap is the natural time to go round the
                // back of something.
                const float share = f.longFlight ? params.frontShare * 0.35f
                                                 : params.frontShare;
                f.inFront   = !toward || f.seekingLand || rnd() < share;
            }
        }
        // --- landing sequence ------------------------------------------
        // land -> settle -> walk -> pause -> take off. Each step is a
        // timer rather than a distance, so it looks the same whatever the
        // Dock size is.
        if (crawling && f.pauseLeft > 0.0f) {
            f.pauseLeft -= dt;
            if (f.pauseLeft <= 0.0f) {
                if (f.crawlStage == 0)
                    f.crawlStage = 1;                 // settled; start walking
                else if (f.crawlStage == 2) {
                    // Refuse to leave if this fly is the last one on the
                    // bin — it goes back to walking instead. Without this
                    // the floor above can only react AFTER the bin is
                    // already bare, which shows.
                    int others = 0;
                    for (const Fly &o : m_flies)
                        if (&o != &f && !o.leaving && o.mode == FlyMode::Crawling)
                            ++others;
                    if (others < params.minLanded && !m_cursorOnBin) {
                        f.crawlStage = 1;
                        f.modeLeft   = rndRange(params.crawlMin, params.crawlMax);
                    } else {
                        enterMode(f, FlyMode::Flying); // paused; now leave
                        f.seekingLand = !f.longFlight
                                        && rnd() < params.landChance;
                        if (f.seekingLand)
                            f.landTarget = surfaceTarget();
                    }
                }
            }
        } else if (crawling && f.crawlStage == 1 && !f.leaving
                   && rnd() < params.restChance) {
            f.pauseLeft = rndRange(params.restMin, params.restMax);
        }
        const bool paused = crawling && f.pauseLeft > 0.0f;

        // --- startle ---------------------------------------------------
        // A crawler occasionally freezes, holds for a beat, then bolts.
        if (crawling && !f.leaving && f.freezeLeft <= 0.0f && !f.bolting
            && rnd() < params.startleChance)
            f.freezeLeft = rndRange(params.freezeMin, params.freezeMax);
        bool frozen = false;
        if (f.freezeLeft > 0.0f) {
            f.freezeLeft -= dt;
            frozen = f.freezeLeft > 0.0f;
            if (!frozen) {
                // Bolt: straight out from the bin, biased upward, and
                // fast enough that the takeoff reads as a single event
                // rather than as the fly resuming its wander.
                QPointF out = f.pos - c;
                const float od = float(std::hypot(out.x(), out.y()));
                out = od > 0.001f ? out / od : QPointF(0, -1);
                out.ry() -= 1.8f;   // strongly upward: sideways bolts ran into the horizontal margin
                const float ol = float(std::hypot(out.x(), out.y()));
                if (ol > 0.001f) out /= ol;
                enterMode(f, FlyMode::Flying);
                f.bolting     = true;
                f.seekingLand = false;
                f.dir         = out;
                f.vel         = out * params.flySpeed * k * params.boltSpeed;
                // The burst is a burst, not a death sentence. Clamping the
                // fly's life here meant it bolted and then immediately
                // faded out, so the sequence read as pause-then-vanish
                // rather than pause-then-fly-away. It now rejoins normal
                // flight and leaves when its time is up like any other.
                f.boltLeft    = params.boltFor;
            }
        }
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

        // --- flight speed envelope -------------------------------------
        // Slow off the mark, quick in the middle, slow onto the bin. The
        // ramp is smoothstepped rather than linear so there is no corner
        // at either end of it.
        if (!crawling && !f.leaving) {
            float env = 1.0f;
            if (f.takeoffLeft > 0.0f) {
                f.takeoffLeft -= dt;
                const float u = std::clamp(
                    1.0f - f.takeoffLeft / qMax(0.001f, params.takeoffRamp),
                    0.0f, 1.0f);
                const float e = u * u * (3.0f - 2.0f * u);
                env *= params.takeoffSpeed + (1.0f - params.takeoffSpeed) * e;
            }
            if (f.seekingLand) {
                const QPointF d = f.landTarget - f.pos;
                const float dist = float(std::hypot(d.x(), d.y()));
                const float range = params.approachRange * halfW * 2.0f;
                if (dist < range) {
                    const float u = std::clamp(dist / qMax(1.0f, range), 0.0f, 1.0f);
                    const float e = u * u * (3.0f - 2.0f * u);
                    env *= params.approachSlow + (1.0f - params.approachSlow) * e;
                }
            }
            speedMax *= env;
        }
        if (crawling)
            speedMax *= f.pace;
        if (frozen || paused)
            speedMax = 0.0f;
        // A bolting fly keeps its speed up. Without this the very next
        // frame clamps it back to the normal cruise limit and the burst
        // never actually happens.
        if (f.bolting) {
            f.boltLeft -= dt;
            if (f.boltLeft <= 0.0f)
                f.bolting = false;
        }
        // Boost decays over the burst rather than ending abruptly, so the
        // fly eases back into its normal cruise.
        if (f.bolting && !frozen) {
            const float t = std::min(1.0f, f.boltLeft / params.boltFor);
            speedMax *= 1.0f + (params.boltSpeed * 0.8f - 1.0f) * t;
        }
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
        QPointF raw;
        if (crawling) {
            // A WALK, not a drift. Sampling the curl field at the fly's
            // own position gave a direction that changed as fast as it
            // moved, so a crawler pivoted on the spot instead of getting
            // anywhere. The heading now persists and only wanders slowly,
            // which is what reads as walking.
            f.walkAngle += (rnd() - 0.5f) * params.walkWander * dt;
            raw = QPointF(std::cos(f.walkAngle), std::sin(f.walkAngle));
        } else {
            QPointF cn = curlAt(float(f.pos.x()), float(f.pos.y()));
            const float dlen = float(std::hypot(cn.x(), cn.y()));
            raw = dlen > 0.001f ? cn / dlen
                                : QPointF(std::cos(f.wanderAngle),
                                          std::sin(f.wanderAngle));
        }
        // Low-pass the DIRECTION before it becomes a target velocity. The
        // curl field is sampled at the fly's own position, so it changes
        // as fast as the fly travels — fed in raw it read as twitching
        // rather than as flying. Smoothing the direction and not the
        // velocity leaves the speed untouched, so this costs no liveliness.
        const float blend = std::min(1.0f, (crawling ? 6.0f : 1.9f) * dt);
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
            // Aim at a chosen spot ON the artwork. Steering at the rect's
            // centre was fine when the whole rect counted as landable;
            // now that only the ink does, the centre is often a hole.
            const QPointF toBin = f.landTarget - f.pos;
            const float db = float(std::hypot(toBin.x(), toBin.y()));
            if (db > 0.001f)
                desired = desired * 0.35f + (toBin / db) * speedMax * 0.9f;
        }
        // A crawler that wanders off the ink is steered back onto it
        // instead of taking off. Walking to the rim and stepping back is
        // what a fly does; launching every time it clips an edge is not.
        //
        // The probe is the fly's NOSE, not its centre. It used to be a
        // quarter-second of travel — about 2px at crawl pace — while the
        // fly itself is several px long, so its front half was already
        // over the rim and being sliced by the mask before anything
        // steered it back. What that looks like on screen is a fly
        // walking toward the edge and losing its head.
        if (crawling && !f.leaving && m_covW > 0) {
            const float icon  = float(qMax(m_bin.width(), m_bin.height()));
            const float reach = float(spriteHalf(icon, f.scale));
            QPointF heading(std::cos(f.walkAngle), std::sin(f.walkAngle));
            const float vl = float(std::hypot(f.vel.x(), f.vel.y()));
            if (vl > 0.001f)
                heading = f.vel / vl;
            // Whichever is further out: the fly's own edge, or where it
            // will be shortly. At crawl pace those are within a pixel or
            // two of each other, so both matter — the travel term alone
            // was what let a stationary-ish fly drift its wing over the
            // rim, and the size term alone would not see a bolting one.
            const QPointF ahead = f.pos + heading * qMax(reach, vl * 0.25f);
            if (!onSurfaceAt(ahead)) {
                const QPointF back = f.landTarget - f.pos;
                const float bl = float(std::hypot(back.x(), back.y()));
                if (bl > 0.001f)
                    desired = (back / bl) * speedMax;
            }
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
                               * std::min(3.2f, 1.0f + (nd - 1.0f) * 5.0f);
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
        // Scatter from the cursor. Strongest at the centre and falling to
        // nothing at the edge of the radius, so flies stream away rather
        // than all snapping outward the instant it comes into range.
        float fleeing = 0.0f;
        if (m_cursorPresent && !f.leaving) {
            const QPointF away = f.pos - m_cursor;
            const float ad = float(std::hypot(away.x(), away.y()));
            const float radius = params.fleeRadius * halfW * 2.0f;
            if (ad < radius) {
                fleeing = 1.0f - ad / radius;
                const QPointF u = ad > 0.001f
                    ? away / ad
                    : QPointF(std::cos(f.wanderAngle), std::sin(f.wanderAngle));
                // Referenced to FLYING speed, not the fly's current one: a
                // crawler's cruise is a few px/s, and scaling the escape
                // to that would have it stroll away from the cursor.
                const float esc = params.flySpeed * k;
                desired += u * esc * params.fleeForce * fleeing;
                // Only a pointer genuinely on top of a crawler scares it
                // off. Cancelling seekingLand anywhere in the radius meant
                // a pointer merely NEAR the bin stopped flies landing at
                // all, which is not what "scatter from the cursor" should
                // do — and the on-bin case is already handled wholesale,
                // by clearing the swarm.
                if (crawling && fleeing > 0.55f) {
                    enterMode(f, FlyMode::Flying);
                    f.onSurface = false;
                }
            }
        }
        if (fleeing > 0.0f)
            speedMax = std::max(speedMax, params.flySpeed * k * params.fleeSpeed * fleeing);
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
            if (!frozen && !paused && rnd() < params.jitterChance) {
                const float side = rnd() < 0.5f ? -1.0f : 1.0f;
                f.vel += QPointF(side, rndRange(-0.25f, 0.25f))
                         * params.jitterImpulse * k;
            }
        } else if (rnd() < params.dartChance * (0.4f + m_fullness)) {
            f.vel += QPointF(rndRange(-1, 1), rndRange(-1, 1))
                     * params.dartImpulse * k;
        }
        if (frozen || paused)
            f.vel = QPointF(0, 0);
        f.vel = limit(f.vel, speedMax * 1.6f);
        // Fliers always cruise. Steering terms can cancel — an outward
        // push against the containment shove, a seek against the curl —
        // and a flier whose speed collapses to nothing spins on the spot,
        // because the sprite's heading is read off the velocity vector.
        // Real flies don't hover in place; enforcing a floor along the
        // current heading removes the whole failure mode rather than
        // chasing the individual pairs of terms that cause it.
        if (!crawling && !frozen) {
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

        // Hard backstop on the roam region. The steering above is a
        // FORCE, and a force cannot guarantee anything: at cruise speed a
        // fly covers more ground in the time it takes to turn than the
        // region is wide, so it sailed out through the overlay edge and
        // got clipped by the window. This is deliberately outside the
        // soft boundary — normal flight never touches it — and it exists
        // so the overlay margins are a contract rather than a hope.
        {
            const QPointF rel2(f.pos.x() - c.x(), f.pos.y() - c.y());
            const float rs2 = crawling ? 1.0f : params.flyRoamScale;
            const float ry2 = (rel2.y() < 0 ? roamUp : roamDown) * rs2;
            const float nx2 = float(rel2.x()) / (roamX * rs2);
            const float ny2 = float(rel2.y()) / ry2;
            const float nd2 = std::sqrt(nx2 * nx2 + ny2 * ny2);
            constexpr float kHardEdge = 1.15f;
            if (nd2 > kHardEdge) {
                const float s = kHardEdge / nd2;
                f.pos = c + QPointF(rel2.x() * s, rel2.y() * s);
                // Drop the outward part of the velocity, keeping the
                // tangential part, so a fly grazes along the edge rather
                // than stopping dead against it.
                const QPointF n(nx2 / nd2 / qMax(0.001f, roamX * rs2),
                                ny2 / nd2 / qMax(0.001f, ry2));
                const float nl = float(std::hypot(n.x(), n.y()));
                if (nl > 0.0001f) {
                    const QPointF u = n / nl;
                    const float vn = float(QPointF::dotProduct(f.vel, u));
                    if (vn > 0.0f)
                        f.vel -= u * vn;
                }
            }
        }
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
        // Wings stop dead during the freeze. A fly holding still with its
        // wings still going would look like a rendering glitch.
        // Wings stop dead while still — during a startle freeze, and while
        // settling or waiting to take off.
        f.phase += (frozen || paused) ? 0.0f : dt * (crawling ? 9.0f : 40.0f);
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
