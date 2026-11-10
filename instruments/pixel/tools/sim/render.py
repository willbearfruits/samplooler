#!/usr/bin/env python3
"""Turn the simulator's out/*.pbm frames into one PNG contact sheet, tinted like the two-colour panel."""
import zlib, struct, glob, sys
def png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
def load(fn):
    L = [l.strip() for l in open(fn) if l.strip()]; w, h = map(int, L[1].split()); return w, h, [[c == '1' for c in l] for l in L[2:2 + h]]
D = sys.argv[2] if len(sys.argv) > 2 else 'out'; TWO = len(sys.argv) <= 3 or sys.argv[3] != 'mono'
K = 3; files = sorted(glob.glob(D + '/*.pbm')); cols = 1 if len(files) == 1 else 2; rows_n = (len(files) + 1) // 2
W, H = load(files[0])[:2]; PAD = 12; sw = cols * (W * K + PAD) + PAD; sh = rows_n * (H * K + PAD) + PAD
img = [[(30, 30, 34)] * sw for _ in range(sh)]
for idx, fn in enumerate(files):
    w, h, px = load(fn); ox = PAD + (idx % cols) * (W * K + PAD); oy = PAD + (idx // cols) * (H * K + PAD)
    for y in range(h):
        for x in range(w):
            on = px[y][x]; yellow = TWO and y < 16
            c = ((255, 200, 40) if yellow else (90, 170, 255)) if on else ((22, 18, 8) if yellow else (6, 10, 22))
            for dy in range(K):
                for dx in range(K):
                    img[oy + y * K + dy][ox + x * K + dx] = c if (dx < K - 1 and dy < K - 1) else tuple(int(v * 0.6) for v in c)
out = sys.argv[1] if len(sys.argv) > 1 else 'out/sheet.png'
png(out, sw, sh, [[v for p in row for v in p] for row in img]); print(out)
