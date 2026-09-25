#!/usr/bin/env python3
"""islands.py <board-dir>: GND pour islands not joined to the main planes, judged from the saved
fill with vias, GND tracks and plated/SMD GND pads as the joints. The DRC is the final judge; this
says where to look."""
import math
import sys
import _repo
import pcbnew
import kit
from pcb import MM

b = _repo.board(sys.argv[1])
live = kit._live_gnd(b)
ps = {l: live[l][0] for l in live}
main = {l: live[l][1] for l in live}
name = {pcbnew.F_Cu: 'F', pcbnew.B_Cu: 'B'}
parent = {}


def island(l, q):
    for i in range(ps[l].OutlineCount()):
        if ps[l].Contains(q, i):
            return i


def find(a):
    parent.setdefault(a, a)
    while parent[a] != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a


def union(a, c):
    parent[find(a)] = find(c)


for l in ps:
    for i in range(ps[l].OutlineCount()):
        find((l, i))
for t in kit.items(b.Tracks()):
    if t.GetNetname() != 'GND':
        continue
    if t.GetClass() == 'PCB_VIA':
        a, c = island(pcbnew.F_Cu, t.GetPosition()), island(pcbnew.B_Cu, t.GetPosition())
        if a is not None and c is not None:
            union((pcbnew.F_Cu, a), (pcbnew.B_Cu, c))
    else:
        l = t.GetLayer()
        a, c = island(l, t.GetStart()), island(l, t.GetEnd())
        if a is not None and c is not None:
            union((l, a), (l, c))
pads = []
for fp in b.Footprints():
    for p in fp.Pads():
        if p.GetNetname() != 'GND':
            continue
        c0 = p.GetPosition()
        r = pcbnew.ToMM(max(p.GetSize().x, p.GetSize().y)) / 2 + 0.6
        hit = set()
        for l in ps:
            if p.IsOnLayer(l):
                for k in range(16):
                    q = c0 + pcbnew.VECTOR2I(MM(r * math.cos(k * math.pi / 8)), MM(r * math.sin(k * math.pi / 8)))
                    i = island(l, q)
                    if i is not None:
                        hit.add((l, i))
        hit = list(hit)
        for h in hit[1:]:
            union(hit[0], h)
        if hit:
            pads.append((p, hit[0]))
root = find((pcbnew.F_Cu, main[pcbnew.F_Cu]))
print('main planes joined:', find((pcbnew.B_Cu, main[pcbnew.B_Cu])) == root)
for l in ps:
    for i in range(ps[l].OutlineCount()):
        if find((l, i)) != root:
            bb = ps[l].Outline(i).BBox()
            print(f'cut off: {name[l]}.Cu island {i}, {abs(ps[l].Outline(i).Area()) / 1e12:.1f} mm2, '
                  f'bbox {_repo.mm(bb.GetPosition())} .. {_repo.mm(bb.GetEnd())}')
for p, n in pads:
    if find(n) != root:
        print(f'  GND pad in a cut-off island: {p.GetParentFootprint().GetReference()}-{p.GetNumber()} '
              f'at {_repo.mm(p.GetPosition())}')
