# hyperbin — architecture and plan

Free giveaway app: animated flies swarm the Trash / Recycle Bin as it
fills. macOS + Windows, Qt 6.11, direct download only.

## Decisions

**Direct download only, no app stores.** Dropped on purpose. The Mac App
Store sandbox blocks every leg of this app (no `~/.Trash` access, no
Accessibility API for Dock geometry), so a MAS build couldn't do the
core feature. Microsoft Store via full-trust MSIX would work, but a
lead magnet on a store is a funnel with the bottom cut off — the store
owns the customer, so no email address and no attribution. Reuse
Hypershot's Developer ID + notarization + Sparkle pipeline instead and
gate the download on an email.

**Qt licensing: LGPL is fine here.** Since we're not shipping to stores,
LGPL v3 works — dynamic-link the Qt libraries and provide relinking
info. That sidesteps the open question of whether Hypershot's commercial
seat extends to a second product. Worth confirming with Qt sales anyway
(commercial Qt is normally per-developer, not per-app, so it likely
does), but hyperbin doesn't have to block on the answer.

**Procedural simulation, not Rive.** Fly motion is emergent, not
authored. Steering behaviours produce better flies in ~200 lines than
keyframed loops would, and respond continuously to fullness rather than
cutting between discrete states. Rive earns its place when a designer
authors the motion; revisit if we add a character. `RiveQtQuickPlugin`
stays available if that changes.

**Neither shaders nor Rive is the power lever** — see `battery.md`. At
128×128 the fill cost is irrelevant; frame cadence and idle behaviour
are everything. A fragment shader at vsync would be *worse* than a CPU
sim at 20fps. So: render on demand at 20fps, zero frames when the bin is
empty, stop when occluded or on low power.

## Shape

```
src/core/       FlySim (steering sim), PowerPolicy (when/how fast to draw)
src/platform/   TrashTarget interface + per-OS impls + a stub
src/render/     FlyItem — one batched QSGGeometryNode, one draw call
qml/            Main.qml: overlay window, tray UI, onboarding
```

`TrashTarget` is the entire platform boundary: icon rect, item count,
status, remediation. Everything above it is shared — realistically
~75–80% of the code.

## Status

Done and building:
- `FlySim` — wander + bin containment + separation + darts, deterministic
- `PowerPolicy` — all render-gating rules in one testable place
- `FlyItem` — batched scene-graph rendering, explicit frame clock
- `StubTrashTarget` — fake geometry cycling fullness, so the sim is
  developable before either native backend exists
- `tests/simtest.cpp` — populates, moves, stays over the bin, scales,
  fully disperses when emptied

Verified: builds clean on macOS, runs 4s without crashing, sim test
passes. Measured swarm bbox 172×187px at fullness 1, which set the
overlay margin to 95px.

## Next

1. **`DockTrashTarget` (macOS).** `AXUIElementCreateApplication` on the
   Dock pid → find the Trash `AXDockItem` → `kAXPositionAttribute` /
   `kAXSizeAttribute`. Needs the Accessibility permission; onboarding
   must explain it before triggering the prompt. Item count from
   `~/.Trash` + per-volume `/.Trashes/<uid>` via FSEvents.
2. ~~**`RecycleBinTarget` (Windows).**~~ Built — see
   `docs/windows-backend.md`. Count and size come from
   `SHQueryRecycleBin` and change events from `SHChangeNotifyRegister`,
   as planned. The icon rect does **not** use `LVM_GETITEMPOSITION`:
   that needs `ReadProcessMemory` inside explorer.exe, which is the
   heuristic that gets an unknown signed download quarantined. It uses
   `IShellWindows` → `IFolderView2::GetItemPosition` instead —
   documented, injection-free, and it reports the icon size too.
3. ~~**Overlay z-order.**~~ Built. macOS: `NSWindow` level above
   `kCGDockWindowLevel`. Windows is **not** parented into the desktop
   layer: `SetParent` under `Progman`/`WorkerW` puts the overlay behind
   the icons permanently, which kills the in-front batch, the landings
   on the front of the bin, and the 4:1 front/behind split. It is
   topmost + click-through, hidden whenever the desktop is not actually
   visible at the bin — which also covers full-screen apps and games for
   free.
4. **Onboarding.** A hidden Recycle Bin is detected
   (`HKCU\...\HideDesktopIcons\NewStartPanel`, CLSID
   `{645FF040-5081-101B-9F08-00AA002F954E}`, watched with
   `RegNotifyChangeKeyValue`) and surfaced in the tray with a line that
   opens Desktop Icon Settings. Deliberately not written for the user.
   Still to do: the first-run walkthrough itself, and explaining
   Accessibility on macOS before prompting.
5. **Power verification** on real hardware — `powermetrics` /
   PowerCfg. Non-negotiable before ship.
6. **Art.** Swap the vertex-coloured quads for a textured sprite atlas;
   the geometry layout already supports it.

## Open risks

- **Windows may have no canvas.** Many Win11 users run a bare desktop,
  and the Recycle Bin can be hidden or dragged anywhere. Decide whether
  the Windows build degrades to a floating desktop companion when the
  icon can't be found.
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
