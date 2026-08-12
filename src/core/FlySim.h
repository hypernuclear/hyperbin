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
#include <cmath>
#include <cstdint>

namespace hyperbin {

/// Flies alternate between actually flying and settling to crawl. The
/// mix is what stops the swarm looking like uniform drifting particles.
enum class FlyMode { Flying, Crawling };

struct Fly
{
    /// Stable for the fly's whole life. The vector index is not — flies
    /// are removed from the middle — so anything following one fly over
    /// time (debugging, tests) needs this.
    quint32 id = 0;
    QPointF pos;         // px, overlay-local
    QPointF vel;         // px/sec
    float   wanderAngle; // rad, integrated per step for smooth heading drift
    float   phase;       // per-fly offset so wingbeat/size don't sync up
    float   scale;       // 0.8-1.2, breaks up visual uniformity

    FlyMode mode     = FlyMode::Flying;
    float   modeLeft = 0.0f; // seconds until it switches mode
    /// Told to go, but not yet fading — it still has to get clear of the
    /// bin first. Flies have no lifespan; see the lifecycle note in
    /// step() for the only three things that set this.
    bool    retiring  = false;
    float   retireFor = 0.0f;
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
    /// Where on the bin this fly is heading to land. Chosen from the
    /// silhouette, so it doesn't aim at a hole in the artwork.
    QPointF landTarget;
    /// Persistent walking direction. A crawler used to take its heading
    /// from the noise field, which changes as fast as it moves, so it
    /// pivoted constantly instead of walking anywhere.
    float   walkAngle = 0.0f;
    /// Seconds left of the takeoff ramp. Flight speed is an envelope,
    /// not a constant: a fly that leaves the bin at full cruise and
    /// arrives at full cruise reads as being on rails.
    float   takeoffLeft = 0.0f;
    /// This flight is a long circuit rather than a hop to the next spot
    /// on the bin. Set when the flight starts, so the landing roll knows
    /// to leave this one alone.
    bool    longFlight  = false;
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

    /// The bin's SILHOUETTE, as a coarse coverage grid over binRect —
    /// 1 where the artwork is solid enough to stand on, 0 where it isn't.
    ///
    /// This exists because the sim and the renderer used to disagree
    /// about what "on the bin" meant. The sim landed flies anywhere in
    /// the bounding rect; the shader clips crawling flies to the artwork's
    /// alpha. The artwork covers well under half of its own tile, so most
    /// landed flies stood on empty tile and were clipped away — the sim
    /// reported a busy bin and the screen showed an empty one.
    ///
    /// Pass an empty grid to fall back to the bounding rect (dev mode,
    /// or before the icon has loaded).
    void setSurface(const QVector<quint8> &coverage, int w, int h);

    /// Is this point on the bin's actual surface?
    bool onSurfaceAt(const QPointF &p) const;
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

    /// How much bigger the DRAWN fly gets as the icon grows.
    ///
    /// Deliberately not proportional at either end, and this is a
    /// different question from how far a fly travels — motion has to stay
    /// proportional (a fly crossing a small bin crosses a small distance),
    /// but a sprite that shrinks with it stops reading as a fly.
    ///
    /// Below the reference tile: a floor. The sprite is only ~2pt across
    /// at k=1, so a 16pt Dock tile (k=0.4) would put it under a pixel.
    ///
    /// Above it: a 0.75 power rather than k. Proportional growth pinned
    /// the fly at a flat 10% of the icon at every size above 40pt, which
    /// is too small to read at rest and no larger, relatively, when the
    /// Dock magnifies. Compressing the top end lets the base constant come
    /// up — making flies bigger where they are actually looked at —
    /// without a magnified tile getting cartoon flies.
    static qreal renderScale(qreal iconSize)
    {
        const qreal k = iconSize > 1.0 ? iconSize / 40.0 : 1.0;
        return qMax(std::pow(k, 0.75), 0.72 + 0.28 * k);
    }

    // ---- THE FLY SIZE DIAL ------------------------------------------
    /// Visible length of a fly at the 40pt reference tile, in points.
    /// **This is the number to edit.**
    ///
    /// Stated as what you SEE, because the quad is not what you see: the
    /// sprite's dark body+head spans only ~58% of its texture cell, the
    /// rest being swept-back wings, a drop shadow and transparent margin.
    /// So the old half-size constant of 2.05 drew a 4.1pt quad containing
    /// a 2.4pt fly — which is why "make it 15% bigger" did nothing
    /// visible, and why tuning the quad size directly feels unresponsive.
    ///
    ///   value   fly at a 40pt tile   fly on a 48pt Recycle Bin
    ///     2.4    (the old size)      2.8pt   — a speck
    ///     7.0    7pt                 8pt     — reads as an insect
    ///    10.0    10pt                11.5pt  — unmissable, cartoonish
    ///
    /// HYPERBIN_FLY_SIZE multiplies this at runtime for quick A/B, but
    /// editing here is the intended way to settle it.
    static constexpr qreal kFlyLengthAt40 = 4.5;

    /// How much of the sprite's quad the visible fly actually fills. The
    /// rest is wings, drop shadow and transparent margin. Shared so the
    /// size dial and the crawl steering agree on where a fly's nose is.
    static constexpr qreal kInkFraction = 0.58;

    /// Half-size of the drawn QUAD at the reference tile, before the
    /// per-fly 0.8–1.2 variation. Derived from kFlyLengthAt40 — tune that.
    static qreal spriteBase();

    /// Half-size of one fly's drawn QUAD — the radius the mask can slice.
    ///
    /// Wings, not body: the dark body reaches only kInkFraction of the way
    /// out, but the wings sweep nearly to the quad's corners and are what
    /// visibly gets cut. Clearance has to be judged on the widest part, or
    /// a fly keeps its head at the rim and loses a wing instead.
    static qreal spriteHalf(qreal iconSize, float flyScale)
    {
        return spriteBase() * renderScale(iconSize) * qreal(flyScale);
    }

    /// Half-size of the drawn fly at this icon size — the bound the
    /// margins have to leave room for.
    ///
    /// Derived from the same two functions the renderer draws with rather
    /// than a hand-copied constant. They used to be independent numbers
    /// "kept in step by hand", which is a clipped swarm waiting to happen
    /// the first time only one of them is retuned.
    static qreal spriteAllowance(qreal iconSize)
    {
        return spriteHalf(iconSize, 1.2f) + 2.0;   // 1.2 = the largest fly
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

        // No lifespan any more: flies leave only when the pointer
        // arrives, the bin empties, or the swarm has to shrink. The old
        // 3-11s expiry read as restlessness rather than as life.
        float fadeTime       = 0.40f;  // arrival/departure ramp

        float crawlSpeed     = 9.0f;   // px/sec — a slow potter
        float flySpeed       = 350.0f; // px/sec, cruise

        // Speed envelope across a flight: push off gently, cruise, then
        // ease down onto the bin. Constant speed was the single most
        // mechanical-looking thing left in the swarm.
        float takeoffRamp    = 0.55f;  // seconds spent getting up to speed
        float takeoffSpeed   = 0.38f;  // fraction of cruise at takeoff
        float approachSlow   = 0.40f;  // fraction of cruise on touchdown
        float approachRange  = 1.10f;  // approach begins, in icon widths

        // Every so often a fly takes a proper lap instead of hopping
        // straight back down. Without this every flight is the same
        // length, which is its own kind of mechanical.
        float longFlightChance = 0.30f;
        float longFlyMin     = 2.6f;
        float longFlyMax     = 8.0f;

        // Mode durations. Landing dominates: the intended read is flies
        // that live ON the bin and take off now and then, not flies that
        // circle it and touch down rarely.
        float crawlMin       = 5.0f;   // seconds spent in each mode
        float crawlMax       = 13.0f;
        float flyMin         = 0.5f;
        float flyMax         = 2.6f;

        /// Chance of settling when a flier's timer is up. A fly on a long
        /// circuit is exempt — it is out for a lap, not a hop.
        float landChance     = 0.95f;
        /// Flies that must be ON the bin at any moment, traffic
        /// permitting. Independent per-fly timers alone can't hold a
        /// population invariant: they drift, and the bin goes bare for
        /// seconds at a time purely by chance. preferLanded is above the
        /// minimum because landing is restricted to flies in front, so one
        /// that arrives behind has to come round before it can land.
        int   minLanded      = 1;
        int   preferLanded   = 3;

        // Stillness either side of the walk, and during it. A fly that
        // lands and immediately starts trundling reads as a machine on
        // rails; the pauses are what make the landing look like a
        // decision.
        float settleMin      = 0.30f;  // held still on touchdown
        float settleMax      = 0.85f;
        float preFlightMin   = 0.25f;  // held still before taking off
        float preFlightMax   = 0.75f;
        float restChance     = 0.012f; // per walking fly per step
        float restMin        = 0.20f;
        float restMax        = 0.80f;
        /// How fast a walking fly's heading drifts, in rad/s. Low: the
        /// point is that it holds a line long enough to cover ground.
        float walkWander     = 3.0f;

        float jitterChance   = 0.02f;  // per crawling fly per step
        float jitterImpulse  = 8.0f;   // px/sec sideways twitch
        float turnRateCrawl  = 2.2f;   // rad/sec the sprite may rotate
        float turnRateFly    = 6.0f;
        /// Fliers range wider than crawlers, as a multiple of the roam
        /// region below.
        float flyRoamScale   = 1.25f;

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
        // Relative to cruise, and cruise is now 165 — at 2.1 a startled fly
        // outran the containment shove entirely and left the overlay.
        float boltSpeed      = 1.45f;   // multiple of normal flying speed
        float boltFor        = 0.75f;  // seconds of boosted flight
        float scatterMin     = 0.30f;  // seconds of flight before fading out
        float scatterMax     = 0.65f;
        float scatterSpeed   = 1.9f;   // multiple of flying speed
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
        float frontShare     = 0.60f;  // fraction passing in front of the bin
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
    /// Retire surplus flies when the bin has less in it than before.
    void   trimSwarm(int want);
    /// A point on the bin's surface, for a fly to aim at. Falls back to
    /// the rect centre when no silhouette is known.
    QPointF surfaceTarget();
    /// Steer toward the surface, and hold the population of landed flies
    /// at the configured floor.
    void    manageLandings();
    int    desiredCount() const;

    QVector<Fly> m_flies;
    QRectF   m_bin;
    /// Coverage grid over m_bin; empty means "no silhouette known".
    QVector<quint8> m_cov;
    int      m_covW = 0, m_covH = 0;
    /// Cell indices that are solid, for picking a landing spot that is
    /// actually on the bin rather than merely inside its bounding box.
    QVector<int> m_covSolid;
    /// Solid cells whose neighbours are solid too — the silhouette eroded
    /// by one cell. Landing here rather than anywhere solid keeps a fly's
    /// whole body on the artwork; touching down on a rim cell put its nose
    /// over the edge, where the mask cut it off the moment it arrived.
    QVector<int> m_covInner;
    float    m_time = 0.0f;   // drives the noise field's evolution
    float    m_fullness       = 0.0f; // eased
    float    m_fullnessTarget = 0.0f;
    QPointF  m_cursor;
    bool     m_cursorPresent = false;
    bool     m_cursorOnBin   = false;
    uint32_t m_rngState;
    quint32  m_nextId = 1;
};

} // namespace hyperbin
