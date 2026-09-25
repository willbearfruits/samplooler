#!/usr/bin/env python3
"""tracks.py <board-dir> <net> [...]: tracks and vias of the nets (names without the leading /)."""
import sys
import _repo
import kit

b = _repo.board(sys.argv[1])
want = set(sys.argv[2:])
for t in kit.items(b.Tracks()):
    n = t.GetNetname().lstrip('/')
    if n not in want:
        continue
    kind = 'via ' if t.GetClass() == 'PCB_VIA' else b.GetLayerName(t.GetLayer())
    print(f'{n:9s} {kind:5s} {_repo.mm(t.GetStart())} -> {_repo.mm(t.GetEnd())}  '
          f'w={t.GetWidth() / 1e6:.2f}  locked={t.IsLocked()}')
