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

QVector3D OozeEffect::eyeAt(float t, float angle, float sink) const
{
    // Between the pool's crest and the gel's surface. Not the very
    // bottom: an eye down in the puddle is behind the widest part of the
    // body from a camera that looks slightly down, so it never shows.
    const float lo = m_shape.poolCrest();
    const float hi = m_shape.surfaceY(m_contentLine, m_sim.level());
    const float y = lo + (hi - lo) * qBound(0.0f, t, 1.0f);

    const float r = m_shape.radiusAt(y) * sink;
    // angle 0 puts the eye on +z, which is the side the camera is on.
    return QVector3D(r * std::sin(angle), y, r * std::cos(angle));
}

} // namespace hyperbin

#include "OozeEffect.moc"
