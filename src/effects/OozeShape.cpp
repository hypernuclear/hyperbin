#include "OozeShape.h"

#include <algorithm>
#include <cmath>

namespace hyperbin {

void OozeShape::measure(const QVector<quint8> &coverage, int gridW, int gridH,
                        float binW, float binH)
{
    m_binW = binW > 0.0f ? binW : 40.0f;
    m_binH = binH > 0.0f ? binH : 40.0f;
    const float halfH = m_binH * 0.5f;
    m_floorY = -halfH;

    if (coverage.isEmpty() || gridW <= 0 || gridH <= 0) {
        for (int r = 0; r < kRows; ++r)
            m_prof[r] = m_binW * 0.5f * (0.72f + 0.28f * (1.0f - r / float(kRows)));
        m_widest = m_binW * 0.5f;
        m_rimY = halfH - m_binH * 0.22f;
        return;
    }

    // Half-width at each height, measured off the artwork.
    int lastRow = -1;
    for (int r = 0; r < kRows; ++r) {
        const int gy = std::clamp(r * gridH / kRows, 0, gridH - 1);
        int lo = -1, hi = -1;
        for (int gx = 0; gx < gridW; ++gx) {
            if (!coverage[gy * gridW + gx])
                continue;
            if (lo < 0)
                lo = gx;
            hi = gx;
        }
        m_prof[r] = lo < 0 ? 0.0f
                           : (hi - lo + 1) * 0.5f / float(gridW) * m_binW;
        if (lo >= 0)
            lastRow = r;
    }
    if (lastRow >= 0)
        m_floorY = halfH - ((lastRow + 1) / float(kRows)) * m_binH;

    // Three passes of a box blur. Taking the silhouette as it comes was
    // the original mistake: the gel inherited the icon's slots and lid
    // notch and BECAME the bin rather than a body the bin sits in. The
    // blur keeps the one thing that matters — the taper — and discards
    // the detail.
    for (int pass = 0; pass < 3; ++pass) {
        float tmp[kRows];
        for (int r = 0; r < kRows; ++r) {
            const int a = std::max(0, r - 2), b = std::min(kRows - 1, r + 2);
            float s = 0.0f;
            int n = 0;
            for (int q = a; q <= b; ++q) {
                if (m_prof[q] <= 0.0f)
                    continue;
                s += m_prof[q];
                ++n;
            }
            tmp[r] = n ? s / n : m_prof[r];
        }
        std::copy(tmp, tmp + kRows, m_prof);
    }

    m_widest = 0.0f;
    int widestRow = 0;
    for (int r = 0; r < kRows; ++r) {
        if (m_prof[r] > m_widest) {
            m_widest = m_prof[r];
            widestRow = r;
        }
    }
    // The lip is a band, not a line — the widest row is somewhere in the
    // middle of it — so the rim proper sits a little below where the
    // profile peaks.
    m_rimY = halfH - ((widestRow + 1.5f) / float(kRows)) * m_binH;
}

float OozeShape::radiusAt(float y) const
{
    const float halfH = m_binH * 0.5f;
    const float t = std::clamp((halfH - y) / m_binH, 0.0f, 0.9999f);
    const float f = t * (kRows - 1);
    const int i0 = int(f);
    const int i1 = std::min(i0 + 1, kRows - 1);
    const float w = m_prof[i0] + (m_prof[i1] - m_prof[i0]) * (f - i0);
    // Wider than the bin at EVERY height, not just tapered like it.
    // Following the local profile exactly meant the gel met the outline
    // instead of swallowing it; leaning it toward the bin's widest point
    // fattens the lower body where the bin is narrow, which is what makes
    // it look surrounded rather than worn.
    // Mostly the LOCAL width. Leaning a quarter of the way toward the
    // bin's widest point filled in its taper and left the gel close to a
    // cylinder — fat at the foot, where the bin is actually narrowest.
    const float blended = w * 0.90f + m_widest * 0.10f;
    // Ten per cent proud of the bin, not eighteen. The widening is a
    // multiplier, so it keeps whatever taper the profile has — but the
    // fatter it is, the more the pool and the ridges have to add on top
    // before the foot looks wider than the rim.
    return std::max(blended, m_binW * 0.05f) * 1.10f;
}

float OozeShape::surfaceY(float contentLine, float level) const
{
    const float halfH = m_binH * 0.5f;
    const float contentY = std::min(halfH - contentLine * m_binH, m_rimY);
    return m_floorY + (contentY - m_floorY) * level;
}

} // namespace hyperbin
