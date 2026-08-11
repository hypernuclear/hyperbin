"""Locate the Dock's drawn trash icon inside a screen capture, by
template-matching the system artwork at a range of scales.

Gives the icon's true visual rect, which is what the overlay needs — the
Accessibility rect is the item's hit area, not the artwork's bounds.
"""
import sys
import numpy as np
from PIL import Image

# argv: capX capY axX axY axW axH  (global points)
_a = [float(v) for v in sys.argv[1:7]]
CAP_ORIGIN = (_a[0], _a[1])
AX = (_a[2], _a[3], _a[4], _a[5])
CAP_SCALE = 2.0

cap = Image.open("/tmp/dockregion.png").convert("RGB")
_req_w = AX[2] + 60
if cap.size[0] / CAP_SCALE < _req_w - 0.5:
    CAP_ORIGIN = (CAP_ORIGIN[0] + (_req_w - cap.size[0] / CAP_SCALE), CAP_ORIGIN[1])
    print(f"capture clipped at display edge; origin corrected to {CAP_ORIGIN}")

capg = np.asarray(cap.convert("L"), dtype=np.float64)

tpl_full = Image.open("/tmp/icon_NSTrashFull.png").convert("RGBA")
a = np.asarray(tpl_full)[:, :, 3]
ys, xs = np.nonzero(a > 8)
tpl_full = tpl_full.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
print(f"artwork cropped to alpha bounds: {tpl_full.size}")

best = None
ASPECT = tpl_full.size[0] / tpl_full.size[1]      # artwork is taller than wide
for h_px in range(20, 90):                        # capture pixels
    w_px = max(1, int(round(h_px * ASPECT)))
    t = tpl_full.resize((w_px, h_px), Image.LANCZOS)
    ta = np.asarray(t)[:, :, 3].astype(np.float64) / 255.0
    tg = np.asarray(t.convert("L"), dtype=np.float64)
    if ta.sum() < 30:
        continue
    th, tw = tg.shape
    if th >= capg.shape[0] or tw >= capg.shape[1]:
        break
    for oy in range(0, capg.shape[0] - th):
        for ox in range(0, capg.shape[1] - tw):
            win = capg[oy:oy + th, ox:ox + tw]
            # Alpha-weighted zero-mean normalised cross-correlation, so
            # transparent parts of the artwork don't drag the score.
            w = ta
            wm = w.sum()
            wv = win - (win * w).sum() / wm
            tv = tg - (tg * w).sum() / wm
            num = (w * wv * tv).sum()
            den = np.sqrt((w * wv * wv).sum() * (w * tv * tv).sum()) + 1e-9
            score = num / den
            if best is None or score > best[0]:
                best = (score, ox, oy, w_px, h_px)

score, ox, oy, tw_px, th_px = best
print(f"best match: score={score:.3f} at capture px ({ox},{oy}) size {tw_px}x{th_px}px")

# Back to global points.
gx = CAP_ORIGIN[0] + ox / CAP_SCALE
gy = CAP_ORIGIN[1] + oy / CAP_SCALE
gw = tw_px / CAP_SCALE
gh = th_px / CAP_SCALE
print(f"icon visual rect (global pt): ({gx:.1f}, {gy:.1f}) {gw:.1f}x{gh:.1f}")

print(f"AX rect:                      {AX}")
print()
print("relationship (what the overlay needs):")
print(f"  width  = AX.w * {gw / AX[2]:.4f}")
print(f"  height = AX.h * {gh / AX[3]:.4f}   (= AX.w * {gh / AX[2]:.4f})")
print(f"  offset x from AX centre = {(gx + gw/2) - (AX[0] + AX[2]/2):+.2f} pt"
      f"  (= AX.w * {((gx + gw/2) - (AX[0] + AX[2]/2)) / AX[2]:+.4f})")
print(f"  offset y from AX centre = {(gy + gh/2) - (AX[1] + AX[3]/2):+.2f} pt"
      f"  (= AX.h * {((gy + gh/2) - (AX[1] + AX[3]/2)) / AX[3]:+.4f})")
