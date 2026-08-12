// Generated from resources/hyperbin.svg — see scripts/gen_glyph.py.
import QtQuick
import QtQuick.Shapes
import hyperbin

// The bin glyph as a shape rather than a bitmap, so its colour is a
// property instead of being baked into the asset. Used for the menu-bar
// item and available to any in-app UI that needs the mark.
Item {
    id: root

    /// Fill colour. Callers bind this to the theme; there is no sensible
    /// default that works on both a light and a dark ground, so the
    /// caller always decides.
    property color color: "#000000"

    implicitWidth: BinGlyphData.viewWidth
    implicitHeight: BinGlyphData.viewHeight

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        smooth: true
        // The path is authored in the SVG's own coordinate space; scale
        // it to whatever size the item was given rather than making
        // every caller do the arithmetic.
        transform: Scale {
            xScale: root.width / BinGlyphData.viewWidth
            yScale: root.height / BinGlyphData.viewHeight
        }

        ShapePath {
            strokeColor: "transparent"
            fillColor: root.color
            fillRule: ShapePath.OddEvenFill
            PathSvg { path: BinGlyphData.path }
        }
    }
}
