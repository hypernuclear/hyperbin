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

class IconTexture;

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
    /// WHERE THE POINTER IS, in the scene's own units, and how much the
    /// eyes should care. The pair is what makes them follow it.
    ///
    /// A point rather than a direction, because the whole charm of a
    /// cluster of eyes tracking something is that they CONVERGE on it: the
    /// ones on the left and the ones on the right turn by different
    /// amounts, and only a shared target produces that. Handing down one
    /// direction would have them all swing in parallel, which reads as the
    /// gel tilting rather than as being looked at.
    ///
    /// Placed on the pointer's view ray at a fixed distance in front of the
    /// bin. Under an orthographic camera every point on that ray projects
    /// to the same pixel, so the depth chosen does not change WHERE the
    /// eyes appear to look — only how strongly they converge. See
    /// kGazeDepth.
    Q_PROPERTY(QVector3D gazeTarget READ gazeTarget NOTIFY frameChanged)
    /// 0 when the pointer is nowhere near, 1 when it is on the bin.
    ///
    /// Faded by distance and then low-passed, because the host polls the
    /// pointer every frame and always reports it present — the overlay is
    /// click-through and gets no enter or leave events, so "present" says
    /// nothing about whether it is anywhere near. Distance is the only
    /// real signal, and a pointer that jumps across the screen has to
    /// arrive as a movement rather than as a step.
    Q_PROPERTY(float gazePull READ gazePull NOTIFY frameChanged)
    /// The scene camera's downward tilt, degrees. WRITTEN BY THE SCENE.
    ///
    /// Needed here because the pointer arrives as a position ON SCREEN and
    /// the eyes are placed in the scene: recovering one from the other
    /// turns on the tilt. Pushed in rather than restated as a constant, so
    /// there is one camera tilt in the app and not two that can drift
    /// apart — the same arrangement TentacleEffect uses, and for the same
    /// reason.
    Q_PROPERTY(float cameraTilt READ cameraTilt WRITE setCameraTilt
                   NOTIFY shapeChanged)
    /// How many there can ever be. The scene builds this many delegates
    /// once and hides the ones that are not out; rebuilding the Repeater3D
    /// whenever the count moved cost four times the whole effect when the
    /// bubbles did it.
    Q_PROPERTY(int maxEyes READ maxEyes CONSTANT)

public:
    /// The most eyes that can ever be out, at a full bin.
    ///
    /// Every one of these is a slot in the gel's shaders as well — the
    /// body has to swell around each and draw a meniscus at its rim, and
    /// a CustomMaterial has no array uniform, so the count is written out
    /// by hand in three places. Raising it means raising it there too;
    /// qml/OozeVisual.qml has the list.
    static constexpr int kMaxEyes = 14;

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
    QVector3D gazeTarget() const { return m_gazeTarget; }
    float gazePull() const { return m_gazePull; }
    float cameraTilt() const { return m_cameraTilt; }
    void setCameraTilt(float degrees);

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

    /// One eye's placement, before it is turned into a seat.
    struct EyeSlot
    {
        float angle;  ///< radians round the bin; 0 faces the camera
        float param;  ///< 0..1 up its own surface, body or puddle
        float radius;
        bool  pool;
    };

    /// Recompute where the eyes are. Called every step.
    void updateEyes();

    /// Push any two that would touch apart, in place.
    ///
    /// The placement sequences spread well but do not GUARANTEE a gap,
    /// and at fourteen they demonstrably fail to: the golden angle has
    /// near-returns at Fibonacci indices, so eyes 0 and 13 come back to
    /// within a fiftieth of a turn of each other. Nothing about the
    /// sequence can be tuned to fix that in general — the next count
    /// would just fail at a different pair — so the gap is enforced here
    /// instead of hoped for.
    void spreadEyes(EyeSlot *slot, int n) const;
    /// Where a slot's parameter puts it, in bin-local units. Body and
    /// puddle share a floor, so this is one line either way.
    float slotY(const EyeSlot &s) const;

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
    /// The last spread, and what it was spread against.
    ///
    /// Where the eyes settle depends only on how many there are and on
    /// the shape they are settling on — never on the clock. The wobble
    /// each carries is added afterwards and is far smaller than the gap
    /// the spread leaves, so it cannot push a pair back together. So the
    /// relaxation runs when the bin changes and not otherwise, which for
    /// a bin that is mostly sitting still is almost never.
    EyeSlot m_spread[kMaxEyes] {};
    int   m_spreadCount = -1;
    float m_spreadKey = 0.0f;
    QSizeF  m_binSize {40.0, 40.0};
    QRectF  m_binRect;
    QPointF m_cursor;
    bool    m_cursorOn = false;
    QVector3D m_gazeTarget;
    float   m_gazePull = 0.0f;
    float   m_cameraTilt = 17.0f;
    /// How far in front of the bin the gaze target is parked, in bin
    /// heights. Only the CONVERGENCE depends on it — the camera is
    /// orthographic, so sliding the target along the pointer's view ray
    /// leaves it over the same pixel. Near, the eyes cross like something
    /// examining a fly on the glass; far, they turn nearly in parallel and
    /// the cluster stops reading as a group looking at one thing. Two bin
    /// heights is a comfortable reading distance for a face this size.
    static constexpr float kGazeDepth = 1.15f;
    /// Recompute where the pointer is and how much the eyes care.
    void updateGaze(float dt);
    float   m_contentLine = 0.22f;
    bool    m_wasEmpty = true;
    QVector<quint8> m_coverage;
    int m_covW = 0, m_covH = 0;
    IconTexture *m_iconTexture = nullptr;
};

} // namespace hyperbin
