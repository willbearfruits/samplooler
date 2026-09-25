#!/usr/bin/env python3
"""c3screen.py <in.pbm> <out.png>: a C3 (72x40) screen from the UI simulator as an emissive texture:
mono blue-white, 8x, with a faint pixel grid."""
import sys
import numpy as np
from PIL import Image

L = [l.strip() for l in open(sys.argv[1]) if l.strip()]
w, h = map(int, L[1].split())
px = np.array([[c == '1' for c in l] for l in L[2:2 + h]])
K = 8
rgb = np.where(px[..., None], np.array([200, 230, 255]), np.array([4, 8, 16])).astype(np.float32)
rgb = np.repeat(np.repeat(rgb, K, 0), K, 1)
rgb[K - 1::K] *= 0.55
rgb[:, K - 1::K] *= 0.55
Image.fromarray(rgb.clip(0, 255).astype(np.uint8)).save(sys.argv[2])
