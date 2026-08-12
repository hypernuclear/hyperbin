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
