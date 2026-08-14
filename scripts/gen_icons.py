"""Generate every platform's app icon from resources/app_icon.png.

Produces:
  resources/hyperbin.icns          macOS bundle icon
  resources/hyperbin.ico           Windows executable + installer icon
  packaging/windows/Assets/*.png   MSIX tiles, at the scales the Store wants

The master is a 1024px RGBA square whose alpha already carries the
squircle — see `mask_master()` for how it was cut from the supplied
artwork. Everything here is a downscale of that one file, so the shape
and the art stay in step across all three platforms.

The menu-bar icon is NOT generated here. That one is drawn at runtime
from resources/hyperbin.svg by TrayMenu::trayIcon(), because a menu-bar
icon has to be a flat monochrome mask that the shell recolours — a
detailed illustration would come out as a dark smudge.

Run from the repo root:
    python3 scripts/gen_icons.py
"""
import os
import struct
import subprocess
import tempfile
import shutil

MASTER = 'resources/app_icon.png'

# The brand edge, matching the splash's.
#
# Held as a FRACTION of the side, not a pixel count: the splash's 5px sits
# on a 640-wide window, and the same 5px on a 1024px master would be a
# quarter of the weight. 0.0078 is that 5/640 carried across.
BORDER_FRACTION = 5 / 640
BORDER_COLOR = (0x72, 0xf9, 0x87, 255)


def load():
    from PIL import Image
    im = Image.open(MASTER).convert('RGBA')
    if im.width != im.height:
        raise SystemExit(f'{MASTER} must be square, got {im.size}')
    return im


def png(im, size, out):
    from PIL import Image
    im.resize((size, size), Image.LANCZOS).save(out, optimize=True)
    return out


def side_of(w, h):
    """The dimension the border fraction is measured against."""
    return max(w, h)


def mask_master(source, out=MASTER, target=1024):
    """Cut the rounded square out of a flat-backed render and square it up.

    The shape is measured and then drawn, rather than flood-filled from
    the corners.

    Flood-filling was the first approach and it fails on dark artwork:
    this illustration is a night-time alley, half of it within a few
    levels of the near-black surround, so the fill escapes through the
    border and eats the shadows. It produced a master that was 43%
    transparent with holes punched clean through the middle of the
    picture, and it looked fine in a thumbnail.

    Measuring instead is deterministic and cannot touch the artwork: the
    edges give the bounding box, the width of the topmost row gives the
    corner radius, and a least-squares fit over the corner profile
    settles the radius to about a pixel. The corner is a plain circular
    arc — checked against a superellipse, which does not fit at all.
    """
    from PIL import Image, ImageDraw
    import numpy as np

    src = Image.open(source).convert('RGB')
    a = np.asarray(src).astype(int)
    h, w, _ = a.shape

    # The surround, sampled just inside the corner. Anything meaningfully
    # different from it is artwork.
    bg = a[2, 2]
    solid = np.abs(a - bg).sum(2) > 40
    rows = [y for y in range(h) if solid[y].sum() > 6]
    cols = [x for x in range(w) if solid[:, x].sum() > 6]
    if not rows or not cols:
        raise SystemExit(f'{source}: could not find the artwork against its background')
    x0, x1, y0, y1 = cols[0], cols[-1], rows[0], rows[-1]
    box_w, box_h = x1 - x0 + 1, y1 - y0 + 1

    # Fit the corner radius against the measured left edge.
    obs = [(dy, int(np.nonzero(solid[y0 + dy])[0].min()) - x0)
           for dy in range(4, min(240, box_h // 4), 4)]
    def err(r):
        return sum((( r - (r * r - (r - dy) ** 2) ** 0.5 if dy < r else 0.0) - m) ** 2
                   for dy, m in obs)
    radius = min((r / 10.0 for r in range(int(box_w * 0.5), int(box_w * 3.5))),
                 key=err)

    # Drawn at 4x and downsampled: PIL's rounded_rectangle has no
    # antialiasing, and a hard edge shimmers badly once this is scaled to
    # 32px — which every output below is.
    ss = 4
    mask = Image.new('L', (box_w * ss, box_h * ss), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, box_w * ss - 1, box_h * ss - 1],
        radius=radius * ss, fill=255)
    mask = mask.resize((box_w, box_h), Image.LANCZOS)

    im = src.convert('RGBA').crop((x0, y0, x1 + 1, y1 + 1))
    im.putalpha(mask)

    # The brand edge, traced along the same rounded rectangle.
    #
    # Inset by half its width so the stroke lands wholly inside the
    # shape: a stroke is centred on its path, so drawing it on the outline
    # itself would put half of it where the alpha mask has already faded
    # to nothing, and the border would come out at half weight and
    # ragged. Supersampled for the same reason the mask is.
    bw = max(1.0, BORDER_FRACTION * side_of(box_w, box_h))
    ring = Image.new('RGBA', (box_w * ss, box_h * ss), (0, 0, 0, 0))
    ImageDraw.Draw(ring).rounded_rectangle(
        [bw * ss / 2, bw * ss / 2,
         box_w * ss - 1 - bw * ss / 2, box_h * ss - 1 - bw * ss / 2],
        radius=max(0.0, (radius - bw / 2)) * ss,
        outline=BORDER_COLOR, width=max(1, round(bw * ss)))
    ring = ring.resize((box_w, box_h), Image.LANCZOS)
    im = Image.alpha_composite(im, ring)

    side = max(im.size)
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
    sq.resize((target, target), Image.LANCZOS).save(out, optimize=True)
    print(f'  shape {box_w}x{box_h} at ({x0},{y0}), corner radius {radius:.1f}px')


def make_icns(im, out):
    """macOS wants a .iconset directory, then iconutil makes the .icns."""
    work = tempfile.mkdtemp(suffix='.iconset')
    try:
        for base in (16, 32, 128, 256, 512):
            png(im, base, f'{work}/icon_{base}x{base}.png')
            png(im, base * 2, f'{work}/icon_{base}x{base}@2x.png')
        subprocess.run(['iconutil', '-c', 'icns', work, '-o', out], check=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def make_ico(im, out, sizes=(16, 24, 32, 48, 64, 128, 256)):
    """A .ico is a short header plus one PNG per size, written by hand.

    Nothing on this machine writes multi-size .ico files, and the format
    is a six-field header plus a sixteen-byte directory entry each — far
    less trouble than depending on ImageMagick being installed.
    """
    work = tempfile.mkdtemp()
    try:
        blobs = [open(png(im, s, f'{work}/{s}.png'), 'rb').read() for s in sizes]
        offset = 6 + 16 * len(sizes)
        with open(out, 'wb') as f:
            f.write(struct.pack('<HHH', 0, 1, len(sizes)))  # reserved, type, count
            for s, b in zip(sizes, blobs):
                # 256 is written as 0: the field is a single byte.
                f.write(struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0,
                                    1, 32, len(b), offset))
                offset += len(b)
            for b in blobs:
                f.write(b)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def make_msix(im, dest):
    os.makedirs(dest, exist_ok=True)
    # The unscaled name has to exist as well — the manifest names it
    # directly, and Windows picks a scale variant only if it is there.
    for base, name in ((150, 'Square150x150Logo'), (44, 'Square44x44Logo')):
        png(im, base, f'{dest}/{name}.png')
        for pct in (125, 150, 200, 400):
            png(im, round(base * pct / 100), f'{dest}/{name}.scale-{pct}.png')
    png(im, 50, f'{dest}/StoreLogo.png')


if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        # Rebuild the master from a freshly supplied render, then carry on.
        mask_master(sys.argv[1])
        print(f'rebuilt {MASTER} from {sys.argv[1]}')
    im = load()
    make_icns(im, 'resources/hyperbin.icns')
    make_ico(im, 'resources/hyperbin.ico')
    make_msix(im, 'packaging/windows/Assets')
    print('icons written')
