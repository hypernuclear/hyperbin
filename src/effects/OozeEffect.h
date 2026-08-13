// Ooze: sludge that pools in the bin, laps over its edges and drips.
//
// Drawn with Qt Quick 3D rather than a scene-graph node, so it can use
// Qt's PBR material — transmission, clearcoat, an environment — instead
// of a hand-rolled approximation of one. See qml/OozeVisual.qml; this
// class owns the state that scene binds to.
#pragma once

#include "../core/Effect.h"
#include "../core/OozeSim.h"
#include "OozeShape.h"

#include <QImage>
#include <QSizeF>
#include <QVector>

namespace hyperbin {

class OozeTextureData;

class OozeEffect : public Effect
{
    Q_OBJECT
    /// Bound by qml/OozeVisual.qml. Everything the scene needs and
    /// nothing it can work out for itself.
    Q_PROPERTY(float level READ level NOTIFY frameChanged)
    Q_PROPERTY(float time READ time NOTIFY frameChanged)
    Q_PROPERTY(float contentLine READ contentLine NOTIFY shapeChanged)
    Q_PROPERTY(QSizeF binSize READ binSize NOTIFY shapeChanged)
    /// The bin's rect in the host item's coordinates. The overlay has
    /// asymmetric margins — lots of room below for drips, almost none
    /// above — so the bin is NOT at the item's centre, and a scene
    /// centred on the item would draw the sludge in the wrong place.
    Q_PROPERTY(QRectF binRect READ binRect NOTIFY shapeChanged)
    /// The bin's lip, in bin-local units. The gel's surface hides behind
    /// its near arc, which is what makes the bin read as standing IN the
    /// gel rather than wearing it.
    Q_PROPERTY(float rimY READ rimY NOTIFY shapeChanged)
    Q_PROPERTY(float rimRadius READ rimRadius NOTIFY shapeChanged)
    Q_PROPERTY(QObject *iconTexture READ iconTexture NOTIFY shapeChanged)

public:
    explicit OozeEffect(QObject *parent = nullptr);
    ~OozeEffect() override;

    void setBinRect(const QRectF &binRect) override;
    void setSurface(const QVector<quint8> &coverage, int w, int h) override;
    void setFullness(float fullness) override;
    void setCursor(const QPointF &pos, bool present) override;
    void setContentLine(float y01) override;
    void step(float dt) override;

    bool isEmpty() const override { return m_sim.isEmpty(); }
    bool isAtRest() const override { return m_sim.isAtRest(); }
    int  preferredFrameIntervalMs() const override
    {
        return m_sim.preferredFrameIntervalMs();
    }

    QMargins margins(qreal iconSize) const override;
    QUrl visualSource() const override;

    /// Not used: this effect draws through its QML visual.
    QSGNode *updateNode(QSGNode *, QQuickWindow *, const QRectF &,
                        QSGTexture *) override
    {
        return nullptr;
    }

    /// The bin's artwork, for the scene to refract. Qt's own screen
    /// texture never produced it, so we hand it over explicitly.
    void setBinImage(const QImage &img) override;

    float level() const { return m_sim.level(); }
    float time() const { return m_sim.time(); }
    float contentLine() const { return m_contentLine; }
    QSizeF binSize() const { return m_binSize; }
    QRectF binRect() const { return m_binRect; }
    float rimY() const { return m_shape.rimY(); }
    float rimRadius() const { return m_shape.rimRadius(); }
    QObject *iconTexture() const;

    const OozeSim &sim() const { return m_sim; }
    /// The gel's silhouette, measured from the bin's artwork. Read by
    /// OozeGeometry, which sweeps the mesh from it — bulk data, so it is
    /// fetched rather than marshalled through a QML property.
    const OozeShape &shape() const { return m_shape; }

signals:
    /// Something that changes every step: the level and the clock.
    void frameChanged();
    /// Something that changes only when the bin does.
    void shapeChanged();

private:
    OozeSim m_sim;
    OozeShape m_shape;
    QSizeF  m_binSize {40.0, 40.0};
    QRectF  m_binRect;
    float   m_contentLine = 0.22f;
    bool    m_wasEmpty = true;
    QVector<quint8> m_coverage;
    int m_covW = 0, m_covH = 0;
    OozeTextureData *m_iconTexture = nullptr;
};

} // namespace hyperbin
