import QtQuick
import QtQuick3D

// Eyeballs set into the gel.
//
// They do not travel. Where each one sits, how big it is and which way
// the gel faces under it all come from OozeEffect, which owns the body's
// profile — see its eyeSpheres/eyeNormals. What is left here is the part
// that needs the CAMERA, which the scene owns and the effect does not:
// aiming them.
//
// The pupils flick between fixations the way real eyes do: a saccade is
// close to instantaneous, so these snap rather than glide. Easing them
// would look like something searching; snapping looks like something
// watching.
Node {
    id: eyes

    /// The OozeEffect. Supplies the clock and where the eyes are.
    required property var effect
    /// The camera's tilt in degrees. Every gaze starts from here.
    required property real camTilt

    /// The gel's own colour and how it absorbs and scatters, handed
    /// straight down from the material that draws the body.
    ///
    /// Passed rather than restated. The lids are goo closing over the
    /// ball and are shaded with the same maths, so if these two ever
    /// disagreed the lid would be a slightly different substance from the
    /// body it is supposed to be part of — the kind of difference nobody
    /// spots as a wrong number, only as "the eyes look stuck on".
    required property color gooTint
    required property real gooAbsorption
    required property real gooScatter

    /// How far the gel's own surface pulls a gaze off the camera.
    ///
    /// At 0 every eye stares down the lens wherever it sits, which is
    /// what this looked like first and why the ones out near the
    /// silhouette read as decals: an eye buried in something has an
    /// orbit, and an orbit points where the surface points. At 1 they
    /// would face straight out of the body and the ones on the sides
    /// would be looking away entirely.
    ///
    /// It costs the eyes on the front almost nothing whatever it is set
    /// to — the normal there is already very nearly the camera — so this
    /// is really a setting for the flanks alone. At a third, one out on
    /// the side turned by only about thirty degrees, which still read as
    /// an eye pasted on the silhouette staring past its own socket. Over
    /// half turns it about fifty, which is a ball sitting in the goo and
    /// looking out of it, and it keeps watching you everywhere it can
    /// plausibly be said to be facing you at all.
    property real gazeBias: 0.55

    readonly property var spheres: effect ? effect.eyeSpheres : []
    readonly property var normals: effect ? effect.eyeNormals : []
    readonly property real time: effect ? effect.time : 0

    /// Toward the camera. It is orthographic, so this is the same
    /// direction everywhere in the scene and there is no per-eye look-at
    /// to compute — only the lean away from it that the gel asks for.
    readonly property vector3d camDir: Qt.vector3d(
        0, Math.sin(camTilt * Math.PI / 180), Math.cos(camTilt * Math.PI / 180))

    /// The camera's own up, in the scene. The lids close along it, so
    /// they sweep the face of each ball that is actually visible rather
    /// than some direction that only happens to suit the eyes on the
    /// body's wall. It is camDir turned a quarter turn about x.
    readonly property vector3d camUp: Qt.vector3d(
        0, Math.cos(camTilt * Math.PI / 180), -Math.sin(camTilt * Math.PI / 180))

    // A stable per-eye value in [0,1). Same index, same number, every
    // frame — so an eye keeps its saccade instead of being re-rolled
    // whenever anything re-evaluates.
    function rnd(a, b) {
        const s = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
        return s - Math.floor(s);
    }

    Repeater3D {
        // CONSTANT, and the list's length only decides how many are
        // shown. Driving the count from the level rebuilds every delegate
        // on the frame it changes, and this scene has been here before:
        // the bubbles did exactly that and cost four times the whole
        // effect before they were removed.
        model: eyes.effect ? eyes.effect.maxEyes : 0

        delegate: Node {
            id: eye
            required property int index

            readonly property bool out: index < eyes.spheres.length
            readonly property var sphere: out ? eyes.spheres[index] : null
            readonly property var normal: out ? eyes.normals[index] : null
            readonly property real size: sphere ? sphere.w : 1
            /// 1 wide open, 0 shut. Rides in the normal's spare slot.
            readonly property real open: normal ? normal.w : 1
            /// How far this eye's lid axis is canted off the horizontal,
            /// in radians. Constant per eye — it is anatomy, not motion —
            /// so it is hashed here rather than sent every frame.
            readonly property real lidRoll: (eyes.rnd(index, 37) - 0.5) * 0.9

            visible: out
            position: sphere ? Qt.vector3d(sphere.x, sphere.y, sphere.z)
                             : Qt.vector3d(0, 0, 0)

            // --- the gaze frame -----------------------------------------
            // Somewhere between the lens and the way the gel faces here.
            readonly property vector3d gaze: normal
                ? eyes.camDir.times(1 - eyes.gazeBias)
                      .plus(Qt.vector3d(normal.x, normal.y, normal.z)
                                .times(eyes.gazeBias))
                      .normalized()
                : eyes.camDir

            // Yaw then pitch, as two nested nodes rather than one node's
            // eulerRotation. Which order Qt composes a pair of Euler
            // angles in is a thing to look up and get wrong; nesting says
            // it outright, and the pair is exactly the decomposition of a
            // direction into (turn, then lift):
            //
            //   Ry(yaw) * Rx(pitch) * +z  =  (sin y cos p, -sin p, cos y cos p)
            Node {
                eulerRotation.y: Math.atan2(eye.gaze.x, eye.gaze.z) * 180 / Math.PI

                Node {
                    eulerRotation.x: -Math.asin(Math.max(-1, Math.min(1, eye.gaze.y)))
                                     * 180 / Math.PI

                    Node {
                        // The saccade. Floor(time) picks a fixation, and
                        // the fixation is hashed into an offset, so the
                        // eye holds still and then jumps — no
                        // interpolation anywhere.
                        readonly property int fix:
                            Math.floor(eyes.time * 1.6 + eye.index * 3.7)
                        readonly property bool blank: eyes.rnd(fix, eye.index) > 0.72

                        // A fixation that lands on the camera, roughly one
                        // in four, is what sells the rest: an eye that is
                        // always darting never appears to have noticed
                        // you.
                        eulerRotation.x: blank ? 0 : (eyes.rnd(fix, eye.index + 11) - 0.5) * 26
                        eulerRotation.y: blank ? 0 : (eyes.rnd(fix, eye.index + 23) - 0.5) * 34

                        Model {
                            source: "qrc:/icons/meshes/eye.mesh"
                            // The iris is on the model's -z; +z is the
                            // back of the eyeball. Measured by rendering
                            // the mesh from six directions rather than
                            // read off the asset.
                            eulerRotation.y: 180
                            scale: Qt.vector3d(eye.size, eye.size, eye.size)

                            // One material per eye, and it has to be:
                            // `openness` is a uniform, and nine eyes
                            // blinking on nine different clocks need nine
                            // values of it. They share one shader and one
                            // pipeline — this is nine uniform buffers,
                            // not nine programs — and the maps are
                            // declared once above and merely referenced,
                            // so it is also one copy of each texture.
                            materials: CustomMaterial {
                                shadingMode: CustomMaterial.Shaded
                                cullMode: CustomMaterial.BackFaceCulling
                                property real openness: eye.open
                                property real gooTime: eyes.time
                                property vector3d camUp: eyes.camUp
                                property vector3d gazeW: eye.gaze
                                property real lidRoll: eye.lidRoll
                                // Keeps the ragged rim of one eye's lid
                                // from being the same rag as its
                                // neighbour's.
                                property real gooSeed: eye.index * 7.31
                                property color gooTint: eyes.gooTint
                                property real gooAbsorption: eyes.gooAbsorption
                                property real gooScatter: eyes.gooScatter
                                property TextureInput eyeAlbedo: TextureInput {
                                    texture: albedoMap
                                }
                                property TextureInput eyeNormalMap: TextureInput {
                                    texture: normalMap
                                }
                                vertexShader: "qrc:/shaders3d/eye3d.vert"
                                fragmentShader: "qrc:/shaders3d/eye3d.frag"
                            }
                        }
                    }
                }
            }
        }
    }

    // Declared once, referenced by all nine materials, so this is one
    // upload of each rather than nine.
    Texture {
        id: albedoMap
        source: "qrc:/icons/maps/eye_albedo.jpg"
        generateMipmaps: true
        mipFilter: Texture.Linear
    }
    Texture {
        id: normalMap
        source: "qrc:/icons/maps/eye_normal.png"
        generateMipmaps: true
        mipFilter: Texture.Linear
    }
}
