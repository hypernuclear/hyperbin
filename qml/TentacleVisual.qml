import QtQuick
import QtQuick3D
import hyperbin

// Tentacles coming out of the bin.
//
// Three arms seated as a triangle in the measured opening, bent in their
// own vertex shader, and cut where the bin and the rubbish stand in front
// of them. The geometry is a sculpted model with its skin and suckers
// baked into an albedo and a normal map — see "Bringing in a model" in
// docs/effects.md. It replaced chains of tapering spheres that existed
// only to prove the occlusion.
//
// The occlusion is still the part to be careful with: an arm starts below
// the lip and inside the heap, so the stretch that has not emerged must
// disappear behind artwork this effect never draws. shaders/tentacle.frag
// is where that happens and why it is done by cutting our own geometry
// rather than repainting the bin.
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
            // Mandatory, not decorative. With a single directional light
            // and nothing else, everything facing away from it renders
            // black — which is exactly what these looked like. The probe
            // is what puts light back into the shadowed side, and the
            // gel's scene already carries it, so it costs no new asset.
            lightProbe: Texture { source: "qrc:/icons/env.hdr" }
            probeExposure: 1.6
            probeHorizon: 0.4
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
            brightness: 1.6
        }
        // The chains are solved in C++ but the camera lives here, and the
        // solver needs its tilt to turn the opening's measured on-screen
        // half-height into a reach in z. Pushed rather than duplicated as
        // a constant on both sides.
        Binding {
            target: root.effect
            property: "cameraTilt"
            value: cam.tilt
            when: root.effect !== null
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
                    // Where this arm sits, as fractions of the opening —
                    // a triangle with its apex at the back. The table and
                    // the reasoning are in src/effects/TentacleEffect.h.
                    readonly property var seat: root.effect
                        && index < root.effect.seats.length
                        ? root.effect.seats[index] : null
                    // Fake perspective, the one thing the seat is still
                    // read for here — an orthographic camera does not
                    // shrink the far arm, so it is scaled by hand.
                    readonly property real sizeScale: seat ? seat.z : 1
                    Model {
                        id: armModel
                        source: "qrc:/icons/meshes/tentacle.mesh"
                        // NO TRANSFORM. Position, length and bend all
                        // live in the solved chain, in scene units, so the
                        // model is a plain container sitting on the bin's
                        // own origin and every vertex is placed by the
                        // shader. It used to carry a position and an
                        // anisotropic scale; with a chain those would have
                        // to be undone before the joints could be used.
                        //
                        // Girth is the one thing left, because the chain
                        // is a centre line and carries no thickness. The
                        // multiplier is MESH-SPECIFIC — it compensates for
                        // the model's own radius-to-length ratio, 0.054 at
                        // its thickest — and lands the base at about 0.22
                        // of the bin's width, which reads as an arm rather
                        // than a cable at Dock size.
                        readonly property real girth:
                            (root.effect ? root.effect.armLength : 20)
                            * arm.sizeScale / 8.932 * 1.52
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
                            property real meshLength: 8.932
                            // The solved chain. Two rows an arm — joint
                            // positions then the frame across it — so the
                            // row to read is twice the index.
                            property TextureInput spineTex: TextureInput {
                                texture: spineTexture
                            }
                            property int armRow: arm.index
                            property int jointCount:
                                root.effect ? root.effect.jointCount : 16
                            property real girthScale: armModel.girth

                            // --- the rubbish, as a volume ---------------
                            //
                            // A mound filling the opening, for arms at the
                            // back of the mouth to pass behind. Its top
                            // and floor are measured against the artwork
                            // in C++; its width is the opening's, in both
                            // axes, which for z means the tilt-corrected
                            // reach and not the screen ellipse's height.
                            readonly property real heapZ:
                                root.effect ? root.effect.mouthReachZ : 20
                            readonly property real heapTop:
                                root.effect ? root.effect.heapTopY : 0
                            readonly property real heapFloor:
                                root.effect ? root.effect.heapFloorY : 0
                            property vector3d heapCentre: Qt.vector3d(
                                root.effect ? root.effect.mouthX : 0,
                                (heapTop + heapFloor) * 0.5, 0)
                            property vector3d heapRadii: Qt.vector3d(
                                root.effect ? root.effect.mouthRadius : 20,
                                Math.max(1, (heapTop - heapFloor) * 0.5),
                                Math.max(1, heapZ))
                            // Toward the eye. The camera is orthographic,
                            // so this is the same for every fragment —
                            // which is the whole reason a ray query
                            // against the heap is affordable at all.
                            property vector3d camToEye: Qt.vector3d(
                                0, Math.sin(cam.tilt * Math.PI / 180),
                                Math.cos(cam.tilt * Math.PI / 180))

                            // Inside the bin is dark. The gradient reaches
                            // a tenth of the bin ABOVE the heap's crest,
                            // so an arm is still in shadow at the moment it
                            // breaks the surface and climbs out of it over
                            // the next sixth — referenced to the crest
                            // itself it would be at full brightness the
                            // instant it became visible.
                            // Darker, deeper, and reaching further up.
                            //
                            // An arm is meant to look like it is coming
                            // OUT of the rubbish, and the strongest cue
                            // for that is the light falling off as it goes
                            // in. At 0.30 over a sixth of the bin the
                            // gradient was there but too weak and too
                            // short to read, so the arm looked laid on top
                            // of the mouth rather than passing through it.
                            property real shadeTopY: heapTop + root.binH * 0.16
                            property real shadeSpan: root.binH * 0.26
                            property real binShade: 0.13
                            // Aerial perspective, from the seat's depth:
                            // -1 at the far lip is dimmed, +1 at the near
                            // one is not.
                            property real depthShade:
                                1.0 - 0.18 * (0.5 - 0.5 * (arm.seat ? arm.seat.y : 0))
                            // The bin's own body, for telling an arm
                            // inside or behind it from one reaching down
                            // the outside. Both axes, because the two
                            // differ: x is the opening's measured
                            // half-width, z is that half-width's
                            // tilt-corrected reach into the scene.
                            //
                            // Nine tenths of the opening, not all of it —
                            // the bin tapers below the lip, so any single
                            // number has to be an average. Erring narrow
                            // shows a sliver of arm that is really inside;
                            // erring wide deletes a strike that is really
                            // outside. Narrow is the cheaper mistake.
                            // The opening's full half-width in both axes;
                            // the shader tapers it with depth rather than
                            // taking a single average.
                            property vector2d binHalfSize: Qt.vector2d(
                                (root.effect ? root.effect.mouthRadius : 20),
                                Math.max(1, heapZ))
                            property real lipY:
                                root.effect ? root.effect.mouthY : 0
                            property real binHeight: root.binH
                            // From C++, so the mask and the strike aim at
                            // the same wall.
                            property real bodyTop:
                                root.effect ? root.effect.bodyTaperTop : 0.98
                            property real bodyFoot:
                                root.effect ? root.effect.bodyTaperFoot : 0.78
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
    // The solved chains. NEAREST and no mipmaps, deliberately: these are
    // joint positions in scene units, not a picture, and the shader reads
    // them with texelFetch. Filtering would interpolate between two arms
    // at the row boundary.
    Texture {
        id: spineTexture
        textureData: root.effect ? root.effect.spineTexture : null
        minFilter: Texture.Nearest
        magFilter: Texture.Nearest
        mipFilter: Texture.None
        generateMipmaps: false
        tilingModeHorizontal: Texture.ClampToEdge
        tilingModeVertical: Texture.ClampToEdge
    }
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
        source: "qrc:/icons/maps/tentacle_normal.jpg"
        generateMipmaps: true
        mipFilter: Texture.Linear
    }
}
