// The swarm's contract: how many flies, how long they live, where they are
// allowed to go, and that it all stops.
//
// Every check here runs the real simulation for tens of simulated seconds
// and asserts on a STATISTIC of the result rather than on any one frame.
// That is deliberate. The sim is stochastic, so a single-frame assertion
// either fixes a seed and tests one arbitrary path, or flakes; a share over
// thousands of samples is stable and still fails loudly when the behaviour
// it describes collapses.
//
// Several thresholds below are FLOORS AND CEILINGS, not targets, and say so
// where that matters. Narrowing one to whatever the current tuning happens
// to produce turns a guard against collapse into a tripwire that fires on
// every deliberate change.
#include <QtTest>

#include "FlySim.h"
#include "Settings.h"
#include "test_helpers.h"

#include <QRectF>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using namespace hyperbin;
using hyperbin::test::binSilhouette;
using hyperbin::test::makeSim;

class TstFlySim : public QObject
{
    Q_OBJECT

private slots:
    // --- count: never more than 6, never fewer than 1 while occupied ---
    void count_fullBin_staysWithinBounds()
    {
        FlySim s = makeSim(40, 1.0f);
        int seen = 0, minSeen = 99;
        for (int i = 0; i < 1200; ++i) { // 60s
            s.step(0.05f);
            seen = std::max(seen, int(s.flies().size()));
            if (i > 40) // let the first one arrive
                minSeen = std::min(minSeen, int(s.flies().size()));
        }
        qInfo("full bin over 60s: %d..%d flies", minSeen, seen);
        QVERIFY2(seen <= 6, "more than 6 flies on screen");
        QVERIFY2(minSeen >= 1, "bin occupied but no flies");
    }

    // A nearly-empty bin still gets its lone fly.
    void count_nearlyEmptyBin_stillDrawsOne()
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
        qInfo("nearly-empty bin: %d..%d flies", minSeen, maxSeen);
        QVERIFY2(minSeen >= 1, "one item in the bin should still draw a fly");
    }

    // There is no lifespan. Rubbish does not stop attracting flies after
    // a few seconds, and the constant churn of arrivals and departures
    // was motion the eye kept getting drawn to for no reason.
    void lifetime_fliesPersist()
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 200; ++i) s.step(0.05f);   // let it fill up
        std::set<quint32> early;
        for (const Fly &f : s.flies()) early.insert(f.id);
        QVERIFY2(!early.empty(), "no swarm to follow");

        for (int i = 0; i < 1200; ++i) s.step(0.05f);  // 60 more seconds
        int survivors = 0;
        for (const Fly &f : s.flies())
            if (early.count(f.id)) ++survivors;
        qInfo("persistence: %d of %zu flies still around after 60s",
              survivors, early.size());
        QCOMPARE(survivors, int(early.size()));
    }

    // With nothing expiring, this is now the ONLY thing that removes a
    // fly short of the pointer arriving, so it has to work.
    void count_lessRubbish_shrinksTheSwarm()
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 300; ++i) s.step(0.05f);
        const int before = int(s.flies().size());
        s.setFullness(0.15f);
        for (int i = 0; i < 600; ++i) s.step(0.05f);
        const int after = int(s.flies().size());
        qInfo("less rubbish: swarm %d -> %d", before, after);
        QVERIFY2(after < before, "swarm does not shrink with the bin");
        QVERIFY2(after >= 1, "swarm shrank past the floor");
    }

    void movement_bothModesOccur()
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
        qInfo("mode mix: %.0f%% crawling, %.0f%% flying",
              crawlShare * 100, (1 - crawlShare) * 100);
        // Both modes must be a real part of the picture. The exact split
        // is a taste setting spread across landChance, the mode durations
        // and preferLanded, and it is tuned by eye — this is a guard
        // against either mode collapsing, NOT a target. Do not narrow it
        // to whatever the current tuning happens to produce.
        QVERIFY2(crawlShare >= 0.30 && crawlShare <= 0.90,
                 "movement mix has collapsed; flies should both land and fly");
    }

    // A fly winking out over the icon reads as it ceasing to exist; the
    // bin behind it is a fixed reference the eye anchors on.
    void fade_happensClearOfTheBin()
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
        qInfo("mid-fade over the bin: %.1f%% of samples (leaving %d, arriving %d)",
              share * 100, lv, ar);
        // Not zero, and shouldn't be forced to zero: `bin` is the Dock's
        // square tile, which is larger than the drawn bin, so a fly
        // clipping its corner is usually over empty pixels. What this
        // guards is the regression where flies routinely materialise and
        // wink out against the icon.
        QVERIFY2(share <= 0.02, "flies appear or vanish on top of the bin");
    }

    // A fly whose velocity collapses spins on the spot: the sprite's
    // heading comes from the velocity vector, so at near-zero speed tiny
    // changes swing it wildly. The cause has always been two steering
    // terms cancelling — most recently the outward push for fading flies
    // against the containment shove.
    void movement_fliersDoNotStall()
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
        qInfo("stalled fliers: %.1f%% of flying samples", share * 100);
        QVERIFY2(share <= 0.08, "fliers stall and spin on the spot");
    }

    // The bin rests on the floor of the Dock. A fly drawn behind it, or
    // crawling on it, must never be below its bottom edge — there is no
    // space there to move through, so the silhouette can only be entered
    // or left at the left, top or right.
    void depth_nothingPassesUnderTheBin()
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
        qInfo("occluded flies under the bin: %d", under);
        QCOMPARE(under, 0);
    }

    // Depth is only re-decided while a fly is clear of the icon and only
    // for flies heading toward it, so it is easy for this to collapse
    // without anyone noticing in the params.
    void depth_someFlightsPassInFront()
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
        qInfo("flights across the bin: %.0f%% in front", share * 100);
        // A floor, not a target: the front/behind balance is a taste
        // setting (params.frontShare), and this only catches it
        // collapsing to nothing.
        QVERIFY2(share >= 0.12, "almost nothing flies in front of the bin");
    }

    void cursor_onTheBinClearsAndRestoresTheSwarm()
    {
        const qreal side = 40.0;
        const QRectF bin(500, 500, side, side * 0.7);
        FlySim s = makeSim(side, 1.0f);
        for (int i = 0; i < 200; ++i) s.step(0.05f);
        QVERIFY2(!s.flies().isEmpty(), "no swarm to clear");
        s.setCursor(bin.center(), true);
        int steps = 0;
        while (!s.flies().isEmpty() && steps < 400) { s.step(0.05f); ++steps; }
        qInfo("pointer on bin: cleared in %.1fs", steps * 0.05);
        QVERIFY2(s.flies().isEmpty(), "swarm does not clear for the pointer");
        // They must SCATTER first and fade second. Clearing almost
        // instantly means they dissolved where they stood, which reads as
        // the pointer killing flies rather than scaring them off.
        QVERIFY2(steps * 0.05 >= 0.5, "flies fade out instead of scattering");
        // ...and stays away while it's there.
        for (int i = 0; i < 400; ++i) s.step(0.05f);
        QVERIFY2(s.flies().isEmpty(), "flies respawn under the pointer");
        // ...and comes back once it leaves.
        s.setCursor(QPointF(-9999, -9999), false);
        steps = 0;
        while (s.flies().isEmpty() && steps < 400) { s.step(0.05f); ++steps; }
        qInfo("pointer away: swarm back in %.1fs", steps * 0.05);
        QVERIFY2(!s.flies().isEmpty(), "swarm never returns");
    }

    // The scatter radius was once ~2 icon widths, which meant a pointer
    // resting anywhere near the Dock held the whole swarm off the bin and
    // crawling stopped entirely. Only a pointer ON the bin clears it.
    void cursor_besideTheBinStillAllowsLanding()
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
        qInfo("pointer beside the bin: %.0f%% still crawling", share * 100);
        QVERIFY2(share >= 0.10, "a pointer near the bin stops flies landing");
    }

    // The bolt used to clamp the fly's remaining life, so the sequence
    // played as pause-then-vanish instead of pause-then-fly-away.
    void startle_isABurstNotADeath()
    {
        FlySim s = makeSim(40, 1.0f, 4711);
        s.params.startleChance = 0.25f;   // startle constantly
        int frozen = 0, boltingSeen = 0, boltRetiring = 0;
        for (int i = 0; i < 1200; ++i) {
            s.step(0.05f);
            for (const Fly &f : s.flies()) {
                if (f.freezeLeft > 0.0f) ++frozen;
                if (f.bolting) {
                    ++boltingSeen;
                    if (f.retiring || f.leaving) ++boltRetiring;
                }
            }
        }
        qInfo("startle: %d frozen samples, %d bolting, %d of those leaving",
              frozen, boltingSeen, boltRetiring);
        QVERIFY2(frozen > 0, "flies never freeze");
        QVERIFY2(boltingSeen > 0, "flies freeze but never bolt");
        // A startle is a burst, not a death: the fly rejoins the swarm
        // afterwards. Flies have no lifespan at all now, so any bolting
        // fly that is also leaving was retired for an unrelated reason.
        QVERIFY2(boltRetiring <= boltingSeen / 2,
                 "bolting flies are being retired immediately");
    }

    // The intended read is: fly in, touch down, sit still, walk a bit,
    // stop again, leave. The stillness either side of the walk is the
    // part that makes it look deliberate, so check it actually happens.
    void landing_playsTheWholeSequence()
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
        qInfo("landed flies: %.0f%% of crawl time held still", stillShare * 100);
        QVERIFY2(sawSettle, "flies never settle after landing");
        QVERIFY2(sawWalk, "flies land but never walk");
        QVERIFY2(sawPreflight, "flies never pause before taking off");
        QVERIFY2(stillShare >= 0.15, "landed flies barely stop moving");
    }

    // The sim used to treat the bin's bounding rect as landable while the
    // renderer clipped crawling flies to the artwork's alpha. The artwork
    // covers under half its tile, so most "landed" flies were invisible:
    // a busy sim and an empty screen.
    void landing_happensOnTheArtworkNotTheBoundingBox()
    {
        const qreal side = 40.0;
        FlySim s = makeSim(side, 1.0f, 24680);
        s.setSurface(binSilhouette(24), 24, 24);

        int landed = 0, landedOffArt = 0;
        int bareFrames = 0, frames = 0;
        int clippedOnArt = 0, unclippedOffArt = 0;
        for (int i = 0; i < 2400; ++i) {
            s.step(0.05f);
            ++frames;
            int onBin = 0;
            for (const Fly &f : s.flies()) {
                if (f.mode != FlyMode::Crawling || f.leaving) continue;
                ++landed;
                ++onBin;
                const bool onArt = s.onSurfaceAt(f.pos);
                if (!onArt) ++landedOffArt;
                // The clip flag has to mean exactly one thing: this fly's
                // centre has left the silhouette. Anything looser and the
                // renderer goes back to slicing wings off flies that are
                // plainly standing on the bin.
                if (f.clipToBin && onArt) ++clippedOnArt;
                if (!f.clipToBin && !onArt) ++unclippedOffArt;
            }
            if (onBin == 0) ++bareFrames;
        }
        QVERIFY2(clippedOnArt == 0 && unclippedOffArt == 0,
                 "clipToBin does not track the fly leaving the silhouette");
        const double offShare = double(landedOffArt) / std::max(1, landed);
        const double bareShare = double(bareFrames) / frames;
        qInfo("landed off the artwork: %.1f%%; bin bare %.1f%% of frames",
              offShare * 100, bareShare * 100);
        QVERIFY2(offShare <= 0.05,
                 "flies land where the renderer will clip them away");
        // ...and the bin should essentially always have someone on it.
        QVERIFY2(bareShare <= 0.12, "the bin is left empty too often");
    }

    // Landing used to check only "am I over the bin", not "am I in
    // front". A fly that arrived behind it is masked out completely, so
    // landing there flipped it from the hidden batch to the clipped-to-
    // surface batch and it appeared out of nowhere on the front.
    void landing_neverBehindTheBinAndNothingPopsIn()
    {
        const qreal side = 40.0;
        FlySim s = makeSim(side, 1.0f, 97531);
        s.setSurface(binSilhouette(24), 24, 24);

        std::map<quint32, bool> wasHidden;   // masked out last frame?
        int landedBehind = 0, pops = 0;
        for (int i = 0; i < 3000; ++i) {
            s.step(0.05f);
            std::map<quint32, bool> now;
            for (const Fly &f : s.flies()) {
                if (f.mode == FlyMode::Crawling && !f.inFront && !f.leaving)
                    ++landedBehind;
                // A fly is invisible when it is behind the bin and over
                // it: the mask erases it entirely.
                const bool hidden = !f.inFront && !f.onSurface
                                    && s.onSurfaceAt(f.pos);
                const bool visible = !hidden && f.fade > 0.5f;
                auto it = wasHidden.find(f.id);
                if (it != wasHidden.end() && it->second && visible
                    && s.onSurfaceAt(f.pos))
                    ++pops;   // went from hidden to visible ON the bin
                now[f.id] = hidden;
            }
            wasHidden.swap(now);
        }
        qInfo("landed while behind: %d; appeared on the bin: %d",
              landedBehind, pops);
        QVERIFY2(landedBehind == 0,
                 "flies land behind the bin, where they cannot be seen");
        QVERIFY2(pops == 0, "flies pop into view on top of the bin");
    }

    // A crawler used to take its heading from the noise field sampled at
    // its own position, which changed as fast as it moved — so it pivoted
    // on the spot rather than walking anywhere.
    void walking_coversGround()
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
        qInfo("walking: %zu bouts, %.0f%% of distance walked was progress",
              scores.size(), straightness * 100);
        QVERIFY2(scores.size() >= 5, "flies hardly ever walk");
        // A fly shuffling in place scores near zero however fast its legs
        // move; one walking a line scores near 1.
        QVERIFY2(straightness >= 0.35,
                 "crawlers shuffle on the spot instead of walking");
    }

    // --- containment: must fit the asymmetric overlay margins ---
    void containment_staysInsideTheOverlay_data()
    {
        QTest::addColumn<qreal>("side");
        QTest::newRow("28px") << 28.0;
        QTest::newRow("40px") << 40.0;
        QTest::newRow("95px") << 95.0;
    }

    void containment_staysInsideTheOverlay()
    {
        QFETCH(qreal, side);
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
        qInfo("icon %3.0fpx: x %.0f/%d  up %.0f/%d  down %.0f/%d",
              side, worstX, mx, worstUp, mt, worstDown, mb);
        QVERIFY2(worstX <= mx, "swarm escapes the overlay sideways");
        QVERIFY2(worstUp <= mt, "swarm escapes the overlay upward");
        QVERIFY2(worstDown <= mb, "swarm escapes the overlay downward");
    }

    // --- scaling: the swarm grows with a magnified Dock tile ---
    void scaling_swarmGrowsWithTheIcon()
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
        qInfo("spread scales x%.2f for a x2.38 icon", ratio);
        QVERIFY2(ratio >= 1.7 && ratio <= 3.1,
                 "swarm does not scale with the icon");
    }

    void determinism_sameSeedSameSwarm()
    {
        FlySim a = makeSim(40, 1.0f, 777), b = makeSim(40, 1.0f, 777);
        for (int i = 0; i < 200; ++i) { a.step(0.05f); b.step(0.05f); }
        QCOMPARE(a.flies().size(), b.flies().size());
        if (!a.flies().isEmpty())
            QCOMPARE(a.flies()[0].pos, b.flies()[0].pos);
    }

    // --- idle: an emptied bin must stop rendering entirely ---
    void idle_emptiedBinStopsRendering()
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 200; ++i) s.step(0.05f);
        s.setFullness(0.0f);
        int steps = 0;
        while (!s.isIdle() && steps < 4000) { s.step(0.05f); ++steps; }
        qInfo("emptied: idle after %.1fs (%lld left)",
              steps * 0.05, (long long)s.flies().size());
        QVERIFY2(s.isIdle(), "swarm never went idle");
    }
};

QTEST_APPLESS_MAIN(TstFlySim)
#include "tst_fly_sim.moc"
