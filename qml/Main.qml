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
    height: 480
    visible: windowedMode

    // The bin itself, UNDER the overlay — the position the shell draws it
    // in. Dev mode used to show only an empty outline, which was fine for
    // judging where flies were but useless for anything that lets the bin
    // show through itself: the ooze cuts its own top surface open so the
    // bin can stand up out of it, and against a blank background that
    // hole was indistinguishable from a rendering failure.
    Image {
        visible: windowedMode
        source: "image://preview/bin"
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
                binRect = Qt.rect(90, 40, 300, 300);
                fullness = 0.85;
                frameIntervalMs = 16;
            }
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
