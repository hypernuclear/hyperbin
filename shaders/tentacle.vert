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

// The arm, laid along a solved joint chain.
//
// This replaced a closed-form circular arc, and the arc is the reason the
// old motion looked stiff: the entire shape was two numbers, a total bend
// and a direction, and an arc has constant curvature by definition. The
// chain is solved on the CPU by FABRIK — see src/core/TentacleChain — and
// arrives here as a texture rather than as uniforms because CustomMaterial
// has no arrays and sixteen joints across three arms is not something to
// unroll by hand.
//
// Still one static mesh and one draw call an arm, which is the whole
// division this app runs on: what it pays for is draw calls and
// scene-graph churn, not per-vertex work.
//
// The mesh is authored straight up its own +Y with its base at the origin,
// so VERTEX.y over the mesh's length is how far along the arm a vertex
// sits. That is all the correspondence a chain needs, which is why the
// model is still unrigged and there are no weights to paint.

/// One joint. Row `row` of the spine texture, joint `j`.
///
/// texelFetch, not texture(): these are joint positions in scene units,
/// not a picture. Filtering between two of them would be a lie at the ends
/// of every row and mipmapping them is meaningless.
vec4 spineTexel(int row, int j)
{
    return texelFetch(spineTex, ivec2(j, row), 0);
}

void MAIN()
{
    float s = clamp(VERTEX.y / max(meshLength, 1e-4), 0.0, 1.0);

    // Which segment of the chain this vertex falls in, and how far along
    // it. The chain's segments are of equal length and the mesh is
    // straight, so the mesh's own y maps to chain arc length directly.
    float f = s * float(jointCount - 1);
    int i = int(floor(f));
    int hi = min(i + 1, jointCount - 1);
    float t = f - float(i);

    int rowP = armRow * 2;
    int rowS = rowP + 1;

    vec3 pA = spineTexel(rowP, i).xyz;
    vec3 pB = spineTexel(rowP, hi).xyz;
    vec3 p = mix(pA, pB, t);

    // The frame, carried along the chain on the CPU and merely interpolated
    // here. Rebuilding it per vertex from the tangent alone would let it
    // spin wherever the arm passes near whatever axis was used to seed it,
    // and this model's suckers are on ONE side — a frame that rolls does
    // not soften the shading, it moves the suckers round the arm.
    vec3 sA = spineTexel(rowS, i).xyz;
    vec3 sB = spineTexel(rowS, hi).xyz;
    vec3 side = normalize(mix(sA, sB, t));

    vec3 tangent = pB - pA;
    // A degenerate segment happens for one frame when two joints coincide;
    // falling back up the arm keeps the cross-section from collapsing.
    tangent = length(tangent) > 1e-5 ? normalize(tangent) : vec3(0.0, 1.0, 0.0);
    side = normalize(side - tangent * dot(side, tangent));
    // RIGHT-HANDED, and it has to be checked rather than guessed. The mesh
    // arrives in glTF's right-handed space with the arm up +Y, so mapping
    // its (x, y, z) onto (side, tangent, up) is only a rotation if
    // side x tangent = up. Built the other way round — up = tangent x side
    // — the determinant is -1 and the whole arm is silently MIRRORED:
    // winding inverts, normals point into the surface, and the result is
    // a washed-out arm with highlights in the wrong places. It renders
    // perfectly happily, which is what makes it easy to miss.
    vec3 up = cross(side, tangent);

    // The cross-section, carried onto the chain's frame. The model itself
    // has no transform — position, length and girth are all in the chain
    // and this one scale — so VERTEX.xz is the authored cross-section and
    // nothing else has touched it.
    vec3 pos = p + side * (VERTEX.x * girthScale) + up * (VERTEX.z * girthScale);

    // The normal is rotated onto the same frame. It was left at its rest
    // value under the arc, on the grounds that the error was invisible at
    // a few pixels wide; that excuse is gone now the arm is thick, has
    // sculpted suckers, and carries a normal map whose relief is lit by
    // this vector.
    vec3 n = normalize(NORMAL);
    NORMAL = normalize(side * n.x + tangent * n.y + up * n.z);
    vec3 tg = normalize(TANGENT);
    TANGENT = normalize(side * tg.x + tangent * tg.y + up * tg.z);
    vec3 bn = normalize(BINORMAL);
    BINORMAL = normalize(side * bn.x + tangent * bn.y + up * bn.z);

    vec4 world = MODEL_MATRIX * vec4(pos, 1.0);
    vLocal = world.xyz - binOrigin;
    vSpine = (MODEL_MATRIX * vec4(p, 1.0)).xyz - binOrigin;
    vScreen = (VIEW_MATRIX * world).xy
            - (VIEW_MATRIX * vec4(binOrigin, 1.0)).xy;

    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(pos, 1.0);
}
