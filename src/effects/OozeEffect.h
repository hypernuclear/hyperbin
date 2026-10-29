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
#include <QVector3D>
#include <QVector4D>
#include <QtQml/qqmlregistration.h>

namespace hyperbin {

class IconTexture;

/// One eye, everything qml/OozeEyes.qml needs to draw it, worked out here.
///
/// Per eye and per property, each with its own signal, and a signal only
/// when the value moved. This used to be two QVariantLists on the effect
/// behind one per-frame signal, and the scene derived the rest in
/// JavaScript: every frame both lists became fresh JS arrays, and all
/// fourteen delegates re-ran their aim, lean, saccade and Euler maths in
/// bindings. Sampled on a real Dock (Oct 29) that was most of the effect's
/// CPU at the display's refresh rate. Now the maths is C++, and a
/// delegate's bindings are plain reads.
class OozeEye : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by OozeEffect")
    /// Whether this slot has an eye in it at all.
    Q_PROPERTY(bool out READ out NOTIFY outChanged)
    /// The ball's centre, bin-local scene units, and its radius.
    Q_PROPERTY(QVector3D position READ position NOTIFY positionChanged)
    Q_PROPERTY(float size READ size NOTIFY sizeChanged)
    /// (x, y, z, radius), all zero when not out: the gel's shaders read it
    /// to swell round the ball. The same numbers as position and size.
    Q_PROPERTY(QVector4D sphere READ sphere NOTIFY sphereChanged)
    /// Where it looks, a unit vector, and the same as two angles in
    /// degrees: yaw (about y) then pitch (about x), nested in the scene.
    Q_PROPERTY(QVector3D gaze READ gaze NOTIFY gazeChanged)
    Q_PROPERTY(float yaw READ yaw NOTIFY gazeChanged)
    Q_PROPERTY(float pitch READ pitch NOTIFY gazeChanged)
    /// The saccade on top of the gaze, degrees about x and y.
    Q_PROPERTY(float dartX READ dartX NOTIFY dartChanged)
    Q_PROPERTY(float dartY READ dartY NOTIFY dartChanged)
    /// 1 wide open, 0 shut.
    Q_PROPERTY(float open READ open NOTIFY openChanged)
    /// Anatomy, not motion: how far its lid axis is canted, in radians,
    /// and the seed that keeps its lid's ragged rim its own.
    Q_PROPERTY(float lidRoll READ lidRoll CONSTANT)
    Q_PROPERTY(float seed READ seed CONSTANT)

public:
    OozeEye(int index, QObject *parent);

    bool out() const { return m_out; }
    QVector3D position() const { return m_position; }
    float size() const { return m_size; }
    QVector4D sphere() const { return m_out ? QVector4D(m_position, m_size) : QVector4D(); }
    QVector3D gaze() const { return m_gaze; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    float dartX() const { return m_dartX; }
    float dartY() const { return m_dartY; }
    float open() const { return m_open; }
    float lidRoll() const { return m_lidRoll; }
    float seed() const { return m_seed; }

    /// The slot is empty this frame. Only `out` and `sphere` move.
    void hide();
    /// Out, at this place and size, looking along `gaze`, darted by
    /// (dartX, dartY) degrees, this open. Signals only what changed.
    void show(const QVector3D &position, float size, const QVector3D &gaze,
              float dartX, float dartY, float open);

signals:
    void outChanged();
    void positionChanged();
    void sizeChanged();
    void sphereChanged();
    void gazeChanged();
    void dartChanged();
    void openChanged();

private:
    bool m_out = false;
    QVector3D m_position;
    float m_size = 1.0f;
    QVector3D m_gaze {0.0f, 0.0f, 1.0f};
    float m_yaw = 0.0f, m_pitch = 0.0f;
    float m_dartX = 0.0f, m_dartY = 0.0f;
    float m_open = 1.0f;
    float m_lidRoll = 0.0f;
    float m_seed = 0.0f;
};

class OozeEffect : public Effect
{
    Q_OBJECT
    /// Bound by qml/OozeVisual.qml. Everything the scene needs and
    /// nothing it can work out for itself.
    Q_PROPERTY(float level READ level NOTIFY levelChanged)
    Q_PROPERTY(float time READ time NOTIFY timeChanged)
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

    /// Slot `i`, 0 to kMaxEyes - 1: the same object for the effect's life.
    Q_INVOKABLE hyperbin::OozeEye *eye(int i) const;
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
    /// Every step.
    void timeChanged();
    /// When the level moves, which a settled bin's does not.
    void levelChanged();
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
    OozeEye *m_eyes[kMaxEyes] {};
    float m_lastLevel = -1.0f;
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
    /// How hard an eye leans off its aim toward the way the gel faces
    /// under it, at rest; halved while the pointer has its attention.
    ///
    /// At 0 every eye stares down the lens wherever it sits, which is why
    /// the ones out near the silhouette read as decals: an eye buried in
    /// something has an orbit, and an orbit points where the surface
    /// points. At a third, one out on the side turned by only about thirty
    /// degrees and still read as pasted on; over half turns it about fifty,
    /// a ball sitting in the goo and looking out of it. Eased off while
    /// tracking because at full strength a pointer swung across the bin
    /// moved the irises by under a quarter of an eye's radius, which is in
    /// the numbers and invisible on screen.
    static constexpr float kGazeBias = 0.55f;
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
