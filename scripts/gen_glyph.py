"""Generate BinGlyph.h (path constant) and BinGlyph.qml from the SVG.

The path lives in exactly one place — a C++ constant exposed to QML as a
singleton — because it is 7.6KB of coordinates and two hand-maintained
copies would silently diverge the first time the artwork is tweaked.
"""
import re
import textwrap

svg = open('resources/hyperbin.svg').read()
d = re.search(r'<path d="([^"]+)"', svg).group(1)
vb = re.search(r'viewBox="0 0 (\d+) (\d+)"', svg)
w, h = vb.group(1), vb.group(2)
rule = re.search(r'fill-rule="(\w+)"', svg)
fill_rule = ('ShapePath.OddEvenFill' if rule and rule.group(1) == 'evenodd'
             else 'ShapePath.WindingFill')

# Wrap into C++ string literal chunks so the source stays readable.
chunks = textwrap.wrap(d, 88, break_long_words=True, break_on_hyphens=False)
lit = '\n'.join('    "%s"' % c.replace('\\', '\\\\').replace('"', '\\"') for c in chunks)

INC = '#include'
header = f'''// The bin glyph, as one SVG path.
//
// Single source of truth for both consumers: the QML component draws it
// with Shape/PathSvg so its colour can follow the theme, and the tray
// builds a QIcon from the same string. It is 7.6KB of coordinates —
// two hand-maintained copies would diverge the first time the artwork
// is touched.
#pragma once

{INC} <QObject>
{INC} <QQmlEngine>
{INC} <QString>

namespace hyperbin {{

class BinGlyphData : public QObject
{{
    Q_OBJECT
    // Named so it does not collide with BinGlyph.qml, which registers
    // the same type name from the same module.
    QML_NAMED_ELEMENT(BinGlyphData)
    QML_SINGLETON
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(qreal viewWidth READ viewWidth CONSTANT)
    Q_PROPERTY(qreal viewHeight READ viewHeight CONSTANT)

public:
    using QObject::QObject;

    static QString pathData();
    QString path() const {{ return pathData(); }}
    qreal viewWidth() const {{ return {w}.0; }}
    qreal viewHeight() const {{ return {h}.0; }}
}};

}} // namespace hyperbin
'''

impl = f'''{INC} "BinGlyph.h"

namespace hyperbin {{

QString BinGlyphData::pathData()
{{
    static const QString kPath = QStringLiteral(
{lit});
    return kPath;
}}

}} // namespace hyperbin
'''

qml = f'''// Generated from resources/hyperbin.svg — see scripts/gen_glyph.py.
import QtQuick
import QtQuick.Shapes
import hyperbin

// The bin glyph as a shape rather than a bitmap, so its colour is a
// property instead of being baked into the asset. Used for the menu-bar
// item and available to any in-app UI that needs the mark.
Item {{
    id: root

    /// Fill colour. Callers bind this to the theme; there is no sensible
    /// default that works on both a light and a dark ground, so the
    /// caller always decides.
    property color color: "#000000"

    implicitWidth: BinGlyphData.viewWidth
    implicitHeight: BinGlyphData.viewHeight

    Shape {{
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        smooth: true
        // The path is authored in the SVG's own coordinate space; scale
        // it to whatever size the item was given rather than making
        // every caller do the arithmetic.
        transform: Scale {{
            xScale: root.width / BinGlyphData.viewWidth
            yScale: root.height / BinGlyphData.viewHeight
        }}

        ShapePath {{
            strokeColor: "transparent"
            fillColor: root.color
            fillRule: {fill_rule}
            PathSvg {{ path: BinGlyphData.path }}
        }}
    }}
}}
'''

open('src/app/BinGlyph.h', 'w').write(header)
open('src/app/BinGlyph.cpp', 'w').write(impl)
open('qml/BinGlyph.qml', 'w').write(qml)
print('wrote src/app/BinGlyph.{h,cpp} and qml/BinGlyph.qml')
