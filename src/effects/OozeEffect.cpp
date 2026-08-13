#include "OozeEffect.h"

#include "../core/DistanceField.h"
#include "../render/OozeMaterial.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTexture>

namespace hyperbin {

namespace {
/// Resolution of the distance field. The coverage grid it comes from is
/// coarse — it only has to answer "is the bin here" — but a field this
/// one hugs a shape with, so it is resampled up and the transform run at
/// a resolution where the taper of the bin is actually a curve.
constexpr int kFieldSize = 128;
/// How far, in field cells, the encoded 0..1 range covers. Only the
/// neighbourhood of the surface matters; spending the 8 bits there is
/// what keeps the coating's edge smooth.
constexpr float kFieldRangeCells = 20.0f;
} // namespace

OozeEffect::OozeEffect(QObject *parent)
    : Effect(parent)
{
}

OozeEffect::~OozeEffect()
{
    delete m_fieldTex;
}

void OozeEffect::setBinRect(const QRectF &binRect) { m_sim.setBinRect(binRect); }
void OozeEffect::setFullness(float fullness)       { m_sim.setFullness(fullness); }

void OozeEffect::setCursor(const QPointF &, bool)
{
    // Sludge is not startled. Nothing here reacts to a pointer, so the
    // effect never reports itself dismissed.
}

void OozeEffect::setSurface(const QVector<quint8> &coverage, int w, int h)
{
    m_field = buildSignedDistanceField(coverage, w, h, kFieldSize, kFieldRangeCells);
    m_fieldRangeCells = kFieldRangeCells;
    m_fieldDirty = true;
}

void OozeEffect::step(float dt)
{
    m_sim.step(dt);
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

void OozeEffect::releaseResources()
{
    delete m_fieldTex;
    m_fieldTex = nullptr;
    m_fieldDirty = true;
}

QSGNode *OozeEffect::updateNode(QSGNode *old, QQuickWindow *window,
                                const QRectF &binRect, QSGTexture *mask)
{
    // `mask` is the bin's artwork in colour. It is no longer used to cut
    // the sludge to shape — the distance field does that — but it IS what
    // shows through the sludge, which is the whole transmission look.
    if (m_sim.isEmpty() || binRect.isEmpty() || m_field.isNull()) {
        delete old;
        return nullptr;
    }

    auto *node = static_cast<QSGGeometryNode *>(old);
    if (!node) {
        node = new QSGGeometryNode;
        auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        node->setGeometry(g);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new OozeMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    if (m_fieldDirty) {
        m_fieldDirty = false;
        delete m_fieldTex;
        m_fieldTex = window->createTextureFromImage(m_field);
        if (m_fieldTex) {
            // Linear, and clamped: the field is smooth by construction so
            // interpolating it is meaningful, and clamping keeps a sample
            // just outside the icon reading as "outside" rather than
            // wrapping to the far edge.
            m_fieldTex->setFiltering(QSGTexture::Linear);
            m_fieldTex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_fieldTex->setVerticalWrapMode(QSGTexture::ClampToEdge);
        }
    }

    const float icon = float(qMax(binRect.width(), binRect.height()));
    auto *mat = static_cast<OozeMaterial *>(node->material());
    mat->field      = m_fieldTex;
    mat->icon       = mask;
    mat->binRect    = binRect;
    mat->level      = m_sim.level();
    mat->time       = m_sim.time();
    mat->coat       = icon * 0.045f;
    mat->fieldRange = m_fieldRangeCells * icon / float(kFieldSize);
    mat->dripCount  = 6;
    mat->dripRadius = icon * 0.085f;
    mat->dripReach  = icon * OozeSim::kDripReach;
    mat->contentLine = m_contentLine;
    mat->bubbleCount  = 5;
    mat->bubbleRadius = icon * 0.055f;
    mat->bubbleSpeed  = 0.45f;
    mat->refraction   = icon * 0.10f;
    mat->dispersion   = 0.35f;
    mat->roughness    = icon * 0.030f;
    mat->absorption   = 0.85f;
    mat->wobble       = icon * 0.020f;

    // The quad covers the icon plus the room a drip can reach below it.
    // Sized to the effect rather than to the item, so the fragment shader
    // is not asked about pixels no drip could ever occupy.
    // The quad covers the icon plus exactly as far as a drip can reach —
    // no more. The fragment shader walks every drip for every pixel it
    // covers, so asking about pixels nothing can occupy is pure waste.
    const QRectF area = binRect.adjusted(-mat->coat * 2, -mat->coat * 2,
                                         mat->coat * 2,
                                         mat->dripReach * OozeSim::kDripStretch
                                             + mat->coat * 3);
    auto *v = node->geometry()->vertexDataAsPoint2D();
    v[0].set(float(area.left()),  float(area.top()));
    v[1].set(float(area.right()), float(area.top()));
    v[2].set(float(area.left()),  float(area.bottom()));
    v[3].set(float(area.right()), float(area.bottom()));
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}

} // namespace hyperbin
