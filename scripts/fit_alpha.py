"""Locate the Dock's trash by fitting the artwork's ALPHA SILHOUETTE to
the screen, maximising IoU against a "differs from the Dock panel" mask.

Binary shape overlap, not greyscale correlation: the bin's interior tone
varies with the wallpaper showing through, which is what made earlier
template matching lock onto the wrong scale.
"""
import re
import subprocess
import sys

import numpy as np
from PIL import Image

out = subprocess.run(["./dockprobe"], capture_output=True, text=True).stdout
line = [l for l in out.splitlines() if "TRASH" in l][0]
axx, axy, axw, axh = [int(v) for v in
    re.search(r"rect=\((-?\d+),(-?\d+) (\d+)x(\d+)\)", line).groups()]

pad = 16
cx, cy, cw, ch = axx - pad, axy - pad, axw + 2*pad, axh + 2*pad
subprocess.run(["screencapture", "-o", "-x", f"-R{cx},{cy},{cw},{ch}",
                "/tmp/fit.png"], check=True)

im = Image.open("/tmp/fit.png").convert("RGB")
a = np.asarray(im, float)
S = im.width / cw          # capture px per point
H, W, _ = a.shape

# Panel colour: sample a vertical strip at the far left of the capture,
# which for a left-hand Dock is panel, never icon.
panel = np.median(a[:, 0:max(2, int(3*S))].reshape(-1, 3), axis=0)
target = (np.abs(a - panel).sum(2) > 55)

tpl = Image.open("/tmp/icon_NSTrashFull.png").convert("RGBA")
ta = np.asarray(tpl)[:, :, 3]
ys, xs = np.nonzero(ta > 8)
tpl = tpl.crop((xs.min(), ys.min(), xs.max()+1, ys.max()+1))
ASPECT = tpl.size[0] / tpl.size[1]

best = None
for hpx in range(int(0.4*axh*S), int(1.6*max(axw, axh)*S)):
    wpx = max(1, int(round(hpx * ASPECT)))
    if hpx >= H or wpx >= W:
        break
    t = np.asarray(tpl.resize((wpx, hpx), Image.LANCZOS))[:, :, 3] > 100
    tsum = t.sum()
    if tsum < 40:
        continue
    for oy in range(0, H - hpx):
        for ox in range(0, W - wpx):
            win = target[oy:oy+hpx, ox:ox+wpx]
            inter = np.logical_and(win, t).sum()
            union = tsum + win.sum() - inter
            iou = inter / union if union else 0
            if best is None or iou > best[0]:
                best = (iou, ox, oy, wpx, hpx)

iou, ox, oy, wpx, hpx = best
gx, gy = cx + ox/S, cy + oy/S
gw, gh = wpx/S, hpx/S
print(f"IoU {iou:.3f}")
print(f"AX      ({axx},{axy}) {axw}x{axh}")
print(f"BIN fit ({gx:.1f},{gy:.1f}) {gw:.1f}x{gh:.1f}")

minD, majD = min(axw, axh), max(axw, axh)
print()
print(f"  h / minDim              = {gh/minD:.4f}   (code: 0.80)")
print(f"  near-edge inset / majDim= {(gx-axx)/majD:.4f}   (code: 0.128)")
cross = (gy + gh/2) - (axy + axh/2)
print(f"  cross-axis centre error = {cross:+.2f} pt  ({cross/minD:+.4f} * minDim)")
