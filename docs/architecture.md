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
2. **`RecycleBinTarget` (Windows).** Count and size from
   `SHQueryRecycleBin`, change events via `SHChangeNotifyRegister` on the
   bin PIDL — both clean public APIs. Icon rect is the hard part:
   `Progman`/`WorkerW` → `SHELLDLL_DefView` → `SysListView32`, then
   `LVM_GETITEMPOSITION` cross-process. Validate on real hardware early;
   it trips AV heuristics and breaks under auto-arrange and mixed DPI.
3. **Overlay z-order.** macOS: `NSWindow` level above
   `kCGDockWindowLevel`. Windows: parent into the desktop layer so real
   windows occlude it, rather than sitting topmost over everything.
4. **Onboarding.** Detect a hidden Recycle Bin
   (`HKCU\...\HideDesktopIcons\NewStartPanel`, CLSID
   `{645FF040-5081-101B-9F08-00AA002F954E}`) and walk the user through
   enabling it — offer to set it with consent rather than silently
   writing to their desktop settings. macOS: explain Accessibility
   before prompting.
5. **Power verification** on real hardware — `powermetrics` /
   PowerCfg. Non-negotiable before ship.
6. **Art.** Swap the vertex-coloured quads for a textured sprite atlas;
   the geometry layout already supports it.

## Open risks

- **Windows may have no canvas.** Many Win11 users run a bare desktop,
  and the Recycle Bin can be hidden or dragged anywhere. Decide whether
  the Windows build degrades to a floating desktop companion when the
  icon can't be found.
- The Explorer memory-read technique is the single biggest unknown in
  the project. Prototype it before designing around it.
