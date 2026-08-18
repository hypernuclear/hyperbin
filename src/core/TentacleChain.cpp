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
    for (int i = 0; i < kJoints; ++i)
        m_was[i] = m_p[i];
    fabrik(base, target);
    constrain(base, maxBend);
    holdRoot(base, emerge);
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
    for (int i = 1; i < kJoints; ++i)
        m_p[i] = m_was[i] + (m_p[i] - m_was[i]) * kAdopt;
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
    if (blend <= 0.0f)
        return;
    // Which plane to coil in: the arm's own current bend, so it rolls the
    // way it was already going rather than snapping into some world axis.
    const QVector3D t0 = dir(m_p[1] - m_p[0], dir(emerge, QVector3D(0, 1, 0)));
    QVector3D axis = QVector3D::crossProduct(t0, m_p[kJoints - 1] - m_p[0]);
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
    const float amp = kAmplitude * length() * flex;
    if (amp <= 0.0f)
        return;

    for (int i = 1; i < kJoints; ++i) {
        const float s = float(i) / float(kJoints - 1);
        const QVector3D t = dir(m_p[i] - m_p[i - 1], QVector3D(0, 1, 0));
        const QVector3D side = dir(m_side[i] - t * QVector3D::dotProduct(m_side[i], t),
                                   QVector3D(1, 0, 0));
        const QVector3D up = QVector3D::crossProduct(t, side);

        const float a = time * kSpeed + phase + s * kFrequency * 6.28318531f;
        const float b = time * kSpeed * 0.61f + phase * 1.7f
                      + s * kFrequency * 4.10318531f;
        // Held at the root. A wave that displaces the first joints pulls
        // the arm out of the hole it is supposed to be coming through.
        const float grip = s * s;
        m_p[i] += side * (std::sin(a) * amp * grip)
                + up   * (std::sin(b) * amp * 0.6f * grip);
    }
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
    m_side[0] = s.normalized();

    for (int i = 1; i < kJoints; ++i) {
        const QVector3D t = dir(m_p[i] - m_p[i - 1], t0);
        QVector3D carried = m_side[i - 1]
                          - t * QVector3D::dotProduct(m_side[i - 1], t);
        m_side[i] = dir(carried, m_side[i - 1]);
        t0 = t;
    }
}

} // namespace hyperbin
