// Procedural fly swarm. Platform-agnostic, no Qt GUI dependency beyond
// QPointF so this stays unit-testable headless.
//
// Motion is emergent rather than authored: each fly is a steering agent
// (Reynolds) combining wander, containment over the bin, separation, and
// an occasional dart. That gives continuous response to bin fullness —
// the swarm grows and agitates smoothly — instead of cutting between
// discrete authored states.
//
// Containment, not orbit: flies roam inside a region shaped like the bin
// and are only pushed back at its edge, so they crawl over it rather
// than circling at a fixed radius. The region is deliberately asymmetric
// — tight left/right so they stay on the icon, taller upward so they can
// rise off it like real flies do.
//
// Each fly also has a short life. They arrive, potter about for a few
// seconds, then leave, and a replacement fades in elsewhere. A fixed
// cast of flies circling forever reads as a screensaver; constant
// turnover reads as an infestation.
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>
#include <cstdint>

namespace hyperbin {

/// Flies alternate between actually flying and settling to crawl. The
/// mix is what stops the swarm looking like uniform drifting particles.
enum class FlyMode { Flying, Crawling };

struct Fly
{
    QPointF pos;         // px, overlay-local
    QPointF vel;         // px/sec
    float   wanderAngle; // rad, integrated per step for smooth heading drift
    float   phase;       // per-fly offset so wingbeat/size don't sync up
    float   scale;       // 0.8-1.2, breaks up visual uniformity

    FlyMode mode     = FlyMode::Flying;
    float   modeLeft = 0.0f; // seconds until it switches mode
    float   life     = 0.0f; // seconds until it leaves
    float   fade     = 0.0f; // 0-1 opacity, ramps on arrival and departure
    bool    leaving  = false;
};

/// Drives the swarm. Deterministic given a seed — same seed, same flight
/// path, which makes the visual regression test possible.
class FlySim
{
public:
    explicit FlySim(uint32_t seed = 0x9E3779B9);

    /// Overlay-local rect of the trash icon itself. Flies stay over this.
    ///
    /// Positions are carried with it: the rect is in window-local coords
    /// and the window moves and resizes under Dock magnification, so
    /// leaving fly positions alone makes the whole swarm jump away from
    /// the icon. Existing flies are translated and scaled to match.
    void setBinRect(const QRectF &r);
    QRectF binRect() const { return m_bin; }

    /// Overlay half-size needed around the icon so the swarm never clips
    /// at the window edge. Measured, not guessed — tests/simtest.cpp
    /// asserts it, so retuning the steering can't silently start
    /// clipping. Asymmetric vertically because flies rise off the bin.
    /// The `+ sprite` term is the fly's own half-size: containment bounds
    /// the fly's centre, so without it the outermost fly is drawn half
    /// outside the window and gets cut in half.
    static int marginX(qreal iconSize)
    {
        return int(qMax(30.0, 0.85 * iconSize + spriteAllowance(iconSize)));
    }
    static int marginTop(qreal iconSize)
    {
        return int(qMax(52.0, 2.1 * iconSize + spriteAllowance(iconSize)));
    }
    static int marginBottom(qreal iconSize)
    {
        return int(qMax(26.0, 0.7 * iconSize + spriteAllowance(iconSize)));
    }

    /// Half-size of the drawn fly at this icon size. Kept in step with
    /// FlyItem's quad size by hand — the renderer owns the constant, the
    /// margins only need to leave room for it.
    static qreal spriteAllowance(qreal iconSize)
    {
        return 3.5 * 1.2 * qMax(1.0, iconSize / 40.0) + 2.0;
    }

    /// Icon size relative to the 40pt Dock tile the tunables were set
    /// against. Multiplies distances and speeds.
    float sizeScale() const
    {
        const qreal d = qMax(m_bin.width(), m_bin.height());
        return d > 1.0 ? float(d / 40.0) : 1.0f;
    }

    /// 0.0 = empty, 1.0 = stuffed. Drives fly count and agitation.
    /// Changes are eased internally, so callers may set this abruptly.
    void setFullness(float f);
    float fullness() const { return m_fullnessTarget; }

    /// Advance by dt seconds. Safe to call with irregular dt.
    void step(float dt);

    const QVector<Fly> &flies() const { return m_flies; }

    /// True when the swarm has fully dispersed and rendering can stop.
    /// Fullness 0 alone isn't enough — existing flies leave first.
    bool isIdle() const { return m_flies.isEmpty() && m_fullness < 0.01f; }

    // Tunables — exposed for the live-tweak panel in dev builds. All
    // distances are px at the 40pt reference tile and scale by
    // sizeScale(); all times are seconds.
    struct Params
    {
        int   minFlies       = 1;      // while the bin has anything in it
        int   maxFlies       = 6;

        float lifeMin        = 2.0f;   // then it leaves and is replaced
        float lifeMax        = 6.0f;
        float fadeTime       = 0.45f;  // arrival/departure ramp

        float crawlSpeed     = 7.0f;   // px/sec — a slow potter
        float flySpeed       = 46.0f;  // px/sec — actual flying
        float crawlMin       = 0.6f;   // seconds spent in each mode
        float crawlMax       = 2.2f;
        float flyMin         = 0.5f;
        float flyMax         = 1.8f;

        float jitterChance   = 0.12f;  // per crawling fly per step
        float jitterImpulse  = 26.0f;  // quick sideways twitch

        float wanderStrength = 5.0f;
        float separation     = 6.0f;   // px, personal space
        float dartChance     = 0.015f; // per flying fly per step
        float dartImpulse    = 40.0f;
        float easeRate       = 0.9f;   // fullness lerp per sec

        // Roam region, as multiples of the icon's own size. Tight
        // horizontally, generous upward.
        float roamX          = 0.52f;
        float roamUp         = 1.35f;
        float roamDown       = 0.45f;
    };
    Params params;

private:
    float  rnd();          // [0,1)
    float  rndRange(float lo, float hi) { return lo + rnd() * (hi - lo); }
    void   spawnFly();
    void   enterMode(Fly &f, FlyMode m);
    int    desiredCount() const;

    QVector<Fly> m_flies;
    QRectF   m_bin;
    float    m_fullness       = 0.0f; // eased
    float    m_fullnessTarget = 0.0f;
    uint32_t m_rngState;
};

} // namespace hyperbin
