#include "TrayMenu.h"

#include "../core/EffectRegistry.h"
#include "../platform/LaunchAtLogin.h"
#include "Settings.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QStyleHints>
#include <QSvgRenderer>

namespace hyperbin {

namespace {

/// HEIGHTS the menu-bar icon is rendered at. macOS asks for @1x and @2x;
/// giving both means the shell never has to scale a bitmap.
///
/// Heights, not sizes: the shell scales the image to the bar's thickness
/// and the width follows, so height is the only dimension that decides
/// how big the mark looks.
constexpr int kIconPx[] = {22, 44, 66};

/// Padding around the glyph, as a fraction of the icon box. The artwork
/// is cropped to its own ink first (see trayIcon), so this is the only
/// empty space in the image and the mark fills the rest.
constexpr qreal kInset = 0.02;

} // namespace

TrayMenu::TrayMenu(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = std::make_unique<QSystemTrayIcon>(trayIcon(), this);
    m_tray->setToolTip(QStringLiteral("hyperbin"));
    build();
    connect(m_menu.get(), &QMenu::aboutToShow, this, [this] {
        // The system owns this setting — the user can turn it off in
        // System Settings, or delete the Startup shortcut — so it is read
        // fresh on every open rather than cached.
        if (m_login) {
            QSignalBlocker b(m_login);
            m_login->setChecked(launchAtLogin::isEnabled());
        }
    });
    m_tray->setContextMenu(m_menu.get());
    m_tray->show();

    // Repaint the glyph when the system flips light/dark. Redundant on
    // macOS, where the template flag already handles it, but harmless
    // there and it is the whole mechanism on Windows and Linux.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) {
        if (m_tray)
            m_tray->setIcon(trayIcon());
    });

    connect(m_settings, &Settings::enabledChanged, this,
            [this] { syncFromSettings(); });
    connect(m_settings, &Settings::appearanceChanged, this,
            [this] { syncFromSettings(); });
}

TrayMenu::~TrayMenu() = default;

bool TrayMenu::available() const
{
    return m_tray != nullptr;
}

QIcon TrayMenu::trayIcon()
{
    // Same artwork the QML component draws — qml/BinGlyph.qml and this
    // both come from resources/hyperbin.svg (see scripts/gen_glyph.py),
    // so the mark can't drift between the menu bar and any in-app UI.
    QSvgRenderer r(QStringLiteral(":/icons/hyperbin.svg"));
    if (!r.isValid()) {
        // A tray item with no icon is invisible on macOS, which looks
        // exactly like the app failing to start. Say which it is.
        qWarning("hyperbin: menu-bar icon failed to load from :/icons");
        return QIcon();
    }

    // Where the ink actually is inside the viewBox. The artwork does not
    // fill its own 1200x1200 box, so rendering the box into the icon
    // produced a small mark floating in empty space — it read as a tray
    // icon half the size of everything else in the bar. Measured once
    // from a high-resolution render rather than assumed, so retouching
    // the SVG can't quietly reintroduce the padding.
    static const QRectF ink = [&r] {
        constexpr int probe = 256;
        QImage img(probe, probe, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter pp(&img);
        r.render(&pp, QRectF(0, 0, probe, probe));
        pp.end();
        int x0 = probe, y0 = probe, x1 = -1, y1 = -1;
        for (int y = 0; y < probe; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < probe; ++x) {
                if (qAlpha(row[x]) > 8) {
                    x0 = qMin(x0, x); x1 = qMax(x1, x);
                    y0 = qMin(y0, y); y1 = qMax(y1, y);
                }
            }
        }
        if (x1 < x0 || y1 < y0)
            return QRectF(0, 0, 1, 1);            // nothing drawn; use the box
        const QRectF vb = r.viewBoxF();
        const qreal sx = vb.width() / probe, sy = vb.height() / probe;
        return QRectF(vb.x() + x0 * sx, vb.y() + y0 * sy,
                      (x1 - x0 + 1) * sx, (y1 - y0 + 1) * sy);
    }();

    const QColor fill = glyphColor();
    QIcon icon;
    // The pixmap takes the INK's aspect rather than being square.
    //
    // A square one wastes whichever dimension the mark is shorter in, and
    // the shell scales the whole image to the bar's thickness — so a mark
    // wider than it is tall ended up drawn at its own aspect inside a
    // square that was itself scaled down to fit, losing height twice. The
    // bin mark is about 1.15:1, which was costing it a seventh of its
    // size against nothing but transparent pixels.
    const qreal aspect = ink.width() / ink.height();
    for (int px : kIconPx) {
        const int w = qMax(1, qRound(px * aspect));
        QPixmap pm(w, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Scale the INK to fill the icon, preserving its aspect ratio,
        // instead of scaling the viewBox and inheriting its dead space.
        const qreal s = qMin(w * (1.0 - 2 * kInset) / ink.width(),
                             px * (1.0 - 2 * kInset) / ink.height());
        const QRectF box((w - ink.width() * s) / 2.0,
                         (px - ink.height() * s) / 2.0,
                         ink.width() * s, ink.height() * s);
        p.save();
        p.translate(box.topLeft());
        p.scale(s, s);
        p.translate(-ink.topLeft());
        r.render(&p, r.viewBoxF());
        p.restore();
        // Recolour through the rendered alpha. The SVG has no fill colour
        // of its own worth keeping — it is a silhouette — so painting the
        // wanted colour over it in SourceIn turns the asset into a shape
        // and leaves the colour a runtime decision.
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), fill);
        p.end();
        icon.addPixmap(pm);
    }

#if defined(Q_OS_MACOS)
    // macOS only. Since macOS 11 the menu bar's appearance follows the
    // WALLPAPER rather than the light/dark setting, so choosing a colour
    // by colour scheme is wrong here and AppKit's template mechanism is
    // right. It
    // tints the silhouette to match what the bar actually looks like,
    // including the translucent cases a colour scheme can't describe.
    icon.setIsMask(true);
#endif
    return icon;
}

QColor TrayMenu::glyphColor()
{
#if defined(Q_OS_MACOS)
    // Ignored in practice — the template flag recolours it — but black is
    // what AppKit expects to receive as the mask.
    return QColor(Qt::black);
#else
    // Windows and Linux notification areas do follow the system light or
    // dark setting, so pick the variant that will read against it.
    const bool dark = QGuiApplication::styleHints()->colorScheme()
                      == Qt::ColorScheme::Dark;
    return dark ? QColor(0xf2, 0xf2, 0xf2) : QColor(0x1c, 0x1c, 0x1e);
#endif
}

void TrayMenu::build()
{
    m_menu = std::make_unique<QMenu>();

    m_status = m_menu->addAction(QString());
    m_status->setEnabled(false);

    // Hidden until something is actually wrong. Sits directly under the
    // status line so the two read as one sentence: what is in the bin,
    // and why you cannot see anything happening at it.
    m_problem = m_menu->addAction(QString());
    m_problem->setVisible(false);
    connect(m_problem, &QAction::triggered, this, &TrayMenu::remediationRequested);

    m_menu->addSeparator();

    m_enable = m_menu->addAction(QStringLiteral("Show Animation"));
    m_enable->setCheckable(true);
    connect(m_enable, &QAction::toggled, m_settings, &Settings::setEnabled);

    // --- effect ---------------------------------------------------------
    // Built from the effect registry rather than hard-coded, so adding an
    // effect is one entry in EffectRegistry.cpp and nothing here.
    QMenu *effectMenu = m_menu->addMenu(QStringLiteral("Effect"));
    m_effectGroup = new QActionGroup(this);
    m_effectGroup->setExclusive(true);
    for (const EffectInfo &e : effects::all()) {
        auto *a = effectMenu->addAction(e.label);
        a->setCheckable(true);
        a->setData(e.id);
        m_effectGroup->addAction(a);
    }
    connect(m_effectGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_settings->setInfestation(a->data().toString());
    });

    // --- amount ---------------------------------------------------------
    // How much of whatever effect is running. Generic on purpose: every
    // effect scales with the trash, so this is a sibling of the chooser
    // rather than something nested inside each effect's own settings.
    QMenu *amount = m_menu->addMenu(QStringLiteral("Amount"));

    m_densityGroup = new QActionGroup(this);
    m_densityGroup->setExclusive(true);
    const struct { const char *label; Settings::Density d; } densities[] = {
        {"Just a Little",         Settings::Density::Few},
        {"Make It Stop",          Settings::Density::Lots},
        {"Oh the Horror!",        Settings::Density::TooMany},
        {"More Trash, More Effect", Settings::Density::Relative},
    };
    for (const auto &e : densities) {
        auto *a = amount->addAction(QString::fromLatin1(e.label));
        a->setCheckable(true);
        a->setData(int(e.d));
        m_densityGroup->addAction(a);
    }
    connect(m_densityGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_settings->setDensity(Settings::Density(a->data().toInt()));
    });

    amount->addSeparator();
    QMenu *thresh = amount->addMenu(QStringLiteral("Trash Threshold"));
    m_thresholdGroup = new QActionGroup(this);
    m_thresholdGroup->setExclusive(true);
    const struct { const char *label; Settings::Threshold t; } thresholds[] = {
        {"1 MB",    Settings::Threshold::OneMB},
        {"100 MB",  Settings::Threshold::HundredMB},
        {"1 GB",    Settings::Threshold::OneGB},
        {"5 GB+",   Settings::Threshold::FiveGB},
    };
    for (const auto &e : thresholds) {
        auto *a = thresh->addAction(QString::fromLatin1(e.label));
        a->setCheckable(true);
        a->setData(int(e.t));
        m_thresholdGroup->addAction(a);
    }
    connect(m_thresholdGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_settings->setThreshold(Settings::Threshold(a->data().toInt()));
    });

    m_menu->addSeparator();

    // Open at login. Hidden entirely where we have no implementation,
    // rather than shown as a switch that does nothing.
    if (launchAtLogin::supported()) {
        m_login = m_menu->addAction(QStringLiteral("Open at Login"));
        m_login->setCheckable(true);
        m_login->setChecked(launchAtLogin::isEnabled());
        connect(m_login, &QAction::triggered, this, [this](bool on) {
            // Reflect what the SYSTEM ended up doing, not what was asked.
            // Registering can fail — an unsigned or relocated bundle on
            // macOS, an unwritable Startup folder on Windows — and a tick
            // that stays put regardless would be lying to the user.
            const bool actual = launchAtLogin::setEnabled(on);
            QSignalBlocker b(m_login);
            m_login->setChecked(actual);
        });
    }

    // Permissions. Always here once the platform needs any, not only
    // while something is broken: a user who dismissed the window at
    // first run, or who revoked access later, needs a way back to it, and
    // hunting through System Settings is not one.
    m_perms = m_menu->addAction(QString());
    m_perms->setVisible(false);
    connect(m_perms, &QAction::triggered, this, &TrayMenu::remediationRequested);
    // The splash, on demand. It shows itself once ever, which is right
    // for an introduction and useless for anyone who wants to look at it
    // again — so this is the way back to it.
    auto *splash = m_menu->addAction(QStringLiteral("Show Splash"));
    connect(splash, &QAction::triggered, this, &TrayMenu::splashRequested);
    auto *quit = m_menu->addAction(QStringLiteral("Quit hyperbin"));
    connect(quit, &QAction::triggered, this, &TrayMenu::quitRequested);

    syncFromSettings();
}

void TrayMenu::syncFromSettings()
{
    QSignalBlocker b(m_enable);
    m_enable->setChecked(m_settings->enabled());

    auto check = [](QActionGroup *g, int value) {
        for (QAction *a : g->actions())
            a->setChecked(a->data().toInt() == value);
    };
    // An unset setting still RUNS the registry's default effect, so the
    // menu has to show that default ticked. Showing nothing selected
    // while flies are plainly on screen reads as a bug.
    QString current = m_settings->infestation();
    if (current.isEmpty() || !effects::exists(current))
        current = effects::defaultId();
    for (QAction *a : m_effectGroup->actions())
        a->setChecked(a->data().toString() == current);
    check(m_densityGroup, int(m_settings->density()));
    check(m_thresholdGroup, int(m_settings->threshold()));

    // The threshold only means anything in Relative mode, and greying it
    // out says so more clearly than a tooltip would.
    for (QAction *a : m_menu->actions()) {
        if (a->menu() && a->text() == QStringLiteral("Amount")) {
            for (QAction *sub : a->menu()->actions())
                if (sub->menu())
                    sub->setEnabled(m_settings->density()
                                    == Settings::Density::Relative);
        }
    }
}

void TrayMenu::setStatusText(const QString &text)
{
    if (m_status)
        m_status->setText(text);
}

void TrayMenu::setPermissionState(int count, int missing)
{
    if (!m_perms)
        return;
    m_perms->setVisible(count > 0);
    if (count <= 0)
        return;
    // The warning sign only when there is something to warn about. A
    // permanent alarm next to a working app teaches the user to ignore
    // it, which is the one thing this entry cannot afford.
    m_perms->setText(missing > 0 ? QStringLiteral("Grant Permissions \u26a0\ufe0f")
                                 : QStringLiteral("Show Permissions"));
}
void TrayMenu::setProblem(const QString &text, bool actionable)
{
    if (!m_problem)
        return;
    m_problem->setText(text);
    m_problem->setVisible(!text.isEmpty());
    // A greyed-out line still explains itself; it just doesn't pretend to
    // be a button when there is nothing for it to open.
    m_problem->setEnabled(actionable);
}

} // namespace hyperbin
