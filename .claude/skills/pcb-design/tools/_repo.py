"""Shared by the tools: find the repo, put hardware/gen on the path, load a board by its folder name."""
import glob
import os
import subprocess
import sys

ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True,
                      cwd=os.path.dirname(os.path.abspath(__file__))).stdout.strip()
HW = os.path.join(ROOT, 'hardware')
sys.path.insert(0, os.path.join(HW, 'gen'))
OX, OY = 50.0, 50.0            # kit.ORIGIN: every board sits here on the KiCad page


def board(name):
    import pcbnew
    return pcbnew.LoadBoard(glob.glob(os.path.join(HW, name, '*.kicad_pcb'))[0])


def mm(v):
    import pcbnew
    return round(pcbnew.ToMM(v.x) - OX, 3), round(pcbnew.ToMM(v.y) - OY, 3)
