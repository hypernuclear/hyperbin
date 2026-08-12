#include "RecycleBinTarget.h"

#include "WinScreen.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// objbase.h first, and not for tidiness: exdisp.h declares its interfaces
// with the `interface` keyword, which is a macro defined over in
// combaseapi.h. Included the other way round, every declaration in
// exdisp.h is a syntax error about a missing semicolon.
#include <objbase.h>

#include <exdisp.h>     // IShellWindows
#include <shellapi.h>   // SHQueryRecycleBin, ShellExecuteW
#include <shlobj.h>     // SHGetKnownFolderIDList, IFolderView2
#include <shlwapi.h>
#include <shobjidl.h>

#include <climits>

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QWinEventNotifier>

namespace hyperbin {

namespace {

/// Minimal COM smart pointer.
///
/// Hand-rolled rather than <wrl/client.h>: WRL is a Microsoft header, and
/// the point of this file is that it builds under whichever toolchain the
/// kit selected — MSVC or llvm-mingw — without either one needing a
/// special case.
template <class T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ComPtr(ComPtr &&o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr &operator=(ComPtr &&o) noexcept
    {
        if (this != &o) { reset(); p = o.p; o.p = nullptr; }
        return *this;
    }

    T **operator&() { reset(); return &p; }   // for IID_PPV_ARGS
    T  *operator->() const { return p; }
    T  *get() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    void reset() { if (p) { p->Release(); p = nullptr; } }

private:
    T *p = nullptr;
};

/// Sent to our message-only window when the shell reports a change.
constexpr UINT kShellNotifyMsg = WM_APP + 1;

constexpr wchar_t kWindowClass[] = L"hyperbinRecycleBinWatcher";

/// Desktop Icon Settings writes the per-icon toggles here; the value name
/// is the Recycle Bin's CLSID and 1 means hidden.
constexpr wchar_t kHideIconsKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel";
constexpr wchar_t kRecycleBinClsid[] = L"{645FF040-5081-101B-9F08-00AA002F954E}";

/// The Recycle Bin's absolute PIDL. Caller frees with CoTaskMemFree.
PIDLIST_ABSOLUTE binPidl()
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &pidl)))
        return nullptr;
    return pidl;
}

/// The desktop's live shell view, and the window it draws into.
///
/// IShellWindows -> IFolderView2, rather than the LVM_GETITEMPOSITION
/// route every sample on the internet takes. That one needs
/// VirtualAllocEx + ReadProcessMemory inside explorer.exe, because the
/// list view belongs to another process — and we ship a signed binary to
/// strangers, where cross-process memory reads into explorer are exactly
/// the behaviour that gets an unknown download quarantined. This path is
/// documented, injection-free, and hands over the icon size as a bonus.
ComPtr<IFolderView2> desktopFolderView(HWND *viewHwnd)
{
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&windows))))
        return {};

    VARIANT empty;
    VariantInit(&empty);
    long desktopHwnd = 0;
    ComPtr<IDispatch> dispatch;
    if (FAILED(windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &desktopHwnd,
                                     SWFO_NEEDDISPATCH, &dispatch))
        || !dispatch)
        return {};

    ComPtr<IServiceProvider> provider;
    if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&provider))))
        return {};
    ComPtr<IShellBrowser> browser;
    if (FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))))
        return {};
    ComPtr<IShellView> view;
    if (FAILED(browser->QueryActiveShellView(&view)))
        return {};

    if (viewHwnd)
        view->GetWindow(viewHwnd);

    ComPtr<IFolderView2> folderView;
    if (FAILED(view->QueryInterface(IID_PPV_ARGS(&folderView))))
        return {};
    return folderView;
}

/// Position of the bin's cell within the view, in the view's own pixels.
///
/// Matched by PIDL, never by display name: "Recycle Bin" is localised, and
/// a user can rename it besides.
bool binCellPosition(IFolderView2 *view, PCUITEMID_CHILD child, POINT *out)
{
    if (SUCCEEDED(view->GetItemPosition(child, out)))
        return true;

    // The view can hold a differently-built PIDL for the same item, in
    // which case the direct lookup misses and the items have to be
    // compared properly. CompareIDs returns the result in the HRESULT's
    // code field, where 0 means "the same item".
    ComPtr<IShellFolder> desktop;
    if (FAILED(SHGetDesktopFolder(&desktop)))
        return false;

    int count = 0;
    if (FAILED(view->ItemCount(SVGIO_ALLVIEW, &count)))
        return false;
    for (int i = 0; i < count; ++i) {
        PITEMID_CHILD item = nullptr;
        if (FAILED(view->Item(i, &item)) || !item)
            continue;
        const HRESULT cmp = desktop->CompareIDs(
            SHCIDS_CANONICALONLY,
            reinterpret_cast<PCUIDLIST_RELATIVE>(item),
            reinterpret_cast<PCUIDLIST_RELATIVE>(child));
        const bool same = SUCCEEDED(cmp) && short(HRESULT_CODE(cmp)) == 0;
        const bool got  = same && SUCCEEDED(view->GetItemPosition(item, out));
        CoTaskMemFree(item);
        if (got)
            return true;
    }
    return false;
}

/// Screen rect of the ARTWORK, in physical pixels, from what
/// GetItemPosition reports.
///
/// Measured, not reasoned about: `binprobe --measure` template-matches the
/// shell's own artwork against the screen, and on a 150% display it found
/// the 72px icon at exactly the reported position + (0,5), with a mean
/// error of 0.1/255 — a pixel-exact match.
///
/// So GetItemPosition hands back the ICON's top-left, not the cell's.
/// `spacing` is the grid pitch and is wider than the icon because labels
/// are; centring the icon inside it — the obvious reading, and the first
/// thing tried — pushed the swarm half the slack (21px here) to the
/// right, which is exactly how it looked on screen. The only residual is
/// a small vertical inset, 5 physical px at 150%, modelled as ~3 logical
/// px so it tracks DPI.
///
/// HYPERBIN_BIN_ADJUST=dx,dy,scale nudges it; HYPERBIN_CALIBRATE draws
/// the result. Re-measure rather than guessing: `binprobe --measure`
/// prints the HYPERBIN_BIN_ADJUST that closes any remaining gap.
QRect visualIconRect(const POINT &cell, int iconPx, qreal scale)
{
    // Logical px, so it scales with the display like the icon does.
    constexpr double kTopInsetLogical = 3.0;

    double dx = 0.0, dy = 0.0, scaleExtra = 1.0;
    if (qEnvironmentVariableIsSet("HYPERBIN_BIN_ADJUST")) {
        const QStringList p = qEnvironmentVariable("HYPERBIN_BIN_ADJUST").split(u',');
        if (p.size() >= 1) dx = p[0].toDouble();
        if (p.size() >= 2) dy = p[1].toDouble();
        if (p.size() >= 3 && p[2].toDouble() > 0) scaleExtra = p[2].toDouble();
    }

    const double side = iconPx * scaleExtra;
    const double left = cell.x + dx;
    const double top  = cell.y + kTopInsetLogical * scale + dy;
    return QRect(qRound(left), qRound(top), qRound(side), qRound(side));
}

/// HBITMAP -> QImage, keeping the alpha channel.
///
/// The alpha IS the mask: FlyItem::rebuildSurface() turns it into the
/// walkable-surface grid, and flymask.frag clips crawlers against it. An
/// all-opaque image means flies land on the whole bounding box, which is
/// precisely the bug the macOS build already had once.
QImage imageFromBitmap(HBITMAP bitmap)
{
    BITMAP info{};
    if (!GetObject(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return {};

    QImage img(info.bmWidth, info.bmHeight, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull())
        return {};

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = info.bmWidth;
    bi.bmiHeader.biHeight      = -info.bmHeight;   // negative = top-down, like QImage
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(nullptr);
    const int copied = GetDIBits(dc, bitmap, 0, UINT(info.bmHeight), img.bits(),
                                 &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (copied == 0)
        return {};

    // Two failure modes worth naming rather than shipping as a mystery.
    bool anyAlpha = false, anyTranslucent = false;
    for (int y = 0; y < img.height() && !(anyAlpha && anyTranslucent); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a != 0)   anyAlpha = true;
            if (a != 255) anyTranslucent = true;
        }
    }
    if (!anyAlpha) {
        // A 32-bit bitmap with an entirely zero alpha channel is the
        // shell saying "I didn't fill this in", not "invisible icon".
        // Taken literally the bin would disappear, so force it opaque and
        // accept a bounding-box mask.
        qWarning("hyperbin: Recycle Bin icon came back with no alpha; the mask "
                 "will be its bounding box");
        img.reinterpretAsFormat(QImage::Format_RGB32);
        return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (!anyTranslucent)
        qWarning("hyperbin: Recycle Bin icon is fully opaque; flies will land on "
                 "the whole square rather than the artwork");
    return img;
}

} // namespace

struct RecycleBinTarget::Impl
{
    HWND        watcher   = nullptr;   // message-only window for shell events
    ULONG       notifyId  = 0;
    HKEY        hiddenKey = nullptr;
    HANDLE      hiddenEvent = nullptr;
    QWinEventNotifier *hiddenNotifier = nullptr;
    bool        comReady  = false;

    ~Impl()
    {
        if (notifyId)
            SHChangeNotifyDeregister(notifyId);
        if (watcher)
            DestroyWindow(watcher);
        delete hiddenNotifier;
        if (hiddenEvent)
            CloseHandle(hiddenEvent);
        if (hiddenKey)
            RegCloseKey(hiddenKey);
        if (comReady)
            CoUninitialize();
    }
};

namespace {

LRESULT CALLBACK watcherProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == kShellNotifyMsg) {
        auto *self = reinterpret_cast<RecycleBinTarget *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        // SHCNRF_NewDelivery hands over shared memory rather than raw
        // pointers, so the payload has to be locked even when — as here —
        // only the fact of the change matters. Skipping the unlock leaks
        // an explorer-side allocation on every trash operation.
        PIDLIST_ABSOLUTE *pidls = nullptr;
        LONG event = 0;
        if (HANDLE lock = SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wp),
                                                    DWORD(lp), &pidls, &event)) {
            SHChangeNotification_Unlock(lock);
        }
        if (self)
            QMetaObject::invokeMethod(self, [self] { self->refreshFullness(); },
                                      Qt::QueuedConnection);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

RecycleBinTarget::RecycleBinTarget(QObject *parent)
    : TrashTarget(parent), d(new Impl)
{
    m_poll.setInterval(kPollMs);
    m_poll.setTimerType(Qt::CoarseTimer);   // nothing here is frame-accurate
    connect(&m_poll, &QTimer::timeout, this, [this] {
        pollIconRect();
        queryFullness();   // cheap, and the backstop if a notification is missed
    });

    // Desktop icons move on a resolution, arrangement or DPI change and
    // essentially never otherwise, so Qt's own screen signals do the real
    // work and the poll above is only the safety net under them.
    //
    // Connected HERE rather than in start(): start() runs again every time
    // the master switch is turned back on, and Qt::UniqueConnection cannot
    // dedupe a lambda — it silently refuses the connection instead ("unique
    // connections require a pointer to member function"), which is exactly
    // how these ended up not connected at all.
    const auto follow = [this](QScreen *s) {
        connect(s, &QScreen::geometryChanged, this, [this] {
            if (m_poll.isActive())
                pollIconRect();
        });
        connect(s, &QScreen::logicalDotsPerInchChanged, this, [this] {
            if (m_poll.isActive())
                pollIconRect();
        });
    };
    for (QScreen *s : QGuiApplication::screens())
        follow(s);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, follow);

    // Qt already owns COM on the GUI thread (the platform plugin calls
    // OleInitialize), so this normally returns S_FALSE — a no-op that
    // takes a reference. It matters when the target is constructed
    // somewhere that hasn't been initialised, e.g. binprobe.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    d->comReady = SUCCEEDED(hr);
}

RecycleBinTarget::~RecycleBinTarget()
{
    delete d;
}

bool RecycleBinTarget::runningElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

void RecycleBinTarget::start()
{
    if (runningElevated()) {
        // Say it plainly. Every downstream symptom of this is "the desktop
        // has no Recycle Bin on it", which is indistinguishable from the
        // icon genuinely being switched off.
        qWarning("hyperbin: running elevated. COM to explorer.exe crosses an "
                 "integrity boundary and the desktop view cannot be found — "
                 "run hyperbin unelevated.");
        setStatus(Status::NotFound);
        return;
    }

    // A message-only window: it never paints, never appears in Alt-Tab,
    // and exists purely to receive shell notifications.
    if (!d->watcher) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = watcherProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);   // harmless if already registered
        d->watcher = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
                                     HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (d->watcher)
            SetWindowLongPtrW(d->watcher, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(this));
    }

    // Watch the bin itself rather than polling it. SHCNE_UPDATEDIR alone
    // misses the single-file cases, and SHCNE_CREATE/DELETE alone miss a
    // bulk empty, so all five are registered.
    if (d->watcher && !d->notifyId) {
        if (PIDLIST_ABSOLUTE pidl = binPidl()) {
            SHChangeNotifyEntry entry{pidl, TRUE};
            d->notifyId = SHChangeNotifyRegister(
                d->watcher,
                SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
                SHCNE_UPDATEDIR | SHCNE_CREATE | SHCNE_DELETE | SHCNE_RENAMEITEM
                    | SHCNE_UPDATEITEM,
                kShellNotifyMsg, 1, &entry);
            CoTaskMemFree(pidl);
        }
        if (!d->notifyId)
            qWarning("hyperbin: shell change notifications unavailable; falling "
                     "back to the %dms poll", kPollMs);
    }

    watchHiddenSetting();

    pollIconRect();
    queryFullness();
    m_poll.start();
}

void RecycleBinTarget::stop()
{
    m_poll.stop();
}

void RecycleBinTarget::setAnimating(bool animating)
{
    if (animating == m_animating)
        return;
    m_animating = animating;
    if (!m_poll.isActive())
        return;
    if (animating) {
        // Take a reading immediately as well: the desktop may have been
        // rearranged while idle, and waiting for the next tick would show
        // as the swarm starting up in the wrong place.
        m_poll.setInterval(kPollMs);
        pollIconRect();
    } else {
        m_poll.setInterval(kIdlePollMs);
    }
}

void RecycleBinTarget::setStatus(Status s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged(s);
}

void RecycleBinTarget::refreshFullness()
{
    queryFullness();
}

void RecycleBinTarget::queryFullness()
{
    SHQUERYRBINFO info{};
    info.cbSize = sizeof(info);
    // nullptr = every drive's bin, which is what the desktop icon shows.
    if (FAILED(SHQueryRecycleBin(nullptr, &info)))
        return;

    const qint64 bytes = qint64(info.i64Size);
    const int    count = int(qMin<qint64>(info.i64NumItems, INT_MAX));

    if (bytes != m_bytes) {
        m_bytes = bytes;
        emit byteSizeChanged(bytes);
    }
    if (count == m_count)
        return;
    qInfo("hyperbin: recycle bin -> %d item(s), %lld byte(s)", count,
          (long long)bytes);
    m_count = count;
    emit itemCountChanged(count);
}

bool RecycleBinTarget::hiddenByDesktopIconSetting()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kHideIconsKey, 0, KEY_READ, &key)
        != ERROR_SUCCESS)
        return false;   // no key at all means nothing has been hidden
    DWORD value = 0, size = sizeof(value), type = 0;
    const LONG r = RegQueryValueExW(key, kRecycleBinClsid, nullptr, &type,
                                    reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_DWORD && value == 1;
}

void RecycleBinTarget::watchHiddenSetting()
{
    if (d->hiddenKey)
        return;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kHideIconsKey, 0, KEY_READ | KEY_NOTIFY,
                      &d->hiddenKey) != ERROR_SUCCESS)
        return;

    d->hiddenEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!d->hiddenEvent)
        return;

    // Unlike macOS there is no permission to grant here — the user flips a
    // setting and the app should simply start working. Waiting for a
    // restart to notice would be the same miserable first run the
    // Accessibility watcher exists to avoid on the other platform.
    const auto arm = [this] {
        RegNotifyChangeKeyValue(d->hiddenKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
                                d->hiddenEvent, TRUE);
    };
    d->hiddenNotifier = new QWinEventNotifier(d->hiddenEvent);
    connect(d->hiddenNotifier, &QWinEventNotifier::activated, this,
            [this, arm](HANDLE) {
        pollIconRect();
        arm();   // one-shot registration; re-arm for the next change
    });
    arm();
}

void RecycleBinTarget::pollIconRect()
{
    if (hiddenByDesktopIconSetting()) {
        setStatus(Status::IconHidden);
        return;
    }

    HWND viewHwnd = nullptr;
    ComPtr<IFolderView2> view = desktopFolderView(&viewHwnd);
    if (!view) {
        setStatus(Status::NotFound);
        return;
    }

    // "Show desktop icons" off leaves the view alive but not visible —
    // there is no separate setting to read, the window simply isn't shown.
    if (viewHwnd && !IsWindowVisible(viewHwnd)) {
        setStatus(Status::IconHidden);
        return;
    }

    PIDLIST_ABSOLUTE pidl = binPidl();
    if (!pidl) {
        setStatus(Status::NotFound);
        return;
    }
    POINT cell{};
    const bool found = binCellPosition(view.get(), ILFindLastID(pidl), &cell);
    CoTaskMemFree(pidl);
    if (!found) {
        setStatus(Status::IconHidden);
        return;
    }

    FOLDERVIEWMODE viewMode = FVM_AUTO;
    int            iconPx   = 0;
    if (FAILED(view->GetViewModeAndIconSize(&viewMode, &iconPx)) || iconPx <= 0) {
        setStatus(Status::NotFound);
        return;
    }
    // Only used for the log line now that the rect no longer centres the
    // icon in it, but it is the first thing worth seeing when the layout
    // looks wrong.
    POINT spacing{};
    if (FAILED(view->GetSpacing(&spacing)))
        spacing = POINT{iconPx, iconPx};

    // View coordinates -> physical screen, then physical -> DIP. Both
    // steps are needed: the shell speaks the monitor's real pixels while
    // iconRect() is contracted to hand back Qt's.
    POINT origin = cell;
    if (viewHwnd)
        ClientToScreen(viewHwnd, &origin);

    // Positions and spacing come back in physical pixels; the icon SIZE
    // does not. GetViewModeAndIconSize reports the shell's logical image
    // size — the 16/32/48/96/256 family — so at 150% the desktop draws a
    // "48px" icon 72 real pixels wide.
    //
    // Both symptoms of taking it literally point the same way and were
    // seen on screen: the swarm scaled to a two-thirds-size bin, and it
    // sat too far right, because centring a 48-wide icon in a 115-wide
    // cell puts its left edge 12px right of where a 72-wide one starts.
    // Cross-checked against the layout: 72 in a 115 cell is the same 63%
    // fill Windows uses at 100%, where 48 sits in a 77-wide cell.
    const qreal scale = scaleAtPhysical(QPoint(origin.x, origin.y));
    const int   iconPhysical = qMax(1, qRound(iconPx * scale));
    const QRect physical = visualIconRect(origin, iconPhysical, scale);

    const QPoint topLeft = physicalToDip(physical.topLeft());
    const QRect  r(topLeft, QSize(qMax(1, int(physical.width() / scale)),
                                  qMax(1, int(physical.height() / scale))));

    const Status was = m_status;
    setStatus(Status::Ok);
    if (was != m_status)
        qInfo("hyperbin: recycle bin at (%d,%d %dx%d) icon=%dpx spacing=%dx%d",
              r.x(), r.y(), r.width(), r.height(), iconPx, int(spacing.x),
              int(spacing.y));
    if (r != m_rect) {
        m_rect = r;
        emit iconRectChanged(r);
    }
}

QImage RecycleBinTarget::iconImage(int px) const
{
    PIDLIST_ABSOLUTE pidl = binPidl();
    if (!pidl)
        return {};

    // The shell's own artwork, at the size we ask for, in the right
    // full/empty state — the same image the desktop is drawing, which is
    // what makes our copy composite invisibly over it.
    ComPtr<IShellItemImageFactory> factory;
    const HRESULT hr = SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory));
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !factory)
        return {};

    HBITMAP bitmap = nullptr;
    const SIZE size{px, px};
    if (FAILED(factory->GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK,
                                 &bitmap)))
        return {};

    QImage img = imageFromBitmap(bitmap);
    DeleteObject(bitmap);
    if (img.isNull())
        return {};

    // BIGGERSIZEOK can hand back a larger icon than asked for. The image
    // and the rect it is mapped onto have to describe the same square, so
    // resize rather than letting the caller stretch it unevenly.
    if (img.width() != px || img.height() != px)
        img = img.scaled(px, px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return img;
}

void RecycleBinTarget::openRemediation()
{
    // Desktop Icon Settings — the dialog with the Recycle Bin checkbox in
    // it. Nothing here prompts for a permission, so this is the whole of
    // the fix: the user ticks a box and the registry watcher picks it up.
    ShellExecuteW(nullptr, L"open", L"rundll32.exe",
                  L"shell32.dll,Control_RunDLL desk.cpl,,0", nullptr,
                  SW_SHOWNORMAL);
}

} // namespace hyperbin
