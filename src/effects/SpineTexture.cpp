#include "SpineTexture.h"

#include "../core/TentacleChain.h"

namespace hyperbin {

void SpineTexture::setChains(const TentacleChain *chains, int count, int maxArms)
{
    constexpr int kJoints = TentacleChain::kJoints;
    const int rows = maxArms * 2;
    const qsizetype bytes = qsizetype(kJoints) * rows * 4 * qsizetype(sizeof(float));
    if (m_scratch.size() != bytes)
        m_scratch.resize(bytes);
    // Zeroed every frame rather than only on resize: an arm that is not
    // out still has two rows in here, and stale joints from the last time
    // it was out would be sampled by a delegate that has not yet been
    // hidden. Cheap — this is a few kilobytes.
    m_scratch.fill('\0');

    auto *f = reinterpret_cast<float *>(m_scratch.data());
    for (int a = 0; a < count && a < maxArms; ++a) {
        if (!chains[a].valid())
            continue;
        const QVector3D *p = chains[a].joints();
        const QVector3D *s = chains[a].sides();
        float *pos = f + qsizetype(a * 2 + 0) * kJoints * 4;
        float *side = f + qsizetype(a * 2 + 1) * kJoints * 4;
        for (int j = 0; j < kJoints; ++j) {
            pos[j * 4 + 0] = p[j].x();
            pos[j * 4 + 1] = p[j].y();
            pos[j * 4 + 2] = p[j].z();
            pos[j * 4 + 3] = 1.0f;
            side[j * 4 + 0] = s[j].x();
            side[j * 4 + 1] = s[j].y();
            side[j * 4 + 2] = s[j].z();
            side[j * 4 + 3] = 0.0f;
        }
    }

    setSize(QSize(kJoints, rows));
    setFormat(QQuick3DTextureData::RGBA32F);
    setHasTransparency(false);
    setTextureData(m_scratch);
}

} // namespace hyperbin
