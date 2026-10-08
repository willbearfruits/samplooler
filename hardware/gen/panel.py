#!/usr/bin/env python3
"""Put finished boards side by side on one JLCPCB panel, joined by mouse-bite tabs.

    python3 hardware/gen/panel.py            # every panel in PANELS
    python3 hardware/gen/panel.py stack
    python3 hardware/gen/panel.py mini       # the Mini and the Samplooler Filter (~/Projects/samplooler-filter)

Run it after build.py: it reads the routed, filled, DRC-clean boards and changes none of them.
The panel copies everything (footprints, tracks, zones with their saved fill, text, openings),
redraws the outer edges with gaps where the tabs are, and drills a row of 0.5 mm holes along
both sides of every tab so it snaps off. Boards are stacked top to bottom, `GAP` mm apart.
Output: hardware/panel-<name>/, and hardware/fab/samplooler-panel-<name>-jlcpcb.zip plus a
merged BOM and CPL whose designators carry the board's prefix (JLC needs them unique).
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pcbnew        # noqa: E402
import kit           # noqa: E402
from pcb import items, MM, V   # noqa: E402

GAP = 2.0            # routed slot between boards
TAB_W = 5.0          # tab width along the edge
BITE_D = 0.5         # mouse-bite drill
BITE_PITCH = 0.8
LIMIT = 95.0         # what the panel has to fit in, both ways
FILTER = os.path.expanduser('~/Projects/samplooler-filter')


def _here(d, project):
    """A board built in this repository (hardware/<d>/, fab files in hardware/fab/)."""
    return dict(pcb=os.path.join(kit.HW, d, project + '.kicad_pcb'),
                bom=os.path.join(kit.FAB, project + '-bom.csv'), cpl=os.path.join(kit.FAB, project + '-cpl.csv'),
                corner=3.0)


# name: boards top to bottom, each with its designator prefix, the tab x centres on its lower edge
# (the tabs join it to the next board's upper edge; keep them off parts at either edge and off the
# rounded corners) and how far the bite holes sit into the slot (`bite_in`: 0 = on the edge).
PANELS = {
    'stack': [dict(_here('stack-bottom', 'samplooler-stack-bottom'), prefix='B', tabs=(14.0, 38.0, 80.0)),
              dict(_here('stack-top', 'samplooler-stack-top'), prefix='T', tabs=())],
    # the Mini's plain lower edge against the Filter's back edge: the tabs sit in the Filter's gaps
    # between its IN jack (x 65-75), USB-C socket (36-46) and OUT jack (7-17); the Filter keeps
    # copper 0.3 mm from its edge, so its bite holes sit 0.2 mm out into the slot
    'mini': [dict(_here('mini', 'samplooler-mini'), prefix='M', tabs=(26.0, 56.0), bite_in=0.2),
             dict(pcb=os.path.join(FILTER, 'hardware', 'kicad', 'sampfilter.kicad_pcb'),
                  bom=os.path.join(FILTER, 'fab', 'sampfilter-bom-jlcpcb.csv'),
                  cpl=os.path.join(FILTER, 'fab', 'sampfilter-cpl-jlcpcb.csv'), corner=2.0, prefix='F', tabs=())],
}


def _edge_outline(b):
    """The outer outline's segments and arcs (circles on Edge.Cuts are openings and stay)."""
    out = []
    for d in items(b.Drawings()):
        d = d.Cast()
        if (d.GetLayer() == pcbnew.Edge_Cuts and isinstance(d, pcbnew.PCB_SHAPE)
                and d.GetShape() in (pcbnew.SHAPE_T_SEGMENT, pcbnew.SHAPE_T_ARC)):
            out.append(d)
    return out


def _dup(item):
    """Duplicate() takes addToParentGroup on some item types and nothing on others."""
    try:
        return item.Duplicate(False).Cast()
    except TypeError:
        return item.Duplicate().Cast()


def _net(dst, name, cache):
    if name not in cache:
        ni = dst.FindNet(name)
        if ni is None:
            ni = pcbnew.NETINFO_ITEM(dst, name)
            dst.Add(ni)
        cache[name] = ni
    return cache[name]


def _copy_board(dst, src, dx, dy, cache):
    """Every item of src into dst, moved by (dx, dy) mm, nets matched by name."""
    off = pcbnew.VECTOR2I(MM(dx), MM(dy))

    def netted(item, new):
        if isinstance(item, pcbnew.BOARD_CONNECTED_ITEM):
            new.SetNet(_net(dst, item.GetNetname(), cache))

    for fp in items(src.Footprints()):
        new = _dup(fp)
        new.Move(off)
        dst.Add(new)
        for p, q in zip(items(fp.Pads()), items(new.Pads())):
            q.SetNet(_net(dst, p.GetNetname(), cache))
    for t in items(src.Tracks()):
        new = _dup(t)
        new.Move(off)
        netted(t, new)
        dst.Add(new)
    for z in items(src.Zones()):
        new = _dup(z)
        new.Move(off)
        netted(z, new)
        dst.Add(new)
    outline = {d.m_Uuid.AsString() for d in _edge_outline(src)}
    for d in items(src.Drawings()):
        if d.m_Uuid.AsString() in outline:
            continue
        new = _dup(d)
        new.Move(off)
        dst.Add(new)


def _seg(b, a, c):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    s.SetStart(V(*kit.A(a))); s.SetEnd(V(*kit.A(c)))
    b.Add(s)


def _arc(b, c, start, deg):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    s.SetCenter(V(*kit.A(c))); s.SetStart(V(*kit.A(start)))
    s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(deg, pcbnew.DEGREES_T), True)
    b.Add(s)


def _outline(b, x0, y0, x1, y1, r, top_tabs, bottom_tabs):
    """A rounded rectangle whose top and bottom edges break where the tabs are."""
    def hline(y, xa, xb, tabs):                 # xa -> xb, leaving out every tab
        cuts = sorted((x - TAB_W / 2, x + TAB_W / 2) for x in tabs)
        xs = [xa]
        for a, c in (cuts if xa < xb else reversed(cuts)):
            xs += [a, c] if xa < xb else [c, a]
        xs.append(xb)
        for i in range(0, len(xs), 2):
            _seg(b, (xs[i], y), (xs[i + 1], y))
    hline(y0, x0 + r, x1 - r, top_tabs)
    _seg(b, (x1, y0 + r), (x1, y1 - r))
    hline(y1, x1 - r, x0 + r, bottom_tabs)
    _seg(b, (x0, y1 - r), (x0, y0 + r))
    _arc(b, (x1 - r, y0 + r), (x1 - r, y0), 90)
    _arc(b, (x1 - r, y1 - r), (x1, y1 - r), 90)
    _arc(b, (x0 + r, y1 - r), (x0 + r, y1), 90)
    _arc(b, (x0 + r, y0 + r), (x0, y0 + r), 90)


def _bites(b, tabs, y_lo, y_hi, bite_in=0.0):
    """The tab's sides across the gap, and a row of holes on each board edge it joins, `bite_in` mm
    out into the slot."""
    fp = pcbnew.FOOTPRINT(b)
    fp.SetPosition(V(*kit.A((0, 0))))
    n = 0
    for x in tabs:
        _seg(b, (x - TAB_W / 2, y_lo), (x - TAB_W / 2, y_hi))
        _seg(b, (x + TAB_W / 2, y_lo), (x + TAB_W / 2, y_hi))
        k = int((TAB_W - BITE_D) // BITE_PITCH)
        x0 = x - k * BITE_PITCH / 2
        for y in (y_lo + bite_in, y_hi - bite_in):
            for i in range(k + 1):
                p = pcbnew.PAD(fp)
                p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
                p.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
                p.SetDrillShape(pcbnew.PAD_DRILL_SHAPE_CIRCLE)
                p.SetSize(pcbnew.VECTOR2I(MM(BITE_D), MM(BITE_D)))
                p.SetDrillSize(pcbnew.VECTOR2I(MM(BITE_D), MM(BITE_D)))
                ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Mask); ls.AddLayer(pcbnew.B_Mask)
                p.SetLayerSet(ls)
                p.SetNumber('')
                p.SetPosition(V(*kit.A((x0 + i * BITE_PITCH, y))))
                fp.Add(p)
                n += 1
    b.Add(fp)
    return n


def _fab(name, parts):
    """Gerbers and drills of the panel as it is saved (no refill), and the merged BOM/CPL."""
    import shutil
    import zipfile
    d = os.path.join(kit.HW, f'panel-{name}')
    pcb = os.path.join(d, f'samplooler-panel-{name}.kicad_pcb')
    gdir = os.path.join(kit.FAB, f'samplooler-panel-{name}-gerbers')
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(gdir)
    code, log = kit.run(['kicad-cli', 'pcb', 'export', 'gerbers', '-o', gdir,
                         '-l', 'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts',
                         '--no-x2', '--no-netlist', '--subtract-soldermask', pcb])
    assert code == 0, log
    code, log = kit.run(['kicad-cli', 'pcb', 'export', 'drill', '-o', gdir + '/', '--format', 'excellon',
                         '--excellon-units', 'mm', '--excellon-zeros-format', 'decimal',
                         '--excellon-oval-format', 'alternate', '--excellon-separate-th',
                         '--generate-map', '--map-format', 'gerberx2', pcb])
    assert code == 0, log
    zpath = os.path.join(kit.FAB, f'samplooler-panel-{name}-jlcpcb.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    bom, cpl = {}, []
    for spec, dx, dy in parts:
        prefix = spec['prefix']
        with open(spec['bom']) as f:
            for row in csv.DictReader(f):
                key = (row['Comment'], row['Footprint'], row['LCSC Part #'])
                bom.setdefault(key, []).extend(prefix + r for r in row['Designator'].split(','))
        with open(spec['cpl']) as f:
            for row in csv.DictReader(f):
                x = float(row['Mid X'].rstrip('m')) + dx
                y = float(row['Mid Y'].rstrip('m')) - dy       # KiCad's pos file has +Y up
                cpl.append([prefix + row['Designator'], f'{x:.4f}mm', f'{y:.4f}mm', row['Layer'], row['Rotation']])
    with open(os.path.join(kit.FAB, f'samplooler-panel-{name}-bom.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (val, fpn, lcsc), refs in sorted(bom.items()):
            w.writerow([val, ','.join(sorted(refs)), fpn, lcsc])
    with open(os.path.join(kit.FAB, f'samplooler-panel-{name}-cpl.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        w.writerows(cpl)
    return zpath


def build(name):
    specs = PANELS[name]
    srcs = [(pcbnew.LoadBoard(sp['pcb']), sp) for sp in specs]
    d = os.path.join(kit.HW, f'panel-{name}')
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, f'samplooler-panel-{name}.kicad_pcb')
    # the first board is the base: its settings, layers and rules carry over; the others are moved
    # so their outlines line up under it (each project places its board on the page its own way)
    base = srcs[0][0]

    def edges(b):
        """The outline's (left, top, width, height) in mm, on the line centres: the bounding box
        includes the 0.1 mm line width, which would put the redrawn outline 0.05 mm off."""
        bb, lw = b.GetBoardEdgesBoundingBox(), 0.1
        return (pcbnew.ToMM(bb.GetLeft()) + lw / 2, pcbnew.ToMM(bb.GetTop()) + lw / 2,
                pcbnew.ToMM(bb.GetWidth()) - lw, pcbnew.ToMM(bb.GetHeight()) - lw)
    left, top0, _, _ = edges(base)
    cache = {}
    y = 0.0
    frames, parts = [], []
    for i, (b, sp) in enumerate(srcs):
        bl, bt, w, h = edges(b)
        dx, dy = left - bl, top0 + y - bt
        if i:
            _copy_board(base, b, dx, dy, cache)
        frames.append((y, w, h, sp))
        parts.append((sp, dx, dy))
        y += h + GAP
    for dr in _edge_outline(base):
        base.Remove(dr)
    total_h = y - GAP
    total_w = max(f[1] for f in frames)
    x0, y0 = left - kit.ORIGIN[0], top0 - kit.ORIGIN[1]
    holes = 0
    for i, (dy, w, h, sp) in enumerate(frames):
        above = frames[i - 1][3]['tabs'] if i else ()
        _outline(base, x0, y0 + dy, x0 + w, y0 + dy + h, sp.get('corner', 3.0), above, sp['tabs'])
        if sp['tabs']:
            holes += _bites(base, [x0 + t for t in sp['tabs']], y0 + dy + h, y0 + dy + h + GAP, sp.get('bite_in', 0.0))
    pcbnew.SaveBoard(out, base, True)
    kit.write_project(d, f'samplooler-panel-{name}')
    zpath = _fab(name, parts)
    fits = total_w <= LIMIT and total_h <= LIMIT
    print(f'panel {name}: {total_w:.1f} x {total_h:.1f} mm ({"fits" if fits else "TOO BIG for"} '
          f'{LIMIT:g} x {LIMIT:g}), {len(srcs)} boards, {sum(len(f[3]["tabs"]) for f in frames)} tabs, '
          f'{holes} bite holes')
    print('    fab:', os.path.relpath(zpath, kit.HW))
    return fits


if __name__ == '__main__':
    names = sys.argv[1:] or list(PANELS)
    ok = all([build(n) for n in names])
    sys.exit(0 if ok else 1)
