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
                    readonly property var state: root.effect
                        && index < root.effect.arms.length
                        ? root.effect.arms[index] : null
                    // Where this arm sits, as fractions of the opening —
                    // a triangle with its apex at the back. The table and
                    // the reasoning are in src/effects/TentacleEffect.h.
                    readonly property var seat: root.effect
                        && index < root.effect.seats.length
                        ? root.effect.seats[index] : null
                    // The opening is a CIRCLE in 3D seen at a tilt, so its
                    // reach in z is its on-screen half-height divided by
                    // the sine of that tilt. Laid out on the screen
                    // ellipse's own dimensions instead, every arm would
                    // bunch against the front of the bin.
                    readonly property real zScale:
                        1.0 / Math.max(0.05, Math.sin(cam.tilt * Math.PI / 180))
                    readonly property real baseX:
                        (root.effect ? root.effect.mouthX : 0)
                        + (root.effect ? root.effect.mouthRadius : 20)
                          * (seat ? seat.x : 0)
                    readonly property real baseZ:
                        (root.effect ? root.effect.mouthDepth : 6) * zScale
                        * (seat ? seat.y : 0)
                    readonly property real baseY:
                        root.effect ? root.effect.rootY : 0
                    // Both from C++, because both are derived from the bin
                    // rather than chosen: the length is what it takes to
                    // reach over the rim and down the outside, and the
                    // size is the fake perspective an orthographic camera
                    // does not supply.
                    readonly property real armLength:
                        (root.effect ? root.effect.armLength : 20)
                        * (seat ? seat.z : 1)

                    // One mesh, one draw call. This was sixteen #Sphere
                    // models per arm — eighty nodes for five arms, each
                    // drawn a couple of pixels across. The bend now
                    // happens in the vertex shader, which is how the gel
                    // does it too (shaders/ooze3d.vert).
                    Model {
                        source: "qrc:/icons/meshes/tentacle.mesh"
                        position: Qt.vector3d(arm.baseX, arm.baseY, arm.baseZ)
                        // The mesh is authored standing up its own +Y,
                        // base at the origin, 8.932 units long — see
                        // "Bringing in a model" in docs/effects.md. Scaled
                        // so an arm reaches from inside the bin to a bit
                        // over the rim whatever size the icon is drawn at.
                        // Length and girth scaled SEPARATELY. Uniform
                        // scaling ties the two together, and this model is
                        // nine times longer than it is wide — sized by
                        // length alone it is a whip, and sized by girth
                        // alone it runs off the top of the screen.
                        //
                        // The multiplier is set so the base lands at about
                        // 0.22 of the bin's width, which is what reads as
                        // an arm rather than a cable at Dock size. It is
                        // MESH-SPECIFIC and has to be re-derived whenever
                        // the model changes: it is compensating for the
                        // mesh's own radius-to-length ratio, which for
                        // this one is 0.054 at its thickest.
                        //
                        // What no longer needs compensating for is the
                        // TAPER. The model this replaced fell to a tenth
                        // of its peak radius by the tip, so most of its
                        // length was thin whatever it was scaled by and
                        // the multiplier was fighting that. This one holds
                        // a third of its peak at the tip, so it reads as
                        // thick along its whole length on its own.
                        readonly property real k: arm.armLength / 8.932
                        readonly property real girth: k * 1.40
                        scale: Qt.vector3d(girth, k, girth)
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
                            property real time: root.effect ? root.effect.time : 0
                            property real armPhase: arm.seed * 6.2831853
                            property real curlAngle: arm.state ? arm.state.x : 0.5
                            property real curlDir: arm.state ? arm.state.y : 0.0

                            // --- the rubbish, as a volume ---------------
                            //
                            // A mound filling the opening, for arms at the
                            // back of the mouth to pass behind. Its top
                            // and floor are measured against the artwork
                            // in C++; its width is the opening's, in both
                            // axes, which for z means the tilt-corrected
                            // reach and not the screen ellipse's height.
                            readonly property real heapZ:
                                (root.effect ? root.effect.mouthDepth : 6)
                                * arm.zScale
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
                            property real shadeTopY: heapTop + root.binH * 0.10
                            property real shadeSpan: root.binH * 0.16
                            property real binShade: 0.30
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
                            property vector2d binHalfSize: Qt.vector2d(
                                (root.effect ? root.effect.mouthRadius : 20) * 0.90,
                                Math.max(1, heapZ * 0.90))
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
        source: "qrc:/icons/maps/tentacle_normal.jpg"
        generateMipmaps: true
        mipFilter: Texture.Linear
    }
}
