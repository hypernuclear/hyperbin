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

Measured on a real Dock on Oct 29, 2026, on the code after 1.2.0 (the ooze's
eyes worked out in C++, the effects' per-frame signals split; see the
CHANGELOG):

- MacBook Pro, M4 Max, macOS 27.0.1, on mains.
- The Dock at its largest icon size (128), on a 4K display at 95Hz.
- A RelWithDebInfo build (the release is Release; the Qt libraries are
  the same release builds either way).
- Amount on *Oh the Horror!*, the most of each effect there is.
- `top` once a second for 60 seconds, after 20 seconds to settle.
  CPU is a share of one core; Energy Impact is Activity Monitor's figure.
  GPU and window-server cost are not in these numbers.

| | Full rate (95fps) | Low Power Mode (30fps) |
| --- | --- | --- |
| Flies | 9.8% | 4.6% |
| Ooze | 19.7% | 8.6% |
| Tentacles | 17.5% | 12.0% |
| Animation off | 0% | 0% |

Energy Impact matched CPU to within 0.05 in every case. Idle wakeups stayed
under one a second. Memory was 150 to 210 MB.

What this says:

- **Nothing to draw costs nothing**, as designed: no window and no clock.
  An empty bin and a hidden Dock take the same path; they were not timed
  separately.
- **Drawing is far over the <0.1% budget above.** At the display's refresh
  rate an effect costs 10 to 20% of a core. Frame cadence is the lever, as
  this document argues: Low Power Mode's 30fps takes flies and ooze down by
  more than half. Tentacles scale less, so part of their cost does not
  follow the frame rate.
- **Where the time goes** (`sample`, ooze at full rate). Before the eyes
  moved to C++, the main thread spent about 17% of a core re-running their
  QML bindings every frame; after, about 2%. The total fell by less than
  that, from 25.2% to 19.7%, and nearly all of what is left is Qt Quick 3D
  preparing and submitting each frame on the render thread. Turning MSAA
  off or rendering the View3D inline made no measurable difference.
- Single runs vary by 2 to 3 points with whatever else the machine is
  doing.

Not measured yet: Windows, an Intel Mac, a 60Hz-only display, the Dock at
its default size, and the default Amount (*More Trash, More Effect*), which
draws less than the maximum.
