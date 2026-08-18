VARYING vec2 vScreen;
VARYING vec3 vLocal;
VARYING vec3 vSpine;

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

        // Below the lip, over solid bin, AND with the bin's own body
        // between us and the eye. All three, and the third is what makes
        // a strike possible: an arm that has curled over and reached down
        // the OUTSIDE of the bin is below the lip and over solid artwork
        // too, and on those two tests alone it vanished at the exact
        // moment it became interesting.
        //
        // "Behind the body" as the FRONT SURFACE of an ellipse, not as a
        // radius. Distance from the axis cannot tell in front from behind
        // — it is the same number either way — and with three arms seated
        // as a triangle that stopped being an academic distinction: the
        // front pair strike out over the near corners of the rim, which is
        // outside the bin in z while still well inside it radially, and
        // their tips were being cut off in mid-flight.
        //
        // The camera is orthographic and level in x, so the sight line
        // runs purely along +z here and the query collapses to a single
        // comparison against the ellipse's near face. (It ignores the
        // line's rise in y, which only matters for points far enough
        // behind the bin to be looked over the top of; nothing reaches
        // there — the deepest seat is 0.8 of the way back.)
        //
        // TAPERED with depth, not a constant. Measured on the macOS trash
        // the body is 0.98 of the opening at the lip and 0.78 near the
        // foot, and this was a single average of 0.90 — which is wrong at
        // both ends and was wrong in a way that showed the moment the arms
        // started lingering near the rim. Everything between 0.90 and the
        // real 0.98 counted as outside the bin, so arm tips draped just
        // under the lip were drawn over the front wall in a band about
        // twenty pixels wide. Measured, 7,135 leaked pixels, all of them
        // over the shaded body and none anywhere else.
        //
        // Asked about the SPINE, not this fragment — see tentacle.vert for
        // why, and for what testing the fragment did to the arm instead.
        // SPLIT between the fragment and the spine, because the two halves
        // of this test want different things and it took two goes to see
        // it. Sideways, the bin's edge is a boundary in SCREEN space: a cut
        // along it is a vertical line through the arm, which leaves a solid
        // shape and reads as the arm passing behind the bin's edge. That
        // wants the fragment, and asking the spine instead makes a whole
        // cross-section visible the moment its centre line clears the edge
        // — so a fat arm draped on the rim spilled half its width across
        // the bin's face. Measured, 8,705 pixels on one frame.
        //
        // In DEPTH it is the opposite, and that is the trap: the wall faces
        // the camera, so a per-fragment depth test discards the near side
        // of the tube and keeps the far side, which is a hole straight
        // through the arm. That half has to ask the spine, once per
        // cross-section.
        float below = clamp((lipY - vLocal.y) / max(binHeight, 1e-4), 0.0, 1.0);
        vec2 halfSize = binHalfSize * mix(bodyTop, bodyFoot, below);
        float ex = vLocal.x / halfSize.x;
        float nearFace = halfSize.y * sqrt(max(0.0, 1.0 - ex * ex));
        bool underBin = uv.y > arc && abs(ex) < 1.0 && vSpine.z < nearFace;

        // --- and behind the RUBBISH -------------------------------------
        //
        // Everything above is about the bin's front wall, which stops at
        // the lip. Above the lip is the opening, and the opening is full:
        // measured on the macOS trash, there is no dark interior visible
        // at all — every opaque pixel inside the mouth is rubbish. So an
        // arm rooted at the BACK of the mouth ought to come up behind that
        // rubbish, and with the wall test alone it did not: it drew over
        // the newspaper and the crumpled paper with its root sitting on
        // top of the heap like a sticker.
        //
        // We do not draw the rubbish and cannot cut a hole in it. What we
        // can do is give the painted heap a PLACE — a mound filling the
        // opening, as wide as the measured mouth and as tall as the
        // rubbish is measured to stand proud of the rim — and then throw
        // away our own fragments that it stands in front of.
        //
        // Which is a ray query, not a depth compare, because the mound is
        // a volume: does the segment from this fragment to the eye cross
        // it? For an ellipsoid that is a quadratic. Substituting the ray
        // into the unit sphere it maps to gives
        //
        //     s^2|d|^2 + 2s(q.d) + |q|^2 - 1 = 0
        //
        // and inside the mound (|q|^2 < 1) needs no roots at all. Outside
        // it, the product of the roots is positive, so both share a sign
        // and the sum settles which: a hit in FRONT of us needs q.d < 0
        // and a real discriminant. No square root, no iteration.
        vec3 q = (vLocal - heapCentre) / heapRadii;
        vec3 hd = camToEye / heapRadii;
        float qq = dot(q, q);
        float qd = dot(q, hd);
        bool underHeap = qq < 1.0
                      || (qd < 0.0 && qd * qd >= dot(hd, hd) * (qq - 1.0));

        // Trimmed against the artwork's own alpha, and that is what makes
        // a mound good enough. The painted heap is lumpy — it clears the
        // rim in three places and not at all in between — so a smooth
        // ellipsoid over-states it almost everywhere above the rim. Where
        // it over-states, there is nothing painted, so the cut does not
        // happen: an arm is never sliced against thin air, only against
        // pixels that are really there.
        if ((underBin || underHeap) && texture(iconTex, uv).a > 0.5)
            discard;
    }

    // LINEAR. A shaded custom material hands BASE_COLOR to the same
    // lighting maths PrincipledMaterial uses, and the scene tonemaps on
    // the way out — an 8-bit map fed in raw is gamma-encoded twice.
    vec4 albedo = texture(albedoTex, UV0);
    vec3 base = pow(max(albedo.rgb, vec3(0.0)), vec3(2.2));

    // Two depth cues, both hand-made, because the camera is orthographic
    // and supplies neither.
    //
    // The first is the bin's own shadow: an arm climbing out of a heap of
    // rubbish is in the dark at the bottom and in the light at the top, and
    // without that it reads as a sticker laid over the mouth. Measured from
    // ABOVE the heap's surface rather than from the opening, which is not a
    // detail — the arm is hidden everywhere below that surface, so a
    // gradient referenced to the opening applied only to fragments that
    // were being discarded anyway and did precisely nothing.
    //
    // The second is aerial perspective: the far arm is dimmed a little.
    // Small, and driven off its seat's own depth, so it stays consistent
    // with the triangle rather than being a per-arm fudge.
    //
    // Both multiplied into the LINEAR albedo, which is why they go here and
    // not after the pow: darkening an sRGB-encoded value is not darkening
    // light, and the result comes out muddy rather than dim.
    float below = clamp((shadeTopY - vLocal.y) / max(shadeSpan, 1e-4),
                        0.0, 1.0);
    // Gated on being inside the bin at all, or the tail of a strike —
    // which hangs down the OUTSIDE, in the light — would be shaded as
    // though it were still in the hole.
    float inside = 1.0 - smoothstep(0.85, 1.05, length(vLocal.xz / binHalfSize));
    BASE_COLOR = vec4(base * depthShade * mix(1.0, binShade, below * inside),
                      1.0);

    vec3 nm = texture(normalTex, UV0).rgb * 2.0 - 1.0;
    NORMAL = normalize(TANGENT * nm.x + BINORMAL * nm.y + normalize(NORMAL) * nm.z);

    ROUGHNESS = armRoughness;
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.5;
}
