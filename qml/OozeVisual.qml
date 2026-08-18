import QtQuick
import QtQuick3D
import hyperbin

// The ooze, in Qt Quick 3D.
//
// Why 3D at all, for something drawn 60 points across: the look wanted
// is transmission — the bin seen THROUGH the sludge, refracted, with
// real specular and a clearcoat. Qt's PrincipledMaterial has all of that
// and Qt Quick 2D has no PBR material at all, so hand-rolling it in a
// QSGMaterial meant approximating one lighting term at a time and never
// arriving.
//
// The one thing Qt will not do for us is the backdrop: its automatic
// screen texture never produced the icon behind the mesh, and Quick3D
// exposes no control over it. So the icon goes in as an ordinary
// texture and the fragment snippet bends the lookup itself. Qt does the
// lighting; we do the refraction.
Item {
    id: root

    /// The OozeEffect that owns this. Set by the host on creation.
    required property var effect

    // Scene units are the bin's own pixels, so nothing here needs a
    // conversion factor and the geometry can be authored in the same
    // numbers the rest of the app uses.
    readonly property real binW: effect ? effect.binSize.width : 40
    readonly property real binH: effect ? effect.binSize.height : 40

    // Where the eyes are, so the gel can climb them. (x, y, z, radius),
    // and only the ones currently out are in the list — see
    // OozeEffect::eyeSpheres.
    readonly property var eyeList: effect ? effect.eyeSpheres : []
    function eyeSphere(i) {
        return i < eyeList.length ? eyeList[i] : Qt.vector4d(0, 0, 0, 0)
    }

    View3D {
        anchors.fill: parent
        camera: cam

        environment: SceneEnvironment {
            // The overlay is transparent; a view that clears to a colour
            // would paint a box over the Dock.
            backgroundMode: SceneEnvironment.Transparent
            // 2x, not 8x. This is a 60-point overlay; the difference is
            // invisible and the cost is not.
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
            // Mandatory, not decorative: a transmissive material with no
            // environment has nothing to reflect and renders as flat dark
            // paint — indistinguishable from the hand-rolled version this
            // replaces.
            lightProbe: Texture { source: "qrc:/icons/env.hdr" }
            probeExposure: 0.75
            probeHorizon: 0.5
        }

        // Orthographic, so world XY maps to the icon's UV exactly. The
        // bin is a flat thing seen head-on; a perspective camera would
        // turn that mapping into a screen-space projection for no gain.
        OrthographicCamera {
            id: cam
            // Tilted to match the shell's own viewpoint. Both platforms
            // draw the bin from above — you can see into it, and its
            // opening is an ellipse about a third as tall as it is wide,
            // which is a camera around 17 degrees up. Level with the bin
            // the gel's surface is edge-on and invisible, so the body
            // ended in a straight horizontal line: a fill gauge, not
            // something with liquid in it.
            readonly property real tilt: 17
            readonly property real dist: 600
            y: dist * Math.sin(tilt * Math.PI / 180)
            z: dist * Math.cos(tilt * Math.PI / 180)
            eulerRotation.x: -tilt
            // Magnification 1 means one world unit per pixel, which is
            // what the geometry is authored in — the mesh is built in the
            // bin's own pixels so nothing needs converting.
            horizontalMagnification: 1.0
            verticalMagnification: 1.0
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-35, -25, 0)
            brightness: 1.1
        }

        // Everything lives under a node placed on the BIN, not on the
        // item. The overlay's margins are asymmetric — room below for
        // drips, almost none above — so the two centres differ by more
        // than the bin is tall.
        Node {
            id: binNode
            x: root.effect ? root.effect.binRect.x + root.effect.binRect.width / 2
                             - root.width / 2 : 0
            y: root.effect ? -(root.effect.binRect.y + root.effect.binRect.height / 2
                               - root.height / 2) : 0
        // The effect places the eyes and works out where the pointer is in
        // the scene, and both of those need the camera's tilt. Pushed down
        // rather than written out again in C++, so there is one tilt in the
        // app — see OozeEffect::cameraTilt.
        Binding {
            target: root.effect
            property: "cameraTilt"
            value: cam.tilt
            when: root.effect !== null
        }

        // The eyes go in BEFORE the gel in the scene graph so the gel's
        // transparent pass composites over them — an eye drawn after it
        // sits on top of the body instead of inside it.
        OozeEyes {
            effect: root.effect
            camTilt: cam.tilt
            // The lids are the same substance as the body, so they read
            // the body's numbers rather than carrying their own.
            gooTint: sludgeMaterial.tint
            gooAbsorption: sludgeMaterial.absorption
            gooScatter: sludgeMaterial.scatter
        }

        Model {
            id: sludge
            geometry: OozeGeometry {
                id: oozeGeom
                level: root.effect ? root.effect.level : 0
                binWidth: root.binW
                binHeight: root.binH
                contentLine: root.effect ? root.effect.contentLine : 0.22
                source: root.effect
            }
            materials: sludgeMaterial
        }

        }
    }
    CustomMaterial {
        id: sludgeMaterial
        shadingMode: CustomMaterial.Shaded
        // ONE, not SrcAlpha. A shaded material's output is already
        // premultiplied — Qt's own fragment epilogue does
        // `colour = tonemap(colour) * alpha` — so blending with SrcAlpha
        // multiplied by the alpha a second time and quietly darkened the
        // whole body by however transparent it was.
        sourceBlend: CustomMaterial.One
        destinationBlend: CustomMaterial.OneMinusSrcAlpha

        property TextureInput iconTex: TextureInput {
            texture: Texture {
                textureData: root.effect ? root.effect.iconTexture : null
                minFilter: Texture.Linear
                magFilter: Texture.Linear
                // Quick3D repeats by default. The gel reaches below the
                // bin and past its sides, so those lookups ran off the
                // artwork and wrapped — the bin's coloured contents came
                // back up inside the puddle at its foot.
                tilingModeHorizontal: Texture.ClampToEdge
                tilingModeVertical: Texture.ClampToEdge
            }
        }
        property real time: root.effect ? root.effect.time : 0
        property real binWidth: root.binW
        property real magnify: 0.06
        property real absorption: 0.60
        // How much light the body scatters back out of itself, against
        // how much it simply lets through. Without this the gel can only
        // ever darken what is behind it.
        property real scatter: 0.55
        property real opacityAmount: 0.93
        property vector2d planeMin: Qt.vector2d(-root.binW / 2, -root.binH / 2)
        property vector2d planeSize: Qt.vector2d(root.binW, root.binH)
        // Enough to distort what is behind, and no more. Too little was
        // the failure that lasted longest: on the flat front of the body
        // the normal points straight at the camera, so a small offset
        // bends nothing at all there and the icon sat inside the gel
        // looking like a decal on glass rather than something submerged.
        property real refractAmount: root.binW * 0.09
        property real dispersion: 0.35
        property color tint: "#6fbf3a"

        // The eyes, one uniform each.
        //
        // Written out rather than looped, because a CustomMaterial has no
        // array uniform and GLSL cannot index a uniform that is not an
        // array. A slot nobody is using has a radius of zero, which the
        // shaders read as "not there" — so the list is as long as
        // OozeEffect::kMaxEyes and usually only part full.
        property vector4d eye0: root.eyeSphere(0)
        property vector4d eye1: root.eyeSphere(1)
        property vector4d eye2: root.eyeSphere(2)
        property vector4d eye3: root.eyeSphere(3)
        property vector4d eye4: root.eyeSphere(4)
        property vector4d eye5: root.eyeSphere(5)
        property vector4d eye6: root.eyeSphere(6)
        property vector4d eye7: root.eyeSphere(7)
        property vector4d eye8: root.eyeSphere(8)
        property vector4d eye9: root.eyeSphere(9)
        property vector4d eye10: root.eyeSphere(10)
        property vector4d eye11: root.eyeSphere(11)
        property vector4d eye12: root.eyeSphere(12)
        property vector4d eye13: root.eyeSphere(13)

        vertexShader: "qrc:/shaders3d/ooze3d.vert"
        fragmentShader: "qrc:/shaders3d/ooze3d.frag"
    }
}
