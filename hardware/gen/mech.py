"""Mechanical model: what occupies the space above and below each board.

Every placed part contributes solids (axis-aligned boxes with a z range) built from its
mechanical spec, in part-local coordinates:
    body features   ('name', (x0, y0, x1, y1), z0, z1)   z measured away from the surface it sits on
    tail            pin tails on the far side of the board, at every through-hole pad
    trim            True when those tails are header pins you can cut flush (then 1.0 mm)
Z is global: for a single board the front surface is z = 0 and the back is z = -1.6; in a
stack the lower board's front is 0 and the upper board sits from GAP to GAP + 1.6.

check() reports hard collisions and the tails that must be trimmed for things to fit.
"""
import math
import pcbnew
from geometry import at, box_at, overlap, dist_box_point, box_in_circle

PCB_T = 1.6
TRIMMED = 1.0
_pad_cache = {}


def tht_pads(lib_path, name):
    """Local positions of a footprint's plated through-hole pads (cached)."""
    key = (lib_path, name)
    if key not in _pad_cache:
        fp = pcbnew.FootprintLoad(lib_path, name)
        pads = []
        for p in fp.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                q = p.GetFPRelativePosition()
                pads.append((pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)))
        _pad_cache[key] = pads
    return _pad_cache[key]


class Solid:
    def __init__(self, owner, name, box, z0, z1, kind='body', circle=None, flags=''):
        self.owner, self.name, self.box, self.z0, self.z1, self.kind = owner, name, box, min(z0, z1), max(z0, z1), kind
        self.circle, self.flags = circle, flags

    def __repr__(self):
        return f'{self.owner}:{self.name} z {self.z0:.1f}..{self.z1:.1f}'


def solids_for(board, part):
    """part: dict(ref, origin, rot, side, spec, pads) -> list of Solid (global z)."""
    out = []
    zf, zb = board['z_front'], board['z_front'] - PCB_T
    side, spec = part['side'], part['spec']
    ref = board['name'] + '/' + part['ref']
    for feat in spec.get('features', ()):
        name, shape, z0, z1 = feat[:4]
        flags = feat[4] if len(feat) > 4 else ''
        circle = None
        if shape[0] == 'circle':                   # ('circle', cx, cy, r) in part-local mm
            _, cx, cy, r = shape
            c = at(part['origin'], (cx, cy), part['rot'], side)
            circle = (c[0], c[1], r)
            b = (c[0] - r, c[1] - r, c[0] + r, c[1] + r)
        else:
            b = box_at(part['origin'], shape, part['rot'], side)
        za, zz = (zf + z0, zf + z1) if side == 'front' else (zb - z0, zb - z1)
        out.append(Solid(ref, name, b, za, zz, circle=circle, flags=flags))
    for name, shape, depth in spec.get('below', ()):
        if shape[0] == 'circle':
            _, cx, cy, r = shape
            c = at(part['origin'], (cx, cy), part['rot'], side)
            circle, b = (c[0], c[1], r), (c[0] - r, c[1] - r, c[0] + r, c[1] + r)
        else:
            circle, b = None, box_at(part['origin'], shape, part['rot'], side)
        za, zz = (zb - depth, zb) if side == 'front' else (zf, zf + depth)
        out.append(Solid(ref, name, b, za, zz, circle=circle))
    tail = spec.get('tail', 0.0)
    if tail > 0:
        for p in part['pads']:
            c = at(part['origin'], p, part['rot'], side)
            b = (c[0] - 0.5, c[1] - 0.5, c[0] + 0.5, c[1] + 0.5)
            if side == 'front':
                out.append(Solid(ref, 'tail', b, zb - tail, zb, 'tail'))
            else:
                out.append(Solid(ref, 'tail', b, zf, zf + tail, 'tail'))
    return out


def check(boards, gap=None, standoff_r=3.3, allowed=()):
    """boards: list of dict(name, z_front, outline (x0,y0,x1,y1), cutouts [(x, y, d)], holes [(x, y)],
    parts [part], trim {ref: bool}, sweeps [(ref, centre, radius, z0, z1)]). Features flagged 'edge'
    may overhang the outline (USB-C, jack nose, the amp). Returns (errors, trims)."""
    errors, trims = [], set()
    solids = []
    for bd in boards:
        for part in bd['parts']:
            for s in solids_for(bd, part):
                s.board = bd
                solids.append(s)
    allowed = {tuple(sorted(a)) for a in allowed}

    def same_part(a, b):
        return a.owner == b.owner

    def pair_ok(a, b):
        return tuple(sorted((a.owner, b.owner))) in allowed

    # 1. solid against solid: z ranges must not overlap where boxes overlap
    for i, a in enumerate(solids):
        for b in solids[i + 1:]:
            if same_part(a, b) or pair_ok(a, b):
                continue
            if a.kind == 'tail' and b.kind == 'tail':
                continue
            if not overlap(a.box, b.box):
                continue
            if a.z1 <= b.z0 + 1e-6 or b.z1 <= a.z0 + 1e-6:
                continue
            # a tail that would fit once trimmed is an instruction, not a collision
            t, o = (a, b) if a.kind == 'tail' else (b, a) if b.kind == 'tail' else (None, None)
            if t is not None:
                spec_trim = t.board['trim'].get(t.owner.split('/', 1)[1], False)
                surf = t.z0 if t.z0 >= t.board['z_front'] - 1e-6 else t.z1
                trimmed = (surf, surf + TRIMMED) if t.z0 >= t.board['z_front'] - 1e-6 else (surf - TRIMMED, surf)
                if spec_trim and (trimmed[1] <= o.z0 + 1e-6 or o.z1 <= trimmed[0] + 1e-6):
                    trims.add(t.owner)
                    continue
            errors.append(f'{a} collides with {b}')

    # 2. anything crossing another board's plane must go through a cut-out (or miss the board)
    for s in solids:
        for bd in boards:
            if bd is s.board:
                continue
            zb0, zb1 = bd['z_front'] - PCB_T, bd['z_front']
            if s.z1 <= zb0 + 1e-6 or s.z0 >= zb1 - 1e-6:
                continue
            ob = bd['outline']
            if not overlap(s.box, ob):
                continue
            def inside(cx, cy, d):
                if s.circle:
                    return math.hypot(s.circle[0] - cx, s.circle[1] - cy) + s.circle[2] <= d / 2 - 0.5
                return box_in_circle(s.box, (cx, cy), d / 2 - 0.5)
            if not any(inside(cx, cy, d) for cx, cy, d in bd['cutouts']):
                errors.append(f'{s} passes through {bd["name"]} outside any opening')

    # 3. sweeping caps/knobs above an upper board: nothing tall within their reach
    for bd in boards:
        for ref, c, r, z0, z1 in bd.get('sweeps', ()):
            for s in solids:
                if s.owner.endswith('/' + ref) or s.kind == 'tail':
                    continue
                if s.z1 > z0 and s.z0 < z1 and dist_box_point(s.box, c) < r:
                    errors.append(f'{ref} sweep (r {r} mm) reaches {s}')

    # 4. standoffs: keep solids between the boards (or under the feet) away from them
    for bd in boards:
        for hx, hy in bd['holes']:
            for s in solids:
                if s.board is not bd:
                    continue
                if dist_box_point(s.box, (hx, hy)) < standoff_r and not s.owner.endswith(('/H1', '/H2', '/H3', '/H4')):
                    errors.append(f'{s} touches the standoff at {hx:.1f},{hy:.1f}')

    # 5. board edges: bodies stay on the board unless the part is meant to overhang
    for s in solids:
        bd = s.board
        ref = s.owner.split('/', 1)[1]
        if s.kind == 'tail' or 'edge' in s.flags:
            continue
        x0, y0, x1, y1 = bd['outline']
        b = s.box
        if b[0] < x0 - 0.01 or b[1] < y0 - 0.01 or b[2] > x1 + 0.01 or b[3] > y1 + 0.01:
            errors.append(f'{s} leaves the board outline')
    return errors, sorted(trims)


def assembly_order(board):
    """On a two-sided board, a module hides the solder joints of anything whose pins come
    through under it from the other side: those parts must be soldered first. Returns
    (order, errors); an error means two modules hide each other's joints (no possible order)."""
    raised = {}
    for part in board['parts']:
        feats = [f for f in part['spec'].get('features', ()) if f[0] == 'body' and f[2] >= 2.0]
        if feats:
            shape = feats[0][1]
            if shape[0] == 'circle':
                _, cx, cy, r = shape
                c = at(part['origin'], (cx, cy), part['rot'], part['side'])
                raised[part['ref']] = (part['side'], (c[0] - r, c[1] - r, c[0] + r, c[1] + r))
            else:
                raised[part['ref']] = (part['side'], box_at(part['origin'], shape, part['rot'], part['side']))
    before = {p['ref']: set() for p in board['parts']}      # ref -> parts that must go on before it
    for part in board['parts']:
        for pad in part['pads']:
            c = at(part['origin'], pad, part['rot'], part['side'])
            for q, (qside, qbox) in raised.items():
                if q != part['ref'] and qside != part['side'] and qbox[0] < c[0] < qbox[2] and qbox[1] < c[1] < qbox[3]:
                    before[q].add(part['ref'])
    order, done, errors = [], set(), []
    pending = {r for r, s in before.items() if s or r in raised}
    while pending:
        ready = sorted(r for r in pending if before[r] <= done | (set(before) - pending))
        if not ready:
            errors.append('solder joints hidden in a loop: ' + ', '.join(sorted(pending)))
            break
        for r in ready:
            order.append(r)
            done.add(r)
            pending.discard(r)
    return order, {q: sorted(s) for q, s in before.items() if s}, errors
