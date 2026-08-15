# Changelog

All notable changes to hyperbin are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Entries are added under `[Unreleased]` as work lands on `main`. Cutting a
release promotes that section to the new version and dates it.

## [Unreleased]

### Added

### Changed

### Fixed

## [1.0.0] - 2026-08-14

First public release.

### Added

- **Two effects over the Trash.** *Flies* sends a swarm crawling and
  circling the bin, landing on it and scattering when the pointer comes
  near. *Ooze* fills it with a translucent gel that gathers, sags and
  undulates, running down the sides as the bin fills.
- **Driven by how full the Trash actually is.** The effect grows with
  what is in there rather than running flat out, and settles back when
  the bin is emptied. The threshold that counts as "full" is
  configurable: 1 MB, 100 MB, 1 GB or 5 GB+.
- **Menu-bar control.** Show Animation as a master switch, an effect
  picker, and an amount submenu — *Just a Little*, *More Trash, More
  Effect*, *Oh the Horror!*, *Make It Stop*.
- **Open at Login**, registered through SMAppService so it appears in
  System Settings → Login Items like any other app.
- **A permissions window** explaining what Full Disk Access is for and
  opening the right Settings pane. macOS cannot prompt for that
  permission, so an app that needs it has to say so itself.
- **A splash on launch**, dismissed by clicking or after four seconds.
- **Auto-update** via Sparkle on macOS and WinSparkle on Windows, from
  one EdDSA-signed appcast. Checks run in the background daily; nothing
  installs itself without being asked.
- **Opt-in usage analytics**, off by default and off in the real sense —
  the SDK is not started, no device id is computed, and no request is
  made until *Share Usage Data* is switched on. What is sent is which
  effect is running, how the amount is configured, whether the bin got
  emptied, the app version and the OS. Never file names, paths, or
  anything about what was thrown away.
- **Windows support**: the Recycle Bin backend, a layered overlay window
  and per-monitor DPI handling.

### Notes

- macOS 14 or later. Windows 10 20H1 or later.
- Full Disk Access is required on macOS to read the Trash's size. Until
  it is granted the menu bar says so and the effect stays idle.
- The animation follows the Trash icon in the Dock, so it has nothing to
  draw on while the Dock is hidden. The menu bar reports this as
  "Trash icon is off screen".
- Near-zero cost when idle: the render clock stops when the bin is empty,
  when the icon is off screen, and when something covers it.
