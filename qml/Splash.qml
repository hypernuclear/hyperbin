import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// The brand moment: shown once on first run, dismissed by clicking it or
// by waiting.
//
// Frameless and borderless on purpose — a title bar and a close button
// around a full-bleed illustration reads as a document window, which is
// the opposite of what this is for. That does mean the artwork carries
// the whole thing, so everything below is about keeping the marks
// legible on top of it without covering it up.
Window {
    id: win

    /// Milliseconds on screen before it leaves by itself. A brand moment
    /// that outstays its welcome stops being one.
    ///
    /// Zero means it stays until clicked. That is the right behaviour
    /// when the user asked to see it: something opened on request should
    /// not take itself away again while they are still looking at it.
    property int dwellMs: 4000

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

        // Scrims, top and bottom.
        //
        // The illustration is busy everywhere — measured, the quietest
        // patch still has a brightness spread of about fifty — so a plain
        // white wordmark laid on it competes with whatever is underneath
        // and loses. These darken just the two bands the marks sit in and
        // leave the middle, which is the part worth looking at, alone.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: parent.height * 0.30
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#cc000000" }
                GradientStop { position: 1.0; color: "#00000000" }
            }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.height * 0.26
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 1.0; color: "#cc000000" }
            }
        }

        // The product mark, centred across the top where the artwork is a
        // night sky and has the least going on.
        HyperbinWordmark {
            color: "#ffffff"
            width: parent.width * 0.34
            height: width * (viewHeight / viewWidth)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.075
        }


        // The house mark, lower left.
        HypernuclearWordmark {
            color: "#ffffff"
            opacity: 0.9
            width: parent.width * 0.115
            height: width * (viewHeight / viewWidth)
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: parent.width * 0.045
            anchors.bottomMargin: parent.height * 0.055
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
        onClicked: win.dismiss()
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

    Timer { id: dwell; interval: win.dwellMs; onTriggered: win.dismiss() }
    // Outlasts the fade, so the window is not torn down mid-animation.
    Timer { id: gone; interval: 260; onTriggered: win.close() }
}
