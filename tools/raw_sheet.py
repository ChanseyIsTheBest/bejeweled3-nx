"""A contact sheet of a raw recording (.rgba + .txt: width height fps frames): every Nth frame, 4 a row.

  python -P tools/raw_sheet.py out/pc/captures/lock_xenos1.rgba out/renut_tests/lock_xenos1.jpg [--every 100]
"""
import argparse
import os
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument('raw')
ap.add_argument('out')
ap.add_argument('--every', type=int, default=100)
ap.add_argument('--scale', type=float, default=0.5)
a = ap.parse_args()
w, h, _, n = open(os.path.splitext(a.raw)[0] + '.txt').read().split()
w, h, n = int(w), int(h), int(n)
frames = list(range(0, n, a.every))
tw, th = int(w * a.scale), int(h * a.scale)
cols = 4
sheet = Image.new('RGB', (tw * cols, th * ((len(frames) + cols - 1) // cols)))
with open(a.raw, 'rb') as f:
    for i, k in enumerate(frames):
        f.seek(k * w * h * 4)
        im = Image.frombytes('RGBA', (w, h), f.read(w * h * 4)).convert('RGB').resize((tw, th))
        sheet.paste(im, ((i % cols) * tw, (i // cols) * th))
os.makedirs(os.path.dirname(a.out), exist_ok=True)
sheet.save(a.out, quality=85)
print(f'{len(frames)} frames -> {a.out}')
