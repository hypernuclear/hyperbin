// The coating's level, which is all OozeSim still owns.
//
// The shape and the drips live in the shader, driven by a signed distance
// field of the bin's artwork — see tst_distance_field for the half of this
// effect that CAN be checked without a screen. What is left here is how far
// the gel has crept and whether anything is still changing.
#include <QtTest>

#include "OozeSim.h"

#include <QRectF>

using namespace hyperbin;

namespace {

/// A bin with the given fullness, run until the level settles.
float settledLevel(float fullness)
{
    OozeSim s;
    s.setBinRect(QRectF(500, 500, 40, 28));
    s.setFullness(fullness);
    for (int i = 0; i < 600; ++i) s.step(0.05f);
    return s.level();
}

} // namespace

class TstOozeSim : public QObject
{
    Q_OBJECT

private slots:
    // A bin with one item in it is not oozing; the effect is a warning,
    // not a constant.
    void level_nearlyEmptyBinStaysClean()
    {
        OozeSim o;
        o.setBinRect(QRectF(500, 500, 40, 28));
        o.setFullness(0.05f);
        for (int i = 0; i < 400; ++i) o.step(0.05f);
        QVERIFY2(o.isEmpty(), "a nearly-empty bin oozes");
        QVERIFY2(o.isAtRest(), "a clean bin never rests");
    }

    // A full one gets coated, and CREEPS rather than snapping on. The
    // level is a critically damped spring for exactly this reason: a rate
    // is a straight line, and nothing thick moves like that.
    void level_fullBinCreepsRatherThanSnapping()
    {
        OozeSim o;
        o.setBinRect(QRectF(500, 500, 40, 28));
        o.setFullness(1.0f);
        o.step(0.05f);
        const float afterOneStep = o.level();
        for (int i = 0; i < 400; ++i) o.step(0.05f);
        const float full = o.level();
        qInfo("coating: %.2f after one step, %.2f settled", afterOneStep, full);
        QVERIFY2(afterOneStep < full * 0.5f,
                 "the coating snaps on instead of creeping");
        QVERIFY2(full >= 0.5f, "a full bin is barely coated");
        QVERIFY2(!o.isEmpty(), "a full bin shows no ooze");
    }

    void level_growsWithTheTrash()
    {
        const float half = settledLevel(0.5f);
        const float full = settledLevel(1.0f);
        qInfo("coating level: %.2f at half, %.2f at full", half, full);
        QVERIFY2(full > half * 1.15f, "the coating does not grow with the trash");
    }

    // Emptying recedes it away completely, and only then does it rest.
    void level_emptyingRecedesCompletely()
    {
        OozeSim o;
        o.setBinRect(QRectF(500, 500, 40, 28));
        o.setFullness(1.0f);
        for (int i = 0; i < 400; ++i) o.step(0.05f);

        o.setFullness(0.0f);
        int steps = 0;
        while (!o.isEmpty() && steps < 2000) { o.step(0.05f); ++steps; }
        qInfo("emptied: receded in %.1fs", steps * 0.05);
        QVERIFY2(o.isEmpty(), "ooze never recedes");
        QVERIFY2(steps >= 4, "ooze vanishes instead of receding");
        // This used to assert that the ooze asked to be run slowly, on the
        // reasoning that an effect which cannot rest has no other lever on
        // its cost. Both halves turned out to be wrong: NO effect here
        // rests (every isAtRest() collapses to isEmpty()), so it was not a
        // property of this one — and the per-effect cap was overriding the
        // power policy rather than cooperating with it, so the gel ran at
        // 30fps on a 120Hz display. Cadence is core/PowerPolicy's alone
        // now, which is where the low-power decision already lives.
        QVERIFY2(o.isAtRest(), "receded ooze never rests");
    }

    // The overlay has to be tall enough for a drip at full stretch.
    // Nothing else can check this: the drip lives in the shader, so a
    // margin that is too short shows as the drop being sliced off at
    // the window edge exactly as it lets go — the one frame anyone
    // would notice.
    void margins_leaveRoomForADripAtFullStretch_data()
    {
        QTest::addColumn<qreal>("icon");
        QTest::newRow("28px") << 28.0;
        QTest::newRow("40px") << 40.0;
        QTest::newRow("95px") << 95.0;
    }

    void margins_leaveRoomForADripAtFullStretch()
    {
        QFETCH(qreal, icon);
        const double reach = icon * OozeSim::kDripReach * OozeSim::kDripStretch;
        const int mb = OozeSim::marginBottom(icon);
        qInfo("ooze %3.0fpx: drip reaches %.0f, margin %d", icon, reach, mb);
        QVERIFY2(mb >= reach,
                 "the overlay is too short for a drip at full stretch");
    }
};

QTEST_APPLESS_MAIN(TstOozeSim)
#include "tst_ooze_sim.moc"
