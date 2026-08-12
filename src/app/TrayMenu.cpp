#include "TrayMenu.h"

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

/// Sizes the menu-bar icon is rendered at. macOS asks for @1x and @2x;
/// giving both means the shell never has to scale a bitmap.
constexpr int kIconPx[] = {18, 36, 54};

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

    const QColor fill = glyphColor();
    QIcon icon;
    for (int px : kIconPx) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        // A hair of padding: the artwork runs to the edge of its viewBox
        // and menu-bar items look cramped without it.
        const qreal inset = px * 0.06;
        r.render(&p, QRectF(inset, inset, px - 2 * inset, px - 2 * inset));
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
    // WALLPAPER rather than the light/dark setting — hypershot hit this
    // and landed on the same answer — so choosing a colour by colour
    // scheme is wrong here, and AppKit's template mechanism is right. It
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

    m_menu->addSeparator();

    m_enable = m_menu->addAction(QStringLiteral("Show Animation"));
    m_enable->setCheckable(true);
    connect(m_enable, &QAction::toggled, m_settings, &Settings::setEnabled);

    // --- type ---------------------------------------------------------
    // One entry today. It exists as a submenu now so that adding a second
    // animation is a new action rather than a new menu structure — and so
    // the settings below it are visibly scoped to the chosen type.
    QMenu *type = m_menu->addMenu(QStringLiteral("Type"));
    m_typeGroup = new QActionGroup(this);
    m_typeGroup->setExclusive(true);
    auto *flies = type->addAction(QStringLiteral("Flies"));
    flies->setCheckable(true);
    flies->setData(int(Settings::Type::Flies));
    m_typeGroup->addAction(flies);
    connect(m_typeGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_settings->setType(Settings::Type(a->data().toInt()));
    });

    // --- settings for the selected type -------------------------------
    QMenu *fly = m_menu->addMenu(QStringLiteral("Flies"));

    m_densityGroup = new QActionGroup(this);
    m_densityGroup->setExclusive(true);
    const struct { const char *label; Settings::Density d; } densities[] = {
        {"A Few Flies",              Settings::Density::Few},
        {"Lots of Flies",            Settings::Density::Lots},
        {"Too Many Flies!",          Settings::Density::TooMany},
        {"Flies Relative to Trash Size", Settings::Density::Relative},
    };
    for (const auto &e : densities) {
        auto *a = fly->addAction(QString::fromLatin1(e.label));
        a->setCheckable(true);
        a->setData(int(e.d));
        m_densityGroup->addAction(a);
    }
    connect(m_densityGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_settings->setDensity(Settings::Density(a->data().toInt()));
    });

    fly->addSeparator();
    QMenu *thresh = fly->addMenu(QStringLiteral("Trash Threshold"));
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
    check(m_typeGroup, int(m_settings->type()));
    check(m_densityGroup, int(m_settings->density()));
    check(m_thresholdGroup, int(m_settings->threshold()));

    // The threshold only means anything in Relative mode, and greying it
    // out says so more clearly than a tooltip would.
    for (QAction *a : m_menu->actions()) {
        if (a->menu() && a->text() == QStringLiteral("Flies")) {
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

} // namespace hyperbin
