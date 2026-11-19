"""Extract 1-bit frames from a video: one byte per pixel (0 black / 1 white).

usage: python3 -I extract.py video width height step out.raw
  step 1 keeps every frame (30 fps source), step 2 every other frame (15 fps).
"""
import subprocess, sys
import numpy as np

src, w, h, step, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
cmd = ["ffmpeg", "-v", "error", "-i", src, "-vf", f"scale={w}:{h}:flags=area,format=gray",
       "-f", "rawvideo", "-pix_fmt", "gray", "-"]
p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
fsz = w * h
i = kept = 0
with open(out, "wb") as f:
    while True:
        buf = p.stdout.read(fsz)
        if len(buf) < fsz:
            break
        if i % step == 0:
            f.write((np.frombuffer(buf, np.uint8) >= 128).astype(np.uint8).tobytes())
            kept += 1
        i += 1
p.wait()
print(f"{kept} frames {w}x{h} -> {out}")
