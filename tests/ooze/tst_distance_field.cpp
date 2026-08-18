// The signed distance field of the bin's silhouette.
//
// Everything the ooze does hangs off this being right — the coating's edge,
// the meniscus cut against the outline, where a drip may start — and it is
// the one part of that effect that CAN be checked without a screen.
#include <QtTest>

#include "DistanceField.h"
#include "test_helpers.h"

#include <QImage>
#include <algorithm>

using namespace hyperbin;
using hyperbin::test::binSilhouette;

class TstDistanceField : public QObject
{
    Q_OBJECT

private:
    static constexpr int kGrid = 24;
    static constexpr float kRange = 20.0f;

    static QImage field()
    {
        return buildSignedDistanceField(binSilhouette(kGrid), kGrid, kGrid,
                                        128, kRange);
    }

    /// Sample in normalised coordinates, back in field cells.
    static double sampleAt(const QImage &f, double u, double v)
    {
        const int x = std::clamp(int(u * f.width()), 0, f.width() - 1);
        const int y = std::clamp(int(v * f.height()), 0, f.height() - 1);
        return (f.constScanLine(y)[x] / 255.0 * 2.0 - 1.0) * kRange;
    }

private slots:
    void build_producesAField()
    {
        const QImage f = field();
        QVERIFY2(!f.isNull(), "no distance field produced");
        QCOMPARE(f.width(), 128);
    }

    // Deep inside the bin is negative, well outside is positive, and the
    // sign flips across the boundary. A field with the sign the wrong way
    // round coats the desktop instead of the bin.
    void sign_isNegativeInsideAndPositiveOutside()
    {
        const QImage f = field();
        const double inside = sampleAt(f, 0.5, 0.6);
        const double above  = sampleAt(f, 0.5, 0.02);
        const double beside = sampleAt(f, 0.02, 0.6);
        qInfo("field: inside %.1f, above %.1f, beside %.1f (cells)",
              inside, above, beside);
        QVERIFY2(inside < 0, "the field says the bin's middle is outside it");
        QVERIFY2(above > 0, "the field says above the lid is inside");
        QVERIFY2(beside > 0, "the field says beside the bin is inside");
    }

    // Monotonic away from the surface: a field that is not makes the
    // coating's edge crawl instead of sitting still.
    void gradient_isMonotonicAwayFromTheBin()
    {
        const QImage f = field();
        double prev = -1e9;
        for (int i = 0; i < 12; ++i) {
            const double d = sampleAt(f, 0.5, 0.55 - i * 0.04);
            QVERIFY2(d >= prev - 0.35,
                     "the field is not monotonic away from the bin");
            prev = d;
        }
    }
};

QTEST_APPLESS_MAIN(TstDistanceField)
#include "tst_distance_field.moc"
