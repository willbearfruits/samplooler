#!/usr/bin/env python3
"""nozone.py <in.kicad_pcb> <out.kicad_pcb>: a copy without the copper pours, to look at the routing."""
import sys
import pcbnew

b = pcbnew.LoadBoard(sys.argv[1])
for z in list(b.Zones()):
    if not z.GetIsRuleArea():
        b.Remove(z)
pcbnew.SaveBoard(sys.argv[2], b)
