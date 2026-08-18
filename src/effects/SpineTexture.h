// The solved tentacle chains, handed to the vertex shader as a texture.
//
// A texture and not uniforms, and that is forced rather than chosen:
// CustomMaterial has no array uniforms, so sixteen joints across three
// arms would have to be written out as ninety-six separate vector3d
// properties and then read back by an unrolled ladder of comparisons in
// the shader. The gel's eyes are already unrolled that way over fourteen
// slots (qml/OozeEyes.qml) and it is at the limit of what is tolerable.
//
// QQuick3DTextureData is the established way round it here — the bin's own
// artwork arrives the same way, see IconTexture — and it costs almost
// nothing: three arms of sixteen joints, position and frame, is 1.5KB a
// frame against a texture upload path that already exists.
#pragma once

#include <QQuick3DTextureData>
#include <QVector3D>

namespace hyperbin {

class TentacleChain;

class SpineTexture : public QQuick3DTextureData
{
    Q_OBJECT
public:
    /// Two rows per arm: joint positions, then the frame across the arm.
    /// Laid out so the shader can fetch both with one row offset.
    void setChains(const TentacleChain *chains, int count, int maxArms);

private:
    QByteArray m_scratch;
};

} // namespace hyperbin
