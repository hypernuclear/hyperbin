// Headless checks for the swarm's contract: how many flies, how long
// they live, where they're allowed to go, and that it all stops.
#include "FlySim.h"

#include <QRectF>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace hyperbin;

namespace {

int fail(const char *msg)
{
    std::printf("FAIL: %s\n", msg);
    return 1;
}

FlySim makeSim(qreal side, float fullness, uint32_t seed = 12345)
{
    FlySim s(seed);
    s.setBinRect(QRectF(500, 500, side, side * 0.7)); // Dock tiles are wide
    s.setFullness(fullness);
    return s;
}

} // namespace

int main()
{
    // --- count: never more than 6, never fewer than 1 while occupied ---
    {
        FlySim s = makeSim(40, 1.0f);
        int seen = 0, minSeen = 99;
        for (int i = 0; i < 1200; ++i) { // 60s
            s.step(0.05f);
            seen = std::max(seen, int(s.flies().size()));
            if (i > 40) // let the first one arrive
                minSeen = std::min(minSeen, int(s.flies().size()));
        }
        std::printf("full bin over 60s: %d..%d flies\n", minSeen, seen);
        if (seen > 6) return fail("more than 6 flies on screen");
        if (minSeen < 1) return fail("bin occupied but no flies");
    }

    // A nearly-empty bin still gets its lone fly.
    {
        FlySim s = makeSim(40, 0.03f);
        int minSeen = 99, maxSeen = 0;
        for (int i = 0; i < 600; ++i) {
            s.step(0.05f);
            if (i > 40) {
                minSeen = std::min(minSeen, int(s.flies().size()));
                maxSeen = std::max(maxSeen, int(s.flies().size()));
            }
        }
        std::printf("nearly-empty bin: %d..%d flies\n", minSeen, maxSeen);
        if (minSeen < 1) return fail("one item in the bin should still draw a fly");
    }

    // --- turnover: flies come and go rather than persisting forever ---
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 100; ++i) s.step(0.05f);
        int departures = 0, wasLeaving = 0;
        for (int i = 0; i < 600; ++i) { // 30s
            s.step(0.05f);
            int leaving = 0;
            for (const Fly &f : s.flies())
                if (f.leaving) ++leaving;
            if (leaving > wasLeaving) departures += leaving - wasLeaving;
            wasLeaving = leaving;
        }
        // Lifetimes are 2-6s, so 30s across up to 6 slots should retire a
        // good number of flies.
        std::printf("turnover: %d departures in 30s\n", departures);
        if (departures < 10) return fail("flies are not being replaced");
    }

    // --- movement: both modes actually occur ---
    {
        FlySim s = makeSim(40, 1.0f);
        int crawlSamples = 0, flySamples = 0;
        for (int i = 0; i < 800; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies())
                (f.mode == FlyMode::Crawling ? crawlSamples : flySamples)++;
        }
        const double crawlShare =
            double(crawlSamples) / std::max(1, crawlSamples + flySamples);
        std::printf("mode mix: %.0f%% crawling, %.0f%% flying\n",
                    crawlShare * 100, (1 - crawlShare) * 100);
        if (crawlShare < 0.15 || crawlShare > 0.85)
            return fail("one movement mode dominates; the mix looks wrong");
    }

    // --- containment: must fit the asymmetric overlay margins ---
    for (qreal side : {28.0, 40.0, 95.0}) {
        FlySim s = makeSim(side, 1.0f, 4242);
        const QPointF c(500 + side / 2, 500 + side * 0.35);
        double worstX = 0, worstUp = 0, worstDown = 0;
        for (int i = 0; i < 1200; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                worstX = std::max(worstX, std::abs(f.pos.x() - c.x()));
                const double dy = f.pos.y() - c.y();
                if (dy < 0) worstUp = std::max(worstUp, -dy);
                else        worstDown = std::max(worstDown, dy);
            }
        }
        const int mx = FlySim::marginX(side);
        const int mt = FlySim::marginTop(side);
        const int mb = FlySim::marginBottom(side);
        const bool ok = worstX <= mx && worstUp <= mt && worstDown <= mb;
        std::printf("icon %3.0fpx: x %.0f/%d  up %.0f/%d  down %.0f/%d %s\n",
                    side, worstX, mx, worstUp, mt, worstDown, mb,
                    ok ? "ok" : "CLIPS");
        if (!ok) return fail("swarm escapes the overlay margins");
    }

    // --- scaling: the swarm grows with a magnified Dock tile ---
    {
        auto spread = [](qreal side) {
            FlySim s = makeSim(side, 1.0f, 999);
            const QPointF c(500 + side / 2, 500 + side * 0.35);
            double sum = 0; int n = 0;
            for (int i = 0; i < 600; ++i) {
                s.step(0.05f);
                for (const Fly &f : s.flies()) {
                    sum += std::hypot(f.pos.x() - c.x(), f.pos.y() - c.y());
                    ++n;
                }
            }
            return n ? sum / n : 0.0;
        };
        const double ratio = spread(95) / spread(40);
        std::printf("spread scales x%.2f for a x2.38 icon\n", ratio);
        if (ratio < 1.7 || ratio > 3.1)
            return fail("swarm does not scale with the icon");
    }

    // --- determinism: same seed, same flight path ---
    {
        FlySim a = makeSim(40, 1.0f, 777), b = makeSim(40, 1.0f, 777);
        for (int i = 0; i < 200; ++i) { a.step(0.05f); b.step(0.05f); }
        if (a.flies().size() != b.flies().size()
            || (!a.flies().isEmpty() && a.flies()[0].pos != b.flies()[0].pos))
            return fail("not deterministic for a fixed seed");
        std::printf("deterministic: same seed reproduces the swarm\n");
    }

    // --- idle: an emptied bin must stop rendering entirely ---
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 200; ++i) s.step(0.05f);
        s.setFullness(0.0f);
        int steps = 0;
        while (!s.isIdle() && steps < 4000) { s.step(0.05f); ++steps; }
        std::printf("emptied: idle after %.1fs (%lld left)\n",
                    steps * 0.05, (long long)s.flies().size());
        if (!s.isIdle()) return fail("swarm never went idle");
    }

    std::printf("PASS\n");
    return 0;
}
