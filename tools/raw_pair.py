"""Side by side frames of two raw recordings at the same index (A | B | diff x4), for comparing renderers by hand.

  python -P tools/raw_pair.py A.rgba B.rgba out.png 300 [310 ...]      (the same index in both)
  python -P tools/raw_pair.py A.rgba B.rgba out.png 800:1749 [...]     (A's frame 800 against B's 1749)
"""
import os
import sys
from PIL import Image, ImageChops


def frame(path, i):
    w, h, _, n = open(os.path.splitext(path)[0] + '.txt').read().split()
    w, h = int(w), int(h)
    with open(path, 'rb') as f:
        f.seek(i * w * h * 4)
        return Image.frombytes('RGBA', (w, h), f.read(w * h * 4)).convert('RGB')


a, b, out = sys.argv[1:4]
pairs = [tuple(int(v) for v in x.split(':')) if ':' in x else (int(x), int(x)) for x in sys.argv[4:]]
rows = []
for i, j in pairs:
    fa, fb = frame(a, i), frame(b, j)
    d = ImageChops.difference(fa, fb).point(lambda v: min(255, v * 4))
    row = Image.new('RGB', (fa.width * 3, fa.height))
    row.paste(fa, (0, 0)); row.paste(fb, (fa.width, 0)); row.paste(d, (fa.width * 2, 0))
    rows.append(row)
sheet = Image.new('RGB', (rows[0].width, rows[0].height * len(rows)))
for k, r in enumerate(rows):
    sheet.paste(r, (0, k * r.height))
sheet.save(out)
print(out)
