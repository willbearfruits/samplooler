"""Minimal VRML 2.0 reader for KiCad's board export: Transform/Group/Shape/IndexedFaceSet with
DEF/USE. Returns triangles grouped by material, in world coordinates (numpy), no bpy needed."""
import re
import numpy as np

TOKEN = re.compile(r'"[^"]*"|[{}\[\],]|[^\s{}\[\],"]+')


def tokens(text):
    text = re.sub(r'#[^\n]*', '', text)
    return [t for t in TOKEN.findall(text) if t != ',']


class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0
        self.defs = {}

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
            name = self.next()
            tok = self.next()
        typ = tok
        assert self.next() == '{', (typ, self.i)
        fields = {}
        while self.peek() != '}':
            key = self.next()
            fields[key] = self.value()
        self.next()
        n = {'type': typ, **fields}
        if name:
            self.defs[name] = n
        return n

    def value(self):
        tok = self.peek()
        if tok == '[':
            self.next()
            items = []
            if self.peek() in ('DEF', 'USE') or (self.peek() and self.peek()[0].isupper() and
                                                  self.i + 1 < len(self.t) and self.t[self.i + 1] == '{'):
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
    """Collect (material key, world triangles) from a node tree. M: 4x4 world matrix."""
    typ = node['type']
    if typ == 'Transform':
        T = np.eye(4)
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
        spec = tuple(round(v, 3) for v in fl(mat.get('specularColor'), 3, [0, 0, 0]))
        shin = round(fl(mat.get('shininess'), 1, [0.2])[0], 3)
        geo = node.get('geometry')
        if not geo or geo['type'] != 'IndexedFaceSet':
            return
        pts = geo['coord']['point'].reshape(-1, 3)
        idx = geo['coordIndex'].astype(int)
        tris = []
        face = []
        for k in idx:
            if k < 0:
                for j in range(1, len(face) - 1):
                    tris.append((face[0], face[j], face[j + 1]))
                face = []
            else:
                face.append(k)
        if len(face) > 2:
            for j in range(1, len(face) - 1):
                tris.append((face[0], face[j], face[j + 1]))
        if not tris:
            return
        P = np.c_[pts, np.ones(len(pts))] @ M.T
        ccw = geo.get('ccw', ['TRUE'])[0] != 'FALSE'
        tri = np.array(tris)
        if not ccw:
            tri = tri[:, ::-1]
        out.setdefault((col, tr, spec, shin), []).append(P[:, :3][tri])


def load(path, scale=1.0):
    """{(color, transparency, specular, shininess): (N, 3, 3) triangles}, times `scale`."""
    p = Parser(tokens(open(path).read()))
    out = {}
    while p.peek() is not None:
        tok = p.peek()
        if tok in ('DEF', 'USE') or (p.i + 1 < len(p.t) and p.t[p.i + 1] == '{'):
            walk(p.node(), np.eye(4), out)
        else:
            p.next()
    return {k: np.concatenate(v) * scale for k, v in out.items()}


if __name__ == '__main__':
    import sys
    import time
    t0 = time.time()
    m = load(sys.argv[1])
    print(f'{time.time() - t0:.1f} s, {len(m)} materials, {sum(len(v) for v in m.values())} triangles')
    allp = np.concatenate([v.reshape(-1, 3) for v in m.values()])
    print('bounds', allp.min(0).round(2), allp.max(0).round(2))
    for k, v in sorted(m.items(), key=lambda kv: -len(kv[1]))[:12]:
        print(k, len(v))
