// The bin mark.
//
// Generated from the SVG by scripts/gen_mark.py. A Shape
// rather than an image so the colour is a property: this sits on a busy
// illustration in one place and could sit on anything later, and a
// baked-in black would be invisible on half of them.
import QtQuick
import QtQuick.Shapes
Item {
    id: root
    /// Fill colour. No default that works on both a light and a dark
    /// ground, so the caller always decides.
    property color color: "#ffffff"
    readonly property real viewWidth: 21
    readonly property real viewHeight: 19
    implicitWidth: viewWidth
    implicitHeight: viewHeight
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        smooth: true
        // Authored in the SVG's own coordinate space; scaled to whatever
        // size the item was given rather than making every caller do the
        // arithmetic.
        transform: Scale {
            xScale: root.width / root.viewWidth
            yScale: root.height / root.viewHeight
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: root.color
            fillRule: ShapePath.WindingFill
            PathSvg { path: "M8.07001 6.79C8.48001 6.94 8.91001 6.88 9.30001 6.73C9.74001 6.53 "
                  + "10.16 6.29 10.52 5.96C10.26 6.34 9.66001 7.17 9.22001 7.41C9.12001"
                  + " 8.05 8.97001 8.45 8.79001 8.94C9.19001 8.94 9.66001 9.41 10.17 9."
                  + "41C10.71 9.41 11.17 8.94 11.59 8.94C11.59 8.21 11.56 7.55 11.51 7."
                  + "01C11.51 6.77 11.46 6.55 11.43 6.32C11.41 6.14 11.38 5.98 11.36 5."
                  + "87C11.3 5.56 11.22 5.27 11.1 5C12.1 3.28 12.24 0.89 10.25 0L10.21 "
                  + "0.07C11.63 0.85 11.64 2.64 10.92 3.9C10.86 4.01 10.78 4.15 10.69 4"
                  + ".29C10.36 4.88 9.88001 5.41 9.24001 5.69C9.24001 5.69 9.24001 5.69"
                  + " 9.24001 5.7C9.21001 5.71 9.18001 5.72 9.15001 5.73C9.13001 5.73 9"
                  + ".11001 5.75 9.09001 5.76C8.78001 5.85 8.47001 5.81 8.22001 5.52C8."
                  + "10001 5.38 8.06001 5.26 8.06001 5.18C8.01001 4.98 8.05001 4.72 8.1"
                  + "8001 4.63C8.10001 4.78 8.12001 4.95 8.21001 5.09C8.68001 5.01 8.94"
                  + "001 4.99 9.11001 5.29C9.59001 5.24 10.3 4.83 10.69 4.27C10.15 3.57"
                  + " 9.31001 3.19 8.06001 3.43C6.23001 3.95 6.30001 6.12 8.07001 6.78V"
                  + "6.79Z" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: root.color
            fillRule: ShapePath.WindingFill
            PathSvg { path: "M6.12002 8.39002C6.30002 8.45002 6.40002 8.63002 6.47002 8.83002C6"
                  + ".90002 8.87002 7.30002 9.67002 8.51002 8.93002C8.35002 8.24002 8.0"
                  + "5002 7.58002 7.44002 7.11002C6.64002 6.41002 5.17002 6.80002 4.620"
                  + "02 7.11002C4.36002 7.16002 4.25002 7.26002 4.13002 7.05002C3.88002"
                  + " 6.59002 4.10002 5.68002 4.07002 5.09002C4.07002 4.85002 3.78002 4"
                  + ".59002 3.54002 4.64002C3.37002 4.65002 3.23002 4.76002 3.10002 4.8"
                  + "4002C2.22002 5.45002 1.15002 5.69002 0.0300195 5.63002L0.0200195 5"
                  + ".70002C0.98002 5.98002 2.08002 5.95002 3.09002 5.64002C2.69002 7.0"
                  + "5002 2.93002 8.81002 4.91002 8.57002C5.45002 8.50002 5.74002 8.320"
                  + "02 6.14002 8.40002L6.12002 8.39002Z" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: root.color
            fillRule: ShapePath.WindingFill
            PathSvg { path: "M19.9799 9.55014C19.3899 9.80014 18.7699 9.99014 18.1599 10.0201C1"
                  + "6.9099 10.1701 16.0699 9.19014 16.4699 8.02014C16.5499 7.75014 16."
                  + "6599 7.55014 16.8199 7.25014C16.9999 6.91014 17.1599 6.45014 17.04"
                  + "99 5.91014C16.9399 5.36014 16.4899 4.91014 16.0699 4.72014C13.9699"
                  + " 3.91014 12.4299 4.97014 11.6899 6.59014C11.8199 7.18014 11.9399 8"
                  + ".00014 11.9399 8.93014C12.2599 8.93014 12.7199 9.24014 12.9699 9.2"
                  + "3014C13.3299 9.22014 13.4299 8.87014 13.6699 8.86014C13.5099 8.140"
                  + "14 13.4999 7.27014 13.8799 6.75014C14.1099 6.22014 15.4699 5.80014"
                  + " 15.7799 6.26014C15.8299 6.35014 15.8399 6.56014 15.7599 6.79014C1"
                  + "4.5099 10.1701 17.3799 11.4901 20.0199 9.63014L19.9799 9.56014V9.5"
                  + "5014Z" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: root.color
            fillRule: ShapePath.WindingFill
            PathSvg { path: "M10.15 10.19C5.88 10.19 6.34 9.04998 5.88 8.97998L6.55 16.96C6.58 "
                  + "17.36 6.91 17.66 7.31 17.66C7.31 17.66 8.72 18.15 10.2 18.15C11.68"
                  + " 18.15 12.99 17.66 12.99 17.66C13.39 17.66 13.71 17.36 13.75 16.96"
                  + "L14.42 8.99998C13.96 9.06998 14.44 10.19 10.16 10.19H10.15ZM8.75 1"
                  + "6.23C8.44 16.25 8.17 16.01 8.15 15.69L7.88 12.17C7.86 11.86 8.1 11"
                  + ".59 8.42 11.57C8.73 11.55 9.00001 11.79 9.02001 12.11L9.29 15.63C9"
                  + ".31 15.94 9.07 16.21 8.75 16.23ZM12.14 15.7C12.12 16.01 11.86 16.2"
                  + "5 11.54 16.24C11.23 16.22 10.99 15.95 11 15.64L11.27 12.12C11.29 1"
                  + "1.81 11.55 11.57 11.87 11.58C12.19 11.59 12.42 11.86 12.41 12.18L1"
                  + "2.14 15.7Z" }
        }
    }
}
