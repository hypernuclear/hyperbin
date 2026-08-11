// Scene-graph material that draws the fly sprite and erases it where the
// bin's artwork is opaque.
//
// The alternative — compositing our own copy of the Trash icon over the
// swarm — was tried and rejected: it has to track the Dock's icon
// exactly, and the Accessibility poll it depends on always lags a frame
// or two under magnification, so the copy visibly desyncs from the real
// icon. A mask degrades far more gracefully, and the Dock keeps drawing
// its own artwork so there's no colour or rendering mismatch at all.
#pragma once

#include <QRectF>
#include <QSGMaterial>
#include <QSGMaterialShader>

class QSGTexture;

namespace hyperbin {

class FlyMaskMaterial : public QSGMaterial
{
public:
    FlyMaskMaterial();

    QSGMaterialType *type() const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override;

    /// Not owned — the item keeps both alive for the node's lifetime.
    QSGTexture *sprite = nullptr;
    QSGTexture *mask   = nullptr;

    /// Bin artwork rect in item coordinates. Empty disables masking.
    QRectF binRect;
    /// HYPERBIN_SHOW_BINRECT: paint the mask region instead of masking,
    /// so its placement is directly visible.
    bool debugRect = false;

    /// 0 none, 1 occlude (fly is behind the bin), 2 clip (fly is
    /// crawling on it, so it's visible only over the silhouette).
    int maskMode = 1;
    /// Tint for the debug silhouette. Distinct per candidate in
    /// calibration mode, so several offsets can be judged in one look.
    float debugRGBA[4] = {0.0f, 0.85f, 0.2f, 1.0f};
};

class FlyMaskShader : public QSGMaterialShader
{
public:
    FlyMaskShader();

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial,
                           QSGMaterial *oldMaterial) override;
    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                            QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override;
};

} // namespace hyperbin
