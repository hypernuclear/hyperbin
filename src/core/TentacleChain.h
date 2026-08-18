// A tentacle as a chain of joints, solved by FABRIK.
//
// This replaced a circular arc, and the arc is worth describing because it
// is exactly why the old motion read as stiff: the whole arm was two
// numbers, a total bend and a direction, plus a small two-frequency
// wobble. An arc has CONSTANT curvature by definition — it is the least
// interesting curve that is not a straight line — so no amount of tuning
// those two numbers could produce anything that looked alive.
//
// The technique is taken from Smitner-Studio/tentacle-arm-showcase (MIT),
// a 2D Godot demo. FABRIK itself is pure vector arithmetic, so it carries
// into 3D unchanged. What is worth knowing before reading further is that
// FABRIK is NOT what makes that demo look good: it only pulls the chain at
// a target and keeps the segments from stretching. The life comes from a
// travelling sine wave laid on afterwards — see applyWave — and their own
// numbers say so, two solver iterations against ten constraint ones.
//
// Headless on purpose, in core/ rather than effects/, so tests/simtest.cpp
// can exercise it without a window.
#pragma once

#include <QVector3D>

namespace hyperbin {

class TentacleChain
{
public:
    /// Joints along one arm. The showcase uses 24 for a 128px line; this
    /// is drawn a tenth that size at a Dock icon and the mesh laid over it
    /// has around forty rings, so past sixteen the chain is finer than
    /// anything sampling it.
    static constexpr int kJoints = 16;

    /// The most one segment may turn against the one before it.
    ///
    /// FABRIK on its own has no idea that an arm is a physical thing: give
    /// it a target well inside its reach and it will happily fold the
    /// chain back through itself, because a folded chain satisfies every
    /// length constraint perfectly. The first render with a real solver
    /// came out crumpled and self-intersecting for exactly that reason.
    ///
    /// Fifteen segments at 22 degrees is 330 degrees of curl available,
    /// which covers a strike over the rim and down the outside at about
    /// 250. A full coil needs more than 360 and asks for it explicitly.
    static constexpr float kMaxBend = 0.384f;   // 22 degrees
    /// Enough for one and a half turns, for the moves that curl right up.
    static constexpr float kCoilBend = 0.62f;   // 35 degrees


    /// Straighten the arm out from `base` along `dir`, `length` long.
    /// Called once, and again if the bin resizes — never per frame, or the
    /// chain loses the state that makes it lag behind its target.
    void reset(const QVector3D &base, const QVector3D &dir, float length);

    /// Advance one frame: reach for `target`, keep the segments honest,
    /// then undulate. `flex` scales the wave — an arm mid-strike should
    /// snap, not ripple.
    /// `maxBend` is the most one joint may turn against the one before
    /// it, in radians. A parameter and not a constant because it is what
    /// separates a reach from a coil: at the default an arm cannot close a
    /// circle over its sixteen joints, and a move that is supposed to curl
    /// right round needs to be allowed to.
    /// `emerge` is the direction the arm leaves its hole in. The first
    /// few joints are held onto it — see holdRoot for why that is not
    /// optional once an arm is asked to reach a long way sideways.
    void solve(const QVector3D &base, const QVector3D &emerge,
               const QVector3D &target,
               float time, float phase, float flex, float maxBend);

    bool valid() const { return m_seg > 0.0f; }
    float length() const { return m_seg * float(kJoints - 1); }

    const QVector3D *joints() const { return m_p; }
    /// One unit vector per joint, across the arm. Carried along the chain
    /// rather than recomputed per joint: the arm's suckers are on ONE
    /// side, so an unstable frame does not merely wobble the shading, it
    /// rolls the suckers round the arm.
    const QVector3D *sides() const { return m_side; }

private:
    void fabrik(const QVector3D &base, const QVector3D &target);
    void constrain(const QVector3D &base, float maxBend);
    void limitBend(float maxBend);
    void holdRoot(const QVector3D &base, const QVector3D &emerge);
    /// How many joints the hole grips.
    ///
    /// FABRIK anchors the base POSITION and nothing else, so an arm asked
    /// for something far to one side satisfies it by swinging everything
    /// including its first segment — the anchor stays put but the arm
    /// leaves it at a new angle, and what the eye reads as "the base" is
    /// the first visible stretch, not the anchored point. Wrapping round
    /// the bin looked like the whole tentacle sliding around the rim.
    ///
    /// Holding four of sixteen is about a quarter of the arm, which is
    /// roughly what is buried in the rubbish and passing through the
    /// opening — the part that physically could not swing.
    static constexpr int kRootHeld = 4;
    void applyWave(float time, float phase, float flex);
    void buildFrames();


    /// Two passes at reaching, ten at not stretching — the showcase's own
    /// ratio, and it is the right way round. Reaching converges fast and
    /// approximately, which is all a tentacle needs; segment lengths that
    /// drift are immediately visible as an arm that grows and shrinks.
    static constexpr int kSolveIterations = 2;
    static constexpr int kConstraintIterations = 10;

    QVector3D m_p[kJoints];
    QVector3D m_side[kJoints];
    float m_seg = 0.0f;
};

} // namespace hyperbin
