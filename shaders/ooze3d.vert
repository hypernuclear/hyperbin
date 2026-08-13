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
// A wave travelling DOWN the body, on top of the noise.
//
// The ridges in the mesh are where the goo paused on its way down; this
// is the going-down itself. It is a plain sine in height, which is why
// it reads as one motion rather than as more texture — every noise
// octave here is deliberately incoherent, and nothing incoherent ever
// looks like flow.
float waveAt(vec3 q)
{
    return sin(q.y * (6.2831853 / (binWidth * 0.38)) - time * 0.85);
}
// Blisters in the puddle: a few rounded domes, as though gas had worked
// its way up and set there. A quadratic falloff rather than a noise
// octave, because what is wanted is a handful of distinct ROUND things,
// and noise of any spectrum gives an even scatter of bumps instead.
float blisterAt(vec3 q)
{
    float s = 0.0;
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        // Golden-angle spread, so five of them never line up, drifting
        // slowly enough to read as the puddle still settling.
        float ang = fi * 2.399963 + time * 0.028;
        float rad = binWidth * (0.16 + 0.10 * fract(sin(fi * 17.31) * 43758.5));
        vec2 centre = vec2(sin(ang), cos(ang)) * rad;
        float d = length(q.xz - centre) / (binWidth * 0.13);
        s = max(s, max(0.0, 1.0 - d * d));
    }
    return s;
}
// Where the puddle has gathered.
//
// A ring of even thickness is the one thing a spill never is: it runs to
// wherever the last of it happened to go and piles up there. Two low
// harmonics give a couple of fat lobes and a couple of thin spots, and
// they turn — slowly enough to read as the goo still finding its level
// rather than as a puddle sliding about, and slowly is the point. Baked
// in, an asymmetry this large would be a permanent lean, which is what
// the rim's undulation used to be and what it looked like.
float gatherAt(vec3 q)
{
    float a = atan(q.x, q.z + 1e-4);
    // Nothing right on the axis, where the angle is meaningless and the
    // pool's underside closes to a point.
    float away = min(1.0, length(q.xz) / (binWidth * 0.10));
    return (sin(a * 2.0 + time * 0.045) * 0.60
          + sin(a * 3.0 - time * 0.028 + 1.7) * 0.40) * away;
}
// What the NORMAL is tilted by: the coarse field the vertices actually
// moved through, the wave they travel on, the puddle's gathering, and
// the fine detail that only exists here.
float shadeAt(vec3 q, float lumpScale, float poolW, out float coarse,
              out float fine)
{
    coarse = coarseAt(q);
    fine = fineAt(q);
    return coarse * binWidth * 0.030 * lumpScale
         + fine * binWidth * 0.030
         + waveAt(q) * binWidth * 0.026 * (1.0 - poolW)
         + gatherAt(q) * binWidth * 0.042 * poolW
         + blisterAt(q) * binWidth * 0.055 * poolW;
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
    // The puddle's share of this vertex: all of it at the floor, none of
    // it by the time the body has started tapering. The travelling wave
    // is windowed by its complement — a puddle has settled, and running
    // the flow through it made it churn like the walls above.
    float poolW = 1.0 - smoothstep(0.02, 0.20, UV0.y);
    float e = binWidth * 0.011;
    float lumpRaw, fine, ignored;
    float s0 = shadeAt(VERTEX, lumpScale, poolW, lumpRaw, fine);
    float su = shadeAt(VERTEX + t1 * e, lumpScale, poolW, ignored, ignored);
    float sv = shadeAt(VERTEX + t2 * e, lumpScale, poolW, ignored, ignored);
    // The coarse noise and the wave move the vertex; the fine band does
    // not, because the mesh cannot hold it.
    float d0 = lumpRaw * binWidth * 0.030 * lumpScale
             + waveAt(VERTEX) * binWidth * 0.026 * (1.0 - poolW)
             + gatherAt(VERTEX) * binWidth * 0.042 * poolW
             + blisterAt(VERTEX) * binWidth * 0.055 * poolW;

    // ...and a breath. One slow cycle over about ten seconds, swelling
    // the whole body a hair — a thing that is perfectly still reads as a
    // model rather than as something alive.
    float breath = 1.0 + 0.014 * sin(time * 0.62);

    // Never inside the bin.
    //
    // UV1.y is how much clearance this vertex has — the gap between the
    // gel's own radius here and the bin's. The undulations swing both
    // ways, and on a body only a tenth proud of the icon a trough was
    // enough to pull the goo in behind the outline it is supposed to be
    // running down. Outward is unbounded; inward stops at the bin.
    d0 = max(d0, -UV1.y);
    vec3 pos = VERTEX * breath + nrm * d0;
    // The rim's own undulation, and it DRIFTS.
    //
    // This used to be baked into the mesh, which made it a permanent lean
    // — the same side always lower, frame after frame, which the eye
    // reads as the whole thing sitting off-centre rather than as
    // something sagging. Two harmonics turning at different rates never
    // settle anywhere, so there is no side to favour.
    float ang = atan(VERTEX.x, VERTEX.z + 1e-4);
    float rimWave = sin(ang * 3.0 + time * 0.21) * 0.55
                  + sin(ang * 5.0 - time * 0.13 + 2.3) * 0.45;
    // Only near the top, and fading out as the rings close: the dome's
    // apex is one point shared by every angle, and an angular offset
    // applied there pulls it apart.
    float rimW = smoothstep(0.55, 1.0, UV0.y)
               * min(1.0, length(VERTEX.xz) / (binWidth * 0.15));
    pos.y -= rimW * binWidth * 0.030 * (0.5 + 0.5 * rimWave);
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
