#!/usr/bin/env python3
"""Build both Samplooler carrier boards from nothing: library, schematics, PCBs, fab files.

    python3 hardware/gen/build.py            # everything
    python3 hardware/gen/build.py --no-route # skip Freerouting (keeps placement only)

Two KiCad projects come out, one per board of the stack:
  hardware/bottom/  modules: ESP32-S3 SuperMini, GY-PCM5102, INMP441, GY-521, KY-023, amp header
  hardware/top/     the panel: SSD1306 OLED, EC11 encoder, joystick opening
They meet through a 2x4 2.54 mm socket (bottom) and header (top, mounted underneath),
which with standard 8.5 mm sockets sets the gap: 11 mm M3 standoffs.
"""
import argparse
import csv
import json
import math
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import modules as M                                   # noqa: E402
import footprints                                     # noqa: E402
from schematic import Sheet, module_symbol, write_symbol_lib, uid   # noqa: E402

HW = os.path.dirname(HERE)
LIB = os.path.join(HW, 'lib')
REV = 'A'
DATE = '2026-09-25'
GAP = 11.0          # standoff length = socket 8.5 + header plastic 2.5
PCB_T = 1.6

# ============================================================== library

SYMBOLS = {  # module -> (left pads, right pads, ref, pin-name override)
    'ESP32-S3_SuperMini': (range(1, 10), range(10, 19), 'U', M.SUPERMINI_PINS),
    'GY-PCM5102': (range(1, 7), range(7, 16), 'U', None),
    'INMP441_Round': ((1, 2, 3), (4, 5, 6), 'U', None),
    'GY-521_MPU6050': (range(1, 9), (), 'U', None),
    'KY-023_Joystick': (range(1, 6), (), 'U', None),
    'MAX98357A_Breakout': (range(1, 8), (), 'J', None),
    'SSD1306_0.96in_I2C': (range(1, 5), (), 'U', None),
}


def build_library():
    import models
    os.makedirs(os.path.join(LIB, 'samplooler.pretty'), exist_ok=True)
    os.makedirs(os.path.join(LIB, 'samplooler.3dshapes'), exist_ok=True)
    syms = {}
    open(os.path.join(LIB, 'samplooler.3dshapes', 'EC11_Vertical_Knob.wrl'), 'w').write(models.ec11_knob().vrml())
    for m in M.MODULES:
        open(os.path.join(LIB, 'samplooler.3dshapes', m['name'] + '.wrl'), 'w').write(
            models.BUILDERS[m['name']](M).vrml())
        footprints.write(os.path.join(LIB, 'samplooler.pretty', m['name'] + '.kicad_mod'),
                         footprints.module_footprint(m))
        left, right, ref, names = SYMBOLS[m['name']]
        pn = {n: nm for (n, x, y, nm) in m['pads']}
        if names:
            pn.update(names)
        syms[m['name']] = module_symbol(
            m['name'], [(n, pn[n], 'passive') for n in left], [(n, pn[n], 'passive') for n in right],
            ref=ref, footprint='samplooler:' + m['name'], desc=m['desc'])
    write_symbol_lib(os.path.join(LIB, 'samplooler.kicad_sym'), list(syms.values()))
    return syms


# ============================================================== netlists
# Each part: ref -> dict(lib=symbol lib_id, fp=footprint lib_id, value, nets={pin: net|None}, extra)

FP_C = 'Capacitor_SMD:C_0805_2012Metric'
FP_R = 'Resistor_SMD:R_0805_2012Metric'
FP_HOLE = 'MountingHole:MountingHole_3.2mm_M3_Pad_Via'
FP_SJ_OPEN = 'Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm'
FP_SJ_B12 = 'Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm_NumberLabels'
LCSC = {'10u': 'C15850', '100n': 'C49678', '4k7': 'C17673'}


def cap(value, a, b='GND'):
    return dict(lib='Device:C', fp=FP_C, value=value, nets={'1': a, '2': b},
                fields={'LCSC': LCSC[value]})


def hole():
    return dict(lib='Mechanical:MountingHole_Pad', fp=FP_HOLE, value='M3', nets={'1': 'GND'}, in_bom=False)


def mod(name, nets, value=None):
    return dict(lib='samplooler:' + name, fp='samplooler:' + name, value=value or name,
                nets={str(k): v for k, v in nets.items()})


LINK = {1: '+3V3', 2: 'GND', 3: 'I2C_SDA', 4: 'I2C_SCL', 5: 'ENC_A', 6: 'ENC_B', 7: 'ENC_SW', 8: 'GND'}

BOTTOM = {
    'U1': mod('ESP32-S3_SuperMini', {
        1: None, 2: None, 3: 'JOY_Y', 4: 'JOY_X', 5: 'JOY_SW', 6: 'ENC_A', 7: 'ENC_B', 8: 'ENC_SW',
        9: 'I2S_BCLK', 10: '+5V', 11: 'GND', 12: '+3V3', 13: 'I2C_SCL', 14: 'I2C_SDA', 15: 'AMP_SD',
        16: 'I2S_DIN', 17: 'I2S_DOUT', 18: 'I2S_WS'}),
    'U2': mod('GY-PCM5102', {
        1: 'GND', 2: 'I2S_BCLK', 3: 'I2S_DOUT', 4: 'I2S_WS', 5: 'GND', 6: '+5V',
        7: 'DAC_FLT', 8: 'DAC_DEMP', 9: 'DAC_XSMT', 10: 'DAC_FMT', 11: 'DAC_3V3', 12: 'GND',
        13: None, 14: 'GND', 15: None}, value='PCM5102A'),
    'U3': mod('INMP441_Round', {1: 'I2S_BCLK', 2: 'I2S_WS', 3: 'GND', 4: 'I2S_DIN', 5: '+3V3', 6: 'GND'},
              value='INMP441'),
    'U4': mod('GY-521_MPU6050', {1: '+3V3', 2: 'GND', 3: 'I2C_SCL', 4: 'I2C_SDA', 5: None, 6: None,
                                 7: 'GND', 8: None}, value='MPU6050'),
    'U5': mod('KY-023_Joystick', {1: 'GND', 2: '+3V3', 3: 'JOY_X', 4: 'JOY_Y', 5: 'JOY_SW'}, value='KY-023'),
    'J2': mod('MAX98357A_Breakout', {1: 'I2S_WS', 2: 'I2S_BCLK', 3: 'I2S_DOUT', 4: None, 5: 'AMP_SD',
                                     6: 'GND', 7: '+5V'}, value='MAX98357A (opt)'),
    'J1': dict(lib='Connector_Generic:Conn_02x04_Odd_Even',
               fp='Connector_PinSocket_2.54mm:PinSocket_2x04_P2.54mm_Vertical', value='To top board',
               nets={str(k): v for k, v in LINK.items()}),
    'JP1': dict(lib='Jumper:SolderJumper_3_Open', fp=FP_SJ_OPEN, value='FLT', in_bom=False, nets={'1': 'DAC_3V3', '2': 'DAC_FLT', '3': 'GND'}),
    'JP2': dict(lib='Jumper:SolderJumper_3_Open', fp=FP_SJ_OPEN, value='DEMP', in_bom=False, nets={'1': 'DAC_3V3', '2': 'DAC_DEMP', '3': 'GND'}),
    'JP3': dict(lib='Jumper:SolderJumper_3_Open', fp=FP_SJ_OPEN, value='XSMT', in_bom=False, nets={'1': 'DAC_3V3', '2': 'DAC_XSMT', '3': 'GND'}),
    'JP4': dict(lib='Jumper:SolderJumper_3_Open', fp=FP_SJ_OPEN, value='FMT', in_bom=False, nets={'1': 'DAC_3V3', '2': 'DAC_FMT', '3': 'GND'}),
    'C1': cap('10u', '+5V'),
    'C2': cap('10u', '+3V3'),
    'C3': cap('100n', '+3V3'),
    'C4': cap('100n', '+3V3'),
    'R1': dict(lib='Device:R', fp=FP_R, value='4k7', nets={'1': '+3V3', '2': 'I2C_SDA'}, dnp=True,
               fields={'LCSC': LCSC['4k7']}),
    'R2': dict(lib='Device:R', fp=FP_R, value='4k7', nets={'1': '+3V3', '2': 'I2C_SCL'}, dnp=True,
               fields={'LCSC': LCSC['4k7']}),
    'H1': hole(), 'H2': hole(), 'H3': hole(), 'H4': hole(),
}

TOP = {
    'J1': dict(lib='Connector_Generic:Conn_02x04_Odd_Even',
               fp='Connector_PinHeader_2.54mm:PinHeader_2x04_P2.54mm_Vertical', value='To bottom board',
               nets={str(k): v for k, v in LINK.items()}),
    'U1': mod('SSD1306_0.96in_I2C', {1: 'OLED_P1', 2: 'OLED_P2', 3: 'I2C_SCL', 4: 'I2C_SDA'}, value='OLED 0.96in'),
    'JP1': dict(lib='Jumper:SolderJumper_3_Bridged12', fp=FP_SJ_B12, value='OLED pin 1', in_bom=False,
                nets={'1': 'GND', '2': 'OLED_P1', '3': '+3V3'}),
    'JP2': dict(lib='Jumper:SolderJumper_3_Bridged12', fp=FP_SJ_B12, value='OLED pin 2', in_bom=False,
                nets={'1': '+3V3', '2': 'OLED_P2', '3': 'GND'}),
    'SW1': dict(lib='Device:RotaryEncoder_Switch_MP',
                fp='Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm', value='EC11',
                nets={'A': 'ENC_A', 'B': 'ENC_B', 'C': 'GND', 'S1': 'ENC_SW', 'S2': 'GND', 'MP': 'GND'}),
    'C1': cap('10u', '+3V3'),
    'C2': cap('100n', '+3V3'),
    'H1': hole(), 'H2': hole(), 'H3': hole(), 'H4': hole(),
}


# ============================================================== schematics

def schematic(name, parts, layout, notes, title, syms, comments):
    sh = Sheet(name, title, REV, DATE, comments)
    for box in layout.get('boxes', []):
        sh.box(*box)
    for ref, (x, y, *rest) in layout['parts'].items():
        p = parts[ref]
        sym = syms.get(p['lib'].split(':')[1]) if p['lib'].startswith('samplooler:') else None
        sh.place(p['lib'], ref, p['value'], (x, y), p['nets'], rot=rest[0] if rest else 0,
                 footprint=p['fp'], sym=sym, fields=p.get('fields'), dnp=p.get('dnp', False),
                 in_bom=p.get('in_bom', True))
    for net, at in layout.get('flags', []):
        sh.pwr_flag(net, at)
    for text, at, *size in notes:
        sh.text(text, at, *(size or [1.27]))
    return sh


def sym_uuid(project, ref):
    return uid(project, 'sym', ref)


BOTTOM_SCH = dict(
    boxes=[((15, 25), (140, 135), 'ESP32-S3 SuperMini'),
           ((150, 25), (290, 135), 'DAC  (GY-PCM5102, line out)'),
           ((300, 25), (405, 80), 'Microphone  (INMP441, I2S)'),
           ((300, 88), (405, 160), 'Tilt  (GY-521 / MPU-6050, I2C 0x68)'),
           ((15, 143), (140, 205), 'Joystick  (KY-023)'),
           ((150, 143), (290, 205), 'Speaker amp header  (optional MAX98357A)'),
           ((15, 213), (140, 280), 'To the top board  (2x4 socket, 8.5 mm)'),
           ((150, 213), (290, 280), 'Supply decoupling, I2C pull-ups'),
           ((300, 168), (405, 280), 'Mounting  (M3, 11 mm standoffs)')],
    parts={
        'U1': (77.47, 78.74),
        'U2': (200.66, 71.12), 'JP1': (247.65, 48.26, 270), 'JP2': (278.13, 48.26, 270), 'JP3': (247.65, 93.98, 270), 'JP4': (278.13, 93.98, 270),
        'U3': (353.06, 55.88),
        'U4': (353.06, 127.0),
        'U5': (80.01, 177.8),
        'J2': (220.98, 177.8),
        'J1': (73.66, 248.92),
        'C1': (170.18, 238.76), 'C2': (185.42, 238.76), 'C3': (200.66, 238.76), 'C4': (215.9, 238.76),
        'R1': (243.84, 238.76), 'R2': (259.08, 238.76),
        'H1': (320.04, 187.96), 'H2': (337.82, 187.96), 'H3': (355.6, 187.96), 'H4': (373.38, 187.96),
    },
    flags=[('+5V', (160.02, 266.7)), ('+3V3', (193.04, 266.7)), ('GND', (226.06, 266.7))],
)
BOTTOM_NOTES = [
    ('USB-C on the SuperMini powers everything: its 5V pin feeds the DAC (and amp),\n'
     'its 3.3 V regulator feeds the mic, IMU, joystick, OLED and encoder.', (20, 125), 1.27),
    ('JP1-JP4 stand in for the H1L-H4L bridges on the back of the\n'
     'DAC board, out of reach once it is mounted. Leave them OPEN\n'
     'if those bridges are set. Otherwise bridge FLT=L, DEMP=L,\n'
     'XSMT=H (unmute), FMT=L.  H is DAC_3V3, the module\'s own\n'
     '3.3 V regulator output, never the board 3V3.', (155, 110), 1.27),
    ('Sound hole UP: the metal can faces the carrier.\nL/R to GND: data in the left slot.', (305, 68), 1.27),
    ('AD0 to GND: address 0x68.  The chip\'s +X axis points\nto the nose (the USB / jack edge).', (305, 145), 1.27),
    ('The "+5V" pin goes to 3V3: the ESP32 ADC\nreads 0-3.3 V.  SW uses the internal pull-up.', (20, 190), 1.27),
    ('GAIN floating = 9 dB.  SD (GPIO11) low = amp off.\nVIN from USB 5V.', (155, 192), 1.27),
    ('R1, R2 not fitted: the OLED and GY-521 boards carry\nI2C pull-ups. Fit them only if neither does.', (155, 258), 1.27),
    ('Holes tie both boards\' ground pours\nthrough metal standoffs.', (305, 205), 1.27),
    ('Stack: bottom board, 11 mm M3 standoffs,\ntop board. The 2x4 socket (8.5 mm) and the\n'
     'top board\'s header (2.5 mm plastic) mate at\nexactly that height.', (305, 225), 1.27),
]

TOP_SCH = dict(
    boxes=[((15, 25), (140, 110), 'From the bottom board  (2x4 header, mounted underneath)'),
           ((150, 25), (300, 110), 'OLED  (SSD1306 0.96", I2C 0x3C)'),
           ((15, 118), (140, 190), 'Encoder  (EC11 with push)'),
           ((150, 118), (300, 190), 'Decoupling and mounting')],
    parts={
        'J1': (73.66, 67.31),
        'U1': (243.84, 60.96), 'JP1': (177.8, 50.8), 'JP2': (177.8, 76.2),
        'SW1': (73.66, 152.4),
        'C1': (170.18, 147.32), 'C2': (185.42, 147.32),
        'H1': (215.9, 142.24), 'H2': (233.68, 142.24), 'H3': (251.46, 142.24), 'H4': (269.24, 142.24),
    },
    flags=[('+3V3', (170.02, 175.26)), ('GND', (205.74, 175.26))],
)
TOP_NOTES = [
    ('Most 4-pin OLEDs are GND-VCC-SCL-SDA: JP1/JP2 come bridged 1-2 for that.\n'
     'For a VCC-GND-SCL-SDA module cut both 1-2 bridges and solder 2-3 on both.', (155, 92), 1.27),
    ('C and S2 to GND; A, B and the switch use the ESP32\'s\ninternal pull-ups. Mounting lugs to GND.', (20, 172), 1.27),
]


# ============================================================== project files

def write_project(dirpath, name):
    os.makedirs(dirpath, exist_ok=True)
    pro = json.load(open(os.path.join(os.environ.get('KICAD10_TEMPLATE_DIR', '/usr/share/kicad/template'),
                                      'kicad.kicad_pro')))
    pro['meta']['filename'] = name + '.kicad_pro'
    json.dump(pro, open(os.path.join(dirpath, name + '.kicad_pro'), 'w'), indent=2)
    rel = os.path.relpath(LIB, dirpath)
    open(os.path.join(dirpath, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n  (version 7)\n'
        f'  (lib (name "samplooler")(type "KiCad")(uri "${{KIPRJMOD}}/{rel}/samplooler.kicad_sym")(options "")(descr "Samplooler modules"))\n'
        + ''.join(f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_SYMBOL_DIR}}/{n}.kicad_sym")(options "")(descr ""))\n'
                  for n in ('Device', 'power', 'Connector_Generic', 'Jumper', 'Mechanical')) + ')\n')
    open(os.path.join(dirpath, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n  (version 7)\n'
        f'  (lib (name "samplooler")(type "KiCad")(uri "${{KIPRJMOD}}/{rel}/samplooler.pretty")(options "")(descr "Samplooler modules"))\n'
        + ''.join(f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_FOOTPRINT_DIR}}/{n}.pretty")(options "")(descr ""))\n'
                  for n in ('Capacitor_SMD', 'Resistor_SMD', 'MountingHole', 'Jumper', 'Rotary_Encoder',
                            'Connector_PinHeader_2.54mm', 'Connector_PinSocket_2.54mm')) + ')\n')


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    out = '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'assert' not in l and 'PROPERTY_ENUM' not in l)
    return r.returncode, out


def netlist(sch):
    """Pad nets exactly as eeschema names them (/LOCAL, GLOBAL, unconnected-(...))."""
    from sexpr import parse, find, find_all
    out = sch.replace('.kicad_sch', '.net')
    code, log = run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', out, sch])
    assert code == 0, log
    nets = {}
    for n in find_all(find(parse(open(out).read()), 'nets'), 'net'):
        name = find(n, 'name')[1]
        for node in find_all(n, 'node'):
            nets.setdefault(find(node, 'ref')[1], {})[find(node, 'pin')[1]] = name
    return nets


def erc(sch):
    rpt = sch.replace('.kicad_sch', '-erc.rpt')
    run(['kicad-cli', 'sch', 'erc', '--severity-all', '-o', rpt, sch])
    lines = open(rpt).read().splitlines()
    errs = [l for l in lines if l.startswith('[')]
    return errs, rpt



# ============================================================== PCBs

import pcbnew                                          # noqa: E402
import layout as L                                     # noqa: E402
from pcb import MM, V, items                           # noqa: E402

A = L._abs


def strip_net_from_dsn(path, net):
    """Freerouting routes what the DSN lists; GND is left to the pours."""
    t = open(path).read()
    i = t.find(f'(net {net}\n')
    if i < 0:
        i = t.find(f'(net {net} ')
    if i >= 0:
        depth, j = 0, i
        while True:
            c = t[j]
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        t = t[:i] + t[j + 1:]
    t = t.replace(f' {net}\n', '\n').replace(f' {net} ', ' ')
    # route 0.01 mm wider apart than DRC checks, so Freerouting's rounding never lands on the limit
    import re as _re
    t = _re.sub(r'\(clearance (\d+(?:\.\d+)?)', lambda m: f'(clearance {float(m.group(1)) + 10:g}', t)
    open(path, 'w').write(t)


def autoroute(bd, workdir, passes=100):
    import os as _os
    bd.b.BuildConnectivity()
    dsn = _os.path.join(workdir, 'route.dsn')
    ses = _os.path.join(workdir, 'route.ses')
    for f in (dsn, ses):
        if _os.path.exists(f):
            _os.remove(f)
    assert pcbnew.ExportSpecctraDSN(bd.b, dsn)
    strip_net_from_dsn(dsn, 'GND')
    from pcb import FREEROUTING
    r = subprocess.run([FREEROUTING, '-de', dsn, '-do', ses, '-mp', str(passes), '--gui.enabled=false'],
                       capture_output=True, text=True, timeout=1800)
    log = r.stdout + r.stderr
    open(_os.path.join(workdir, 'freerouting.log'), 'w').write(log)
    if not _os.path.exists(ses):
        raise RuntimeError('freerouting produced no session, see freerouting.log')
    assert pcbnew.ImportSpecctraSES(bd.b, ses)
    final = [l for l in log.splitlines() if 'final score' in l]
    return final[-1].split('INFO', 1)[-1].strip() if final else '(no score line)'


def pours(bd):
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        bd.pour('GND', layer)
    bd.fill()


def silk(bd, s, at, size=1.0, layer=pcbnew.F_SilkS, **kw):
    return bd.text(s, A(at), size, layer, **kw)


def silk_arrow_up(bd, x, y, layer=pcbnew.F_SilkS):
    """A small filled-looking arrowhead pointing at the nose edge."""
    for a, b in (((x - 0.9, y + 0.8), (x, y - 0.4)), ((x, y - 0.4), (x + 0.9, y + 0.8)),
                 ((x - 0.9, y + 0.8), (x + 0.9, y + 0.8))):
        bd.silk_line(A(a), A(b), layer, 0.2)


def build_bottom_pcb(route=True):
    d = os.path.join(HW, 'bottom')
    path = os.path.join(d, 'samplooler-bottom.kicad_pcb')
    bd = L.build_board(path, os.path.join(LIB, 'samplooler.pretty'), BOTTOM, L.BOTTOM_PLACE, L.BOTTOM_SMALL,
                       'samplooler-bottom', 'samplooler-bottom.kicad_sch', 'bottom', NETS['samplooler-bottom'])
    # keep copper away from the SuperMini's chip antenna
    (ax0, ay0), (ax1, ay1) = M.SUPERMINI['antenna']
    o = L.BOTTOM_PLACE['U1'][1]
    # no pour under the chip antenna (no radio in use today, BLE-MIDI tomorrow); traces may pass
    L.keepout(bd, (o[0] + ax0 + 0.7, o[1] + ay0), (o[0] + ax1 - 0.7, o[1] + ay1), tracks=False, vias=False,
              name='antenna')
    L.add_power_routes(bd)

    # ---- silkscreen, front: what faces the nose, how the modules go on
    jx = L.at(L.BOTTOM_PLACE['U2'][1], (24.5, 0), 180)[0]
    usb_x = L.BOTTOM_PLACE['U1'][1][0] + M.SM_ROW / 2
    mic = L.BOTTOM_PLACE['U3'][1]
    silk(bd, 'SOUND HOLE UP', (mic[0], 21.3), 0.8)
    for (x, name) in ((32.725, 'FLT'), (27.645, 'XSMT')):
        silk(bd, name, (x, 18.6), 0.8)
    for (x, name) in ((30.185, 'DEMP'), (25.105, 'FMT')):
        silk(bd, name, (x, 25.9), 0.8)
    silk(bd, 'DAC straps', (9.6, 19.6), 0.8, justify='left')
    silk(bd, '1-2 = H  2-3 = L', (9.6, 21.1), 0.8, justify='left')
    silk(bd, 'open if set on', (9.6, 22.6), 0.8, justify='left')
    silk(bd, 'the DAC board', (9.6, 24.1), 0.8, justify='left')
    silk(bd, '<- top board', (49.2, 51.0), 0.8, justify='left')
    silk(bd, 'I2C pull-ups:', (35.6, 53.4), 0.8, justify='left')
    silk(bd, 'fit only if no', (35.6, 54.8), 0.8, justify='left')
    silk(bd, 'module has them', (35.6, 56.2), 0.8, justify='left')
    silk(bd, 'AMP (opt)', (86.3, 30.2), 0.8)
    silk(bd, 'chip +X', (72.5, 45.6), 0.8)
    silk(bd, 'to nose', (72.5, 47.0), 0.8)
    silk_arrow_up(bd, 76.6, 45.9)
    # ---- back: identity, stack, and where JLC may print its order number
    B = pcbnew.B_SilkS
    silk(bd, 'SAMPLOOLER', (63.0, 51.5), 3.0, B, bold=True)
    silk(bd, f'bottom board  rev {REV}  {DATE}', (63.0, 55.0), 1.2, B)
    silk(bd, 'stack: 11 mm M3 standoffs, 2x4 socket 8.5 mm', (63.0, 57.2), 1.0, B)
    silk(bd, 'willbearfruits / samplooler', (63.0, 59.2), 1.0, B)
    silk(bd, 'JLCJLCJLCJLC', (21.0, 45.0), 1.0, B)
    bd.save()
    return bd, d


def build_top_pcb(route=True):
    d = os.path.join(HW, 'top')
    path = os.path.join(d, 'samplooler-top.kicad_pcb')
    bd = L.build_board(path, os.path.join(LIB, 'samplooler.pretty'), TOP, L.TOP_PLACE, L.TOP_SMALL,
                       'samplooler-top', 'samplooler-top.kicad_sch', 'top', NETS['samplooler-top'])
    # the stock EC11 STEP model is not in every KiCad install: use ours (with a knob)
    sw = bd.fps['SW1']
    ms = sw.Models()
    while len(ms):
        ms.pop()
    m3 = pcbnew.FP_3DMODEL()
    m3.m_Filename = '${KIPRJMOD}/../lib/samplooler.3dshapes/EC11_Vertical_Knob.wrl'
    m3.m_Scale.x = m3.m_Scale.y = m3.m_Scale.z = 1 / 2.54
    ms.append(m3)
    bd.circle_cut(A(L.STICK), L.STICK_HOLE_D)
    bd.circle_cut(A(L.BOOT_AT), L.ACCESS_D)
    bd.circle_cut(A(L.RST_AT), L.ACCESS_D)

    jx = L.at(L.BOTTOM_PLACE['U2'][1], (24.5, 0), 180)[0]
    usb_x = L.BOTTOM_PLACE['U1'][1][0] + M.SM_ROW / 2
    mic = L.BOTTOM_PLACE['U3'][1]
    # nose edge: what is under it
    for x, name in ((jx, 'LINE OUT'), (mic[0], 'MIC'), (usb_x, 'USB-C')):
        silk(bd, name, (x, 2.2), 1.0)
    silk(bd, 'BOOT', (L.BOOT_AT[0], L.BOOT_AT[1] + 3.6), 0.9)
    silk(bd, 'RST', (L.RST_AT[0], L.RST_AT[1] + 3.6), 0.9)
    silk(bd, 'tilt this edge down: faster', (20.0, 5.0), 0.9)
    silk_arrow_up(bd, 20.0, 7.3)
    # controls
    sx, sy = L.STICK
    silk(bd, 'click: tilt on/off   hold: menu', (sx + 1.5, sy + L.STICK_HOLE_D / 2 + 2.2), 0.9)
    ex, ey = L.ENC_SHAFT
    silk(bd, 'turn: value', (ex, ey + 9.4), 0.9)
    silk(bd, 'hold: record', (ex, ey + 11.0), 0.9)
    silk(bd, 'OLED pin order', (36.6, 32.7), 0.8, justify='left')
    silk(bd, 'GND VCC: as is', (36.6, 37.7), 0.8, justify='left')
    silk(bd, 'VCC GND: cut 1-2, bridge 2-3', (36.6, 39.1), 0.8, justify='left')
    silk(bd, 'SAMPLOOLER', (45.0, 57.5), 2.4, bold=True)
    silk(bd, f'rev {REV}', (45.0, 60.6), 0.9)
    B = pcbnew.B_SilkS
    silk(bd, 'SAMPLOOLER top board', (45.0, 22.0), 1.6, B, bold=True)
    silk(bd, f'rev {REV}  {DATE}', (45.0, 24.4), 1.0, B)
    silk(bd, 'J1 header goes on THIS side, pins down', (45.0, 41.0), 0.9, B)
    silk(bd, 'JLCJLCJLCJLC', (72.0, 60.0), 1.0, B)
    bd.save()
    return bd, d


def finish(bd, d, name, route):
    report = {}
    if route:
        report['route'] = autoroute(bd, d)
    pours(bd)
    avoid = [((L.BOTTOM_PLACE['U1'][1][0], 20.0), (L.BOTTOM_PLACE['U1'][1][0] + M.SM_ROW, 24.5))] \
        if name == 'bottom' else [((L.STICK[0] - 14, L.STICK[1] - 14), (L.STICK[0] + 14, L.STICK[1] + 14))]
    circles = [] if name == 'bottom' else [(*L.STICK, L.STICK_HOLE_D / 2), (*L.BOOT_AT, L.ACCESS_D / 2),
                                           (*L.RST_AT, L.ACCESS_D / 2)]
    added = L.stitch(bd, pitch=8.0, avoid=avoid, avoid_circles=circles)
    bd.fill()
    pruned = L.prune_vias(bd)
    bridged, left = L.bridge_islands(bd)
    report['stitch'] = f'{len(added) - pruned} grid vias, {len(bridged)} island vias, {left} still unconnected'
    pcbnew.SaveBoard(bd.path, bd.b, False)
    return report


def drc(pcb):
    rpt = pcb.replace('.kicad_pcb', '-drc.rpt')
    # The pcbnew Python filler runs without the DRC engine, so it ignores edge and hole clearances.
    # Let kicad-cli refill with the real rules and SAVE that fill: the Gerbers are plotted from it.
    run(['kicad-cli', 'pcb', 'drc', '--refill-zones', '--save-board', '-o', rpt, pcb])
    run(['kicad-cli', 'pcb', 'drc', '--schematic-parity', '--severity-all', '-o', rpt, pcb])
    text = open(rpt).read()
    found = [l for l in text.splitlines() if l.startswith('[')]
    return found, rpt



# ============================================================== fab outputs

FAB = os.path.join(HW, 'fab')


def fab_outputs(which, parts):
    """Gerbers + drill zipped for JLCPCB, JLC-format BOM/CPL for the SMD caps, schematic PDF."""
    name = f'samplooler-{which}'
    d = os.path.join(HW, which)
    pcb = os.path.join(d, name + '.kicad_pcb')
    gdir = os.path.join(FAB, name + '-gerbers')
    if os.path.isdir(gdir):
        shutil.rmtree(gdir)
    os.makedirs(gdir)
    code, log = run(['kicad-cli', 'pcb', 'export', 'gerbers', '-o', gdir,
                     '-l', 'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts',
                     '--no-x2', '--no-netlist', '--subtract-soldermask', '--check-zones', pcb])
    assert code == 0, log
    code, log = run(['kicad-cli', 'pcb', 'export', 'drill', '-o', gdir + '/', '--format', 'excellon',
                     '--excellon-units', 'mm', '--excellon-zeros-format', 'decimal',
                     '--excellon-oval-format', 'alternate', '--excellon-separate-th',
                     '--generate-map', '--map-format', 'gerberx2', pcb])
    assert code == 0, log
    zpath = os.path.join(FAB, name + '-jlcpcb.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)

    # BOM for JLC SMT: only parts with an LCSC number that are fitted
    groups = {}
    for ref, p in parts.items():
        lcsc = (p.get('fields') or {}).get('LCSC')
        if not lcsc or p.get('dnp'):
            continue
        key = (p['value'], p['fp'].split(':')[1], lcsc)
        groups.setdefault(key, []).append(ref)
    with open(os.path.join(FAB, name + '-bom.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (val, fpn, lcsc), refs in sorted(groups.items()):
            w.writerow([val, ','.join(sorted(refs)), fpn, lcsc])
    pos = os.path.join(FAB, name + '-pos-kicad.csv')
    code, log = run(['kicad-cli', 'pcb', 'export', 'pos', '-o', pos, '--format', 'csv', '--units', 'mm',
                     '--side', 'both', '--smd-only', '--exclude-dnp', pcb])
    assert code == 0, log
    with open(pos) as f, open(os.path.join(FAB, name + '-cpl.csv'), 'w', newline='') as g:
        r = csv.DictReader(f)
        w = csv.writer(g)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        for row in r:
            if not (parts.get(row['Ref'], {}).get('fields') or {}).get('LCSC'):
                continue
            w.writerow([row['Ref'], row['PosX'] + 'mm', row['PosY'] + 'mm',
                        'Top' if row['Side'] == 'top' else 'Bottom', row['Rot']])
    os.remove(pos)
    code, log = run(['kicad-cli', 'sch', 'export', 'pdf', '-o', os.path.join(FAB, name + '-schematic.pdf'),
                     os.path.join(d, name + '.kicad_sch')])
    assert code == 0, log
    return zpath


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--no-route', action='store_true')
    ap.add_argument('--only', choices=['bottom', 'top'])
    args = ap.parse_args()

    syms = build_library()
    NETS = {}
    for name, parts, lay, notes, title, comments in (
            ('samplooler-bottom', BOTTOM, BOTTOM_SCH, BOTTOM_NOTES, 'Samplooler bottom board: modules',
             ('ESP32-S3 SuperMini carrier: DAC, mic, IMU, joystick, amp header',
              'Mates with samplooler-top through J1 (2x4, 11 mm stack)')),
            ('samplooler-top', TOP, TOP_SCH, TOP_NOTES, 'Samplooler top board: panel',
             ('OLED, encoder and the opening for the joystick',
              'Mates with samplooler-bottom through J1 (header on the underside)'))):
        if args.only and not name.endswith(args.only):
            continue
        d = os.path.join(HW, name.split('-')[1])
        write_project(d, name)
        sh = schematic(name, parts, lay, notes, title, syms, comments)
        path = os.path.join(d, name + '.kicad_sch')
        sh.save(path)
        errs, rpt = erc(path)
        NETS[name] = netlist(path)
        print(f'{name}: ERC {len(errs)} findings ({rpt})')
        for e in errs:
            print('   ', e)

    probs = L.mechanical_check()
    print(f'mechanical check: {len(probs)} problems')
    for p in probs:
        print('   ', p)
    for which, fn in (('bottom', build_bottom_pcb), ('top', build_top_pcb)):
        if args.only and which != args.only:
            continue
        bd, d = fn()
        pcbnew.SaveBoard(bd.path, bd.b, False)
        rep = finish(bd, d, which, not args.no_route)
        found, rpt = drc(bd.path)
        print(f'samplooler-{which}: {rep}  DRC {len(found)} findings ({rpt})')
        for f in found[:40]:
            print('   ', f)
        os.makedirs(FAB, exist_ok=True)
        print('   fab:', os.path.relpath(fab_outputs(which, BOTTOM if which == 'bottom' else TOP), HW))
