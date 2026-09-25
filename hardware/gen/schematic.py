"""Write a KiCad schematic from a component list.

Every pin gets a short wire stub ending in a net label, a power symbol, or a no-connect
flag, so the sheet reads as blocks and the connectivity lives in the names. Stock symbols
are copied out of the system libraries into lib_symbols; the module symbols come from
the project library `samplooler.kicad_sym`, written here too.
"""
import os
import uuid as _uuid
from sexpr import Sym, parse, dump, find, find_all

SYMDIR = os.environ.get('KICAD10_SYMBOL_DIR', '/usr/share/kicad/symbols')
POWER_NETS = {'GND': 'power:GND', '+3V3': 'power:+3V3', '+5V': 'power:+5V'}
GRID = 2.54
SNAP = 1.27          # KiCad's connection grid (50 mil)
STUB = 5.08

_ns = _uuid.UUID('6f1c7a52-9b1e-4d0c-9a57-5a3b1e2c7d10')


def uid(*key):
    return str(_uuid.uuid5(_ns, '/'.join(map(str, key))))


def S(x):
    return Sym(x)


def font(size=1.27, **kw):
    f = [S('font'), [S('size'), size, size]]
    if kw.get('bold'):
        f.append([S('bold'), S('yes')])
    e = [S('effects'), f]
    if kw.get('justify'):
        e.append([S('justify')] + [S(j) for j in kw['justify'].split()])
    if kw.get('hide'):
        e.append([S('hide'), S('yes')])
    return e


# ---------------------------------------------------------------- library symbols

_libcache = {}


def stock_symbol(lib_id):
    lib, name = lib_id.split(':')
    if lib not in _libcache:
        _libcache[lib] = parse(open(f'{SYMDIR}/{lib}.kicad_sym').read())
    root = _libcache[lib]
    for s in find_all(root, 'symbol'):
        if s[1] == name:
            if find(s, 'extends'):
                raise ValueError(f'{lib_id} extends another symbol; pick the base')
            s = [S('symbol'), lib_id] + s[2:]
            return s
    raise KeyError(lib_id)


def module_symbol(name, left, right, ref='U', value=None, footprint='', desc=''):
    """A box with `left` and `right` pin lists of (number, name, type). Width fits the names."""
    n = max(len(left), len(right))
    longest = max([len(p[1]) for p in left + right] + [4])
    half_w = max(7.62, GRID * round((longest * 1.27 * 0.9 + 3) / GRID))
    top = GRID * ((n - 1) / 2 + 1)
    top = GRID * round(top / GRID)
    body = [S('symbol'), f'{name}_0_1',
            [S('rectangle'), [S('start'), -half_w, top], [S('end'), half_w, top - GRID * (n + 1)],
             [S('stroke'), [S('width'), 0.254], [S('type'), S('default')]],
             [S('fill'), [S('type'), S('background')]]]]
    pins = [S('symbol'), f'{name}_1_1']
    for side, lst in (('L', left), ('R', right)):
        for i, (num, pname, ptype) in enumerate(lst):
            y = top - GRID * (i + 1)
            x = -half_w - GRID if side == 'L' else half_w + GRID
            ang = 0 if side == 'L' else 180
            pins.append([S('pin'), S(ptype), S('line'), [S('at'), x, y, ang], [S('length'), GRID],
                         [S('name'), pname, font()], [S('number'), str(num), font()]])
    sym = [S('symbol'), name,
           [S('pin_names'), [S('offset'), 1.016]],
           [S('exclude_from_sim'), S('no')], [S('in_bom'), S('yes')], [S('on_board'), S('yes')],
           [S('property'), 'Reference', ref, [S('at'), -half_w, top + 1.27, 0], font(justify='left bottom')],
           [S('property'), 'Value', value or name, [S('at'), -half_w, top - GRID * (n + 1) - 1.27, 0], font(justify='left top')],
           [S('property'), 'Footprint', footprint, [S('at'), 0, 0, 0], font(hide=True)],
           [S('property'), 'Datasheet', '~', [S('at'), 0, 0, 0], font(hide=True)],
           [S('property'), 'Description', desc, [S('at'), 0, 0, 0], font(hide=True)],
           body, pins, [S('embedded_fonts'), S('no')]]
    return sym


def write_symbol_lib(path, symbols):
    lib = [S('kicad_symbol_lib'), [S('version'), 20241209], [S('generator'), 'samplooler_gen'],
           [S('generator_version'), '1.0']] + symbols
    open(path, 'w').write(dump(lib) + '\n')


# ---------------------------------------------------------------- pin geometry

def sym_pins(sym):
    """{number: (x, y, angle)} in library coordinates (Y up)."""
    out = {}

    def walk(node):
        for c in node:
            if isinstance(c, list) and c:
                if c[0] == 'pin':
                    at = find(c, 'at')
                    num = find(c, 'number')[1]
                    out[str(num)] = (float(at[1]), float(at[2]), int(float(at[3])) if len(at) > 3 else 0)
                elif c[0] == 'symbol':
                    walk(c)
    walk(sym)
    return out


def _rot(x, y, r):
    r %= 360
    return {0: (x, y), 90: (-y, x), 180: (-x, -y), 270: (y, -x)}[r]


def pin_world(inst_at, inst_rot, px, py, pang):
    """Connection point (schematic coords, Y down) and the outward unit direction."""
    rx, ry = _rot(px, py, inst_rot)
    wx, wy = inst_at[0] + rx, inst_at[1] - ry
    # the pin body points along `pang` from its tip; outward is the opposite direction
    ang = (pang + inst_rot + 180) % 360
    d = {0: (1, 0), 90: (0, -1), 180: (-1, 0), 270: (0, 1)}[ang]
    return (round(wx, 4), round(wy, 4)), d


# ---------------------------------------------------------------- sheet

class Sheet:
    def __init__(self, project, title, rev, date, comments=()):
        self.project = project
        self.root = uid(project, 'root')
        self.items = []
        self.libs = {}
        self.pwr_n = 0
        self.title = title
        self.rev = rev
        self.date = date
        self.comments = comments

    def _lib(self, lib_id, sym=None):
        if lib_id not in self.libs:
            self.libs[lib_id] = sym if sym is not None else stock_symbol(lib_id)
            if sym is not None:
                self.libs[lib_id] = [S('symbol'), lib_id] + sym[2:]
        return self.libs[lib_id]

    def _instance(self, lib_id, ref, value, at, rot=0, footprint='', fields=None, pins=(), dnp=False,
                  ref_at=None, val_at=None, hide_ref=False, hide_val=False, in_bom=True, on_board=True,
                  ref_just=None, val_just=None):
        fa = 90 if rot % 180 == 90 else 0      # keep field text horizontal on quarter-turned symbols
        props = [
            [S('property'), 'Reference', ref, [S('at'), *(ref_at or (at[0], at[1] - 2.54)), fa],
             font(hide=hide_ref, justify=ref_just)],
            [S('property'), 'Value', value, [S('at'), *(val_at or (at[0], at[1] + 2.54)), fa],
             font(hide=hide_val, justify=val_just)],
            [S('property'), 'Footprint', footprint, [S('at'), at[0], at[1], 0], font(hide=True)],
            [S('property'), 'Datasheet', '~', [S('at'), at[0], at[1], 0], font(hide=True)],
            [S('property'), 'Description', '', [S('at'), at[0], at[1], 0], font(hide=True)],
        ]
        for k, v in (fields or {}).items():
            props.append([S('property'), k, v, [S('at'), at[0], at[1], 0], font(hide=True)])
        node = [S('symbol'), [S('lib_id'), lib_id], [S('at'), at[0], at[1], rot], [S('unit'), 1],
                [S('exclude_from_sim'), S('no')], [S('in_bom'), S('yes' if in_bom else 'no')],
                [S('on_board'), S('yes' if on_board else 'no')], [S('dnp'), S('yes' if dnp else 'no')],
                [S('uuid'), uid(self.project, 'sym', ref)]] + props
        for p in pins:
            node.append([S('pin'), str(p), [S('uuid'), uid(self.project, 'pin', ref, p)]])
        node.append([S('instances'), [S('project'), self.project,
                                      [S('path'), '/' + self.root, [S('reference'), ref], [S('unit'), 1]]]])
        self.items.append(node)

    def wire(self, a, b):
        self.items.append([S('wire'), [S('pts'), [S('xy'), *a], [S('xy'), *b]],
                           [S('stroke'), [S('width'), 0], [S('type'), S('default')]],
                           [S('uuid'), uid(self.project, 'wire', a, b)]])

    def label(self, name, at, d):
        ang = {(-1, 0): 180, (1, 0): 0, (0, -1): 90, (0, 1): 270}[d]
        just = 'right bottom' if ang in (180, 270) else 'left bottom'
        self.items.append([S('label'), name, [S('at'), at[0], at[1], ang], font(justify=just),
                           [S('uuid'), uid(self.project, 'label', name, at)]])

    def no_connect(self, at):
        self.items.append([S('no_connect'), [S('at'), *at], [S('uuid'), uid(self.project, 'nc', at)]])

    def power(self, net, at, d=None):
        """Power symbol with its pin on `at`, pointing along `d` (the stub direction, away from
        the pin). Default: GND points down, supplies and PWR_FLAG up."""
        self.pwr_n += 1
        at = (round(round(at[0] / SNAP) * SNAP, 4), round(round(at[1] / SNAP) * SNAP, 4))
        lib_id = POWER_NETS[net] if net in POWER_NETS else net
        self._lib(lib_id)
        is_gnd = net == 'GND'
        if d is None:
            d = (0, 1) if is_gnd else (0, -1)
        # symbol graphic points down (GND) or up (supplies) at rotation 0; CCW 90 turns down->right
        base = (0, 1) if is_gnd else (0, -1)
        order = [(0, 1), (1, 0), (0, -1), (-1, 0)]          # down, right, up, left: CCW steps
        rot = ((order.index(d) - order.index(base)) % 4) * 90
        reach = 5.08 if is_gnd else 3.81
        label = net if lib_id != 'power:PWR_FLAG' else 'PWR_FLAG'
        if d[0]:                                   # sideways: centred text clear of the arrow
            reach += 0.9 + 0.6 * len(label)
        vx, vy = at[0] + d[0] * reach, at[1] + d[1] * reach
        just = None
        if d == (0, 1):
            vy += 0.6
        ref = f'#PWR{self.pwr_n:02d}' if lib_id != 'power:PWR_FLAG' else f'#FLG{self.pwr_n:02d}'
        self._instance(lib_id, ref, net if lib_id != 'power:PWR_FLAG' else 'PWR_FLAG', at, rot,
                       pins=['1'], hide_ref=True, val_at=(round(vx, 4), round(vy, 4)), val_just=just)

    def text(self, s, at, size=1.27, bold=False):
        self.items.append([S('text'), s, [S('exclude_from_sim'), S('no')], [S('at'), at[0], at[1], 0],
                           font(size, bold=bold, justify='left top'), [S('uuid'), uid(self.project, 'text', s[:40], at)]])

    def box(self, a, b, title=None):
        self.items.append([S('rectangle'), [S('start'), *a], [S('end'), *b],
                           [S('stroke'), [S('width'), 0.254], [S('type'), S('dash')]],
                           [S('fill'), [S('type'), S('none')]], [S('uuid'), uid(self.project, 'box', a, b)]])
        if title:
            self.text(title, (a[0] + 1.27, a[1] + 1.27), 1.8, bold=True)

    def place(self, lib_id, ref, value, at, nets, rot=0, footprint='', sym=None, fields=None, dnp=False,
              stub=STUB, ref_at=None, val_at=None, in_bom=True):
        """Place a symbol and hang a label / power symbol / NC flag off each pin.
        `nets` maps pin number -> net name (None = no connect)."""
        at = (round(round(at[0] / SNAP) * SNAP, 4), round(round(at[1] / SNAP) * SNAP, 4))
        s = self._lib(lib_id, sym)
        geo = sym_pins(s)
        fpos = {}
        for p in find_all(s, 'property'):
            if p[1] in ('Reference', 'Value'):
                a = find(p, 'at')
                rx, ry = _rot(float(a[1]), float(a[2]), rot)
                eff = find(p, 'effects')
                j = find(eff, 'justify') if eff else None
                just = ' '.join(str(x) for x in j[1:]) if j and rot == 0 else None
                fpos[p[1]] = ((round(at[0] + rx, 4), round(at[1] - ry, 4)), just)
        self._instance(lib_id, ref, value, at, rot, footprint, fields, pins=sorted(geo), dnp=dnp, in_bom=in_bom,
                       ref_at=ref_at or fpos.get('Reference', (None, None))[0],
                       val_at=val_at or fpos.get('Value', (None, None))[0],
                       ref_just=fpos.get('Reference', (None, None))[1], val_just=fpos.get('Value', (None, None))[1])
        for num, (px, py, pa) in geo.items():
            net = nets.get(num, 'NC?')
            if net == 'NC?':
                raise KeyError(f'{ref} pin {num} has no net assignment')
            tip, d = pin_world(at, rot, px, py, pa)
            if net is None:
                self.no_connect(tip)
                continue
            end = (round(tip[0] + d[0] * stub, 4), round(tip[1] + d[1] * stub, 4))
            self.wire(tip, end)
            if net in POWER_NETS:
                self.power(net, end, d)
            else:
                self.label(net, end, d)

    def pwr_flag(self, net, at):
        """PWR_FLAG tied to a net via a short wire and a power symbol."""
        at = (round(round(at[0] / SNAP) * SNAP, 4), round(round(at[1] / SNAP) * SNAP, 4))
        self.power(net, at)
        flag = (at[0] + 10.16, at[1])
        self.wire(at, flag)
        self.power('power:PWR_FLAG', flag)

    def save(self, path, paper='A3'):
        tb = [S('title_block'), [S('title'), self.title], [S('date'), self.date], [S('rev'), self.rev],
              [S('company'), 'Samplooler']]
        for i, c in enumerate(self.comments, 1):
            tb.append([S('comment'), i, c])
        doc = [S('kicad_sch'), [S('version'), 20250114], [S('generator'), 'eeschema'],
               [S('generator_version'), '9.0'], [S('uuid'), self.root], [S('paper'), paper], tb,
               [S('lib_symbols')] + list(self.libs.values())] + self.items + [
            [S('sheet_instances'), [S('path'), '/', [S('page'), '1']]], [S('embedded_fonts'), S('no')]]
        open(path, 'w').write(dump(doc) + '\n')
