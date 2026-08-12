#include "app/Settings.h"
#include "app/TrayMenu.h"
#include "core/PowerPolicy.h"
#include "platform/TrashTarget.h"
#include "render/FlyItem.h"

#include <QApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include "platform/MacOverlay.h"
#endif

using namespace hyperbin;

int main(int argc, char **argv)
{
    // Transparent overlay surfaces need an alpha-capable default format
    // requested before the first window exists.
    QQuickWindow::setDefaultAlphaBuffer(true);

    // QApplication, not QGuiApplication: QSystemTrayIcon and QMenu are
    // Widgets classes. The overlay itself is still pure Quick.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("hyperbin"));
    app.setOrganizationName(QStringLiteral("Hypernuclear"));
    // The menu-bar item IS the app's UI, so the app must survive the
    // overlay window being hidden — which happens whenever the bin is
    // empty or the animation is switched off.
    QApplication::setQuitOnLastWindowClosed(false);

    // Dev mode: a normal window instead of a click-through overlay, so
    // the simulation can be iterated on without fighting the shell.
    const bool windowed = app.arguments().contains(QStringLiteral("--windowed"));

    auto target = TrashTarget::create();
    PowerPolicy power;
    Settings settings;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("windowedMode"), windowed);
    engine.loadFromModule("hyperbin", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *fly = win ? win->findChild<FlyItem *>(QStringLiteral("flies")) : nullptr;
    if (!fly)
        return -1;

    // Windowed dev mode is a manual sim playground: the slider owns
    // fullness and the window is always visible. Wiring the target here
    // too would fight it — the stub emits itemCountChanged(0) on start,
    // resetting fullness, and PowerPolicy would hide the window whenever
    // the bin read empty.
    if (windowed)
        return app.exec();

#if defined(Q_OS_MACOS)
    // Above the Dock, click-through, on every Space. Must happen before
    // the window is first shown or the level change flickers.
    configureOverlayWindow(win);
#endif

    // The trash icon can be present but unreachable — an auto-hidden Dock
    // parks it off-screen. Don't render at something the user can't see.
    QObject::connect(target.get(), &TrashTarget::statusChanged, &power,
                     [&](TrashTarget::Status s) {
        power.setTargetVisible(s == TrashTarget::Status::Ok);
        if (s == TrashTarget::Status::PermissionRequired)
            qInfo("hyperbin: waiting for Accessibility. The system prompt is showing; "
                  "tracking starts as soon as it's granted, no restart needed.");
    });

    // Wire the three inputs that decide whether we draw at all.
    QObject::connect(&power, &PowerPolicy::changed, fly, [&] {
        fly->setFrameIntervalMs(power.frameIntervalMs());
        if (win)
            win->setVisible(power.shouldRender());
#if defined(Q_OS_MACOS)
        // Without this the swarm stops a few seconds after another app
        // takes focus: we're a background agent, which is exactly what
        // App Nap throttles. Held only while animating.
        setAnimationActivity(power.shouldRender());
#endif
    });
    QObject::connect(fly, &FlyItem::swarmWentIdle, &power,
                     [&] { power.setSwarmIdle(true); });

    // Fullness comes from the SETTINGS, not straight from the bin: the
    // three fixed densities deliberately ignore what's actually in there,
    // and only the relative mode looks at the trash at all.
    auto applyDensity = [&] {
        const int n = target->itemCount();
        fly->setFullness(n == 0 ? 0.0
                                : settings.fullnessFor(target->byteSize(), n));
    };
    QObject::connect(&settings, &Settings::appearanceChanged, fly, applyDensity);
    QObject::connect(target.get(), &TrashTarget::byteSizeChanged, fly,
                     [&](qint64) { applyDensity(); });

    // Pointer on the bin: the swarm scatters, then rendering stops dead
    // until it leaves. FlyItem keeps one coarse timer alive to notice.
    QObject::connect(fly, &FlyItem::scatteredChanged, &power,
                     [&](bool s) { power.setScattered(s); });

    QObject::connect(target.get(), &TrashTarget::itemCountChanged, fly, [&](int n) {
        applyDensity();
        power.setBinEmpty(n == 0);
        // Full and empty are different artwork; refresh when it flips.
        const QRect ir = target->iconRect();
        if (!ir.isEmpty())
            fly->setBinIcon(target->iconImage(int(qMax(ir.width(), ir.height()) * 2)));
        if (n > 0)
            power.setSwarmIdle(false);
    });
    QObject::connect(target.get(), &TrashTarget::iconRectChanged, fly,
                     [&](const QRect &r) {
        if (!win || windowed)
            return;
        // Overlay = icon plus flight margin, scaled with the icon so a
        // magnified Dock tile doesn't clip the swarm. Asymmetric: flies
        // rise well above the bin but barely stray to either side.
        const qreal d  = qMax(r.width(), r.height());
        const int mx   = FlySim::marginX(d);
        const int mtop = FlySim::marginTop(d);
        const int mbot = FlySim::marginBottom(d);
        win->setGeometry(r.adjusted(-mx, -mtop, mx, mbot));
        fly->setBinRect(QRectF(mx, mtop, r.width(), r.height()));
        fly->setSize(QSizeF(win->width(), win->height()));

        // Flies pass behind the bin: our own copy of the Trash artwork is
        // composited over the swarm. This only works because iconRect()
        // now reports the artwork's true bounds rather than the
        // Accessibility hit area — see visualIconRect().
        fly->setBinIcon(target->iconImage(int(qMax(r.width(), r.height()) * 2)));
    });

    // HYPERBIN_DEBUG=1 reports where the overlay thinks it is. Kept because
    // "nothing is drawing" has several indistinguishable causes here —
    // wrong screen, wrong level, not visible, or simply no flies — and
    // this separates them in one line.
    if (qEnvironmentVariableIsSet("HYPERBIN_DEBUG")) {
        auto *dbg = new QTimer(&app);
        dbg->setInterval(1000);
        QObject::connect(dbg, &QTimer::timeout, [&] {
            const QRect g = win->geometry();
            const QRect ir = target->iconRect();
            qInfo("dbg: win visible=%d geom=(%d,%d %dx%d) icon=(%d,%d %dx%d) "
                  "screen=%s fullness=%.2f interval=%d render=%d",
                  win->isVisible(), g.x(), g.y(), g.width(), g.height(),
                  ir.x(), ir.y(), ir.width(), ir.height(),
                  win->screen() ? qPrintable(win->screen()->name()) : "none",
                  fly->fullness(), fly->frameIntervalMs(), power.shouldRender());
        });
        dbg->start();
    }

    // --- menu-bar item --------------------------------------------------
    TrayMenu tray(&settings);
    if (!tray.available())
        qWarning("hyperbin: no system tray is available; the app is running "
                 "with no way to configure or quit it");
    QObject::connect(&tray, &TrayMenu::quitRequested, &app, &QApplication::quit);

    auto describeTrash = [&]() -> QString {
        const int n = target->itemCount();
        if (n == 0)
            return QStringLiteral("Trash is empty");
        const QString items = QStringLiteral("%1 item%2")
            .arg(n).arg(n == 1 ? QString() : QStringLiteral("s"));
        const qint64 b = target->byteSize();
        // byteSize() is -1 when ~/.Trash can't be read, which is the
        // normal case without Full Disk Access. Say what we know rather
        // than showing a wrong or empty size.
        if (b < 0)
            return QStringLiteral("Trash: ") + items;
        return QStringLiteral("Trash: %1, %2")
            .arg(items, QLocale().formattedDataSize(b));
    };
    auto refreshStatus = [&] { tray.setStatusText(describeTrash()); };
    QObject::connect(target.get(), &TrashTarget::itemCountChanged, &tray,
                     [&](int) { refreshStatus(); });
    QObject::connect(target.get(), &TrashTarget::byteSizeChanged, &tray,
                     [&](qint64) { refreshStatus(); });

    // --- master switch ----------------------------------------------------
    // Off means genuinely off: no frames, no overlay surface, and the
    // trash poll stopped as well. Leaving the poll running would be a
    // background app doing work the user has just told it not to do.
    auto applyEnabled = [&](bool on) {
        power.setEnabled(on);
        if (on) {
            target->start();
            power.setBinEmpty(target->itemCount() == 0);
            power.setTargetVisible(target->status() == TrashTarget::Status::Ok);
        } else {
            target->stop();
            if (win)
                win->setVisible(false);
        }
        refreshStatus();
    };
    QObject::connect(&settings, &Settings::enabledChanged, &app,
                     [&](bool on) { applyEnabled(on); });

    power.setSwarmIdle(true);
    applyEnabled(settings.enabled());

    return app.exec();
}
