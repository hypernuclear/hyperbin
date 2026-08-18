// Tentacles reaching out of the bin.
//
// Three arms seated as a triangle in the measured opening. Each is a joint
// chain solved by FABRIK — core/TentacleChain — rather than the circular
// arc this used to bend, which is the difference between an arm that has a
// shape and one that has an angle.
//
// The masking is the other half, and it needs no depth proxy and no model
// of the bin. A fragment is thrown away where it falls below the mouth's
// near lip with the bin's body between it and the eye, or behind the
// rubbish filling the opening — see shaders/tentacle.frag, which also
// records the version that did this by repainting the bin over the top
// instead, and why cutting our own geometry is the better side of the join
// to work on.
#pragma once

#include "../core/Effect.h"
#include "../core/TentacleChain.h"

#include <QImage>
#include <QSizeF>
#include <QVariantList>
#include <QVector4D>
#include <QVector>

namespace hyperbin {

class IconTexture;
class SpineTexture;

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
    /// Every arm's SOLVED CHAIN, as texture data. Two rows per arm: joint
    /// positions in scene units, then the frame across the arm.
    ///
    /// This replaced a pair of angles describing a circular arc. An arc is
    /// two numbers for a whole arm and has constant curvature by
    /// definition, which is precisely why the old motion read as stiff —
    /// see core/TentacleChain, which also records where the technique
    /// comes from and what in it actually does the work.
    Q_PROPERTY(QObject *spineTexture READ spineTexture NOTIFY shapeChanged)
    /// Joints per arm, so the shader knows the texture's width without
    /// being told twice.
    Q_PROPERTY(int jointCount READ jointCount CONSTANT)

    /// Where each arm SITS, as (lateral, depth, size, 0). Constant.
    ///
    /// Only `size` is still read by the scene, for the cross-section: the
    /// other two are now consumed here, because with a chain the arm's
    /// whole shape is solved in scene units and the model no longer
    /// carries a transform of its own.
    Q_PROPERTY(QVariantList seats READ seats CONSTANT)

    /// The scene camera's downward tilt, degrees. WRITTEN BY THE SCENE.
    ///
    /// Needed here because the opening is measured on screen and the
    /// chains are solved in the scene: turning the mouth's on-screen
    /// half-height into a reach in z divides by the sine of this. Pushed
    /// in rather than duplicated as a constant, so there is one camera
    /// tilt in the app and not two that can drift apart.
    Q_PROPERTY(float cameraTilt READ cameraTilt WRITE setCameraTilt
                   NOTIFY shapeChanged)

    /// How long an arm is, in scene units. Derived from the bin, because
    /// what an arm has to be able to do is reach over the rim and down
    /// the outside, and how far that is is set by the bin's own size.
    Q_PROPERTY(float armLength READ armLength NOTIFY shapeChanged)

    /// The RUBBISH, as a place in the scene rather than as a picture.
    ///
    /// Both in scene units, +y up from the centre of the bin's rect: the
    /// top of the heap and the level below which it is simply solid. The
    /// heap is what an arm at the back of the mouth has to pass behind,
    /// and it cannot do that against something with no depth — see
    /// shaders/tentacle.frag.
    Q_PROPERTY(float heapTopY READ heapTopY NOTIFY shapeChanged)
    Q_PROPERTY(float heapFloorY READ heapFloorY NOTIFY shapeChanged)
    /// How high an arm's root sits, scene units. Below the opening: it is
    /// meant to be buried in the rubbish.
    Q_PROPERTY(float rootY READ rootY NOTIFY shapeChanged)
    /// The opening's reach into the scene along z, scene units.
    ///
    /// The measurement is the on-screen half-height of what is really a
    /// circle, so recovering the circle divides by the sine of the
    /// camera's tilt. Exposed rather than recomputed in QML: it was, and
    /// when the delegate that owned the tilt was rewritten the expression
    /// silently became NaN, which turned every occlusion test false and
    /// let arms draw straight through the bin.
    Q_PROPERTY(float mouthReachZ READ mouthReachZ NOTIFY shapeChanged)
    /// How the bin's body narrows below the lip, as shares of the opening
    /// at the rim and near the foot. Owned here and pushed to the shader
    /// rather than written down in both places: the mask compares an arm
    /// against this profile and the strike AIMS at it, and the two
    /// disagreeing puts a tip through the wall it was supposed to land on.
    Q_PROPERTY(float bodyTaperTop READ bodyTaperTop CONSTANT)
    Q_PROPERTY(float bodyTaperFoot READ bodyTaperFoot CONSTANT)

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
    /// NO OPINION — whatever the power policy says.
    ///
    /// This used to return 33, which does not mean "ask for 30fps": the
    /// host takes qMax(policy, this), so an effect can only ever ask to be
    /// drawn LESS often. The policy already runs 60fps on mains and halves
    /// it on battery for everything, so 33 was quietly overriding that to
    /// 30 on mains as well — and this effect whips, so a strike crossing
    /// the bin in a fifth of a second was stepping rather than moving.
    ///
    /// Zero is the way to say "no floor", and it puts the mains/battery
    /// decision back where it belongs, in core/PowerPolicy.
    int preferredFrameIntervalMs() const override { return 0; }

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
    QVariantList seats() const;
    float armLength() const;
    QObject *spineTexture() const;
    int jointCount() const { return TentacleChain::kJoints; }
    float cameraTilt() const { return m_cameraTilt; }
    void setCameraTilt(float degrees);
    QSizeF binSize() const { return m_binSize; }
    QRectF binRect() const { return m_binRect; }
    QObject *iconTexture() const;

    float mouthReachZ() const;
    float bodyTaperTop() const { return kBodyTop; }
    float bodyTaperFoot() const { return kBodyFoot; }
    /// The bin's half-width at a given scene height, in scene units.
    float binHalfWidthAt(float y) const;
    float heapTopY() const;
    float heapFloorY() const;
    float rootY() const;

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
    /// The most that can ever be out — one draw call each.
    static constexpr int kMaxTentacles = 3;

    /// An arm's place in the bin, and what it does from there.
    struct Seat
    {
        float lateral;  ///< across the opening, fraction of its half-width
        float depth;    ///< into it: -1 at the far lip, +1 at the near one
        float size;     ///< scale for the whole arm, for fake perspective
        float strike;   ///< which way it lashes, radians, 0 = at the camera
        float swing;    ///< how far that direction wanders, radians
        float sink;     ///< extra root depth, fractions of the bin's height
    };

    /// A TRIANGLE, apex at the back — one arm behind two.
    ///
    /// This was a golden-angle spread, which is the right answer for a
    /// dozen things that must not clump and the wrong one for three, where
    /// it just reads as three arms at arbitrary angles. A triangle has a
    /// front and a back, and under an ORTHOGRAPHIC camera that is worth
    /// arranging deliberately: there is no perspective to shrink the far
    /// one, so every depth cue has to be built. The three here are the
    /// back arm being occluded by the rubbish (shaders/tentacle.frag),
    /// crossing in front of each other, and `size`.
    ///
    /// `size` is fake perspective, and it is fake on purpose: an ortho
    /// camera genuinely does not shrink with distance, so the far arm is
    /// scaled down by hand. Small — eight per cent — because the cue only
    /// has to nudge; overdone it reads as three different creatures.
    ///
    /// Seated well inside the rim, not on it. The strike has to have
    /// somewhere to go: an arm rooted at 0.8 of the opening's half-width
    /// is already almost at the wall, and the curl that would carry it
    /// there is so slack that it barely bends. Measured, an arm on the
    /// centre line needs to travel the whole radius, which is what sets
    /// how long an arm has to be — see kArmLength.
    ///
    /// The back arm strikes SIDEWAYS, and it has to. Outward for an arm at
    /// the back of the mouth means away from the camera, so a strike along
    /// its own radius would flop over the far rim and down the back of the
    /// bin where none of it can be seen. Its swing is wide enough to take
    /// it over either side, which is also what makes it cross the front
    /// pair often enough for the occlusion to do its work.
    /// The front pair strike FORWARD of their own radius, not along it.
    ///
    /// How far out a strike lands is solved from the lateral part of its
    /// direction alone (see updateArms), so leaning it forward does not
    /// change where the tip arrives across the bin — only how near the
    /// camera it gets there. Along its own radius the arm came down exactly
    /// on the bin's silhouette edge and read as passing beside the bin
    /// rather than landing on it. Forward of it, the arm clears the front
    /// wall and is drawn over the bin's face, which is the only way a slap
    /// can show contact with anything.
    /// The front pair sit JUST BEHIND the near lip, not forward of it.
    /// At 0.40 of the way toward the camera their roots cleared the rim
    /// and they read as standing in front of the bin; at 0.12 the lip
    /// crosses in front of the base and they look seated in the opening,
    /// which is what an arm coming out of a hole should look like. They
    /// are still a triangle — the separation that matters is the lateral
    /// one, and the back arm is a long way behind both.
    static constexpr Seat kSeats[kMaxTentacles] = {
        //  lateral  depth   size   strike  swing
        //                                                      sink
        {      0.00f, -0.30f, 0.92f,  1.45f, 1.15f,             0.10f },  // back, centre
        {     -0.62f,  0.12f, 0.78f, -0.75f, 0.28f,             0.00f },  // front left
        {      0.62f,  0.12f, 0.78f,  0.75f, 0.28f,             0.00f },  // front right
    };

    /// An arm's length, as a fraction of the bin's height.
    ///
    /// DERIVED, not chosen. The demanding case is the arm on the centre
    /// line: for its tip to clear the side rim and come to rest a quarter
    /// of the bin down the outside, it has to travel the opening's full
    /// half-width outward while dropping that quarter. An arc that does
    /// both has (1-cos c)/-sin c = 1.33, so c = 4.43, and the length that
    /// then puts the tip on the wall is 1.15 bin heights.
    ///
    /// This was 0.55 and the arms could not reach: at that length the
    /// widest a strike can ever throw the tip is 0.64 x 0.55 = 0.35 bin
    /// heights, against the 0.37 needed just to arrive at the wall with
    /// nothing left over to come down it. Slack curls looked like a
    /// gesture at the bin rather than a hit on it, and no amount of tuning
    /// the ANGLE could have fixed a length that was short.
    static constexpr float kArmLength = 1.38f;

    /// Where a strike aims, as a share of the opening's half-width.
    ///
    /// Not 1.0: the bin TAPERS, and a strike lands below the rim rather
    /// than on it. Measured on the macOS trash, the body is 0.98 of the
    /// opening at the lip and 0.93 a quarter of the way down, which is
    /// where the tip of a 4.4-radian curl arrives.
    static constexpr float kStrikeTarget = 0.94f;
    /// The body's width as a share of the opening, at the lip and near the
    /// foot. Measured on the macOS trash: 0.98 at the lip, 0.93 a quarter
    /// down, 0.78 at four fifths.
    static constexpr float kBodyTop = 0.98f;
    static constexpr float kBodyFoot = 0.78f;

    /// How high the rubbish stands above the opening, and how far below it
    /// stays solid. Both fractions of the bin's height.
    ///
    /// The rise is MEASURED, not assumed: differencing the topmost opaque
    /// row of the full macOS trash against the empty one, column by
    /// column, the rubbish stands proud of the rim by up to 55 rows of a
    /// 512px tile — 0.107 — and that peak is the newspaper on the left.
    /// (Over most columns it does not clear the rim at all, which is why
    /// the heap is trimmed against the artwork's own alpha rather than
    /// trusted as a shape.)
    ///
    /// The floor is not measured because it cannot be: nothing in the
    /// artwork says how deep the rubbish goes. It only has to be deeper
    /// than an arm's root, so that a root reads as buried in the rubbish
    /// instead of sitting on it.
    /// Lowered from the measured peak of 0.107. That figure is the
    /// NEWSPAPER — the single highest thing in the artwork — and using it
    /// made the mound as tall as its tallest point everywhere.
    ///
    /// It also turned out that SINKING an arm does not make it look lower,
    /// which is worth writing down because it is counter-intuitive: where
    /// an arm appears to start is set by where it clears the mound, not by
    /// where its root is. Burying the back arm deeper only buried more of
    /// it. What moves the emergence point down is a shallower mound — and
    /// pulling the arm forward, because the mound is cleared along the
    /// LINE OF SIGHT, so something further back has to rise higher to get
    /// over the same mound.
    static constexpr float kHeapRise = 0.018f;
    static constexpr float kHeapSink = 0.22f;

    /// How far below the opening an arm's root starts. Buried, so the arm
    /// comes OUT of the rubbish rather than beginning at its surface, and
    /// no deeper than that — every unit below here is arm that is never
    /// seen.
    /// Deepened, and with a per-seat extra on top.
    ///
    /// The back arm needed both. It sits 0.8 of the opening away from the
    /// camera, and moving away from a camera looking DOWN raises you on
    /// screen — so at the same root height as the others it emerged well
    /// clear of the rim and floated above the rubbish rather than out of
    /// it. Its own sink cancels that.
    ///
    /// The extra depth all round is what the slither is made of: an arm
    /// with a length buried has somewhere to come out FROM. See kRootSlide.
    static constexpr float kRootDepth = 0.20f;
    /// How far an arm slides in and out along its own axis, as a fraction
    /// of the bin's height.
    ///
    /// This is the movement "along the spine" that was missing. Nothing
    /// else in the vocabulary changes how much of an arm is OUT — every
    /// move rearranges a fixed visible length — so the arms read as three
    /// fixed-length things waving rather than as something living in the
    /// bin. Sliding the anchor along the emergence axis pushes the arm
    /// through the mask that is already there, which costs nothing and is
    /// the one motion the occlusion makes convincing for free.
    static constexpr float kRootSlide = 0.185f;
    /// The withdrawal, in seconds: reach up, thrash, then drop.
    static constexpr float kRetreatRise = 0.26f;
    static constexpr float kRetreatThrash = 0.42f;
    static constexpr float kRetreatSink = 0.40f;
    static constexpr float kRetreatHold = kRetreatRise + kRetreatThrash;
    static constexpr float kRetreatAll = kRetreatHold + kRetreatSink;

    /// What an arm is doing. A vocabulary rather than one parameterised
    /// gesture: three arms all running the same curve with different
    /// phases read as one creature flexing, however well that curve is
    /// tuned. These differ in what they reach for and how tightly they are
    /// allowed to bend, which is enough to look like separate intentions.
    enum class Move {
        Idle,   ///< drifting, up and out
        Slap,   ///< over the rim and down the wall, landing along it
        Coil,   ///< curled up on itself, tip near its own root
        Wrap,   ///< tip travelling round the bin's circumference
        Roll,   ///< spiralled up on itself like a fruit rollup
        Reach,  ///< after the pointer
    };
    struct ArmState
    {
        Move move = Move::Idle;
        float t = 0.0f;         ///< seconds into the move
        float duration = 2.0f;
        float seed = 0.0f;      ///< re-rolled per move, so repeats differ
    };
    /// Re-solve every chain that is out. Every step.
    void updateArms();
    /// Advance one arm's move clock, picking a new move when it runs out.
    void advanceMove(int i, float dt);
    /// 0 at the start of the current move, 1 at its end.
    float moveProgress(int i) const;
    ArmState m_state[kMaxTentacles];
    QVector3D m_lastTarget[kMaxTentacles];
    QVector3D m_lastJoints[kMaxTentacles][TentacleChain::kJoints];
    QPointF m_cursor;
    bool m_cursorOn = false;

    /// Where an arm is rooted, and what it is reaching for, both in scene
    /// units. `hit` is 0 at rest and 1 at full strike.
    QVector3D armBase(int i) const;
    QVector3D armEmerge(int i) const;
    /// Push a target in front of the bin if it is below the rim.
    QVector3D keepInFront(const QVector3D &t) const;
    /// Clamp a target to within reach of the bin, measured from its axis.
    QVector3D keepNear(const QVector3D &t) const;
    /// The leaving flourish: up, thrash, then down out of sight.
    QVector3D retreatTarget(int i, float &flex, float &sink) const;
    QVector3D armTarget(int i, float &flex, float &maxBend) const;
    /// How tightly this arm is rolled up right now, 0..1.
    float rollAmount(int i) const;


    BinMouth m_mouth;
    QSizeF m_binSize {40.0, 40.0};
    QRectF m_binRect;
    float m_time = 0.0f;
    float m_level = 0.0f;
    float m_vel = 0.0f;
    float m_target = 0.0f;
    bool m_wasEmpty = true;
    float m_cameraTilt = 17.0f;
    /// Seconds into the withdrawal, or -1 when not withdrawing.
    ///
    /// A timeline of its own rather than a shape of the level spring,
    /// because the two want opposite things: the spring's job is to reach
    /// zero so isEmpty() can go true and the overlay can be torn down, and
    /// the withdrawal's job is to still be visible while it happens. The
    /// level is HELD until the flourish is done and only then released.
    float m_retreat = -1.0f;
    float m_dt = 0.033f;
    /// One per seat, and they PERSIST between frames. That is the point of
    /// a chain: it is solved from where it already was, so it lags its
    /// target and carries the wave that was running down it. Rebuilt from
    /// scratch each frame it would be as memoryless as the arc was.
    TentacleChain m_chain[kMaxTentacles];
    IconTexture *m_iconTexture = nullptr;
    SpineTexture *m_spine = nullptr;
};

} // namespace hyperbin
