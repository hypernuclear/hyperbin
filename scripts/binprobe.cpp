// Dump what the shell says about the Recycle Bin, once a second, so the
// icon rect can be calibrated against something real instead of guessed.
//
// The macOS equivalent (dockprobe.mm) was written after a day of fighting
// the Dock rect by eye; this one exists before the same fight starts. It
// deliberately duplicates RecycleBinTarget's lookups rather than linking
// it: the point is to see the RAW numbers — cell position, spacing, icon
// size, and the DPI scale — separately from the model built on top of
// them, so a wrong rect can be blamed on the right one of the two.
//
// Build (from a Developer Command Prompt, or any shell with cl on PATH):
//   cl /nologo /EHsc /std:c++17 scripts\binprobe.cpp /link ole32.lib ^
//      oleaut32.lib uuid.lib shell32.lib user32.lib gdi32.lib shcore.lib ^
//      advapi32.lib
// llvm-mingw:
//   clang++ -std=c++17 scripts/binprobe.cpp -o binprobe.exe -lole32 ^
//      -loleaut32 -luuid -lshell32 -luser32 -lgdi32 -lshcore -ladvapi32
//
// Run it unelevated — an elevated probe cannot reach explorer's desktop
// view and will report "no desktop view" for reasons that have nothing to
// do with the Recycle Bin.
//
// `binprobe --measure` goes further: it grabs the bin's cell off the
// screen and template-matches the shell's OWN artwork against it, at a
// sweep of sizes, and prints where the icon really is. That turns "nudge
// HYPERBIN_BIN_ADJUST until it looks right" into a number — which
// matters, because the two ways to be wrong here (a mis-sized icon and a
// mis-placed one) look identical by eye once the swarm scales with the
// rect. Needs the desktop actually VISIBLE at the bin; it says so if a
// window is in the way rather than matching against that window.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>

#include <exdisp.h>
#include <shellapi.h>
#include <shellscalingapi.h>   // GetDpiForMonitor
#include <shlobj.h>
#include <shobjidl.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {

template <class T>
struct Com
{
    T *p = nullptr;
    ~Com() { if (p) p->Release(); }
    T **operator&() { if (p) { p->Release(); p = nullptr; } return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

const char *viewModeName(FOLDERVIEWMODE m)
{
    switch (m) {
    case FVM_ICON:      return "icon";
    case FVM_SMALLICON: return "small";
    case FVM_LIST:      return "list";
    case FVM_DETAILS:   return "details";
    case FVM_THUMBNAIL: return "thumbnail";
    case FVM_TILE:      return "tile";
    case FVM_THUMBSTRIP:return "thumbstrip";
    case FVM_CONTENT:   return "content";
    default:            return "auto/other";
    }
}

/// The desktop's shell view. Same route RecycleBinTarget takes.
bool desktopView(Com<IFolderView2> &out, HWND *hwnd)
{
    Com<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&windows))))
        return false;

    VARIANT empty;
    VariantInit(&empty);
    long desktopHwnd = 0;
    Com<IDispatch> dispatch;
    if (FAILED(windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &desktopHwnd,
                                     SWFO_NEEDDISPATCH, &dispatch))
        || !dispatch)
        return false;

    Com<IServiceProvider> provider;
    if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&provider))))
        return false;
    Com<IShellBrowser> browser;
    if (FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))))
        return false;
    Com<IShellView> view;
    if (FAILED(browser->QueryActiveShellView(&view)))
        return false;
    view->GetWindow(hwnd);
    return SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&out)));
}

bool elevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size);
    CloseHandle(token);
    return ok && e.TokenIsElevated;
}

/// Alpha coverage of the bin's own artwork, as a percentage.
///
/// This is the number that decides where flies can stand: FlyItem feeds
/// the icon's alpha into the walkable-surface grid. 100% means the shell
/// handed back an opaque square and the swarm will land on the whole
/// bounding box — the exact bug the macOS build shipped once.
int iconAlphaCoverage(PIDLIST_ABSOLUTE pidl, int px)
{
    Com<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory))))
        return -1;
    HBITMAP bitmap = nullptr;
    const SIZE size{px, px};
    if (FAILED(factory->GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)))
        return -1;

    BITMAP info{};
    if (!GetObject(bitmap, sizeof(info), &info)) {
        DeleteObject(bitmap);
        return -1;
    }
    const int w = info.bmWidth, h = info.bmHeight;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    auto *pixels = new unsigned char[size_t(w) * h * 4];
    HDC dc = GetDC(nullptr);
    const int copied = GetDIBits(dc, bitmap, 0, UINT(h), pixels, &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(bitmap);
    if (!copied) {
        delete[] pixels;
        return -1;
    }

    long opaque = 0;
    for (long i = 0; i < long(w) * h; ++i)
        if (pixels[i * 4 + 3] > 115)   // matches flymask.frag's cutoff
            ++opaque;
    delete[] pixels;
    return int(opaque * 100 / (long(w) * h));
}

/// RGBA pixels of an area of the screen, in physical pixels.
bool grabScreen(int x, int y, int w, int h, std::vector<unsigned char> *out)
{
    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    const bool ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY) != 0;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out->resize(size_t(w) * h * 4);
    const bool got = ok && GetDIBits(mem, bmp, 0, UINT(h), out->data(), &bi,
                                     DIB_RGB_COLORS) != 0;

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return got;
}

/// The shell's artwork at an exact size, as RGBA.
bool iconPixels(PIDLIST_ABSOLUTE pidl, int px, std::vector<unsigned char> *out)
{
    Com<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory))))
        return false;
    HBITMAP bitmap = nullptr;
    const SIZE size{px, px};
    // No BIGGERSIZEOK here, unlike the app: for matching we need exactly
    // the size we asked for, not the nearest one the shell has cached.
    if (FAILED(factory->GetImage(size, SIIGBF_ICONONLY, &bitmap)))
        return false;

    BITMAP info{};
    if (!GetObject(bitmap, sizeof(info), &info) || info.bmWidth != px) {
        DeleteObject(bitmap);
        return false;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = px;
    bi.bmiHeader.biHeight      = -px;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out->resize(size_t(px) * px * 4);
    HDC dc = GetDC(nullptr);
    const bool ok = GetDIBits(dc, bitmap, 0, UINT(px), out->data(), &bi,
                              DIB_RGB_COLORS) != 0;
    ReleaseDC(nullptr, dc);
    DeleteObject(bitmap);
    return ok;
}

bool desktopVisibleAt(int x, int y)
{
    const POINT pt{x, y};
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd)
        return true;
    wchar_t cls[64]{};
    GetClassNameW(hwnd, cls, 64);
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0
        || wcscmp(cls, L"SHELLDLL_DefView") == 0
        || wcscmp(cls, L"SysListView32") == 0;
}

/// Where the artwork actually is, by matching it against the screen.
///
/// Only fully opaque icon pixels are compared: the soft edges are
/// composited over an unknown wallpaper, so they would score against
/// whatever happens to be behind them rather than against the shape.
void measure(PIDLIST_ABSOLUTE pidl, POINT cell, POINT spacing, int logicalIcon,
             double scale)
{
    if (!desktopVisibleAt(cell.x + spacing.x / 2, cell.y + spacing.y / 4)) {
        std::printf("  --measure: the desktop is covered at the bin; "
                    "show it and run again\n");
        return;
    }

    const int w = spacing.x, h = spacing.y;
    std::vector<unsigned char> screen;
    if (!grabScreen(cell.x, cell.y, w, h, &screen)) {
        std::printf("  --measure: screen grab failed\n");
        return;
    }

    int bestSize = 0, bestX = 0, bestY = 0;
    double bestScore = 1e18;
    for (int px = 24; px <= 128; px += 8) {
        if (px > w || px > h)
            break;
        std::vector<unsigned char> icon;
        if (!iconPixels(pidl, px, &icon))
            continue;

        for (int oy = 0; oy + px <= h; ++oy) {
            for (int ox = 0; ox + px <= w; ++ox) {
                double sum = 0;
                long   n   = 0;
                for (int iy = 0; iy < px; ++iy) {
                    const unsigned char *irow = &icon[size_t(iy) * px * 4];
                    const unsigned char *srow = &screen[(size_t(oy + iy) * w + ox) * 4];
                    for (int ix = 0; ix < px; ++ix) {
                        if (irow[ix * 4 + 3] < 250)
                            continue;
                        sum += std::abs(int(irow[ix * 4 + 0]) - int(srow[ix * 4 + 0]))
                             + std::abs(int(irow[ix * 4 + 1]) - int(srow[ix * 4 + 1]))
                             + std::abs(int(irow[ix * 4 + 2]) - int(srow[ix * 4 + 2]));
                        ++n;
                    }
                }
                // Normalised per compared pixel, so a bigger icon isn't
                // penalised for having more of them to disagree about.
                if (n < 64)
                    continue;
                const double score = sum / n;
                if (score < bestScore) {
                    bestScore = score;
                    bestSize  = px;
                    bestX     = ox;
                    bestY     = oy;
                }
            }
        }
    }

    if (!bestSize) {
        std::printf("  --measure: no match\n");
        return;
    }

    // What RecycleBinTarget::visualIconRect() would predict, and the nudge
    // that closes the gap. Kept in step with that function deliberately:
    // a probe that models the icon differently from the app prints
    // adjustments which make the app worse.
    const int modelled = int(logicalIcon * scale + 0.5);
    const int predX    = 0;                                  // icon's own left edge
    const int predY    = int(3.0 * scale + 0.5);             // measured top inset
    std::printf("  --measure: icon is %dpx at cell+(%d,%d), mean error %.1f/255\n",
                bestSize, bestX, bestY, bestScore);
    std::printf("             model says %dpx at cell+(%d,%d)  ->  "
                "HYPERBIN_BIN_ADJUST=%d,%d,%.3f%s\n",
                modelled, predX, predY, bestX - predX, bestY - predY,
                bestSize / double(modelled ? modelled : 1),
                (bestX == predX && bestY == predY && bestSize == modelled)
                    ? "   (i.e. nothing to change)" : "");
}

} // namespace

int main(int argc, char **argv)
{
    bool wantMeasure = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--measure") == 0)
            wantMeasure = true;

    // Unbuffered: this runs until it is killed, and a probe whose last
    // second of output died in the stdio buffer is a probe that lies.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Per-monitor DPI aware, BEFORE anything asks Windows a question.
    // Without this Windows virtualises every coordinate a DPI-unaware
    // process sees: on a 150% display the probe would cheerfully report a
    // 48px icon at (21,155) while the shell is really drawing a 72px one
    // at (32,232). Calibrating hyperbin — which IS DPI aware, because Qt
    // makes it so — against those numbers would bake the scale factor
    // into the model twice.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Every monitor, so a mixed-DPI layout can be read at a glance.
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(mon, &mi))
                return TRUE;
            UINT dx = 96, dy = 96;
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
            std::printf("monitor %ls%s rect=(%ld,%ld %ldx%ld) dpi=%u scale=%.2f\n",
                        mi.szDevice,
                        (mi.dwFlags & MONITORINFOF_PRIMARY) ? " [primary]" : "",
                        mi.rcMonitor.left, mi.rcMonitor.top,
                        mi.rcMonitor.right - mi.rcMonitor.left,
                        mi.rcMonitor.bottom - mi.rcMonitor.top, dx, dx / 96.0);
            return TRUE;
        }, 0);
    std::printf("\n");
    std::printf("elevated = %d%s\n", elevated(),
                elevated() ? "   <-- explorer is unreachable from here" : "");

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &pidl))) {
        std::printf("no Recycle Bin PIDL\n");
        return 1;
    }
    PCUITEMID_CHILD child = ILFindLastID(pidl);

    for (;;) {
        SHQUERYRBINFO rb{};
        rb.cbSize = sizeof(rb);
        if (SUCCEEDED(SHQueryRecycleBin(nullptr, &rb)))
            std::printf("bin: %lld item(s), %lld byte(s)\n",
                        (long long)rb.i64NumItems, (long long)rb.i64Size);
        else
            std::printf("bin: SHQueryRecycleBin failed\n");

        HWND viewHwnd = nullptr;
        Com<IFolderView2> view;
        if (!desktopView(view, &viewHwnd)) {
            std::printf("  no desktop view (elevated? explorer restarting?)\n\n");
            Sleep(1000);
            continue;
        }
        // The view's own size says which space its coordinates are in: it
        // spans the whole virtual desktop, so a client rect matching the
        // real pixel dimensions means GetItemPosition is handing back
        // physical pixels, and a smaller one means logical.
        RECT client{}, window{};
        if (viewHwnd) {
            GetClientRect(viewHwnd, &client);
            GetWindowRect(viewHwnd, &window);
        }
        std::printf("  view hwnd=%p visible=%d client=%ldx%ld window=(%ld,%ld %ldx%ld)\n",
                    (void *)viewHwnd, viewHwnd ? IsWindowVisible(viewHwnd) : 0,
                    client.right, client.bottom, window.left, window.top,
                    window.right - window.left, window.bottom - window.top);
        std::printf("  virtual desktop = %dx%d at (%d,%d)\n",
                    GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_CYVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN));

        int items = 0;
        view->ItemCount(SVGIO_ALLVIEW, &items);
        FOLDERVIEWMODE mode = FVM_AUTO;
        int iconPx = 0;
        view->GetViewModeAndIconSize(&mode, &iconPx);
        POINT spacing{};
        view->GetSpacing(&spacing);
        std::printf("  desktop items=%d mode=%s icon=%dpx spacing=%ldx%ld\n",
                    items, viewModeName(mode), iconPx, spacing.x, spacing.y);

        POINT cell{};
        if (SUCCEEDED(view->GetItemPosition(child, &cell))) {
            POINT screen = cell;
            if (viewHwnd)
                ClientToScreen(viewHwnd, &screen);

            HMONITOR mon = MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST);
            UINT dpiX = 96, dpiY = 96;
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

            // The model RecycleBinTarget applies: the reported position IS
            // the icon's top-left, plus a ~3 logical px inset, at the icon's
            // size in physical pixels. Confirmed by --measure below.
            const double sc = dpiX / 96.0;
            const int side  = int(iconPx * sc + 0.5);
            std::printf("  cell=(%ld,%ld) screen=(%ld,%ld) -> icon=(%ld,%d %dx%d)\n",
                        cell.x, cell.y, screen.x, screen.y,
                        screen.x, int(screen.y + 3.0 * sc + 0.5), side, side);

            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(mon, &mi);
            std::printf("  monitor=%ls origin=(%ld,%ld) dpi=%u scale=%.2f\n",
                        mi.szDevice, mi.rcMonitor.left, mi.rcMonitor.top, dpiX,
                        dpiX / 96.0);
            std::printf("  icon in physical px = %d (logical %d x scale %.2f)\n",
                        int(iconPx * dpiX / 96.0 + 0.5), iconPx, dpiX / 96.0);
            if (wantMeasure)
                measure(pidl, screen, spacing, iconPx, dpiX / 96.0);
        } else {
            std::printf("  bin not positioned in the view "
                        "(hidden in Desktop Icon Settings?)\n");
        }

        const int coverage = iconAlphaCoverage(pidl, 128);
        std::printf("  icon alpha coverage = %d%%%s\n", coverage,
                    coverage >= 100 ? "   <-- opaque; flies will use the whole square"
                                    : "");
        std::printf("\n");
        Sleep(1000);
    }
    return 0;
}
