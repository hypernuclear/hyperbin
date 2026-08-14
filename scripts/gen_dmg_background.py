"""Generate the DMG window's background.

Writes packaging/macos/dmg-background.tiff — a two-representation TIFF
carrying both @1x and @2x.

That format is the whole point. Finder draws a DMG background at 1:1 in
POINTS and does not scale it, so a plain @2x PNG renders at double size
and you see the top-left quarter of it. A plain @1x PNG is the right size
and soft on every Mac sold in the last decade. A TIFF holding both
representations is the only thing that is both.

Usage:
    python3 scripts/gen_dmg_background.py            # as shipped (no rule)
    python3 scripts/gen_dmg_background.py --inset    # ...with an inset rule
    python3 scripts/gen_dmg_background.py --png out.png --scale 1   # preview

The geometry below must agree with scripts/package-macos.sh: the window
is 660x400 and the two icons are centred at y=185.
"""
import argparse
import os
import subprocess
import tempfile

W, H = 660, 400            # must match --window-size in package-macos.sh
ICON_CY = 185              # must match the --icon / --app-drop-link y
# Ground and figure. Black rather than the brand's near-black ink: at
# this size ink reads as "dark grey that might be a mistake", where black
# reads as chosen. Swapping these two inverts the whole design.
BG = '#000000'
FG = '#72f987'             # brandGreen — lockup, caption and inset rule

# Caption baseline, in points from the top of the window.
CAPTION_Y = H * 0.885 - 15

LOCKUP = 'resources/hyperbin_lockup.svg'


def build_svg(scale, inset):
    """The background as SVG, at a given pixel scale."""
    art = open(LOCKUP).read()
    inner = art[art.index('>', art.index('<svg')) + 1:art.rindex('</svg>')]
    # The lockup's paths carry fill="white"; an inherited fill on a
    # wrapping group would not override that.
    inner = inner.replace('fill="white"', f'fill="{FG}"')
    vb = [float(v) for v in art.split('viewBox="')[1].split('"')[0].split()]

    w, h = W * scale, H * scale
    # The lockup sits above the icons, filling a third of the width. Its
    # baseline has to clear the top of a 110pt icon centred at y=185,
    # which starts at 130 — so everything here stays above ~110.
    lw = w * 0.30
    s = lw / vb[2]
    tx, ty = (w - lw) / 2, h * 0.105

    rule = ''
    if inset:
        # A hairline inset from the edge, echoing the green border the
        # splash draws over its own dark artwork. Set well in from the
        # edge rather than hugging it: at 10pt it read as a window
        # chrome artifact, close enough to the frame to look like part of
        # Finder rather than part of the design.
        m = 22 * scale
        rule = (f'<rect x="{m}" y="{m}" width="{w - 2*m}" height="{h - 2*m}" '
                f'rx="{6*scale}" ry="{6*scale}" fill="none" '
                f'stroke="{FG}" stroke-opacity="0.28" stroke-width="{1.5*scale}"/>')

    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}">'
            f'<rect width="{w}" height="{h}" fill="{BG}"/>'
            f'{rule}'
            f'<g transform="translate({tx},{ty}) scale({s})">{inner}</g>'
            f'<text x="{w/2}" y="{CAPTION_Y*scale}" font-family="Helvetica" '
            f'font-size="{13*scale}" fill="{FG}" fill-opacity="0.62" '
            f'text-anchor="middle">Drag hyperbin into Applications</text>'
            f'</svg>')


def render(scale, inset, out):
    with tempfile.NamedTemporaryFile('w', suffix='.svg', delete=False) as f:
        f.write(build_svg(scale, inset))
        tmp = f.name
    try:
        subprocess.run(['rsvg-convert', '-w', str(W * scale), '-h', str(H * scale),
                        '-o', out, tmp], check=True)
    finally:
        os.unlink(tmp)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--inset', action='store_true',
                    help='draw a faint inset rule inside the edge')
    ap.add_argument('--png', help='write a single PNG here instead of the TIFF')
    ap.add_argument('--scale', type=int, default=1)
    a = ap.parse_args()

    if a.png:
        render(a.scale, a.inset, a.png)
        print(f'{a.png}: {W*a.scale}x{H*a.scale}')
        return

    work = tempfile.mkdtemp()
    one = render(1, a.inset, f'{work}/bg.png')
    two = render(2, a.inset, f'{work}/bg@2x.png')
    out = 'packaging/macos/dmg-background.tiff'
    # -cathidpicheck is what marks the second representation as the HiDPI
    # variant of the first; plain -cat would produce a two-page TIFF that
    # Finder shows the first page of, at the wrong size.
    subprocess.run(['tiffutil', '-cathidpicheck', one, two, '-out', out], check=True)
    print(f'{out}: {W}x{H} @1x + {W*2}x{H*2} @2x')


if __name__ == '__main__':
    main()
