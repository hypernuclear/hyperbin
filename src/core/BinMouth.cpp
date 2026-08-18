#include "BinMouth.h"

#include <QtGlobal>
#include <algorithm>
#include <vector>

namespace hyperbin {
namespace {

/// Alpha at which artwork counts as present at all. Low: the empty macOS
/// trash is TRANSLUCENT through its opening — you see the desktop through
/// the mouth — so a threshold set for "solid" throws the whole top of the
/// bin away and the silhouette starts below the rim.
constexpr int kAnyAlpha = 20;
/// ...and the one for "solid enough to be the body", used only for the
/// width profile, where the translucent mouth SHOULD be excluded.
constexpr int kSolidAlpha = 128;

} // namespace

BinMouth measureBinMouth(const QImage &icon)
{
    BinMouth m;
    if (icon.isNull())
        return m;

    const QImage src = icon.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 16 || h < 16)
        return m;

    // --- the artwork's own bounds -------------------------------------
    // The icon is a square tile and the bin does not fill it, so every
    // fraction below is against the ARTWORK, not the tile.
    int top = -1, bottom = -1, left = w, right = -1;
    std::vector<int> width(h, 0);
    std::vector<int> solidWidth(h, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        int lo = -1, hi = -1, solid = 0;
        for (int x = 0; x < w; ++x) {
            const int a = qAlpha(row[x]);
            if (a > kAnyAlpha) {
                if (lo < 0)
                    lo = x;
                hi = x;
            }
            if (a > kSolidAlpha)
                ++solid;
        }
        if (lo >= 0) {
            if (top < 0)
                top = y;
            bottom = y;
            left = std::min(left, lo);
            right = std::max(right, hi);
            width[y] = hi - lo + 1;
        }
        solidWidth[y] = solid;
    }
    if (top < 0 || bottom <= top || right <= left)
        return m;

    const float artH = float(bottom - top + 1);
    const float artW = float(right - left + 1);

    // --- the widest row ------------------------------------------------
    // For a tapered bin the widest part is the lip, and the row where the
    // opening is widest is the height of its CENTRE — the ellipse's own
    // horizontal diameter. Searched in the top half only: the puddle of
    // an effect never appears here, but the bin's foot can flare, and on
    // some artwork the base is as wide as the rim.
    int widest = top, widestW = 0;
    for (int y = top; y <= top + int(artH * 0.5f); ++y) {
        if (solidWidth[y] > widestW) {
            widestW = solidWidth[y];
            widest = y;
        }
    }
    if (widestW <= 0)
        return m;

    // --- the near lip ---------------------------------------------------
    // Down the middle third of the artwork, the strongest luminance DROP
    // in the top of the bin is where the lip's lit edge gives way to the
    // shadowed front wall. That is the near arc.
    //
    // Why this signal and not the silhouette: the obvious construction is
    // that the top of the outline is the far lip, so the centre row minus
    // the top gives the ellipse's half-height. It is exactly right on an
    // empty bin and wrong on a full one, because the rubbish stands PROUD
    // of the rim — measured on macOS, a full bin's outline starts 22 rows
    // above an empty one's and the mouth comes out half again too tall.
    //
    // The drop does not care what is in the bin. Measured on both macOS
    // trash states at 512px it lands on row 124 in each, to the row,
    // while being the largest edge in the search band by a factor of two.
    const int c0 = left + int(artW / 3.0f);
    const int c1 = left + int(artW * 2.0f / 3.0f);
    // Scaled to the icon, so the same physical edge is found whatever
    // size the shell hands over.
    const int span = std::max(2, int(artH / 128.0f * 4.0f));
    const int limit = top + int(artH * 0.45f);

    std::vector<float> lum(h, -1.0f);
    for (int y = top; y <= std::min(bottom, limit + span); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        float sum = 0.0f;
        int n = 0;
        for (int x = c0; x <= c1 && x < w; ++x) {
            if (qAlpha(row[x]) <= kAnyAlpha)
                continue;
            sum += 0.299f * qRed(row[x]) + 0.587f * qGreen(row[x])
                 + 0.114f * qBlue(row[x]);
            ++n;
        }
        // A handful of pixels is antialiasing, not a surface.
        if (n >= std::max(4, (c1 - c0) / 8))
            lum[y] = sum / float(n);
    }

    int arc = -1;
    float bestDrop = 0.0f;
    for (int y = top + span; y <= limit; ++y) {
        if (lum[y] < 0.0f || lum[y - span] < 0.0f)
            continue;
        const float drop = lum[y - span] - lum[y];
        if (drop > bestDrop) {
            bestDrop = drop;
            arc = y;
        }
    }

    // Nothing convincing: a flat-shaded or heavily stylised icon may
    // simply not have a lit lip. Fall back rather than trusting a
    // one-count edge, and say so.
    if (arc < 0 || bestDrop < 12.0f)
        return m;

    // The near arc must sit BELOW the widest row — it is the bottom of
    // the same ellipse. If the strongest edge came out above it, what was
    // found is not the lip and the geometry would be inside out.
    if (arc <= widest + 1)
        return m;

    // Against the whole TILE, not the artwork's own bounds.
    //
    // The tile is what gets mapped onto the bin's rect — the icon is a
    // square with the bin sitting somewhere inside it, and the rect
    // covers all of that. Every other measurement in this app is a
    // fraction of the tile (contentLine, OozeShape's rows), and a mouth
    // measured against the artwork instead would be right only for an
    // icon that happened to fill its tile.
    m.centre = QPointF(double(left + right + 1) * 0.5 / double(w),
                       double(widest) / double(h));
    m.halfWidth = float(widestW) * 0.5f / float(w);
    m.depth = float(arc - widest) / float(h);
    m.measured = true;
    return m;
}

} // namespace hyperbin
