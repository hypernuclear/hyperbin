// Fixtures shared between the headless suites.
//
// Both of these describe the SHAPE of the thing being simulated against —
// a Dock tile and the trash artwork inside it — which is the one piece of
// context every sim here needs and none of them owns.
#pragma once

#include "FlySim.h"

#include <QVector>
#include <QtGlobal>

namespace hyperbin::test {

/// A swarm over a Dock tile of the given side. Dock tiles are wide, so the
/// rect is deliberately not square: a sim that assumes it is escapes the
/// overlay vertically and nothing else notices.
FlySim makeSim(qreal side, float fullness, uint32_t seed = 12345);

/// Coverage grid shaped like the real trash artwork: it fills only the
/// middle ~54% x 78% of its tile, which is the whole reason the sim needs
/// to know about it. A grid of all ones passes every landing test and
/// tells you nothing.
QVector<quint8> binSilhouette(int n);

} // namespace hyperbin::test
