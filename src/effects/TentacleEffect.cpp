#include "TentacleEffect.h"

#include "IconTexture.h"

#include <algorithm>
#include <cmath>

namespace hyperbin {

TentacleEffect::TentacleEffect(QObject *parent)
    : Effect(parent)
    , m_iconTexture(new IconTexture)
{
}

TentacleEffect::~TentacleEffect()
{
    delete m_iconTexture;
}

QUrl TentacleEffect::visualSource() const
{
    return QUrl(QStringLiteral("qrc:/qt/qml/hyperbin/qml/TentacleVisual.qml"));
}

QObject *TentacleEffect::iconTexture() const
{
    return m_iconTexture;
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

void TentacleEffect::setCursor(const QPointF &, bool)
{
    // Not startled by a pointer, at least not yet.
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

/// The curl that throws the tip `outward` scene units to the side.
///
/// Solved rather than tuned, and it has to be solved because the arm's
/// length and the bin's size are both discovered at runtime — the icon
/// arrives from the shell at whatever size the Dock is drawn at, and a
/// hand-picked angle is only ever right for one of them.
///
/// The spine is an arc, so a bend of c over a length L puts the tip
/// L(1-cos c)/c out from its own root. Inverting that looks impossible at
/// first glance: the outward term rises to a maximum near c = 2.3 and
/// falls away on both sides, so it is two-to-one over the whole range.
/// But a strike is only ever past HALF A TURN — under pi the tip is still
/// rising and merely gestures at the wall (2.55 radians left it 36 units
/// above the rim, measured) — and on (pi, 2pi) the term falls from 2L/pi
/// straight down to zero. Monotonic, so bisection converges, and the one
/// answer it finds is the one that is wanted.
float TentacleEffect::curlForReach(float outward) const
{
    const float L = armLength();
    if (L <= 1.0f)
        return 4.2f;
    constexpr float kPi = 3.14159265f;
    // Never right up against either end: at pi the tip is exactly level
    // with its root and the strike does not come DOWN at all, and at 2pi
    // the arm has closed a full circle back onto itself.
    float lo = kPi + 0.05f, hi = 2.0f * kPi - 0.60f;
    const auto reachAt = [L](float c) { return L * (1.0f - std::cos(c)) / c; };
    // Too far for this arm even at its slackest, or already past the
    // target. Clamping is the honest answer: an arm that cannot reach
    // should fall as short as it can rather than adopt some other curl.
    if (outward >= reachAt(lo))
        return lo;
    if (outward <= reachAt(hi))
        return hi;
    // Sixteen halvings of a 2.9-radian bracket is 4e-5 radians, which is
    // far below what a few pixels of arm can show. Three of these a frame
    // at 30Hz is 1440 cosines a second.
    for (int i = 0; i < 16; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (reachAt(mid) > outward)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

void TentacleEffect::updateArms()
{
    m_arms.clear();
    const int n = count();
    for (int i = 0; i < n; ++i) {
        const float fi = float(i);
        const float sa = armHash(fi, 1.0f);
        const float sb = armHash(fi, 2.0f);
        const float sc = armHash(fi, 3.0f);
        const Seat &seat = kSeats[i];

        // --- the slap ----------------------------------------------------
        //
        // Out fast, held for a moment, drawn back slowly. That asymmetry
        // is the whole of what makes it read as a strike rather than as
        // a wave: something that returns as quickly as it left is
        // oscillating, not hitting anything.
        //
        // Each arm on its own clock, so three of them never strike
        // together — which would read as one creature flexing rather
        // than three arms with their own ideas.
        constexpr float kOut = 0.20f, kHold = 0.10f, kBack = 0.85f;
        const float period = 4.2f + 3.4f * sb;
        const float phase = std::fmod(m_time + sc * period, period);
        float hit = 0.0f;
        if (phase < kOut)
            hit = smooth(phase / kOut);
        else if (phase < kOut + kHold)
            hit = 1.0f;
        else if (phase < kOut + kHold + kBack)
            hit = 1.0f - smooth((phase - kOut - kHold) / kBack);

        // Never straight, even at rest — a tentacle at ease still hangs
        // in a curve, and a straight one reads as a spike.
        //
        // This went UP when the arms got twice as long, and had to. Curl
        // is a total bend over the whole arm, so the same angle spread
        // over twice the length is half the curvature: at 0.45 radians a
        // 1.15-bin-height arm is very nearly a straight line, and three of
        // them stood up out of the bin like railings. Around 1.2 the tip
        // finishes about 0.8 bin heights up and 0.7 out, which is an arc
        // that reads as an arm.
        const float idle = 1.15f + 0.35f * sa
                         + 0.20f * std::sin(m_time * 0.55f + sa * 6.28f);

        // Which way it lashes. Wandering, so a strike is not always along
        // the same line; the back arm's wander is wide enough to take it
        // over either side of the bin — see kSeats.
        const float dir = seat.strike
                        + seat.swing * std::sin(m_time * 0.37f + sb * 6.28f);

        // How far the tip has to travel SIDEWAYS to arrive at the wall:
        // the target, less however much of the way the root already sits,
        // and then divided out of the strike's own direction because only
        // its lateral part closes that gap. An arm lashing straight at the
        // camera never reaches the side of anything, and the guard is what
        // stops that case asking for an infinite reach.
        const float lateral = std::sin(dir);
        const float rootX = seat.lateral * mouthRadius();
        const float wall = std::copysign(kStrikeTarget * mouthRadius(), lateral);
        const float outward = std::abs(lateral) > 0.15f
                            ? (wall - rootX) / lateral
                            : mouthRadius();
        const float curl = idle + (curlForReach(outward) - idle) * hit;

        m_arms.append(QVariant::fromValue(QVector4D(curl, dir, 0.0f, 0.0f)));
    }
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
