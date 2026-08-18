#include "test_helpers.h"

#include <QRectF>
#include <cmath>

namespace hyperbin::test {

FlySim makeSim(qreal side, float fullness, uint32_t seed)
{
    FlySim s(seed);
    s.setBinRect(QRectF(500, 500, side, side * 0.7)); // Dock tiles are wide
    s.setFullness(fullness);
    return s;
}

} // namespace hyperbin::test
