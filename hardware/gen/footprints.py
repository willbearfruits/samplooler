"""Footprints for the breakout modules: 2.54 mm headers soldered through the carrier.

Each module (see modules.py) is described by its header pads, its PCB outline and any
features worth drawing (jack, USB, mic port). Coordinates are in mm, footprint frame
(Y down), origin at pad 1. The outline goes on F.Fab and the silkscreen, pin names on the
silkscreen so the board can be assembled without the schematic.
"""
from sexpr import Sym, dump
from schematic import uid
from modules import HDR_DRILL, HDR_PAD

S = Sym


def _font(size=1.0, thick=None, justify=None, hide=False):
    e = [S('effects'), [S('font'), [S('size'), size, size], [S('thickness'), thick or round(size * 0.15, 3)]]]
    if justify:
        e.append([S('justify')] + [S(j) for j in justify.split()])
    if hide:
        e.append([S('hide'), S('yes')])
    return e


def _stroke(w):
    return [S('stroke'), [S('width'), w], [S('type'), S('solid')]]


def _line(a, b, layer, w):
    return [S('fp_line'), [S('start'), *a], [S('end'), *b], _stroke(w), [S('layer'), layer],
            [S('uuid'), uid('fpl', a, b, layer)]]


def _rect(a, b, layer, w):
    return [S('fp_rect'), [S('start'), *a], [S('end'), *b], _stroke(w), [S('fill'), S('no')],
            [S('layer'), layer], [S('uuid'), uid('fpr', a, b, layer)]]


def _circle(c, r, layer, w):
    return [S('fp_circle'), [S('center'), *c], [S('end'), c[0] + r, c[1]], _stroke(w), [S('fill'), S('no')],
            [S('layer'), layer], [S('uuid'), uid('fpc', c, r, layer)]]


def _prop(kind, s, at, layer, size, hide=False):
    return [S('property'), kind, s, [S('at'), at[0], at[1], 0], [S('layer'), layer],
            [S('uuid'), uid('fpp', kind, s, at)], _font(size, hide=hide)]


def _text(s, at, layer, size=0.8, justify=None, rot=0):
    return [S('fp_text'), S('user'), s, [S('at'), at[0], at[1], rot], [S('layer'), layer],
            [S('uuid'), uid('fpt', s, at, layer)], _font(size, justify=justify)]


def _draw(items, layer, w):
    out = []
    for e in items:
        kind = e[0]
        if kind == 'rect':
            out.append(_rect(e[1], e[2], layer, w))
        elif kind == 'line':
            out.append(_line(e[1], e[2], layer, w))
        elif kind == 'circle':
            out.append(_circle(e[1], e[2], layer, w))
        elif kind == 'text':
            out.append(_text(e[1], e[2], layer, e[3] if len(e) > 3 else 0.8, rot=e[4] if len(e) > 4 else 0))
        elif kind == 'text_back' and layer == 'F.SilkS':     # pinout printed on the far side
            t = _text(e[1], e[2], 'B.SilkS', e[3] if len(e) > 3 else 0.8, rot=e[4] if len(e) > 4 else 0)
            t[-1].append([S('justify'), S('mirror')])
            out.append(t)
    return out


def _text_w(s, size):
    return 0.72 * size * len(s)


def _label(pname, x, y, side, pad, ls, off=None):
    """A pin name beside its pad, centred so KiCad's keep-upright flip cannot move it onto the pad.
    `off`: distance from the pad centre to the label's near edge (default just past the pad)."""
    off = pad / 2 + 0.45 if off is None else off
    w = _text_w(pname, ls)
    if side == 'right':
        return _text(pname, (x + off + w / 2, y), 'F.SilkS', ls)
    if side == 'left':
        return _text(pname, (x - off - w / 2, y), 'F.SilkS', ls)
    sgn = -1 if side in ('up', 'above') else 1
    if side in ('above', 'below') or len(pname) <= 2:
        return _text(pname, (x, y + sgn * (off + ls / 2 + 0.05)), 'F.SilkS', ls)
    return _text(pname, (x, y + sgn * (off + w / 2)), 'F.SilkS', ls, rot=90)


def module_footprint(m):
    name = m['name']
    pads = m['pads']
    fp = [S('footprint'), name, [S('version'), 20241229], [S('generator'), 'samplooler_gen'],
          [S('generator_version'), '1.0'], [S('layer'), 'F.Cu'], [S('descr'), m['desc']],
          [S('tags'), 'module breakout header']]
    o = m['outline']
    if o[0] == 'rect':
        _, x0, y0, x1, y1 = o
        cx, top, bot = (x0 + x1) / 2, y0, y1
        fp += [_rect((x0, y0), (x1, y1), 'F.Fab', 0.1),
               _rect((x0 - 0.25, y0 - 0.25), (x1 + 0.25, y1 + 0.25), 'F.CrtYd', 0.05)]
        if m.get('silk_outline', True):
            fp.append(_rect((x0, y0), (x1, y1), 'F.SilkS', 0.12))
    else:
        _, ccx, ccy, r = o
        cx, top, bot = ccx, ccy - r, ccy + r
        fp += [_circle((ccx, ccy), r, 'F.Fab', 0.1), _circle((ccx, ccy), r, 'F.SilkS', 0.12),
               _circle((ccx, ccy), r + 0.25, 'F.CrtYd', 0.05)]
    ref_at = m.get('ref_at', (cx, (top + bot) / 2 - 2.2) if m.get('silk_outline', True) else (cx, -3.6))
    fp.append(_prop('Reference', 'REF**', ref_at, 'F.SilkS', 1.0))
    fp.append(_prop('Value', name, (cx, bot + 1.2), 'F.Fab', 0.8))
    fp.append(_text('${REFERENCE}', (cx, (top + bot) / 2 + 2.5), 'F.Fab', 1.0))
    fp += _draw(m.get('silk', []), 'F.SilkS', 0.12)
    fp += _draw(m.get('fab', []), 'F.Fab', 0.1)

    xs = [p[1] for p in pads]
    xmid = (min(xs) + max(xs)) / 2
    big = set(m.get('big_drill', ()))
    drills = m.get('drills', {})
    for num, x, y, pname in pads:
        drill, size = drills.get(num, (1.2, 1.9) if num in big else (HDR_DRILL, HDR_PAD))
        shape = 'rect' if num == 1 else 'circle'
        fp.append([S('pad'), str(num), S('thru_hole'), S(shape), [S('at'), x, y], [S('size'), size, size],
                   [S('drill'), drill], [S('layers'), '*.Cu', '*.Mask'], [S('remove_unused_layers'), S('no')],
                   [S('pinfunction'), pname], [S('pintype'), 'passive'], [S('uuid'), uid('pad', name, num)]])
        ls = m.get('label_side', {})
        side = ls.get(num, 'auto')
        if side is None:                      # labelled some other way (e.g. one label per row)
            continue
        if side == 'auto':
            side = 'right' if x <= xmid else 'left'
        fp.append(_label(pname, x, y, side, size, m.get('label_size', 0.8), m.get('label_off')))
    for num, x, y, pname, w, h in m.get('smd', ()):
        fp.append([S('pad'), str(num), S('smd'), S('roundrect'), [S('at'), x, y], [S('size'), w, h],
                   [S('layers'), 'F.Cu', 'F.Paste', 'F.Mask'], [S('roundrect_rratio'), 0.2],
                   [S('pinfunction'), pname], [S('pintype'), 'passive'], [S('uuid'), uid('smd', name, num)]])
        fp.append(_text(pname.replace('GPIO', ''), (x, y), 'F.Fab', 0.5))
    for i, (x, y, d) in enumerate(m.get('holes', ())):
        fp.append([S('pad'), '', S('np_thru_hole'), S('circle'), [S('at'), x, y], [S('size'), d, d],
                   [S('drill'), d], [S('layers'), '*.Cu', '*.Mask'], [S('uuid'), uid('hole', name, i)]])
    k = 1 / 2.54      # KiCad reads VRML in 0.1 inch units
    fp.append([S('model'), '${KIPRJMOD}/../lib/samplooler.3dshapes/' + name + '.wrl',
               [S('offset'), [S('xyz'), 0, 0, 0]], [S('scale'), [S('xyz'), k, k, k]],
               [S('rotate'), [S('xyz'), 0, 0, 0]]])
    fp.append([S('embedded_fonts'), S('no')])
    return fp


def write(path, fp):
    open(path, 'w').write(dump(fp) + '\n')
