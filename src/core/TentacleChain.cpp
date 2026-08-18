#include "TentacleChain.h"

#include <algorithm>
#include <cmath>

namespace hyperbin {
namespace {

/// Normalise, or fall back — a zero-length segment happens the moment two
/// joints coincide, which FABRIK can do transiently when the target sits
/// exactly on a joint.
QVector3D dir(const QVector3D &v, const QVector3D &fallback)
{
    const float l = v.length();
    return l > 1e-5f ? v / l : fallback;
}

} // namespace

void TentacleChain::reset(const QVector3D &base, const QVector3D &d, float length)
{
    m_seg = length / float(kJoints - 1);
    const QVector3D u = dir(d, QVector3D(0, 1, 0));
    for (int i = 0; i < kJoints; ++i)
        m_p[i] = base + u * (m_seg * float(i));
    buildFrames();
}

void TentacleChain::solve(const QVector3D &base, const QVector3D &emerge,
                          const QVector3D &target,
                          float time, float phase, float flex, float maxBend)
{
    if (!valid())
        return;

    // The showcase's four stages, in its order. The last one is not
    // redundant: the wave displaces joints off the chain, which stretches
    // every segment it touches, and without a second constraint pass the
    // arm visibly lengthens and shortens as the wave runs down it.
    // Real seconds, from the clock the caller already passes. Clamped
    // because the first frame has no previous time and a stall must not
    // let the bank jump.
    const float dt = m_prevTime < 0.0f ? 0.016f
                                       : std::clamp(time - m_prevTime, 0.0f, 0.1f);
    m_prevTime = time;
    for (int i = 0; i < kJoints; ++i)
        m_was[i] = m_p[i];
    fabrik(base, target);
    constrain(base, maxBend);
    holdRoot(base, emerge);
    // Once a frame, before anything reads the frames. buildFrames runs
    // three times over a step — twice here and again from settle — so
    // updating the bank inside it would apply the rate limit three times
    // and make the speed depend on how often the frames are rebuilt.
    updateRoll(dt);
    // FRAMES BEFORE THE WAVE, and this is not tidiness — it is what stops
    // the arm shaking.
    //
    // The wave displaces along m_side. Built only at the END of solve,
    // m_side described a chain that already had the previous frame's wave
    // in it, so the wave's direction depended on its own last output: a
    // feedback loop, and one with enough gain to matter. Measured with the
    // target held completely still, joints were lurching up to 49 units a
    // frame with a median of 7, which is the "sticking then jittering"
    // exactly. Deriving the frame from the solved but UN-waved pose breaks
    // the loop — the wave now rides on a shape that is a pure function of
    // the target.
    buildFrames();
    applyWave(time, phase, flex);
    constrain(base, maxBend);
    holdRoot(base, emerge);
    // Adopt only part of the new pose. Run BEFORE the last constraint pass
    // so the blend cannot leave segments the wrong length — a lerp between
    // two valid chains is not itself a valid chain.
    //
    // Less of it the further out the joint is, so the tip arrives after the
    // root does. See kTipLag.
    for (int i = 1; i < kJoints; ++i) {
        const float s = float(i) / float(kJoints - 1);
        const float adopt = kAdopt * (1.0f - kTipLag * s);
        m_p[i] = m_was[i] + (m_p[i] - m_was[i]) * adopt;
    }
    constrain(base, maxBend);
    holdRoot(base, emerge);
    buildFrames();
}

void TentacleChain::fabrik(const QVector3D &base, const QVector3D &target)
{
    for (int it = 0; it < kSolveIterations; ++it) {
        // Backward: put the tip on the target and walk down, each joint
        // pulled to a segment's distance from the one above it. The base
        // ends up wherever it ends up.
        m_p[kJoints - 1] = target;
        for (int i = kJoints - 2; i >= 0; --i)
            m_p[i] = m_p[i + 1]
                   + dir(m_p[i] - m_p[i + 1], QVector3D(0, -1, 0)) * m_seg;

        // Forward: put the base back where it belongs and walk up. The tip
        // now falls short of the target by however far it was out of
        // reach, which is the correct answer and needs no special case —
        // an arm asked for something beyond its length simply points at it.
        m_p[0] = base;
        for (int i = 1; i < kJoints; ++i)
            m_p[i] = m_p[i - 1]
                   + dir(m_p[i] - m_p[i - 1], QVector3D(0, 1, 0)) * m_seg;
    }
}

void TentacleChain::constrain(const QVector3D &base, float maxBend)
{
    // Bilateral: where a segment is the wrong length, both of its ends
    // move half the error towards fixing it, and the correction ripples
    // outward over the iterations. Moving only the outer joint instead
    // pushes every error to the tip, where it accumulates into a visible
    // flick.
    for (int it = 0; it < kConstraintIterations; ++it) {
        m_p[0] = base;
        for (int i = 0; i < kJoints - 1; ++i) {
            const QVector3D d = m_p[i + 1] - m_p[i];
            const float len = d.length();
            if (len < 1e-5f)
                continue;
            const QVector3D fix = d * ((len - m_seg) / len * 0.5f);
            // The base is an anchor, so when it is one end of the segment
            // the other end takes the whole correction.
            if (i == 0) {
                m_p[i + 1] -= fix * 2.0f;
            } else {
                m_p[i] += fix;
                m_p[i + 1] -= fix;
            }
        }
        // Interleaved with the lengths rather than run once afterwards.
        // Each fights the other — straightening a kink stretches the
        // segments it moved, and fixing a length re-bends the joint — so
        // they have to converge together. Ten passes of both settles.
        limitBend(maxBend);
    }
    m_p[0] = base;
}

void TentacleChain::limitBend(float maxBend)
{
    QVector3D prev = dir(m_p[1] - m_p[0], QVector3D(0, 1, 0));
    for (int i = 1; i < kJoints - 1; ++i) {
        QVector3D cur = dir(m_p[i + 1] - m_p[i], prev);
        const float c = std::clamp(QVector3D::dotProduct(prev, cur), -1.0f, 1.0f);
        if (std::acos(c) > maxBend) {
            // Swung back to the limit in the plane the two segments
            // already share, so the joint bends less rather than
            // somewhere else. Built from the component of `cur` across
            // `prev`, which is that plane's second axis; if there is none
            // the two are collinear and there was nothing to fix.
            QVector3D perp = cur - prev * c;
            if (perp.lengthSquared() > 1e-10f) {
                perp.normalize();
                cur = prev * std::cos(maxBend) + perp * std::sin(maxBend);
            } else {
                cur = prev;
            }
            m_p[i + 1] = m_p[i] + cur * m_seg;
        }
        prev = cur;
    }
}

void TentacleChain::curlUp(const QVector3D &base, const QVector3D &emerge,
                           float turn, float blend)
{
    if (blend <= 0.0f) {
        m_curling = false;
        return;
    }
    const QVector3D t0 = dir(m_p[1] - m_p[0], dir(emerge, QVector3D(0, 1, 0)));
    // Which plane to coil in: the arm's own bend AT THE MOMENT THE ROLL
    // STARTS, so it rolls the way it was already going — and then that
    // plane is held, so the spiral cannot turn over underneath itself.
    if (!m_curling) {
        QVector3D axis = QVector3D::crossProduct(t0, m_p[kJoints - 1] - m_p[0]);
        if (axis.lengthSquared() < 1e-8f)
            axis = QVector3D::crossProduct(t0, QVector3D(0, 0, 1));
        if (axis.lengthSquared() < 1e-8f)
            axis = QVector3D::crossProduct(t0, QVector3D(1, 0, 0));
        m_curlAxis = axis.normalized();
        m_curling = true;
    }
    // Re-squared against the arm's current direction. The plane is held,
    // but the arm it belongs to still swings, and an axis that has drifted
    // out of perpendicular makes the spiral cone outward instead of
    // closing — Rodrigues below assumes the two are square.
    QVector3D axis = m_curlAxis - t0 * QVector3D::dotProduct(m_curlAxis, t0);
    if (axis.lengthSquared() < 1e-8f)
        axis = QVector3D::crossProduct(t0, QVector3D(0, 0, 1));
    if (axis.lengthSquared() < 1e-8f)
        axis = QVector3D::crossProduct(t0, QVector3D(1, 0, 0));
    axis.normalize();
    // Walk out from the base turning a little more at every joint. The
    // ramp is what makes it a roll-up rather than a circle: a constant
    // turn per joint IS a circle, and a fruit rollup is a spiral, tight at
    // the middle and open at the outside.
    QVector3D p = base;
    QVector3D d = t0;
    for (int i = 1; i < kJoints; ++i) {
        const float s = float(i) / float(kJoints - 1);
        // s^1.5 rather than s^2. The steeper ramp put nearly all of the
        // turning into the last few joints, which hit the bend limit and
        // stopped, so the total came out well under a full circle however
        // high `turn` went. A gentler ramp spreads the same total over
        // more joints and actually closes the spiral.
        const float a = turn * s * std::sqrt(s);
        // Rotate d about axis by a — Rodrigues, with d perpendicular to
        // axis by construction so the parallel term drops out.
        const QVector3D perp = QVector3D::crossProduct(axis, d);
        d = dir(d * std::cos(a) + perp * std::sin(a), d);
        p += d * m_seg;
        m_p[i] += (p - m_p[i]) * blend;
    }
}
void TentacleChain::settle(const QVector3D &base, const QVector3D &emerge,
                           float maxBend)
{
    constrain(base, maxBend);
    holdRoot(base, emerge);
    buildFrames();
}
void TentacleChain::pushOutside(float topY, float binHeight, float halfX,
                                float halfZ, float taper, int fromJoint)
{
    // ONCE OUT, STAY OUT.
    //
    // Walking the chain in order and remembering whether it has left the
    // bin yet is what settles an otherwise ambiguous question. A joint
    // below the lip and near the axis is either an arm still down in the
    // rubbish — which should stay hidden — or the curled-back tip of one
    // that went over the rim and is hanging outside, which must not be.
    // Position alone cannot tell those apart: they are the same point.
    //
    // Order along the chain can, because an arm cannot re-enter the bin
    // through its own wall. Everything past the first joint that is out is
    // also out. Without this, the tip of a slap hooked back toward the bin,
    // fell under the threshold that had been holding it in front, and was
    // cut off with a detached sliver left behind.
    bool escaped = false;
    for (int i = std::max(1, fromJoint); i < kJoints; ++i) {
        const float y = m_p[i].y();
        if (y >= topY) {
            escaped = true;   // in the opening, where it belongs
            continue;
        }
        const float below = std::clamp((topY - y) / std::max(binHeight, 1e-4f),
                                       0.0f, 1.0f);
        const float sx = halfX * (1.0f + (taper - 1.0f) * below);
        const float sz = halfZ * (1.0f + (taper - 1.0f) * below);
        const float ex = m_p[i].x() / std::max(sx, 1e-4f);
        const float ez = m_p[i].z() / std::max(sz, 1e-4f);
        const float r = std::sqrt(ex * ex + ez * ez);
        const float front = sz * std::sqrt(std::max(0.0f, 1.0f - ex * ex));

        if (r >= 1.0f || m_p[i].z() >= front) {
            escaped = true;   // already outside it, or already ahead of it
            continue;
        }

        if (escaped) {
            m_p[i].setZ(front * 1.04f);
            continue;
        }

        // Still on its way out, so the nearest surface is the right one —
        // this is an arm inside the opening and the shortest way out is the
        // way it came. Past two thirds of the way to the edge, though, the
        // nearest surface is the SIDE, and a joint parked on the side sits
        // at a small z where the mask correctly puts the bin in front of
        // it. Out sideways is not out in front.
        if (std::abs(ex) > 0.66f) {
            m_p[i].setZ(std::max(m_p[i].z(), front * 1.04f));
        } else if (r < 1e-3f) {
            m_p[i].setZ(sz * 1.02f);
        } else {
            const float k = 1.02f / r;
            m_p[i].setX(m_p[i].x() * k);
            m_p[i].setZ(m_p[i].z() * k);
        }
    }
}
void TentacleChain::holdRoot(const QVector3D &base, const QVector3D &emerge)
{
    // Pull the first joints back onto the line the arm leaves its hole
    // along, fading out over the held span so there is no crease where the
    // hold stops. Run AFTER the length pass and before the next one, so
    // whatever it moves gets its segments fixed again.
    const QVector3D u = dir(emerge, QVector3D(0, 1, 0));
    for (int i = 1; i <= kRootHeld && i < kJoints; ++i) {
        const float w = 1.0f - float(i - 1) / float(kRootHeld);
        const QVector3D want = base + u * (m_seg * float(i));
        m_p[i] += (want - m_p[i]) * w;
    }
}
void TentacleChain::applyWave(float time, float phase, float flex)
{
    // THIS is the part that makes it look alive, not the solver above.
    //
    // A sine laid across the chain, its phase advancing with distance
    // along the arm, so the crest travels from base to tip rather than the
    // whole arm flapping in step. Amplitude is deliberately NOT tapered —
    // the showcase leaves it constant, and tapering it to nothing at the
    // tip removes the motion from the one part of the arm that has the
    // freedom to show it.
    //
    // Two waves, not one. The showcase is 2D and has only a single axis to
    // displace along; in three dimensions a lone wave is a flat flap seen
    // edge-on half the time. The second runs across the first at a rate
    // that does not divide into it, which turns the flap into a slow
    // helical roll that never repeats.
    constexpr float kFrequency = 2.0f;
    constexpr float kSpeed = 3.0f;
    constexpr float kAmplitude = 0.040f;   // of the arm's own length

    // BREATH AND WANDER, and this is the difference between an undulation
    // and a machine running.
    //
    // Two fixed sines produce a wave of exactly one strength at exactly one
    // pace forever, and the eye finds that period quickly however
    // complicated the sum of them looks. Living motion gathers and eases:
    // it pushes harder for a stroke or two, then idles.
    //
    // The pace wander is added to the PHASE and not multiplied into the
    // time, which is the only form of this that works. Scaling time by a
    // varying factor makes the effective frequency d/dt[t*k(t)] = k + t*k',
    // so the modulation grows without bound and the wave ends up buzzing
    // after a few minutes. Displacing the phase speeds the crest up and
    // slows it down around a fixed mean and stays there.
    const float breath = 0.70f + 0.48f * std::sin(time * 0.23f + phase);
    const float wander = 0.90f * std::sin(time * 0.17f + phase * 1.9f);
    const float amp = kAmplitude * length() * flex * breath;
    if (amp <= 0.0f)
        return;

    for (int i = 1; i < kJoints; ++i) {
        const float s = float(i) / float(kJoints - 1);
        const QVector3D t = dir(m_p[i] - m_p[i - 1], QVector3D(0, 1, 0));
        const QVector3D side = dir(m_side[i] - t * QVector3D::dotProduct(m_side[i], t),
                                   QVector3D(1, 0, 0));
        const QVector3D up = QVector3D::crossProduct(t, side);

        const float a = time * kSpeed + phase + wander
                      + s * kFrequency * 6.28318531f;
        const float b = time * kSpeed * 0.61f + phase * 1.7f + wander * 0.7f
                      + s * kFrequency * 4.10318531f;
        // Held at the root. A wave that displaces the first joints pulls
        // the arm out of the hole it is supposed to be coming through.
        const float grip = s * s;
        m_p[i] += side * (std::sin(a) * amp * grip)
                + up   * (std::sin(b) * amp * 0.6f * grip);
    }
}

namespace {
/// Rotate v about axis by ang, for v perpendicular to axis — Rodrigues
/// without its parallel term.
QVector3D bankBy(const QVector3D &v, const QVector3D &axis, float ang)
{
    return v * std::cos(ang) + QVector3D::crossProduct(axis, v) * std::sin(ang);
}
} // namespace
void TentacleChain::updateRoll(float dt)
{
    // A BANK, in response to movement. Nothing is aimed at anything.
    //
    // Three earlier versions tried to point the frame somewhere — at the
    // inside of the curl, then at world down — and all of them failed the
    // same way. An aim is a property of the POSE: it jumps when the pose
    // wobbles, and it inverts outright when a bend passes through
    // straight, which is what made the suckers flip over. Worse, the frame
    // is also carried by transport, so bounding the correction bounded
    // nothing and the roll went round anyway.
    //
    // This asks a smaller question with a bounded answer: which way is the
    // arm sweeping, and lean into it, a little. Like a fish banking into a
    // turn. The frame underneath is untouched — the stable one that
    // shipped — and the whole chain turns together on top of it.
    const int j = kJoints * 2 / 3;
    const QVector3D tj = dir(m_p[j] - m_p[j - 1], QVector3D(0, 1, 0));
    // The outer half's travel this frame. The root is held and barely
    // moves, so including it only dilutes the direction with noise.
    QVector3D moved;
    for (int i = kJoints / 2; i < kJoints; ++i)
        moved += m_p[i] - m_was[i];
    m_travel += (moved - m_travel) * std::min(1.0f, dt * 6.0f);
    // How much of that travel is ACROSS the arm, along the frame's own
    // side axis — which is the direction a bank would show in. Measured
    // in segment lengths so it means the same on any size of bin.
    const QVector3D side = m_side[j] - tj * QVector3D::dotProduct(m_side[j], tj);
    float bank = 0.0f;
    if (side.lengthSquared() > 1e-6f && m_seg > 0.0f)
        bank = QVector3D::dotProduct(m_travel, side.normalized()) / m_seg;
    const float want = std::clamp(bank * kRollGain, -kRollLimit, kRollLimit);
    // Rate limited, so the arm is seen to roll rather than found rolled.
    m_roll += std::clamp(want - m_roll, -kRollRate * dt, kRollRate * dt);
    m_roll = std::clamp(m_roll, -kRollLimit, kRollLimit);
}
void TentacleChain::buildFrames()
{
    // Rotation-minimising, by projection: carry the previous joint's side
    // vector forward and take out whatever component the new tangent has
    // acquired. Rebuilding each frame from a fixed world axis instead
    // makes the frame spin wherever the arm passes near that axis, which
    // on this model rolls the sucker row round to the far side.
    QVector3D t0 = dir(m_p[1] - m_p[0], QVector3D(0, 1, 0));
    QVector3D s = QVector3D::crossProduct(t0, QVector3D(0, 0, 1));
    if (s.lengthSquared() < 1e-6f)
        s = QVector3D::crossProduct(t0, QVector3D(1, 0, 0));
    // The bank, laid on the seed. Transport carries it unchanged to every
    // joint, so one angle turns the entire arm and no two rings can
    // disagree — which is the difference between the arm rotating and the
    // mesh winding up along its length.
    m_side[0] = bankBy(s.normalized(), t0, m_roll);

    for (int i = 1; i < kJoints; ++i) {
        const QVector3D t = dir(m_p[i] - m_p[i - 1], t0);
        QVector3D carried = m_side[i - 1]
                          - t * QVector3D::dotProduct(m_side[i - 1], t);
        m_side[i] = dir(carried, m_side[i - 1]);
        t0 = t;
    }
}

} // namespace hyperbin
