// The trash artwork's shape, as a coverage grid.
//
// Kept in its own translation unit and NOT alongside makeSim: test_common
// is a static library, so an object file is pulled in whole. With both
// fixtures in one file the distance-field test — which wants nothing but
// this grid — dragged in makeSim and failed to link on FlySim symbols it
// never calls.
#include "test_helpers.h"

#include <cmath>

namespace hyperbin::test {

QVector<quint8> binSilhouette(int n)
{
    QVector<quint8> cov(n * n, 0);
    for (int y = 0; y < n; ++y) {
        const double v = double(y) / n;
        if (v < 0.14 || v > 0.94) continue;      // above the lid / below the base
        const double half = 0.27 - 0.04 * (v - 0.2); // slight taper
        for (int x = 0; x < n; ++x) {
            const double u = double(x) / n - 0.5;
            if (std::abs(u) <= half)
                cov[y * n + x] = 1;
        }
    }
    return cov;
}

} // namespace hyperbin::test
