#!/usr/bin/env python3
"""oled_frames.py <work> <section> [scale]: the twin's screen (20 fps) as PNGs, one per video frame of a
section, tinted like the two-colour panel (yellow header rows, blue body) with a faint pixel grid."""
import json, os, sys
import numpy as np
from PIL import Image

W, SEC = sys.argv[1], sys.argv[2]
K = int(sys.argv[3]) if len(sys.argv) > 3 else 4
T = json.load(open(os.path.join(W, 'timeline.json')))
raw = np.fromfile(os.path.join(W, 'oled.raw'), dtype=np.uint8).reshape(-1, 64, 128)
sec = [s for s in T['sections'] if s['name'] == SEC][0]
out = os.path.join(W, 'oled-' + SEC)
os.makedirs(out, exist_ok=True)
top = np.zeros((64, 128), bool); top[:16] = True
lut_on = np.where(top[..., None], np.array([255, 200, 40]), np.array([90, 170, 255]))
lut_off = np.where(top[..., None], np.array([22, 18, 8]), np.array([6, 10, 22]))
grid = np.ones((64 * K, 128 * K, 1))
grid[K - 1::K, :, :] = 0.55
grid[:, K - 1::K, :] = 0.55
n = int(round(sec['dur'] * T['fps']))
for f in range(n):
    t = sec['start'] + f / T['fps']
    k = min(max(int(t * 20) - 1, 0), len(raw) - 1)
    px = raw[k].astype(bool)[..., None]
    rgb = np.where(px, lut_on, lut_off).astype(np.float32)
    rgb = np.repeat(np.repeat(rgb, K, 0), K, 1) * grid
    Image.fromarray(rgb.clip(0, 255).astype(np.uint8)).save(os.path.join(out, f'{f + 1:04d}.png'))
print(out, n, 'frames')
