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
    /// Seconds since the coating first appeared. Drives the drip cycles.
    float time() const { return m_time; }

    /// Nothing drawn at all.
    bool isEmpty() const { return m_level <= 0.0005f; }

    /// Ooze drips for as long as there is anything to drip, so it only
    /// rests once it is gone. What is done about the cost of that is in
    /// preferredFrameIntervalMs() — an effect that cannot stop should at
    /// least ask to be run slowly.
    bool isAtRest() const { return isEmpty(); }

    int preferredFrameIntervalMs() const { return params.frameIntervalMs; }

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
        /// How far down the bin the coating reaches when the bin is full.
        // Never all the way to the contents: some bin has to stay visible
        // above the sludge or it reads as a green bin rather than a
        // fouled one.
        float maxLevel  = 0.88f;
        /// Shape of the ramp from startAt to full. Squared was too steep
        /// — a bin at 40% showed almost nothing and a full one was
        /// entirely green, so the effect was invisible for most of its
        /// range.
        float ramp      = 1.15f;
        /// Where the level starts once the bin is dirty at all, as a
        /// share of maxLevel. Without a floor the ramp spends its first
        /// half producing a smear too shallow to see through a
        /// translucent bin — the effect existed but was not visible.
        float minShare  = 0.42f;
        float creepRate = 0.22f;  // level per second, filling
        float recedeRate = 0.55f; // and draining: emptying is a relief
        int   frameIntervalMs = 33;
    };
    Params params;

private:
    QRectF m_bin;
    float  m_target = 0.0f;
    float  m_level  = 0.0f;
    float  m_time   = 0.0f;
};

} // namespace hyperbin
