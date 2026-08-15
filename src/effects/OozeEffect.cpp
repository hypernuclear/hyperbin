#include "OozeEffect.h"

#include <QQuick3DTextureData>
#include <cmath>

namespace hyperbin {

/// The bin's artwork, handed to Qt Quick 3D as texture data.
///
/// QQuick3DTextureData rather than a file or a QSGTexture: the artwork
/// comes from the shell at runtime, changes when the bin fills or
/// empties, and never exists on disk.
class OozeTextureData : public QQuick3DTextureData
{
    Q_OBJECT
public:
    void setImage(const QImage &img)
    {
        if (img.isNull()) {
            setTextureData({});
            return;
        }
        const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        setSize(rgba.size());
        setFormat(QQuick3DTextureData::RGBA8);
        setHasTransparency(true);
        setTextureData(QByteArray(reinterpret_cast<const char *>(rgba.constBits()),
                                  qsizetype(rgba.sizeInBytes())));
    }
};

OozeEffect::OozeEffect(QObject *parent)
    : Effect(parent)
    , m_iconTexture(new OozeTextureData)
{
}

OozeEffect::~OozeEffect()
{
    delete m_iconTexture;
}
QUrl OozeEffect::visualSource() const
{
    return QUrl(QStringLiteral("qrc:/qt/qml/hyperbin/qml/OozeVisual.qml"));
}

QObject *OozeEffect::iconTexture() const
{
    return m_iconTexture;
}

void OozeEffect::setBinImage(const QImage &img)
{
    m_iconTexture->setImage(img);
    emit shapeChanged();
}

void OozeEffect::setBinRect(const QRectF &binRect)
{
    m_sim.setBinRect(binRect);
    if (m_binRect != binRect) {
        m_binRect = binRect;
        m_binSize = binRect.size();
        m_shape.measure(m_coverage, m_covW, m_covH, float(m_binSize.width()),
                        float(m_binSize.height()));
        emit shapeChanged();
    }
}

void OozeEffect::setFullness(float fullness) { m_sim.setFullness(fullness); }

void OozeEffect::setCursor(const QPointF &, bool)
{
    // Sludge is not startled. Nothing here reacts to a pointer, so the
    // effect never reports itself dismissed.
}

void OozeEffect::setContentLine(float y01)
{
    if (qFuzzyCompare(m_contentLine, y01))
        return;
    m_contentLine = y01;
    emit shapeChanged();
}

void OozeEffect::setSurface(const QVector<quint8> &coverage, int w, int h)
{
    m_coverage = coverage;
    m_covW = w;
    m_covH = h;
    // No distance field here any more. It existed to trim the mesh's
    // edge against the bin's silhouette, back when the mesh was a height
    // field that overshot it. The mesh is its own shape now — a swept
    // profile — so the field was a 128x128 chamfer pass and a texture
    // upload on every icon change that nothing sampled.
    m_shape.measure(m_coverage, m_covW, m_covH, float(m_binSize.width()),
                    float(m_binSize.height()));
    emit shapeChanged();
}

void OozeEffect::step(float dt)
{
    m_sim.step(dt);
    updateEyes();
    emit frameChanged();

    const bool empty = isEmpty();
    if (empty != m_wasEmpty) {
        m_wasEmpty = empty;
        emit activityChanged();
    }
}

QMargins OozeEffect::margins(qreal iconSize) const
{
    const int x = OozeSim::marginX(iconSize);
    return QMargins(x, OozeSim::marginTop(iconSize),
                    x, OozeSim::marginBottom(iconSize));
}

OozeEffect::EyeSeat OozeEffect::sweepSeat(float r, float y, float drdp,
                                          float dydp, float angle)
{
    // Every surface here is one ring radius and one height, both moving
    // with a single parameter and swept round the axis. Its normal falls
    // straight out of the two tangents:
    //
    //   P(a, p) = (r sin a, y, r D cos a)
    //   dP/da x dP/dp  =  r * (D y' sin a, -r' D, y' cos a)
    //
    // The leading r only scales it, so it drops out in the normalise.
    // Note the D on the x term and NOT on the z: a squashed circle's
    // normal leans toward the flattened axis, which is why an eye on the
    // front of the body faces the camera more squarely than one on the
    // side faces sideways.
    //
    // The body walks this with p = height, so y' is 1 and r' is the
    // profile's slope. The puddle walks it with p along the shoulder,
    // where both move — and there r' is large and negative, which is what
    // turns the normal to face nearly straight up.
    // angle 0 puts the eye on +z, which is the side the camera is on.
    EyeSeat s;
    s.pos = QVector3D(r * std::sin(angle), y,
                      r * std::cos(angle) * OozeShape::kDepth);
    s.nrm = QVector3D(OozeShape::kDepth * dydp * std::sin(angle),
                      -drdp * OozeShape::kDepth,
                      dydp * std::cos(angle))
                .normalized();
    return s;
}

OozeEffect::EyeSeat OozeEffect::bodySeat(float t, float angle) const
{
    // Between the pool's crest and the gel's surface.
    const float lo = m_shape.poolCrest();
    const float hi = m_shape.surfaceY(m_contentLine, m_sim.level());
    const float y = lo + (hi - lo) * qBound(0.0f, t, 1.0f);

    const float e = std::max(0.5f, float(m_binSize.height()) * 0.02f);
    const float dR = (m_shape.radiusAt(y + e) - m_shape.radiusAt(y - e)) / (2.0f * e);
    return sweepSeat(m_shape.radiusAt(y), y, dR, 1.0f, angle);
}

OozeEffect::EyeSeat OozeEffect::poolSeat(float u, float angle) const
{
    // The puddle's top, which is the shoulder OozeGeometry sweeps: out at
    // the rim it is the widest part of the spill, and it climbs and draws
    // back in on a smoothstep until it meets the foot of the body. Walked
    // here with the same two lines the mesh uses, because an eye that sat
    // on a different curve from the one the goo is made of would float
    // above the spill or disappear under it.
    const float poolR = m_shape.poolRadius(m_sim.level());
    const float crest = m_shape.poolCrest();
    const float topY = m_shape.poolTop();
    const auto ring = [&](float p) {
        p = qBound(0.0f, p, 1.0f);
        const float y = crest + (topY - crest) * p;
        const float e = p * p * (3.0f - 2.0f * p);
        return QVector3D(poolR + (m_shape.radiusAt(y) - poolR) * e, y, 0.0f);
    };
    constexpr float kDu = 0.02f;
    const QVector3D at = ring(u);
    const QVector3D lo = ring(u - kDu), hi = ring(u + kDu);
    return sweepSeat(at.x(), at.y(),
                     (hi.x() - lo.x()) / (2.0f * kDu),
                     (hi.y() - lo.y()) / (2.0f * kDu), angle);
}

/// The same hash the scene used when it placed these itself, so an eye
/// keeps the identity it already had: same index, same size, same spot.
static float eyeHash(float a, float b)
{
    const float s = std::sin(a * 127.1f + b * 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

void OozeEffect::updateEyes()
{
    m_eyeSpheres.clear();
    m_eyeNormals.clear();

    const int n = qBound(0, int(std::lround(m_sim.level() * kMaxEyes)), kMaxEyes);
    const float t = m_sim.time();
    const float binW = float(m_binSize.width());

    for (int i = 0; i < n; ++i) {
        const float fi = float(i);
        const float seedA = eyeHash(fi, 1.0f);
        const float seedB = eyeHash(fi, 2.0f);
        const float seedC = eyeHash(fi, 3.0f);

        // RADIUS, not diameter. The mesh is a unit sphere, so a scale of s
        // spans 2s — sized as a diameter, the first pass came out at twice
        // the intent and the eyes sat on the gel rather than in it.
        const float radius = binW * (0.036f + 0.028f * seedA);

        // WHERE an eye is does not change, and that is the point.
        //
        // These used to walk round the body — an angle with the clock in
        // it — and a ball of gel with eyes revolving through it does not
        // read as anything alive. Nothing suspended in something this
        // thick travels; it sits where it set. What is left is a wobble
        // small enough to be the gel shifting under it rather than the eye
        // going anywhere: about a degree of turn and a hair of rise.
        //
        // Where they go is a LOW-DISCREPANCY set, not a random draw.
        //
        // Random was fine while they travelled: one that started out of
        // sight came round eventually. Now that they stay put, a draw
        // that happens to put six of the nine on the back of the bin puts
        // them there for good, which is exactly what the first run of
        // this looked like.
        //
        // The two coordinates get DIFFERENT constructions, and the reason
        // is that this is nine points, not nine hundred.
        //
        // An R2 sequence went in first, on the reasoning that a
        // two-dimensional low-discrepancy set is the right tool for a
        // two-dimensional placement. It is — asymptotically. At nine
        // points its stride comes back to within a fiftieth of where it
        // started after only four steps, so while the 2D spread is fine
        // its ONE-dimensional projections clump into pairs and triples.
        // An angle that clumps is three eyes stacked in a vertical line
        // down the front of the bin, which is what it drew.
        //
        // A golden-angle stride has no such return and lays nine points
        // out very nearly evenly; the radical inverse in base two does
        // the same for the height and does not track it. Both are chosen
        // for THIS count rather than in the limit.
        const float ua = std::fmod(fi * 0.3819660113f, 1.0f);
        float ub = 0.0f;
        for (int bits = i, place = 1; bits; bits >>= 1, ++place)
            ub += (bits & 1) ? std::ldexp(1.0f, -place) : 0.0f;

        // Squeezed toward the camera, and LINEARLY.
        //
        // Spread over the whole circle, two thirds of them sit on a side
        // of the body the camera never sees. Bending the angle by a power
        // was the first attempt at that and it traded one fault for
        // another: a power pulls hardest near zero, so it stacked eyes on
        // the centre line while barely moving the ones round the back. A
        // plain scale keeps whatever spacing it was handed and only
        // narrows the arc — nine eyes evenly spread across the front and
        // both flanks, the outermost pair just past the silhouette where
        // they read as something further down in the goo rather than as
        // nothing at all.
        const float bent = (2.0f * ua - 1.0f) * 3.14159265f * 0.62f;
        const float angle = bent + 0.020f * std::sin(t * 0.21f + fi * 1.7f);

        // The low third of them go in the PUDDLE, not on the wall.
        //
        // Which third is not a separate draw — it is the bottom of the
        // same height sequence, so the two populations cannot both grow
        // or both empty out. The spill is a nearly horizontal sheet and
        // the body is a nearly vertical one, so an eye in it is looking
        // up out of a flat pool rather than out of a wall; that falls out
        // of the seat's own normal and needs nothing said here.
        constexpr float kPoolShare = 0.22f;
        const bool inPool = ub < kPoolShare;

        // Stopping short of the surface, and how far short depends on
        // which way round the body it is.
        //
        // The camera looks DOWN. An eye on the far side of the body is
        // therefore drawn HIGHER on screen than one at the same height in
        // front of it, by about twice the body's radius times the sine of
        // the tilt — here roughly a third of the whole range. Left with
        // one ceiling for all of them, the ones at the back climbed over
        // the bin's front rim; and because the gel's lid is cut away
        // exactly there, they did not disappear behind the goo, they
        // appeared to float in the bin's open mouth.
        //
        // The front gets the higher ceiling anyway. The gel's collar has
        // run down the OUTSIDE of the bin and sits lower than the liquid
        // within it, so even there the top of the range is not the top of
        // the gel.
        const float back = 0.5f * (1.0f - std::cos(bent));
        const float top = 0.90f - 0.34f * back;
        const float height = 0.16f + (top - 0.16f) * (0.94f * ub + 0.06f * seedB)
                           + 0.008f * std::sin(t * 0.27f + fi * 2.3f);

        const EyeSeat seat = inPool
            ? poolSeat(0.14f + 0.62f * (ub / kPoolShare), angle)
            : bodySeat(height, angle);

        // The whole of the motion, and it runs along the normal: an eye
        // rises out of the gel and settles back into it.
        //
        // Measured in the EYE's own radius rather than the body's, which
        // is what lets one number serve a wall and a puddle at once — and
        // the eyes vary in size by nearly two to one, so a distance fixed
        // against the bin sat the small ones deeper than the large ones
        // for no reason anyone could see.
        //
        // The useful range is narrow and not obvious. At 0 the ball's
        // centre is ON the surface and half of it stands clear, which is
        // already enough to expose the pupil; the gel's own lumps swing
        // by more than this does, so the low end of the range is where
        // they close over it. Much past 0.6 and it is a bead resting on
        // top. Each eye breathes at its own rate so they never pulse
        // together.
        const float proud = radius * (0.30f
            + 0.32f * std::sin(t * (0.28f + 0.14f * seedA) + seedC * 6.2831853f));

        // Blinking, and only how MUCH — the lids themselves are drawn on
        // the eyeball, in shaders/eye3d.frag, because the gel is 64
        // segments round and one of them is as wide as a whole eye.
        //
        // Not a symmetric fade. A real lid slams shut in about a tenth of
        // a second, rests there, and opens back at half the speed, and
        // that asymmetry is most of what makes a blink read as a blink
        // rather than as something dissolving. Each eye keeps its own
        // period and phase, so nine of them never go together.
        constexpr float kShut = 0.11f, kHold = 0.05f, kOpen = 0.21f;
        const float period = 3.2f + 4.6f * seedB;
        const float phase = std::fmod(t + seedC * period, period);
        float open = 1.0f;
        if (phase < kShut) {
            open = 1.0f - phase / kShut;
        } else if (phase < kShut + kHold) {
            open = 0.0f;
        } else if (phase < kShut + kHold + kOpen) {
            const float o = (phase - kShut - kHold) / kOpen;
            open = o * o * (3.0f - 2.0f * o);
        }

        m_eyeSpheres.append(
            QVariant::fromValue(QVector4D(seat.pos + seat.nrm * proud, radius)));
        m_eyeNormals.append(QVariant::fromValue(QVector4D(seat.nrm, open)));
    }
}

} // namespace hyperbin

#include "OozeEffect.moc"
