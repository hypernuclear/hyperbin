#include "DistanceField.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hyperbin {

namespace {

/// Two-pass chamfer distance transform, 3-4 weights.
///
/// Not exact — a true Euclidean transform needs 8SSEDT or jump flooding —
/// but the error is under 2% for a 3-4 kernel, and this field is used to
/// hug a shape and hang drips off it, not to measure anything. Two linear
/// passes over a 128x128 grid is nothing; the exact versions are several
/// times the code for an error nobody can see at 40 points across.
void chamfer(QVector<float> &d, int w, int h)
{
    constexpr float kOrtho = 1.0f;
    constexpr float kDiag  = 1.41421356f;
    auto at = [&](int x, int y) -> float & { return d[y * w + x]; };
    auto relax = [](float &v, float other, float cost) {
        v = std::min(v, other + cost);
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float &v = at(x, y);
            if (y > 0) {
                relax(v, at(x, y - 1), kOrtho);
                if (x > 0)     relax(v, at(x - 1, y - 1), kDiag);
                if (x < w - 1) relax(v, at(x + 1, y - 1), kDiag);
            }
            if (x > 0) relax(v, at(x - 1, y), kOrtho);
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            float &v = at(x, y);
            if (y < h - 1) {
                relax(v, at(x, y + 1), kOrtho);
                if (x > 0)     relax(v, at(x - 1, y + 1), kDiag);
                if (x < w - 1) relax(v, at(x + 1, y + 1), kDiag);
            }
            if (x < w - 1) relax(v, at(x + 1, y), kOrtho);
        }
    }
}

} // namespace

QImage buildSignedDistanceField(const QVector<quint8> &coverage, int w, int h,
                                int outSize, float rangeCells)
{
    if (w <= 0 || h <= 0 || coverage.size() < w * h || outSize <= 0)
        return {};

    const int n = outSize;
    const float big = float(n * 4);

    // Resample the coverage grid up to the working size. Nearest is
    // deliberate: the grid is already a hard in/out decision, and
    // smoothing it here would only blur a boundary the transform is
    // about to measure precisely anyway.
    QVector<quint8> solid(n * n, 0);
    for (int y = 0; y < n; ++y) {
        const int sy = std::clamp(y * h / n, 0, h - 1);
        for (int x = 0; x < n; ++x) {
            const int sx = std::clamp(x * w / n, 0, w - 1);
            solid[y * n + x] = coverage[sy * w + sx];
        }
    }

    // Distance to the nearest solid cell, and to the nearest empty one.
    // The signed field is the difference: outside minus inside.
    QVector<float> outside(n * n), inside(n * n);
    for (int i = 0; i < n * n; ++i) {
        outside[i] = solid[i] ? 0.0f : big;
        inside[i]  = solid[i] ? big  : 0.0f;
    }
    chamfer(outside, n, n);
    chamfer(inside, n, n);

    QImage img(n, n, QImage::Format_Grayscale8);
    const float range = std::max(1.0f, rangeCells);
    for (int y = 0; y < n; ++y) {
        uchar *row = img.scanLine(y);
        for (int x = 0; x < n; ++x) {
            const int i = y * n + x;
            // Half a cell of bias so the surface sits between the last
            // solid cell and the first empty one rather than on top of
            // the solid one — without it the shape reads half a cell fat.
            const float sd = outside[i] - inside[i] + (solid[i] ? 0.5f : -0.5f);
            const float t = std::clamp(0.5f + sd / (2.0f * range), 0.0f, 1.0f);
            row[x] = uchar(std::lround(t * 255.0f));
        }
    }
    return img;
}

} // namespace hyperbin
