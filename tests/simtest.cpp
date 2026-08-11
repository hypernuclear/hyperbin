// Headless sanity check for the swarm: does it populate, move, stay
// near the bin, and fully disperse when the bin is emptied?
#include "FlySim.h"

#include <QRectF>
#include <cmath>
#include <cstdio>

using namespace hyperbin;

int main()
{
    FlySim s(12345);
    s.setBinRect(QRectF(78, 78, 64, 64));

    s.setFullness(1.0f);
    for (int i = 0; i < 200; ++i)
        s.step(0.05f);

    const int n = s.flies().size();
    std::printf("full: %d flies after 10s\n", n);
    if (n < 8) { std::printf("FAIL: swarm too small\n"); return 1; }

    double sp = 0;
    QRectF bb;
    for (const Fly &f : s.flies()) {
        sp += std::hypot(f.vel.x(), f.vel.y());
        const QRectF r(f.pos, QSizeF(1, 1));
        bb = bb.isNull() ? r : bb.united(r);
    }
    sp /= n;
    std::printf("mean speed %.1f px/s | bbox %.0fx%.0f at (%.0f,%.0f)\n",
                sp, bb.width(), bb.height(), bb.center().x(), bb.center().y());
    if (sp < 5.0) { std::printf("FAIL: flies are not moving\n"); return 1; }
    if (bb.width() > 300 || bb.height() > 300) {
        std::printf("FAIL: swarm dispersed, should orbit the bin\n"); return 1;
    }

    // Determinism: same seed must reproduce the same flight path.
    FlySim s2(12345);
    s2.setBinRect(QRectF(78, 78, 64, 64));
    s2.setFullness(1.0f);
    for (int i = 0; i < 200; ++i)
        s2.step(0.05f);
    if (s2.flies().size() != n || s2.flies()[0].pos != s.flies()[0].pos) {
        std::printf("FAIL: not deterministic for a fixed seed\n"); return 1;
    }
    std::printf("deterministic: same seed reproduces the swarm\n");

    // The swarm must scale with the icon: Dock magnification grows the
    // tile from 26pt to 95pt here, and a fixed orbit radius would leave
    // the flies buried inside a magnified bin.
    auto meanOrbit = [](qreal side) {
        FlySim s(999);
        s.setBinRect(QRectF(200, 200, side, side));
        s.setFullness(1.0f);
        for (int i = 0; i < 300; ++i)
            s.step(0.05f);
        const QPointF c(200 + side / 2, 200 + side / 2);
        double sum = 0;
        for (const Fly &f : s.flies())
            sum += std::hypot(f.pos.x() - c.x(), f.pos.y() - c.y());
        return s.flies().isEmpty() ? 0.0 : sum / s.flies().size();
    };
    const double small = meanOrbit(40), big = meanOrbit(95);
    const double ratio = big / small;
    std::printf("orbit scales %.0f -> %.0f px (x%.2f for a x2.38 icon)\n",
                small, big, ratio);
    if (ratio < 1.7 || ratio > 3.1) {
        std::printf("FAIL: swarm does not scale with the icon\n"); return 1;
    }


    // How far out does the swarm actually reach, per icon size? The
    // overlay margin is derived from this; too small and flies clip at
    // the window edge.
    for (qreal side : {28.0, 40.0, 95.0}) {
        FlySim s(4242);
        s.setBinRect(QRectF(500, 500, side, side));
        s.setFullness(1.0f);
        double worst = 0;
        for (int i = 0; i < 600; ++i) {
            s.step(0.05f);
            const QPointF c(500 + side / 2, 500 + side / 2);
            for (const Fly &f : s.flies())
                worst = std::max(worst, std::hypot(f.pos.x() - c.x(), f.pos.y() - c.y()));
        }
        const int margin = FlySim::recommendedMargin(side);
        std::printf("icon %3.0fpx -> reach %.0fpx, margin %d %s\n",
                    side, worst, margin, worst <= margin ? "ok" : "CLIPS");
        if (worst > margin) {
            std::printf("FAIL: swarm clips at the overlay edge\n"); return 1;
        }
    }

    // Emptying the bin must drive it to fully idle, or we never stop
    // rendering and the power budget is blown.
    s.setFullness(0.0f);
    int steps = 0;
    while (!s.isIdle() && steps < 4000) { s.step(0.05f); ++steps; }
    std::printf("emptied: idle after %.1fs (%lld left)\n",
                steps * 0.05, (long long)s.flies().size());
    if (!s.isIdle()) { std::printf("FAIL: swarm never went idle\n"); return 1; }

    std::printf("PASS\n");
    return 0;
}
