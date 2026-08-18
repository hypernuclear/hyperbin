// What the preferences mean, and that they survive being killed.
#include <QtTest>

#include "Settings.h"

#include <QProcess>
#include <QString>

// _exit(), for the kill-the-writer half of the persistence test. POSIX
// declares it in <unistd.h>, which does not exist on MSVC; there it lives
// in <process.h>. Same function either way — the point is to leave without
// running a single destructor.
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cmath>

using namespace hyperbin;

class TstSettings : public QObject
{
    Q_OBJECT

private slots:
    // The three fixed steps are thirds by design, so they line up with
    // the relative mode's scale instead of being arbitrary numbers.
    void density_fixedStepsAreThirds()
    {
        // Throwaway store: never the user's real preferences.
        Settings s(nullptr, QStringLiteral("hyperbin-selftest"));
        const qint64 mb = 1024 * 1024;

        s.setDensity(Settings::Density::Few);
        QVERIFY2(std::abs(s.fullnessFor(0, 0) - 1.0 / 3.0) < 1e-9,
                 "'a few flies' is not a third");
        s.setDensity(Settings::Density::Lots);
        QVERIFY2(std::abs(s.fullnessFor(0, 0) - 2.0 / 3.0) < 1e-9,
                 "'lots of flies' is not two thirds");
        s.setDensity(Settings::Density::TooMany);
        QVERIFY2(std::abs(s.fullnessFor(0, 0) - 1.0) < 1e-9,
                 "'too many flies' is not full");
        // Fixed densities must ignore the bin entirely — that is the
        // whole reason to offer them.
        QVERIFY2(s.fullnessFor(0, 0) == s.fullnessFor(50 * mb, 12),
                 "a fixed density still looks at the trash");
        s.clearStore();
    }

    void density_relativeScalesWithTheThreshold()
    {
        Settings s(nullptr, QStringLiteral("hyperbin-selftest"));
        const qint64 mb = 1024 * 1024;

        s.setDensity(Settings::Density::Relative);
        s.setThreshold(Settings::Threshold::HundredMB);
        QVERIFY2(s.fullnessFor(0, 0) == 0.0, "an empty bin should draw nothing");
        QVERIFY2(std::abs(s.fullnessFor(50 * mb, 3) - 0.5) < 1e-9,
                 "half the threshold should be half full");
        QVERIFY2(std::abs(s.fullnessFor(500 * mb, 3) - 1.0) < 1e-9,
                 "past the threshold should saturate, not overflow");
        s.setThreshold(Settings::Threshold::OneGB);
        QVERIFY2(std::abs(s.fullnessFor(50 * mb, 3) - 50.0 / 1024.0) < 1e-9,
                 "threshold change does not rescale");
        s.clearStore();
    }

    // No size available means no Full Disk Access, and there is no longer
    // a count to fall back to. Deliberate: Finder can be asked for a count
    // without Full Disk Access but returns "missing value" for the size of
    // any FOLDER, so a trashed app bundle or project directory weighed
    // nothing — and the count fallback ignored the threshold outright,
    // which made the whole Trash Threshold menu a no-op on a stock Mac.
    // Better to draw nothing and say why than to draw something quietly
    // wrong.
    void density_withoutASizeDrawsNothing()
    {
        Settings s(nullptr, QStringLiteral("hyperbin-selftest"));
        s.setDensity(Settings::Density::Relative);
        s.setThreshold(Settings::Threshold::HundredMB);
        QVERIFY2(s.fullnessFor(-1, 20) == 0.0,
                 "without a size, fullness must be zero, not guessed");
        s.clearStore();
    }

    // A menu-bar app is killed far more often than it is quit — the
    // debugger's stop button, a logout, Force Quit — so "saved when we
    // exit cleanly" is not saved at all.
    //
    // This binary re-runs ITSELF with --write-settings; that half sets two
    // values and leaves via _exit, so nothing is flushed by a destructor.
    // Doing it in one process proves nothing: QSettings keeps an
    // in-process cache, so the second read would be answered from memory
    // whether or not anything reached the disk.
    void persistence_survivesBeingKilled()
    {
        const QString store = QStringLiteral("hyperbin-persisttest");
        Settings before(nullptr, store);
        before.clearStore();

        const QString self = QCoreApplication::applicationFilePath();
        QCOMPARE(QProcess::execute(self, {QStringLiteral("--write-settings")}), 0);

        // A fresh process wrote them; this one has never read this store,
        // so its cache cannot answer for the disk.
        Settings after(nullptr, store);
        qInfo("across a restart: infestation=%s density=%d",
              qPrintable(after.infestation()), int(after.density()));
        const bool ok = after.infestation() == QStringLiteral("ooze")
                        && after.density() == Settings::Density::Lots;
        after.clearStore();
        QVERIFY2(ok, "settings do not survive being killed");
    }
};

// Hand-written rather than QTEST_APPLESS_MAIN: the persistence test needs
// this same binary to act as the WRITER half, and that has to be handled
// before QTest ever looks at the arguments.
int main(int argc, char **argv)
{
    if (argc > 1 && QString::fromLocal8Bit(argv[1])
                        == QStringLiteral("--write-settings")) {
        Settings s(nullptr, QStringLiteral("hyperbin-persisttest"));
        s.setInfestation(QStringLiteral("ooze"));
        s.setDensity(Settings::Density::Lots);
        _exit(0);
    }
    // QCoreApplication, not appless: applicationFilePath() is how the test
    // finds itself to re-run, and it is empty without one.
    QCoreApplication app(argc, argv);
    TstSettings tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_settings.moc"
