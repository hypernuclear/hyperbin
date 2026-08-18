# Windows backend — implementation notes

Everything above `TrashTarget` is portable and was left untouched:
`FlySim`, the surface-coverage/mask architecture, the shaders,
`PowerPolicy`, `Settings`, `TrayMenu`. This covers the four pieces that
are not.

Read `docs/architecture.md` first for the seam this fits into, and
`src/platform/DockTrashTarget.mm` for the macOS backend it mirrors — the
two are deliberately the same shape.

## Prerequisites

- Qt 6.11+ with a matching kit. Nothing here is MSVC-specific; the tree
  builds under both `msvc2022_64` and `llvm-mingw_64`.
- In Qt Creator, use a normal kit (*Desktop Qt 6.11.1 MSVC2022 64bit*),
  not a "(CMake preset)" build configuration — the presets are macOS and
  Linux only, see `CMakePresets.json` and the README.
- From the command line, build from a Developer Command Prompt:
  `cmake -S . -B build -G Ninja`.
- **Do not run the app elevated.** Step 1 talks to explorer.exe over COM,
  and cross-integrity-level COM simply fails to find the desktop.
  `RecycleBinTarget::start()` checks and says so rather than leaving it
  as a mystery.

---

## Step 1 — `RecycleBinTarget`

`src/platform/RecycleBinTarget.{h,cpp}`, wired into `TrashTarget::create()`
and built under `$<$<PLATFORM_ID:Windows>:…>`. Links `shell32 user32 ole32
oleaut32 uuid shlwapi gdi32 dwmapi advapi32 shcore`.

### 1a. Fullness

`SHQueryRecycleBin(nullptr, …)` gives the item count and the byte size
together, for every drive, with no permission at all. So `byteSize()`
returns a real number on Windows — unlike macOS, where it stays -1
without Full Disk Access, and "Flies Relative to Trash Size" works
properly here first. Nothing above the seam changed.

Change notification is a `SHChangeNotifyRegister` on the bin's PIDL,
delivered to a message-only (`HWND_MESSAGE`) window, so no visible window
is involved. `SHCNRF_NewDelivery` means the payload arrives as shared
memory and must be `SHChangeNotification_Lock`/`Unlock`ed even though only
the fact of the change matters — skipping the unlock leaks an
explorer-side allocation per trash operation. Emits only on an actual
change, same discipline as the macOS backend.

Screen and DPI changes are picked up from Qt's own `QScreen` signals
rather than `WM_SETTINGCHANGE`/`WM_DISPLAYCHANGE`: a message-only window
does not receive broadcast messages, so those would never have arrived.

### 1b. Position — `IFolderView2`

The Recycle Bin is an item in the desktop's `SysListView32`. Three routes;
this uses the third:

- ❌ **`LVM_GETITEMPOSITION`.** Needs `VirtualAllocEx` +
  `ReadProcessMemory` inside explorer.exe. It works, and it is what most
  sample code does. Not used: we ship a signed binary to strangers, and
  cross-process memory reads into explorer are exactly the heuristic that
  gets an unknown download quarantined.
- ⚠️ **UI Automation.** No injection, and the direct analogue of the macOS
  Accessibility approach. Still the fallback if the below misbehaves.
- ✅ **`IShellWindows` → `IFolderView2`.** Documented, injection-free, and
  it gives the icon size as well as the position.

```
IShellWindows::FindWindowSW(SWC_DESKTOP, SWFO_NEEDDISPATCH)
  -> IServiceProvider (SID_STopLevelBrowser)
  -> IShellBrowser::QueryActiveShellView
  -> IFolderView2
       ::GetItemPosition(pidl, &pt)          // the CELL's top-left
       ::GetViewModeAndIconSize(&mode, &px)  // icon size, physical px
       ::GetSpacing(&pt)                     // cell size, physical px
  -> IShellView::GetWindow(&hwnd)            // for ClientToScreen
```

The bin's PIDL comes from `SHGetKnownFolderIDList` + `ILFindLastID`, and
is matched with `IShellFolder::CompareIDs` when the direct lookup misses —
never by display name, which is localised and renameable.

Three gotchas, two of them measured:

1. **`GetItemPosition` returns the icon's top-left** — measured, and not
   what was assumed. The obvious reading is that it reports the cell and
   the icon is centred in it (cells are wider than icons because labels
   are), and that was the first model. It shipped a swarm sitting half the
   cell slack — 21 physical px — to the right, which is exactly how it
   looked on screen. `binprobe --measure` then template-matched the
   shell's own artwork against the desktop and found the icon at the
   reported position + (0,5), mean error 0.1/255. Only the small vertical
   inset is a constant, modelled as ~3 logical px so it tracks DPI.
2. **The icon SIZE is logical, while positions are physical.**
   `GetViewModeAndIconSize` reports the shell's image-list size — the
   16/32/48/96/256 family — so at 150% a "48px" icon is drawn 72 real
   pixels wide. Taking it literally made the swarm scale to a
   two-thirds-size bin *and* pushed it right, because centring a 48-wide
   icon in a 115-wide cell starts it 12px right of where a 72-wide one
   does. One bug, two symptoms.
3. **Units.** Measured on a mixed-DPI pair: the view's client rect is
   `5120x1440`, the full virtual desktop in *physical* pixels, so
   `IFolderView2` speaks physical and `iconRect()` must divide by the
   monitor's scale. `WinScreen.cpp` does it, anchored on each monitor's
   own origin and DPI — see the comment there for why matching a
   `HMONITOR` to a `QScreen` by name does not work.
4. **Poll rate.** No Dock magnification to chase, so 1Hz, dropping to 4s
   when nothing is drawn — not the Dock's 16ms.

### 1c. Icon artwork for the mask

`SHCreateItemFromIDList` → `IShellItemImageFactory::GetImage` with
`SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK` gives the correct full/empty
variant at the size asked for. Converted to `QImage` preserving alpha —
the alpha *is* the mask that `FlyItem::rebuildSurface()` turns into the
walkable-surface grid. Measured on a live bin: **56% alpha coverage**, so
the artwork is genuinely shaped and flies will not land on the whole
bounding box. Both degenerate cases (no alpha at all, fully opaque) are
detected and warned about rather than shipped as a mystery.

### Verifying step 1

- `ctest` must still pass untouched — the sims do not know about platforms,
  which is the point. It does.
- `scripts/binprobe.cpp` is the port of `scripts/dockprobe.mm`: cell
  position, spacing, icon size, monitor DPI, item count, byte size and the
  icon's alpha coverage, once a second. It sets per-monitor DPI awareness
  before asking Windows anything — without that, Windows virtualises every
  coordinate it sees and calibrating against those numbers would bake the
  scale factor in twice.
- `binprobe --measure` is the calibration itself: it grabs the bin's cell
  off the screen, template-matches the shell's own artwork against it over
  a sweep of sizes, and prints the icon's true rect plus the
  `HYPERBIN_BIN_ADJUST` that would close any gap. It needs the desktop
  visible at the bin and says so if a window is in the way. Its model is
  kept identical to `visualIconRect()` on purpose — a probe that models
  the icon differently prints adjustments that make the app worse.

---

## Step 2 — Overlay window and occlusion

The Dock is always on top and always visible. The Recycle Bin sits on the
desktop, **underneath every window**.

**The `WorkerW` trick is not used.** Parenting into the desktop layer is
what wallpaper apps do, but it puts the overlay *behind* the icons
permanently — killing the in-front batch, the landings on the front of the
bin, and the 4:1 front/behind split, i.e. most of what the swarm does.

Instead: topmost + click-through (`WS_EX_LAYERED | WS_EX_TRANSPARENT |
WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`), hidden unless the desktop is
actually visible at the bin. `WS_EX_TRANSPARENT` also takes the overlay
out of hit-testing, so the occlusion test never finds itself.

The test walks windows in z-order and asks what the first real one at the
bin's centre is; the desktop is visible if that is `Progman`, `WorkerW`,
`SHELLDLL_DefView`, or nothing. Walking z-order rather than a single
`WindowFromPoint` is what lets cloaked windows be skipped — a window on
another virtual desktop is "visible" by `IsWindowVisible` and would
otherwise hide the swarm for a window the user cannot see. Full-screen
apps and games fall out for free, which is the main reason to test what is
on top rather than who has focus.

It is sampled on `EVENT_SYSTEM_FOREGROUND` plus a 1Hz poll, never per
frame, and feeds the same `PowerPolicy::setTargetVisible()` that the
auto-hidden Dock drives on macOS — so a covered desktop costs nothing, and
on Windows that is most of the time. `main.cpp` ANDs it with the target's
status; both are re-evaluated together so neither can be forgotten.

---

## Step 3 — The swarm architecture is untouched

No code, by design. `ctest` passes unchanged, and the renderer, sim and
shaders took no Windows-specific edits.

Still to check **on screen**, which needs a visible desktop:

- flies land on the artwork, not the bounding box;
- the mask clips crawlers to the silhouette and occludes the behind batch;
- landing only happens in front, and nothing pops into view over the bin.

If any of these misbehave, the bug is almost certainly the rect model
(1b) or the alpha (1c), not `FlySim`.

---

## Step 4 — Hidden-bin detection

`TrashTarget::Status::IconHidden` already existed in the enum for this.

Detected three ways: the Recycle Bin specifically hidden
(`HKCU\...\HideDesktopIcons\NewStartPanel`, value
`{645FF040-5081-101B-9F08-00AA002F954E}` = 1), all desktop icons hidden
(the shell view exists but `IsWindowVisible` is false), and the bin simply
not present in the view.

`openRemediation()` opens Desktop Icon Settings
(`rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,0`), surfaced as a
tray line that states the problem and opens the dialog — not a console
message. Unlike macOS there is **no permission to grant**: nothing here
prompts, so the app can go from broken to working on a settings change
alone. `RegNotifyChangeKeyValue` watches for exactly that, so it recovers
without a restart, the same way the macOS build watches for Accessibility.

---

## Deliberately not here

- **Packaging** — `windeployqt`, a per-monitor-v2 manifest, signing.
- **A Windows-specific canvas.** The desktop is covered most of the time,
  so the flies will be seen far less than on macOS. Ship the desktop
  version and measure before inventing a second surface.
