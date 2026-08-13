// The sludge, as actual geometry.
//
// A solid of revolution: one profile in the (radius, height) plane —
// puddle, sides, meniscus — swept around the bin's axis. That gives a
// closed volume the PBR material can light and refract through, which an
// implicit surface evaluated in a fragment shader never could: it had no
// normals worth the name and no thickness at all.
//
// The profile is measured from the bin's own artwork and smoothed, so
// the body tapers the way the bin does without inheriting its slots and
// lid notch, and it is widened so the gel envelops the icon rather than
// stopping at its outline. OozeShape owns that measurement — see there
// for why it is not worked out in two places.
#pragma once

#include "OozeShape.h"

#include <QQuick3DGeometry>

namespace hyperbin {

class OozeGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    /// 0..1, how far up the bin the sludge has risen.
    Q_PROPERTY(float level READ level WRITE setLevel NOTIFY levelChanged)
    /// Size of the bin in scene units. The mesh is built centred on the
    /// origin so the QML never has to position it.
    Q_PROPERTY(float binWidth READ binWidth WRITE setBinWidth NOTIFY binWidthChanged)
    Q_PROPERTY(float binHeight READ binHeight WRITE setBinHeight NOTIFY binHeightChanged)
    /// Where the bin's contents start, 0..1 from its top.
    Q_PROPERTY(float contentLine READ contentLine WRITE setContentLine
                   NOTIFY contentLineChanged)
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)

public:
    explicit OozeGeometry(QQuick3DObject *parent = nullptr);

    float level() const { return m_level; }
    void  setLevel(float v);
    float binWidth() const { return m_binW; }
    void  setBinWidth(float v);
    float binHeight() const { return m_binH; }
    void  setBinHeight(float v);
    float contentLine() const { return m_contentLine; }
    void  setContentLine(float v);

    /// The effect that owns the state this mesh is built from. Set from
    /// QML. The silhouette is pulled from it rather than passed as a
    /// property: it is a few kilobytes of grid, and it changes only when
    /// the icon does.
    QObject *source() const { return m_source; }
    void     setSource(QObject *s);

signals:
    void levelChanged();
    void binWidthChanged();
    void binHeightChanged();
    void contentLineChanged();
    void sourceChanged();

private:
    void rebuild();

    QObject *m_source = nullptr;
    OozeShape m_shape;
    float m_level = 0.0f;
    float m_binW = 40.0f;
    float m_binH = 40.0f;
    float m_contentLine = 0.22f;
};

} // namespace hyperbin
