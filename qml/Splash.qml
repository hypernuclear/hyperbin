import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Shapes

// The brand moment: shown once on first run, dismissed by clicking it or
// by waiting.
//
// Frameless and borderless on purpose — a title bar and a close button
// around a full-bleed illustration reads as a document window, which is
// the opposite of what this is for. That does mean the artwork carries
// the whole thing, so everything below is about keeping the marks
// legible on top of it without covering it up.
Window {
    id: root

    /// Milliseconds on screen before it leaves by itself. A brand moment
    /// that outstays its welcome stops being one.
    ///
    /// Zero means it stays until clicked. That is the right behaviour
    /// when the user asked to see it: something opened on request should
    /// not take itself away again while they are still looking at it.
    property int dwellMs: 4000
    /// The brand's green, and the ink that sits on it.
    readonly property color brandGreen: "#72f987"
    readonly property color brandInk: "#0d1a0f"

    // Bundled, not requested by name: the system UI face is what a
    // missing font quietly becomes, and it looks close enough to Inter
    // that nobody would notice the panel had gone generic.
    FontLoader { id: inter; source: "qrc:/icons/Inter-SemiBold.ttf" }

    // Sized from the artwork's own aspect so it is never letterboxed or
    // cropped, at roughly half its pixel size — the source is 1280x960,
    // so this lands near 1:1 on a Retina display and stays sharp.
    width: 640
    height: 480
    flags: Qt.SplashScreen | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    // The rounded corner, as a MASK rather than a clip.
    //
    // Rectangle.radius plus clip: true does not do it: clipping in Qt
    // Quick is rectangular whatever shape the clipper is drawn as, so the
    // artwork ran straight into all four square corners. Masking the
    // whole composed surface is the only thing that actually cuts it.
    Rectangle {
        id: cornerMask
        anchors.fill: parent
        radius: 14
        color: "black"
        visible: false
        layer.enabled: true
    }
    Item {
        id: body
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: cornerMask
            // Without a threshold the mask's antialiased edge is treated
            // as partial coverage twice over and the corner comes out
            // soft and grey.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
        Rectangle {
            anchors.fill: parent
            color: "#0b0b0d"   // shows for the one frame before decode
        }

        Image {
            anchors.fill: parent
            source: "qrc:/icons/splash.jpg"
            fillMode: Image.PreserveAspectCrop
            asynchronous: false   // a splash that fades in blank is worse
            smooth: true
        }

        // --- the house mark, lower left --------------------------------
        // On a dark glow rather than a scrim. A black shadow with no
        // offset and a wide blur darkens only what is immediately behind
        // the letters, which is what a multiply would do — for pure black
        // the two are the same operation — and it leaves the rest of the
        // illustration alone. A rectangular scrim did not: it flattened a
        // whole band of the artwork to make room for two words.
        HypernuclearWordmark {
            color: root.brandGreen
            width: parent.width * 0.158
            height: width * (viewHeight / viewWidth)
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: parent.width * 0.060
            anchors.bottomMargin: parent.height * 0.085
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowBlur: 1.0
                shadowOpacity: 0.85
                shadowScale: 1.06
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
            }
        }
        // --- the product panel, lower right ----------------------------
        // Flush to the right edge and floating clear of the bottom, as
        // drawn. Every number here is measured off the comp rather than
        // judged: the panel, the lockup's ink and the version's cap
        // heights were read out of the artwork in pixels and divided
        // through, which is why they are five figures and not round.
        //
        // The lockup goes in as one asset: mark and wordmark were
        // separate pieces here and the spacing between them was mine to
        // guess, which is not a thing worth guessing about.
        Item {
            id: panel
            width: parent.width * 0.31190
            height: parent.height * 0.13469
            anchors.right: parent.right
            anchors.rightMargin: parent.width * 0.00484
            anchors.bottom: parent.bottom
            anchors.bottomMargin: parent.height * 0.05720

            /// How far the foot of the left edge sits in from its head.
            /// Held against the panel's own height so the lean stays the
            /// same angle — 20° off vertical — whatever the panel is
            /// scaled to.
            readonly property real slant: height * 0.35616

            // A trapezoid, not a rectangle: three square edges and one
            // leaning in from the left, running roughly along the mark's
            // own diagonal. Drawn as a Shape because there is no way to
            // ask a Rectangle for a non-rectangular corner.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                antialiasing: true
                ShapePath {
                    fillColor: root.brandGreen
                    strokeColor: "transparent"
                    startX: 0
                    startY: 0
                    PathLine { x: panel.width; y: 0 }
                    PathLine { x: panel.width; y: panel.height }
                    PathLine { x: panel.slant; y: panel.height }
                    PathLine { x: 0; y: 0 }
                }
            }
            // Sized by HEIGHT, with the width following the artwork's
            // aspect. Driving it from the width instead is what pushed
            // the wordmark out through the right edge and left the
            // version sitting below the panel entirely: the lockup is
            // four times wider than it is tall, so a width chosen against
            // the panel decides a height nobody checked against it.
            //
            // Set from the right edge, not centred. The slant eats into
            // the left, so a centred lockup would drift right as the
            // panel scaled; the gap that has to hold is the one to the
            // straight edge.
            HyperbinLockup {
                id: lockup
                color: root.brandInk
                height: panel.height * 0.58790
                width: height * (viewWidth / viewHeight)
                anchors.right: parent.right
                anchors.rightMargin: panel.width * 0.11774
                anchors.top: parent.top
                anchors.topMargin: panel.height * 0.11644
            }
            // Right-aligned under the wordmark, and close under it — the
            // comp leaves three pixels of air between the two, so this
            // tucks up with a negative margin. The lockup's own box
            // carries a sliver of empty space below its ink, and left
            // alone that sliver becomes a gap nobody drew.
            //
            // Reads the app's real version rather than carrying a copy:
            // two places to change a version number is one place to
            // forget.
            Text {
                text: "v" + Qt.application.version
                color: root.brandInk
                font.family: inter.name
                font.weight: Font.DemiBold
                font.pixelSize: panel.height * 0.21653
                anchors.right: lockup.right
                anchors.top: lockup.bottom
                anchors.topMargin: -panel.height * 0.06480
            }
        }
        // The brand edge, drawn last so it sits over the artwork.
        //
        // A border on this Rectangle rather than on the window: the
        // window is transparent and frameless, so there is no frame to
        // colour. Inside the masked item, so the rounded corner cuts the
        // border along with everything else — a border drawn outside the
        // mask would square off the corners it is meant to trace.
        //
        // radius one pixel under the mask's own, because a stroke is
        // centred on the path: at equal radii its outer half falls where
        // the mask has already faded out, and the corner reads thinner
        // than the straight edges.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: cornerMask.radius - 1
            border.width: 5
            border.color: root.brandGreen
            antialiasing: true
        }
        // Anywhere, not a button. There is no close control on a
        // frameless window, so the whole surface has to be the way out.
    }
    // Outside the masked surface: a mask makes its input a texture, and
    // input handling inside one still works but there is no reason to
    // pay for it here.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.dismiss()
    }

    // Fading rather than vanishing, both ways. A splash that pops in and
    // pops out reads as a glitch on something this brief.
    opacity: 0
    Behavior on opacity { NumberAnimation { duration: 220 } }

    function appear() {
        visible = true;
        opacity = 1;
        if (dwellMs > 0)
            dwell.restart();
        else
            dwell.stop();
    }
    function dismiss() {
        dwell.stop();
        opacity = 0;
        gone.restart();
    }

    Timer { id: dwell; interval: root.dwellMs; onTriggered: root.dismiss() }
    // Outlasts the fade, so the window is not torn down mid-animation.
    Timer { id: gone; interval: 260; onTriggered: root.close() }
}
