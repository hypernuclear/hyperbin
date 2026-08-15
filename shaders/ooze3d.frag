VARYING vec2 vScreen;
VARYING vec3 vNormalW;
VARYING vec3 vPosO;
VARYING float vThick;
VARYING float vHeight;
VARYING float vLump;
VARYING float vCap;
VARYING float vBehind;

// sRGB in, linear out.
//
// Everything below works in LINEAR light, because Qt does: a shaded
// CustomMaterial hands BASE_COLOR to the same lighting maths as
// PrincipledMaterial, and the scene's tonemapper gamma-encodes the result
// on the way out. Feeding it sRGB values — which is what a QML colour
// literal and an 8-bit icon both are — meant every midtone was encoded a
// second time. That is the whole of the "gamma looks high, everything is
// washed out" problem: 0.44 went in, came back as 0.69, and the green
// lost most of its saturation on the way.
vec3 toLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

// The meniscus where the gel meets an eye.
//
// This is the same division of labour the lumps use in the vertex shader,
// for the same reason. There, the coarse band moves vertices and the fine
// band only tilts the normal, because detail below the vertex spacing
// cannot be in the geometry at all. Here the eye is the thing below the
// spacing: the body is 64 segments round, and at the size these are, one
// segment is as wide as a whole eye. The broad swell fits in the mesh and
// is done there; the crease around the rim of the ball does not fit in
// the mesh at any amplitude, and pushing harder on the vertices only ever
// made the swell wider.
//
// A RING, and that is the whole point of it. Something pressed into a
// thick liquid is not surrounded by a mound — the liquid is held down by
// the ball itself and piles up at its edge, so the peak belongs just
// outside the silhouette. A shape that peaked at the centre would have to
// be tall enough to show at the rim, and anything that tall closed over
// the pupil.
//
// Returns the ring's height, 0..1, and accumulates the slope along the
// surface so the light has something to catch.
float eyeCollar(vec3 p, vec3 nrm, vec4 e, inout vec3 slope)
{
    if (e.w <= 0.0)
        return 0.0;
    vec3 rel = p - e.xyz;
    float u = length(rel) / e.w;
    float k = (u - 1.20) / 1.15;   // peak just outside the ball, gone by 2.35
    float w = 1.0 - k * k;
    if (w <= 0.0)
        return 0.0;
    // Away from the eye, along the surface. Near the ball's silhouette
    // this is very nearly the view direction, so it has to be projected
    // back onto the tangent plane or the tilt goes into the surface.
    vec3 t = rel - nrm * dot(rel, nrm);
    float tl = length(t);
    if (tl > 1e-5)
        slope += (t / tl) * (-2.0 * k / 1.15);
    return w;
}

void MAIN()
{
    // Where we are on the bin, 0..1.
    //
    // From the SCREEN position, not from object xy. The camera looks down
    // seventeen degrees, so a point on the front of the body is seen
    // against a lower part of the bin than the one directly behind it;
    // mapping by xy ignored that and slid the refracted copy up by as
    // much as a seventh of the bin's width at the front.
    vec2 uv = (vScreen - planeMin) / planeSize;
    uv.y = 1.0 - uv.y;

    // No silhouette trim here any more. The mesh IS the shape now — a
    // tapered volume of its own — and cutting it against the icon's
    // outline was what made the gel take the bin's form, slots and all,
    // instead of being a body the bin sits inside.
    vec3 n = normalize(vNormalW);

    // Deepest wins rather than stacking, as everywhere else here: two
    // eyes close together would otherwise raise a rim twice the size of
    // anything either could make alone. No check for whether the eye is
    // on the far side of the body — the ring dies out at a couple of eye
    // radii and the far side is most of a bin away, so the distance has
    // already answered it.
    vec3 slope = vec3(0.0);
    float collar = eyeCollar(vPosO, n, eye0, slope);
    collar = max(collar, eyeCollar(vPosO, n, eye1, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye2, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye3, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye4, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye5, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye6, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye7, slope));
    collar = max(collar, eyeCollar(vPosO, n, eye8, slope));
    // Overstated, and deliberately — the same constant fudge the vertex
    // shader applies to the lumps and for the same reason. The body is
    // smooth and the environment is a soft studio probe, so a
    // geometrically honest tilt moves this shading by almost nothing. An
    // honest 0.55 went in first and the ring was, measured against a flat
    // red debug fill, landing exactly where it should and shading like it
    // was not there at all.
    n = normalize(n - slope * 1.70);

    // The lumps thicken and thin the body as well as bending its surface.
    // Shape alone was nearly invisible: a smooth gel under a soft studio
    // probe shades almost identically whichever way its normal points, so
    // the animation was running and could not be seen. Varying how much
    // gel the light has to cross is what makes it read as something
    // alive, because it changes the COLOUR and not just the highlight.
    // The collar is gel piled up, so there is more of it to see through
    // here — which is what turns the ring from a highlight into something
    // with depth, for the same reason the lumps feed this at all.
    float thick = clamp(vThick * (1.0 + vLump * 0.55) + collar * 0.55, 0.0, 1.0);

    // --- refraction ---------------------------------------------------
    // Two parts, and the second is what makes it read as something
    // INSIDE the gel rather than a tint over it:
    //
    //   * the surface normal bends the lookup, which follows the lumps;
    //   * the whole image is magnified about the body's centre, so what
    //     is behind appears to sit deeper in and swims as the surface
    //     moves. A lens does this; a flat offset does not, and without
    //     it the refraction is invisible because it samples an image
    //     that is already aligned with itself.
    vec2 centre = vec2(0.5);
    vec2 lensed = centre + (uv - centre) * (1.0 - magnify * thick);
    vec2 bend = n.xy * (refractAmount / planeSize) * (0.35 + thick);
    vec2 base = lensed + bend;

    float ca = dispersion * 0.03 * thick;
    vec4 behind;
    behind.r = texture(iconTex, base + n.xy * ca).r;
    behind.g = texture(iconTex, base).g;
    behind.b = texture(iconTex, base - n.xy * ca).b;
    behind.a = texture(iconTex, base).a;
    // Past the artwork's edge there is nothing to see through: clamping
    // alone would smear its last row of pixels down the whole puddle.
    vec2 edge = step(vec2(0.0), base) * step(base, vec2(1.0));
    behind.a *= edge.x * edge.y;

    // --- the bin's shape, cut out of the lid ---------------------------
    // The gel's surface is INSIDE the bin, and at this angle the bin's
    // own walls hide all of it: the front wall stands between the eye and
    // the near half, and the far half is behind the bin altogether. So
    // the whole of the meniscus that falls within the bin's outline is
    // simply not visible, and what is left of the gel's top is the collar
    // outside its walls.
    //
    // An earlier version worked this out arc by arc — the rim circle
    // projected through the scene's camera, cutting only below its near
    // edge. It was right as far as it went, but it left the far side of
    // the meniscus arcing up ACROSS the bin's back rim, which is behind
    // the bin and should never have been drawn, and its hole was narrower
    // than the opening. The silhouette answers both at once.
    // Two ways to be hidden, and the lip needs both. Its inner half is
    // the surface itself, which the bin's walls cover at this angle
    // whichever side it is on; its outer half is a bead standing proud of
    // the wall, visible in FRONT of the bin and hidden behind it. Cutting
    // on vCap alone left the back of the bead arcing over the bin's far
    // rim — the one part of it that is definitely occluded.
    float hidden = clamp(vCap + vBehind, 0.0, 1.0);
    float lidHole = hidden * smoothstep(0.15, 0.50,
                                        texture(iconTex, uv).a * edge.x * edge.y);
    // DISCARD, not just alpha.
    //
    // Taking the alpha to zero was not enough and the reason is worth
    // knowing: Qt scales the diffuse term by opacity but adds the
    // specular on top of it regardless — a sheet of glass is transparent
    // and still has a highlight. The cap's normal points at the sky, so
    // what survived was a big soft reflection of the light probe in
    // exactly the shape of the surface that was supposed to be hidden.
    // Everything about the cut was working; the evidence for it was a
    // pale ellipse that looked identical to no cut at all.
    if (lidHole > 0.5)
        discard;

    // Absorption, the way a coloured liquid actually does it: what comes
    // out is what went in, minus what the medium took, per channel and
    // exponential in the distance travelled.
    //
    // The previous version tinted by INTERPOLATING toward green, which
    // works only if you already know how bright the backdrop is. Tuned
    // against a dark stand-in glyph it looked right; over the real Trash
    // — a pale translucent mesh — there was nothing to darken and the
    // whole body came out white. Beer-Lambert has no such dependency: a
    // white backdrop through green gel is green, which is the entire
    // point of the effect.
    vec3 tintL = toLinear(tint.rgb);
    vec3 sigma = (vec3(1.0) - tintL) * absorption * 3.0;

    // What the gel is looking at. Two things, and the second is what
    // finally made the bin visible inside it:
    //
    //   * past the icon's edge there is no icon, so the gel is looking at
    //     the desktop. Standing that in with a light neutral and running
    //     it through the SAME absorption is what keeps the two
    //     continuous; a separate body colour for the uncovered part
    //     stepped at the alpha edge and drew a hard green rectangle
    //     across the body wherever the artwork ended.
    //
    //   * the bin is an object IN the gel, not a picture behind it. Where
    //     it is solid it blocks the light coming past it, so it has to
    //     read DARKER than the gel around it. Sampling its colour alone
    //     put a pale grey mesh against a pale grey stand-in — almost the
    //     same value — and the bin simply had no silhouette.
    // The neutral is a MID grey, not a bright one. Standing the desktop
    // in at 0.88 against an icon body around 0.55 made the covered part
    // four times darker than the uncovered part, and the gel came out
    // looking like two different materials meeting at the bin's outline.
    // A desktop and a trash can are roughly as bright as each other.
    vec3 backdrop = mix(toLinear(vec3(0.72)), toLinear(behind.rgb) * 0.92,
                        behind.a);
    // A floor under the path length, and a short span above it. The
    // thickest part of the body is also the part with the bin behind it,
    // so a wide range here darkens the middle twice over and the gel goes
    // black exactly where it most needs to be readable.
    vec3 trans = exp(-sigma * (0.60 + 0.35 * thick));

    // Absorption AND scattering. Absorption alone is a filter, and a
    // filter over something dark can only produce something darker — so
    // the middle of the body, which is both the thickest gel and the part
    // with the bin's dark interior behind it, went to near black. Real
    // gel is not a filter: light bounces around inside it and some comes
    // back out, which is why a jar of it glows in its own colour rather
    // than turning into a silhouette. That is this second term, and it
    // is what puts a floor under the whole body.
    vec3 col = backdrop * trans + tintL * scatter * (vec3(1.0) - trans);

    // Denser at the foot. Liquid that has run down and settled is deeper
    // where it pooled, and a body of uniform colour top to bottom is the
    // one thing that reads as plastic rather than as something poured.
    col *= mix(0.90, 1.0, clamp(vHeight * 1.4, 0.0, 1.0));

    // One alpha, everywhere but the hole.
    //
    // Fading out past the icon's edge double-counted the backdrop: the
    // colour above already assumes something behind to absorb, and then
    // the blend let the real background through on top of it. What the
    // eye got was the desktop at forty per cent through a body that had
    // already been shaded as though it were covered — a washed-out rim
    // all the way round. The gel draws its own refracted copy of the bin,
    // so it can afford to be nearly opaque and simply BE the colour it
    // computes.
    BASE_COLOR = vec4(col, opacityAmount * (1.0 - lidHole));
    // Low roughness with a clearcoat is what makes it wet rather than
    // rubbery; the environment does the rest.
    // Not a mirror: at 0.07 with a clearcoat this read as chromed metal.
    METALNESS = 0.0;
    FRESNEL_POWER = 2.5;
    // Wettest at the meniscus. The crest of the collar is the thinnest,
    // most sharply curved gel anywhere on the body, and on a real one
    // that is where the light runs in a line. It is also the cue that
    // does the most work here: the ring's SHAPE is nearly invisible under
    // a probe this soft, but a bright edge at the ball's rim reads
    // immediately as the two things touching.
    float wet = smoothstep(0.45, 1.0, collar);
    ROUGHNESS = 0.24 - 0.09 * wet;
    SPECULAR_AMOUNT = (0.40 + 0.50 * wet) * (1.0 - lidHole);
}
