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
    vec3 up0 = normalize(camUp - gazeW * dot(camUp, gazeW));
    // ...and then CANTED, by its own angle per eye.
    //
    // Lids square to the horizon are the giveaway. Every real set sits on
    // a tilted axis — the outer corner higher than the inner — and a
    // whole bin of them closing along exactly the same line reads as nine
    // copies of one thing rather than nine of a kind. The roll turns the
    // whole aperture, both lids together, which is what an eye does; two
    // lids canted separately would be a pair of jaws.
    vec3 right0 = cross(up0, gazeW);
    vec3 up = up0 * cos(lidRoll) + right0 * sin(lidRoll);
    vec3 right = right0 * cos(lidRoll) - up0 * sin(lidRoll);
    // The face of the ball, as flat coordinates. Both axes are square to
    // the gaze, and the camera is orthographic, so these ARE the pixel
    // offsets across the visible disc: -1 to 1 either way.
    float x = dot(vDirW, right);
    float y = dot(vDirW, up);

    // A ragged edge, but only slightly. A clean arc reads as a moulded
    // eyelid and the point of this is that gel has closed over the ball,
    // which does not have an edge like that. At 0.15 though it stopped
    // reading as a lid at all: the noise is worth a sixth of the ball's
    // radius, enough to retract one end of the lid past the other and
    // leave what looked like a bite taken out of the top. Drifting
    // slowly, so the rim creeps.
    float wob = (vnoise3(vDirW * 4.6 + vec3(gooSeed, gooTime * 0.18, gooSeed))
                 - 0.5) * 0.07;

    // Open is NOT bare. A lid you only ever see on the way down is not a
    // lid, it is a wipe: the eye has to be sitting in an aperture the
    // whole time so that closing it is the same shape continuing to move.
    // These sat out past the poles at first and took a sliver off — the
    // ball read as a bead that occasionally got swallowed.
    //
    // y IS the projected height up the visible disc, so 0.52 takes the
    // top quarter of it: an upper lid resting over the top of the iris,
    // which is where a real one sits. The lower takes a little less, and
    // the upper travels further on the way down, as a real pair does.
    // Shut, the two OVERLAP, and by more than looks necessary.
    //
    // They used to cross by a tenth, which was ample while the edges were
    // straight. Bowing them toward each other eats that margin from the
    // middle: two curves that meet at the corners are furthest apart at
    // the centre, and with the wobble at full throw the overlap there
    // went negative — leaving a bright open slit across the eye on the
    // one frame it was supposed to be shut. Worked out rather than
    // eyeballed, because it is a single frame in a blink and would
    // never have been caught by looking.
    float topEdge = mix(-0.40, 0.52, openness) + wob;
    float botEdge = mix(0.06, -0.58, openness) - wob;

    // The edges CURVE, and they have to.
    //
    // Straight ones were the whole of what looked wrong. A lid defined by
    // "everything past this latitude" is a plane cut through a sphere,
    // and a plane cut is a half-dome cap — so the eye wore two skullcaps,
    // top and bottom, with a straight band of aperture between them.
    //
    // Tilting the two cuts apart does not fix it either, and that is
    // worth knowing before trying it: both boundary circles contain the
    // view direction, so under an orthographic camera each projects to a
    // straight LINE. Two lines cross once. Tilting them just seals one
    // side of the eye and opens the other into a wedge.
    //
    // What makes an eye an eye is that both edges bow toward each other
    // and meet at BOTH ends, which is a curve, not a cut. A quadratic is
    // enough: at this coefficient the two meet just inside the ball's
    // silhouette with about forty-five degrees between them, so the
    // aperture is a proper pointed almond and the corners land where the
    // goo is already lapping over the rim anyway.
    //
    // The upper bows harder than the lower. They do on a face too.
    float overTop = (y + 0.62 * x * x) - topEdge;
    float overBot = (botEdge + 0.50 * x * x) - y;
    float over = max(overTop, overBot);
    // A short ramp. The goo has to fade in rather than step — a hard
    // alpha edge on a ball this size crawls with aliasing as the lid
    // moves — but over a fifth of the radius the lid had no line to it at
    // all and read as a stain.
    float cover = smoothstep(-0.02, 0.10, over);

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
