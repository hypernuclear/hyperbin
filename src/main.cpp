#include "core/PowerPolicy.h"
#include "platform/TrashTarget.h"
#include "render/FlyItem.h"

#include <QGuiApplication>
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

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("hyperbin"));
    app.setOrganizationName(QStringLiteral("Hypernuclear"));

    // Dev mode: a normal window instead of a click-through overlay, so
    // the simulation can be iterated on without fighting the shell.
    const bool windowed = app.arguments().contains(QStringLiteral("--windowed"));

    auto target = TrashTarget::create();
    PowerPolicy power;

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

    QObject::connect(target.get(), &TrashTarget::itemCountChanged, fly, [&](int n) {
        // Saturate at ~40 items; beyond that it's already a cloud.
        fly->setFullness(qMin(1.0, n / 40.0));
        power.setBinEmpty(n == 0);
        // Full and empty are different artwork; refresh when it flips.
        const QRect ir = target->iconRect();
        if (qEnvironmentVariableIsSet("HYPERBIN_BIN_MASK") && !ir.isEmpty())
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

        // Flies-behind-the-bin: we composite our own copy of the Trash
        // artwork over the swarm. Correct in principle, but the
        // Accessibility rect is NOT the artwork's visual bounds (it
        // reports 40x28 for a square icon), so the copy lands offset from
        // the Dock's own rendering and shows as a double image. Off until
        // the icon's true rect can be derived — a misaligned copy looks
        // considerably worse than flies simply passing in front.
        if (qEnvironmentVariableIsSet("HYPERBIN_BIN_MASK"))
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
            qInfo("dbg: win visible=%d geom=(%d,%d %dx%d) screen=%s | fly size=%.0fx%.0f "
                  "fullness=%.2f interval=%d render=%d",
                  win->isVisible(), g.x(), g.y(), g.width(), g.height(),
                  win->screen() ? qPrintable(win->screen()->name()) : "none",
                  fly->width(), fly->height(), fly->fullness(),
                  fly->frameIntervalMs(), power.shouldRender());
        });
        dbg->start();
    }

    target->start();
    power.setSwarmIdle(true);
    power.setBinEmpty(target->itemCount() == 0);
    power.setTargetVisible(target->status() == TrashTarget::Status::Ok);

    return app.exec();
}
