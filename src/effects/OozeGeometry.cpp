#include "OozeGeometry.h"

#include "OozeEffect.h"

#include <QDebug>
#include <QVector2D>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace hyperbin {

OozeGeometry::OozeGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void OozeGeometry::setLevel(float v)
{
    if (qFuzzyCompare(m_level, v))
        return;
    m_level = v;
    rebuild();
    emit levelChanged();
}

void OozeGeometry::setBinWidth(float v)
{
    if (qFuzzyCompare(m_binW, v))
        return;
    m_binW = v;
    rebuild();
    emit binWidthChanged();
}

void OozeGeometry::setBinHeight(float v)
{
    if (qFuzzyCompare(m_binH, v))
        return;
    m_binH = v;
    rebuild();
    emit binHeightChanged();
}

void OozeGeometry::setContentLine(float v)
{
    if (qFuzzyCompare(m_contentLine, v))
        return;
    m_contentLine = v;
    rebuild();
    emit contentLineChanged();
}

void OozeGeometry::setSource(QObject *s)
{
    if (s == m_source)
        return;
    if (m_source)
        m_source->disconnect(this);
    m_source = s;
    auto pull = [this] {
        if (auto *eff = qobject_cast<OozeEffect *>(m_source)) {
            m_shape = eff->shape();
            rebuild();
        }
    };
    if (auto *eff = qobject_cast<OozeEffect *>(m_source))
        connect(eff, &OozeEffect::shapeChanged, this, pull);
    pull();
    emit sourceChanged();
}

void OozeGeometry::rebuild()
{
    clear();

    // The silhouette, measured once and shared. See OozeShape.
    const OozeShape &shape = m_shape;
    const float surfaceY = shape.surfaceY(m_contentLine, m_level);
    if (m_level <= 0.005f) {
        setStride(sizeof(float) * 10);
        setVertexData({});
        setIndexData({});
        update();
        return;
    }
    // --- the outline -------------------------------------------------------
    // The whole body is one profile in the (radius, height) plane, swept
    // around the bin's axis. Everything — puddle, sides, meniscus — is a
    // stretch of that single polyline.
    //
    // It used to be three loops emitting rings independently, and they
    // did not agree: the pool's rows ran downward while the body's ran
    // up, which reversed its winding and back-face culling threw the
    // entire puddle away. One list that only ever goes upward cannot
    // develop that disagreement.
    struct P { float r, y; };
    QVector<P> outline;
    auto push = [&](float r, float y) { outline.push_back({std::max(r, 0.0f), y}); };

    // The puddle. Wider than the foot of the body and rolled under at its
    // edge, so it reads as liquid that has spread out rather than as a
    // disc parked underneath.
    const float poolR      = shape.poolRadius(m_level);
    const float poolBottom = shape.poolBottom();
    const float poolCrest  = shape.poolCrest();
    const float poolTop    = shape.poolTop();
    constexpr int kFoot = 5;
    for (int j = 0; j <= kFoot; ++j) {
        // A quarter-ellipse from the centre of the underside out to the
        // widest point, so the puddle closes rather than leaving a hole
        // for the camera to look up into.
        const float a = float(M_PI_2) * float(j) / kFoot;
        push(poolR * std::sin(a),
             poolBottom + (poolCrest - poolBottom) * (1.0f - std::cos(a)));
    }
    // ...and back in to meet the body, on a smoothstep so the join is a
    // shoulder rather than a crease.
    constexpr int kShoulder = 5;
    for (int j = 1; j <= kShoulder; ++j) {
        const float t = float(j) / kShoulder;
        const float e = t * t * (3.0f - 2.0f * t);
        const float y = poolCrest + (poolTop - poolCrest) * t;
        push(poolR + (shape.radiusAt(y) - poolR) * e, y);
    }

    // The body, following the bin's smoothed taper — with a slight barrel
    // on top of it, swelling at the waist. The bin is a straight-sided
    // cylinder and a gel that copies it exactly comes out looking turned
    // on a lathe; a body of liquid held together by its own surface
    // tension is fatter in the middle.
    const float bodyTop = std::max(surfaceY, poolTop + m_binH * 0.02f);
    // The collar outside the bin sits LOWER than the surface inside it:
    // what has gone over the rim has run down. With both at the same
    // height the gel's outer edge rode level with the liquid and its top
    // crowded the bin's opening from either side, pinching what should
    // have been a hole the full width of the bin into a narrow lens.
    const float collarTop = std::max(bodyTop - m_binH * 0.025f,
                                     poolTop + m_binH * 0.01f);
    // How tall the rolled edge at the top is.
    const float bead = m_binH * 0.030f;
    // Ridges, stacked up the taper.
    //
    // Each one swells toward its top and tucks in sharply above — the
    // same move the lip makes at the rim, repeated on the way down. It is
    // what something thick does as it runs: it gathers, holds, and lets
    // go, and it leaves a ledge everywhere it paused.
    constexpr int kRidges = 3;
    auto ridgeAt = [](float t) {
        const float band = t * kRidges;
        const float u = band - std::floor(band);
        auto ss = [](float a, float b, float x) {
            const float k = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return k * k * (3.0f - 2.0f * k);
        };
        // Uneven. Three identical ledges evenly spaced is a ribbed hose;
        // what makes it read as something that ran is that each pause
        // held a different amount.
        const float weight[3] = {1.0f, 0.55f, 0.80f};
        const int idx = std::clamp(int(band), 0, kRidges - 1);
        return ss(0.0f, 0.75f, u) * (1.0f - ss(0.82f, 1.0f, u)) * weight[idx];
    };
    // Enough rows that the tuck above each ridge is an edge rather than a
    // chamfer: three ridges over twenty-four rows gave each one eight,
    // and its whole falling side was two of them.
    constexpr int kBody = 44;
    for (int j = 1; j <= kBody; ++j) {
        const float t = float(j) / kBody;
        const float y = poolTop + ((collarTop - bead) - poolTop) * t;
        // The collar thins as it climbs. What is still up near the rim is
        // a film; what has run down has piled up below it.
        //
        // It is also what makes the gel's visible top edge read as a
        // round arc rather than a flat line. That edge is a ring seen
        // from above, so it projects as an ellipse — but the cut stops at
        // the bin's outline, so if the ring is wider than the bin you
        // only ever see the middle of that ellipse, which is its
        // flattest part. Bring the ring in to about the bin's own radius
        // and nearly all of the curve is visible.
        // Barely thinned. At fifteen per cent this cancelled the bin's
        // own taper outright — the gel came out a cylinder, and a foot as
        // wide as the rim is exactly what "too fat at the bottom" means.
        const float thin = 1.0f - 0.06f * t * t;
        // Shallower than they were. The travelling wave in the vertex
        // shader now bands the body as well, and the two together were
        // corrugating it — the ledges are where the goo PAUSED, and
        // there is no reason for them to be as deep as the flow itself.
        push(shape.radiusAt(y) * (1.0f + 0.030f * ridgeAt(t)) * thin, y);
    }
    // The lip has to start exactly where the body stopped. Taking it from
    // radiusAt() again dropped the thinning above, so the two met with a
    // fifteen per cent step between them — a ring-shaped crack right
    // across the front of the gel with the bin showing through it.
    const float collarR = outline.last().r;
    // A ROLLED edge, not a round-over.
    //
    // Goo lapping over a wall thickens where it turns and then tucks
    // back under itself, so the top of it is a bead with a crease below.
    // Rolling the body smoothly into the surface instead gave a shape
    // with no edge anywhere on it, which is most of what made the gel
    // read as one rotund lump rather than as something poured.
    push(collarR * 1.060f, collarTop - bead * 0.45f);
    push(collarR * 1.050f, collarTop);
    push(collarR * 0.930f, collarTop + bead * 0.30f);
    // Over the rim: in from the bead to the liquid's own radius. This is
    // what joins the outside of the bin to the inside.
    const float innerR = std::min(shape.rimRadius() * 0.94f, collarR * 0.90f);
    constexpr int kLip = 4;
    const float lipR = outline.last().r, lipY = outline.last().y;
    for (int j = 1; j <= kLip; ++j) {
        const float t = float(j) / kLip;
        const float e = t * t * (3.0f - 2.0f * t);
        push(lipR + (innerR - lipR) * e, lipY + (bodyTop - lipY) * e);
    }
    // The surface itself, closed with a low dome.
    //
    // The body used to be a half-shell — front only — on the theory that
    // an orthographic camera never sees the back. It does see the back of
    // the TOP: the shell draws the bin from above, and looking down into
    // a liquid means looking across its surface at the far rim. Without
    // the back half there was no far rim, so the gel ended in a straight
    // horizontal line and read as a fill gauge rather than as a surface.
    constexpr int kCap = 8;
    for (int j = 1; j <= kCap; ++j) {
        const float a = float(M_PI_2) * float(j) / kCap;
        push(innerR * std::cos(a), bodyTop + innerR * 0.13f * std::sin(a));
    }
    // --- the sweep ---------------------------------------------------------
    constexpr int kU = 64;          // around, all the way round
    constexpr float kDepth = 0.86f; // the bin is a little deeper than wide

    const int nr = outline.size();
    // A true solid of revolution, and it has to stay one.
    //
    // A set of angular harmonics briefly rode on the radius and the
    // height here, so the rim dipped on one side. Baked into the mesh
    // that is a FIXED asymmetry — the same side always lower — and a
    // fixed asymmetry does not read as something sagging. It reads as
    // the whole effect being mis-centred, which is exactly how it was
    // reported. The undulation lives in the vertex shader now, where it
    // drifts and averages out.
    struct V { QVector3D p, n; float thick, height, cap; };
    QVector<V> verts;
    QVector<quint32> idx;
    verts.resize(nr * kU);
    const int firstCap = nr - (kCap + kLip);
    for (int j = 0; j < nr; ++j) {
        // How much gel a ray has to cross here, 0 at the silhouette and 1
        // through the middle. Measured on the CPU because the shader
        // cannot know the body's radius at a given height, and derived
        // from z it made the meniscus — the thing you are looking through
        // the most of — come out the thinnest.
        const float capT = firstCap > 0 && j >= firstCap
                             ? float(j - firstCap + 1) / kCap
                             : 0.0f;
        const float hFrac = std::clamp((outline[j].y - poolBottom)
                                           / std::max(1.0f, bodyTop - poolBottom),
                                       0.0f, 1.0f);
        for (int i = 0; i < kU; ++i) {
            const float a = 2.0f * float(M_PI) * float(i) / kU;
            const float r = outline[j].r;
            const float y = outline[j].y;
            verts[j * kU + i].p = QVector3D(r * std::sin(a), y,
                                            r * std::cos(a) * kDepth);
            verts[j * kU + i].thick = std::max(std::abs(std::cos(a)), capT * 0.9f);
            verts[j * kU + i].height = hFrac;
            verts[j * kU + i].cap = capT;
        }
    }
    // Normals from the MESH, not from the profile.
    //
    // A surface of revolution's normal can be worked out in two
    // dimensions and rotated into place, which is exact and was right
    // until the folds arrived. It is not a surface of revolution any
    // more: the radius varies with angle and so does the height, so the
    // normal has a component around the body that a profile cannot know
    // about. Differencing the neighbours costs nothing here — this runs
    // when the level moves, not per frame — and it cannot fall out of
    // step with whatever the positions above decide to do.
    for (int j = 0; j < nr; ++j) {
        for (int i = 0; i < kU; ++i) {
            const QVector3D &l = verts[j * kU + (i + kU - 1) % kU].p;
            const QVector3D &r = verts[j * kU + (i + 1) % kU].p;
            const QVector3D &d = verts[std::max(j - 1, 0) * kU + i].p;
            const QVector3D &u = verts[std::min(j + 1, nr - 1) * kU + i].p;
            QVector3D n = QVector3D::crossProduct(r - l, u - d);
            if (n.lengthSquared() < 1e-12f)
                n = QVector3D(0.0f, j < nr / 2 ? -1.0f : 1.0f, 0.0f);
            verts[j * kU + i].n = n.normalized();
        }
    }
    for (int j = 0; j < nr - 1; ++j) {
        for (int i = 0; i < kU; ++i) {
            const quint32 a = quint32(j * kU + i);
            const quint32 b = quint32(j * kU + (i + 1) % kU);
            const quint32 c = quint32((j + 1) * kU + i);
            const quint32 d = quint32((j + 1) * kU + (i + 1) % kU);
            // Counter-clockwise seen from outside: rows run upward and
            // the sweep runs left to right across the front.
            idx << a << b << c;
            idx << b << d << c;
        }
    }

    // position, normal, (thickness, height), (cap, 0). The cap flag has
    // to reach the fragment shader so it can cut the bin's shape out of
    // the gel's top surface, and there is nothing else free to carry it.
    constexpr int kFloats = 10;
    QByteArray vbuf;
    vbuf.resize(verts.size() * int(sizeof(float)) * kFloats);
    float *vp = reinterpret_cast<float *>(vbuf.data());
    QVector3D lo = verts.first().p, hi = verts.first().p;
    for (const V &v : verts) {
        *vp++ = v.p.x(); *vp++ = v.p.y(); *vp++ = v.p.z();
        *vp++ = v.n.x(); *vp++ = v.n.y(); *vp++ = v.n.z();
        *vp++ = v.thick; *vp++ = v.height;
        *vp++ = v.cap;   *vp++ = 0.0f;
        lo.setX(std::min(lo.x(), v.p.x())); lo.setY(std::min(lo.y(), v.p.y()));
        lo.setZ(std::min(lo.z(), v.p.z()));
        hi.setX(std::max(hi.x(), v.p.x())); hi.setY(std::max(hi.y(), v.p.y()));
        hi.setZ(std::max(hi.z(), v.p.z()));
    }
    QByteArray ibuf(reinterpret_cast<const char *>(idx.constData()),
                    idx.size() * int(sizeof(quint32)));

    setStride(sizeof(float) * kFloats);
    setVertexData(vbuf);
    setIndexData(ibuf);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, sizeof(float) * 3,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, sizeof(float) * 6,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord1Semantic, sizeof(float) * 8,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                 QQuick3DGeometry::Attribute::U32Type);
    setBounds(lo, hi);
    if (qEnvironmentVariableIsSet("HYPERBIN_DEBUG"))
        qInfo("ooze mesh: %lld verts, level %.2f, floor %.1f surface %.1f",
              (long long)verts.size(), m_level, shape.floorY(), surfaceY);
    update();
}

} // namespace hyperbin
