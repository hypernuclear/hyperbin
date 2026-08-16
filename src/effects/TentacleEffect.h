// Tentacles reaching out of the bin.
//
// PLACEHOLDER GEOMETRY, on purpose. What is being built here first is the
// occlusion, not the creature: something coming out of the bin has to
// pass BEHIND its front wall on the way, and until that reads correctly
// there is no point modelling anything. The tentacles are chains of
// tapering spheres, and they are meant to be replaced.
//
// The masking is the interesting part, and it needs no depth proxy and no
// model of the bin. A tentacle fragment is thrown away where it falls
// below the mouth's near lip and over solid artwork — see
// shaders/tentacle.frag, which also records the version that did this by
// repainting the bin over the top instead, and why cutting our own
// geometry is the better side of the join to work on.
#pragma once

#include "../core/Effect.h"

#include <QImage>
#include <QSizeF>
#include <QVector>

namespace hyperbin {

class IconTexture;

class TentacleEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(float time READ time NOTIFY frameChanged)
    Q_PROPERTY(float level READ level NOTIFY frameChanged)
    Q_PROPERTY(int count READ count NOTIFY frameChanged)
    /// How many the scene should build. CONSTANT, and it matters that it
    /// is: the scene builds this many once and hides the ones that are
    /// not out. Driving a Repeater3D from `count` instead rebuilds every
    /// delegate on the frame the count moves, which it does all the way
    /// through emerging — the ooze bubbles did exactly that and cost four
    /// times the whole effect before they were removed.
    Q_PROPERTY(int maxTentacles READ maxTentacles CONSTANT)
    Q_PROPERTY(QSizeF binSize READ binSize NOTIFY shapeChanged)
    Q_PROPERTY(QRectF binRect READ binRect NOTIFY shapeChanged)
    Q_PROPERTY(QObject *iconTexture READ iconTexture NOTIFY shapeChanged)

    /// The opening, in the SCENE's units: bin-local pixels, origin at the
    /// centre of the bin's rect, +y up. Converted here rather than in QML
    /// so the scene never has to know that the measurement arrives as a
    /// fraction of the icon and with +y down.
    Q_PROPERTY(float mouthX READ mouthX NOTIFY shapeChanged)
    Q_PROPERTY(float mouthY READ mouthY NOTIFY shapeChanged)
    Q_PROPERTY(float mouthRadius READ mouthRadius NOTIFY shapeChanged)
    Q_PROPERTY(float mouthDepth READ mouthDepth NOTIFY shapeChanged)
    /// The same opening again, left as FRACTIONS of the icon.
    ///
    /// Both forms are wanted and neither can serve for the other: the
    /// scene places tentacles in its own pixels, and the mask works in
    /// the icon's texture coordinates, where the lip's ellipse is
    /// compared against a sampled alpha.
    Q_PROPERTY(float mouthCentreX READ mouthCentreX NOTIFY shapeChanged)
    Q_PROPERTY(float mouthCentreY READ mouthCentreY NOTIFY shapeChanged)
    Q_PROPERTY(float mouthHalfWidth READ mouthHalfWidth NOTIFY shapeChanged)
    Q_PROPERTY(float mouthDepthFraction READ mouthDepthFraction NOTIFY shapeChanged)
    Q_PROPERTY(bool mouthMeasured READ mouthMeasured NOTIFY shapeChanged)

public:
    explicit TentacleEffect(QObject *parent = nullptr);
    ~TentacleEffect() override;

    void setBinRect(const QRectF &binRect) override;
    void setSurface(const QVector<quint8> &coverage, int w, int h) override;
    void setFullness(float fullness) override;
    void setCursor(const QPointF &pos, bool present) override;
    void setContentLine(float y01) override;
    void setMouth(const BinMouth &mouth) override;
    void setBinImage(const QImage &img) override;
    void step(float dt) override;

    bool isEmpty() const override { return m_level <= 0.005f; }
    bool isAtRest() const override { return isEmpty(); }
    int preferredFrameIntervalMs() const override { return 33; }

    QMargins margins(qreal iconSize) const override;
    QUrl visualSource() const override;

    /// Not used: this effect draws through its QML visual.
    QSGNode *updateNode(QSGNode *, QQuickWindow *, const QRectF &,
                        QSGTexture *) override
    {
        return nullptr;
    }

    float time() const { return m_time; }
    float level() const { return m_level; }
    int count() const;
    int maxTentacles() const { return kMaxTentacles; }
    QSizeF binSize() const { return m_binSize; }
    QRectF binRect() const { return m_binRect; }
    QObject *iconTexture() const;

    float mouthX() const;
    float mouthY() const;
    float mouthRadius() const;
    float mouthDepth() const;
    float mouthCentreX() const { return float(m_mouth.centre.x()); }
    float mouthCentreY() const { return float(m_mouth.centre.y()); }
    float mouthHalfWidth() const { return m_mouth.halfWidth; }
    float mouthDepthFraction() const { return m_mouth.depth; }
    bool mouthMeasured() const { return m_mouth.measured; }

signals:
    void frameChanged();
    void shapeChanged();

private:
    /// The most that can ever be out. Small: each is its own chain of
    /// draw calls until this stops being a placeholder.
    static constexpr int kMaxTentacles = 5;

    BinMouth m_mouth;
    QSizeF m_binSize {40.0, 40.0};
    QRectF m_binRect;
    float m_time = 0.0f;
    float m_level = 0.0f;
    float m_vel = 0.0f;
    float m_target = 0.0f;
    bool m_wasEmpty = true;
    IconTexture *m_iconTexture = nullptr;
};

} // namespace hyperbin
