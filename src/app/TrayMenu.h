// The menu-bar / notification-area item and everything hanging off it.
//
// QSystemTrayIcon rather than a native NSStatusItem: the menu is plain
// and the cross-platform version behaves correctly in both shells, so
// there's nothing to be gained from writing it twice.
#pragma once

#include <QAction>
#include <QColor>
#include <QObject>
#include <QSystemTrayIcon>
#include <memory>

class QActionGroup;
class QMenu;

namespace hyperbin {

class Settings;

class TrayMenu : public QObject
{
    Q_OBJECT
public:
    TrayMenu(Settings *settings, QObject *parent = nullptr);
    ~TrayMenu() override;

    bool available() const;

    /// Text shown under the type submenu, e.g. how much is in the bin.
    void setStatusText(const QString &text);

    /// Say what is wrong and, when `actionable`, offer to fix it. Empty
    /// text hides the line entirely.
    ///
    /// A background agent with no window has nowhere else to say "I can
    /// see the trash but I can't draw at it" — without this the app is
    /// simply, silently, doing nothing.
    void setProblem(const QString &text, bool actionable);
    /// Show the permissions entry, and say whether anything is still
    /// switched off. Called with `count == 0` on platforms that need no
    /// permissions at all, which hides the entry outright rather than
    /// offering a window listing nothing.
    void setPermissionState(int count, int missing);

    /// Show the "Check for Updates" entry, and whether it can be used
    /// right now. Never called on a build without an updater, which
    /// leaves the entry hidden — an update check that cannot happen is
    /// worse than no menu item.
    void setUpdateState(bool available, bool canCheck);

    /// Show the analytics switch, and where it currently sits. Hidden on
    /// a build with no analytics backend.
    void setAnalyticsState(bool available, bool on);

signals:
    void quitRequested();
    /// The problem line, or the permissions entry, was clicked.
    void remediationRequested();
    /// "Show Splash" was chosen.
    void splashRequested();
    /// "Check for Updates…" was chosen.
    void updateCheckRequested();
    /// "Share Usage Data" was ticked or unticked.
    void analyticsToggled(bool on);
    /// "Open at Login" was toggled, carrying what the SYSTEM ended up
    /// doing rather than what was asked — registering can fail, and an
    /// event recording the request would quietly overcount.
    void loginItemToggled(bool on);

private:
    void build();
    void syncFromSettings();
    /// Hide separators that have nothing left to separate.
    ///
    /// Most entries here can be absent — no analytics backend, no
    /// login-item support, no permissions to grant, no updater — and a
    /// separator does not disappear just because everything around it
    /// did. Without this, a Linux build shows a rule floating above Quit
    /// with an empty group above it. Re-run whenever visibility changes.
    void tidySeparators();
    /// The template icon, rendered from the bundled SVG. Menu-bar icons
    /// are monochrome masks on macOS, so the SVG's colour is irrelevant
    /// and only its silhouette matters.
    static QIcon trayIcon();
    /// Fill for the glyph. Only meaningful where the shell does NOT
    /// recolour a template image for us — see trayIcon().
    static QColor glyphColor();

    Settings *m_settings = nullptr;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu>           m_menu;
    QAction      *m_enable    = nullptr;
    QAction      *m_login     = nullptr;
    QAction      *m_status    = nullptr;
    QAction      *m_problem   = nullptr;
    QAction      *m_perms     = nullptr;
    QAction      *m_update    = nullptr;
    QAction      *m_analytics = nullptr;
    QActionGroup *m_effectGroup = nullptr;
    QActionGroup *m_lowPowerGroup = nullptr;
    QActionGroup *m_densityGroup   = nullptr;
    QActionGroup *m_thresholdGroup = nullptr;
};

} // namespace hyperbin
