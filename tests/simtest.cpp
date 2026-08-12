// Headless checks for the swarm's contract: how many flies, how long
// they live, where they're allowed to go, and that it all stops.
#include "FlySim.h"
#include "Settings.h"

#include <QRectF>
#include <algorithm>
#include <map>
#include <vector>
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


namespace {

/// Coverage grid shaped like the real trash artwork: it fills only the
/// middle ~54% x 78% of its tile, which is the whole reason the sim needs
/// to know about it.
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
        // Landing is meant to dominate now: the intended read is flies
        // that live ON the bin and take off occasionally, not flies that
        // circle it and touch down rarely. Flight still has to happen —
        // every visit begins and ends with one.
        if (crawlShare < 0.55 || crawlShare > 0.90)
            return fail("movement mix is wrong; landing should dominate "
                        "but flying must still occur");
    }

    // --- flies fade in and out clear of the bin ----------------------
    // A fly winking out over the icon reads as it ceasing to exist; the
    // bin behind it is a fixed reference the eye anchors on.
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f, 31337);
        int over = 0, lv = 0, ar = 0, tot = 0;
        for (int i = 0; i < 2400; ++i) { // 120s
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                ++tot;
                if (f.fade < 0.85f && bin.contains(f.pos))
                    { ++over; (f.leaving ? lv : ar)++; }
            }
        }
        const double share = double(over) / std::max(1, tot);
        std::printf("mid-fade over the bin: %.1f%% of samples (leaving %d, arriving %d)\n",
                    share * 100, lv, ar);
        // Not zero, and shouldn't be forced to zero: `bin` is the Dock's
        // square tile, which is larger than the drawn bin, so a fly
        // clipping its corner is usually over empty pixels. What this
        // guards is the regression where flies routinely materialise and
        // wink out against the icon.
        if (share > 0.02) return fail("flies appear or vanish on top of the bin");
    }
    // --- no stalled fliers -------------------------------------------
    // A fly whose velocity collapses spins on the spot: the sprite's
    // heading comes from the velocity vector, so at near-zero speed tiny
    // changes swing it wildly. The cause has always been two steering
    // terms cancelling — most recently the outward push for fading flies
    // against the containment shove.
    {
        FlySim s = makeSim(40, 1.0f, 8191);
        int stalled = 0, flying = 0;
        for (int i = 0; i < 2400; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                if (f.mode != FlyMode::Flying || f.leaving) continue;
                ++flying;
                if (std::hypot(f.vel.x(), f.vel.y()) < 0.15 * 52.0) ++stalled;
            }
        }
        const double share = double(stalled) / std::max(1, flying);
        std::printf("stalled fliers: %.1f%% of flying samples\n", share * 100);
        if (share > 0.08) return fail("fliers stall and spin on the spot");
    }
    // --- nothing passes under the bin --------------------------------
    // The bin rests on the floor of the Dock. A fly drawn behind it, or
    // crawling on it, must never be below its bottom edge — there is no
    // space there to move through, so the silhouette can only be entered
    // or left at the left, top or right.
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f, 2027);
        int under = 0;
        for (int i = 0; i < 2400; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                if (f.inFront && !f.onSurface) continue;   // in front: fine
                if (std::abs(f.pos.x() - bin.center().x()) >= bin.width() * 0.5)
                    continue;                              // clear of the icon
                if (f.pos.y() > bin.bottom() + 0.5) ++under;
            }
        }
        std::printf("occluded flies under the bin: %d\n", under);
        if (under > 0) return fail("flies pass under the bin, through the Dock floor");
    }
    // --- passes across the FRONT of the bin --------------------------
    // Depth is only re-decided while a fly is clear of the icon and only
    // for flies heading toward it, so it is easy for this to collapse
    // without anyone noticing in the params.
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f, 5150);
        int over = 0, inFront = 0;
        for (int i = 0; i < 2400; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                if (f.onSurface || !bin.contains(f.pos)) continue;
                ++over;
                if (f.inFront) ++inFront;
            }
        }
        const double share = double(inFront) / std::max(1, over);
        std::printf("flights across the bin: %.0f%% in front\n", share * 100);
        // A floor, not a target: the front/behind balance is a taste
        // setting (params.frontShare), and this only catches it
        // collapsing to nothing.
        if (share < 0.12) return fail("almost nothing flies in front of the bin");
    }
    // --- pointer on the bin clears the swarm -------------------------
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f);
        for (int i = 0; i < 200; ++i) s.step(0.05f);
        if (s.flies().isEmpty()) return fail("no swarm to clear");
        s.setCursor(bin.center(), true);
        int steps = 0;
        while (!s.flies().isEmpty() && steps < 400) { s.step(0.05f); ++steps; }
        std::printf("pointer on bin: cleared in %.1fs\n", steps * 0.05);
        if (!s.flies().isEmpty()) return fail("swarm does not clear for the pointer");
        // They must SCATTER first and fade second. Clearing almost
        // instantly means they dissolved where they stood, which reads as
        // the pointer killing flies rather than scaring them off.
        if (steps * 0.05 < 0.5) return fail("flies fade out instead of scattering");
        // ...and stays away while it's there.
        for (int i = 0; i < 400; ++i) s.step(0.05f);
        if (!s.flies().isEmpty()) return fail("flies respawn under the pointer");
        // ...and comes back once it leaves.
        s.setCursor(QPointF(-9999, -9999), false);
        steps = 0;
        while (s.flies().isEmpty() && steps < 400) { s.step(0.05f); ++steps; }
        std::printf("pointer away: swarm back in %.1fs\n", steps * 0.05);
        if (s.flies().isEmpty()) return fail("swarm never returns");
    }
    // --- a pointer NEAR the bin doesn't stop flies landing ------------
    // The scatter radius was once ~2 icon widths, which meant a pointer
    // resting anywhere near the Dock held the whole swarm off the bin and
    // crawling stopped entirely. Only a pointer ON the bin clears it.
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f, 6060);
        s.setCursor(QPointF(bin.center().x() + side * 1.1, bin.center().y()), true);
        int crawl = 0, total = 0;
        for (int i = 0; i < 1600; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                ++total;
                if (f.mode == FlyMode::Crawling) ++crawl;
            }
        }
        const double share = double(crawl) / std::max(1, total);
        std::printf("pointer beside the bin: %.0f%% still crawling\n", share * 100);
        if (share < 0.10) return fail("a pointer near the bin stops flies landing");
    }
    // --- startle is a burst, not a death ------------------------------
    // The bolt used to clamp the fly's remaining life, so the sequence
    // played as pause-then-vanish instead of pause-then-fly-away.
    {
        FlySim s = makeSim(40, 1.0f, 4711);
        s.params.startleChance = 0.25f;   // startle constantly
        int frozen = 0, boltingSeen = 0;
        double lifeAfterBolt = 0;
        for (int i = 0; i < 1200; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                if (f.freezeLeft > 0.0f) ++frozen;
                if (f.bolting) {
                    ++boltingSeen;
                    lifeAfterBolt = std::max(lifeAfterBolt, double(f.life));
                }
            }
        }
        std::printf("startle: %d frozen samples, %d bolting, max life left %.1fs\n",
                    frozen, boltingSeen, lifeAfterBolt);
        if (frozen == 0) return fail("flies never freeze");
        if (boltingSeen == 0) return fail("flies freeze but never bolt");
        // A bolting fly must still have a normal amount of life ahead of
        // it — that is the difference between flying off and blinking out.
        if (lifeAfterBolt < 1.0) return fail("bolting flies are killed off immediately");
    }
    // --- landing sequence --------------------------------------------
    // The intended read is: fly in, touch down, sit still, walk a bit,
    // stop again, leave. The stillness either side of the walk is the
    // part that makes it look deliberate, so check it actually happens.
    {
        FlySim s = makeSim(40, 1.0f, 1234);
        int still = 0, crawlSamples = 0;
        bool sawSettle = false, sawWalk = false, sawPreflight = false;
        for (int i = 0; i < 2400; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                if (f.mode != FlyMode::Crawling) continue;
                ++crawlSamples;
                if (std::hypot(f.vel.x(), f.vel.y()) < 0.01) ++still;
                if (f.crawlStage == 0 && f.pauseLeft > 0) sawSettle = true;
                if (f.crawlStage == 1) sawWalk = true;
                if (f.crawlStage == 2 && f.pauseLeft > 0) sawPreflight = true;
            }
        }
        const double stillShare = double(still) / std::max(1, crawlSamples);
        std::printf("landed flies: %.0f%% of crawl time held still\n",
                    stillShare * 100);
        if (!sawSettle)    return fail("flies never settle after landing");
        if (!sawWalk)      return fail("flies land but never walk");
        if (!sawPreflight) return fail("flies never pause before taking off");
        if (stillShare < 0.15)
            return fail("landed flies barely stop moving");
    }

    // --- landed flies are on the ARTWORK, not just inside its box -----
    // The sim used to treat the bin's bounding rect as landable while the
    // renderer clipped crawling flies to the artwork's alpha. The artwork
    // covers under half its tile, so most "landed" flies were invisible:
    // a busy sim and an empty screen.
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f, 24680);
        s.setSurface(binSilhouette(24), 24, 24);

        int landed = 0, landedOffArt = 0;
        int bareFrames = 0, frames = 0;
        for (int i = 0; i < 2400; ++i) {
            s.step(0.05f);
            ++frames;
            int onBin = 0;
            for (const Fly &f : s.flies()) {
                if (f.mode != FlyMode::Crawling || f.leaving) continue;
                ++landed;
                ++onBin;
                if (!s.onSurfaceAt(f.pos)) ++landedOffArt;
            }
            if (onBin == 0) ++bareFrames;
        }
        const double offShare = double(landedOffArt) / std::max(1, landed);
        const double bareShare = double(bareFrames) / frames;
        std::printf("landed off the artwork: %.1f%%; bin bare %.1f%% of frames\n",
                    offShare * 100, bareShare * 100);
        if (offShare > 0.05)
            return fail("flies land where the renderer will clip them away");
        // ...and the bin should essentially always have someone on it.
        if (bareShare > 0.12)
            return fail("the bin is left empty too often");
    }

    // --- walking covers ground ---------------------------------------
    // A crawler used to take its heading from the noise field sampled at
    // its own position, which changed as fast as it moved — so it pivoted
    // on the spot rather than walking anywhere.
    {
        const qreal side = 40.0;
        FlySim s = makeSim(side, 1.0f, 13579);
        s.setSurface(binSilhouette(24), 24, 24);

        // Follow individual flies by id (the vector index is not stable —
        // flies are removed from the middle) and score each continuous
        // walking bout by how much of the distance walked was progress.
        struct Bout { QPointF start, prev; double path = 0, net = 0; };
        std::map<quint32, Bout> bouts;
        std::vector<double> scores;
        for (int i = 0; i < 1600; ++i) {
            s.step(0.05f);
            std::map<quint32, bool> alive;
            for (const Fly &f : s.flies()) {
                const bool walking = f.mode == FlyMode::Crawling
                                     && f.crawlStage == 1 && f.pauseLeft <= 0.0f
                                     && !f.leaving;
                alive[f.id] = walking;
                auto it = bouts.find(f.id);
                if (!walking) {
                    if (it != bouts.end()) {
                        if (it->second.path > 4.0)
                            scores.push_back(it->second.net / it->second.path);
                        bouts.erase(it);
                    }
                    continue;
                }
                if (it == bouts.end()) {
                    Bout b; b.start = f.pos; b.prev = f.pos;
                    bouts[f.id] = b;
                    continue;
                }
                Bout &b = it->second;
                b.path += std::hypot(f.pos.x() - b.prev.x(), f.pos.y() - b.prev.y());
                b.prev = f.pos;
                b.net = std::max(b.net,
                                 std::hypot(f.pos.x() - b.start.x(),
                                            f.pos.y() - b.start.y()));
            }
            for (auto it = bouts.begin(); it != bouts.end();)
                it = alive.count(it->first) ? std::next(it) : bouts.erase(it);
        }
        double sum = 0;
        for (double v : scores) sum += v;
        const double straightness = scores.empty() ? 0.0 : sum / scores.size();
        std::printf("walking: %zu bouts, %.0f%% of distance walked was progress\n",
                    scores.size(), straightness * 100);
        if (scores.size() < 5)
            return fail("flies hardly ever walk");
        // A fly shuffling in place scores near zero however fast its legs
        // move; one walking a line scores near 1.
        if (straightness < 0.35)
            return fail("crawlers shuffle on the spot instead of walking");
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

    // --- density settings ---------------------------------------------
    // The three fixed steps are thirds by design, so they line up with
    // the relative mode's scale instead of being arbitrary numbers.
    {
        // Throwaway store: never the user's real preferences.
        Settings s(nullptr, QStringLiteral("hyperbin-selftest"));
        const qint64 mb = 1024 * 1024;

        s.setDensity(Settings::Density::Few);
        if (std::abs(s.fullnessFor(0, 0) - 1.0 / 3.0) > 1e-9)
            return fail("'a few flies' is not a third");
        s.setDensity(Settings::Density::Lots);
        if (std::abs(s.fullnessFor(0, 0) - 2.0 / 3.0) > 1e-9)
            return fail("'lots of flies' is not two thirds");
        s.setDensity(Settings::Density::TooMany);
        if (std::abs(s.fullnessFor(0, 0) - 1.0) > 1e-9)
            return fail("'too many flies' is not full");
        // Fixed densities must ignore the bin entirely — that is the
        // whole reason to offer them.
        if (s.fullnessFor(0, 0) != s.fullnessFor(50 * mb, 12))
            return fail("a fixed density still looks at the trash");

        s.setDensity(Settings::Density::Relative);
        s.setThreshold(Settings::Threshold::HundredMB);
        if (s.fullnessFor(0, 0) != 0.0)
            return fail("an empty bin should draw nothing");
        if (std::abs(s.fullnessFor(50 * mb, 3) - 0.5) > 1e-9)
            return fail("half the threshold should be half full");
        if (std::abs(s.fullnessFor(500 * mb, 3) - 1.0) > 1e-9)
            return fail("past the threshold should saturate, not overflow");
        s.setThreshold(Settings::Threshold::OneGB);
        if (std::abs(s.fullnessFor(50 * mb, 3) - 50.0 / 1024.0) > 1e-9)
            return fail("threshold change does not rescale");
        // No size available (no Full Disk Access) must fall back to the
        // count rather than reporting an empty bin.
        if (s.fullnessFor(-1, 20) <= 0.0)
            return fail("unreadable trash size should fall back to the count");
        s.clearStore();
        std::printf("density: thirds, relative scaling and fallback all hold\n");
    }

    std::printf("PASS\n");
    return 0;
}
