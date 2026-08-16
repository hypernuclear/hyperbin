VARYING vec2 vScreen;

// The arm's bend, and where the result lands on the bin.
//
// One static mesh deformed here rather than a chain of posed models —
// the same division the gel uses (shaders/ooze3d.vert), and for the same
// reason: what this app pays for is draw calls and scene-graph churn, not
// per-vertex work. Five arms are five draw calls whatever they are doing.
//
// The mesh is authored straight up its own +Y with its base at the
// origin, so VERTEX.y over the mesh's length is how far along the arm a
// vertex sits — no skeleton needed to know that.
void MAIN()
{
    float s = clamp(VERTEX.y / max(meshLength, 1e-4), 0.0, 1.0);

    // Nothing bends at the root. An arm that sways from the very bottom
    // pivots like a wiper blade; one reaching out of a hole is gripped
    // where it passes through and free above. Squared on top of that, so
    // the tip travels far more than the middle.
    float free = smoothstep(0.12, 1.0, s);
    float bend = free * free;

    // Two rates that do not divide into each other, so an arm never
    // returns to the same shape and never reads as a loop.
    vec3 p = VERTEX;
    p.x += sin(time * 1.10 + armPhase + s * 2.3) * meshLength * 0.20 * bend;
    p.z += cos(time * 0.83 + armPhase + s * 1.7) * meshLength * 0.13 * bend;

    // The normal is left alone. It is wrong by the bend's own gradient,
    // which on something drawn three pixels wide under one directional
    // light is not a difference anybody can see — and getting it right
    // means differencing the displacement per vertex, which is real work
    // for no visible return. Revisit if these are ever drawn large.

    // Measured from the BENT position, not the rest one. The mask cuts
    // this arm where it crosses the bin's lip, and an arm that has swung
    // a fifth of its length sideways crosses it somewhere else entirely —
    // taken from VERTEX the cut would sit still while the arm moved
    // through it.
    vec4 world = MODEL_MATRIX * vec4(p, 1.0);
    vScreen = (VIEW_MATRIX * world).xy
            - (VIEW_MATRIX * vec4(binOrigin, 1.0)).xy;

    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(p, 1.0);
}
