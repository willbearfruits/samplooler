#!/usr/bin/env python3
"""Raw 1-bit frames from the simulator (128x64, row-major, LSB = leftmost) to an animated GIF, scaled 3x
and tinted like the two-colour panel (header band yellow, the rest blue), 25 fps."""
import sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
W, H, K = 128, 64, 3
data = open(src, 'rb').read()
fb = W * H // 8
frames = []
pal = [6, 10, 22, 90, 170, 255, 22, 18, 8, 255, 200, 40, 3, 5, 11, 54, 102, 153, 11, 9, 4, 153, 120, 24] + [0] * (768 - 24)
for i in range(len(data) // fb):
    f = data[i * fb:(i + 1) * fb]
    im = Image.new('P', (W * K, H * K))
    im.putpalette(pal)
    px = im.load()
    for y in range(H):
        for x in range(W):
            on = (f[(y * W + x) >> 3] >> (x & 7)) & 1
            c = (3 if on else 2) if y < 16 else (1 if on else 0)
            for dy in range(K):
                for dx in range(K):
                    px[x * K + dx, y * K + dy] = c if (dx < K - 1 and dy < K - 1) else c + 4
    frames.append(im)
if frames:
    frames[0].save(dst, save_all=True, append_images=frames[1:], duration=40, loop=0, optimize=False, disposal=1)
    print(dst, len(frames), 'frames')
