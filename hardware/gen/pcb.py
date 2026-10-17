"""pcbnew helpers: board setup, placement, outline, pours, Freerouting round trip.

Python 3.14 + KiCad 10 SWIG iterators are broken for some containers (board.Tracks()),
so containers are walked by index.
"""
import math
import os
import subprocess
import pcbnew

MM = pcbnew.FromMM
V = pcbnew.VECTOR2I_MM
SYSFP = os.environ.get('KICAD10_FOOTPRINT_DIR', '/usr/share/kicad/footprints')
FREEROUTING = os.path.expanduser('~/.local/share/freerouting/freerouting-2.4.1-linux-x64/bin/freerouting')


def items(c):
    return [c[i] for i in range(len(c))]


# Design rules, one copy: applied to the board here and written into the .kicad_pro by build.py
# (the project file is where KiCad keeps them; pcbnew skips saving it while KiCad has it open).
RULES = dict(min_clearance=0.15, min_track_width=0.15, min_via_diameter=0.5, min_through_hole_diameter=0.3,
             min_copper_edge_clearance=0.5, min_hole_to_hole=0.5, min_resolved_spokes=1)
NETCLASSES = {
    'Default': dict(clearance=0.2, track_width=0.25, via_diameter=0.6, via_drill=0.3),
    'Power': dict(clearance=0.2, track_width=0.5, via_diameter=0.8, via_drill=0.4),
}
POWER_NETS = ['+5V', '+3V3', 'GND', '/OLED_P1', '/OLED_P2', '/DAC_3V3']


class Board:
    def __init__(self, path, projlib):
        self.path = path
        self.projlib = projlib          # path to samplooler.pretty
        self.b = pcbnew.NewBoard(path)
        self.b.SetCopperLayerCount(2)
        self.nets = {}
        self.fps = {}

    # -- rules (JLCPCB 2-layer standard: 0.127 mm min, we stay well above) --
    def rules(self):
        ds = self.b.GetDesignSettings()
        ds.m_MinClearance = MM(RULES['min_clearance'])
        ds.m_TrackMinWidth = MM(RULES['min_track_width'])
        ds.m_ViasMinSize = MM(RULES['min_via_diameter'])
        ds.m_MinThroughDrill = MM(RULES['min_through_hole_diameter'])
        ds.m_CopperEdgeClearance = MM(RULES['min_copper_edge_clearance'])
        ds.m_HoleToHoleMin = MM(RULES['min_hole_to_hole'])
        ds.m_MinResolvedSpokes = RULES['min_resolved_spokes']
        ds.m_SolderMaskExpansion = MM(0.05)
        ns = ds.m_NetSettings
        for name, c in NETCLASSES.items():
            nc = ns.GetDefaultNetclass() if name == 'Default' else pcbnew.NETCLASS(name)
            nc.SetClearance(MM(c['clearance'])); nc.SetTrackWidth(MM(c['track_width']))
            nc.SetViaDiameter(MM(c['via_diameter'])); nc.SetViaDrill(MM(c['via_drill']))
            if name != 'Default':
                ns.SetNetclass(name, nc)
        for pattern in POWER_NETS:
            ns.SetNetclassPatternAssignment(pattern, 'Power')

    def net(self, name):
        if name not in self.nets:
            ni = pcbnew.NETINFO_ITEM(self.b, name)
            self.b.Add(ni)
            self.nets[name] = ni
        return self.nets[name]

    # -- placement --
    def place(self, lib, name, ref, value, at, rot=0, back=False, nets=None, sym_uuid=None,
              sheetfile=None, fields=None, dnp=False, hide_value=True):
        libpath = self.projlib if lib == 'samplooler' else f'{SYSFP}/{lib}.pretty'
        fp = pcbnew.FootprintLoad(libpath, name)
        if fp is None:
            raise KeyError(f'{lib}:{name}')
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.Value().SetVisible(not hide_value)
        self.b.Add(fp)
        fp.SetPosition(V(*at))
        fp.SetOrientationDegrees(rot)
        if back:
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        if sym_uuid:
            fp.SetPath(pcbnew.KIID_PATH('/' + sym_uuid))
            fp.SetSheetname('/')
            fp.SetSheetfile(sheetfile or '')
        for k, v in (fields or {}).items():
            fp.SetField(k, v)
            fld = fp.GetField(k)
            if fld:
                fld.SetVisible(False)
        if dnp:
            fp.SetDNP(True)
            fp.SetExcludedFromPosFiles(True)
        for p in fp.Pads():
            num = p.GetNumber()
            if not num:
                continue
            if nets is None or num not in nets:
                raise KeyError(f'{ref} pad {num} has no net')
            if nets[num]:
                p.SetNet(self.net(nets[num]))
        self.fps[ref] = fp
        return fp

    def pad_pos(self, ref, num):
        for p in self.fps[ref].Pads():
            if p.GetNumber() == str(num):
                q = p.GetPosition()
                return (pcbnew.ToMM(q.x), pcbnew.ToMM(q.y))
        raise KeyError((ref, num))

    # -- outline and cutouts --
    def _seg(self, a, b, layer=pcbnew.Edge_Cuts, w=0.1):
        s = pcbnew.PCB_SHAPE(self.b)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(layer); s.SetStart(V(*a)); s.SetEnd(V(*b)); s.SetWidth(MM(w))
        self.b.Add(s)

    def _arc(self, c, start, angle_deg, layer=pcbnew.Edge_Cuts, w=0.1):
        s = pcbnew.PCB_SHAPE(self.b)
        s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetLayer(layer); s.SetWidth(MM(w))
        s.SetCenter(V(*c)); s.SetStart(V(*start))
        s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(angle_deg, pcbnew.DEGREES_T), True)
        self.b.Add(s)

    def outline(self, x0, y0, x1, y1, r=3.0):
        self._seg((x0 + r, y0), (x1 - r, y0))
        self._seg((x1, y0 + r), (x1, y1 - r))
        self._seg((x1 - r, y1), (x0 + r, y1))
        self._seg((x0, y1 - r), (x0, y0 + r))
        self._arc((x1 - r, y0 + r), (x1 - r, y0), 90)
        self._arc((x1 - r, y1 - r), (x1, y1 - r), 90)
        self._arc((x0 + r, y1 - r), (x0 + r, y1), 90)
        self._arc((x0 + r, y0 + r), (x0, y0 + r), 90)
        self.bounds = (x0, y0, x1, y1)

    def rrect_cut(self, x0, y0, x1, y1, r=1.0):
        """A rounded-rectangle opening inside the outline (a screen window)."""
        self._seg((x0 + r, y0), (x1 - r, y0))
        self._seg((x1, y0 + r), (x1, y1 - r))
        self._seg((x1 - r, y1), (x0 + r, y1))
        self._seg((x0, y1 - r), (x0, y0 + r))
        self._arc((x1 - r, y0 + r), (x1 - r, y0), 90)
        self._arc((x1 - r, y1 - r), (x1, y1 - r), 90)
        self._arc((x0 + r, y1 - r), (x0 + r, y1), 90)
        self._arc((x0 + r, y0 + r), (x0, y0 + r), 90)

    def circle_cut(self, c, d):
        s = pcbnew.PCB_SHAPE(self.b)
        s.SetShape(pcbnew.SHAPE_T_CIRCLE)
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
        s.SetCenter(V(*c)); s.SetEnd(V(c[0] + d / 2, c[1]))
        self.b.Add(s)

    def silk_rect(self, a, b, layer=pcbnew.F_SilkS, w=0.15):
        s = pcbnew.PCB_SHAPE(self.b)
        s.SetShape(pcbnew.SHAPE_T_RECT)
        s.SetLayer(layer); s.SetWidth(MM(w))
        s.SetStart(V(*a)); s.SetEnd(V(*b))
        self.b.Add(s)

    def silk_line(self, a, b, layer=pcbnew.F_SilkS, w=0.15):
        self._seg(a, b, layer, w)

    def text(self, s, at, size=1.0, layer=pcbnew.F_SilkS, rot=0, bold=False, justify=None):
        t = pcbnew.PCB_TEXT(self.b)
        t.SetText(s)
        t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
        t.SetTextThickness(MM(size * (0.2 if bold else 0.15)))
        t.SetPosition(V(*at))
        t.SetTextAngleDegrees(rot)
        if layer in (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Fab):
            t.SetMirrored(True)
        if justify == 'left':
            t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
        elif justify == 'right':
            t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
        self.b.Add(t)
        return t

    # -- pours --
    def pour(self, net, layer, margin=0.3, priority=0):
        x0, y0, x1, y1 = self.bounds
        z = pcbnew.ZONE(self.b)
        z.SetLayer(layer)
        z.SetNet(self.net(net))
        z.SetAssignedPriority(priority)
        z.SetLocalClearance(MM(0.3))
        z.SetMinThickness(MM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(MM(0.4))
        z.SetThermalReliefSpokeWidth(MM(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        ol = z.Outline()
        ol.NewOutline()
        for p in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            ol.Append(V(*p))
        self.b.Add(z)
        return z

    def fill(self):
        filler = pcbnew.ZONE_FILLER(self.b)
        filler.Fill(self.b.Zones())

    # -- routing --
    def track(self, net, pts, width, layer=pcbnew.F_Cu):
        for a, b in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(self.b)
            t.SetStart(V(*a)); t.SetEnd(V(*b)); t.SetWidth(MM(width)); t.SetLayer(layer)
            t.SetNet(self.net(net))
            self.b.Add(t)

    def via(self, net, at, d=0.6, drill=0.3):
        v = pcbnew.PCB_VIA(self.b)
        v.SetPosition(V(*at)); v.SetWidth(MM(d)); v.SetDrill(MM(drill))
        v.SetNet(self.net(net))
        self.b.Add(v)

    def autoroute(self, workdir, passes=60, keep_prerouted=True):
        self.b.BuildConnectivity()
        dsn = os.path.join(workdir, 'route.dsn')
        ses = os.path.join(workdir, 'route.ses')
        if os.path.exists(ses):
            os.remove(ses)
        assert pcbnew.ExportSpecctraDSN(self.b, dsn)
        r = subprocess.run([FREEROUTING, '-de', dsn, '-do', ses, '-mp', str(passes), '--gui.enabled=false'],
                           capture_output=True, text=True, timeout=900)
        log = r.stdout + r.stderr
        final = [l for l in log.splitlines() if 'final score' in l or 'unrouted' in l]
        if not os.path.exists(ses):
            raise RuntimeError('freerouting produced no session:\n' + log[-3000:])
        assert pcbnew.ImportSpecctraSES(self.b, ses)
        return final[-1] if final else ''

    def save(self):
        self.b.BuildConnectivity()
        self.b.Save(self.path)
