import re, subprocess
from PIL import Image, ImageDraw
out = subprocess.run(["./dockprobe"], capture_output=True, text=True).stdout
line = [l for l in out.splitlines() if "TRASH" in l][0]
axx, axy, axw, axh = [int(v) for v in
    re.search(r"rect=\((-?\d+),(-?\d+) (\d+)x(\d+)\)", line).groups()]
pad = 26
cx, cy, cw, ch = axx - pad, axy - pad, axw + 2*pad, axh + 2*pad
subprocess.run(["screencapture","-o","-x",f"-R{cx},{cy},{cw},{ch}","/tmp/ruler.png"],check=True)
im = Image.open("/tmp/ruler.png").convert("RGB")
S = im.width / cw
Z = 8
big = im.resize((im.width*Z, im.height*Z), Image.NEAREST).convert("RGBA")
ov = Image.new("RGBA", big.size, (0,0,0,0)); dr = ImageDraw.Draw(ov)
for gx in range(cx, cx+cw+1, 4):
    X = (gx-cx)*S*Z
    dr.line([X,0,X,big.height], fill=(255,0,0,110), width=1)
    dr.text((X+2, 4), str(gx), fill=(255,0,0,255))
for gy in range(cy, cy+ch+1, 4):
    Y = (gy-cy)*S*Z
    dr.line([0,Y,big.width,Y], fill=(0,90,255,90), width=1)
    dr.text((3, Y+2), str(gy), fill=(0,90,255,255))
big.alpha_composite(ov); big.save("/tmp/rulerbig.png")
print(f"AX rect ({axx},{axy}) {axw}x{axh}; grid every 4pt, red=x blue=y")
