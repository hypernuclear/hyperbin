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
#include <QVariantList>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

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
    /// The eyes, one entry each, only as many as are currently out.
    ///
    /// (x, y, z, radius) in bin-local units. Two consumers want this and
    /// they want it in different places — qml/OozeEyes.qml puts a model
    /// there, and the gel's vertex shader tents its surface up over it —
    /// so it is computed once, here, against the same profile the mesh is
    /// swept from. Working it out in QML was the earlier arrangement and
    /// it could not answer the second question at all: the scene knows
    /// where it PUT an eye, but not which way the gel faces there.
    Q_PROPERTY(QVariantList eyeSpheres READ eyeSpheres NOTIFY frameChanged)
    /// (x, y, z, open) — the gel's outward normal where each eye sits,
    /// and how open that eye is, 0 shut to 1 wide. The scene leans a gaze
    /// by the first and hands the second to the eyeball's own material,
    /// which is where the lids are drawn.
    Q_PROPERTY(QVariantList eyeNormals READ eyeNormals NOTIFY frameChanged)
    /// How many there can ever be. The scene builds this many delegates
    /// once and hides the ones that are not out; rebuilding the Repeater3D
    /// whenever the count moved cost four times the whole effect when the
    /// bubbles did it.
    Q_PROPERTY(int maxEyes READ maxEyes CONSTANT)

public:
    /// The most eyes that can ever be out, at a full bin.
    static constexpr int kMaxEyes = 9;

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

    QVariantList eyeSpheres() const { return m_eyeSpheres; }
    QVariantList eyeNormals() const { return m_eyeNormals; }
    int maxEyes() const { return kMaxEyes; }

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
    /// A place on the gel to sit an eye: a point, and which way the gel
    /// faces there.
    ///
    /// The two always travel together. Working the normal out separately
    /// meant two functions walking the same profile and either could be
    /// changed without the other, which is the drift this whole file is
    /// arranged to prevent.
    struct EyeSeat
    {
        QVector3D pos;
        QVector3D nrm;
    };

    /// Recompute where the eyes are. Called every step.
    void updateEyes();

    /// On the body's wall.
    /// @param t     0 at the pool's crest, 1 at the gel's surface.
    /// @param angle radians around the bin's axis; 0 faces the camera.
    EyeSeat bodySeat(float t, float angle) const;
    /// On the puddle's upper surface, which is a different shape and a
    /// nearly horizontal one — so an eye there is looking up out of a
    /// flat spill rather than out of a wall, and its normal has to come
    /// from the shoulder the mesh actually sweeps.
    /// @param u     0 at the puddle's outer rim, 1 where it meets the body.
    EyeSeat poolSeat(float u, float angle) const;

    /// Both seats are the same surface of revolution seen twice: a ring
    /// radius and a height that each move with one parameter. Given how
    /// fast they move, the normal is the same three lines either way.
    static EyeSeat sweepSeat(float r, float y, float drdp, float dydp,
                             float angle);

    OozeSim m_sim;
    OozeShape m_shape;
    QVariantList m_eyeSpheres;
    QVariantList m_eyeNormals;
    QSizeF  m_binSize {40.0, 40.0};
    QRectF  m_binRect;
    float   m_contentLine = 0.22f;
    bool    m_wasEmpty = true;
    QVector<quint8> m_coverage;
    int m_covW = 0, m_covH = 0;
    OozeTextureData *m_iconTexture = nullptr;
};

} // namespace hyperbin
