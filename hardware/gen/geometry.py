"""Placement geometry shared by every board.

KiCad places a footprint at `pos` with orientation `deg` (CCW positive, Y down). On the
back it first mirrors the footprint's Y, then rotates (measured in KiCad 10: a flipped
footprint maps local (x, y) to R(deg) . (x, -y)).
"""
import math


def rot(p, deg):
    x, y = p
    c, s = round(math.cos(math.radians(deg)), 9), round(math.sin(math.radians(deg)), 9)
    return (x * c + y * s, -x * s + y * c)


def xform(local, deg, side='front'):
    x, y = local
    if side == 'back':
        y = -y
    return rot((x, y), deg)


def at(origin, local, deg, side='front'):
    dx, dy = xform(local, deg, side)
    return (origin[0] + dx, origin[1] + dy)


def box_at(origin, box, deg, side='front'):
    """Axis-aligned bounds of a local box (x0, y0, x1, y1) after placement."""
    x0, y0, x1, y1 = box
    pts = [at(origin, p, deg, side) for p in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return (min(xs), min(ys), max(xs), max(ys))


def outline_box(m):
    o = m['outline']
    if o[0] == 'rect':
        return (o[1], o[2], o[3], o[4])
    return (o[1] - o[3], o[2] - o[3], o[1] + o[3], o[2] + o[3])


def origin_for(local_pt, deg, side, target):
    """The footprint origin that puts `local_pt` of the footprint on `target`."""
    dx, dy = xform(local_pt, deg, side)
    return (target[0] - dx, target[1] - dy)


def overlap(a, b, margin=0.0):
    return a[0] < b[2] + margin and b[0] < a[2] + margin and a[1] < b[3] + margin and b[1] < a[3] + margin


def dist_box_point(b, p):
    cx = min(max(p[0], b[0]), b[2])
    cy = min(max(p[1], b[1]), b[3])
    return math.hypot(cx - p[0], cy - p[1])


def box_in_circle(b, c, r):
    """True when every corner of box b lies inside the circle (c, r)."""
    return all(math.hypot(x - c[0], y - c[1]) <= r for x, y in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3])))
