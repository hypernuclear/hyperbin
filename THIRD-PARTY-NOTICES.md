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
| Inter (`resources/Inter-SemiBold.ttf`) | SIL Open Font License 1.1 | committed |

Inter's own name table records: *"This Font Software is licensed under the
SIL Open Font License, Version 1.1"*, © 2016 The Inter Project Authors
(https://github.com/rsms/inter). The OFL permits bundling in software; it
forbids selling the font on its own and requires that any modified version
not use the reserved name.

## Artwork drawn from the host

The bin the effects are drawn over is the operating system's own Trash or
Recycle Bin icon, read at runtime from the running shell. None of it is
redistributed in this repository or in a build; it is never written to disk.
