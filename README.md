# hyperbin

Animated flies that swarm your Trash / Recycle Bin as it fills up.
Free giveaway app for macOS and Windows. Direct download only.

Cross-platform Qt 6.11 + C++. Separate from Hypershot — its own repo,
its own signing identity, its own updater feed.

## Status

macOS works end to end: the overlay sits above the Dock, tracks the real
Trash icon through magnification, and the swarm grows with the item
count. Windows is still a stub. See `docs/architecture.md` for the
decisions and `docs/battery.md` for the power budget.

## Build

```sh
sh scripts/install-hooks.sh          # once per clone — see Secrets below
export QT_ROOT=~/Qt/6.11.1/macos     # or use CMakeUserPresets.json
cmake --preset dev && cmake --build --preset dev
```

Qt's location is never committed. `CMakePresets.json` reads it from
`QT_ROOT`; to pin it per machine instead, create a `CMakeUserPresets.json`
(git-ignored) inheriting `dev` and setting `CMAKE_PREFIX_PATH`.

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

## Testing

**macOS is live** — `DockTrashTarget` reads your real Trash and finds the
real Dock icon. Windows still returns the stub.

Two permissions, and neither is Full Disk Access. Both are asked for by
the app itself — no console-only failures:

- **Accessibility** — the only way to get Dock item geometry. If it's
  missing the app shows the system prompt (which carries an "Open System
  Settings" button), then watches once a second and starts tracking the
  moment it's granted. No restart needed.
- **Automation (Finder)** — for the item count. Reading `~/.Trash`
  directly is TCC-protected and *would* need Full Disk Access; asking
  Finder needs only an Automation prompt, a far smaller ask for a free
  app. If FDA happens to be granted we use the direct read instead.

`openRemediation()` opens whichever settings pane is the blocker, for
when the one-shot system prompt has already been dismissed.

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

**Windowed (use this one).** A manual sim playground: normal window, a
drawn stand-in for the bin, and a slider that owns fullness. Nothing else
drives it, so you can hold a value and watch the swarm settle.

```sh
./build/hyperbin.app/Contents/MacOS/hyperbin --windowed
```

Drag the slider 0 → 100%: one lonely fly at the low end, up to 10 at
full, agitation rising with it. Drag back to 0 and they should disperse
outward and disappear entirely — if any remain, the render clock never
stops and the power budget is broken.

**Overlay.** The real thing: frameless, click-through, above the Dock, on
every Space, tracking your actual Trash.

```sh
./build/hyperbin.app/Contents/MacOS/hyperbin
HYPERBIN_DEBUG=1 ./build/hyperbin.app/Contents/MacOS/hyperbin   # geometry log
./build/hyperbin.app/Contents/MacOS/hyperbin --paintdebug       # fill the overlay
```

Nothing draws when the Dock is auto-hidden — the icon is parked outside
every display and there is nothing to sit on. Turn auto-hide off to see
it, or reveal the Dock and wait up to a second for the poll to notice.

**Headless.** `./build/simtest` — asserts the swarm populates, moves,
stays over the bin, scales with it, and fully disperses when emptied.

VS Code: `.vscode/launch.json` has all three as debug configs.

## Known gaps

- **Windows has no backend yet** — still the stub.
- **Only `~/.Trash`.** Items trashed from other volumes live in
  `/Volumes/<x>/.Trashes/<uid>` and aren't counted.
- **Polling, not notifications.** The icon is polled at 50ms while on
  screen (fast enough to follow Dock magnification) and 1s when hidden,
  so revealing an auto-hidden Dock lags up to a second. An `AXObserver`
  on the Dock item would make it immediate and cheaper.
- **No menu-bar item, no onboarding.** Both permissions currently have to
  be granted by hand; nothing explains them to the user yet.
- **Power is unmeasured.** 60fps was chosen for looks over the original
  20fps budget; `docs/battery.md` has the numbers that need verifying.

## Layout

```
src/core/       platform-agnostic: fly simulation, bin state, settings
src/platform/   the ~20% that differs: TrashTarget + OverlaySurface impls
src/render/     QSGGeometryNode instanced sprite renderer
qml/            tray UI + onboarding (not the flies — those are scene graph)
```

## Why not Rive

Fly motion is emergent, not authored. Steering behaviours (wander +
separation + containment) produce better-looking flies in ~200 lines than
hand-keyframed loops would, and they respond continuously to bin
fullness instead of switching between discrete states. Rive earns its
place when a designer needs to author the motion; here the motion is a
simulation. Revisit if we add authored character animation.

The simulation is also what makes the power budget achievable — see below.
