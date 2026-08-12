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

    /// Whether this fly draws in front of the bin rather than behind it.
    /// Only ever changed while the fly is clear of the icon — swapping
    /// depth on top of it would pop — and only a fly heading TOWARD the
    /// bin is allowed behind. A fly leaving the middle of the bin has
    /// nothing to go behind, so being masked on the way out looked wrong.
    bool    inFront  = false;
    /// Last known approach direction, so depth is re-decided on the
    /// transition rather than re-rolled every frame.
    bool    wasToward = false;
    /// Heading for the bin to land on it. Without this a fly could only
    /// land if its timer happened to expire while it was already over the
    /// bin — and once fliers were given a wider berth, that almost never
    /// happened and crawling all but vanished.
    bool    seekingLand = false;
    /// Low-passed steering direction. The curl field is sampled at the
    /// fly's position, so it changes as fast as the fly moves; feeding it
    /// straight into the velocity made flight twitch. Smoothing the
    /// DIRECTION rather than the velocity keeps the speed honest.
    QPointF dir;
    /// Crawling pace, re-rolled every second or so. A crawler that moves
    /// at one steady speed reads as a machine; real ones surge and stall.
    float   pace     = 1.0f;
    float   paceLeft = 0.0f;
    /// Startle: a crawler freezes dead still, then bolts off the bin.
    /// The stillness is the whole effect — it only reads as alarm because
    /// everything else on screen is in constant motion.
    float   freezeLeft = 0.0f;
    bool    bolting    = false;
    /// Ordinary stillness, as opposed to the startle freeze above. A fly
    /// settles when it lands and pauses again before taking off; neither
    /// of those ends in a bolt, so they can't share a timer with it.
    float   pauseLeft  = 0.0f;
    /// 0 = just landed and settling, 1 = walking, 2 = paused before it
    /// leaves. The visible sequence is land, stop, walk, stop, go.
    int     crawlStage = 0;
    float   boltLeft   = 0.0f;   // seconds of boosted speed remaining
    /// Seconds of scattering left before a fly driven off by the pointer
    /// is allowed to start fading. Without it they faded where they
    /// stood, and the pointer read as killing flies rather than
    /// scaring them.
    float   scatterLeft = 0.0f;

    /// Actually crawling ON the bin this frame. The renderer clips
    /// these to the silhouette rather than occluding them.
    bool    onSurface   = false;
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
    /// Cursor position in overlay-local pixels, and whether it's close
    /// enough to matter. Polled rather than delivered as events: the
    /// overlay is click-through, so it never receives hover at all.
    void setCursor(const QPointF &p, bool present);
    QRectF binRect() const { return m_bin; }

    /// Overlay half-size needed around the icon so the swarm never clips
    /// at the window edge. Measured, not guessed — tests/simtest.cpp
    /// asserts it, so retuning the steering can't silently start
    /// clipping. Asymmetric vertically because flies rise off the bin.
    /// The `+ sprite` term is the fly's own half-size: containment bounds
    /// the fly's centre, so without it the outermost fly is drawn half
    /// outside the window and gets cut in half.
    /// Sized for the FLYING roam region (roam* x flyRoamScale), not the
    /// crawling one — fliers range much wider, and margins fitted to the
    /// tighter region clipped them at the window edge.
    static int marginX(qreal iconSize)
    {
        return int(qMax(30.0, 0.98 * iconSize + spriteAllowance(iconSize)));
    }
    static int marginTop(qreal iconSize)
    {
        return int(qMax(34.0, 1.12 * iconSize + spriteAllowance(iconSize)));
    }
    static int marginBottom(qreal iconSize)
    {
        return int(qMax(24.0, 0.62 * iconSize + spriteAllowance(iconSize)));
    }

    /// Half-size of the drawn fly at this icon size. Kept in step with
    /// FlyItem's quad size by hand — the renderer owns the constant, the
    /// margins only need to leave room for it.
    static qreal spriteAllowance(qreal iconSize)
    {
        return 2.05 * 1.2 * qMax(1.0, iconSize / 40.0) + 2.0;
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
    QPointF cursor() const { return m_cursor; }
    /// True while the pointer is over the bin: the swarm clears out and
    /// stays away until it leaves.
    bool cursorOnBin() const { return m_cursorOnBin; }
    bool isIdle() const { return m_flies.isEmpty() && m_fullness < 0.01f; }

    // Tunables — exposed for the live-tweak panel in dev builds. All
    // distances are px at the 40pt reference tile and scale by
    // sizeScale(); all times are seconds.
    struct Params
    {
        int   minFlies       = 1;      // while the bin has anything in it
        int   maxFlies       = 6;

        float lifeMin        = 3.0f;   // then it leaves and is replaced
        float lifeMax        = 11.0f;
        float fadeTime       = 0.40f;  // arrival/departure ramp

        float crawlSpeed     = 6.0f;   // px/sec — a slow potter
        float flySpeed       = 104.0f;  // px/sec — actual flying

        // Flying is the default state; crawling is a brief visit. The
        // previous split had them settling almost as often as flying,
        // which read as a swarm that had given up.
        float crawlMin       = 4.2f;   // seconds spent in each mode
        float crawlMax       = 10.5f;
        float flyMin         = 0.5f;
        float flyMax         = 1.2f;

        /// Chance of actually settling when a flier passes over the bin
        /// with its timer up. Below 1 so most passes stay airborne.
        float landChance     = 0.95f;
        // Stillness either side of the walk. A fly that lands and
        // immediately starts trundling reads as a machine on rails; the
        // pause is what makes the landing look like a decision.
        float settleMin      = 0.30f;
        float settleMax      = 0.85f;
        float preFlightMin   = 0.25f;
        float preFlightMax   = 0.75f;
        // Stops DURING the walk, not just at either end. Dwell time on
        // the bin is now several seconds, and pausing only on arrival and
        // departure left a long uninterrupted trundle in the middle —
        // which is the machine-on-rails look the pauses exist to avoid.
        float restChance     = 0.020f; // per walking fly per step
        float restMin        = 0.25f;
        float restMax        = 1.10f;

        // Crawling twitches: infrequent and small. At the previous rate
        // (0.12 / 26px) a crawling fly was in near-constant spasm rather
        // than picking its way over the surface.
        float jitterChance   = 0.02f;  // per crawling fly per step
        float jitterImpulse  = 8.0f;   // quick sideways twitch
        /// How fast the drawn heading may rotate, rad/sec. Snapping it
        /// straight to the velocity vector made near-stationary flies
        /// spin on the spot, which is what "stuck and spinning" was.
        float turnRateCrawl  = 2.2f;
        float turnRateFly    = 6.0f;
        /// Flying flies range wider than crawling ones — they arc out
        /// around the bin rather than hugging it.
        float flyRoamScale   = 1.25f;

        // Curl-noise flow field. A divergence-free field means the flies
        // follow coherent swirls instead of each jittering independently,
        // which is what a plain random walk produced. Modelled on
        // Kelley/Ouellette-style swarm work (curl noise + interaction
        // forces + friction) rather than hand-tuned wander.
        float noiseScale     = 0.055f; // spatial frequency, 1/px
        float noiseGain      = 210.0f; // px/sec^2 at fullness 1
        float noiseDrift     = 0.35f;  // how fast the field itself evolves
        float friction       = 3.4f;   // velocity damping, 1/sec

        float separation     = 6.0f;   // px, personal space
        // Startle. Chance is per crawling fly per step, so at 60fps a
        // crawler is startled roughly every 8 seconds of crawling.
        float startleChance  = 0.002f;
        float freezeMin      = 0.35f;  // seconds held dead still
        float freezeMax      = 1.10f;
        float boltSpeed      = 2.1f;   // multiple of normal flying speed
        float boltFor        = 0.75f;  // seconds of boosted flight
        float scatterMin     = 0.30f;  // seconds of flight before fading out
        float scatterMax     = 0.65f;
        float scatterSpeed   = 2.6f;   // multiple of flying speed
        // Cursor scatter. Radius is a multiple of the icon's WIDTH, so it
        // tracks Dock magnification like everything else. Keep it tight:
        // at 1.9 it reached almost two icons either side, so a pointer
        // resting anywhere near the Dock held the whole swarm off the bin
        // and crawling stopped altogether.
        float fleeRadius     = 0.85f;
        float fleeForce      = 3.2f;   // multiple of flying speed
        float fleeSpeed      = 1.8f;   // speed cap while fleeing
        float dartChance     = 0.005f; // per flying fly per step
        float dartImpulse    = 24.0f;
        float easeRate       = 0.9f;   // fullness lerp per sec
        // Four passes in front of the bin for every one behind. Behind is the
        // more interesting trick, but it hides the fly — as the majority it
        // just read as the swarm avoiding the front of the bin.
        float frontShare     = 0.80f;  // fraction passing in front of the bin
        /// Fliers throttle back while over the bin — they slow to look
        /// at it rather than barrelling straight past.
        float overBinSlow    = 0.6f;

        // Roam region, as multiples of the icon's own size. Upward used
        // to be 1.35 — over an icon's full height above the bin, which
        // read as flies wandering off into the desktop rather than
        // bothering the rubbish.
        float roamX          = 0.55f;
        float roamUp         = 0.78f;
        float roamDown       = 0.42f;
        /// Constant inward bias, as a fraction of the roam region, applied
        /// everywhere rather than only at the boundary. Containment alone
        /// spreads flies evenly across the region; this concentrates them
        /// on the bin with occasional strays, which is what a swarm around
        /// rubbish actually looks like.
        float centrePull     = 0.6f;
    };
    Params params;

private:
    float  rnd();          // [0,1)
    /// Divergence-free 2D flow, as the curl of a scalar noise potential.
    QPointF curlAt(float x, float y) const;
    float  rndRange(float lo, float hi) { return lo + rnd() * (hi - lo); }
    void   spawnFly();
    void   enterMode(Fly &f, FlyMode m);
    int    desiredCount() const;

    QVector<Fly> m_flies;
    QRectF   m_bin;
    float    m_time = 0.0f;   // drives the noise field's evolution
    float    m_fullness       = 0.0f; // eased
    float    m_fullnessTarget = 0.0f;
    QPointF  m_cursor;
    bool     m_cursorPresent = false;
    bool     m_cursorOnBin   = false;
    uint32_t m_rngState;
};

} // namespace hyperbin
