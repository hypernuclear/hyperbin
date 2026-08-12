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

signals:
    void quitRequested();
    /// The problem line was clicked: open whatever fixes it.
    void remediationRequested();

private:
    void build();
    void syncFromSettings();
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
    QActionGroup *m_typeGroup = nullptr;
    QActionGroup *m_densityGroup   = nullptr;
    QActionGroup *m_thresholdGroup = nullptr;
};

} // namespace hyperbin
