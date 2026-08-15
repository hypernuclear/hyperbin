#include "app/Settings.h"
#include "app/TrayMenu.h"
#include "core/PowerPolicy.h"
#include "platform/TrashTarget.h"
#include "render/EffectItem.h"
#include "update/AppUpdater.h"
#include "analytics/Analytics.h"

#include <QApplication>
#include <QLocale>
#include <QPainter>
#include <QSvgRenderer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPointer>
#include <QQmlComponent>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include "platform/DockTrashTarget.h"
#include "platform/MacOverlay.h"
#elif defined(Q_OS_WIN)
#include "platform/WinOverlay.h"
#endif

using namespace hyperbin;

namespace {
/// The onboarding window's connection to the platform layer.
///
/// A tiny object rather than exposing TrashTarget to QML directly: the
/// window needs exactly two verbs and one list, and TrashTarget is a
/// large interface that has nothing else to do with the UI.
class PermissionBridge : public QObject
{
    Q_OBJECT
public:
    PermissionBridge(TrashTarget *target, QQmlApplicationEngine *engine,
                     QObject *parent = nullptr)
        : QObject(parent), m_target(target), m_engine(engine)
    {
    }
    /// Push the current state into the QML context. Re-setting the
    /// property is what re-evaluates the bindings that read it.
    void publish()
    {
        QVariantList out;
        for (const TrashTarget::Permission &p : m_target->permissions()) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), p.id);
            m.insert(QStringLiteral("title"), p.title);
            m.insert(QStringLiteral("detail"), p.detail);
            m.insert(QStringLiteral("granted"), p.granted);
            out.append(m);
        }
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("permissionList"), out);
    }
    Q_INVOKABLE void open(const QString &id) { m_target->openPermission(id); }
    Q_INVOKABLE void refresh() { m_target->refreshPermissions(); }
private:
    TrashTarget *m_target;
    QQmlApplicationEngine *m_engine;
};
} // namespace
namespace {
/// Serves the preview's stand-in for the shell's trash artwork to QML, so
/// dev mode can draw the bin underneath the overlay the way the Dock
/// does. Dev only: nothing in the shipping path goes through this.
class PreviewIconProvider : public QQuickImageProvider
{
public:
    explicit PreviewIconProvider(QImage image)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_image(std::move(image))
    {
    }
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        if (size)
            *size = m_image.size();
        return m_image;
    }
private:
    QImage m_image;
};
} // namespace

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
    // Reaches QML as Qt.application.version; the splash shows it.
    app.setApplicationVersion(QStringLiteral(HYPERBIN_VERSION));
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

    // The bin's artwork, for dev mode only. Built before the engine loads
    // because the preview draws it BEHIND the effect, the way the shell
    // does — without it the overlay floats over nothing, and anything an
    // effect cuts out of itself to let the bin show through is judged
    // against an empty background.
    QImage previewIcon;
    if (windowed) {
        // The SHELL's artwork, not our own mark. The app's glyph is a
        // clean opaque outline; the real Trash is a translucent mesh with
        // solid contents behind it, and an effect that reads well over
        // the first can be unrecognisable over the second. Tuning against
        // a stand-in cost several rounds of exactly that.
#if defined(Q_OS_MACOS)
        previewIcon = macTrashIcon(512, true);
#endif
        // Windows falls straight through to the glyph below. That is the
        // stand-in the comment above warns about, so treat a preview tuned
        // here with suspicion until the Recycle Bin's own artwork is
        // wired in — RecycleBinTarget::iconImage() already returns it.
        if (previewIcon.isNull()) {
            QSvgRenderer glyph(QStringLiteral(":/icons/hyperbin.svg"));
            if (glyph.isValid()) {
                previewIcon = QImage(512, 512, QImage::Format_ARGB32_Premultiplied);
                previewIcon.fill(Qt::transparent);
                QPainter p(&previewIcon);
                p.setRenderHint(QPainter::Antialiasing, true);
                glyph.render(&p, QRectF(40, 20, 432, 472));
            }
        }
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("windowedMode"), windowed);
    engine.addImageProvider(QStringLiteral("preview"),
                            new PreviewIconProvider(previewIcon));
    engine.loadFromModule("hyperbin", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *fx = win ? win->findChild<EffectItem *>(QStringLiteral("effect")) : nullptr;
    if (!fx)
        return -1;

    // Windowed dev mode is a manual sim playground: the slider owns
    // fullness and the window is always visible. Wiring the target here
    // too would fight it — the stub emits itemCountChanged(0) on start,
    // resetting fullness, and PowerPolicy would hide the window whenever
    // the bin read empty.
    if (windowed) {
        // Dev harness. Iterating on how an effect LOOKS by screenshotting
        // a 49-pixel Dock icon does not work — at that size the whole
        // gel is twenty pixels tall and every feature worth judging is
        // sub-pixel. This shows it at whatever size the window is, with
        // the real bin artwork, so a change can actually be seen.
        //
        //   HYPERBIN_PREVIEW=ooze ./hyperbin --windowed
        const QString preview = qEnvironmentVariable("HYPERBIN_PREVIEW");
        if (!preview.isEmpty())
            fx->setEffectId(preview);
        // The SHELL's artwork, not our own mark. The app's glyph is a
        // clean opaque outline; the real Trash is a translucent mesh with
        // solid contents behind it, and an effect that reads well over
        // the first can be unrecognisable over the second. Tuning against
        // a stand-in cost several rounds of exactly that.
        QImage icon;
#if defined(Q_OS_MACOS)
        icon = macTrashIcon(512, true);
#endif
        if (icon.isNull()) {
            QSvgRenderer glyph(QStringLiteral(":/icons/hyperbin.svg"));
            if (glyph.isValid()) {
                icon = QImage(512, 512, QImage::Format_ARGB32_Premultiplied);
                icon.fill(Qt::transparent);
                QPainter p(&icon);
                p.setRenderHint(QPainter::Antialiasing, true);
                glyph.render(&p, QRectF(40, 20, 432, 472));
            }
        }
        if (!icon.isNull())
            fx->setBinIcon(icon);
        // HYPERBIN_PREVIEW_SHOT=<path> grabs the window and exits, so a
        // change to an effect's look can be seen without hunting for the
        // right window on screen — which is its own source of wrong
        // conclusions.
        const QString shot = qEnvironmentVariable("HYPERBIN_PREVIEW_SHOT");
        if (!shot.isEmpty()) {
            // HYPERBIN_PREVIEW_SHOT_MS moves the grab in time. Two grabs
            // at different moments is the only way to tell an animation
            // that is running from one that is merely present in the
            // shader — twice this session a "subtle" motion turned out to
            // be no motion at all.
            const int at = qEnvironmentVariableIntValue("HYPERBIN_PREVIEW_SHOT_MS");
            QTimer::singleShot(at > 0 ? at : 2500, win, [win, shot] {
                const QImage img = win->grabWindow();
                if (img.save(shot))
                    qInfo("hyperbin: preview written to %s", qPrintable(shot));
                else
                    qWarning("hyperbin: could not write %s", qPrintable(shot));
                QCoreApplication::quit();
            });
        }
        return app.exec();
    }

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    // Click-through and above what it sits on. Must happen before the
    // window is first shown or the level change flickers. Both backends
    // declare the same call; what it has to arrange differs completely.
    configureOverlayWindow(win);
#endif

#if defined(Q_OS_WIN)
    // The Dock is always on top; the desktop is always underneath. So on
    // Windows there is a second reason not to draw that macOS never has:
    // the bin is there, visible in principle, and buried under whatever
    // the user is working in. Feeding it into the SAME targetVisible flag
    // means a covered desktop costs exactly what an auto-hidden Dock
    // costs, which is nothing.
    DesktopWatcher desktop;
    desktop.setOverlay(win);
#endif
    bool desktopVisible = true;

    // Two independent reasons the target may be unusable, one flag. Both
    // have to be re-evaluated together, or whichever changed last wins and
    // the other is forgotten.
    auto applyTargetVisible = [&] {
        power.setTargetVisible(target->status() == TrashTarget::Status::Ok
                               && desktopVisible);
    };
#if defined(Q_OS_WIN)
    QObject::connect(&desktop, &DesktopWatcher::desktopVisibleChanged, &power,
                     [&](bool visible) {
        desktopVisible = visible;
        applyTargetVisible();
    });
#endif

    // The trash icon can be present but unreachable — an auto-hidden Dock
    // parks it off-screen, and on Windows it can be switched off entirely
    // in Desktop Icon Settings. Don't render at something the user can't
    // see.
    QObject::connect(target.get(), &TrashTarget::statusChanged, &power,
                     [&](TrashTarget::Status s) {
        applyTargetVisible();
        if (s == TrashTarget::Status::PermissionRequired)
            qInfo("hyperbin: waiting for Accessibility. The system prompt is showing; "
                  "tracking starts as soon as it's granted, no restart needed.");
        if (s == TrashTarget::Status::DiskAccessRequired)
            qInfo("hyperbin: waiting for Full Disk Access. There is no prompt for "
                  "this one — the app is already listed in Settings > Privacy & "
                  "Security > Full Disk Access, and only needs switching on.");
    });

    // Wire the three inputs that decide whether we draw at all.
    // Applied on every change AND once at startup. PowerPolicy only
    // signals when the outcome actually moves, so nothing here ran until
    // the first transition — the app began life tracking the Dock at
    // frame rate no matter what state it was really in.
    auto applyPower = [&] {
        fx->setFrameIntervalMs(power.frameIntervalMs());
        // Stop tracking the Dock at frame rate when nothing is drawn.
        // This is the difference between "not rendering" and "not costing
        // anything": the AX poll ran at 60Hz whatever the swarm was doing,
        // which is the whole of the residual cost in the shooed-away
        // state.
        target->setAnimating(power.shouldRender());
        if (win)
            win->setVisible(power.shouldRender());
#if defined(Q_OS_MACOS)
        // Without this the swarm stops a few seconds after another app
        // takes focus: we're a background agent, which is exactly what
        // App Nap throttles. Held only while animating.
        setAnimationActivity(power.shouldRender());
#endif
    };
    QObject::connect(&power, &PowerPolicy::changed, fx, applyPower);
    QObject::connect(fx, &EffectItem::becameEmpty, &power,
                     [&] { power.setEffectIdle(true); });

    // Fullness comes from the SETTINGS, not straight from the bin: the
    // three fixed densities deliberately ignore what's actually in there,
    // and only the relative mode looks at the trash at all.
    auto applyDensity = [&] {
        const int n = target->itemCount();
        fx->setFullness(n == 0 ? 0.0
                                : settings.fullnessFor(target->byteSize(), n));
    };
    QObject::connect(&settings, &Settings::appearanceChanged, fx, applyDensity);
    QObject::connect(&settings, &Settings::infestationChanged, fx,
                     [&](const QString &id) {
        fx->setEffectId(id);
        // A new effect wants its own room around the icon; re-apply the
        // geometry rather than leaving it sized for the old one.
        const QRect ir = target->iconRect();
        if (!ir.isEmpty())
            emit target->iconRectChanged(ir);
    });
    fx->setEffectId(settings.infestation());
    QObject::connect(target.get(), &TrashTarget::byteSizeChanged, fx,
                     [&](qint64) { applyDensity(); });

    // Pointer on the bin: the effect reacts — the swarm scatters — and
    // then rendering stops dead until it leaves. EffectItem keeps one
    // coarse timer alive to notice.
    QObject::connect(fx, &EffectItem::dismissedChanged, &power,
                     [&](bool s) { power.setDismissed(s); });

    QObject::connect(target.get(), &TrashTarget::itemCountChanged, fx, [&](int n) {
        applyDensity();
        power.setBinEmpty(n == 0);
        // Full and empty are different artwork; refresh when it flips.
        const QRect ir = target->iconRect();
        if (!ir.isEmpty())
            fx->setBinIcon(target->iconImage(int(qMax(ir.width(), ir.height()) * 2)));
        if (n > 0)
            power.setEffectIdle(false);
    });
    QObject::connect(target.get(), &TrashTarget::iconRectChanged, fx,
                     [&](const QRect &r) {
        if (!win || windowed)
            return;
        // Overlay = icon plus flight margin, scaled with the icon so a
        // magnified Dock tile doesn't clip the swarm. Asymmetric: flies
        // rise well above the bin but barely stray to either side.
        // Room comes from the EFFECT, not from the fly simulation: each
        // one needs a different shape of space around the icon, and an
        // overlay sized for the wrong one clips at the window edge.
        const qreal d      = qMax(r.width(), r.height());
        const QMargins mgn = fx->margins(d);
        win->setGeometry(r.adjusted(-mgn.left(), -mgn.top(),
                                    mgn.right(), mgn.bottom()));
        fx->setBinRect(QRectF(mgn.left(), mgn.top(), r.width(), r.height()));
        fx->setSize(QSizeF(win->width(), win->height()));
#if defined(Q_OS_WIN)
        // Test occlusion at the bin itself, not at the overlay's corner:
        // the margins reach well above the icon, and a window covering
        // only the flight room is not covering the bin.
        desktop.setPoint(r.center());
#endif

        // Flies pass behind the bin: our own copy of the Trash artwork is
        // composited over the swarm. This only works because iconRect()
        // now reports the artwork's true bounds rather than the
        // Accessibility hit area — see visualIconRect().
        fx->setBinIcon(target->iconImage(int(qMax(r.width(), r.height()) * 2)));
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
                  fx->fullness(), fx->frameIntervalMs(), power.shouldRender());
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

    // Nothing is drawing, and why. The alternative is a console warning
    // nobody sees, in an app whose only UI is this menu.
    //
    // Windows differs from macOS in a way worth saying out loud here:
    // there is no permission to grant. A hidden Recycle Bin is a setting,
    // so the app can go from broken to working without a restart or a
    // system prompt — the fix is one checkbox, and this is the line that
    // opens the dialog holding it.
    auto refreshProblem = [&] {
        switch (target->status()) {
        case TrashTarget::Status::Ok:
            tray.setProblem(QString(), false);
            break;
        case TrashTarget::Status::PermissionRequired:
        case TrashTarget::Status::DiskAccessRequired:
            // Left to the permissions entry, which carries the warning
            // sign and opens the window listing every permission at once.
            // Saying it twice, two lines apart, reads as two problems.
            tray.setProblem(QString(), false);
            break;
        case TrashTarget::Status::IconHidden:
#if defined(Q_OS_WIN)
            tray.setProblem(QStringLiteral("Recycle Bin is hidden — show it"), true);
#else
            // An auto-hidden Dock isn't broken, and there is no settings
            // pane that would "fix" it, so this one only explains itself.
            tray.setProblem(QStringLiteral("Trash icon is off screen"), false);
#endif
            break;
        case TrashTarget::Status::NotFound:
            tray.setProblem(QStringLiteral("Can't find the trash icon"), false);
            break;
        }
    };
    QObject::connect(target.get(), &TrashTarget::statusChanged, &tray,
                     [&](TrashTarget::Status) { refreshProblem(); });

    // --- onboarding -------------------------------------------------------
    // Shown when something the app needs is switched off, and reachable
    // afterwards from the menu bar. macOS cannot prompt for Full Disk
    // Access at all, so a window that explains what is needed and opens
    // the right pane is the entire remedy available.
    PermissionBridge permBridge(target.get(), &engine);
    engine.rootContext()->setContextProperty(QStringLiteral("permissionBridge"),
                                             &permBridge);
    permBridge.publish();
    QQmlComponent permComponent(&engine,
                                QUrl(QStringLiteral(
                                    "qrc:/qt/qml/hyperbin/qml/Permissions.qml")));
    QPointer<QQuickWindow> permWindow;
    auto showPermissions = [&] {
        permBridge.publish();
        if (!permWindow) {
            permWindow = qobject_cast<QQuickWindow *>(permComponent.create());
            if (!permWindow) {
                qWarning("hyperbin: permissions window failed to load: %s",
                         qPrintable(permComponent.errorString()));
                return;
            }
        }
        permWindow->show();
        permWindow->raise();
        permWindow->requestActivate();
#if defined(Q_OS_MACOS)
        // An accessory app has no Dock icon and is not in the activation
        // order, so the window would otherwise open behind whatever the
        // user was doing. requestActivate() alone cannot fix that.
        activateApp();
#endif
    };
    auto refreshPermissionEntry = [&] {
        const QList<TrashTarget::Permission> ps = target->permissions();
        int missing = 0;
        for (const TrashTarget::Permission &p : ps)
            if (!p.granted)
                ++missing;
        tray.setPermissionState(int(ps.size()), missing);
    };
    QObject::connect(target.get(), &TrashTarget::permissionsChanged, &app, [&] {
        permBridge.publish();
        refreshPermissionEntry();
    });
    // Ask the OS before deciding, rather than reading the cache.
    //
    // The cache is populated by the target's own start-up probe, which
    // happens further down — so at this point both flags are still at
    // their "not yet tested" default of false. Trusting them put the
    // window up on every launch, granted or not, and then closed it
    // again a moment later when the real answer arrived. A window that
    // appears and vanishes is worse than one that never appears: it
    // reads as a fault.
    target->refreshPermissions();
    refreshPermissionEntry();
    bool permissionsMissing = false;
    for (const TrashTarget::Permission &p : target->permissions())
        if (!p.granted)
            permissionsMissing = true;

    // --- the brand moment -------------------------------------------------
    // Every launch, not just the first. This is a menu-bar agent: it is
    // started once at login and then not thought about again, so "once
    // ever" meant the introduction happened on a machine's first boot
    // after install and never again on any of the hundreds after it.
    // Four seconds of logo at login is the whole reason the app is free.
    //
    // Including the launches where a permission is missing. It is
    // tempting to skip it there and get the user straight to the thing
    // they have to do, but the permissions window already waits for the
    // splash to leave, so the cost is four seconds, and this is the one
    // window the app exists to show.
    QPointer<QQuickWindow> splashWindow;
    QQmlComponent splashComponent(
        &engine, QUrl(QStringLiteral("qrc:/qt/qml/hyperbin/qml/Splash.qml")));
    // dwellMs of 0 means "stay until clicked" — see qml/Splash.qml. The
    // automatic version is for the introduction nobody asked for; the
    // manual one is for someone who went looking, and taking it away from
    // them on a timer would be rude.
    auto showSplash = [&](int dwellMs) {
        if (!splashWindow)
            splashWindow = qobject_cast<QQuickWindow *>(splashComponent.create());
        if (!splashWindow) {
            qWarning("hyperbin: splash failed to load: %s",
                     qPrintable(splashComponent.errorString()));
            return;
        }
        splashWindow->setProperty("dwellMs", dwellMs);
        // Centred by hand: a frameless splash has no window manager
        // placement to inherit, so it would otherwise land wherever the
        // platform felt like putting it. Recomputed each time, because
        // the display it should appear on may have changed since.
        if (QScreen *sc = QGuiApplication::primaryScreen()) {
            const QRect a = sc->availableGeometry();
            splashWindow->setPosition(a.center()
                                      - QPoint(splashWindow->width() / 2,
                                               splashWindow->height() / 2));
        }
        QMetaObject::invokeMethod(splashWindow, "appear");
#if defined(Q_OS_MACOS)
        // Same reason as the permissions window: an accessory app is not
        // in the activation order, so a window it opens lands behind
        // whatever the user was looking at. Declared in MacOverlay.h,
        // which only compiles on Darwin — hence the guard.
        activateApp();
#endif
    };
    showSplash(4000);
    // The permissions window comes after, never over. They would stack,
    // and the point of a brand moment is that it is the only thing on
    // screen.
    if (permissionsMissing) {
        if (splashWindow && splashWindow->isVisible())
            QObject::connect(splashWindow, &QWindow::visibleChanged, &app,
                             [showPermissions](bool vis) {
                                 if (!vis)
                                     showPermissions();
                             });
        else
            showPermissions();
    }
    // The menu bar's problem line opens the window rather than jumping
    // straight to a settings pane: when two permissions are missing, a
    // single jump can only ever address one of them.
    QObject::connect(&tray, &TrayMenu::remediationRequested, &app,
                     [&] { showPermissions(); });
    // Asked for, so it waits to be dismissed rather than timing out.
    QObject::connect(&tray, &TrayMenu::splashRequested, &app,
                     [&] { showSplash(0); });

    // --- updates ----------------------------------------------------------
    // Null on a platform with no updater, or a build made without one.
    // That is a supported configuration rather than a failure: the menu
    // entry stays hidden and nothing else changes.
    //
    // Parented to `app`, so the updater outlives every window and is torn
    // down last — Sparkle's controller wants to still exist when the
    // installer relaunches us.
    AppUpdater *updater = AppUpdater::create(&app);
    if (updater) {
        tray.setUpdateState(true, updater->canCheckForUpdates());
        QObject::connect(updater, &AppUpdater::canCheckForUpdatesChanged, &tray,
                         [&tray](bool can) { tray.setUpdateState(true, can); });
        QObject::connect(&tray, &TrayMenu::updateCheckRequested, updater,
                         &AppUpdater::checkForUpdates);
        updater->setAutomaticChecksEnabled(true);
    }

    // --- analytics --------------------------------------------------------
    // Null when no app key was compiled in, which is every local build.
    // Opt-in: created here, but it sends nothing at all until the menu
    // switch is turned on, and the switch starts off.
    Analytics *analytics = Analytics::create(&app);
    if (analytics) {
        tray.setAnalyticsState(true, analytics->enabled());
        QObject::connect(&tray, &TrayMenu::analyticsToggled, analytics,
                         [analytics](bool on) { analytics->setEnabled(on); });
        // Describe the install once it is allowed to be described, which
        // is both at startup (if already opted in) and at the moment
        // somebody opts in mid-session.
        auto describe = [analytics, &settings] {
            analytics->property(Prop::kVersion, QStringLiteral(HYPERBIN_VERSION));
            analytics->property(Prop::kOs, QSysInfo::productType());
            analytics->property(Prop::kOsVersion, QSysInfo::productVersion());
            analytics->property(Prop::kEffect, settings.infestation());
            analytics->event(Ev::kLaunch);
        };
        QObject::connect(analytics, &Analytics::enabledChanged, &app,
                         [describe](bool on) { if (on) describe(); });
        if (analytics->enabled())
            describe();

        // Which effect people actually pick, and how they tune it. All
        // of this is a no-op while opted out — the checks live in the
        // service, so there is no path where a caller has to remember.
        QObject::connect(&settings, &Settings::infestationChanged, analytics,
                         [analytics](const QString &id) {
                             analytics->event(Ev::kEffect, {{"effect", id}});
                             analytics->property(Prop::kEffect, id);
                         });
        QObject::connect(&settings, &Settings::enabledChanged, analytics,
                         [analytics](bool on) {
                             analytics->event(Ev::kEnabled,
                                              {{"on", on ? "1" : "0"}});
                         });
        // Emptying the bin is the thing the app is nagging about, so it
        // is the one number worth having. A count, never a content.
        QObject::connect(target.get(), &TrashTarget::itemCountChanged, analytics,
                         [analytics, prev = -1](int n) mutable {
                             const int was = prev;
                             prev = n;
                             if (was > 0 && n == 0)
                                 analytics->event(Ev::kEmptied);
                         });
    }

    // --- master switch ----------------------------------------------------
    // Off means genuinely off: no frames, no overlay surface, and the
    // trash poll stopped as well. Leaving the poll running would be a
    // background app doing work the user has just told it not to do.
    auto applyEnabled = [&](bool on) {
        power.setEnabled(on);
        if (on) {
            target->start();
            power.setBinEmpty(target->itemCount() == 0);
            applyTargetVisible();
        } else {
            target->stop();
            if (win)
                win->setVisible(false);
        }
#if defined(Q_OS_WIN)
        // Off means off: the occlusion hook and its poll go too, rather
        // than sitting there answering a question nothing is asking.
        desktop.setEnabled(on);
#endif
        refreshStatus();
        refreshProblem();
    };
    QObject::connect(&settings, &Settings::enabledChanged, &app,
                     [&](bool on) { applyEnabled(on); });

    // HYPERBIN_FORCE_SCATTER=1 pins the app in the shooed-away state so
    // its residual cost can be measured without having to park a pointer
    // on the Dock for the length of a powermetrics run.
    if (qEnvironmentVariableIntValue("HYPERBIN_FORCE_SCATTER") == 1) {
        qInfo("hyperbin: forced into the scattered state for measurement");
        power.setDismissed(true);
    }
    power.setEffectIdle(true);
    applyEnabled(settings.enabled());
    applyPower();

    return app.exec();
}

#include "main.moc"
