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

    emit frameChanged();

    const bool empty = isEmpty();
    if (empty != m_wasEmpty) {
        m_wasEmpty = empty;
        emit activityChanged();
    }
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
    return QMargins(int(iconSize * 0.7), int(iconSize * 1.1),
                    int(iconSize * 0.7), int(iconSize * 0.15));
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

} // namespace hyperbin
