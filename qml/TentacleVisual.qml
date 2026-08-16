import QtQuick
import QtQuick3D
import hyperbin

// Tentacles coming out of the bin, and the bin's front wall drawn back
// over them.
//
// The tentacles are PLACEHOLDERS — chains of tapering spheres on a swaying
// curve. What is being tested here is the occlusion: each one starts well
// down inside the bin and rises out through the opening, so the stretch
// below the near lip must disappear behind the front wall. If it does
// not, the mask is wrong, and that is the point of drawing them at all.
Item {
    id: root

    required property var effect

    readonly property real binW: effect ? effect.binSize.width : 40
    readonly property real binH: effect ? effect.binSize.height : 40

    View3D {
        anchors.fill: parent
        camera: cam

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
        }

        // The same camera the ooze uses, and it has to be: both scenes
        // are drawn over artwork the shell rendered from its own
        // viewpoint, and the opening's measured ellipse only projects
        // back to the right shape from this one.
        OrthographicCamera {
            id: cam
            readonly property real tilt: 17
            readonly property real dist: 600
            y: dist * Math.sin(tilt * Math.PI / 180)
            z: dist * Math.cos(tilt * Math.PI / 180)
            eulerRotation.x: -tilt
            horizontalMagnification: 1.0
            verticalMagnification: 1.0
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-35, -25, 0)
            brightness: 1.2
        }

        Node {
            id: binNode
            x: root.effect ? root.effect.binRect.x + root.effect.binRect.width / 2
                             - root.width / 2 : 0
            y: root.effect ? -(root.effect.binRect.y + root.effect.binRect.height / 2
                               - root.height / 2) : 0

            // --- the tentacles ---------------------------------------
            Repeater3D {
                // CONSTANT, and only visibility follows the count.
                //
                // Bound to `count` this rebuilt every delegate on every
                // frame the count moved, which is the whole of emerging
                // and the whole of withdrawing. qml/OozeEyes.qml carries
                // the same note and the reason: the ooze bubbles did
                // exactly this and cost four times the entire effect.
                model: root.effect ? root.effect.maxTentacles : 0

                delegate: Node {
                    id: arm
                    required property int index

                    visible: root.effect && index < root.effect.count

                    readonly property real seed: {
                        const s = Math.sin(index * 127.1 + 311.7) * 43758.5453;
                        return s - Math.floor(s);
                    }
                    // Spread around the opening. The mouth is a circle in
                    // 3D seen at a tilt; its on-screen half-height is the
                    // measured depth, so the circle's true reach in z is
                    // that divided by the sine of the tilt. Placing them
                    // on a circle of the SCREEN ellipse's dimensions would
                    // bunch them all against the front of the bin.
                    readonly property real ang: index * 2.39996 + seed * 0.6
                    readonly property real rootR:
                        (root.effect ? root.effect.mouthRadius : 20) * 0.55
                    readonly property real zScale:
                        1.0 / Math.max(0.05, Math.sin(cam.tilt * Math.PI / 180))
                    readonly property real baseX:
                        (root.effect ? root.effect.mouthX : 0) + rootR * Math.sin(ang)
                    readonly property real baseZ:
                        (root.effect ? root.effect.mouthDepth : 6) * zScale * 0.55
                        * Math.cos(ang)
                    // Just under the lip — far enough down that the mask
                    // is exercised, and no further.
                    //
                    // It started a third of the bin down, on the reasoning
                    // that a root already above the lip tests nothing. But
                    // the model is ten times longer than it is wide, so
                    // burying half of it left only the thin end showing
                    // and five arms read as five spikes. What has to clear
                    // the rim is the THICK part; the taper belongs in the
                    // air above it.
                    readonly property real baseY:
                        (root.effect ? root.effect.mouthY : 0) - root.binH * 0.10
                    readonly property real armLength: root.binH * 0.50

                    // One mesh, one draw call. This was sixteen #Sphere
                    // models per arm — eighty nodes for five arms, each
                    // drawn a couple of pixels across. The bend now
                    // happens in the vertex shader, which is how the gel
                    // does it too (shaders/ooze3d.vert).
                    Model {
                        source: "qrc:/icons/meshes/tentacle.mesh"
                        position: Qt.vector3d(arm.baseX, arm.baseY, arm.baseZ)
                        // The mesh is authored standing up its own +Y,
                        // base at the origin, 135.8 units long — see
                        // "Bringing in a model" in docs/effects.md. Scaled
                        // so an arm reaches from inside the bin to a bit
                        // over the rim whatever size the icon is drawn at.
                        readonly property real k: arm.armLength / 135.838
                        scale: Qt.vector3d(k, k, k)
                        // One material per arm, and it has to be: the
                        // sway phase is a uniform, and five arms waving
                        // in step read as one object with five prongs.
                        // They share the shader and the maps — this is
                        // five uniform buffers, not five pipelines.
                        materials: CustomMaterial {
                            shadingMode: CustomMaterial.Shaded
                            property TextureInput iconTex: TextureInput {
                                texture: binTexture
                            }
                            property TextureInput albedoTex: TextureInput {
                                texture: albedoMap
                            }
                            property TextureInput normalTex: TextureInput {
                                texture: normalMap
                            }
                            // Where the bin's origin sits in the scene, so
                            // a fragment can work out where it lands on the
                            // bin whatever model it belongs to.
                            property vector3d binOrigin:
                                Qt.vector3d(binNode.x, binNode.y, 0)
                            property vector2d planeMin:
                                Qt.vector2d(-root.binW / 2, -root.binH / 2)
                            property vector2d planeSize:
                                Qt.vector2d(root.binW, root.binH)
                            property vector2d mouthCentre: Qt.vector2d(
                                root.effect ? root.effect.mouthCentreX : 0.5,
                                root.effect ? root.effect.mouthCentreY : 0.17)
                            property real mouthHalfWidth:
                                root.effect ? root.effect.mouthHalfWidth : 0.42
                            property real mouthDepth:
                                root.effect ? root.effect.mouthDepthFraction : 0.075
                            property real armRoughness: 0.39
                            // The mesh's own length, so the shader can say
                            // how far along an arm a vertex is without
                            // knowing how it has been scaled.
                            property real meshLength: 135.838
                            property real time: root.effect ? root.effect.time : 0
                            property real armPhase: arm.seed * 6.2831853
                            vertexShader: "qrc:/shaders3d/tentacle.vert"
                            fragmentShader: "qrc:/shaders3d/tentacle.frag"
                        }
                    }
                }
            }

        }
    }

    // The bin's artwork, and the arm's own maps. Declared once and
    // referenced by every arm's material, so each is uploaded once.
    Texture {
        id: binTexture
        textureData: root.effect ? root.effect.iconTexture : null
        minFilter: Texture.Linear
        magFilter: Texture.Linear
        tilingModeHorizontal: Texture.ClampToEdge
        tilingModeVertical: Texture.ClampToEdge
    }
    Texture {
        id: albedoMap
        source: "qrc:/icons/maps/tentacle_albedo.jpg"
        generateMipmaps: true
        mipFilter: Texture.Linear
    }
    Texture {
        id: normalMap
        source: "qrc:/icons/maps/tentacle_normal.png"
        generateMipmaps: true
        mipFilter: Texture.Linear
    }
}
