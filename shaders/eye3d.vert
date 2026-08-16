// The eyeball, and the goo that closes over it.
//
// All this stage does is hand the fragment shader the direction of this
// surface point in the SCENE's frame rather than the model's. Everything
// about the lid is decided there, and every one of those decisions has to
// be independent of which way the eye is currently pointing:
//
//   * the lid must not turn with the eye. Eyelids stay put while the eye
//     rotates under them, and these are goo — goo hangs downward whatever
//     the ball beneath it is looking at. Measured in model space the lid
//     line rolled with every saccade.
//
//   * the ragged edge must not swim. Noise sampled on the model's own
//     coordinates is carried around by the ball's rotation, so the
//     wobble crawled along the rim each time the eye darted. Sampled on
//     the scene direction it is fixed in the world, which is where the
//     goo is.
//
// One vector answers both. For a sphere the outward direction IS the
// surface normal, so this is also exactly the axis the latitude wants.
VARYING vec3 vDirW;

void MAIN()
{
    vDirW = normalize(mat3(MODEL_MATRIX) * VERTEX);
}
