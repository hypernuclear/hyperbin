# The power budget

The single hardest requirement: an always-running animated overlay that
costs approximately nothing. This document is the reasoning the rest of
the architecture follows from.

## Shaders are not the lever

The instinct is "Rive is heavy, shaders are light." That's the wrong
axis. For a ~128×128px overlay the per-pixel cost is irrelevant on any
GPU made this decade — 16k pixels is noise whether a fragment shader or
a CPU rasteriser fills them.

What actually costs power, in descending order:

1. **Frame cadence.** Every frame wakes the render thread, submits GPU
   commands, and forces the window server to recomposite. At 60fps
   that's 60 wakeups/sec preventing every idle state in the system from
   engaging. This dominates everything else by an order of magnitude.
2. **Preventing GPU/display idle.** A continuously animating layer keeps
   the compositor from entering low-power paths and, on laptops, can
   keep a discrete GPU awake.
3. **Being alive at all when nothing is happening.** An empty bin should
   cost literally zero — not "a cheap frame," zero.
4. Pixel/fill cost. Effectively free at this size.

A pure fragment shader animating at vsync is *worse* than a CPU
simulation drawing at 20fps, because it maximises the thing that
actually matters (1) to optimise the thing that doesn't (4).

## What we do instead

- **Render on demand, never continuously.** Qt Quick only renders when
  the scene changes. We drive `update()` from an explicit timer, so
  frame rate is a number we choose, not whatever vsync gives us.
- **At rest, stop the clock.** An effect that has settled (an ooze
  puddle, flies that have landed and stopped) keeps its last frame on
  screen and produces no new ones. The same goes once the pointer is on
  the bin and the effect has finished reacting to it.
- **Zero frames when the bin is empty.** No flies, no timer, no window.
  The overlay surface is destroyed, not hidden.
- **Stop when nobody can see it**: display asleep, screen locked,
  session switched, a fullscreen app covering the target, the Dock
  auto-hidden, the desktop not the foreground (Windows).
- **Run at the display's refresh rate**, and drop to 30fps when
  conserving. A 120Hz screen gets 120Hz; the old fixed 16ms cap meant it
  did not. The first budget was 20fps, on the theory that twitchy suits
  flies; once there was something to look at it read as choppy, and it
  went (`PowerPolicy.cpp`). Frame cadence is still the dominant cost, which is why the
  conserving path exists at all — users forgive a slower novelty, they do
  not forgive battery drain, and it is the top uninstall reason for this
  app class.

  **Conserving is the user's call, not the battery's.** "On battery" was
  the original rule and it was the wrong question: somebody on mains may
  want the thing calm and somebody unplugged may not want it throttled
  behind their back. The menu offers Low Power Mode > On / Off / Auto, and
  Auto follows the OS — `NSProcessInfo.isLowPowerModeEnabled` on macOS,
  Battery Saver via `GetSystemPowerStatus` on Windows. `src/platform/
  LowPower.h` is the one place that reads it; `PowerPolicy` is handed a
  single already-decided boolean.

  Worth knowing why this is written so emphatically: for most of this
  file's life the policy had `setOnBattery` and `setLowPowerMode`, read
  both, and **nothing ever called either of them**. Everything above was
  true as documentation and false as behaviour — full rate on battery,
  Low Power Mode ignored. An input nobody feeds is worse than no input,
  because it reads like a feature.
- **Small dirty region.** The overlay window is sized to the trash icon
  plus a flight margin, nothing more.

The fly simulation itself — at most six flies with wander/separation
steering — is a few hundred floating point ops per frame. It will never
be the bottleneck. Budget: **<0.1% CPU average, 0% when the bin is
empty.** That is a target; what has been measured is at the end.

`PowerPolicy` in `src/core/` owns all of this so the rules live in one
place rather than being scattered through the render path.

## Measuring, not guessing

How to check every claim above on real hardware:

- macOS: `powermetrics --samplers cpu_power,gpu_power` and Activity
  Monitor's Energy tab (the "Preventing Sleep" and "App Nap" columns).
- Windows: PowerCfg `/energy` and `/sleepstudy`, plus Task Manager's
  power-usage column.

Target: hyperbin does not appear in the top-20 energy consumers with a
full bin, and is indistinguishable from not running with an empty one.

## Measured

<!-- MEASURED (HYP-325): real-Dock runs go here. Per effect, full bin,
Dock at its default size: machine, OS version, refresh rate, CPU % and
energy impact, at full rate and with Low Power Mode on; plus an empty
bin and a hidden Dock. Windows the same, if measured. -->

Not yet measured on a real Dock. The only figures so far come from the
preview window, at a far larger size than the Dock ever draws, where
Ooze runs about 1.3x the flies.
