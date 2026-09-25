#!/usr/bin/env python3
"""pads.py <board-dir> [ref ...]: pad positions (board frame, mm), side, type and net."""
import sys
import _repo
import pcbnew

b = _repo.board(sys.argv[1])
refs = set(sys.argv[2:])
for fp in b.Footprints():
    if refs and fp.GetReference() not in refs:
        continue
    side = 'B' if fp.IsFlipped() else 'F'
    for p in fp.Pads():
        x, y = _repo.mm(p.GetPosition())
        kind = 'smd' if p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD else 'tht'
        print(f'{fp.GetReference():5s}{side} {p.GetNumber():>3s} {x:8.2f} {y:8.2f}  {kind}  {p.GetNetname()}')
