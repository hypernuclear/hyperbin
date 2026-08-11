#version 440

// Erase the fly wherever the bin's artwork is opaque, so it reads as
// passing behind the bin.
//
// This is a mask, not a copy of the icon: the Dock keeps drawing its own
// trash, and we only decide which fly pixels survive. That matters
// because the bin rect comes from an Accessibility poll and always lags
// the Dock slightly under magnification — a lagging *copy* shows as a
// visible ghost, whereas a lagging *mask* only hides flies a few pixels
// off, which is nearly impossible to notice on something this small.

layout(location = 0) in vec2 vSpriteCoord;
layout(location = 1) in vec2 vMaskCoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    vec4  binRect;
    float debugRect;
    float maskMode;
    vec4  debugColor;
};

layout(binding = 1) uniform sampler2D spriteTex;
layout(binding = 2) uniform sampler2D maskTex;

void main()
{
    bool inRect = all(greaterThanEqual(vMaskCoord, vec2(0.0)))
               && all(lessThanEqual(vMaskCoord, vec2(1.0)));
    // Debug: paint the mask region flat so its placement can be seen.
    // Must short-circuit BEFORE the mask is applied — otherwise the mask
    // erases the very rect this is meant to reveal, which is exactly how
    // the flag appeared to do nothing.
    if (debugRect > 0.5) {
        // Trace the actual occluder — the artwork's alpha, not its
        // bounding box — so what you see is exactly what gets erased.
        // A flat rect only tells you where the box is, which is the
        // wrong question once the box is roughly right.
        // OUTLINE, not fill. A filled silhouette hides the very icon you
        // are trying to align it against — and with several candidates
        // stacked, only the last one is visible at all.
        // A constant ~1px outline at ANY magnification. Sampling
        // neighbours at a fixed UV offset made the line scale with the
        // icon: hairline when small, a fat band that swamped the bin when
        // magnified. fwidth() gives the alpha's rate of change per screen
        // pixel, so the band stays one pixel wide either way.
        float band = 0.0;
        if (inRect) {
            float a = texture(maskTex, vMaskCoord).a;
            float w = max(fwidth(a), 0.0015);
            band = 1.0 - smoothstep(0.0, w * 1.6, abs(a - 0.5));
        }
        fragColor = debugColor * band * 0.72;
        return;
    }
    vec4 sprite = texture(spriteTex, vSpriteCoord);

    // Outside the bin rect there is nothing to hide behind.
    // Three ways a fly can relate to the bin:
    //   0  in front  — nothing hidden
    //   1  behind    — hidden WHERE the bin is (it occludes the fly)
    //   2  on it     — hidden where the bin ISN'T, i.e. clipped to the
    //                  silhouette, so a fly crawling to the rim slides out
    //                  of view around the side instead of floating off it
    float occl = 0.0;
    float a = inRect ? texture(maskTex, vMaskCoord).a : 0.0;
    if (maskMode > 1.5) {
        occl = 1.0 - smoothstep(0.25, 0.65, a);
    } else if (maskMode > 0.5) {
        // Slightly hard-edged: the artwork's own antialiased rim would
        // otherwise leave a faint halo of half-erased fly around the bin.
        occl = smoothstep(0.35, 0.75, a);
    }

    fragColor = sprite * qt_Opacity * (1.0 - occl);
}
