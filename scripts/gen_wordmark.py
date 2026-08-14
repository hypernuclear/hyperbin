import re, sys, textwrap
def gen(svg_text, component, doc):
    vb = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg_text)
    w, h = vb.group(1), vb.group(2)
    paths = re.findall(r'<path d="([^"]+)"', svg_text)
    body = []
    for p in paths:
        # Sliced at fixed offsets, NOT textwrap.wrap(). Path data is
        wrapped = [p[i:i + 66] for i in range(0, len(p), 66)]
        lit = '\n'.join('                  + "%s"' % c for c in wrapped[1:])
        first = '"%s"' % wrapped[0]
        body.append(
            '        ShapePath {\n'
            '            strokeColor: "transparent"\n'
            '            fillColor: root.color\n'
            '            fillRule: ShapePath.WindingFill\n'
            f'            PathSvg {{ path: {first}\n{lit} }}\n'
            '        }')
    return f'''// {doc}
//
// Generated from the supplied SVG by scripts/gen_wordmark.py. A Shape
// rather than an image so the colour is a property: this sits on a busy
// illustration in one place and could sit on anything later, and a
// baked-in black would be invisible on half of them.
import QtQuick
import QtQuick.Shapes
Item {{
    id: root
    /// Fill colour. No default that works on both a light and a dark
    /// ground, so the caller always decides.
    property color color: "#ffffff"
    readonly property real viewWidth: {w}
    readonly property real viewHeight: {h}
    implicitWidth: viewWidth
    implicitHeight: viewHeight
    Shape {{
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        smooth: true
        // Authored in the SVG's own coordinate space; scaled to whatever
        // size the item was given rather than making every caller do the
        // arithmetic.
        transform: Scale {{
            xScale: root.width / root.viewWidth
            yScale: root.height / root.viewHeight
        }}
{chr(10).join(body)}
    }}
}}
'''
if __name__ == '__main__':
    src, out, comp, doc = sys.argv[1:5]
    open(out, 'w').write(gen(open(src).read(), comp, doc))
    print(f'{out}: {len(re.findall(r"<path", open(src).read()))} paths')
