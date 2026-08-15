import QtQuick
import QtQuick3D

// Eyeballs suspended in the gel.
//
// They drift through the body, turn up in greater numbers as the bin
// fills, and all look at the camera — which is what makes the thing read
// as awake rather than as debris. The pupils flick between fixations the
// way real eyes do: a saccade is close to instantaneous, so these snap
// rather than glide. Easing them would look like something searching;
// snapping looks like something watching.
Node {
    id: eyes

    /// The OozeEffect. Supplies the level, the clock, and eyeAt().
    required property var effect
    /// The camera's tilt in degrees. The gaze frame leans back by this
    /// so every eye faces the lens.
    required property real camTilt
    /// The bin's width in scene units; eye size is a fraction of it.
    required property real binWidth

    /// The most that can ever be on screen.
    ///
    /// The delegate count is CONSTANT and the level only decides how many
    /// of them are visible. Driving the count from the level rebuilds
    /// every delegate on the frame it changes, and this scene has been
    /// here before: the bubbles did exactly that and cost four times the
    /// whole effect before they were removed.
    readonly property int maxEyes: 9
    readonly property real level: effect ? effect.level : 0
    readonly property real time: effect ? effect.time : 0
    readonly property int active: Math.round(level * maxEyes)

    // A stable per-eye value in [0,1). Same index, same number, every
    // frame — so an eye keeps its size and its path instead of being
    // re-rolled whenever anything re-evaluates.
    function rnd(a, b) {
        const s = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
        return s - Math.floor(s);
    }

    Repeater3D {
        model: eyes.maxEyes

        delegate: Node {
            id: eye
            required property int index

            readonly property real seedA: eyes.rnd(index, 1)
            readonly property real seedB: eyes.rnd(index, 2)
            readonly property real seedC: eyes.rnd(index, 3)

            // RADIUS, not diameter. The mesh is a unit sphere, so a
            // scale of s spans 2s — sized as a diameter, the first pass
            // came out at twice the intent and the eyes sat on the gel
            // rather than in it.
            readonly property real size: eyes.binWidth * (0.036 + 0.028 * seedA)
            readonly property real drift: 0.045 + 0.075 * seedB

            // Around the bin, and up and down it. The vertical wander is
            // small: an eye that travels the body's whole height reads as
            // falling, not floating.
            readonly property real angle: seedC * 6.2831853
                                          + eyes.time * drift * (seedA > 0.5 ? 1 : -1)
            readonly property real height: 0.18 + 0.62 * seedB
                                           + 0.06 * Math.sin(eyes.time * drift * 2.1
                                                             + index)

            // How deep it sits, as a fraction of the body's half-width.
            //
            // The useful range is narrow and not obvious. An eye breaks
            // the surface only once sink exceeds 1 - eyeRadius/bodyRadius,
            // which for these sizes is about 0.92 — below that it is
            // wholly inside and merely ghosts through the gel. So this
            // rides just under and just over 1: the low end buries it,
            // the high end stands it half clear, and the swing carries
            // each eye slowly between the two.
            readonly property real sink: 0.965 + 0.075 * Math.sin(eyes.time * 0.31 + index * 2.0)

            visible: index < eyes.active && eyes.effect
            position: eyes.effect ? eyes.effect.eyeAt(height, angle, sink)
                                  : Qt.vector3d(0, 0, 0)

            // --- the gaze frame -----------------------------------------
            // Leaned back to match the camera, so a child pointing along
            // +z is pointing down the lens. The camera is orthographic,
            // so this is the same for every eye no matter where it sits —
            // there is no per-eye look-at to compute.
            eulerRotation.x: -eyes.camTilt

            Node {
                // The saccade. Floor(time) picks a fixation, and the
                // fixation is hashed into an offset, so the eye holds
                // still and then jumps — no interpolation anywhere.
                readonly property int fix: Math.floor(eyes.time * 1.6 + eye.index * 3.7)
                readonly property bool blank: eyes.rnd(fix, eye.index) > 0.72

                // A fixation that lands on the camera, roughly one in
                // four, is what sells the rest: an eye that is always
                // darting never appears to have noticed you.
                eulerRotation.x: blank ? 0 : (eyes.rnd(fix, eye.index + 11) - 0.5) * 26
                eulerRotation.y: blank ? 0 : (eyes.rnd(fix, eye.index + 23) - 0.5) * 34

                Model {
                    source: "qrc:/icons/meshes/eye.mesh"
                    // The iris is on the model's -z; +z is the back of
                    // the eyeball. Measured by rendering the mesh from
                    // six directions rather than read off the asset.
                    eulerRotation.y: 180
                    scale: Qt.vector3d(eye.size, eye.size, eye.size)
                    materials: eyeMaterial
                }
            }
        }
    }

    PrincipledMaterial {
        id: eyeMaterial
        baseColorMap: Texture {
            source: "qrc:/icons/maps/eye_albedo.jpg"
            generateMipmaps: true
            mipFilter: Texture.Linear
        }
        normalMap: Texture {
            source: "qrc:/icons/maps/eye_normal.png"
            generateMipmaps: true
            mipFilter: Texture.Linear
        }
        // Wet, not shiny. The cornea mesh was dropped, so the highlight
        // has to come from the material — and a glossy sphere with no
        // clearcoat reads as a marble.
        roughness: 0.18
        specularAmount: 0.9
        cullMode: PrincipledMaterial.BackFaceCulling
    }
}
