#!/usr/bin/env python3
"""tour.sh's frames: each out/tour/*.pbm to a PNG, 6x, with a visible pixel grid, tinted like the
two-colour panel (16 yellow rows, then blue), on a dark bezel. Pure Python, no PIL needed."""
import glob, os, struct, sys, zlib

def png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def load(fn):
    L = [l.strip() for l in open(fn) if l.strip()]
    w, h = map(int, L[1].split())
    return w, h, [[c == '1' for c in l] for l in L[2:2 + h]]

src, dst = sys.argv[1], sys.argv[2]
K, PAD = 6, 24
BEZEL = (12, 12, 14)
for fn in sorted(glob.glob(src + '/*.pbm')):
    w, h, px = load(fn)
    W, H = w * K + 2 * PAD, h * K + 2 * PAD
    rows = [[*BEZEL] * W for _ in range(H)]
    for y in range(h):
        yellow = y < 16
        on_c = (255, 200, 40) if yellow else (90, 170, 255)
        off_c = (22, 18, 8) if yellow else (6, 10, 22)
        for x in range(w):
            c = on_c if px[y][x] else off_c
            grid = tuple(int(v * 0.55) for v in c)
            for dy in range(K):
                row = rows[PAD + y * K + dy]
                for dx in range(K):
                    cc = c if (dx < K - 1 and dy < K - 1) else grid
                    o = 3 * (PAD + x * K + dx)
                    row[o:o + 3] = cc
    png(os.path.join(dst, os.path.basename(fn)[:-4] + '.png'), W, H, rows)
