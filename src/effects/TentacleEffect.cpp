#include "TentacleEffect.h"

#include "IconTexture.h"
#include "SpineTexture.h"

#include <algorithm>
#include <cmath>

namespace hyperbin {

TentacleEffect::TentacleEffect(QObject *parent)
    : Effect(parent)
    , m_iconTexture(new IconTexture)
    , m_spine(new SpineTexture)
{
}

TentacleEffect::~TentacleEffect()
{
    delete m_iconTexture;
    delete m_spine;
}

QUrl TentacleEffect::visualSource() const
{
    return QUrl(QStringLiteral("qrc:/qt/qml/hyperbin/qml/TentacleVisual.qml"));
}

QObject *TentacleEffect::iconTexture() const
{
    return m_iconTexture;
}

QObject *TentacleEffect::spineTexture() const
{
    return m_spine;
}

void TentacleEffect::setBinImage(const QImage &img)
{
    m_iconTexture->setImage(img);
    emit shapeChanged();
}

void TentacleEffect::setBinRect(const QRectF &binRect)
{
    if (m_binRect == binRect)
        return;
    m_binRect = binRect;
    m_binSize = binRect.size();
    emit shapeChanged();
}

void TentacleEffect::setSurface(const QVector<quint8> &, int, int)
{
    // Nothing here reads the silhouette. What this effect needs to know
    // about the bin's shape is where its OPENING is, and the coverage
    // grid cannot answer that — the near lip runs through the middle of
    // the silhouette, not around its edge. See setMouth.
}

void TentacleEffect::setContentLine(float)
{
    // Where the rubbish starts does not matter to something that comes
    // out from under it.
}

void TentacleEffect::setMouth(const BinMouth &mouth)
{
    m_mouth = mouth;
    emit shapeChanged();
}

void TentacleEffect::setFullness(float fullness)
{
    m_target = std::clamp(fullness, 0.0f, 1.0f);
}

void TentacleEffect::setCursor(const QPointF &pos, bool present)
{
    m_cursor = pos;
    m_cursorOn = present;
}

void TentacleEffect::step(float dt)
{
    m_time += dt;

    // Critically damped, so it eases at both ends and never overshoots —
    // the same spring OozeSim uses, and for the same reason. An
    // exponential follow was here first: it leaves at full speed and
    // creeps in, which on something the size of a Dock icon reads as a
    // bar filling rather than as something climbing out. A spring starts
    // at nothing, gathers pace and settles.
    //
    // Faster to withdraw than to emerge. Coming out is deliberate;
    // going back in is a flinch.
    //
    // Velocity first, then position — semi-implicit, which stays stable
    // at the frame intervals this runs at where the explicit form would
    // not.
    constexpr float kEmergeEase = 2.6f;
    constexpr float kWithdrawEase = 4.4f;
    const float omega = m_target > m_level ? kEmergeEase : kWithdrawEase;
    m_vel += (omega * omega * (m_target - m_level) - 2.0f * omega * m_vel) * dt;
    m_level += m_vel * dt;
    // A spring approaches asymptotically and would keep the clock
    // running forever chasing the last thousandth. Close and slow is
    // arrived — and it has to actually arrive, because reaching zero is
    // what lets isEmpty() go true and the overlay be torn down.
    if (std::abs(m_target - m_level) < 0.002f && std::abs(m_vel) < 0.02f) {
        m_level = m_target;
        m_vel = 0.0f;
    }
    m_level = std::clamp(m_level, 0.0f, 1.0f);

    m_dt = dt;
    updateArms();
    emit frameChanged();

    const bool empty = isEmpty();
    if (empty != m_wasEmpty) {
        m_wasEmpty = empty;
        emit activityChanged();
    }
}

/// Same hash the rest of the app uses, so an arm keeps its identity.
static float armHash(float a, float b)
{
    const float s = std::sin(a * 127.1f + b * 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

/// Ease in and out, 0..1.
static float smooth(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

QVector3D TentacleEffect::armBase(int i) const
{
    const Seat &seat = kSeats[i];
    return QVector3D(mouthX() + seat.lateral * mouthRadius(),
                     rootY(),
                     seat.depth * mouthReachZ());
}
float TentacleEffect::moveProgress(int i) const
{
    return std::clamp(m_state[i].t / std::max(m_state[i].duration, 0.01f), 0.0f, 1.0f);
}
void TentacleEffect::advanceMove(int i, float dt)
{
    ArmState &st = m_state[i];
    st.t += dt;
    if (st.t < st.duration)
        return;
    // Pick the next one. Weighted by hand rather than uniformly: Idle is
    // the resting state and has to be common or the bin looks frantic, and
    // an arm that has just done something showy should settle before doing
    // another. Never the same move twice running, which is the cheapest
    // way to stop a coincidence of timing reading as a pattern.
    const Move was = st.move;
    st.t = 0.0f;
    st.seed = armHash(float(i), m_time * 0.37f + 11.0f);
    const float r = armHash(float(i) + 0.5f, m_time * 0.61f + 3.0f);
    if (m_cursorOn && r < 0.55f) {
        st.move = Move::Reach;
        st.duration = 1.6f + 1.4f * st.seed;
    } else if (was != Move::Idle && r < 0.45f) {
        st.move = Move::Idle;
        st.duration = 1.8f + 2.2f * st.seed;
    } else if (r < 0.62f) {
        st.move = Move::Slap;
        st.duration = 1.15f;
    } else if (r < 0.78f) {
        st.move = Move::Coil;
        st.duration = 2.4f + 1.2f * st.seed;
    } else if (r < 0.90f) {
        st.move = Move::Wrap;
        st.duration = 3.0f + 1.5f * st.seed;
    } else {
        st.move = Move::Idle;
        st.duration = 1.8f + 2.2f * st.seed;
    }
    if (st.move == was && st.move != Move::Idle) {
        st.move = Move::Idle;
        st.duration = 1.6f;
    }
}
QVector3D TentacleEffect::armTarget(int i, float &flex, float &maxBend) const
{
    const Seat &seat = kSeats[i];
    const ArmState &st = m_state[i];
    const float sa = armHash(float(i), 1.0f);
    const float sb = armHash(float(i), 2.0f);
    const QVector3D base = armBase(i);
    const float L = armLength() * seat.size;
    const float binH = float(m_binSize.height());
    const float u = moveProgress(i);
    flex = 1.0f;
    maxBend = TentacleChain::kMaxBend;
    // Which way it lashes. Wandering, so a move is not always along the
    // same line; the back arm's wander is wide enough to take it over
    // either side of the bin -- see kSeats.
    const float dir = seat.strike
                    + seat.swing * std::sin(m_time * 0.37f + sb * 6.28f);
    // FLATTENED IN Z, and deliberately. The camera is orthographic and
    // barely tilted, so movement toward it is nearly invisible on screen
    // while still being movement -- an arm leaning out along its own radius
    // spent most of its travel coming at the viewer, where it read as
    // barely moving and yet swung far enough forward to be legitimately in
    // FRONT of the bin, which broke the occlusion for a gesture nobody
    // could see. Biasing the reach into the screen plane costs nothing
    // that shows and keeps the arms where the mask expects them.
    const QVector3D out(std::sin(dir), 0.0f, std::cos(dir) * 0.30f);
    // Near FULL STRETCH, not comfortably inside it. The first attempt put
    // this at two thirds of the arm's reach on the reasoning that slack is
    // what makes a chain hang in a curve. It is not: FABRIK has no notion
    // of an arm being a physical object, so slack is simply folded away,
    // and three arms came out as crumpled self-intersecting stubs. The
    // curve comes from the bend limit and the wave; the target's job is to
    // keep the arm extended enough to have a shape at all.
    const QVector3D idle = base
        + QVector3D(0.0f, 1.0f, 0.0f) * (L * (0.80f + 0.07f * std::sin(m_time * 0.43f + sa * 6.28f)))
        // Sideways reach scaled to the BIN, not to the arm. Scaled to the
        // arm it was 290 units on a bin only 160 deep, so an idle wave
        // carried the tip clear past the bin's own footprint.
        + out * (mouthRadius() * (1.00f + 0.35f * std::sin(m_time * 0.31f + sb * 6.28f)));
    switch (st.move) {
    case Move::Idle:
        return idle;
    case Move::Slap: {
        // Out fast, held, drawn back slowly -- the asymmetry is the whole
        // of what makes it read as a hit rather than a wave.
        const float hit = u < 0.18f ? smooth(u / 0.18f)
                        : u < 0.30f ? 1.0f
                        : 1.0f - smooth((u - 0.30f) / 0.70f);
        flex = 1.0f - 0.75f * hit;
        // WELL BELOW the contact point, not on it. Aimed AT the wall the
        // arm arrives pointing at it and pokes; aimed at a place further
        // down, the last joints have to run along the wall to get there
        // and the arm lands on its flat instead of its tip. That is the
        // whole difference between prodding the bin and hitting it, and it
        // is a property of where the target is, not of the solver.
        // ON the wall at the depth it actually lands, not at a fixed
        // radius. The bin narrows going down, so a constant lateral target
        // put the tip outside a wall that was no longer there, and the
        // mask -- which does know about the taper -- correctly drew it in
        // front of the bin. Measured, 9,132 leaked pixels on the deepest
        // slap. Both sides now read the same profile.
        const float wallY = rootY() - binH * 0.55f;
        const QVector3D wall(std::copysign(binHalfWidthAt(wallY) * 0.98f, out.x()),
                             wallY,
                             out.z() * mouthReachZ() * 0.55f);
        return idle + (wall - idle) * hit;
    }
    case Move::Coil: {
        // Curled right up, tip brought back near its own root. Needs the
        // bend limit lifted: sixteen joints at the default cannot close a
        // circle, so without this the arm merely hooks.
        const float c = smooth(std::min(u * 2.0f, 1.0f))
                      * (1.0f - smooth(std::max((u - 0.72f) / 0.28f, 0.0f)));
        maxBend = TentacleChain::kMaxBend
                + (TentacleChain::kCoilBend - TentacleChain::kMaxBend) * c;
        flex = 1.0f - 0.55f * c;
        const QVector3D knot = base
            + QVector3D(0.0f, 1.0f, 0.0f) * (L * 0.16f)
            + out * (mouthRadius() * 0.35f);
        return idle + (knot - idle) * c;
    }
    case Move::Wrap: {
        // The tip travels ROUND the bin rather than to a point on it, so
        // the arm lies along the rim instead of across it. A target that
        // moves is the only way to get a path out of a solver that only
        // knows about destinations.
        const float turn = (sa * 6.28318531f) + u * 3.4f;
        const float ring = smooth(std::min(u * 3.0f, 1.0f))
                         * (1.0f - smooth(std::max((u - 0.78f) / 0.22f, 0.0f)));
        flex = 1.0f - 0.45f * ring;
        const float ringY = rootY() - binH * (0.10f + 0.16f * ring);
        const QVector3D around(std::sin(turn) * binHalfWidthAt(ringY) * 0.98f,
                               ringY,
                               std::cos(turn) * mouthReachZ() * 0.85f);
        return idle + (around - idle) * ring;
    }
    case Move::Reach: {
        // After the pointer. The overlay is click-through and never gets
        // hover events, so this arrives polled -- but it is in the host
        // item's pixels with +y DOWN, and the scene is bin-local with +y
        // UP, so it needs the same conversion the mouth measurements get.
        const float rx = float(m_cursor.x() - m_binRect.center().x());
        const float ry = float(m_binRect.center().y() - m_cursor.y());
        const float ring = smooth(std::min(u * 2.5f, 1.0f))
                         * (1.0f - smooth(std::max((u - 0.80f) / 0.20f, 0.0f)));
        // Clamped to the arm's own reach, or an arm asked for something
        // across the screen simply points at it and stops looking like an
        // arm at all.
        QVector3D want(rx, ry, 0.0f);
        const QVector3D rel = want - base;
        if (rel.length() > L * 0.95f)
            want = base + rel.normalized() * (L * 0.95f);
        return idle + (want - idle) * ring;
    }
    }
    return idle;
}
float TentacleEffect::binHalfWidthAt(float y) const
{
    const float binH = float(m_binSize.height());
    const float below = std::clamp((mouthY() - y) / std::max(binH, 1e-4f), 0.0f, 1.0f);
    return mouthRadius() * (kBodyTop + (kBodyFoot - kBodyTop) * below);
}
float TentacleEffect::mouthReachZ() const
{
    const float s = std::sin(m_cameraTilt * 3.14159265f / 180.0f);
    return mouthDepth() / std::max(0.05f, s);
}
void TentacleEffect::setCameraTilt(float degrees)
{
    if (qFuzzyCompare(m_cameraTilt, degrees))
        return;
    m_cameraTilt = degrees;
    emit shapeChanged();
}
void TentacleEffect::updateArms()
{
    const int n = count();
    for (int i = 0; i < n; ++i) {
        advanceMove(i, m_dt);
        const QVector3D base = armBase(i);
        const float want = armLength() * kSeats[i].size;
        // Rebuilt only when the arm's length actually changes -- which is
        // when the bin resizes, and never per frame. Resetting a chain
        // throws away the state that makes it lag, and a chain with no lag
        // is the arc again.
        if (!m_chain[i].valid() || std::abs(m_chain[i].length() - want) > 0.5f)
            m_chain[i].reset(base, QVector3D(0, 1, 0), want);
        float flex = 1.0f;
        float maxBend = TentacleChain::kMaxBend;
        const QVector3D target = armTarget(i, flex, maxBend);
        m_chain[i].solve(base, target, m_time,
                         armHash(float(i), 3.0f) * 6.28318531f, flex, maxBend);
    }
    m_spine->setChains(m_chain, n, kMaxTentacles);
}
QVariantList TentacleEffect::seats() const
{
    QVariantList out;
    out.reserve(kMaxTentacles);
    for (const Seat &s : kSeats)
        out.append(QVariant::fromValue(QVector4D(s.lateral, s.depth, s.size, 0.0f)));
    return out;
}

float TentacleEffect::armLength() const
{
    return kArmLength * float(m_binSize.height());
}

int TentacleEffect::count() const
{
    if (isEmpty())
        return 0;
    return std::clamp(int(std::lround(m_level * kMaxTentacles)), 1, kMaxTentacles);
}

QMargins TentacleEffect::margins(qreal iconSize) const
{
    // Nearly all of it above. These reach UP out of the bin and wave
    // about; anything drawn past the window's edge is clipped and reads
    // as broken, and the window is sized from this.
    //
    // The top has to cover an arm standing straight up, which is its root
    // (a third of the way up the bin) plus its whole length (kArmLength,
    // 1.15 bin heights) plus what the sway adds, less the half of the bin
    // that is already inside the rect. At 1.6 there is room for the sway
    // and for a longer arm than the current one before this has to be
    // revisited. Fill costs effectively nothing at this size — see
    // docs/battery.md — so the margin is cheap and clipping is not.
    return QMargins(int(iconSize * 0.9), int(iconSize * 1.6),
                    int(iconSize * 0.9), int(iconSize * 0.15));
}

// --- the mouth, in scene units ------------------------------------------
//
// The measurement arrives as a fraction of the icon's tile with +y DOWN
// from its top; the scene is bin-local pixels with the origin at the
// rect's centre and +y UP. Converted here, once, so no caller has to
// remember which way round either convention runs.

float TentacleEffect::mouthX() const
{
    return float(m_mouth.centre.x() - 0.5) * float(m_binSize.width());
}

float TentacleEffect::mouthY() const
{
    return float(0.5 - m_mouth.centre.y()) * float(m_binSize.height());
}

float TentacleEffect::mouthRadius() const
{
    return m_mouth.halfWidth * float(m_binSize.width());
}

float TentacleEffect::mouthDepth() const
{
    return m_mouth.depth * float(m_binSize.height());
}

float TentacleEffect::heapTopY() const
{
    return mouthY() + kHeapRise * float(m_binSize.height());
}

float TentacleEffect::heapFloorY() const
{
    return mouthY() - kHeapSink * float(m_binSize.height());
}

float TentacleEffect::rootY() const
{
    return mouthY() - kRootDepth * float(m_binSize.height());
}

} // namespace hyperbin
