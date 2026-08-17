VARYING vec2 vScreen;
// Where this vertex is relative to the bin's AXIS, in scene units.
VARYING vec3 vLocal;
// And where the SPINE is at this point along the arm, in the same space.
//
// Two of them, because the two occlusion tests in tentacle.frag want
// different things. Anything cutting the arm across its length — the lip,
// the surface of the rubbish — should follow the arm's own outline, and
// wants the fragment. Anything cutting it along the LINE OF SIGHT — the
// bin's front wall — must not, and this is not a matter of taste: the
// wall's surface faces the camera, so testing each fragment against it
// discards the near side of the tube wherever that side passes behind the
// wall and leaves the far side drawn, which is a hole straight through the
// arm. It looked like the arm had been eaten. Asking about the spine
// instead answers once per cross-section, so an arm is behind the wall or
// in front of it as a whole and there is nothing to see through.
VARYING vec3 vSpine;

// The arm's curl.
//
// One static mesh deformed here rather than a chain of posed models — the
// same division the gel uses (shaders/ooze3d.vert), and for the same
// reason: what this app pays for is draw calls and scene-graph churn, not
// per-vertex work. Three arms are three draw calls whatever they do.
//
// A CIRCULAR bend, not a sine offset. The first version displaced each
// vertex sideways by a sine of its height, which is an S — the arm
// slithered but never curled over, and nothing that cannot pass ninety
// degrees can reach down the outside of anything. Bending the spine
// through an arc lets the tip come right over and point back down, which
// is what a tentacle does and what makes a strike possible at all.
//
// The mesh is authored straight up its own +Y with its base at the
// origin, so VERTEX.y over the mesh's length is how far along the arm a
// vertex sits. No skeleton is needed to know that, which is why the rig
// went unused.
void MAIN()
{
    float s = clamp(VERTEX.y / max(meshLength, 1e-4), 0.0, 1.0);

    // Nothing bends at the root. An arm that curves from the very bottom
    // pivots like a wiper blade; one reaching out of a hole is gripped
    // where it passes through and free above.
    float grip = smoothstep(0.10, 0.55, s);
    float a = curlAngle * s * grip;

    // Which way the arc leans, on the ground plane.
    vec2 d = vec2(sin(curlDir), cos(curlDir));
    vec2 side = vec2(-d.y, d.x);

    // The spine: an arc of radius L/curl, so total bend over the whole
    // arm is exactly curlAngle. As curl goes to zero this tends to the
    // straight mesh rather than blowing up — R grows, the arc flattens.
    float R = meshLength / max(curlAngle, 1e-3);
    float outward = R * (1.0 - cos(a));
    vec3 spine = vec3(d.x * outward, R * sin(a), d.y * outward);

    // Carry the cross-section round with the bend, or the tube flattens
    // where it turns. Split the vertex's own offset into the bend plane
    // and across it, then rebuild it on the rotated frame.
    float u = dot(VERTEX.xz, d);
    float v = dot(VERTEX.xz, side);
    vec3 inPlane = vec3(d.x * cos(a), -sin(a), d.y * cos(a));
    vec3 across  = vec3(side.x, 0.0, side.y);
    vec3 p = spine + inPlane * u + across * v;

    // A little life on top of the pose, so an arm holding still is not
    // holding perfectly still. Two rates that do not divide into each
    // other, so it never returns to the same shape.
    //
    // Added to the SPINE as well, or the two drift apart and the fragment
    // shader ends up asking about a centre line the arm is no longer on.
    float free = grip * s;
    vec3 wob = vec3(sin(time * 1.10 + armPhase + s * 2.3) * meshLength * 0.045,
                    0.0,
                    cos(time * 0.83 + armPhase + s * 1.7) * meshLength * 0.035)
             * free;
    p += wob;
    spine += wob;

    // The normal is left at its rest value. It is wrong by the bend's own
    // gradient, which on something drawn a few pixels wide under one
    // light and a soft probe is not a difference anybody can see —
    // getting it right means differencing the displacement per vertex,
    // which is real work for no visible return.

    // Measured from the BENT position, not the rest one. The mask cuts
    // this arm where it crosses the bin's lip, and an arm that has curled
    // over has moved a long way — taken from VERTEX the cut would sit
    // still while the arm swung through it.
    vec4 world = MODEL_MATRIX * vec4(p, 1.0);
    vLocal = world.xyz - binOrigin;
    vSpine = (MODEL_MATRIX * vec4(spine, 1.0)).xyz - binOrigin;
    vScreen = (VIEW_MATRIX * world).xy
            - (VIEW_MATRIX * vec4(binOrigin, 1.0)).xy;

    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(p, 1.0);
}
