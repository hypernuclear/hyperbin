// Headless checks for the swarm's contract: how many flies, how long
// they live, where they're allowed to go, and that it all stops.
#include "FlySim.h"
#include "DistanceField.h"
#include <QProcess>
// _exit(), for the kill-the-writer half of the persistence test. POSIX
// declares it in <unistd.h>, which does not exist on MSVC; there it lives
// in <process.h>. Same function either way — the point is to leave
// without running a single destructor.
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#include "OozeSim.h"
#include "Settings.h"

#include <QRectF>
#include <algorithm>
#include <map>
#include <set>
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

int main(int argc, char **argv)
{
    // Writer half of the persistence test below: set two values and exit
    // via _exit, so nothing is flushed by a destructor. If the settings
    // still arrive, they were written through at the moment of the call.
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--write-settings")) {
        Settings s(nullptr, QStringLiteral("hyperbin-persisttest"));
        s.setInfestation(QStringLiteral("ooze"));
        s.setDensity(Settings::Density::Lots);
        _exit(0);
    }

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

    // --- flies persist ------------------------------------------------
    // There is no lifespan. Rubbish does not stop attracting flies after
    // a few seconds, and the constant churn of arrivals and departures
    // was motion the eye kept getting drawn to for no reason.
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 200; ++i) s.step(0.05f);   // let it fill up
        std::set<quint32> early;
        for (const Fly &f : s.flies()) early.insert(f.id);
        if (early.empty()) return fail("no swarm to follow");

        for (int i = 0; i < 1200; ++i) s.step(0.05f);  // 60 more seconds
        int survivors = 0;
        for (const Fly &f : s.flies())
            if (early.count(f.id)) ++survivors;
        std::printf("persistence: %d of %zu flies still around after 60s\n",
                    survivors, early.size());
        if (survivors < int(early.size()))
            return fail("flies are disappearing on their own");
    }

    // --- the swarm shrinks when the bin does --------------------------
    // With nothing expiring, this is now the ONLY thing that removes a
    // fly short of the pointer arriving, so it has to work.
    {
        FlySim s = makeSim(40, 1.0f);
        for (int i = 0; i < 300; ++i) s.step(0.05f);
        const int before = int(s.flies().size());
        s.setFullness(0.15f);
        for (int i = 0; i < 600; ++i) s.step(0.05f);
        const int after = int(s.flies().size());
        std::printf("less rubbish: swarm %d -> %d\n", before, after);
        if (after >= before) return fail("swarm does not shrink with the bin");
        if (after < 1)       return fail("swarm shrank past the floor");
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
        // Both modes must be a real part of the picture. The exact split
        // is a taste setting spread across landChance, the mode durations
        // and preferLanded, and it is tuned by eye — this is a guard
        // against either mode collapsing, NOT a target. Do not narrow it
        // to whatever the current tuning happens to produce.
        if (crawlShare < 0.30 || crawlShare > 0.90)
            return fail("movement mix has collapsed; flies should both "
                        "land and fly");
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
        std::printf("startle: %d frozen samples, %d bolting, %d of those leaving\n",
                    frozen, boltingSeen, boltRetiring);
        if (frozen == 0) return fail("flies never freeze");
        if (boltingSeen == 0) return fail("flies freeze but never bolt");
        // A startle is a burst, not a death: the fly rejoins the swarm
        // afterwards. Flies have no lifespan at all now, so any bolting
        // fly that is also leaving was retired for an unrelated reason.
        if (boltRetiring > boltingSeen / 2)
            return fail("bolting flies are being retired immediately");
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
        if (clippedOnArt || unclippedOffArt)
            return fail("clipToBin does not track the fly leaving the silhouette");
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

    // --- nothing lands behind the bin, and nothing pops into view -----
    // Landing used to check only "am I over the bin", not "am I in
    // front". A fly that arrived behind it is masked out completely, so
    // landing there flipped it from the hidden batch to the clipped-to-
    // surface batch and it appeared out of nowhere on the front.
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
        std::printf("landed while behind: %d; appeared on the bin: %d\n",
                    landedBehind, pops);
        if (landedBehind > 0)
            return fail("flies land behind the bin, where they cannot be seen");
        if (pops > 0)
            return fail("flies pop into view on top of the bin");
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
        // No size available means no Full Disk Access, and there is no
        // longer a count to fall back to. Deliberate: Finder can be asked
        // for a count without Full Disk Access but returns "missing
        // value" for the size of any FOLDER, so a trashed app bundle or
        // project directory weighed nothing — and the count fallback
        // ignored the threshold outright, which made the whole Trash
        // Threshold menu a no-op on a stock Mac. Better to draw nothing
        // and say why than to draw something quietly wrong.
        if (s.fullnessFor(-1, 20) != 0.0)
            return fail("without a size, fullness must be zero, not guessed");
        s.clearStore();
        std::printf("density: thirds, relative scaling and fallback all hold\n");
    }


    // ================= ooze ==============================================
    // The shape and the drips live in the shader now, driven by a signed
    // distance field of the bin. What is testable headlessly is the field
    // itself and the coating's level — which is all this class still owns.
    {
        // --- the distance field ------------------------------------------
        // Everything the ooze does hangs off this being right, and it is
        // the one part that CAN be checked without a screen.
        {
            const int n = 24;
            const QVector<quint8> cov = binSilhouette(n);
            const float range = 20.0f;
            const QImage f = buildSignedDistanceField(cov, n, n, 128, range);
            if (f.isNull() || f.width() != 128)
                return fail("no distance field produced");

            auto sample = [&](double u, double v) {
                const int x = std::clamp(int(u * f.width()), 0, f.width() - 1);
                const int y = std::clamp(int(v * f.height()), 0, f.height() - 1);
                return (f.constScanLine(y)[x] / 255.0 * 2.0 - 1.0) * range;
            };
            // Deep inside the bin is negative, well outside is positive,
            // and the sign flips across the boundary.
            const double inside = sample(0.5, 0.6);
            const double above  = sample(0.5, 0.02);
            const double beside = sample(0.02, 0.6);
            std::printf("field: inside %.1f, above %.1f, beside %.1f (cells)\n",
                        inside, above, beside);
            if (inside >= 0) return fail("the field says the bin's middle is outside it");
            if (above  <= 0) return fail("the field says above the lid is inside");
            if (beside <= 0) return fail("the field says beside the bin is inside");

            // Monotonic away from the surface: a field that is not makes
            // the coating's edge crawl instead of sitting still.
            double prev = -1e9;
            bool climbs = true;
            for (int i = 0; i < 12; ++i) {
                const double d = sample(0.5, 0.55 - i * 0.04);
                if (d < prev - 0.35) { climbs = false; break; }
                prev = d;
            }
            if (!climbs) return fail("the field is not monotonic away from the bin");
            std::printf("field is signed and monotonic\n");
        }

        // --- the coating's level ------------------------------------------
        OozeSim o;
        o.setBinRect(QRectF(500, 500, 40, 28));

        // A nearly-empty bin stays clean.
        o.setFullness(0.05f);
        for (int i = 0; i < 400; ++i) o.step(0.05f);
        if (!o.isEmpty()) return fail("a nearly-empty bin oozes");
        if (!o.isAtRest()) return fail("a clean bin never rests");

        // A full one gets coated, and creeps rather than snapping on.
        o.setFullness(1.0f);
        o.step(0.05f);
        const float afterOneStep = o.level();
        for (int i = 0; i < 400; ++i) o.step(0.05f);
        const float full = o.level();
        std::printf("coating: %.2f after one step, %.2f settled\n",
                    afterOneStep, full);
        if (afterOneStep >= full * 0.5f) return fail("the coating snaps on instead of creeping");
        if (full < 0.5f) return fail("a full bin is barely coated");
        if (o.isEmpty()) return fail("a full bin shows no ooze");

        // More trash, more coating.
        auto settled = [&](float f) {
            OozeSim s;
            s.setBinRect(QRectF(500, 500, 40, 28));
            s.setFullness(f);
            for (int i = 0; i < 600; ++i) s.step(0.05f);
            return s.level();
        };
        const float half = settled(0.5f);
        std::printf("coating level: %.2f at half, %.2f at full\n", half, full);
        if (!(full > half * 1.15f)) return fail("the coating does not grow with the trash");

        // Emptying recedes it away completely, and only then does it rest.
        o.setFullness(0.0f);
        int steps = 0;
        while (!o.isEmpty() && steps < 2000) { o.step(0.05f); ++steps; }
        std::printf("emptied: receded in %.1fs\n", steps * 0.05);
        if (!o.isEmpty()) return fail("ooze never recedes");
        if (steps < 4)    return fail("ooze vanishes instead of receding");
        if (!o.isAtRest()) return fail("receded ooze never rests");

        // The overlay has to be tall enough for a drip at full stretch.
        // Nothing else can check this: the drip lives in the shader, so a
        // margin that is too short shows as the drop being sliced off at
        // the window edge exactly as it lets go — the one frame anyone
        // would notice.
        for (qreal icon : {28.0, 40.0, 95.0}) {
            const double reach = icon * OozeSim::kDripReach * OozeSim::kDripStretch;
            const int mb = OozeSim::marginBottom(icon);
            std::printf("ooze %3.0fpx: drip reaches %.0f, margin %d\n",
                        icon, reach, mb);
            if (mb < reach)
                return fail("the overlay is too short for a drip at full stretch");
        }
        // An effect that cannot rest while it is visible has to at least
        // ask to be animated slowly — it is the only lever left on cost.
        if (o.preferredFrameIntervalMs() < 20)
            return fail("ooze does not ask to be slowed");
        std::printf("ooze asks for %dms frames\n", o.preferredFrameIntervalMs());
    }


    // --- settings survive a restart ------------------------------------
    // A menu-bar app is killed far more often than it is quit — the
    // debugger's stop button, a logout, Force Quit — so "saved when we
    // exit cleanly" is not saved at all.
    //
    // Run with one argument this binary acts as the WRITER half and exits
    // without a clean shutdown; the parent then re-reads the store in a
    // fresh process. Doing it in one process proves nothing: QSettings
    // keeps an in-process cache, so the second read would be answered
    // from memory whether or not anything reached the disk.
    {
        const QString store = QStringLiteral("hyperbin-persisttest");
        Settings before(nullptr, store);
        before.clearStore();
        const QString self = QString::fromLocal8Bit(argv[0]);
        if (QProcess::execute(self, {QStringLiteral("--write-settings")}) != 0)
            return fail("could not run the settings writer");
        Settings after(nullptr, store);
        // QSettings caches per process; this one is fresh, but force a
        // re-read anyway so the test cannot pass on a stale cache.
        std::printf("across a restart: infestation=%s density=%d\n",
                    qPrintable(after.infestation()), int(after.density()));
        const bool ok = after.infestation() == QStringLiteral("ooze")
                        && after.density() == Settings::Density::Lots;
        after.clearStore();
        if (!ok)
            return fail("settings do not survive being killed");
    }
    std::printf("PASS\n");
    return 0;
}
