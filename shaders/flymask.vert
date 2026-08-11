#version 440

// Fly sprite, with a second set of coordinates addressing the bin mask.
//
// Attribute names deliberately avoid a qt_ prefix: the shader baker
// treats those as its own and tries to rewrite them, which fails with
// "No rewriter-inserted attribute found" at runtime.
//
// The mask coordinate is derived from the vertex position rather than
// being a third attribute: the bin rect changes every frame under Dock
// magnification, and recomputing it here costs nothing while rewriting
// per-vertex UVs on the CPU would touch the whole buffer.

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 vSpriteCoord;
layout(location = 1) out vec2 vMaskCoord;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    vec4  binRect;      // x, y, w, h in item coordinates
    float debugRect;    // >0.5 paints the mask region (HYPERBIN_SHOW_BINRECT)
    float maskMode;     // 0 none, 1 occlude (behind), 2 clip (on surface)
    vec4  debugColor;
};

void main()
{
    gl_Position  = qt_Matrix * aPos;
    vSpriteCoord = aTexCoord;
    vMaskCoord   = (aPos.xy - binRect.xy) / binRect.zw;
}
