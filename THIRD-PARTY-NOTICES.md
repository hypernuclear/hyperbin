# Third-party notices

hyperbin's own source is MIT — see `LICENSE`. This file covers everything
else that ends up in, or is linked by, a build.

Every entry below was checked against the licence text actually present in
the dependency rather than from memory: the Qt module pages, the font's own
name table, and the `LICENSE` files in the vendored sources.

## Qt 6 — and the one that decides what you may ship

**Qt is the only dependency whose terms constrain the finished binary**, and
which terms apply depends on **how the Qt you linked was obtained**, not on
what this repository says.

| Module | Open-source terms |
|---|---|
| **Qt Quick 3D** | **GPLv3 or commercial only — there is no LGPLv3 option** |
| Qt Core, Gui, Widgets, Svg, Qml, Quick, Quick Controls 2, Test | LGPLv3 |
| Qt Shader Tools (runtime library) | LGPLv3 or GPLv2 |
| `qsb` (build-time shader compiler) | GPLv3, with The Qt Company GPL Exception 1.0 |

Three consequences worth stating plainly:

* **Built against open-source Qt**, the resulting binary must be distributed
  under GPLv3, because Qt Quick 3D is GPL-only and the ooze and tentacle
  effects are both Quick3D scenes. Publishing this repository satisfies the
  corresponding source obligation.
* **Built against commercially licensed Qt**, that obligation does not
  arise and the binary may be distributed on whatever terms you choose.
* `qsb` is a **tool**, not a library. The Qt Company GPL Exception 1.0
  covers "the output of this application", so compiling shaders with it does
  not place the compiled shaders or the app under GPLv3.

The distinction is about the build, not the repo. A third party who clones
this and builds it with open-source Qt produces a GPLv3-encumbered binary
even though this source is MIT.

## Bundled and linked components

| Component | Licence | How it arrives |
|---|---|---|
| [Sparkle](https://sparkle-project.org) | MIT | fetched at configure time (macOS updater) |
| [WinSparkle](https://winsparkle.org) | MIT | fetched at configure time (Windows updater) |
| [Countly C++ SDK](https://github.com/Countly/countly-sdk-cpp) | MIT | git submodule under `third_party/`; optional, `-DHYPERBIN_ANALYTICS=OFF` removes it |
| Satoshi | ITF Free Font License | fetched at configure time, compiled in; never committed (below) |
| [Ashima Arts' webgl-noise](https://github.com/ashima/webgl-noise) (3D simplex noise) | MIT, © 2011 Ashima Arts | copied into `shaders/buttonfire.frag`, with its notice; the welcome window's button fire |

### Platform marks

`qml/PlatformMarks.qml` draws the Apple logo and the Windows logo beside
each download in the welcome window, only to say which systems an app runs
on. They are trademarks of Apple Inc. and Microsoft Corporation and are not
covered by this repository's MIT licence.

### Satoshi: fetched at build time, never committed

The app sets all of its type (the splash and the welcome window) in
Satoshi, by Indian Type Foundry, under the
[ITF Free Font License](https://www.fontshare.com/licenses/itf-ffl) from
[Fontshare](https://www.fontshare.com/fonts/satoshi). That licence allows
embedding the font in an application but not redistributing the font files
(through a repository, among other ways) or modifying them. So the files
are not in this repository: CMake downloads Fontshare's own zip at
configure time and compiles four of its OTFs, unmodified, into the binary.
Whoever builds this gets their own copy from Fontshare under that licence.
There is no fallback face: a configure that can't download it fails.
Satoshi is not covered by this repository's MIT licence.

## Artwork drawn from the host

The bin the effects are drawn over is the operating system's own Trash or
Recycle Bin icon, read at runtime from the running shell. None of it is
redistributed in this repository or in a build; it is never written to disk.
