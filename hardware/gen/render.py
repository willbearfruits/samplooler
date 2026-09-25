#!/usr/bin/env python3
"""Renders for the README: each board, and the assembled stack.

KiCad renders one board at a time, so the stack is faked honestly: the top board is
exported as VRML (with all its parts) and hung, as a 3D model, 11 mm above a copy of the
bottom board that exists only for this render. Output: hardware/images/*.png
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pcbnew                    # noqa: E402
import layout as L               # noqa: E402

HW = os.path.dirname(HERE)
IMG = os.path.join(HW, 'images')
TMP = os.path.join(HW, '.stack')     # a sibling of bottom/ and top/, so ${KIPRJMOD}/../lib resolves


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)


def render(pcb, out, rotate, zoom=1.0, side='top', w=1800, h=1200, pan=None):
    cmd = ['kicad-cli', 'pcb', 'render', '--side', side, '--quality', 'high', '--width', str(w),
           '--height', str(h), '--background', 'opaque', '--perspective', '--zoom', str(zoom), '-o', out, pcb]
    if rotate:
        cmd[3:3] = ['--rotate', rotate]
    if pan:
        cmd[3:3] = ['--pan', pan]
    run(cmd)
    print(os.path.relpath(out, HW))


def stack_board():
    os.makedirs(TMP, exist_ok=True)
    top_wrl = os.path.join(TMP, 'top.wrl')
    run(['kicad-cli', 'pcb', 'export', 'vrml', '-f', '--units', 'tenths',
         '--user-origin', f'{L.ORIGIN[0]}x{L.ORIGIN[1]}mm', '-o', top_wrl,
         os.path.join(HW, 'top', 'samplooler-top.kicad_pcb')])
    b = pcbnew.LoadBoard(os.path.join(HW, 'bottom', 'samplooler-bottom.kicad_pcb'))
    fp = pcbnew.FOOTPRINT(b)
    fp.SetReference('STACK')
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetPosition(pcbnew.VECTOR2I_MM(*L.ORIGIN))
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = top_wrl
    m.m_Offset.z = L.GAP + 0.8          # the export centres the 1.6 mm board on z = 0
    fp.Models().append(m)
    b.Add(fp)
    path = os.path.join(TMP, 'stack.kicad_pcb')
    pcbnew.SaveBoard(path, b)
    return path


if __name__ == '__main__':
    os.makedirs(IMG, exist_ok=True)
    bot = os.path.join(HW, 'bottom', 'samplooler-bottom.kicad_pcb')
    top = os.path.join(HW, 'top', 'samplooler-top.kicad_pcb')
    render(bot, os.path.join(IMG, 'bottom-3d.png'), '-45,0,-20', 0.95)
    render(top, os.path.join(IMG, 'top-3d.png'), '-45,0,-20', 0.95)
    render(bot, os.path.join(IMG, 'bottom-plan.png'), None, 1.0)
    render(top, os.path.join(IMG, 'top-plan.png'), None, 1.0)
    stack = stack_board()
    render(stack, os.path.join(IMG, 'stack-3d.png'), '-55,0,-25', 0.9)
    render(stack, os.path.join(IMG, 'stack-nose.png'), '-80,0,180', 0.9)
