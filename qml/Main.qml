import QtQuick
import QtQuick.Controls
import hyperbin

Window {
    id: root

    // Overlay mode: frameless, click-through, transparent, no taskbar
    // entry. Native code raises it above the Dock / parents it into the
    // desktop layer — Qt can't express either portably.
    flags: windowedMode
           ? Qt.Window
           : (Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
              | Qt.Tool | Qt.WindowTransparentForInput)

    // Flies are near-black silhouettes, so the dev backdrop has to be
    // light or they're invisible against it — stand in for a desktop
    // rather than using an app-chrome dark.
    // --paintdebug fills the overlay so you can see where it actually is.
    // A transparent overlay that renders nothing looks identical to one
    // that isn't there at all; this tells them apart in one screenshot.
    readonly property bool paintDebug:
        Qt.application.arguments.indexOf("--paintdebug") >= 0

    color: windowedMode ? "#cfcac2"
                        : (paintDebug ? "#80ff0000" : "transparent")
    width: 480
    height: 540
    visible: windowedMode

    // The bin itself, UNDER the overlay — the position the shell draws it
    // in. Dev mode used to show only an empty outline, which was fine for
    // judging where flies were but useless for anything that lets the bin
    // show through itself: the ooze cuts its own top surface open so the
    // bin can stand up out of it, and against a blank background that
    // hole was indistinguishable from a rendering failure.
    Image {
        visible: windowedMode
        // Source, not just visibility: an Image loads its source whether
        // or not it is shown, so binding only `visible` still sent
        // overlay mode to the provider — which has no preview artwork
        // outside dev mode and answered with "Failed to get image from
        // provider" on every launch.
        source: windowedMode ? "image://preview/bin" : ""
        x: flies.binRect.x
        y: flies.binRect.y
        width: flies.binRect.width
        height: flies.binRect.height
        smooth: true
    }
    Rectangle {
        visible: windowedMode
        x: flies.binRect.x
        y: flies.binRect.y
        width: flies.binRect.width
        height: flies.binRect.height
        color: "#00000000"
        border.color: "#3a7a736a"
        border.width: 1
    }

    // The measured mouth, drawn over the artwork it was measured from.
    //
    // --binmouth only. This is the one thing in the app whose correctness
    // cannot be judged from the effect that uses it: a tentacle masked
    // against a mouth ten pixels too low looks like a tentacle that is
    // slightly wrong, not like a mouth that is. Drawn on the icon, a
    // wrong answer is obvious in one screenshot.
    Item {
        id: mouthDebug
        visible: windowedMode
                 && Qt.application.arguments.indexOf("--binmouth") >= 0
        x: flies.binRect.x
        y: flies.binRect.y
        width: flies.binRect.width
        height: flies.binRect.height

        Rectangle {
            // The opening. Ellipse via a rounded rectangle: radius at half
            // the smaller side IS an ellipse, and this needs no Shape.
            x: (flies.mouthCentre.x - flies.mouthHalfWidth) * parent.width
            y: (flies.mouthCentre.y - flies.mouthDepth) * parent.height
            width: flies.mouthHalfWidth * 2 * parent.width
            height: flies.mouthDepth * 2 * parent.height
            radius: Math.min(width, height) / 2
            color: "transparent"
            border.color: flies.mouthMeasured ? "#ff3030" : "#ffb020"
            border.width: 2
        }
        Rectangle {
            // The near lip: what an emerging shape has to pass behind.
            y: (flies.mouthCentre.y + flies.mouthDepth) * parent.height
            width: parent.width
            height: 1
            color: "#30dd50"
        }
        Text {
            y: (flies.mouthCentre.y + flies.mouthDepth) * parent.height + 4
            color: "#207030"
            font.pixelSize: 11
            text: (flies.mouthMeasured ? "measured" : "FALLBACK")
                  + "  near " + flies.mouthCentre.y.toFixed(3) + "+"
                  + flies.mouthDepth.toFixed(3)
        }
    }

    EffectItem {
        id: flies
        objectName: "effect"
        anchors.fill: parent

        // In windowed dev mode nothing drives these, so give the sim a
        // target to orbit and a clock to run on.
        Component.onCompleted: {
            if (windowedMode) {
                // Big, so an effect can be judged. The overlay's real
                // margins are proportional to the icon, so this is the
                // same geometry the Dock produces, just larger.
                // Headroom above for anything that reaches OUT of the
                // bin — tentacles clear the rim by about a fifth of the
                // icon and were being clipped by the window. The room
                // below is unchanged, which the ooze needs for its drips.
                binRect = Qt.rect(90, 100, 300, 300);
                // HYPERBIN_PREVIEW_FULL pins the slider, so anything that
                // scales with fullness — the ooze's level, how many eyes
                // surface — can be shot at a chosen value instead of
                // being dragged to it by hand.
                fullness = Number(Qt.application.arguments.indexOf("--full") >= 0
                                  ? Qt.application.arguments[
                                        Qt.application.arguments.indexOf("--full") + 1]
                                  : 0.85);
                frameIntervalMs = 16;
            }
        }

        // Dev harness: --empty-at <ms> empties the bin at a chosen moment,
        // so anything that only happens on the way OUT can be shot.
        // Without it a grab-and-exit run can never see a withdrawal at
        // all, which is how a leaving animation ends up shipped on the
        // strength of "it looked right when I dragged the slider".
        readonly property int emptyAt: {
            const i = Qt.application.arguments.indexOf("--empty-at");
            return i >= 0 ? Number(Qt.application.arguments[i + 1]) : 0;
        }
        Timer {
            running: windowedMode && flies.emptyAt > 0
            interval: Math.max(1, flies.emptyAt)
            onTriggered: flies.fullness = 0
        }
    }

    // Dev affordance: drag to feel the swarm grow and agitate.
    Row {
        visible: windowedMode
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 8

        Slider {
            id: fullnessSlider
            width: parent.width - 64
            value: flies.fullness
            onMoved: flies.fullness = value
        }
        Label {
            width: 56
            anchors.verticalCenter: fullnessSlider.verticalCenter
            color: "#2b2622"
            font.pixelSize: 11
            text: Math.round(flies.fullness * 100) + "%"
        }
    }
}
