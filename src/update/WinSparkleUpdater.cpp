#include "WinSparkleUpdater.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>

#include <winsparkle.h>

namespace hyperbin {

WinSparkleUpdater *WinSparkleUpdater::s_instance = nullptr;

WinSparkleUpdater::WinSparkleUpdater(QObject *parent)
    : AppUpdater(parent)
{
    s_instance = this;

    // The same display-versus-comparison split macOS gets from
    // CFBundleShortVersionString and CFBundleVersion:
    //   set_app_details      → the string in "you have X", human-readable
    //   set_app_build_version→ the value compared to <sparkle:version>
    // Passing only the first would make WinSparkle compare marketing
    // strings, and "0.1.0" never sorts above "0.1.0".
    win_sparkle_set_app_details(L"Hypernuclear", L"hyperbin",
                                QStringLiteral(HYPERBIN_VERSION).toStdWString().c_str());
    win_sparkle_set_app_build_version(
        QStringLiteral(HYPERBIN_VERSION_BUILD).toStdWString().c_str());

    win_sparkle_set_appcast_url(HYPERBIN_APPCAST_URL);
    win_sparkle_set_eddsa_public_key(HYPERBIN_EDDSA_PUBKEY);

    // Without a shutdown callback WinSparkle kills the process to swap
    // the binary, and the overlay window and tray icon are torn down by
    // the OS rather than by us. Routing it through quit() lets the
    // normal teardown run.
    win_sparkle_set_can_shutdown_callback(onCanShutdown);
    win_sparkle_set_shutdown_request_callback(onShutdownRequest);

    win_sparkle_init();
}

WinSparkleUpdater::~WinSparkleUpdater()
{
    win_sparkle_cleanup();
    s_instance = nullptr;
}

// WinSparkle asks before it starts a check, not while one is running, so
// there is no state here to report — unlike Sparkle, whose controller
// genuinely does refuse re-entry.
bool WinSparkleUpdater::canCheckForUpdates() const { return true; }

void WinSparkleUpdater::checkForUpdates() { win_sparkle_check_update_with_ui(); }

void WinSparkleUpdater::setAutomaticChecksEnabled(bool enabled)
{
    win_sparkle_set_automatic_check_for_updates(enabled ? 1 : 0);
}

int WinSparkleUpdater::onCanShutdown() { return 1; }

void WinSparkleUpdater::onShutdownRequest()
{
    // Called on WinSparkle's own thread. quit() has to happen on the one
    // that owns the event loop.
    QMetaObject::invokeMethod(qApp, [] { qApp->quit(); }, Qt::QueuedConnection);
}

AppUpdater *AppUpdater::create(QObject *parent)
{
    return new WinSparkleUpdater(parent);
}

} // namespace hyperbin
