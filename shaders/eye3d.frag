VARYING vec3 vDirW;

// sRGB in, linear out. Same reason as the gel's: a shaded CustomMaterial
// hands BASE_COLOR to Qt's own lighting and the scene tonemaps on the way
// out, so a colour literal or an 8-bit map fed in raw is gamma-encoded
// twice.
vec3 toLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

float h31(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}
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

// The lids, drawn ON the eyeball.
//
// The obvious build is a second sphere around this one, split and peeled
// back, wearing the gel's own material. It cannot work here, and the
// reason is worth writing down because it is not about the modelling:
//
//   * the gel is drawn AFTER the eyes and is transparent, so it is depth
//     tested and does not depth write. Wherever the ball is nearer than
//     the gel, the ball wins outright — so nothing in the gel's fragment
//     shader can ever cover an eye, however opaque it makes itself.
//
//   * the gel CAN cover one by moving, which is what the socket does.
//     But the body is 64 segments round and at these sizes one segment
//     is as wide as a whole eye, so the gel can only close over one as a
//     blob. A blink is a fast, crisp edge; that read as mush.
//
// The ball has neither problem. Its mesh is dense, and it is the nearest
// surface, so it owns these pixels by construction. What makes it read as
// goo rather than as an eyelid is that it is shaded through the SAME
// absorption and scattering the gel uses, from the same tint: where the
// cover is thin the eyeball still shows through it, and it goes opaque
// further in. The lid is not a colour, it is depth of goo.
void MAIN()
{
    vec4 albedo = texture(eyeAlbedo, UV0);
    vec3 eyeCol = toLinear(albedo.rgb);
    vec3 n0 = normalize(NORMAL);

    // --- how far under the goo -----------------------------------------
    // Latitude up the face of the ball you can actually SEE: the camera's
    // up, squared off against this eye's own gaze. Three frames were
    // tried and the first two each failed somewhere specific.
    //
    //   * the model's own frame rolls with every saccade, so the lids
    //     swam about as the eye darted.
    //
    //   * the scene's up is right for an eye on the body's wall and
    //     exactly wrong for one in the puddle. That surface is nearly
    //     horizontal, so a puddle eye's pole faces the camera, and a lid
    //     closing from "the top of the ball" closed over the pupil first
    //     and left the white all round it bare.
    //
    //   * the camera's up, raw, is better and still not right. A puddle
    //     eye looks up out of the spill by about forty degrees, so the
    //     centre of its visible face is not at zero in that frame — the
    //     lid reached the pupil while the top of the ball was still
    //     clear, which is the same failure a third of the way along.
    //
    // Taking the component perpendicular to the gaze puts the middle of
    // the visible face at zero for every eye, wall or puddle, so one pair
    // of numbers closes all of them the same way. It cannot go degenerate
    // here: no gaze turns more than about a third of a right angle off
    // the lens, and the camera's up is square to that.
    vec3 up = normalize(camUp - gazeW * dot(camUp, gazeW));
    float lat = dot(vDirW, up);

    // A ragged edge. A clean arc reads as a moulded eyelid; the whole
    // point of this is that the gel has closed over it, and gel does not
    // have an edge like that. Drifting, slowly, so the rim creeps.
    float wob = (vnoise3(vDirW * 4.6 + vec3(gooSeed, gooTime * 0.18, gooSeed))
                 - 0.5) * 0.15;

    // Open, the two edges sit out past the poles and take only a sliver
    // off the top and bottom of the disc — the goo already laps over the
    // ball's rim as GEOMETRY, and doing that job twice cost more of the
    // eye than either did alone. Shut, they cross, and the upper travels
    // half again as far as the lower, as a real lid does.
    float topEdge = mix(-0.22, 0.92, openness) + wob;
    float botEdge = mix(-0.12, -0.95, openness) - wob;
    float over = max(lat - topEdge, botEdge - lat);
    float cover = smoothstep(-0.03, 0.20, over);

    // --- the goo over it ------------------------------------------------
    // Beer-Lambert against the eyeball itself, exactly as the gel runs it
    // against the bin. At cover 0 the transmittance is 1 and this is the
    // eyeball untouched, so the two sides meet with no seam to hide.
    vec3 tintL = toLinear(gooTint.rgb);
    vec3 sigma = (vec3(1.0) - tintL) * gooAbsorption * 3.0;
    vec3 trans = exp(-sigma * (1.45 * cover));
    BASE_COLOR = vec4(eyeCol * trans + tintL * gooScatter * (vec3(1.0) - trans),
                      1.0);

    // The eyeball's own bump, and it stops where the goo starts: what is
    // under a covering of gel does not show its texture through it.
    vec3 nm = texture(eyeNormalMap, UV0).rgb * 2.0 - 1.0;
    vec3 mapped = normalize(TANGENT * nm.x + BINORMAL * nm.y + n0 * nm.z);
    NORMAL = normalize(mix(mapped, n0, cover));

    // Wet where it is bare and gel where it is not. The ball is polished
    // and the gel is not quite, so this is a real difference and it is
    // one more thing telling the two apart at a size where the shapes
    // cannot.
    ROUGHNESS = mix(0.18, 0.24, cover);
    SPECULAR_AMOUNT = mix(0.90, 0.40, cover);
    METALNESS = 0.0;
    FRESNEL_POWER = 2.5;
}
