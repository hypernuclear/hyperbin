// Screen position, not object xy.
//
// The whole scene is translated onto the bin and the camera looks down at
// it, so neither world nor object coordinates line up with the artwork:
// world is off by the translation, and object xy ignores the tilt, which
// slides the refracted copy up by as much as a seventh of the bin's width
// on the parts of the body nearest the camera. What the fragment shader
// wants is where the pixel actually LANDS, in the bin's own pixels.
VARYING vec2 vScreen;
VARYING vec3 vNormalW;
VARYING float vThick;
VARYING float vHeight;
VARYING float vLump;
VARYING float vCap;
VARYING float vBehind;

float h31(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

// Value noise in three dimensions. Two, on VERTEX.xy, was the earlier
// mistake: this is a body of revolution, so the front and back of it map
// to the same xy and the noise came out mirrored front to back — which
// on a rotating lump reads as a pattern rather than as a surface.
float vnoise3(vec3 p)
{
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(h31(i),                  h31(i + vec3(1, 0, 0)), f.x);
    float b = mix(h31(i + vec3(0, 1, 0)),  h31(i + vec3(1, 1, 0)), f.x);
    float c = mix(h31(i + vec3(0, 0, 1)),  h31(i + vec3(1, 0, 1)), f.x);
    float d = mix(h31(i + vec3(0, 1, 1)),  h31(i + vec3(1, 1, 1)), f.x);
    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);
}

// The lumps come in two bands, and they are used for different things.
//
// COARSE moves the vertices. It has to stay something the mesh can
// actually represent: pushed too far or too fine, the surface folds and
// the silhouette grows spikes, which is what happened when one field did
// both jobs.
//
// FINE only tilts the normal. Detail below the vertex spacing cannot be
// in the geometry at all, but it is exactly what makes a surface read as
// lumpy rather than moulded — so it goes into the shading, where there
// is a pixel to put it in.
float coarseAt(vec3 q)
{
    // Fine enough that several lumps fit across the body. At three cells
    // across, one lobe of the noise covered most of the mesh and the
    // whole thing just leaned to one side.
    float s = 5.4 / binWidth;
    // A big slow lobe, first and heaviest. This is the one that changes
    // the body's SHAPE — it is nearly two bin-widths across, so where it
    // swells, a whole side of the gel swells with it over several
    // seconds. The finer octaves only ever shimmered the surface, which
    // is why the thing read as textured rather than as moving.
    // Weighted well under one. At 1.6 a single lobe was deeper than the
    // vertex spacing could follow and the surface folded over itself —
    // the body grew a crease down one side. Amplitude is not motion;
    // what makes this read as alive is that the lobes DRIFT.
    float l = (vnoise3(q * s * 0.42 + vec3(time * 0.13, time * 0.08, 0.0))
               - 0.5) * 0.80;
    l += vnoise3(q * s + vec3(0.0, time * 0.20, 0.0)) - 0.5;
    return l;
}
float fineAt(vec3 q)
{
    float s = 5.4 / binWidth;
    float l = (vnoise3(q * s * 2.4 + vec3(time * 0.28, 0.0, -time * 0.16)) - 0.5);
    l += (vnoise3(q * s * 5.1 + vec3(-time * 0.19, time * 0.31, 0.0)) - 0.5) * 0.65;
    return l;
}

// How far the surface moves out at a point: just the drifting lumps.
//
// There used to be bubbles here too — each one swelling the body as it
// rose past. They are gone for now; see qml/OozeVisual.qml. The loop
// sampled twelve texels three times per vertex and the spheres cost a
// second draw pass, and none of it was earning its energy.
// What the NORMAL is tilted by: the coarse field the vertices actually
// moved through, plus the fine detail that only exists here.
float shadeAt(vec3 q, float lumpScale, out float coarse, out float fine)
{
    coarse = coarseAt(q);
    fine = fineAt(q);
    return coarse * binWidth * 0.030 * lumpScale
         + fine * binWidth * 0.030;
}
void MAIN()
{
    vec3 nrm = normalize(NORMAL);

    // A tangent frame on the surface, so the displacement can tilt the
    // normal as well as move the vertex.
    //
    // Moving vertices alone was not enough to see. The body is smooth and
    // glassy, so all a displacement does is wobble the silhouette by a
    // pixel or two; every lump in the middle of the surface stays
    // invisible because the shading does not know it is there. Tilting
    // the normal by the displacement's own gradient is what turns it into
    // something the light can find.
    vec3 up = abs(nrm.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t1 = normalize(cross(up, nrm));
    vec3 t2 = cross(nrm, t1);

    // Lumpier at the foot. What has run down and collected there is
    // thicker and slower than what is still on the walls, and a puddle
    // as smooth as the body above it reads as a moulded base.
    float lumpScale = 1.0 + 0.9 * (1.0 - clamp(UV0.y * 2.2, 0.0, 1.0));

    // Small enough to resolve the FINEST octave in the field.
    //
    // At 0.035 of the bin's width the offset was wider than the shortest
    // wavelength, so the difference between the two samples was not a
    // gradient at all — it was two unrelated values of the noise. The
    // surface got noisier and no lumpier, which is a good description of
    // aliasing.
    float e = binWidth * 0.011;
    float lumpRaw, fine, ignored;
    float s0 = shadeAt(VERTEX, lumpScale, lumpRaw, fine);
    float su = shadeAt(VERTEX + t1 * e, lumpScale, ignored, ignored);
    float sv = shadeAt(VERTEX + t2 * e, lumpScale, ignored, ignored);
    // Only the coarse part moves the vertex.
    float d0 = lumpRaw * binWidth * 0.030 * lumpScale;

    // ...and a breath. One slow cycle over about ten seconds, swelling
    // the whole body a hair — a thing that is perfectly still reads as a
    // model rather than as something alive.
    float breath = 1.0 + 0.014 * sin(time * 0.62);

    vec3 pos = VERTEX * breath + nrm * d0;
    // The normal is tilted HARDER than the displacement strictly implies.
    // The body is smooth and the environment is a soft studio probe, so a
    // geometrically honest tilt moved the shading by almost nothing;
    // overstating it is what lets the light find the lumps at all, and it
    // costs nothing but a constant.
    vec3 n = normalize(nrm - (t1 * (su - s0) + t2 * (sv - s0)) * (1.5 / e));

    // Thickness comes from the mesh, in UV0.x. Deriving it from z put the
    // meniscus — which you look through the most of — at nearly zero.
    vThick  = UV0.x;
    vHeight = UV0.y;
    // Both bands, before the amplitudes and the foot's extra weight. The
    // fragment shader uses this to vary how much gel the light has to
    // cross — which is what turns the lumps into a change of COLOUR
    // rather than only a change of highlight, and on a body this glossy
    // under a probe this soft, colour is most of what is actually
    // visible.
    vLump   = lumpRaw + fine * 0.85;
    vCap    = UV1.x;
    // Behind the bin's own mid-plane, so the bin is between this and the
    // eye. Only the up-facing surfaces need it — the body's back half is
    // back-face culled and never gets here — but it is what tells the
    // rolled lip that its far side is hidden while its near side is not.
    vBehind = smoothstep(0.0, -binWidth * 0.10, VERTEX.z);

    // View space, relative to the bin's own origin. The camera is
    // orthographic at magnification one, so this IS the pixel offset from
    // the centre of the bin on screen — tilt and all — with no viewport
    // or projection maths to get wrong.
    mat4 mv = VIEW_MATRIX * MODEL_MATRIX;
    vScreen = (mv * vec4(pos, 1.0)).xy - (mv * vec4(0.0, 0.0, 0.0, 1.0)).xy;

    vNormalW = normalize(NORMAL_MATRIX * n);
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(pos, 1.0);
}
