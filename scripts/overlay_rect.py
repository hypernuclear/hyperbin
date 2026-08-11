import re, subprocess, sys
from PIL import Image, ImageDraw
out = subprocess.run(["./dockprobe"], capture_output=True, text=True).stdout
line = [l for l in out.splitlines() if "TRASH" in l][0]
axx, axy, axw, axh = [int(v) for v in
    re.search(r"rect=\((-?\d+),(-?\d+) (\d+)x(\d+)\)", line).groups()]
pad = 22
cx, cy, cw, ch = axx - pad, axy - pad, axw + 2*pad, axh + 2*pad
subprocess.run(["screencapture","-o","-x",f"-R{cx},{cy},{cw},{ch}","/tmp/ov.png"],check=True)
minD, majD = min(axw, axh), max(axw, axh)
fh = round(0.7857 * minD); fw = round(fh * 98.0/112.0)
ccx, ccy = axx + axw/2, axy + axh/2
shift = round(0.125 * majD)
if axw >= axh: ccx -= shift
else:          ccy += shift
fx, fy = ccx - fw/2, ccy - fh/2
print(f"AX      ({axx},{axy}) {axw}x{axh}")
print(f"FORMULA ({fx},{fy}) {fw}x{fh}")
im = Image.open("/tmp/ov.png").convert("RGBA")
S = im.width / cw            # capture px per point
d = Image.new("RGBA", im.size, (0,0,0,0)); dr = ImageDraw.Draw(d)
dr.rectangle([(axx-cx)*S, (axy-cy)*S, (axx-cx+axw)*S, (axy-cy+axh)*S],
             outline=(0,120,255,255), width=1)          # blue = AX rect
dr.rectangle([(fx-cx)*S, (fy-cy)*S, (fx-cx+fw)*S, (fy-cy+fh)*S],
             outline=(255,0,0,255), width=1)            # red  = our mask rect
im.alpha_composite(d)
im.resize((im.width*6, im.height*6), Image.NEAREST).save("/tmp/ovbig.png")
print("blue = AX rect, red = mask rect")
