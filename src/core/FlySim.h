// Procedural fly swarm. Platform-agnostic, no Qt GUI dependency beyond
// QPointF so this stays unit-testable headless.
//
// Motion is emergent rather than authored: each fly is a steering agent
// (Reynolds) combining wander, containment over the bin, separation, and
// an occasional dart. That gives continuous response to bin fullness —
// the swarm grows and agitates smoothly — instead of cutting between
// discrete authored states.
//
// Containment, not orbit: flies roam freely inside an ellipse matching
// the icon's proportions and are only pushed back at its edge, so they
// crawl over the bin rather than circling it at a fixed radius.
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>
#include <cstdint>

namespace hyperbin {

struct Fly
{
    QPointF pos;         // px, overlay-local
    QPointF vel;         // px/sec
    float   wanderAngle; // rad, integrated per step for smooth heading drift
    float   phase;       // per-fly offset so wingbeat/size don't sync up
    float   scale;       // 0.8-1.2, breaks up visual uniformity
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

    /// Overlay half-size needed around the icon so the swarm never clips
    /// at the window edge. Measured, not guessed: now that the flies are
    /// confined to the bin they reach ~1.15x the icon dimension at
    /// fullness 1, plus room for the sprite itself. tests/simtest.cpp
    /// asserts this, so retuning the steering can't silently clip.
    static int recommendedMargin(qreal iconSize)
    {
        return int(qMax(30.0, 1.5 * iconSize));
    }

    /// Icon size relative to the 40pt Dock tile the tunables were set
    /// against. Multiplies orbit radius, speeds and spacing.
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
    /// Fullness 0 alone isn't enough — existing flies fly off first.
    bool isIdle() const { return m_flies.isEmpty() && m_fullness < 0.01f; }

    // Tunables — exposed for the live-tweak panel in dev builds.
    /// All distances are in px at the 40pt reference tile and scale by
    /// sizeScale(). Speeds are deliberately low — these read as flies
    /// crawling over rubbish, not bees orbiting a hive.
    struct Params
    {
        int   maxFlies       = 10;
        float roamRadius     = 17.0f;  // how far from centre they stay
        float maxSpeed       = 30.0f;  // px/sec at fullness 1
        float minSpeed       = 12.0f;  // px/sec at fullness 0
        float wanderStrength = 4.5f;
        float separation     = 7.0f;   // px, personal space
        float dartChance     = 0.020f; // per fly per step
        float dartImpulse    = 34.0f;
        float easeRate       = 0.9f;   // fullness lerp per sec
    };
    Params params;

private:
    float  rnd();          // [0,1)
    float  rndRange(float lo, float hi) { return lo + rnd() * (hi - lo); }
    void   spawnFly();
    int    desiredCount() const;

    QVector<Fly> m_flies;
    QRectF   m_bin;
    float    m_fullness       = 0.0f; // eased
    float    m_fullnessTarget = 0.0f;
    uint32_t m_rngState;
};

} // namespace hyperbin
