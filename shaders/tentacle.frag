VARYING vec2 vScreen;

// A tentacle, cut off where the bin stands in front of it.
//
// The first version of this did the opposite: it drew the bin's front
// wall back over the tentacle, out of the same icon the shell had already
// drawn underneath. That composites invisibly — measured, it came back
// identical to the shell's own pixels bar a rounding step — but it is
// still the wrong shape of solution. It repaints a large area of
// somebody else's artwork every frame to hide a few pixels of ours, it
// has to be kept in register with a bin we do not draw, and every
// question about it ("is the shadow doubled? is the outline
// thickened?") is a question that only exists because we touched pixels
// we had no business touching.
//
// Cutting our own geometry instead answers all of them at once. Nothing
// outside this effect is written to, there is no second pass and no
// full-bin texture read per frame, and it cannot fall out of alignment
// with artwork it never touches.
void MAIN()
{
    // Into the icon's own coordinates, which run DOWN from its top.
    vec2 uv = (vScreen - planeMin) / planeSize;
    uv.y = 1.0 - uv.y;

    // Off the artwork entirely: there is no bin here to hide behind.
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
        // The near lip, as the ELLIPSE it actually is rather than as a
        // level line. A straight cut is right only across the middle of
        // the bin; out toward the sides the real lip has already curved
        // up by most of the mouth's depth, and a tentacle there was
        // being sliced a good fraction of the opening too low.
        float k = (uv.x - mouthCentre.x) / max(mouthHalfWidth, 1e-4);
        float arc = mouthCentre.y
                  + mouthDepth * sqrt(max(0.0, 1.0 - k * k));

        // Below the lip AND over solid bin. Both, because the bin only
        // hides what is actually behind it: a tentacle that has flopped
        // over the outside of the rim is below the lip too, and it
        // belongs in front.
        if (uv.y > arc && texture(iconTex, uv).a > 0.5)
            discard;
    }

    // LINEAR. A shaded custom material hands BASE_COLOR to the same
    // lighting maths PrincipledMaterial uses, and the scene tonemaps on
    // the way out — an 8-bit map fed in raw is gamma-encoded twice.
    vec4 albedo = texture(albedoTex, UV0);
    BASE_COLOR = vec4(pow(max(albedo.rgb, vec3(0.0)), vec3(2.2)), 1.0);

    vec3 nm = texture(normalTex, UV0).rgb * 2.0 - 1.0;
    NORMAL = normalize(TANGENT * nm.x + BINORMAL * nm.y + normalize(NORMAL) * nm.z);

    ROUGHNESS = armRoughness;
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.5;
}
