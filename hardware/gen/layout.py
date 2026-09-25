"""Placement, mechanical checks and PCB construction for both boards.

Board frame: origin at the top-left corner of the outline, +X right, +Y down (toward the
player). The top edge is the "nose": USB-C, the line-out jack and the microphone face it,
and the MPU-6050's +X axis points at it, so tilting that edge down plays forward (firmware
pitch = atan2(-ax, ...), TILT_SIGN +1). Both boards share the outline and the four M3 holes.

Stack heights (mm, from the bottom board's top surface): GAP = 11 to the top board's
underside. Module heights include the 2.5 mm header plastic they sit on.
"""
import math
import pcbnew
import modules as M
from pcb import Board, MM, V, items

W, H = 90.0, 64.0
CORNER_R = 3.0
HOLES = [(4.0, 4.0), (W - 4.0, 4.0), (4.0, H - 4.0), (W - 4.0, H - 4.0)]
STANDOFF_R = 3.3            # M3 hex standoff, 5.5 across flats, 6.35 across corners
GAP = 11.0
ORIGIN = (50.0, 50.0)       # where the board sits on the KiCad page

# ---------------------------------------------------------------- placement (board frame)
STICK = (22.0, 44.0)        # joystick stick centre: the top board's opening is centred here
STICK_HOLE_D = 27.0
ENC_SHAFT = (70.0, 46.0)
LINK_AT = (47.0, 44.0)      # J1 on both boards (socket below, header underneath the top board)


def rot(p, deg):
    """KiCad footprint rotation: local -> board offset (Y down, CCW positive)."""
    x, y = p
    c, s = round(math.cos(math.radians(deg)), 9), round(math.sin(math.radians(deg)), 9)
    return (x * c + y * s, -x * s + y * c)


def at(origin, local, deg):
    dx, dy = rot(local, deg)
    return (origin[0] + dx, origin[1] + dy)


def bbox(m, origin, deg):
    o = m['outline']
    if o[0] == 'rect':
        pts = [(o[1], o[2]), (o[3], o[2]), (o[3], o[4]), (o[1], o[4])]
    else:
        pts = [(o[1] - o[3], o[2] - o[3]), (o[1] + o[3], o[2] + o[3])]
    b = [at(origin, p, deg) for p in pts]
    xs, ys = [p[0] for p in b], [p[1] for p in b]
    return (min(xs), min(ys), max(xs), max(ys))


# ref: (module or footprint, origin, rotation)
_joy_o = (STICK[0] - rot(M.KY023['stick'], 270)[0], STICK[1] - rot(M.KY023['stick'], 270)[1])
BOTTOM_PLACE = {
    'U2': (M.PCM5102, (39.2, 15.35), 180),       # jack to the nose edge, left
    'U1': (M.SUPERMINI, (64.29, 2.09), 0),       # USB-C to the nose edge, right
    'U3': (M.INMP441, (51.3, 9.0), 0),           # between them, sound hole up
    'U5': (M.KY023, _joy_o, 270),                # header toward the MCU, like the kit case
    'U4': (M.GY521, (63.5, 41.3), 90),           # chip +X to the nose
    'J2': (M.MAX98357, (86.5, 12.0), 270),       # amp hangs off the right edge
}
BOTTOM_SMALL = {  # ref: (x, y, rot)
    'J1': (LINK_AT[0], LINK_AT[1], 0),
    'JP1': (32.725, 20.4, 0), 'JP3': (27.645, 20.4, 0), 'JP2': (30.185, 23.4, 0), 'JP4': (25.105, 23.4, 0),
    'C1': (42.7, 2.65, 0), 'C2': (50.6, 44.0, 0), 'C3': (51.3, 18.0, 270), 'C4': (63.5, 45.0, 270),
    'R1': (40.2, 44.6, 270), 'R2': (40.2, 49.4, 270),
    'H1': (*HOLES[0], 0), 'H2': (*HOLES[1], 0), 'H3': (*HOLES[2], 0), 'H4': (*HOLES[3], 0),
}
OLED_PIN1 = (41.19, 5.0)
TOP_PLACE = {'U1': (M.OLED, OLED_PIN1, 0)}
TOP_SMALL = {
    'J1': (LINK_AT[0], LINK_AT[1], 0, 'back'),
    'SW1': (ENC_SHAFT[0] - 7.5, ENC_SHAFT[1] - 2.5, 0),
    'JP1': (38.6, 35.4, 0), 'JP2': (45.4, 35.4, 0),
    'C1': (51.4, 35.4, 90), 'C2': (54.4, 35.4, 90),
    'H1': (*HOLES[0], 0), 'H2': (*HOLES[1], 0), 'H3': (*HOLES[2], 0), 'H4': (*HOLES[3], 0),
}
# reference labels of the small parts: offset from the part, board frame (None = hidden)
REF_AT = {
    'bottom': {'C1': (0, 1.7), 'C2': (0, 1.7), 'C3': (1.7, 0), 'C4': (1.7, 0), 'R1': (-1.7, 0), 'R2': (-1.7, 0),
               'JP1': None, 'JP2': None, 'JP3': None, 'JP4': None, 'J1': (-1.27, -2.6),
               'H1': None, 'H2': None, 'H3': None, 'H4': None},
    'top': {'C1': (0, 2.3), 'C2': (0, 2.3), 'JP1': None, 'JP2': None, 'J1': (-1.27, -2.6),
            'SW1': None, 'H1': None, 'H2': None, 'H3': None, 'H4': None},
}
BOOT_AT = at(BOTTOM_PLACE['U1'][1], (3.1, 7.0), 0)
RST_AT = at(BOTTOM_PLACE['U1'][1], (M.SM_ROW - 3.1, 7.0), 0)
ACCESS_D = 4.5

# ---------------------------------------------------------------- mechanical model
# Heights on the bottom board (top of the tallest part above the carrier surface).
HEIGHT = {'U1': 7.0, 'U2': 5.6, 'U3': 4.0, 'U4': 5.3, 'U5': 5.1, 'J2': 5.5}
DEPTH_HDR = 4.5      # untrimmed male-header tails under the top board
DEPTH_ENC = 3.0      # EC11 terminals and lugs
DEPTH_SCREW = 4.0    # M2 screw tail + nut under the top board (OLED corners)
MARGIN = 0.5


def _boxes_overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def mechanical_check():
    """Everything that can collide in the 11 mm gap, plus edges and standoffs."""
    problems = []
    bottom = []  # (name, box, height)
    for ref, (m, o, r) in BOTTOM_PLACE.items():
        bottom.append((ref, bbox(m, o, r), HEIGHT[ref]))
    jx0, jy0, jx1, jy1 = at(BOTTOM_PLACE['U2'][1], (21.5, 2.4), 180) + at(BOTTOM_PLACE['U2'][1], (27.5, 16.3), 180)
    bottom.append(('U2 jack', (min(jx0, jx1), min(jy0, jy1), max(jx0, jx1), max(jy0, jy1)), 9.6))
    sx, sy = STICK
    bottom.append(('J1 socket', (LINK_AT[0] - 3.9, LINK_AT[1] - 1.3, LINK_AT[0] + 1.3, LINK_AT[1] + 8.9), 8.5))
    for ref in ('C1', 'C2', 'C3', 'C4', 'R1', 'R2'):
        x, y, _ = BOTTOM_SMALL[ref]
        bottom.append((ref, (x - 1.3, y - 1.3, x + 1.3, y + 1.3), 1.3))

    top = []     # (name, box, depth below the top board)
    ox, oy = OLED_PIN1
    for i in range(4):
        top.append((f'OLED pin {i + 1}', (ox + 2.54 * i - 0.8, oy - 0.8, ox + 2.54 * i + 0.8, oy + 0.8), DEPTH_HDR))
    for (hx, hy, d) in M.OLED['holes']:
        x, y = ox + hx, oy + hy
        top.append((f'OLED screw {x:.0f},{y:.0f}', (x - 2.5, y - 2.5, x + 2.5, y + 2.5), DEPTH_SCREW))
    ex, ey = TOP_SMALL['SW1'][:2]
    for (px, py) in ((0, 0), (0, 2.5), (0, 5), (14.5, 0), (14.5, 5), (7.5, -3.1), (7.5, 8.1)):
        top.append((f'EC11 pin {px},{py}', (ex + px - 1.2, ey + py - 1.2, ex + px + 1.2, ey + py + 1.2), DEPTH_ENC))

    for tn, tb, depth in top:
        for bn, bb, h in bottom:
            if _boxes_overlap(tb, bb) and h + depth > GAP - MARGIN:
                problems.append(f'{tn} (down to {GAP - depth:.1f}) hits {bn} ({h:.1f} tall)')

    # joystick gimbal (16 x 16, 14.6 tall) must pass through the opening
    half = 8.0
    corner = math.hypot(half, half)
    if corner > STICK_HOLE_D / 2 - 1.0:
        problems.append(f'joystick gimbal corner {corner:.1f} too close to the opening edge')
    # the thumb cap sweeps about 12 mm around the stick just above the top board
    for tn, tb in (('OLED', bbox(M.OLED, OLED_PIN1, 0)),):
        cx = min(max(sx, tb[0]), tb[2]); cy = min(max(sy, tb[1]), tb[3])
        if math.hypot(cx - sx, cy - sy) < 15.0:
            problems.append(f'{tn} within 15 mm of the stick: the cap would hit it')

    # standoffs and edges
    allowed_out = {'J2'}     # the amp module is meant to overhang the right edge
    for bn, bb, h in bottom:
        for (hx, hy) in HOLES:
            cx = min(max(hx, bb[0]), bb[2]); cy = min(max(hy, bb[1]), bb[3])
            if math.hypot(cx - hx, cy - hy) < STANDOFF_R:
                problems.append(f'{bn} touches the standoff at {hx},{hy}')
        if bn not in allowed_out and bn != 'U2 jack':
            if bb[0] < 0.3 or bb[1] < 0.3 or bb[2] > W - 0.3 or bb[3] > H - 0.3:
                problems.append(f'{bn} leaves the board: {tuple(round(v, 2) for v in bb)}')
    for i, (an, ab, _) in enumerate(bottom):
        for bn, bb, _ in bottom[i + 1:]:
            if an.split()[0] == bn.split()[0]:
                continue
            if _boxes_overlap(ab, bb) and not (an.startswith(('C', 'R')) or bn.startswith(('C', 'R'))):
                problems.append(f'{an} overlaps {bn}')
    return problems


# ---------------------------------------------------------------- board construction

def _abs(p):
    return (ORIGIN[0] + p[0], ORIGIN[1] + p[1])


def build_board(path, projlib, parts, place, small, project, sheetfile, which, nets):
    bd = Board(path, projlib)
    bd.rules(clearance=0.2, track=0.25, power=(0.5, ['+5V', '+3V3', 'GND', '/OLED_P1', '/OLED_P2', '/DAC_3V3']))
    ds = bd.b.GetDesignSettings()
    for fld in ('m_TentViasFront', 'm_TentViasBack'):
        if hasattr(ds, fld):
            setattr(ds, fld, True)
    x0, y0 = ORIGIN
    bd.outline(x0, y0, x0 + W, y0 + H, CORNER_R)

    def fp_of(ref):
        lib, name = parts[ref]['fp'].split(':')
        return lib, name

    for ref, (m, o, r) in place.items():
        lib, name = fp_of(ref)
        bd.place(lib, name, ref, parts[ref]['value'], _abs(o), r, nets=nets[ref],
                 sym_uuid=_sym_uuid(project, ref), sheetfile=sheetfile, fields=parts[ref].get('fields'),
                 dnp=parts[ref].get('dnp', False))
    for ref, spec in small.items():
        x, y, r = spec[:3]
        back = len(spec) > 3 and spec[3] == 'back'
        lib, name = fp_of(ref)
        fp = bd.place(lib, name, ref, parts[ref]['value'], _abs((x, y)), r, back=back, nets=nets[ref],
                      sym_uuid=_sym_uuid(project, ref), sheetfile=sheetfile, fields=parts[ref].get('fields'),
                      dnp=parts[ref].get('dnp', False))
        if ref in REF_AT[which]:
            off = REF_AT[which][ref]
            t = fp.Reference()
            if off is None:
                t.SetVisible(False)
            else:
                t.SetPosition(V(*_abs((x + off[0], y + off[1]))))
                t.SetTextAngleDegrees(0)
                t.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
                t.SetTextThickness(MM(0.15))
    return bd


def _sym_uuid(project, ref):
    from schematic import uid
    return uid(project, 'sym', ref)


def keepout(bd, a, b, tracks=True, vias=True, pour=True, layers=('F.Cu', 'B.Cu'), name='keepout'):
    z = pcbnew.ZONE(bd.b)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowZoneFills(pour)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(bd.b.GetLayerID(l))
    z.SetLayerSet(ls)
    z.SetZoneName(name)
    ol = z.Outline()
    ol.NewOutline()
    (ax, ay), (bx, by) = _abs(a), _abs(b)
    for p in ((ax, ay), (bx, ay), (bx, by), (ax, by)):
        ol.Append(V(*p))
    bd.b.Add(z)


def stitch(bd, net='GND', pitch=4.0, edge=2.0, avoid=(), avoid_circles=()):
    """GND vias on a grid wherever they clear every pad, track, hole and keepout by 0.35 mm."""
    b = bd.b
    b.BuildConnectivity()
    obstacles = []   # (x, y, radius) in mm, rough but conservative
    for fp in b.Footprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            cx, cy = pcbnew.ToMM(bb.GetCenter().x), pcbnew.ToMM(bb.GetCenter().y)
            r = max(pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())) / 2
            obstacles.append((cx, cy, r))
    segs = []
    for t in items(b.Tracks()):
        s, e = t.GetStart(), t.GetEnd()
        segs.append((pcbnew.ToMM(s.x), pcbnew.ToMM(s.y), pcbnew.ToMM(e.x), pcbnew.ToMM(e.y), pcbnew.ToMM(t.GetWidth()) / 2))
    added = []
    x0, y0 = ORIGIN
    nx, ny = int((W - 2 * edge) / pitch), int((H - 2 * edge) / pitch)
    for i in range(nx + 1):
        for j in range(ny + 1):
            x = x0 + edge + i * (W - 2 * edge) / nx
            y = y0 + edge + j * (H - 2 * edge) / ny
            if any(math.hypot(x - hx - x0, y - hy - y0) < STANDOFF_R + 1.0 for hx, hy in HOLES):
                continue
            if any(a[0] <= x - x0 <= b[0] and a[1] <= y - y0 <= b[1] for a, b in avoid):
                continue
            if any(math.hypot(x - x0 - cx, y - y0 - cy) < r + 1.2 for cx, cy, r in avoid_circles):
                continue
            ok = all(math.hypot(x - ox, y - oy) > r + 0.3 + 0.35 for ox, oy, r in obstacles)
            if ok:
                for sx, sy, ex, ey, hw in segs:
                    dx, dy = ex - sx, ey - sy
                    L2 = dx * dx + dy * dy
                    t = 0 if L2 == 0 else max(0, min(1, ((x - sx) * dx + (y - sy) * dy) / L2))
                    if math.hypot(x - (sx + t * dx), y - (sy + t * dy)) < hw + 0.3 + 0.35:
                        ok = False
                        break
            if ok:
                bd.via(net, (x, y), 0.6, 0.3)
                added.append((x, y))
    return added


def prune_vias(bd, net='GND'):
    """Drop stitching vias that do not land in the filled pour on both layers."""
    b = bd.b
    removed = 0
    zones = [z for z in b.Zones() if not z.GetIsRuleArea() and z.GetNetname() == net]
    for t in items(b.Tracks()):
        if t.GetClass() != 'PCB_VIA' or t.GetNetname() != net:
            continue
        pos = t.GetPosition()
        layers_hit = set()
        for z in zones:
            for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                if z.IsOnLayer(layer) and z.HitTestFilledArea(layer, pos):
                    layers_hit.add(layer)
        if len(layers_hit) < 2:
            b.Remove(t)
            removed += 1
    return removed


# ---------------------------------------------------------------- hand-routed power (bottom)
# F.Cu runs east-west, B.Cu risers north-south, vias at the corners. Everything here is
# locked, so Freerouting routes the signals around it. Widths: 0.6 mm.
PW = 0.6
Y5 = 33.0          # +5V feed line
Y3 = 34.0          # +3V3 feed line (clear of the joystick's M3 hole at y 31.5)
X5 = 76.6          # +5V riser inside U1's footprint (the carrier under the module is free)
X3 = 77.8          # +3V3 riser beside it; the corridor between U1 and the amp stays free for signals
XD = 41.75         # +5V riser up to the DAC, between its header and the mic (= C1 pad 1)


def power_routes():
    u = BOTTOM_PLACE['U1'][1]
    p5 = (u[0] + M.SM_ROW, u[1])                  # U1 5V
    p3 = (u[0] + M.SM_ROW, u[1] + 2 * 2.54)       # U1 3V3
    dac_vin = at(BOTTOM_PLACE['U2'][1], (0, 5 * 2.54), 180)
    amp_vin = at(BOTTOM_PLACE['J2'][1], (6 * 2.54, 0), 270)
    mic = BOTTOM_PLACE['U3'][1]
    mic_vdd = (mic[0], mic[1] + 3.81)
    imu_vcc = BOTTOM_PLACE['U4'][1]
    joy = BOTTOM_PLACE['U5'][1]
    joy_3v3 = at(joy, (0, 2.54), 270)
    c1 = BOTTOM_SMALL['C1']; c3 = BOTTOM_SMALL['C3']; c4 = BOTTOM_SMALL['C4']; c2 = BOTTOM_SMALL['C2']
    c1_p1 = (c1[0] - 0.95, c1[1])
    c2_p1 = (c2[0] - 0.95, c2[1])
    c3_p1 = (c3[0], c3[1] - 0.95)
    c4_p1 = (c4[0], c4[1] - 0.95)
    j1_p1 = LINK_AT
    tracks = [  # (net, layer, points)
        ('+5V', 'B.Cu', [p5, (X5, p5[1] + (p5[0] - X5)), (X5, Y5)]),
        ('+5V', 'F.Cu', [(X5, amp_vin[1]), amp_vin]),
        ('+5V', 'F.Cu', [(X5, Y5), (XD, Y5)]),
        ('+5V', 'B.Cu', [(XD, Y5), (XD, c1_p1[1] + 1.65)]),
        ('+5V', 'F.Cu', [(XD, c1_p1[1] + 1.65), c1_p1, dac_vin]),
        ('+3V3', 'B.Cu', [p3, (X3, p3[1] + (p3[0] - X3)), (X3, Y3)]),
        ('+3V3', 'F.Cu', [(X3, Y3), (joy_3v3[0], Y3), joy_3v3]),
        ('+3V3', 'B.Cu', [(imu_vcc[0], Y3), imu_vcc]),
        ('+3V3', 'F.Cu', [imu_vcc, c4_p1]),
        ('+3V3', 'B.Cu', [(j1_p1[0], Y3), j1_p1]),
        ('+3V3', 'F.Cu', [j1_p1, c2_p1]),
        ('+3V3', 'B.Cu', [(mic_vdd[0], Y3), mic_vdd]),
        ('+3V3', 'F.Cu', [mic_vdd, c3_p1]),
    ]
    vias = [('+5V', (X5, Y5)), ('+5V', (X5, amp_vin[1])), ('+5V', (XD, Y5)), ('+5V', (XD, c1_p1[1] + 1.65)),
            ('+3V3', (X3, Y3)), ('+3V3', (imu_vcc[0], Y3)), ('+3V3', (j1_p1[0], Y3)), ('+3V3', (mic_vdd[0], Y3))]
    return tracks, vias


def add_power_routes(bd):
    tracks, vias = power_routes()
    layer = {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}
    for net, lay, pts in tracks:
        bd.track(net, [_abs(p) for p in pts], PW, layer[lay])
    for net, p in vias:
        bd.via(net, _abs(p), 0.8, 0.4)
    for t in items(bd.b.Tracks()):
        t.SetLocked(True)


def _unconnected(bd):
    bd.fill()
    bd.b.BuildConnectivity()
    c = bd.b.GetConnectivity()
    c.RecalculateRatsnest()
    return c.GetUnconnectedCount(False)


def bridge_islands(bd, net='GND', step=0.8, max_tries=400):
    """Add a via inside every pour island that is cut off, keeping only vias that help."""
    b = bd.b
    zones = {z.GetLayer(): z for z in b.Zones() if not z.GetIsRuleArea() and z.GetNetname() == net}
    u = _unconnected(bd)
    tries = 0
    added = []
    holes = []
    for fp in b.Footprints():
        for p in fp.Pads():
            if p.HasHole():
                q = p.GetPosition()
                holes.append((pcbnew.ToMM(q.x), pcbnew.ToMM(q.y), pcbnew.ToMM(max(p.GetDrillSize().x, p.GetDrillSize().y)) / 2))
    for t in items(b.Tracks()):
        if t.GetClass() == 'PCB_VIA':
            v = t.Cast()
            q = v.GetPosition()
            holes.append((pcbnew.ToMM(q.x), pcbnew.ToMM(q.y), pcbnew.ToMM(v.GetDrillValue()) / 2))
    while u > 0 and tries < max_tries:
        progress = False
        polys = {l: z.GetFilledPolysList(l) for l, z in zones.items()}
        islands = []
        for l, ps in polys.items():
            for i in range(ps.OutlineCount()):
                ol = ps.Outline(i)
                islands.append((abs(ol.Area()), l, i, ol.BBox()))
        islands.sort(key=lambda t: t[0])
        for area, l, i, bb in islands[:-2]:          # the two biggest are the main pours
            other = polys[pcbnew.B_Cu if l == pcbnew.F_Cu else pcbnew.F_Cu]
            x0, y0 = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
            x1, y1 = x0 + pcbnew.ToMM(bb.GetWidth()), y0 + pcbnew.ToMM(bb.GetHeight())
            cands = []
            y = y0 + step / 2
            while y < y1:
                x = x0 + step / 2
                while x < x1:
                    ring = [(x, y)] + [(x + 0.45 * math.cos(a), y + 0.45 * math.sin(a))
                                       for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2, math.pi / 4,
                                                 3 * math.pi / 4, 5 * math.pi / 4, 7 * math.pi / 4)]
                    if all(polys[l].Contains(V(*p), i) for p in ring) and \
                            all(other.Contains(V(*p)) for p in ring) and \
                            all(math.hypot(x - hx, y - hy) > hr + 0.15 + 0.5 for hx, hy, hr in holes):
                        cands.append((x, y))
                    x += step
                y += step
            for (x, y) in cands[:6]:
                tries += 1
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(V(x, y)); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3))
                v.SetNet(bd.net(net))
                b.Add(v)
                u2 = _unconnected(bd)
                if u2 < u:
                    u = u2
                    added.append((x, y))
                    holes.append((x, y, 0.15))
                    progress = True
                    break
                b.Remove(v)
            if progress:
                break
        if not progress:
            break
    bd.fill()
    return added, u
