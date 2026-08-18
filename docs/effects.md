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
graph, which matters because the tests link it and must stay headless.

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

### Pinning down one gesture

A vocabulary of moves picked by a hash cannot be verified by screenshot:
catching a particular one is luck, and *"it looked right when I happened
to see one"* is how the tentacles' roll-up shipped invisible for two
rounds — it was being computed and then erased every frame, and every
grab that would have shown it was of some other move.

```sh
HYPERBIN_PREVIEW_CURSOR=300,180   # pin the pointer, in the item's pixels
HYPERBIN_FORCE_MOVE=1             # every arm runs this move, in enum order
HYPERBIN_DEBUG=1                  # shout when a target or a joint teleports
HYPERBIN_TRACE=1                  # ...and log EVERY frame: arm, move, u, jump
--empty-at 2500                   # empty the bin, to see a leaving animation
```

`HYPERBIN_FORCE_MOVE` alternates the forced move with an idle, so the move
under test always starts from rest. Run back to back it would start from
wherever its own last instance left the arm — which read the slap's
wind-up as moving the arm *outward*, the exact opposite of what it does,
because the frames were showing the previous slap still recovering.

Prefer `HYPERBIN_TRACE` over the threshold-triggered lines from
`HYPERBIN_DEBUG` when comparing two versions of a move. A count of alarming
frames is the wrong statistic: lengthen a move and it accrues more of them
while being calmer per frame. That reading once said a re-timed slap was
twice as bad when its per-frame motion had in fact more than halved.

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

### How many triangles

Fewer than feels right, and the limit is not performance.

Three arms of 3,000 vertices at 30fps is 270,000 vertex-shader invocations
a second, which is nothing — what this app pays for is frame cadence and
keeping the GPU from idling, and per-vertex work is below fill cost in
`docs/battery.md`, which already calls fill "effectively free at this
size". Triangles are free. Frames are not. So the budget is set by the
point where more geometry stops *showing*, not by a cost ceiling.

That point can be worked out rather than guessed. A tube of screen radius
`r` px approximated with `K` sides deviates from the circle by
`r(1 - cos(pi/K))`, so keeping the facets under half a pixel needs
`K > pi / acos(1 - 0.5/r)`:

| drawn at | arm radius | sides needed |
| --- | --- | --- |
| 49pt Dock icon, 2x | 12 px | 11 |
| 96pt Dock icon, 2x | 24 px | 16 |

**Sixteen sides** covers a Dock icon twice the default size. The same sum
along the arm, against the outer edge of the sharpest strike (4.4 radians —
see `TentacleEffect::kArmLength`), wants **20 to 24 rings**. That is the
*floor* — about 500 triangles — not a target. 2,000 is comfortable: 24
sides by 42 rings, which is round enough for a Dock icon four times the
default and nearly twice the rings the sharpest strike needs.

**Count vertices, not triangles.** The bend runs in the vertex shader and
the `.mesh` file is 67 bytes per vertex, so vertices are the number that
costs — and the two are not proportional. The arm in the tree is 6,568
triangles for 4,070 vertices; a downloaded model it replaced was 2,728
triangles for **2,920** vertices, from only **1,375 unique positions**. 53%
of that one was a single position exported more than once — not flat
shading (just 63 of 1,209 shared positions carried different normals) but a
UV map in many islands, on a mesh whose positions had been welded and whose
UVs had not. Blender's statistics showed 1,375 and gave no hint of it.

So a *larger* triangle count, unwrapped with one seam, can cost a third of
a smaller one. Check the vertex count after export, not in the viewport.

Two things that model got wrong and are worth not repeating:

* **Distribution.** It put 45% of its vertices in the top quarter of the
  arm, where the radius had fallen to 1–2.7 units of a 12.5-unit base —
  below a pixel at Dock size — while the thick part that is actually seen
  got 80 vertices a band. That is what decimating a sculpt does. Build the
  low-poly tube first, sculpt on a multires or duplicated copy, and bake
  down; then the budget lands where it was put rather than where the
  decimator left it.
* **Taper.** It fell to a tenth of its peak radius by the tip, so most of
  its length was thin whatever it was scaled by, and the girth multiplier
  in `TentacleVisual.qml` was fighting that rather than setting it. The
  replacement holds a third of its peak at the tip and reads as thick along
  its whole length on its own — measured on screen, its cross-section
  varies only between 0.12 and 0.16 of the bin's width from mid-arm to tip.

### Low-poly that does not look low-poly

* **Smooth (averaged) vertex normals.** The single biggest lever, and it is
  free: a 16-sided tube shaded smooth reads as round, and the same tube
  flat-shaded reads as a hex bolt. In Blender that is Shade Smooth with no
  Edge Split modifier and no custom split normals. A vertex count above the
  triangle count is the tell that something is splitting them.
* **The silhouette is the only thing a normal map cannot fake.** So spend
  geometry on exactly two things — how round the tube is, and how smoothly
  it bends — and put suckers, wrinkles and veins in the maps, baked from a
  high-poly sculpt. `shaders/tentacle.frag` already reads a normal map
  through `TANGENT`/`BINORMAL`, so export tangents and one UV map or its
  orientation is undefined.
* **Do not taper to a point.** The last rings of a spike are sub-pixel and
  only shimmer. End on a small rounded cap.
* **Taper less than looks right in the viewport.** The current model is at
  55% of its base radius by mid-length and 21% by three-quarters, which is
  why it reads as thin however it is scaled — scaling cannot fix a taper.
* **A cylindrical unwrap with one seam on the underside** keeps the maps
  continuous along the arm. A non-square texture (256x1024) spends its
  texels evenly on something five times longer than it is wide.

### What the vertex shader expects of the mesh

`shaders/tentacle.vert` bends the arm from `VERTEX.y` alone, so the mesh
carries a contract and no rig — a skeleton was tried and went unused,
because nothing needs a joint to know how far along an arm a vertex sits:

* **Straight up +Y, base at the origin**, and the axis on `x = z = 0`. The
  shader splits `VERTEX.xz` into the bend plane and across it, treating it
  as an offset from the centre line; the current import's axis is off by
  about 1.9 units, which it silently carries through the bend.
* **Apply every transform before export.** An unapplied armature scale of
  15.5 cost a debugging session once already.
* **Author it 100 units long.** The length goes into the shader as a
  literal (`meshLength`), so a round number is one less magic constant.
* **No animation actions.**

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

The tests link the simulation only, never the renderer. Keep the
behaviour in a plain class with no Qt GUI dependency (as `FlySim` is) and
the effect a thin wrapper around it, and the interesting parts stay
testable without a display.

Qt Test, one executable per area under `tests/`, every test function
registered with CTest on its own:

```sh
cd build && ctest --output-on-failure
ctest -R Tentacle          # just the solver
ctest -R Flies.walking     # one check
```

Registering each function separately is not bookkeeping. The harness this
replaced ran everything from one `main()` and aborted on the first
`return fail(...)`, so a broken check hid every check after it — and it
did: the bend-limit test failing meant the wave, settling and
`pushOutside` checks never ran at all.

Every hard-won behaviour in the fly simulation has an assertion, and
several of them exist because a "fix" quietly broke something else two
turns later. When you tune a number, check whether a test now encodes the
old taste rather than a real invariant — and widen it deliberately rather
than deleting it.

**Set a threshold by breaking the thing it guards.** A test that passes
against code with the feature ripped out is worse than no test, because it
reads as coverage. Three of the tentacle checks were written, passed, and
turned out to be exactly that — one asserted on a joint the code writes
directly, so it could never fail; one aimed at a target the solver reached
without the routine under test; one sampled a 6-second window of a
27-second cycle and made correct code look twice as bad as broken code.
Delete the implementation, watch the test go red, then put it back. The
figures those runs produce are what the bound should be built from, and
`tests/tentacle/tst_tentacle_chain.cpp` quotes them beside each one.
