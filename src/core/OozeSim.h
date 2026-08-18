// Ooze: green sludge coating the bin from the rim down, dripping off the
// coating's lower edge, receding when the trash goes.
//
// Almost nothing here. The shape lives in a signed distance field of the
// bin's own artwork, and the drips are generated in the fragment shader
// from time and a hash — so this class owns only the two things a shader
// cannot know: how far the coating has crept, and whether anything is
// still changing.
//
// The previous version simulated metaball gobs running down the bin. It
// is gone: the shape it produced never hugged the icon, because it was
// approximating with blobs what a distance field describes exactly.
#pragma once

#include <QRectF>
#include <cstdint>

namespace hyperbin {

class OozeSim
{
public:
    OozeSim() = default;

    void setBinRect(const QRectF &r) { m_bin = r; }
    void setFullness(float f);
    void step(float dt);

    QRectF binRect() const { return m_bin; }
    float  fullness() const { return m_target; }

    /// How far down the bin the coating has crept, 0..1. Eased, so a bin
    /// emptied in one go does not snap clean.
    float level() const { return m_level; }

    /// How full the bin READS, 0 to 1, over the level's own easing.
    ///
    /// Not the same question as level(), and the difference has caught
    /// something already. level() is how deep the gel is, and it is
    /// floored at minShare the instant the bin is dirty at all — so it
    /// only ever moves across the top third of its range, because a
    /// shallower smear than that cannot be seen through a translucent
    /// bin. Anything that wants to scale with how full the bin is, rather
    /// than with how deep the goo is, has to have that floor taken back
    /// off; used raw, level() makes a quarter-full bin look nearly the
    /// same as a full one.
    ///
    /// Taken off the eased level rather than from fullness() directly, so
    /// whatever reads this arrives with the goo rather than snapping the
    /// moment the bin changes.
    float fill() const
    {
        const float lo = params.maxLevel * params.minShare;
        const float span = params.maxLevel - lo;
        if (span <= 0.0f)
            return m_level > 0.0f ? 1.0f : 0.0f;
        const float f = (m_level - lo) / span;
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }
    /// Seconds since the coating first appeared. Drives the drip cycles.
    float time() const { return m_time; }

    /// Nothing drawn at all.
    bool isEmpty() const { return m_level <= 0.0005f; }

    /// Ooze drips for as long as there is anything to drip, so it only
    /// rests once it is gone — which is true of EVERY effect here, not just
    /// this one, and is why none of them carries a private frame-rate cap
    /// any more. This used to answer preferredFrameIntervalMs() with 33 and
    /// so ran at 30fps whatever the display could do. See the note in
    /// core/Effect.h.
    bool isAtRest() const { return isEmpty(); }

    /// How far a drip hangs, as a fraction of the icon — and the most
    /// the cycle ever stretches that. The margin below the bin is
    /// computed from BOTH, because they are the same number seen from two
    /// places: the drip curves peak at ~2.45x reach, and a margin that
    /// does not allow for it clips the drop off at the window edge just
    /// as it lets go. Kept here so the effect and the margin cannot drift
    /// apart.
    static constexpr float kDripReach   = 0.30f;
    static constexpr float kDripStretch = 2.45f;
    static int marginX(qreal iconSize);
    static int marginTop(qreal iconSize);
    static int marginBottom(qreal iconSize);

    struct Params
    {
        /// Fullness below which nothing seeps at all. A bin with one item
        /// in it is not oozing; the effect is a warning, not a constant.
        float startAt   = 0.15f;
        /// How far up the bin the coating reaches when the bin is full.
        ///
        /// All the way, now. It used to stop short so that some bin
        /// stayed visible above the sludge — otherwise the icon read as a
        /// green bin rather than a fouled one. That job belongs to the
        /// SHAPE now: the gel's surface is capped at the rim and the
        /// meniscus is cut against the bin's outline, so the lip and the
        /// contents are always visible however full it gets.
        float maxLevel  = 1.0f;
        /// Shape of the ramp from startAt to full. Squared was too steep
        /// — a bin at 40% showed almost nothing and a full one was
        /// entirely green, so the effect was invisible for most of its
        /// range.
        float ramp      = 0.85f;
        /// Where the level starts once the bin is dirty at all, as a
        /// share of maxLevel. Without a floor the ramp spends its first
        /// half producing a smear too shallow to see through a
        /// translucent bin — the effect existed but was not visible.
        float minShare  = 0.70f;
        /// How briskly the level converges on where it should be, as the
        /// natural frequency of a critically damped spring — roughly,
        /// 4.7 / this is the time to get most of the way there.
        ///
        /// A spring rather than a rate, because a rate is a straight
        /// line: the level set off at full speed, held it the whole way,
        /// and stopped dead on arrival. Nothing thick moves like that.
        /// This leaves at nothing, gathers pace, and settles in.
        float creepEase  = 3.4f;  // filling: about 1.4s
        float recedeEase = 5.0f;  // and draining: emptying is a relief
    };
    Params params;

private:
    QRectF m_bin;
    float  m_target = 0.0f;
    float  m_level  = 0.0f;
    float  m_vel    = 0.0f;   // level per second, for the ease
    float  m_time   = 0.0f;
};

} // namespace hyperbin
