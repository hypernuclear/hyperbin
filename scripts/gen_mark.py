import re, sys

# Absolute-only commands; that is all the exported marks use, and a
# parser that quietly mishandles a relative one it was never given is
# worse than a parser that refuses it.
_CMDS = 'MLCHVZ'


def _subpaths(d):
    """Split a path's `d` into its subpaths, each still a valid `d`."""
    return [s.strip() for s in re.split(r'(?=M)', d) if s.strip()]


def _points(d):
    """Every coordinate a subpath mentions, control points included.

    A curve is inside the hull of its control points, so a box drawn
    around all of them contains the shape. That is looser than the real
    outline but it is only used to ask which subpath sits inside which,
    and glyph counters are nowhere near the edges of their letter.
    """
    toks = re.findall(r'[A-Za-z]|-?\d*\.?\d+(?:[eE]-?\d+)?', d)
    pts, cmd, i = [], None, 0
    while i < len(toks):
        t = toks[i]
        if t.isalpha():
            if t.upper() not in _CMDS:
                raise ValueError(f'unsupported path command {t!r}')
            cmd, i = t, i + 1
            continue
        n = {'M': 2, 'L': 2, 'C': 6, 'H': 1, 'V': 1}[cmd.upper()]
        vals = [float(v) for v in toks[i:i + n]]
        if cmd.upper() == 'H':
            pts.append((vals[0], None))
        elif cmd.upper() == 'V':
            pts.append((None, vals[0]))
        else:
            pts += list(zip(vals[0::2], vals[1::2]))
        i += n
    return pts


def _bbox(d):
    pts = _points(d)
    xs = [p[0] for p in pts if p[0] is not None]
    ys = [p[1] for p in pts if p[1] is not None]
    return min(xs), min(ys), max(xs), max(ys)


def _group(subs):
    """Group each outline with the holes that sit inside it.

    Qt's CurveRenderer drops geometry when a single ShapePath carries
    enough subpaths — the lockup's 16 came out as `hyperbi`, with the
    final `n` missing and no warning. One ShapePath per glyph keeps every
    subpath count small enough to survive, but the counters have to ride
    along with their letter: winding is what cuts the hole out, and a
    counter promoted to its own ShapePath is just a filled blob.
    """
    boxes = [_bbox(s) for s in subs]

    def inside(a, b):   # is box a strictly within box b
        return (boxes[b][0] <= boxes[a][0] and boxes[b][1] <= boxes[a][1]
                and boxes[b][2] >= boxes[a][2] and boxes[b][3] >= boxes[a][3]
                and a != b)

    def area(i):
        return (boxes[i][2] - boxes[i][0]) * (boxes[i][3] - boxes[i][1])

    owner = {}
    for i in range(len(subs)):
        # The tightest container wins, so a hole inside a hole attaches
        # to the shape it actually sits in.
        cands = [j for j in range(len(subs)) if inside(i, j)]
        if cands:
            owner[i] = min(cands, key=area)

    def root(i):
        while i in owner:
            i = owner[i]
        return i

    groups = {}
    for i in range(len(subs)):
        groups.setdefault(root(i), []).append(i)
    return [[subs[i] for i in sorted(g)] for _, g in sorted(groups.items())]


def _literal(d, indent):
    # Sliced at fixed offsets, NOT textwrap.wrap(). Path data is
    # whitespace-significant: rewrapping it on spaces silently welds
    # `-43.875 9.1875` into `-43.8759.1875` and the mark comes out wrong.
    chunks = [d[i:i + 66] for i in range(0, len(d), 66)]
    rest = '\n'.join(f'{indent}+ "{c}"' for c in chunks[1:])
    return f'"{chunks[0]}"' + ('\n' + rest if rest else '')


def gen(svg_text, component, doc):
    rule = re.search(r'fill-rule="(\w+)"', svg_text)
    fill_rule = ('ShapePath.OddEvenFill' if rule and rule.group(1) == 'evenodd'
                 else 'ShapePath.WindingFill')
    vb = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg_text)
    w, h = vb.group(1), vb.group(2)
    paths = re.findall(r'<path d="([^"]+)"', svg_text)
    groups = [g for p in paths for g in _group(_subpaths(p))]
    body = []
    for g in groups:
        body.append(
            '        ShapePath {\n'
            '            strokeColor: "transparent"\n'
            '            fillColor: root.color\n'
            f'            fillRule: {fill_rule}\n'
            '            PathSvg { path: '
            + _literal(' '.join(g), ' ' * 30) + ' }\n'
            '        }')
    return f'''// {doc}
//
// Generated from the SVG by scripts/gen_mark.py. A Shape
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
    svg = open(src).read()
    open(out, 'w').write(gen(svg, comp, doc))
    n_sub = sum(len(_subpaths(p)) for p in re.findall(r'<path d="([^"]+)"', svg))
    n_grp = sum(len(_group(_subpaths(p)))
                for p in re.findall(r'<path d="([^"]+)"', svg))
    print(f'{out}: {len(re.findall(r"<path", svg))} paths, '
          f'{n_sub} subpaths -> {n_grp} ShapePaths')
