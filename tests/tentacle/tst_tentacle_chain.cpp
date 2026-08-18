// The FABRIK solver behind the tentacles.
//
// This is the one piece of the effect that is testable without a window,
// which is why it lives in core/ rather than effects/. Everything here is
// an invariant that has actually been broken at least once: an arm that
// changes length, a joint that folds through its neighbour, a wave that
// speeds up the longer the app is left running.
//
// EVERY THRESHOLD BELOW WAS SET BY BREAKING THE THING IT GUARDS. Each was
// re-measured against a copy of TentacleChain with the relevant piece
// removed, and the figures from those runs are quoted where they set the
// bound. Three earlier drafts of these tests passed against the broken
// copies and had to be rewritten — see the notes on the roll and the root
// hold. If you add a check here, break the code first and watch it fail.
//
// Geometry throughout is the real thing to scale — a 300px bin with a
// 1.38-bin arm, the numbers TentacleEffect uses.
#include <QtTest>

#include "TentacleChain.h"

#include <QVector3D>
#include <algorithm>
#include <cmath>

using namespace hyperbin;

class TstTentacleChain : public QObject
{
    Q_OBJECT

private:
    static constexpr int kN = TentacleChain::kJoints;
    static constexpr float kBinH = 300.0f;
    static constexpr float kLen = 1.38f * kBinH;
    static constexpr float kSeg = kLen / float(kN - 1);

    static QVector3D base() { return QVector3D(0.0f, -0.20f * kBinH, 0.0f); }
    static QVector3D emerge() { return QVector3D(0.0f, 1.0f, 0.0f); }

    static float ease(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// The roll-up's own fade, as TentacleEffect::rollAmount shapes it: in
    /// over the first third, held, out over the last quarter.
    ///
    /// Not a detail of the test — switching the blend from 0 to 1 in one
    /// frame asks curlUp to rewrite the entire chain at once, and it duly
    /// reports a 284-unit lurch on a 414-unit arm. Nothing in the app ever
    /// does that, so a test that does is measuring its own harness.
    static float rollAt(int f, int f0, int f1)
    {
        if (f < f0 || f >= f1) return 0.0f;
        const float u = float(f - f0) / float(f1 - f0);
        return ease(std::min(u / 0.34f, 1.0f))
             * (1.0f - ease(std::max((u - 0.76f) / 0.24f, 0.0f)));
    }

    /// A target that swings the arm around most of its range, at a reach
    /// the arm can nearly meet. Deliberately not a straight line: the
    /// failures worth catching happen when the arm is turning.
    static QVector3D swing(float t)
    {
        return base() + QVector3D(std::sin(t * 1.3f) * 0.75f, 0.85f,
                                  std::cos(t * 0.9f) * 0.45f)
                            .normalized() * (kLen * 0.85f);
    }

    /// One frame in the order updateArms runs it, so what is exercised is
    /// the pipeline rather than solve() on its own.
    static void step(TentacleChain &c, int f, float roll)
    {
        const float t = float(f) * 0.016f;
        c.solve(base(), emerge(), swing(t), t, 1.0f, 1.0f,
                TentacleChain::kMaxBend);
        c.curlUp(base(), emerge(), 1.30f, roll);
        c.pushOutside(0.0f, kBinH, 150.0f, 110.0f, 0.8f,
                      TentacleChain::kRootHeld + 1);
        c.settle(base(), emerge(), TentacleChain::kMaxBend);
    }

private slots:
    // An arm does not grow or shrink.
    //
    // Three separate passes move joints without regard for the segments
    // that reach them — the wave, the roll-up, and the push out of the
    // bin's body — and each is followed by a constraint pass for exactly
    // this reason.
    void length_armDoesNotGrowOrShrink()
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        float worst = 0.0f, worstTotal = 0.0f, sum = 0.0f;
        int n = 0;
        for (int f = 0; f < 900; ++f) {
            step(c, f, rollAt(f, 300, 600));
            float total = 0.0f;
            for (int j = 0; j + 1 < kN; ++j) {
                const float l = (c.joints()[j + 1] - c.joints()[j]).length();
                worst = std::max(worst, std::abs(l - kSeg));
                sum += std::abs(l - kSeg);
                ++n;
                total += l;
            }
            worstTotal = std::max(worstTotal, std::abs(total - kLen));
        }
        qInfo("segment error mean %.2f worst %.2f of %.1f; whole arm %.2f of %.0f",
              double(sum / n), double(worst), double(kSeg),
              double(worstTotal), double(kLen));
        // Two thresholds, and the loose one is the important one.
        //
        // WHOLE-ARM length is what an eye can see: an arm that grows and
        // shrinks as it moves. That is held to about 1%.
        //
        // A single segment is allowed to run further out because
        // pushOutside slams a joint clear of the bin's body in one go and
        // the ten constraint passes that follow do not fully converge from
        // a displacement that large — the worst segment sits near 9% and
        // the mean an order of magnitude below it, so it is a transient at
        // one joint rather than a chain that has come apart.
        //
        // A COARSE GUARD, and worth knowing how coarse. Deleting the
        // constraint pass that follows the damping blend still passes here
        // — the arm goes from 1.1% to 1.7% out and the worst segment
        // barely moves, because the passes that remain largely cover for
        // it. What this catches is an arm that visibly grows and shrinks,
        // not the loss of any one pass.
        QVERIFY2(worst <= kSeg * 0.20f, "tentacle segments stretch");
        QVERIFY2(worstTotal <= kLen * 0.03f,
                 "the arm changes length as it moves");
    }

    // No joint folds through the one before it.
    //
    // FABRIK has no idea an arm is a physical object: a chain folded back
    // through itself satisfies every length constraint perfectly, and the
    // first render with a real solver came out crumpled for precisely that
    // reason. Verified: with limitBend stubbed out this reads 2.911 rad
    // against the 0.522 it holds now.
    void bend_noJointFoldsThroughItsNeighbour()
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        float worstBend = 0.0f;
        for (int f = 0; f < 900; ++f) {
            step(c, f, rollAt(f, 300, 600));
            for (int j = 1; j + 1 < kN; ++j) {
                const QVector3D a = (c.joints()[j] - c.joints()[j - 1]).normalized();
                const QVector3D b = (c.joints()[j + 1] - c.joints()[j]).normalized();
                worstBend = std::max(worstBend, std::acos(std::clamp(
                    QVector3D::dotProduct(a, b), -1.0f, 1.0f)));
            }
        }
        qInfo("worst bend %.3f rad against a limit of %.3f",
              double(worstBend), double(TentacleChain::kMaxBend));
        // The roll-up asks for a tighter curl than kMaxBend and is settled
        // back to it afterwards, so the ceiling that has to hold here is
        // the coil limit, not the walking one.
        QVERIFY2(worstBend <= TentacleChain::kCoilBend,
                 "a tentacle joint folds through its neighbour");
    }

    // The arm leaves its hole along the hole.
    //
    // FABRIK anchors the base POSITION and nothing else, so an arm asked
    // for something far to one side satisfies it by swinging its first
    // segment too — the anchor stays put and the arm leaves it at a new
    // angle. What the eye reads as the base is the first visible stretch,
    // so without this a wrap looked like the whole tentacle sliding around
    // the rim.
    //
    // Measured on the WHOLE held stretch, base to joint kRootHeld, and not
    // on the first joint. The first joint is written to the emergence line
    // outright, so an assertion about it reads 0.00 however broken
    // everything else is — it cannot fail, which makes it worse than no
    // test. That was the first draft of this check.
    void rootHold_armLeavesAlongTheHole()
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        const QVector3D sideways =
            base() + QVector3D(0.97f, 0.24f, 0.0f).normalized() * (kLen * 0.92f);
        float worstLean = 0.0f;
        for (int f = 0; f < 400; ++f) {
            c.solve(base(), emerge(), sideways, float(f) * 0.016f, 1.0f, 1.0f,
                    TentacleChain::kMaxBend);
            const QVector3D held =
                (c.joints()[TentacleChain::kRootHeld] - base()).normalized();
            worstLean = std::max(worstLean, std::acos(std::clamp(
                QVector3D::dotProduct(held, emerge()), -1.0f, 1.0f)));
        }
        qInfo("emerging stretch leans %.3f rad under a hard sideways reach",
              double(worstLean));
        // The bend limit ALONE would allow kRootHeld * kMaxBend = 1.54 rad
        // here, so 0.4 is comfortably inside what only the hold can
        // deliver — it measures 0.145, and 1.232 with holdRoot stubbed out.
        QVERIFY2(worstLean <= 0.4f,
                 "the arm does not leave its hole along the hole");
    }

    // A roll-up does not lurch.
    //
    // Driven the way TentacleEffect drives it: the target is pulled IN as
    // the roll tightens, the bend limit is raised to let the spiral close,
    // and the wave is turned down. That combination is the test. Given a
    // target at full stretch instead — a straight arm being spiralled —
    // the tip moves 180 units in a frame and always has; the effect avoids
    // asking for that rather than the solver coping with it, which is why
    // the Roll move gathers its target.
    //
    // WHAT THIS DOES NOT COVER, stated so nobody trusts it further than it
    // goes: the bug behind the roll's jitter was curlUp deriving its coil
    // plane afresh each frame, so an arm passing near straight had an
    // ill-conditioned cross product and the spiral could turn over. The
    // fix is measured — 65 lurch events in 45s against 8 — but only in the
    // running app, with the pointer dragging the arm straight while it
    // rolled. Six attempts to provoke the flip headlessly all came out
    // identical on both versions, so there is no assertion here that would
    // fail if the fix were reverted. Do not add one without checking it
    // fails on a copy that has the fix backed out.
    void roll_doesNotLurch()
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        const QVector3D up(0.0f, 1.0f, 0.0f);
        auto rollStep = [&](int f, float roll) {
            const float t = float(f) * 0.016f;
            // Rest is out at 0.80 of reach; a tight roll gathers to 0.44.
            // See TentacleEffect's Move::Roll.
            const QVector3D lean(std::sin(t * 0.4f) * 0.18f, 0.0f, 0.0f);
            const QVector3D rest = base() + up * (kLen * 0.80f) + lean * kLen;
            const QVector3D gathered = base() + up * (kLen * 0.44f) + lean * kLen;
            c.solve(base(), emerge(), rest + (gathered - rest) * roll, t, 1.0f,
                    0.45f, 1.32f);
            c.curlUp(base(), emerge(), 1.30f, roll);
            c.settle(base(), emerge(), 1.32f);
        };
        for (int f = 0; f < 200; ++f) rollStep(f, 0.0f);
        float worstJump = 0.0f;
        QVector3D last[kN];
        for (int j = 0; j < kN; ++j) last[j] = c.joints()[j];
        for (int f = 200; f < 700; ++f) {
            rollStep(f, rollAt(f, 200, 700));
            for (int j = 0; j < kN; ++j) {
                worstJump = std::max(worstJump, (c.joints()[j] - last[j]).length());
                last[j] = c.joints()[j];
            }
        }
        qInfo("roll: worst joint jump %.1f of a %.0f arm",
              double(worstJump), double(kLen));
        QVERIFY2(worstJump <= kLen * 0.08f,
                 "a rolling tentacle lurches between frames");
    }

    // The arm banks as ONE PIECE — it does not wind up along its length.
    //
    // The bank is a single angle laid on the seed, and transport carries
    // it to every joint unchanged, so the twist between neighbouring
    // frames is zero by construction. That is worth a test because the
    // obvious-looking alternative is not: an earlier version rolled every
    // joint onto its OWN bend plane, and since neighbouring joints curl in
    // slightly different planes, each got a slightly different roll and
    // the mesh wound along its length. Verified at 201 degrees of twist
    // from base to tip — an arm being wrung out rather than turning.
    //
    // Run with the wave on, because that is the case where per-joint
    // alignment is most tempting and most wrong.
    void roll_armBanksAsOnePiece_data()
    {
        // Two floats rather than one QVector3D, and not for tidiness.
        //
        // A QTest column is stored by calling QMetaType(id).create() —
        // a lookup by NUMERIC id, not by the compile-time type. QVector3D
        // is a QtGui built-in, and in an appless test binary on Windows
        // nothing has put QtGui's interface for that id into the registry
        // by the time a _data() function runs. create() then returns
        // null, the row's element is a null pointer, and QFETCH
        // dereferences it: an access violation with no diagnostic, inside
        // the test function, before its first line of real work.
        //
        // It cost a morning because every signpost pointed elsewhere: the
        // rows enumerate correctly under -datatags, macOS passes (frame-
        // works register their metatypes in a different order to DLLs),
        // and the same solver loop lifted into a standalone binary runs
        // clean at both -Od and -O2. Float columns are core types and
        // marshal correctly, so the direction is carried as its
        // components and rebuilt here.
        QTest::addColumn<float>("dx");
        QTest::addColumn<float>("dz");
        QTest::newRow("sweep +x") <<  1.0f << 0.0f;
        QTest::newRow("sweep -x") << -1.0f << 0.0f;
        QTest::newRow("sweep +z") <<  0.0f << 1.0f;
        QTest::newRow("sweep -z") <<  0.0f << -1.0f;
    }
    void roll_armBanksAsOnePiece()
    {
        QFETCH(float, dx);
        QFETCH(float, dz);
        const QVector3D dir(dx, 0.0f, dz);
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        for (int f = 0; f < 400; ++f) {
            const float u = ease(f / 200.0f);
            const QVector3D tgt = base()
                + QVector3D(0, 1, 0) * (kLen * (0.85f - 0.45f * u))
                + dir * (kLen * 0.55f * u);
            c.solve(base(), emerge(), tgt, float(f) * 0.016f, 1.0f, 0.3f,
                    TentacleChain::kCoilBend);
            c.settle(base(), emerge(), TentacleChain::kCoilBend);
        }
        float twist = 0.0f;
        for (int i = 2; i < kN; ++i) {
            const QVector3D t = (c.joints()[i] - c.joints()[i - 1]).normalized();
            QVector3D a = c.sides()[i - 1]
                        - t * QVector3D::dotProduct(c.sides()[i - 1], t);
            if (a.lengthSquared() < 1e-8f)
                continue;
            a.normalize();
            const QVector3D b = c.sides()[i];
            const float cs = std::clamp(QVector3D::dotProduct(a, b), -1.0f, 1.0f);
            const float sn = QVector3D::dotProduct(QVector3D::crossProduct(a, b), t);
            twist += std::abs(std::atan2(sn, cs));
        }
        const float deg = twist * 180.0f / 3.14159265f;
        qInfo("total twist along the arm: %.1f degrees; bank %.1f",
              double(deg), double(c.roll() * 180.0f / 3.14159265f));
        QVERIFY2(deg < 5.0f, "the arm winds up along its length instead of banking");
        // ...and the bank stays inside its stop, which is what stops the
        // suckers ever travelling round to the back.
        QVERIFY2(std::abs(c.roll()) <= TentacleChain::kRollLimit + 1e-3f,
                 "the bank exceeded its limit");
    }
    // The wave does not speed up the longer it runs, and does not chatter.
    //
    // The undulation's pace wanders so it does not read as a machine, and
    // there is exactly one way to write that which works. Scaling time by
    // a varying factor — sin(t * k(t)) — gives an effective frequency of
    // k + t*k', which GROWS WITHOUT BOUND: fine in a test that runs for
    // five seconds, a buzz in an app left open all afternoon. Displacing
    // the phase instead wanders around a fixed mean and stays there.
    //
    // Measured as DIRECTION REVERSALS of the tip, which is the frequency
    // itself. Summed displacement will not do: the amplitude is fixed, so
    // once the wave outruns the frame rate a faster one covers the same
    // ground per frame, and a metric that saturates cannot tell fast from
    // far too fast. Against a copy carrying the time-scaled form,
    // displacement separated the two by x1.14 and reversals by x6.8.
    void wave_doesNotAccelerateOrChatter()
    {
        const float early = tipReversalsPerSecond(10.0f);
        const float late = tipReversalsPerSecond(600.0f);
        qInfo("wave: %.2f tip reversals/s at 10s, %.2f at 600s (x%.2f)",
              double(early), double(late),
              double(late / std::max(early, 1e-4f)));
        // The correct form measures x0.90 here and the time-scaled trap
        // x6.79, so this sits with better than a factor of two either side
        // rather than being fitted to the current number.
        QVERIFY2(late <= early * 2.0f,
                 "the wave gets faster the longer the app runs");
        // The same number, read as an absolute, catches the OTHER way this
        // goes wrong. A tip riding the wave turns about 2.5 times a
        // second; a tip carrying the solver's chatter on top turns 12
        // times a second, because FABRIK does not have one solution and
        // wanders between the equally valid ones every frame. Built
        // against a copy with the damping removed — kAdopt at 1.0 — it
        // measures 12.34 against 2.51, so 6 separates them with room on
        // both sides. This is the assertion that would notice kAdopt being
        // tuned away; settling_stillArmGoesStill does not, because an
        // undamped chain also settles once the wave is switched off.
        QVERIFY2(early <= 6.0f,
                 "the arm chatters: the solver is not being damped");
    }

    // A still arm goes still.
    //
    // With the wave switched off (flex 0) and the target held, there is
    // one right answer and the solver has to arrive at it and stay.
    //
    // This does NOT test the damping, which is what it was written to do:
    // run against a copy with kAdopt at 1.0 it still reads exactly 0.0000,
    // because an undamped chain converges perfectly well once nothing is
    // kicking it. Damping is what stops the solver wandering between
    // equally valid solutions WHILE the wave kicks it, so the guard for
    // that is the absolute reversal rate above. What is left here is still
    // worth having — a solver that never converges, or one left creeping
    // by a constraint pass that fights itself, shows up as a non-zero
    // number — but it is a weaker claim.
    void settling_stillArmGoesStill()
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        const QVector3D still =
            base() + QVector3D(-0.3f, 0.9f, 0.15f).normalized() * (kLen * 0.85f);
        for (int f = 0; f < 400; ++f)
            c.solve(base(), emerge(), still, float(f) * 0.016f, 1.0f, 0.0f,
                    TentacleChain::kMaxBend);
        QVector3D last[kN];
        for (int j = 0; j < kN; ++j) last[j] = c.joints()[j];
        float worst = 0.0f;
        for (int f = 400; f < 500; ++f) {
            c.solve(base(), emerge(), still, float(f) * 0.016f, 1.0f, 0.0f,
                    TentacleChain::kMaxBend);
            for (int j = 0; j < kN; ++j) {
                worst = std::max(worst, (c.joints()[j] - last[j]).length());
                last[j] = c.joints()[j];
            }
        }
        qInfo("settled arm still moves %.4f a frame", double(worst));
        QVERIFY2(worst <= kLen * 0.002f,
                 "a tentacle shivers with nothing driving it");
    }

    // An arm gets out of the bin it came from.
    //
    // The straight route from a root buried in the rubbish to a target
    // down by the foot goes THROUGH the bin, which the mask then correctly
    // hides — so the arm appears as a floating fragment with a gap where
    // its middle should be. Pushing joints to the nearest surface is what
    // makes an arm go over the rim to get down the outside, which is what
    // it would physically have to do.
    void pushOutside_armGetsOutOfTheBin()
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        constexpr float kTopY = 0.0f, kHalfX = 150.0f, kHalfZ = 110.0f;
        constexpr float kTaper = 0.8f;
        // STRAIGHT DOWN, through the middle of the bin.
        //
        // Not down the front, which was the first choice and proves
        // nothing: a target already outside the near face is one FABRIK
        // reaches without ever entering the body, so that case measures 0
        // buried joints whether pushOutside runs or not. Straight down is
        // the case the routine exists for — it leaves 3 joints inside the
        // bin when pushOutside is skipped and none when it runs.
        //
        // A target on the FAR side is worse with pushOutside than without
        // (3 buried against 0): shoving joints toward the near face is the
        // wrong way out when the tip is behind the bin. Nothing asks for
        // that today — keepInFront drags every below-the-rim target
        // forward before the solver sees it — so it is recorded here
        // rather than guarded.
        const QVector3D down(0.0f, -0.8f * kBinH, 0.0f);
        for (int f = 0; f < 400; ++f) {
            c.solve(base(), emerge(), down, float(f) * 0.016f, 1.0f, 1.0f,
                    TentacleChain::kMaxBend);
            c.pushOutside(kTopY, kBinH, kHalfX, kHalfZ, kTaper,
                          TentacleChain::kRootHeld + 1);
            c.settle(base(), emerge(), TentacleChain::kMaxBend);
        }
        int buried = 0;
        for (int j = TentacleChain::kRootHeld + 1; j < kN; ++j) {
            const QVector3D &p = c.joints()[j];
            if (p.y() >= kTopY) continue;      // in the opening
            const float below = std::clamp((kTopY - p.y()) / kBinH, 0.0f, 1.0f);
            const float sx = kHalfX * (1.0f + (kTaper - 1.0f) * below);
            const float sz = kHalfZ * (1.0f + (kTaper - 1.0f) * below);
            const float ex = p.x() / sx, ez = p.z() / sz;
            // Inside the body AND behind its near face is the state that
            // draws as a hole in the arm.
            if (ex * ex + ez * ez < 1.0f
                && p.z() < sz * std::sqrt(std::max(0.0f, 1.0f - ex * ex)))
                ++buried;
        }
        qInfo("%d of %d joints left inside the bin's body", buried, kN);
        QCOMPARE(buried, 0);
    }

private:
    /// Direction reversals of the tip per second, averaged over 110
    /// seconds — four full cycles of the wave's own 27-second breath.
    ///
    /// A short window lands at a different point in that breath at each
    /// epoch, which on the first attempt showed the CORRECT implementation
    /// running x2.36 faster and would have made any useful threshold a
    /// coin toss.
    static float tipReversalsPerSecond(float startT)
    {
        TentacleChain c;
        c.reset(base(), emerge(), kLen);
        constexpr int kWin = int(110.0 / 0.016);
        const int f0 = int(startT / 0.016f);
        // A target held perfectly still, so what is measured is the wave
        // and nothing else.
        const QVector3D still =
            base() + QVector3D(0.1f, 0.95f, 0.2f).normalized() * (kLen * 0.85f);
        for (int f = f0; f < f0 + 40; ++f)
            c.solve(base(), emerge(), still, float(f) * 0.016f, 1.0f, 1.0f,
                    TentacleChain::kMaxBend);
        int reversals = 0;
        float prevX = c.joints()[kN - 1].x(), prevD = 0.0f;
        for (int f = f0 + 40; f < f0 + 40 + kWin; ++f) {
            c.solve(base(), emerge(), still, float(f) * 0.016f, 1.0f, 1.0f,
                    TentacleChain::kMaxBend);
            const float x = c.joints()[kN - 1].x(), d = x - prevX;
            if (d * prevD < 0.0f) ++reversals;
            if (std::abs(d) > 1e-4f) prevD = d;
            prevX = x;
        }
        return float(reversals) / (float(kWin) * 0.016f);
    }
};

QTEST_APPLESS_MAIN(TstTentacleChain)
#include "tst_tentacle_chain.moc"
