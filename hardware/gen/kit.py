"""Board-independent build steps: library, schematic, project, PCB, routing, pours, checks, fab.

A board is described by a spec dict (see stack.py and mini.py):
    key, project, dir, title, comments         naming; dir is relative to hardware/
    parts {ref: part}                          see parts.py
    sch {boxes, parts {ref: (x, y[, rot])}, flags}, notes
    size (W, H), corner, holes [(x, y)]        board frame: origin top-left, +Y toward the player
    place {ref: (x, y, rot, side)}             footprint origins in the board frame
    ref_at {ref: (dx, dy) | None}              reference label offsets for the small parts
    cutouts [(x, y, d)], keepouts [dict]       openings through the board, rule areas
    silk(bd, S)                                board-specific silkscreen
    power(bd) | None                           hand-routed, locked copper before autorouting
    stitch_avoid [((x0, y0), (x1, y1))], models {ref: wrl}
"""
import csv
import glob
import json
import math
import os
import shutil
import subprocess
import zipfile

import pcbnew
import modules as M
import footprints
import models
import mech
from geometry import at
from pcb import Board, MM, V, items, FREEROUTING, RULES, NETCLASSES, POWER_NETS
from schematic import Sheet, module_symbol, write_symbol_lib, uid

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
LIB = os.path.join(HW, 'lib')
FAB = os.path.join(HW, 'fab')
REV = 'B'
DATE = '2026-09-25'
ORIGIN = (50.0, 50.0)          # where every board sits on the KiCad page
SYM_LIBS = ('Device', 'power', 'Connector_Generic', 'Connector_Generic_MountingPin', 'Jumper', 'Mechanical', 'Switch')
FP_LIBS = ('Capacitor_SMD', 'Resistor_SMD', 'MountingHole', 'Jumper', 'Rotary_Encoder', 'Button_Switch_THT',
           'Connector_JST', 'Connector_PinHeader_2.54mm', 'Connector_PinSocket_2.54mm')


def A(p):
    return (ORIGIN[0] + p[0], ORIGIN[1] + p[1])


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    out = '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'assert' not in l and 'PROPERTY_ENUM' not in l)
    return r.returncode, out


def locked_projects(dirs):
    return [f for d in dirs for f in glob.glob(os.path.join(HW, d, '~*.lck'))]


# ============================================================== library

def symbol_columns(m):
    """Symbol pin layout for a module: (left pads, right pads, ref, name overrides)."""
    if m.get('breakout'):
        nums = [p[0] for p in m['pads']]
        return [n for n in nums if n % 2], [n for n in nums if not n % 2], 'J', None
    if 'sym' in m:
        return m['sym']
    nums = [p[0] for p in m['pads']]
    return nums, [], 'U', None


def build_library(extra=()):
    os.makedirs(os.path.join(LIB, 'samplooler.pretty'), exist_ok=True)
    os.makedirs(os.path.join(LIB, 'samplooler.3dshapes'), exist_ok=True)
    open(os.path.join(LIB, 'samplooler.3dshapes', 'EC11_Vertical_Knob.wrl'), 'w').write(models.ec11_knob(25.0).vrml())
    open(os.path.join(LIB, 'samplooler.3dshapes', 'EC11_Vertical_Knob_20.wrl'), 'w').write(models.ec11_knob(20.0).vrml())
    syms = {}
    for m in list(M.MODULES) + list(extra):
        if m['name'] in models.BUILDERS:
            open(os.path.join(LIB, 'samplooler.3dshapes', m['name'] + '.wrl'), 'w').write(
                models.BUILDERS[m['name']](M).vrml())
        footprints.write(os.path.join(LIB, 'samplooler.pretty', m['name'] + '.kicad_mod'),
                         footprints.module_footprint(m))
        left, right, ref, names = symbol_columns(m)
        pn = {n: nm for (n, x, y, nm) in m['pads']}
        pn.update(names or {})
        syms[m['name']] = module_symbol(
            m['name'], [(n, pn[n], 'passive') for n in left], [(n, pn[n], 'passive') for n in right],
            ref=ref, footprint='samplooler:' + m['name'], desc=m['desc'])
    write_symbol_lib(os.path.join(LIB, 'samplooler.kicad_sym'), list(syms.values()))
    # the library holds exactly what the designs use: drop footprints and models of old layouts
    names = {m['name'] for m in list(M.MODULES) + list(extra)}
    for f in glob.glob(os.path.join(LIB, 'samplooler.pretty', '*.kicad_mod')):
        if os.path.basename(f)[:-len('.kicad_mod')] not in names:
            os.remove(f)
    for f in glob.glob(os.path.join(LIB, 'samplooler.3dshapes', '*.wrl')):
        stem = os.path.basename(f)[:-len('.wrl')]
        if stem not in names and not stem.startswith('EC11_Vertical_Knob'):
            os.remove(f)
    return syms


# ============================================================== schematic, project, checks

def write_project(dirpath, name, severities=None):
    os.makedirs(dirpath, exist_ok=True)
    pro = json.load(open(os.path.join(os.environ.get('KICAD10_TEMPLATE_DIR', '/usr/share/kicad/template'),
                                      'kicad.kicad_pro')))
    pro['meta']['filename'] = name + '.kicad_pro'
    # Net classes and rules go in by hand: pcbnew is told never to save the project file, so it
    # comes out the same whether or not KiCad has the project open.
    blank = 'rgba(0, 0, 0, 0.000)'
    pro['net_settings'] = {
        'classes': [dict(name=n, bus_width=12, diff_pair_gap=0.25, diff_pair_via_gap=0.25, diff_pair_width=0.2,
                         line_style=0, microvia_diameter=0.3, microvia_drill=0.1, pcb_color=blank,
                         schematic_color=blank, tuning_profile='', wire_width=6,
                         priority=2147483647 if n == 'Default' else i, **c)
                    for i, (n, c) in enumerate(NETCLASSES.items())],
        'meta': {'version': 5}, 'net_colors': None, 'netclass_assignments': None,
        'netclass_patterns': [{'netclass': 'Power', 'pattern': p} for p in POWER_NETS]}
    pro.setdefault('board', {})['design_settings'] = {'meta': {'version': 2}, 'rules': dict(RULES)}
    if severities:
        pro['board']['design_settings']['rule_severities'] = dict(severities)
    json.dump(pro, open(os.path.join(dirpath, name + '.kicad_pro'), 'w'), indent=2)
    rel = os.path.relpath(LIB, dirpath)
    open(os.path.join(dirpath, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n  (version 7)\n'
        f'  (lib (name "samplooler")(type "KiCad")(uri "${{KIPRJMOD}}/{rel}/samplooler.kicad_sym")(options "")(descr "Samplooler modules"))\n'
        + ''.join(f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_SYMBOL_DIR}}/{n}.kicad_sym")(options "")(descr ""))\n'
                  for n in SYM_LIBS) + ')\n')
    open(os.path.join(dirpath, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n  (version 7)\n'
        f'  (lib (name "samplooler")(type "KiCad")(uri "${{KIPRJMOD}}/{rel}/samplooler.pretty")(options "")(descr "Samplooler modules"))\n'
        + ''.join(f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_FOOTPRINT_DIR}}/{n}.pretty")(options "")(descr ""))\n'
                  for n in FP_LIBS) + ')\n')


def schematic(spec, syms):
    sh = Sheet(spec['project'], spec['title'], REV, DATE, spec['comments'])
    for box in spec['sch'].get('boxes', []):
        sh.box(*box)
    for ref, (x, y, *rest) in spec['sch']['parts'].items():
        p = spec['parts'][ref]
        sym = syms.get(p['lib'].split(':')[1]) if p['lib'].startswith('samplooler:') else None
        sh.place(p['lib'], ref, p['value'], (x, y), p['nets'], rot=rest[0] if rest else 0,
                 footprint=p['fp'], sym=sym, fields=p.get('fields'), dnp=p.get('dnp', False),
                 in_bom=p.get('in_bom', True))
    for net, pos in spec['sch'].get('flags', []):
        sh.pwr_flag(net, pos)
    for text, pos, *size in spec.get('notes', []):
        sh.text(text, pos, *(size or [1.27]))
    return sh


def netlist(sch):
    """Pad nets exactly as eeschema names them (/LOCAL, GLOBAL, unconnected-(...))."""
    from sexpr import parse, find, find_all
    out = sch.replace('.kicad_sch', '.net')
    code, log = run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', out, sch])
    assert code == 0, log
    nets = {}
    for n in find_all(find(parse(open(out).read()), 'nets'), 'net'):
        name = find(n, 'name')[1]
        for node in find_all(n, 'node'):
            nets.setdefault(find(node, 'ref')[1], {})[find(node, 'pin')[1]] = name
    return nets


def erc(sch):
    rpt = sch.replace('.kicad_sch', '-erc.rpt')
    run(['kicad-cli', 'sch', 'erc', '--severity-all', '-o', rpt, sch])
    return [l for l in open(rpt).read().splitlines() if l.startswith('[')], rpt


def paths(spec):
    d = os.path.join(HW, spec['dir'])
    return d, os.path.join(d, spec['project'] + '.kicad_sch'), os.path.join(d, spec['project'] + '.kicad_pcb')


# ============================================================== mechanical model

def fp_lib_path(fpid):
    lib, name = fpid.split(':')
    return (os.path.join(LIB, 'samplooler.pretty') if lib == 'samplooler' else
            os.path.join(os.environ.get('KICAD10_FOOTPRINT_DIR', '/usr/share/kicad/footprints'), lib + '.pretty')), name


def mech_board(spec, z_front, sweeps=()):
    parts = []
    for ref, (x, y, r, side) in spec['place'].items():
        p = spec['parts'][ref]
        spec_m = p.get('mech', {})
        pads = mech.tht_pads(*fp_lib_path(p['fp'])) if spec_m.get('tail') else []
        parts.append(dict(ref=ref, origin=(x, y), rot=r, side=side, spec=spec_m, pads=pads))
    W, H = spec['size']
    return dict(name=spec['key'], z_front=z_front, outline=(0, 0, W, H), cutouts=spec.get('cutouts', []),
                holes=spec['holes'], parts=parts, trim={r: spec['parts'][r].get('mech', {}).get('trim', False)
                                                        for r in spec['place']}, sweeps=list(sweeps))


# ============================================================== PCB

def keepout(bd, a, b, tracks=True, vias=True, pour=True, layers=('F.Cu', 'B.Cu'), name='keepout'):
    z = pcbnew.ZONE(bd.b)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(tracks); z.SetDoNotAllowVias(vias); z.SetDoNotAllowZoneFills(pour)
    z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(bd.b.GetLayerID(l))
    z.SetLayerSet(ls)
    z.SetZoneName(name)
    ol = z.Outline()
    ol.NewOutline()
    (ax, ay), (bx, by) = A(a), A(b)
    for p in ((ax, ay), (bx, ay), (bx, by), (ax, by)):
        ol.Append(V(*p))
    bd.b.Add(z)


def ring_keepout(bd, c, r, n=24):
    """No tracks or vias within r of c (the router ignores inner board edges otherwise)."""
    z = pcbnew.ZONE(bd.b)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True); z.SetDoNotAllowZoneFills(False)
    z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    z.SetZoneName('opening')
    ol = z.Outline()
    ol.NewOutline()
    R = r / math.cos(math.pi / n)          # polygon circumscribes the circle
    for i in range(n):
        a = 2 * math.pi * i / n
        ol.Append(V(*A((c[0] + R * math.cos(a), c[1] + R * math.sin(a)))))
    bd.b.Add(z)


def make_board(spec, nets):
    d, _, path = paths(spec)
    bd = Board(path, os.path.join(LIB, 'samplooler.pretty'))
    bd.rules()
    ds = bd.b.GetDesignSettings()
    ds.m_TentViasFront = True
    ds.m_TentViasBack = True
    W, H = spec['size']
    x0, y0 = ORIGIN
    bd.outline(x0, y0, x0 + W, y0 + H, spec.get('corner', 3.0))
    for ref, (x, y, r, side) in spec['place'].items():
        p = spec['parts'][ref]
        lib, name = p['fp'].split(':')
        fp = bd.place(lib, name, ref, p['value'], A((x, y)), r, back=(side == 'back'), nets=nets[ref],
                      sym_uuid=uid(spec['project'], 'sym', ref), sheetfile=spec['project'] + '.kicad_sch',
                      fields=p.get('fields'), dnp=p.get('dnp', False))
        if side == 'back':
            fp.SetOrientationDegrees(r)
        off = spec.get('ref_at', {}).get(ref, 'keep')
        if off != 'keep':
            t = fp.Reference()
            if off is None:
                t.SetVisible(False)
            else:
                t.SetPosition(V(*A((x + off[0], y + off[1]))))
                t.SetTextAngleDegrees(0)
                t.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
                t.SetTextThickness(MM(0.15))
        if ref in spec.get('models', {}):
            ms = fp.Models()
            while len(ms):
                ms.pop()
            m3 = pcbnew.FP_3DMODEL()
            m3.m_Filename = spec['models'][ref]
            m3.m_Scale.x = m3.m_Scale.y = m3.m_Scale.z = 1 / 2.54
            ms.append(m3)
    for (x, y, dia) in spec.get('cutouts', []):
        bd.circle_cut(A((x, y)), dia)
        ring_keepout(bd, (x, y), dia / 2 + 0.7)
    for k in spec.get('keepouts', []):
        keepout(bd, **k)
    return bd


def silk(bd, s, pos, size=1.0, layer=pcbnew.F_SilkS, **kw):
    return bd.text(s, A(pos), size, layer, **kw)


def arrow_up(bd, x, y, layer=pcbnew.F_SilkS):
    """A small triangle pointing at the nose edge."""
    for a, b in (((x - 0.9, y + 0.8), (x, y - 0.4)), ((x, y - 0.4), (x + 0.9, y + 0.8)),
                 ((x - 0.9, y + 0.8), (x + 0.9, y + 0.8))):
        bd.silk_line(A(a), A(b), layer, 0.2)


def track(bd, net, layer, pts, width=0.6):
    bd.track(net, [A(p) for p in pts], width, {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}[layer])


def via(bd, net, p, d=0.8, drill=0.4):
    bd.via(net, A(p), d, drill)


def lock_all(bd):
    for t in items(bd.b.Tracks()):
        t.SetLocked(True)


# ============================================================== routing and pours

def strip_net_from_dsn(path, net):
    """Freerouting routes what the DSN lists; GND is left to the pours."""
    import re
    t = open(path).read()
    i = t.find(f'(net {net}\n')
    if i < 0:
        i = t.find(f'(net {net} ')
    if i >= 0:
        depth, j = 0, i
        while True:
            c = t[j]
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        t = t[:i] + t[j + 1:]
    t = t.replace(f' {net}\n', '\n').replace(f' {net} ', ' ')
    # route 0.01 mm wider apart than DRC checks, so Freerouting's rounding never lands on the limit
    t = re.sub(r'\(clearance (\d+(?:\.\d+)?)', lambda m: f'(clearance {float(m.group(1)) + 10:g}', t)
    open(path, 'w').write(t)


# Freerouting reads these from the DSN's structure scope. Each variant routes from the same start
# (only the locked copper); the build keeps the one that leaves the fewest connections. The fixed
# escape runs vertically on both layers, which the default split (front horizontal, back
# vertical) works against, hence the variants.
ROUTE_VARIANTS = [
    None,                                               # Freerouting's own defaults
    dict(F='vertical', B='horizontal'),
    dict(F='vertical', B='vertical', against=1.6),
    dict(F='horizontal', B='vertical', via=25),
    dict(F='horizontal', B='horizontal', against=1.6),
]


def _autoroute_settings(v):
    rule = lambda layer, d: (f'(layer_rule {layer} (active on) (preferred_direction {d}) '
                             f'(preferred_direction_trace_costs 1.0) '
                             f'(against_preferred_direction_trace_costs {v.get("against", 2.5)}))')
    return (f'(autoroute_settings (fanout off) (autoroute on) (postroute on) (vias on) '
            f'(via_costs {v.get("via", 50)}) (plane_via_costs 5) (start_ripup_costs 100) (start_pass_no 1) '
            f'{rule("F.Cu", v["F"])} {rule("B.Cu", v["B"])})')


def _clear_routing(bd):
    for t in items(bd.b.Tracks()):
        if not t.IsLocked():
            bd.b.Remove(t)


def _route_once(bd, workdir, passes, variant, ses):
    bd.b.BuildConnectivity()
    dsn = os.path.join(workdir, 'route.dsn')
    for f in (dsn, ses):
        if os.path.exists(f):
            os.remove(f)
    assert pcbnew.ExportSpecctraDSN(bd.b, dsn)
    strip_net_from_dsn(dsn, 'GND')
    if variant:
        t = open(dsn).read()
        # after the layers: the layer rules refer to them by name
        open(dsn, 'w').write(t.replace('    (boundary', '    ' + _autoroute_settings(variant) + '\n    (boundary', 1))
    r = subprocess.run([FREEROUTING, '-de', dsn, '-do', ses, '-mp', str(passes), '--gui.enabled=false'],
                       capture_output=True, text=True, timeout=2400)
    log = r.stdout + r.stderr
    if not os.path.exists(ses):
        return None, 10 ** 6, 0.0, log
    final = [l for l in log.splitlines() if 'final score' in l]
    score = final[-1].split('final score:', 1)[-1].split(',')[0].strip() if final else '(no score line)'
    try:
        left, value = int(score.split('(')[1].split()[0]), float(score.split()[0])
    except (IndexError, ValueError):
        left, value = 10 ** 5, 0.0
    return score, left, value, log


def autoroute(bd, workdir, passes=250):
    """Freerouting with each of ROUTE_VARIANTS from the same start, stopping at the first that
    routes everything; the one with the fewest connections left (then the best score) is kept."""
    best = None
    for i, v in enumerate(ROUTE_VARIANTS):
        ses = os.path.join(workdir, f'route-{i}.ses')
        score, left, value, log = _route_once(bd, workdir, passes, v, ses)
        if score and (best is None or (left, -value) < (best[1], -best[2])):
            best = (score, left, value, log, ses, i)
        if left == 0:
            break
    if best is None:
        raise RuntimeError('freerouting produced no session, see freerouting.log')
    score, left, value, log, ses, i = best
    _clear_routing(bd)
    assert pcbnew.ImportSpecctraSES(bd.b, ses)
    open(os.path.join(workdir, 'freerouting.log'), 'w').write(log)
    os.replace(ses, os.path.join(workdir, 'route.ses'))
    for f in glob.glob(os.path.join(workdir, 'route-*.ses')):
        os.remove(f)
    return f'{score}, variant {i}'


def stitch(bd, spec, net='GND', pitch=8.0, edge=2.0):
    """GND vias on a grid wherever they clear every pad, track, hole and opening."""
    b = bd.b
    b.BuildConnectivity()
    obstacles = []
    for fp in b.Footprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            obstacles.append((pcbnew.ToMM(bb.GetCenter().x), pcbnew.ToMM(bb.GetCenter().y),
                              max(pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())) / 2))
    segs = []
    for t in items(b.Tracks()):
        s, e = t.GetStart(), t.GetEnd()
        segs.append((pcbnew.ToMM(s.x), pcbnew.ToMM(s.y), pcbnew.ToMM(e.x), pcbnew.ToMM(e.y), pcbnew.ToMM(t.GetWidth()) / 2))
    W, H = spec['size']
    x0, y0 = ORIGIN
    added = []
    nx, ny = max(1, int((W - 2 * edge) / pitch)), max(1, int((H - 2 * edge) / pitch))
    circles = [(x, y, d / 2) for x, y, d in spec.get('cutouts', [])]
    for i in range(nx + 1):
        for j in range(ny + 1):
            x = x0 + edge + i * (W - 2 * edge) / nx
            y = y0 + edge + j * (H - 2 * edge) / ny
            if any(math.hypot(x - hx - x0, y - hy - y0) < 4.3 for hx, hy in spec['holes']):
                continue
            if any(a[0] <= x - x0 <= c[0] and a[1] <= y - y0 <= c[1] for a, c in spec.get('stitch_avoid', [])):
                continue
            if any(math.hypot(x - x0 - cx, y - y0 - cy) < r + 1.2 for cx, cy, r in circles):
                continue
            ok = all(math.hypot(x - ox, y - oy) > r + 0.65 for ox, oy, r in obstacles)
            if ok:
                for sx, sy, ex, ey, hw in segs:
                    dx, dy = ex - sx, ey - sy
                    L2 = dx * dx + dy * dy
                    t = 0 if L2 == 0 else max(0, min(1, ((x - sx) * dx + (y - sy) * dy) / L2))
                    if math.hypot(x - (sx + t * dx), y - (sy + t * dy)) < hw + 0.65:
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
        if t.GetClass() != 'PCB_VIA' or t.GetNetname() != net or t.IsLocked():
            continue
        pos = t.GetPosition()
        hit = set()
        for z in zones:
            for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                if z.IsOnLayer(layer) and z.HitTestFilledArea(layer, pos):
                    hit.add(layer)
        if len(hit) < 2:
            b.Remove(t)
            removed += 1
    return removed


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
    tries, added, holes = 0, [], []
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
        for area, l, i, bb in islands[:-2]:
            other = polys[pcbnew.B_Cu if l == pcbnew.F_Cu else pcbnew.F_Cu]
            x0, y0 = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
            x1, y1 = x0 + pcbnew.ToMM(bb.GetWidth()), y0 + pcbnew.ToMM(bb.GetHeight())
            cands = []
            y = y0 + step / 2
            while y < y1:
                x = x0 + step / 2
                while x < x1:
                    ring = [(x, y)] + [(x + 0.45 * math.cos(a), y + 0.45 * math.sin(a))
                                       for a in [k * math.pi / 4 for k in range(8)]]
                    if all(polys[l].Contains(V(*p), i) for p in ring) and all(other.Contains(V(*p)) for p in ring) \
                            and all(math.hypot(x - hx, y - hy) > hr + 0.65 for hx, hy, hr in holes):
                        cands.append((x, y))
                    x += step
                y += step
            for (x, y) in cands[:6]:
                tries += 1
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(V(x, y)); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3)); v.SetNet(bd.net(net))
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


def prune_dangling(bd, keep=('GND',)):
    """Drop what routing left hanging: track ends that lead nowhere (the router found a shorter
    way than a fixed escape line) and vias joined on one layer only. Repeats until nothing
    changes. GND is left alone: its vias and stubs meet the pours, which come later."""
    b = bd.b
    removed = 0
    while True:
        b.BuildConnectivity()
        c = b.GetConnectivity()
        dead = []
        for t in items(b.Tracks()):
            if t.GetNetname() in keep:
                continue
            if t.GetClass() == 'PCB_VIA':
                if sum(c.IsConnectedOnLayer(t, l) for l in (pcbnew.F_Cu, pcbnew.B_Cu)) < 2:
                    dead.append(t)
            elif c.TestTrackEndpointDangling(t, False):
                dead.append(t)
        if not dead:
            return removed
        for t in dead:
            b.Remove(t)
        removed += len(dead)


def finish(bd, spec, route=True):
    d = paths(spec)[0]
    report = {}
    if route:
        report['route'] = autoroute(bd, d)
        report['pruned'] = prune_dangling(bd)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        bd.pour('GND', layer)
    bd.fill()
    added = stitch(bd, spec)
    bd.fill()
    pruned = prune_vias(bd)
    bridged, left = bridge_islands(bd)
    report['stitch'] = f'{len(added) - pruned} grid + {len(bridged)} island vias, {left} unconnected'
    pcbnew.SaveBoard(bd.path, bd.b, True)    # project file is written by write_project()
    return report


def _live_gnd(b, net='GND'):
    """The main GND plane on each layer: {layer: (filled polygons, index of the largest island)}."""
    out = {}
    for z in b.Zones():
        if z.GetIsRuleArea() or z.GetNetname() != net:
            continue
        for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
            if not z.IsOnLayer(layer):
                continue
            ps = z.GetFilledPolysList(layer)
            if ps.OutlineCount():
                out[layer] = (ps, max(range(ps.OutlineCount()), key=lambda i: abs(ps.Outline(i).Area())))
    return out


def _inside(ps, i, q, margin):
    """q and a ring of `margin` around it lie in island i of ps (holes in the fill count as out)."""
    ring = [q] + [q + pcbnew.VECTOR2I(MM(margin * math.cos(k * math.pi / 4)), MM(margin * math.sin(k * math.pi / 4)))
                  for k in range(8)]
    return all(ps.Contains(v, i) for v in ring)


def rescue_gnd(pcb, net='GND'):
    """Tie GND copper the pours left cut off back to the planes, on the saved real fill:
      - every GND pad the DRC reports unconnected gets a short stub (one or two straight segments,
        ending in a pour on its layer or in a via onto the other one), checked against every
        other net with KiCad's own shapes; the router can wall a pad in so that no thermal spoke
        fits, or leave spokes the DRC does not count;
      - every pour island with no via or plated GND pad of its own gets a via onto the other
        layer's main plane.
    Everything added is GND copper inside GND pours. Returns the number of pads and islands tied."""
    rpt = pcb.replace('.kicad_pcb', '-rescue.json')
    run(['kicad-cli', 'pcb', 'drc', '--refill-zones', '--save-board', '--format', 'json', '-o', rpt, pcb])
    found = json.load(open(rpt))
    os.remove(rpt)
    gnd = [u for u in found.get('unconnected_items', [])
           if all(f'[{net}]' in it.get('description', '') for it in u['items'])]
    if not gnd:
        return 0
    lost = {it['uuid'] for u in gnd for it in u['items'] if 'pad' in it.get('description', '').lower()}
    b = pcbnew.LoadBoard(pcb)
    live = _live_gnd(b, net)
    others = {layer: [] for layer in (pcbnew.F_Cu, pcbnew.B_Cu)}
    for fp in b.Footprints():
        for p in fp.Pads():
            if p.GetNetname() != net:
                for layer in others:
                    if p.IsOnLayer(layer):
                        others[layer].append(p.GetEffectiveShape(layer))
    for t in items(b.Tracks()):
        if t.GetNetname() != net:
            for layer in others:
                if t.IsOnLayer(layer):
                    others[layer].append(t.GetEffectiveShape(layer))
    boxes = {layer: [(sh, sh.BBox(0)) for sh in shapes] for layer, shapes in others.items()}
    holes = [(p.GetPosition(), pcbnew.ToMM(max(p.GetDrillSize().x, p.GetDrillSize().y)) / 2)
             for fp in b.Footprints() for p in fp.Pads() if p.HasHole()]
    holes += [(t.GetPosition(), 0.15) for t in items(b.Tracks()) if t.GetClass() == 'PCB_VIA']
    # vias already joining the layers (a plated pad does not count: it can be walled in on one side)
    links = [t.GetPosition() for t in items(b.Tracks()) if t.GetClass() == 'PCB_VIA' and t.GetNetname() == net]
    cl_track = MM(RULES['min_clearance'] + 0.05 + 0.15)       # clearance + margin + half a 0.3 track
    cl_via = MM(RULES['min_clearance'] + 0.05 + 0.3)          # clearance + margin + via radius
    fills = {layer: ps for layer, (ps, _) in live.items()}

    def clear(layer, a, c, cl):
        """No other net within cl of the segment a-c on this layer."""
        s = pcbnew.SEG(a, c)
        x0, x1 = min(a.x, c.x) - cl, max(a.x, c.x) + cl
        y0, y1 = min(a.y, c.y) - cl, max(a.y, c.y) + cl
        return not any(bb.GetRight() >= x0 and bb.GetX() <= x1 and bb.GetBottom() >= y0 and bb.GetY() <= y1
                       and sh.Collide(s, cl) for sh, bb in boxes[layer])

    def in_fill(layer, q, margin=0.35):
        ps = fills.get(layer)
        return ps is not None and any(_inside(ps, i, q, margin) for i in range(ps.OutlineCount()))

    def via_ok(q):
        return clear(pcbnew.F_Cu, q, q, cl_via) and clear(pcbnew.B_Cu, q, q, cl_via) and \
            all((h - q).EuclideanNorm() > MM(0.15 + r + RULES['min_hole_to_hole'] + 0.05) for h, r in holes)

    def add_via(q):
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(q); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3)); v.SetNet(b.FindNet(net))
        b.Add(v)
        holes.append((q, 0.15))
        links.append(q)

    def ring(c, radii):
        for r in radii:
            for k in range(24):
                a = 2 * math.pi * k / 24
                yield c + pcbnew.VECTOR2I(MM(r * math.cos(a)), MM(r * math.sin(a)))

    def stub_for(c0, layers):
        radii = [0.6 + 0.4 * i for i in range(9)]
        for layer in layers:
            far = pcbnew.B_Cu if layer == pcbnew.F_Cu else pcbnew.F_Cu
            hops = []
            for q in ring(c0, radii):
                if clear(layer, c0, q, cl_track):
                    if in_fill(layer, q):
                        return layer, [c0, q], False
                    if in_fill(far, q) and via_ok(q):
                        return layer, [c0, q], True
                    hops.append(q)
            for q1 in hops[:40]:
                for q2 in ring(q1, radii):
                    if clear(layer, q1, q2, cl_track):
                        if in_fill(layer, q2):
                            return layer, [c0, q1, q2], False
                        if in_fill(far, q2) and via_ok(q2):
                            return layer, [c0, q1, q2], True
        return None

    tied = 0
    for p in [p for fp in b.Footprints() for p in fp.Pads() if p.m_Uuid.AsString() in lost]:
        s = stub_for(p.GetPosition(), [l for l in (pcbnew.F_Cu, pcbnew.B_Cu) if p.IsOnLayer(l)])
        if s:
            layer, pts, with_via = s
            for a, c in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(b)
                t.SetStart(a); t.SetEnd(c); t.SetWidth(MM(0.3)); t.SetLayer(layer); t.SetNet(p.GetNet())
                b.Add(t)
            if with_via:
                add_via(pts[-1])
            tied += 1
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        far = pcbnew.B_Cu if layer == pcbnew.F_Cu else pcbnew.F_Cu
        if layer not in live or far not in live:
            continue
        (ps, main), (fps, fmain) = live[layer], live[far]
        for i in range(ps.OutlineCount()):
            if i == main or any(ps.Contains(q, i) and fps.Contains(q, fmain) for q in links):
                continue                              # already has a via onto the far main plane
            bb = ps.Outline(i).BBox()
            best = None
            y = bb.GetY() + MM(0.3)
            while y < bb.GetBottom() and best is None:
                x = bb.GetX() + MM(0.3)
                while x < bb.GetRight():
                    q = pcbnew.VECTOR2I(int(x), int(y))
                    if _inside(ps, i, q, 0.45) and _inside(fps, fmain, q, 0.45) and via_ok(q):
                        best = q
                        break
                    x += MM(0.6)
                y += MM(0.6)
            if best is not None:
                add_via(best)
                tied += 1
    pcbnew.SaveBoard(pcb, b, True)
    return tied


def drc(pcb):
    rpt = pcb.replace('.kicad_pcb', '-drc.rpt')
    # The pcbnew Python filler runs without the DRC engine, so it ignores edge and hole clearances.
    # Let kicad-cli refill with the real rules and SAVE that fill: the Gerbers are plotted from it.
    run(['kicad-cli', 'pcb', 'drc', '--refill-zones', '--save-board', '-o', rpt, pcb])
    run(['kicad-cli', 'pcb', 'drc', '--schematic-parity', '--severity-all', '-o', rpt, pcb])
    return [l for l in open(rpt).read().splitlines() if l.startswith('[')], rpt


# ============================================================== fab outputs

def fab_outputs(spec):
    """Gerbers + drill zipped for JLCPCB, JLC-format BOM/CPL for the SMD parts, schematic PDF."""
    name = spec['project']
    d, sch, pcb = paths(spec)
    os.makedirs(FAB, exist_ok=True)
    gdir = os.path.join(FAB, name + '-gerbers')
    if os.path.isdir(gdir):
        shutil.rmtree(gdir)
    os.makedirs(gdir)
    code, log = run(['kicad-cli', 'pcb', 'export', 'gerbers', '-o', gdir,
                     '-l', 'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts',
                     '--no-x2', '--no-netlist', '--subtract-soldermask', '--check-zones', pcb])
    assert code == 0, log
    code, log = run(['kicad-cli', 'pcb', 'export', 'drill', '-o', gdir + '/', '--format', 'excellon',
                     '--excellon-units', 'mm', '--excellon-zeros-format', 'decimal',
                     '--excellon-oval-format', 'alternate', '--excellon-separate-th',
                     '--generate-map', '--map-format', 'gerberx2', pcb])
    assert code == 0, log
    zpath = os.path.join(FAB, name + '-jlcpcb.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    parts = spec['parts']
    groups = {}
    for ref, p in parts.items():
        lcsc = (p.get('fields') or {}).get('LCSC')
        if lcsc and not p.get('dnp'):
            groups.setdefault((p['value'], p['fp'].split(':')[1], lcsc), []).append(ref)
    with open(os.path.join(FAB, name + '-bom.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (val, fpn, lcsc), refs in sorted(groups.items()):
            w.writerow([val, ','.join(sorted(refs)), fpn, lcsc])
    pos = os.path.join(FAB, name + '-pos-kicad.csv')
    code, log = run(['kicad-cli', 'pcb', 'export', 'pos', '-o', pos, '--format', 'csv', '--units', 'mm',
                     '--side', 'both', '--smd-only', '--exclude-dnp', pcb])
    assert code == 0, log
    with open(pos) as f, open(os.path.join(FAB, name + '-cpl.csv'), 'w', newline='') as g:
        w = csv.writer(g)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        for row in csv.DictReader(f):
            if (parts.get(row['Ref'], {}).get('fields') or {}).get('LCSC'):
                w.writerow([row['Ref'], row['PosX'] + 'mm', row['PosY'] + 'mm',
                            'Top' if row['Side'] == 'top' else 'Bottom', row['Rot']])
    os.remove(pos)
    code, log = run(['kicad-cli', 'sch', 'export', 'pdf', '-o', os.path.join(FAB, name + '-schematic.pdf'), sch])
    assert code == 0, log
    return zpath


# ============================================================== automatic schematic layout

def _sym_extent(sym, nets):
    """Symbol bounds in schematic mm around its origin, with room for stubs and net labels."""
    from schematic import sym_pins, stock_symbol
    from sexpr import find_all, find
    pins = sym_pins(sym)
    xs, ys = [0.0], [0.0]
    for sub in find_all(sym, 'symbol'):
        for r in find_all(sub, 'rectangle'):
            s, e = find(r, 'start'), find(r, 'end')
            xs += [float(s[1]), float(e[1])]
            ys += [-float(s[2]), -float(e[2])]
    lab = {'L': 0.0, 'R': 0.0, 'U': 0.0, 'D': 0.0}
    for num, (px, py, ang) in pins.items():
        xs.append(px); ys.append(-py)
        net = nets.get(num)
        n = 6.0 + (0.9 * len(net) if net else 1.0)
        side = {0: 'L', 180: 'R', 90: 'D', 270: 'U'}[ang % 360]
        lab[side] = max(lab[side], n)
    return (min(xs) - lab['L'] - 1, min(ys) - lab['U'] - 3, max(xs) + lab['R'] + 1, max(ys) + lab['D'] + 4)


def auto_sch(spec, syms, groups, width=390.0, x0=15.0, y0=25.0):
    """Lay each group out in a titled dashed box, boxes flowing left to right, rows downward."""
    from schematic import stock_symbol
    boxes, place = [], {}
    x, y, row_h = x0, y0, 0.0
    for title, refs in groups:
        cells = []
        for ref in refs:
            p = spec['parts'][ref]
            sym = syms[p['lib'].split(':')[1]] if p['lib'].startswith('samplooler:') else stock_symbol(p['lib'])
            ex = _sym_extent(sym, p['nets'])
            cells.append((ref, ex))
        bw = sum(e[2] - e[0] for _, e in cells) + 8.0 * len(cells) + 4
        bw = max(bw, 7.0 + 2.0 * len(title))
        bh = max(e[3] - e[1] for _, e in cells) + 14.0
        if x + bw > x0 + width:
            x, y, row_h = x0, y + row_h + 8.0, 0.0
        cx = x + 6.0
        for ref, e in cells:
            place[ref] = (cx - e[0], y + 11.0 - e[1])
            cx += e[2] - e[0] + 8.0
        boxes.append(((x, y), (x + bw, y + bh), title))
        x += bw + 8.0
        row_h = max(row_h, bh)
    return dict(boxes=boxes, parts=place), y + row_h


def fanout_gnd(bd, spec, net='GND', dist=1.25):
    """Give every surface-mount GND pad its own via to the other layer before routing, so no
    pad depends on how the pour happens to fill around it. Tries eight directions, nearest free."""
    b = bd.b
    others = []          # (x, y, radius, net, key)
    for fp in b.Footprints():
        for p in fp.Pads():
            q, bb = p.GetPosition(), p.GetBoundingBox()
            others.append((pcbnew.ToMM(q.x), pcbnew.ToMM(q.y),
                           max(pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())) / 2, p.GetNetname(),
                           (fp.GetReference(), p.GetNumber(), round(pcbnew.ToMM(q.x), 3), round(pcbnew.ToMM(q.y), 3))))
    segs = [(pcbnew.ToMM(t.GetStart().x), pcbnew.ToMM(t.GetStart().y), pcbnew.ToMM(t.GetEnd().x),
             pcbnew.ToMM(t.GetEnd().y), pcbnew.ToMM(t.GetWidth()) / 2, t.GetNetname()) for t in items(b.Tracks())]
    bounds, cutouts = spec['size'], spec.get('cutouts', [])
    added = 0
    for fp in b.Footprints():
        c = fp.GetPosition()
        cx, cy = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
        for p in fp.Pads():
            if p.GetNetname() != net or p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            q = p.GetPosition()
            px, py = pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)
            me = (fp.GetReference(), p.GetNumber(), round(px, 3), round(py, 3))
            front = p.IsOnLayer(pcbnew.F_Cu)
            away = math.atan2(py - cy, px - cx) if (px, py) != (cx, cy) else 0.0
            for k in range(8):
                a = away + (k // 2 + (k % 2)) * (math.pi / 4) * (1 if k % 2 else -1)
                vx, vy = px + dist * math.cos(a), py + dist * math.sin(a)
                bx, by = vx - ORIGIN[0], vy - ORIGIN[1]
                if not (0.9 < bx < bounds[0] - 0.9 and 0.9 < by < bounds[1] - 0.9):
                    continue
                if any(math.hypot(bx - cx, by - cy) < d / 2 + 1.0 for cx, cy, d in cutouts):
                    continue
                ok = all(key == me or math.hypot(vx - ox, vy - oy) > r + 0.3 + 0.25
                         for ox, oy, r, n, key in others)
                if ok:
                    for sx, sy, ex, ey, hw, n in segs:
                        dx, dy = ex - sx, ey - sy
                        L2 = dx * dx + dy * dy
                        t = 0 if L2 == 0 else max(0, min(1, ((vx - sx) * dx + (vy - sy) * dy) / L2))
                        if n != net and math.hypot(vx - (sx + t * dx), vy - (sy + t * dy)) < hw + 0.3 + 0.25:
                            ok = False
                            break
                if ok:
                    bd.via(net, (vx, vy), 0.6, 0.3)
                    bd.track(net, [(px, py), (vx, vy)], 0.4, pcbnew.F_Cu if front else pcbnew.B_Cu)
                    others.append((vx, vy, 0.3, net, ('via', added, vx, vy)))
                    added += 1
                    break
    lock_all(bd)
    return added


# ============================================================== SuperMini escape pattern

Y_OUT = 21.91 + 1.5          # the row just below the SuperMini where every escape line ends


HEADER_GAP_FIT = 0.76          # a pad escapes through a header gap if the gap's centre line crosses its land


def supermini_escape(through_header=False):
    """The fixed escape for the 20 pogo pads (1.753 mm pitch, three columns), in the module's frame:
    {pad: [(layer, points) | ('via', point)]}, layer 'pad' = the pads' own copper, 'other' = the far
    side. Lines end on Y_OUT, just below the module, so a breakout can take them in order:
      column C (8 pads, next to the TX column): out to the right on the pad layer, fanned
      column A rows 4-7: out to the left on the pad layer, below column B
      column B (rows 0-3): via just left of the pad, fanned out on the other layer
      column A rows 0-3: via in the A-B gap, fanned out on the other layer, toward the 5V column
    through_header: a column C pad whose row lines up with the gap between two header pins leaves
    straight out through that gap instead (for a breakout on the header's side of the module).
    """
    pogo = {p[0]: (p[1], p[2]) for p in M.SUPERMINI['pogo']}
    cols = {}
    for num, (x, y) in pogo.items():
        cols.setdefault(round(x, 2), []).append((y, num))
    xc, xb, xa = sorted(cols)                      # C (left), B (middle), A (right)
    C, Bc, Acol = ([n for _, n in sorted(cols[k])] for k in (xc, xb, xa))
    plan = {}
    gaps = [2.54 * (i + 0.5) for i in range(8)]    # between header pins 1..9
    down = []
    for n in C:
        x, y = pogo[n]
        g = min(gaps, key=lambda g: abs(g - y))
        if through_header and abs(g - y) <= HEADER_GAP_FIT:
            # 45 degrees from the pad centre onto the gap's centre line, then straight out
            plan[n] = [('pad', [(x, y), (x - abs(g - y), g), (-1.4, g)])]
        else:
            down.append(n)
    for k, n in enumerate(down):                   # top row takes the innermost vertical
        x, y = pogo[n]
        xv = 3.4 + 0.55 * (len(down) - 1 - k)
        plan[n] = [('pad', [(x, y), (xv, y), (xv, Y_OUT)])]
    for j, n in enumerate(Acol[4:]):               # below column B: straight out to the left
        x, y = pogo[n]
        xv = 9.8 + 0.55 * j
        plan[n] = [('pad', [(x, y), (xv, y), (xv, Y_OUT)])]
    for k, n in enumerate(Bc):                     # via left of the pad, then fan left
        x, y = pogo[n]
        vx, xo = 8.9, 6.5 + 0.55 * k
        plan[n] = [('pad', [(x, y), (vx, y)]), ('via', (vx, y)), ('other', [(vx, y), (xo, y + (vx - xo)), (xo, Y_OUT)])]
    for k, n in enumerate(Acol[:4]):               # via in the A-B gap, then fan right
        x, y = pogo[n]
        vx, xo = round((xb + xa) / 2, 3), 13.95 - 0.55 * k
        plan[n] = [('pad', [(x, y), (vx, y)]), ('via', (vx, y)), ('other', [(vx, y), (xo, y + (xo - vx)), (xo, Y_OUT)])]
    return plan


# 5V from pad 10 (row top), 3V3 from pad 12: along the top on the other layer, down the left side
SUPERMINI_RISERS = {10: [(M.SM_ROW, 0.0), (M.SM_ROW - 1.2, 1.2), (3.6, 1.2), (3.6, Y_OUT + 1.0)],
                    12: [(M.SM_ROW, 2 * 2.54), (4.8, 2 * 2.54), (4.8, Y_OUT + 1.0)]}


def supermini_exits(through_header=False):
    """Where each escape line ends: {pad: (x, y) in the module frame}, the pogo pads plus the 5V
    (10) and 3V3 (12) risers. Breakouts order their pins by this."""
    out = {n: steps[-1][1][-1] for n, steps in supermini_escape(through_header).items()}
    out.update({n: pts[-1] for n, pts in SUPERMINI_RISERS.items()})
    return out


def right_to_left(seq):
    """Pin order for a 2-row header placed at 270 degrees (pin 1 top right, columns numbered from
    the right, odd pins on top), from nets listed left to right as (top, bottom) pairs."""
    n = len(seq) // 2
    pins = [None] * len(seq)
    for i, net in enumerate(seq):
        pins[2 * (n - 1 - i // 2) + i % 2] = net
    return pins


def supermini_fanout(bd, ref, origin, rot, side, through_header=False):
    """Draw supermini_escape() and the 5V/3V3 risers for a placed SuperMini, locked. Drawn in
    the module's own frame, so it works front or back; Freerouting only joins the line ends."""
    fp = bd.fps[ref]
    net = {p.GetNumber(): p.GetNetname() for p in fp.Pads()}
    layer = {'pad': 'F.Cu' if side == 'front' else 'B.Cu', 'other': 'B.Cu' if side == 'front' else 'F.Cu'}
    P = lambda p: at(origin, p, rot, side)
    for n, steps in supermini_escape(through_header).items():
        for kind, geo in steps:
            if kind == 'via':
                via(bd, net[str(n)], P(geo), 0.6, 0.3)
            else:
                track(bd, net[str(n)], layer[kind], [P(p) for p in geo], 0.25)
    for n, pts in SUPERMINI_RISERS.items():
        track(bd, net[str(n)], layer['other'], [P(p) for p in pts], 0.6)
    lock_all(bd)
