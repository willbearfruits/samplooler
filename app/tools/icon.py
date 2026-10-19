#!/usr/bin/env python3
"""Draws the app's icon (src-tauri/icons): a cassette's two reels and the tape between them, in the
Samplooler's yellow on black, drawn at 32 px and scaled up square. Needs Pillow:
  uv run --with pillow tools/icon.py"""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src-tauri', 'icons')
N = 32
Y = (242, 201, 76, 255)
K = (6, 6, 6, 255)

im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rounded_rectangle([1, 1, 30, 30], radius=4, fill=K)
d.rectangle([4, 4, 27, 6], fill=Y)                        # the yellow strip of a two-colour screen
for cx in (10, 21):
    d.ellipse([cx - 5, 10, cx + 5, 20], outline=Y, width=2)
    d.rectangle([cx - 1, 14, cx + 1, 16], fill=Y)
d.line([5, 25, 26, 25], fill=Y, width=2)                  # the tape
d.line([5, 20, 5, 25], fill=Y, width=1)
d.line([26, 20, 26, 25], fill=Y, width=1)

os.makedirs(OUT, exist_ok=True)
for s, name in ((32, '32x32.png'), (128, '128x128.png'), (256, '128x128@2x.png'), (512, 'icon.png')):
    im.resize((s, s), Image.NEAREST).save(os.path.join(OUT, name))
im.resize((256, 256), Image.NEAREST).save(os.path.join(OUT, 'icon.ico'),
                                          sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
im.resize((1024, 1024), Image.NEAREST).save(os.path.join(OUT, 'icon.icns'))
print('icons written to', os.path.normpath(OUT))
