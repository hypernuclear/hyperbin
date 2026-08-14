# hyperbin

Animated effects over your Trash / Recycle Bin, growing as it fills up.
Free giveaway app for macOS and Windows. Direct download only.

Cross-platform Qt 6.11 + C++. Two effects ship today — **Flies** and
**Ooze** — picked from the menu-bar item.

## Status

**macOS works end to end.** The overlay sits above the Dock, tracks the
real Trash icon through magnification, and the effect grows with what is
in the bin.

**Windows works too.** `RecycleBinTarget` finds the bin through the
desktop's shell view, reads count *and* size from `SHQueryRecycleBin`,
and the overlay hides itself whenever a window covers the desktop at the
bin. The icon rect is calibrated by measurement rather than by eye:
`binprobe --measure` matches the shell's own artwork against the screen
and prints where the icon really is. See `docs/windows-backend.md`.

A menu-bar / tray item owns the settings: which effect, how much of it,
how full the bin has to be before anything happens, whether to run at
all, and open-at-login.

See `docs/architecture.md` for the decisions, `docs/effects.md` for the
contract a new effect implements, and `docs/battery.md` for the power
budget.

## The effects

Both are driven by the same host (`EffectItem`) and the same three-state
power model, and neither knows anything about the other.

**Flies** — a steering-behaviour swarm. Wander, separation and
containment produce the motion; nothing is keyframed. They land on the
bin, walk about, take off, pass behind it, and scatter from the pointer.
Drawn as instanced sprites through a scene-graph node, with a shader that
erases a fly wherever the bin's artwork is opaque so it reads as passing
behind.

**Ooze** — green sludge filling the bin and running down the outside.
Qt Quick 3D, because the look wanted is transmission: the bin seen
*through* the gel, refracted, with real specular and an environment.
The body is a solid of revolution swept from the bin's own measured
silhouette, and everything that moves — the drifting lumps, the wave
travelling down it, the puddle gathering at the foot — happens in the
vertex shader, so the mesh is rebuilt only when the level changes.

Adding a third means one entry in `EffectRegistry` and a class
implementing `Effect`. `docs/effects.md` is the contract, including the
three Qt colour-pipeline rules that will otherwise cost you an afternoon.

## Build

macOS / Linux:

```sh
sh scripts/install-hooks.sh          # once per clone — see Secrets below
cmake --preset dev && cmake --build --preset dev
```

Windows — the presets are deliberately disabled here (`cmake --preset dev`
answers "Could not use disabled preset"). Build from a **Developer Command
Prompt for VS**, which is what puts `cl.exe` on PATH:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Ninja ships with Qt at `C:\Qt\Tools\Ninja` if it isn't already on PATH.

In Qt Creator on Windows, use a normal kit — *Desktop Qt 6.11.1 MSVC2022
64bit* — not a "(CMake preset)" build configuration. Qt Creator derives a
kit from every enabled preset, and a preset that names no compiler yields
a kit with no toolchain and no Qt: it then configures with the NMake
generator outside the MSVC environment and dies on `nmake -?` / "no such
file or directory". Naming the compiler in the preset doesn't help — Qt
Creator matches a toolchain by absolute `cl.exe` path, which is
machine-specific and can't be committed — so the presets simply stop at
the Windows boundary and leave kit selection to the IDE.

Qt's location is never committed. The build auto-detects the newest
`~/Qt/6.*` install; override with `QT_ROOT`, with
`-DCMAKE_PREFIX_PATH=...`, or by pinning it in a `CMakeUserPresets.json`
(git-ignored) that inherits `dev`.

An empty `-DCMAKE_PREFIX_PATH=` is deliberately treated as "not
specified" rather than "search nowhere": an editor that expands an unset
`$env{}` to an empty string would otherwise silently break the Qt lookup,
which is exactly how this broke once already.

## Secrets

This repo is public. `gitleaks` runs in two places:

- **Pre-commit hook** (`.githooks/pre-commit`) — the one that actually
  prevents a leak, since CI only fires after a push has already made the
  secret public. Enable per clone with `scripts/install-hooks.sh`.
- **CI** (`.github/workflows/secret-scan.yml`) — full history on every
  PR, plus weekly, so secrets missed by an older ruleset still surface.

If something is caught, **rotate the credential first**. Removing it from
the diff does nothing for a value that was already pushed — it stays in
the history and must be treated as compromised.

## Running it

**Preview (use this one for anything visual).** A normal window with the
bin's real artwork drawn underneath, at a size where you can actually see
what an effect is doing. Judging a look by screenshotting a 49-point Dock
icon does not work — at that size every feature worth arguing about is
sub-pixel.

```sh
HYPERBIN_PREVIEW=ooze  ./build/hyperbin.app/Contents/MacOS/hyperbin --windowed
HYPERBIN_PREVIEW=flies ./build/hyperbin.app/Contents/MacOS/hyperbin --windowed
```

A slider owns fullness, so you can hold a value and watch it settle.
Drag to 0 and everything should leave under its own power — if anything
remains, the render clock never stops and the power budget is broken.

Two knobs make it scriptable, and between them they are how most of the
visual work gets checked:

```sh
HYPERBIN_PREVIEW_SHOT=/tmp/a.png     # grab the window, then exit
HYPERBIN_PREVIEW_SHOT_MS=12000       # ...at this moment, not the default 2.5s
```

Shooting the same scene twice and differencing the two is the only
reliable way to tell an animation that is running from one that is merely
present in the shader. That has been wrong twice.

**Overlay.** The real thing: frameless, click-through, above the Dock, on
every Space, tracking your actual Trash.

```sh
./build/hyperbin.app/Contents/MacOS/hyperbin
HYPERBIN_DEBUG=1 ./build/hyperbin.app/Contents/MacOS/hyperbin   # geometry + clock log
./build/hyperbin.app/Contents/MacOS/hyperbin --paintdebug       # fill the overlay
```

`HYPERBIN_DEBUG` prints the render clock every time it changes, with the
interval the power policy asked for and the floor the current effect
imposed. That number decides the whole energy story and is otherwise
invisible.

Nothing draws when the Dock is auto-hidden — the icon is parked outside
every display and there is nothing to sit on.

**Headless.** `./build/simtest` — the simulations only, never the
renderer, so it runs without a display. It asserts the swarm populates,
moves, lands, walks, stays over the bin, scales with it and fully
disperses when emptied; that the ooze creeps rather than snapping on,
grows with the trash and recedes to nothing; that the overlay margins are
large enough for what is drawn in them; and that settings survive a
restart.

VS Code: `.vscode/launch.json` has the debug configs.

## Permissions (macOS)

Two, and the app watches for both — neither needs a restart once
granted, and neither fails silently into a console nobody reads.

- **Accessibility** — the only way to get Dock item geometry. If it's
  missing the app shows the system prompt (which carries an "Open System
  Settings" button), then polls once a second and starts tracking the
  moment it's granted.
- **Full Disk Access** — to size `~/.Trash`. There is no prompt API for
  this one; the most any app can do is open the pane. What makes that
  bearable is that TCC lists an app under Full Disk Access as soon as it
  *attempts* a protected read — so hyperbin probes the trash on startup
  and the user finds it already in the list, needing only the switch.
  `fullDiskAccessGranted()` performs a real `contentsOfDirectoryAtPath:`
  for exactly that reason: `access()` and `QDir::isReadable()` answer the
  question without touching anything, and an app that never attempts the
  read never appears in the list for the user to enable.

**Size, not item count, drives the effect.** Asking Finder for a count
needs only Automation and is how this used to work — but Finder returns
`missing value` for the size of any *folder*, so a trashed app bundle,
package or project directory weighed nothing, and the count fallback
ignored the Trash Threshold setting outright, which made that whole menu
a no-op on a stock Mac. Without disk access the app now draws nothing and
says so, rather than drawing something quietly wrong.

`openRemediation()` opens whichever pane is the blocker — Accessibility
first, since without it there is no icon to draw on and granting disk
access alone would change nothing.

**The build MUST be signed with a stable identity** — the CMake post-build
step does this with the Developer ID. Without it the bundle is ad-hoc /
linker-signed, and TCC identifies ad-hoc apps by their *code hash*, which
changes on every rebuild. Every rebuild then looks like a brand new app,
the Accessibility grant silently evaporates, and the app re-prompts
forever. Verify with:

```sh
codesign -d -r- build/hyperbin.app   # must name the identifier + team,
                                     # not be adhoc
```

Set `-DHYPERBIN_CODESIGN_IDENTITY=` to skip signing (CI without certs);
expect the re-prompt behaviour if you do.

**Dev gotcha:** launching from a terminal that itself has Accessibility
makes the terminal the responsible process, so the app inherits the grant
and you can never reach the ungranted path. `HYPERBIN_FORCE_NO_AX=1`
forces it, which is how the prompt flow gets tested.

## Known gaps

- **No onboarding.** Both macOS permissions have to be granted by hand;
  nothing explains them to the user yet.
- **The mask lags the Dock by a frame.** Flies pass behind the bin via a
  shader that erases them where the Trash artwork is opaque
  (`shaders/flymask.*`). Its position comes from the Accessibility poll,
  which is one frame behind during Dock magnification, so flies are
  hidden a few pixels off. Barely perceptible — and far better than the
  copy-the-icon approach it replaced, where the same lag showed as a
  visibly desynced duplicate bin.
- **The ooze infers the bin's rim** from the widest row of its
  silhouette. Right for a tapered container, which both shells draw, but
  it is an inference about artwork rather than a fact about it.
- **The ooze level is not a gauge.** It reads high early on purpose — a
  half-full bin and a full one look nearly the same. `OozeSim::minShare`
  is the knob if that should track fullness more honestly.
- **Only `~/.Trash`.** Items trashed from other volumes live in
  `/Volumes/<x>/.Trashes/<uid>` and aren't counted.
- **Polling, not notifications.** The icon is polled at 16ms while on
  screen (fast enough to follow Dock magnification) and 1s when hidden,
  so revealing an auto-hidden Dock lags up to a second. An `AXObserver`
  on the Dock item would make it immediate and cheaper.
- **Energy is measured only in the preview**, at a far larger size than
  the Dock ever draws. Ooze runs about 1.3x the flies there; at icon size
  the gap should close, but that has not been checked on a real Dock.

## Layout

```
src/core/       platform-agnostic: simulations, the Effect contract,
                the effect registry, the power policy
src/effects/    one directory per effect's implementation
src/platform/   the ~20% that differs: TrashTarget + overlay surface
src/render/     EffectItem, the host that clocks an effect and hands it
                the bin's geometry, artwork and silhouette
shaders/        fly mask (baked .qsb) and the ooze's Quick3D snippets
qml/            tray glyph and the ooze's Quick3D scene
docs/           architecture, the effect contract, battery, Windows
```

