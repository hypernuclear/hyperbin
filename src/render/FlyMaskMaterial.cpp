#include "FlyMaskMaterial.h"

#include <QSGTexture>

namespace hyperbin {

namespace {
QSGMaterialType g_type;
} // namespace

FlyMaskMaterial::FlyMaskMaterial()
{
    // Fly sprites are antialiased and the mask erases through them, so
    // this always needs blending.
    setFlag(QSGMaterial::Blending, true);

    // Opt out of batch merging. The batch renderer merges compatible draw
    // calls by rewriting the vertex shader to inject its own attribute;
    // a custom shader that consumes the combined matrix itself can't be
    // rewritten, and the attempt fails at runtime with "No
    // rewriter-inserted attribute found, this should not happen."
    setFlag(QSGMaterial::RequiresFullMatrix, true);
}

QSGMaterialType *FlyMaskMaterial::type() const
{
    return &g_type;
}

QSGMaterialShader *FlyMaskMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new FlyMaskShader;
}

int FlyMaskMaterial::compare(const QSGMaterial *other) const
{
    const auto *o = static_cast<const FlyMaskMaterial *>(other);
    if (sprite != o->sprite)
        return sprite < o->sprite ? -1 : 1;
    if (mask != o->mask)
        return mask < o->mask ? -1 : 1;
    if (binRect != o->binRect)
        return binRect.x() < o->binRect.x() ? -1 : 1;
    if (debugRect != o->debugRect)
        return debugRect ? 1 : -1;
    if (maskMode != o->maskMode)
        return maskMode < o->maskMode ? -1 : 1;
    return 0;
}

FlyMaskShader::FlyMaskShader()
{
    setShaderFileName(VertexStage, QStringLiteral(":/shaders/flymask.vert.qsb"));
    setShaderFileName(FragmentStage, QStringLiteral(":/shaders/flymask.frag.qsb"));
}

bool FlyMaskShader::updateUniformData(RenderState &state, QSGMaterial *newMaterial,
                                      QSGMaterial *)
{
    QByteArray *buf = state.uniformData();
    Q_ASSERT(buf->size() >= 128);  // std140: debugColor vec4 ends at 128
    bool changed = false;

    if (state.isMatrixDirty()) {
        const QMatrix4x4 m = state.combinedMatrix();
        memcpy(buf->data(), m.constData(), 64);
        changed = true;
    }
    if (state.isOpacityDirty()) {
        const float o = state.opacity();
        memcpy(buf->data() + 64, &o, 4);
        changed = true;
    }

    // std140 puts the vec4 on a 16-byte boundary, so it starts at 80,
    // not immediately after the float at 64.
    const auto *mat = static_cast<FlyMaskMaterial *>(newMaterial);
    QRectF r = mat->binRect;
    // Degenerate rect => mask coord never lands in [0,1] => nothing masked.
    // The debug view deliberately doesn't require a mask texture, so the
    // rect can still be inspected in windowed mode where no bin icon is
    // ever loaded.
    if (r.isEmpty() || (!mat->mask && !mat->debugRect))
        r = QRectF(0, 0, -1, -1);
    const float rect[4] = {float(r.x()), float(r.y()),
                           float(r.width()), float(r.height())};
    memcpy(buf->data() + 80, rect, 16);
    const float dbg = mat->debugRect ? 1.0f : 0.0f;
    memcpy(buf->data() + 96, &dbg, 4);
    const float mode = float(mat->maskMode);
    memcpy(buf->data() + 100, &mode, 4);
    memcpy(buf->data() + 112, mat->debugRGBA, 16);  // std140: vec4 on 16b
    changed = true;

    return changed;
}

void FlyMaskShader::updateSampledImage(RenderState &state, int binding,
                                       QSGTexture **texture,
                                       QSGMaterial *newMaterial, QSGMaterial *)
{
    auto *mat = static_cast<FlyMaskMaterial *>(newMaterial);
    // Binding 2 falls back to the sprite when there's no mask yet: a null
    // texture here would drop the whole draw call, losing the flies too.
    QSGTexture *t = (binding == 1) ? mat->sprite
                                   : (mat->mask ? mat->mask : mat->sprite);
    if (!t)
        return;
    // Must be committed with the *render state's* rhi and update batch —
    // passing nulls segfaults the render thread.
    t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    *texture = t;
}

} // namespace hyperbin
