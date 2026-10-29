# hyperbin — architecture

Free app: an animated effect (flies, ooze or tentacles) grows on the
macOS Trash or the Windows Recycle Bin as it fills, and leaves when it is
emptied. macOS + Windows, Qt 6.11, direct download only.

## Decisions

**Direct download only, no app stores.** The Mac App Store sandbox blocks
every leg of this app (no `~/.Trash` access, no Accessibility API for
Dock geometry), so a MAS build couldn't do the core feature. Microsoft
Store via full-trust MSIX would work, but the store would own the
customer and the attribution. So it reuses our own Developer ID +
notarization + Sparkle pipeline on macOS and a signed installer + MSIX
with WinSparkle on Windows (`distribution.md`). The download is a plain
link: no email gate, no account. The welcome window offers the
newsletter; nothing depends on it.

**Licensing: source MIT, release binaries GPLv3.** This first said "LGPL
is fine", which stopped being true the day the ooze arrived: Qt Quick 3D
is GPLv3 or commercial, with no LGPL option, and the ooze and tentacles
are Quick3D scenes. Release CI installs open-source Qt, so the binaries
we ship are GPLv3, while everything in this repository stays MIT.
Publishing the source meets the GPL's source obligation. A build against
commercial Qt carries no such obligation. Detail per module in
`THIRD-PARTY-NOTICES.md`.

**Procedural simulation, not Rive.** Fly motion is emergent, not
authored. Steering behaviours produce better flies in ~200 lines than
keyframed loops would, and respond continuously to fullness rather than
cutting between discrete states. Rive earns its place when a designer
authors the motion; revisit if we add a character.

**Neither shaders nor Rive is the power lever** — see `battery.md`. At
icon size the fill cost is irrelevant; frame cadence and idle behaviour
are everything. So: render on demand from an explicit clock, at the
display's refresh rate or 30fps when conserving; stop the clock when the
effect is at rest; zero frames and no window when the bin is empty; stop
whenever nobody can see the bin.

## Shape

```
src/core/       simulations (FlySim, OozeSim, TentacleChain), the Effect
                contract and registry, PowerPolicy (when/how fast to draw)
src/effects/    Flies (one batched scene-graph node), Ooze and Tentacles
                (Qt Quick 3D scenes)
src/render/     EffectItem: clocks the current effect and hands it the
                bin's geometry, artwork and silhouette
src/platform/   TrashTarget + DockTrashTarget (macOS) / RecycleBinTarget
                (Windows) / a stub; overlays, Low Power, launch at login
src/update/     Sparkle / WinSparkle behind one AppUpdater
src/analytics/  Countly, off by default, switchable on
qml/            splash, permissions, welcome window, the 3D scenes
```

`TrashTarget` is the entire platform boundary: icon rect, count and
size, status, remediation. Everything above it is shared — realistically
~75–80% of the code. A new effect implements `Effect` and adds one line
to `EffectRegistry.cpp` (`effects.md`).

## Status (1.2.1, Oct 2026)

Shipped:
- **Both backends.**
  - macOS (`DockTrashTarget`): the Trash `AXDockItem` through the
    Accessibility API, polled at 16ms while on screen so it follows Dock
    magnification, 1s when hidden. `~/.Trash` is sized with a directory
    walk at about 1Hz, which needs Full Disk Access.
  - Windows (`RecycleBinTarget`): count and size from
    `SHQueryRecycleBin`, change events from `SHChangeNotifyRegister`,
    the icon rect from `IShellWindows` → `IFolderView2::GetItemPosition`.
    Not `LVM_GETITEMPOSITION`, which needs `ReadProcessMemory` inside
    explorer.exe — the heuristic that gets an unknown signed download
    quarantined. See `windows-backend.md`.
- **Overlays.** macOS: an `NSWindow` above `kCGDockWindowLevel`,
  click-through, on every Space. Windows is **not** parented into the
  desktop layer: `SetParent` under `Progman`/`WorkerW` puts the overlay
  behind the icons permanently, which kills the in-front batch, the
  landings on the front of the bin, and the 4:1 front/behind split. It is
  topmost + click-through, hidden whenever the desktop is not actually
  visible at the bin — which also covers full-screen apps and games.
- **Three effects**: Flies, Ooze (with eyes that follow the pointer) and
  Tentacles, one at a time, with an amount and a threshold from the
  menu.
- **Power**: `PowerPolicy` gates every frame; Low Power Mode is On / Off
  / Auto, and Auto follows the OS.
- **Onboarding**: a permissions window on macOS explains Accessibility
  and Full Disk Access before asking; a hidden Recycle Bin on Windows is
  detected (`HideDesktopIcons`, watched) and surfaced in the tray with a
  line that opens Desktop Icon Settings, deliberately not changed for
  the user. A welcome window says what the app does, with switches for
  Open at Login and usage data.
- **Tests** under `tests/` for the flies, ooze, tentacles, settings and
  links.

## Next

- **Measure power on a real Dock** (`battery.md`, *Measured*). So far
  energy has only been measured in the preview window.
- **Other volumes.** Only `~/.Trash` is counted; items trashed from
  `/Volumes/<x>/.Trashes/<uid>` are not.
- **`AXObserver` instead of polling** the Dock item, so revealing an
  auto-hidden Dock is immediate and cheaper.

## Open risks

- **Windows may have no canvas.** Many Win11 users run a bare desktop,
  and the Recycle Bin can be hidden or dragged anywhere. A hidden bin is
  detected and explained; there is no fallback that draws without it.
- ~~The Explorer memory-read technique is the single biggest unknown in
  the project.~~ Retired: nothing reads explorer's memory. `IFolderView2`
  gives the same numbers through a documented interface. What it costs
  instead is an integrity-level constraint — an elevated hyperbin cannot
  reach explorer over COM at all, so `start()` checks and says so.
- ~~**The icon rect model is calibrated, not derived.**~~ Now measured:
  `binprobe --measure` template-matches the shell's artwork against the
  screen and the model reproduces it exactly (mean error 0.1/255) on a
  150% display. Two assumptions died doing it — the reported position is
  the icon's top-left rather than the cell's, and the reported icon size
  is logical while positions are physical. Both remain worth re-measuring
  on a 100% display and at other icon sizes, which is one command.
