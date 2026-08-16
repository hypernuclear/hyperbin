# Writing an effect

An **effect** is one animation over the bin — flies, ooze, whatever comes
next. One runs at a time; they are alternatives, not layers. Nothing in
the app composites two of them, and nothing should.

Adding one means implementing `Effect` (`src/core/Effect.h`) and adding a
line to `EffectRegistry.cpp`. It means touching neither the host item,
nor the menu, nor the power policy, nor the settings — all four are
driven from the registry.

## What the host gives you

`EffectItem` owns everything every effect needs and no effect should
re-implement:

| | |
|---|---|
| `setBinRect` | the icon's bounding rect, in item coordinates |
| `setSurface` | the icon's **silhouette** as a coverage grid |
| `setFullness` | 0–1, already resolved from the user's density and threshold settings |
| `setCursor` | polled, because the overlay is click-through and gets no hover events |
| `updateNode(…, mask)` | the icon's artwork as a texture, for the occlusion modes |

**Use the silhouette, not the rect.** This is the single mistake that has
cost the most here. The mask shader clips to the artwork's alpha, and the
trash artwork covers well under half of its own tile — so anything placed
by bounding rect lands where the renderer then erases it. The symptom is
brutal to diagnose: the simulation reports a busy bin and the screen shows
an empty one.

## The three states, and why they are three

This is the part to get right.

```
isEmpty()   nothing is drawn          -> the overlay can be torn down
isAtRest()  drawn, but will not change -> keep the frame, stop the clock
neither     animating                  -> paying full price
```

Flies only ever needed the first: they leave, so "nothing to draw" and
"the frame is final" are the same condition, and `FliesEffect::isAtRest()`
just returns `isEmpty()`.

Anything that leaves something behind needs the second. A settled ooze
puddle costs nothing to leave on screen and a full frame rate to redraw
sixty times a second. `PowerPolicy` treats these differently on purpose:
`setEffectIdle()` hides the surface, `setEffectAtRest()` only stops the
clock.

`isDismissed()` is separate again: the pointer is on the bin and the
effect has finished reacting to it. It is temporary and reverses when the
pointer leaves, which the host watches for on a 200ms timer — the only
wakeup in the app in that state.

**Emit `activityChanged()` whenever any of these flip.** Emitting
spuriously is harmless. Failing to emit is a stuck animation or, worse, a
timer that never restarts.

## The rules

1. **Report rest honestly.** The zero-CPU behaviour is measured, not
   assumed — see `docs/battery.md`. An effect that never rests undoes it.
2. **Stay inside your margins.** `margins()` sizes the overlay window.
   Anything drawn outside is clipped by the window edge and looks broken,
   so containment has to be a guarantee, not a steering force. The fly
   simulation learned this the hard way: at speed, a fly covers more
   ground while turning than the region is wide, so there is a hard
   positional backstop outside the soft boundary.
3. **Drain, don't vanish.** When the bin empties, fullness goes to 0 and
   whatever is on screen should leave under its own power. Popping out of
   existence reads as a bug even when it is instant.
4. **Never appear or disappear on top of the bin.** The icon is a fixed
   reference the eye anchors to, so anything fading against it reads as
   winking out rather than arriving or leaving.
5. **Own your GPU resources**, and release them in `releaseResources()` —
   called on the render thread. The mask texture is *not* yours; the host
   builds it from the icon and hands it over each frame.

## Settings

Per-effect settings are namespaced by id (`flies/density`,
`ooze/viscosity`), so two effects cannot collide. The selected effect is
stored as its **id string**, not an enum ordinal — ordinals silently
change meaning when the list is reordered.

`Settings` deliberately does not know which effects exist. It stores an
opaque string; `EffectRegistry` resolves an empty or unknown id to its
default. That keeps the value store out of the renderer's dependency
graph, which matters because `simtest` links it and must stay headless.

## Looking at it

Judging how an effect *looks* by screenshotting a 49-point Dock icon does
not work: at that size the whole thing is twenty pixels tall and every
feature worth arguing about is sub-pixel. Use the preview harness.

```sh
HYPERBIN_PREVIEW=ooze ./hyperbin --windowed          # live, big
HYPERBIN_PREVIEW_SHOT=/tmp/a.png                     # grab, then exit
HYPERBIN_PREVIEW_SHOT_MS=12000                       # ...at this moment
```

Two things it took several wasted rounds to learn:

* **It draws the shell's own artwork**, not the app's mark. The Trash is
  a pale translucent mesh with solid contents behind it; a clean opaque
  glyph is a different subject entirely, and an effect tuned against one
  can be unrecognisable over the other. The tint that looked right over
  the glyph turned the real bin white.
* **Two grabs at different times is the only proof an animation runs.**
  Twice, a motion described as "subtle" turned out to be no motion at
  all. `HYPERBIN_PREVIEW_SHOT_MS` exists for exactly that: shoot the same
  scene half a second apart and difference the two.

## Bringing in a model

Most geometry here is generated, not imported — `OozeGeometry.cpp` sweeps
the whole gel body from a measured profile, and that is the pattern to
reach for first. Import a model when the shape is *sculpted* and could not
be described by a profile and a few numbers.

There is no runtime asset loading. `Quick3DAssetImport` is not linked, and
`scripts/package-macos.sh` prunes the asset-loading plugins on the way into
the DMG. Models are converted **once, by hand**, and the converted output
is what ships:

```
$QT_ROOT/bin/balsam --generateMipMaps -o resources/ model.glb
```

Keep the `.mesh` and the maps; throw away the `.qml` balsam writes beside
them — it is a scene wrapper this app has no use for. Commit the source
`.glb` under `assets/` so the conversion can be repeated; the eye mesh's
source was not kept, and it cannot be regenerated or adjusted.

Files under `resources/` reach QML through the `hyperbin_icons` block in
`CMakeLists.txt`, which is `PREFIX "/icons" BASE resources`. So
`resources/meshes/eye.mesh` is `qrc:/icons/meshes/eye.mesh` — the `/icons`
prefix is inherited by everything in that bucket whether or not it is an
icon.

Two things worth knowing before modelling:

* **Detail below a couple of pixels is wasted.** The bin is drawn around
  49 points across. Put surface detail in the normal and albedo maps,
  where there is a pixel to hold it, and keep the silhouette simple.
* **Drop what will not be seen.** The eye model's cornea was a second,
  transparent mesh; it was left out because a transparent PBR shell costs
  a full extra pass to produce a highlight that is sub-pixel here.

Animation clips are a separate question and the answer so far has been no:
balsam emits them as `QtQuick.Timeline`, which is a module this app does
not link and the packaging script does not ship. Skinning
(`Skeleton`/`Joint`) is part of `QtQuick3D` proper and costs nothing extra,
but the house pattern is one static mesh deformed in its vertex shader —
see `shaders/ooze3d.vert`.

## Colour, if you write a Quick3D material

Three things about Qt's pipeline that are not obvious and each cost a
round of "why does it look wrong":

* **A shaded material's `BASE_COLOR` is LINEAR light.** Qt's fragment
  epilogue is `colour = tonemap(colour) * alpha`, and in the default
  `TonemapModeLinear` the tonemap *is* a linear-to-sRGB encode. A QML
  colour literal and an 8-bit icon are both sRGB, so they need
  `pow(c, 2.2)` on the way in. Skip it and every midtone is encoded twice:
  0.44 goes in and comes back 0.69, washed out and desaturated.
* **An unshaded material's `FRAGCOLOR` is not tonemapped.** Same scene,
  opposite rule. Measured, not assumed — write a flat 0.5 and read the
  pixel back.
* **Shaded output is premultiplied**, so blend with `One`, not
  `SrcAlpha`. `SrcAlpha` multiplies by alpha a second time.

And one that looks like a bug in your own maths: **alpha scales the
diffuse term but not the specular.** A fragment taken to `alpha = 0` still
reflects the light probe — a sheet of glass is transparent and still has a
highlight. To make something genuinely disappear, `discard` it.

## Testing

`simtest` links the simulation only, never the renderer. Keep the
behaviour in a plain class with no Qt GUI dependency (as `FlySim` is) and
the effect a thin wrapper around it, and the interesting parts stay
testable without a display.

Every hard-won behaviour in the fly simulation has an assertion, and
several of them exist because a "fix" quietly broke something else two
turns later. When you tune a number, check whether a test now encodes the
old taste rather than a real invariant — and widen it deliberately rather
than deleting it.
