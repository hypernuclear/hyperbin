import QtQuick
import QtQuick3D

// Eyeballs set into the gel.
//
// They do not travel. Where each one sits, how big it is, where it looks,
// its saccades and its blinks all come from OozeEffect, one OozeEye per
// slot: it owns the body's profile and is told the camera's tilt. What is
// left here is drawing them, and every binding is a plain read of a value
// that signals only when it moved.
//
// Working the aim out here, in JavaScript, was the earlier arrangement:
// fourteen delegates re-running vector maths every frame was most of the
// effect's CPU on a real Dock (OozeEye says more).
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

    readonly property real time: effect ? effect.time : 0

    /// The camera's own up, in the scene. The lids close along it, so
    /// they sweep the face of each ball that is actually visible rather
    /// than some direction that only happens to suit the eyes on the
    /// body's wall. It is camDir turned a quarter turn about x.
    readonly property vector3d camUp: Qt.vector3d(
        0, Math.cos(camTilt * Math.PI / 180), -Math.sin(camTilt * Math.PI / 180))

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
            readonly property OozeEye d: eyes.effect ? eyes.effect.eye(index) : null

            visible: d ? d.out : false
            position: d ? d.position : Qt.vector3d(0, 0, 0)

            // Where it looks, as yaw then pitch on two nested nodes.
            Node {
                eulerRotation.y: eye.d ? eye.d.yaw : 0

                Node {
                    eulerRotation.x: eye.d ? eye.d.pitch : 0

                    // The saccade on top.
                    Node {
                        eulerRotation.x: eye.d ? eye.d.dartX : 0
                        eulerRotation.y: eye.d ? eye.d.dartY : 0

                        Model {
                            source: "qrc:/icons/meshes/eye.mesh"
                            // The iris is on the model's -z; +z is the
                            // back of the eyeball. Measured by rendering
                            // the mesh from six directions rather than
                            // read off the asset.
                            eulerRotation.y: 180
                            scale: eye.d ? Qt.vector3d(eye.d.size, eye.d.size, eye.d.size)
                                         : Qt.vector3d(1, 1, 1)

                            // One material per eye, and it has to be:
                            // `openness` is a uniform, and eyes blinking on
                            // their own clocks need their own values of it.
                            // They share one shader and one pipeline, this
                            // is one uniform buffer each, not one program,
                            // and the maps are declared once below and
                            // merely referenced.
                            materials: CustomMaterial {
                                shadingMode: CustomMaterial.Shaded
                                cullMode: CustomMaterial.BackFaceCulling
                                property real openness: eye.d ? eye.d.open : 1
                                property real gooTime: eyes.time
                                property vector3d camUp: eyes.camUp
                                property vector3d gazeW: eye.d ? eye.d.gaze : Qt.vector3d(0, 0, 1)
                                property real lidRoll: eye.d ? eye.d.lidRoll : 0
                                // Keeps the ragged rim of one eye's lid
                                // from being the same rag as its
                                // neighbour's.
                                property real gooSeed: eye.d ? eye.d.seed : 0
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
