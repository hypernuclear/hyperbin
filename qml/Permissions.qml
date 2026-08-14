import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What the app needs, and where to switch it on.
//
// This exists because macOS gives an app no way to ask for Full Disk
// Access. There is a prompt for Accessibility and nothing at all for
// disk access — the most any app can do is open the settings pane and
// explain itself. So the explaining happens here.
//
// What makes that bearable is that the app is already listed in the pane
// by the time the user arrives: TCC adds an app to Full Disk Access the
// moment it ATTEMPTS a protected read, and hyperbin attempts one on
// startup for exactly this reason. The instruction can therefore be
// "turn hyperbin on", not "find hyperbin and add it".
ApplicationWindow {
    id: win

    // Set from C++: a list of { id, title, detail, granted }.
    property var items: permissionList

    readonly property bool allGranted: {
        for (let i = 0; i < items.length; ++i)
            if (!items[i].granted)
                return false;
        return items.length > 0;
    }

    title: qsTr("hyperbin Permissions")
    flags: Qt.Dialog
    width: 460
    height: content.implicitHeight + 48
    minimumWidth: 460
    minimumHeight: content.implicitHeight + 48

    // Explicit width, no anchors, and the window's height follows from
    // this layout rather than the other way round.
    //
    // Both halves matter. Anchoring the layout to fill its parent makes
    // it take its height FROM the parent, so deriving the parent's height
    // from it is circular and the last row falls off the bottom. And the
    // width has to be a plain number before any of it works at all:
    // wrapped text cannot report a height until it knows how wide it is,
    // so a layout whose own width is still being resolved measures every
    // wrapped label as one line.
    ColumnLayout {
        id: content
        x: 24
        y: 24
        width: win.width - 48
        spacing: 18

        Label {
            Layout.fillWidth: true
            text: qsTr("hyperbin needs your permission to find the Trash and "
                       + "see how full it is.")
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            font.bold: true
        }

        Repeater {
            model: win.items
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                // A tick or a dot rather than a colour alone — the two
                // states have to be distinguishable without relying on
                // being able to tell green from grey.
                Label {
                    text: modelData.granted ? "✓" : "●"
                    color: modelData.granted ? "#2e9e4f" : "#b0aca6"
                    font.pixelSize: 18
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: modelData.title
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.detail
                        wrapMode: Text.WordWrap
                        opacity: 0.75
                        font.pixelSize: 12
                    }
                }

                Button {
                    text: modelData.granted ? qsTr("Granted") : qsTr("Open Settings…")
                    enabled: !modelData.granted
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: permissionBridge.open(modelData.id)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            // Nothing left to instruct once everything is switched on.
            visible: !win.allGranted
            text: qsTr("Switch hyperbin on in the list that opens — it is "
                       + "already there. This window updates on its own; no "
                       + "restart needed.")
            wrapMode: Text.WordWrap
            opacity: 0.7
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: win.allGranted ? qsTr("Done") : qsTr("Later")
                onClicked: win.close()
            }
        }
    }

    // The OS notifies nobody when a switch is flicked, so the state has
    // to be re-tested. Only while this window is open: the honest test
    // for disk access is a directory listing, which is not something to
    // run once a second forever.
    Timer {
        running: win.visible
        interval: 1000
        repeat: true
        onTriggered: permissionBridge.refresh()
    }

    // Closing itself the moment the last switch goes on is the whole
    // reward for the trip to Settings.
    onAllGrantedChanged: if (allGranted && visible) closeSoon.start()
    Timer {
        id: closeSoon
        interval: 900   // long enough to see the last tick appear
        onTriggered: win.close()
    }
}
