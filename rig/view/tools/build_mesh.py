#!/usr/bin/env python3
"""Build view/assets/stack.mesh: the rev C Stack (tag checkpoint/boards-rev-c) for slrig-view.

    python3 rig/view/tools/build_mesh.py                  # export from git + KiCad, about a minute
    python3 rig/view/tools/build_mesh.py --wrl DIR        # reuse DIR/top.wrl and DIR/bottom.wrl

What it does:

1. `git archive checkpoint/boards-rev-c hardware` of the public repo into a temporary directory, then
   `kicad-cli pcb export vrml` of stack-top and stack-bottom (origin at the board's top-left corner).
2. Reads the VRML (Transform/Group/Shape/IndexedFaceSet with DEF/USE, as lab/tools/showcase/vrml.py).
3. Puts the boards together: the bottom board, and the top board 12.6 mm above it (11 mm gap + 1.6 mm PCB),
   hex standoffs at the four holes. Then moves everything into the device axes of rig/PROTOCOL.md:
   x to the screen's right, y towards the screen's bottom edge (the player), z out of the screen, in mm,
   origin at the stack's centre (the board outline's centre, half way between the two boards).
4. Merges the triangles by material (the solder mask keeps its transparency: the viewer draws it over the
   copper and the core as KiCad does), takes the OLED's active area (material colour 0.05 0.25 0.45) out of the mesh and records its corners: the
   viewer draws the live screen there.
5. Welds equal vertices per material and writes the file below.

File format, little-endian:

    char[8]  magic "SLMESH1\\0"
    u32      version (1)
    u32      group count G
    f32      position scale s (mm per unit: a stored int16 times s is mm)
    f32[3]   bounds min (mm, device axes)
    f32[3]   bounds max
    f32[12]  the screen's active area: four corners (x y z, mm, device axes) in the order
             top-left pixel corner, top-right, bottom-right, bottom-left. Pixel (0, 0) of the 128 x 64
             framebuffer is at the first corner; x runs to the second, y to the fourth.
    f32[3]   the screen's outward normal
    G times:
      f32[4] colour r g b a (a < 1: drawn after the opaque groups, blended: the solder mask)
      u32    flags (1 = board solder mask, 2 = silkscreen, 4 = copper, 8 = added part)
      u32    vertex count V
      u32    index count I (a multiple of 3)
      i16[3V] positions (times s = mm)
      u32[I]  triangle indices into this group's vertices
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
VIEW = os.path.dirname(HERE)
RIG = os.path.dirname(VIEW)
REPO = os.path.expanduser('~/Projects/samplooler/samplooler')
OUT = os.path.join(VIEW, 'assets', 'stack.mesh')
TAG = 'checkpoint/boards-rev-c'

W, H = 94.0, 46.0                      # board outline, mm (lab/hardware/gen/stack.py)
HOLES = [(4.0, 4.0), (90.0, 4.0), (4.0, 42.0), (90.0, 26.0)]
T = 1.6                                # PCB
GAP = 11.0                             # socket 8.5 + header plastic 2.5
TOP_Z = GAP + T                        # the top board's mid plane above the bottom board's
SCREEN = (0.05, 0.25, 0.45)            # the OLED's active area (lab/tools/showcase/common.py)
MASK = (0.078, 0.2, 0.141)             # KiCad's solder mask (drawn with its transparency)
SILK = (0.961, 0.961, 0.961)
COPPER = (0.75, 0.61, 0.23)
CORE = (0.0, 0.0, 0.0)
STEEL = (0.62, 0.63, 0.65)
SCALE = 1.0 / 256.0                    # int16 positions: ±128 mm in 1/256 mm steps

# ------------------------------------------------------------------------------------------- VRML

TOKEN = re.compile(r'"[^"]*"|[{}\[\],]|[^\s{}\[\],"]+')


def tokens(text):
    text = re.sub(r'#[^\n]*', '', text)
    return [t for t in TOKEN.findall(text) if t != ',']


class Parser:
    def __init__(self, toks):
        self.t, self.i, self.defs = toks, 0, {}

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def next(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def node(self):
        tok = self.next()
        if tok == 'USE':
            return self.defs[self.next()]
        name = None
        if tok == 'DEF':
            name, tok = self.next(), self.next()
        assert self.next() == '{', (tok, self.i)
        fields = {}
        while self.peek() != '}':
            key = self.next()
            fields[key] = self.value()
        self.next()
        n = {'type': tok, **fields}
        if name:
            self.defs[name] = n
        return n

    def value(self):
        tok = self.peek()
        if tok == '[':
            self.next()
            if self.peek() in ('DEF', 'USE') or (self.peek() and self.peek()[0].isupper()
                                                  and self.i + 1 < len(self.t) and self.t[self.i + 1] == '{'):
                items = []
                while self.peek() != ']':
                    items.append(self.node())
                self.next()
                return items
            start = self.i
            while self.t[self.i] != ']':
                self.i += 1
            nums = self.t[start:self.i]
            self.next()
            return np.array(nums, dtype=float) if nums else np.zeros(0)
        if tok in ('DEF', 'USE') or (self.i + 1 < len(self.t) and self.t[self.i + 1] == '{'):
            return self.node()
        vals = []
        while True:
            tok = self.peek()
            if tok is None or tok in ('}', ']'):
                break
            if re.match(r'^[-+]?(\d|\.\d)', tok) or tok in ('TRUE', 'FALSE') or tok.startswith('"'):
                vals.append(self.next())
            else:
                break
        return vals


def rot_matrix(axis_angle):
    x, y, z, a = axis_angle
    n = np.linalg.norm([x, y, z]) or 1.0
    x, y, z = x / n, y / n, z / n
    c, s, C = np.cos(a), np.sin(a), 1 - np.cos(a)
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def fl(v, n, default):
    if v is None or len(v) < n:
        return default
    return [float(x) for x in v[:n]]


def walk(node, M, out):
    typ = node['type']
    if typ == 'Transform':
        t = fl(node.get('translation'), 3, [0, 0, 0])
        r = fl(node.get('rotation'), 4, [0, 0, 1, 0])
        s = fl(node.get('scale'), 3, [1, 1, 1])
        c = fl(node.get('center'), 3, [0, 0, 0])
        L = np.eye(4)
        L[:3, :3] = rot_matrix(r) @ np.diag(s)
        Tc, Tci, Tt = np.eye(4), np.eye(4), np.eye(4)
        Tc[:3, 3] = c
        Tci[:3, 3] = [-v for v in c]
        Tt[:3, 3] = t
        M = M @ Tt @ Tc @ L @ Tci
        for ch in node.get('children', []):
            walk(ch, M, out)
    elif typ == 'Group':
        for ch in node.get('children', []):
            walk(ch, M, out)
    elif typ == 'Shape':
        app = node.get('appearance') or {}
        mat = app.get('material') or {}
        col = tuple(round(v, 3) for v in fl(mat.get('diffuseColor'), 3, [0.8, 0.8, 0.8]))
        tr = round(fl(mat.get('transparency'), 1, [0.0])[0], 2)
        geo = node.get('geometry')
        if not geo or geo['type'] != 'IndexedFaceSet':
            return
        pts = geo['coord']['point'].reshape(-1, 3)
        idx = geo['coordIndex'].astype(int)
        tris, face = [], []
        for k in idx:
            if k < 0:
                tris += [(face[0], face[j], face[j + 1]) for j in range(1, len(face) - 1)]
                face = []
            else:
                face.append(k)
        tris += [(face[0], face[j], face[j + 1]) for j in range(1, len(face) - 1)]
        if not tris:
            return
        P = np.c_[pts, np.ones(len(pts))] @ M.T
        tri = np.array(tris)
        if geo.get('ccw', ['TRUE'])[0] == 'FALSE':
            tri = tri[:, ::-1]
        out.setdefault((col, tr), []).append(P[:, :3][tri])


def load_wrl(path):
    p = Parser(tokens(open(path).read()))
    out = {}
    while p.peek() is not None:
        if p.peek() in ('DEF', 'USE') or (p.i + 1 < len(p.t) and p.t[p.i + 1] == '{'):
            walk(p.node(), np.eye(4), out)
        else:
            p.next()
    return {k: np.concatenate(v) for k, v in out.items()}

# ------------------------------------------------------------------------------------------- export


def export(work):
    """rev C from git, then KiCad's VRML of both boards into `work`."""
    src = os.path.join(work, 'src')
    os.makedirs(src)
    arch = subprocess.run(['git', '-C', REPO, 'archive', TAG, 'hardware'], check=True, capture_output=True).stdout
    subprocess.run(['tar', '-x', '-C', src], input=arch, check=True)
    for side in ('top', 'bottom'):
        pcb = os.path.join(src, 'hardware', f'stack-{side}', f'samplooler-stack-{side}.kicad_pcb')
        out = os.path.join(work, f'{side}.wrl')
        t0 = time.time()
        subprocess.run(['kicad-cli', 'pcb', 'export', 'vrml', '-f', '--no-dnp', '--units', 'mm',
                        '--user-origin', '50x50mm', '-o', out, pcb], check=True, capture_output=True)
        print(f'  exported {side} in {time.time() - t0:.0f} s ({os.path.getsize(out) // 1024} KB)')
    return work

# ------------------------------------------------------------------------------------------- the stack


def hex_prism(cx, cy, z0, z1, across=5.5, hole=3.2):
    """A hex standoff (board coordinates, z up), triangles (N, 3, 3): six sides and two hex rings."""
    r = across / 2 / np.cos(np.pi / 6)
    a = np.pi / 6 + np.arange(6) * np.pi / 3
    ring = np.c_[cx + r * np.cos(a), cy + r * np.sin(a)]
    tris = []
    for i in range(6):
        j = (i + 1) % 6
        p0, p1 = ring[i], ring[j]
        q = [(*p0, z0), (*p1, z0), (*p1, z1), (*p0, z1)]
        tris += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
    for z in (z0, z1):
        c = (cx, cy, z)
        tris += [(c, (*ring[i], z), (*ring[(i + 1) % 6], z)) for i in range(6)]
    return np.array(tris, dtype=float)


def board_to_device(tris, z_off):
    """VRML (x right, y up = toward the nose, z up out of the front) to device axes."""
    out = np.empty_like(tris)
    out[..., 0] = tris[..., 0] - W / 2
    out[..., 1] = -tris[..., 1] - H / 2
    out[..., 2] = tris[..., 2] + z_off - TOP_Z / 2
    # y is mirrored, so the stored winding is mirrored too; the viewer mirrors y back when it draws
    return out


def weld(tris):
    q = np.round(tris.reshape(-1, 3) / SCALE).astype(np.int64)
    if np.abs(q).max() >= 32767:
        raise SystemExit('a vertex is out of the int16 range')
    uniq, inv = np.unique(q, axis=0, return_inverse=True)
    idx = inv.reshape(-1, 3).astype(np.uint32)
    keep = (idx[:, 0] != idx[:, 1]) & (idx[:, 1] != idx[:, 2]) & (idx[:, 0] != idx[:, 2])
    return uniq.astype(np.int16), idx[keep]


def build(wrl_dir):
    groups = {}

    def add(key, tris):
        if len(tris):
            groups.setdefault(key, []).append(tris)

    screen = None
    for side, z in (('bottom', 0.0), ('top', TOP_Z)):
        mats = load_wrl(os.path.join(wrl_dir, f'{side}.wrl'))
        print(f'  {side}: {len(mats)} materials, {sum(len(v) for v in mats.values())} triangles')
        for (col, tr), tris in mats.items():
            if col == SCREEN:
                p = tris.reshape(-1, 3)
                screen = (p[:, 0].min(), p[:, 0].max(), -p[:, 1].max(), -p[:, 1].min(), p[:, 2].max() + z)
                continue
            add((col, tr), board_to_device(tris, z))
    if screen is None:
        raise SystemExit('no screen (material 0.05 0.25 0.45) in the top board')
    # standoffs between the boards
    st = np.concatenate([hex_prism(x, -y, T / 2, TOP_Z - T / 2) for x, y in HOLES])
    add((STEEL, 0.0), board_to_device(st, 0.0))

    x0, x1, y0, y1, zs = screen
    print(f'  screen (board mm): x {x0:.2f}..{x1:.2f}  y {y0:.2f}..{y1:.2f}  z {zs:.2f}'
          f'  = {x1 - x0:.2f} x {y1 - y0:.2f} mm')
    dev = lambda bx, by: (bx - W / 2, by - H / 2, zs - TOP_Z / 2)  # noqa: E731
    # the module's header is at the nose (y = 0 side) and the firmware sets segment remap + COM scan
    # reversed (oled.c 0xA1 0xC8), the usual upright image: pixel (0, 0) at the nose-left corner
    corners = [dev(x0, y0), dev(x1, y0), dev(x1, y1), dev(x0, y1)]
    return groups, corners


def flags_for(col, tr):
    f = 0
    if col == MASK or tr > 0:
        f |= 1
    if col == SILK:
        f |= 2
    if col == COPPER:
        f |= 4
    if col == STEEL:
        f |= 8
    return f


def write(groups, corners, path):
    allp = np.concatenate([np.concatenate(v).reshape(-1, 3) for v in groups.values()])
    lo, hi = allp.min(0), allp.max(0)
    body = bytearray()
    ntri = nvert = 0
    order = sorted(groups.items(), key=lambda kv: (kv[0][1] > 0, -sum(len(t) for t in kv[1])))
    for (col, tr), parts in order:
        v, idx = weld(np.concatenate(parts))
        body += struct.pack('<4f3I', *col, 1.0 - tr, flags_for(col, tr), len(v), idx.size)
        body += v.astype('<i2').tobytes()
        body += idx.astype('<u4').tobytes()
        ntri += len(idx)
        nvert += len(v)
    head = b'SLMESH1\0' + struct.pack('<2If', 1, len(groups), SCALE)
    head += struct.pack('<6f', *lo, *hi)
    head += struct.pack('<12f', *[c for p in corners for c in p])
    head += struct.pack('<3f', 0.0, 0.0, 1.0)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(head + bytes(body))
    print(f'  {path}: {len(groups)} groups, {nvert} vertices, {ntri} triangles, '
          f'{os.path.getsize(path) / 1e6:.2f} MB')
    print(f'  bounds {np.round(lo, 2)} .. {np.round(hi, 2)} mm')
    print(f'  screen corners (TL TR BR BL): {[tuple(round(float(c), 2) for c in p) for p in corners]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--wrl', help='a directory with top.wrl and bottom.wrl (skips the export)')
    ap.add_argument('--out', default=OUT)
    a = ap.parse_args()
    t0 = time.time()
    tmp = None
    try:
        if a.wrl:
            wrl = a.wrl
        else:
            for tool in ('git', 'kicad-cli', 'tar'):
                if not shutil.which(tool):
                    raise SystemExit(f'{tool} is not on PATH')
            tmp = tempfile.mkdtemp(prefix='slrig-mesh-')
            print(f'exporting {TAG} into {tmp}')
            wrl = export(tmp)
        groups, corners = build(wrl)
        write(groups, corners, a.out)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    print(f'done in {time.time() - t0:.1f} s')


if __name__ == '__main__':
    sys.exit(main())
