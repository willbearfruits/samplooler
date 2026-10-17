"""The breakout modules: header geometry, outlines, pin names.

Frames follow the footprint convention: component side up, +X right, +Y down, origin at
pad 1, as the module is mounted on the carrier. Sources and confidence for each number
are in hardware/README.md. Where sources disagree the pads sit at the midpoint and the
drills are opened up so either variant goes in:
  - SuperMini row spacing: 15.24 in footprints, ~15.4 on nologo's S3 drawing -> 15.33, 1.1 mm drills.
  - GY-PCM5102 strap row: x offset 6.35..6.6, y -0.5..-0.8 -> midpoints, 1.2 mm drills.
"""

HDR_DRILL = 1.1
HDR_PAD = 1.8


def col(x, y0, names, first=1, step=2.54):
    return [(first + i, x, y0 + i * step, n) for i, n in enumerate(names)]


def row(y, x0, names, first=1, step=2.54):
    return [(first + i, x0 + i * step, y, n) for i, n in enumerate(names)]


# ---------------------------------------------------------------- ESP32-S3 SuperMini
SM_ROW = 15.33
SUPERMINI = dict(
    name='ESP32-S3_SuperMini',
    desc='ESP32-S3 SuperMini (nologo/Tenstar) on 2.54 mm headers, USB-C toward -Y. '
         'Row spacing 15.33 (15.24..15.45 fits the 1.1 mm drills).',
    pads=col(0, 0, ['TX', 'RX', '1', '2', '3', '4', '5', '6', '7'])
         + col(SM_ROW, 0, ['5V', 'GND', '3V3', '13', '12', '11', '10', '9', '8'], first=10),
    outline=('rect', -1.38, -1.59, SM_ROW + 1.38, 21.91),
    fab=[('rect', (SM_ROW / 2 - 4.5, -3.6), (SM_ROW / 2 + 4.5, 4.6)),       # USB-C, 2 mm past the edge
         ('rect', (2.1, 19.9), (8.2, 21.9)),                                # chip antenna
         ('text', 'USB-C', (SM_ROW / 2, 1.2), 0.8),
         ('text', 'ANT', (5.15, 20.9), 0.6)],
    ref_at=(SM_ROW / 2, 16.5),
    # the underside pads fill the space between the columns from y 5.3 on: pin names go outside,
    # except 5V and GND, which fit inside above the pads (outside, a Mini OLED pin sits there)
    silk=[('text', 'ESP32-S3', (SM_ROW / 2, 3.4), 0.8), ('text', 'USB', (SM_ROW / 2, 0.6), 0.8)],
    label_side={**{n: 'left' for n in range(1, 10)}, 10: 'left', 11: 'left',
                **{n: 'right' for n in range(12, 19)}},
    label_off=1.9,                     # clear of the module outline (1.38 from the pins)
    # keep copper pour away from the antenna end (module-local rect)
    antenna=((0.6, 18.4), (SM_ROW - 0.6, 21.91)),
    height=5.0,
    mech_extra=[('usb-c', (SM_ROW / 2 - 4.5, -3.6, SM_ROW / 2 + 4.5, 4.6), 3.5, 7.0, 'edge')],
    boot=(3.1, 7.0), rst=(SM_ROW - 3.1, 7.0),
)
# Pads on the SuperMini's underside, reached with pogo pins from the carrier (the module sits
# 2.5 mm up on its headers). nologo footprint + rectified board photo, +-0.05 mm, in the module's
# centreline frame so they stay centred whatever the true row spacing. Rows 1.753 mm apart.
# Column C's order (48 at the top) follows the silkscreen; nologo's schematic shows it reversed,
# so check with a meter (diode mode: the "48" pad to GND lights the module's red LED).
_POGO_Y = [round(6.03 + 1.753 * k, 3) for k in range(8)]
_POGO_COLS = [(-5.47, [48, 47, 46, 45, 42, 41, 40, 39]),     # column C, next to the TX column
              (+2.77, [38, 37, 36, 35]),                     # column B, rows 0-3
              (+5.46, [34, 33, 21, 18, 17, 16, 15, 14])]     # column A, next to the 5V column
POGO = []
for _dx, _gpios in _POGO_COLS:
    for _k, _g in enumerate(_gpios):
        POGO.append((19 + len(POGO), round(SM_ROW / 2 + _dx, 3), _POGO_Y[_k], f'GPIO{_g}'))
POGO_LAND = 1.5          # square land: fits Same Sky CPG-15 (1.35 base) on every pad, 2 mm-flange pins on alternate ones
SUPERMINI['pogo'] = POGO
SUPERMINI['smd'] = [(n, x, y, name, POGO_LAND, POGO_LAND) for n, x, y, name in POGO]

SUPERMINI_PINS = {  # pad -> symbol pin name
    1: 'GPIO43/TX', 2: 'GPIO44/RX', 3: 'GPIO1', 4: 'GPIO2', 5: 'GPIO3', 6: 'GPIO4', 7: 'GPIO5', 8: 'GPIO6',
    9: 'GPIO7', 10: '5V', 11: 'GND', 12: '3V3', 13: 'GPIO13', 14: 'GPIO12', 15: 'GPIO11', 16: 'GPIO10',
    17: 'GPIO9', 18: 'GPIO8'}
SUPERMINI_PINS.update({p[0]: p[3] + ' (pad)' for p in POGO})
SUPERMINI['sym'] = (list(range(1, 10)) + [p[0] for p in POGO[:8]],
                    list(range(10, 19)) + [p[0] for p in POGO[8:]], 'U', SUPERMINI_PINS)

# ---------------------------------------------------------------- GY-PCM5102 (purple)
DAC_ROW_X = 6.475
DAC_ROW_Y = -0.65
PCM5102 = dict(
    name='GY-PCM5102',
    desc='GY-PCM5102 purple I2S DAC: 6-pin I2S header plus the 9-pin strap/line row '
         '(FLT DEMP XSMT FMT A3V3 AGND ROUT AGND LROUT). Jack exits the +Y long edge.',
    pads=col(0, 0, ['SCK', 'BCK', 'DIN', 'LCK', 'GND', 'VIN'])
         + row(DAC_ROW_Y, DAC_ROW_X, ['FLT', 'DEMP', 'XSMT', 'FMT', 'A3V3', 'AGND', 'ROUT', 'AGND', 'LROUT'], first=7),
    big_drill=range(7, 16),
    label_side={n: 'down' for n in range(7, 16)},
    outline=('rect', -1.5, -2.15, 30.2, 14.85),
    fab=[('rect', (21.5, 2.4), (27.5, 16.3)), ('text', 'JACK', (24.5, 9.0), 0.8)],
    silk=[('text', 'PCM5102A', (13.0, 6.0), 1.0), ('text', 'DAC', (13.0, 8.0), 1.0),
          ('text', 'JACK', (24.5, 13.3), 0.8)],
    height=5.6,
    mech_extra=[('jack', (21.5, 2.4, 27.5, 16.3), 4.1, 9.6, 'edge')],
    jack=(24.5, 16.3),
)

# ---------------------------------------------------------------- INMP441
INMP441 = dict(
    name='INMP441_Round',
    desc='INMP441 round I2S MEMS mic, 2x3 pins, rows 7.62 apart. Mounted sound hole UP '
         '(label side up, the metal can faces the carrier).',
    pads=[(1, -2.54, -3.81, 'SCK'), (2, 0, -3.81, 'WS'), (3, 2.54, -3.81, 'L/R'),
          (4, -2.54, 3.81, 'SD'), (5, 0, 3.81, 'VDD'), (6, 2.54, 3.81, 'GND')],
    outline=('circle', 0, 0, 7.0),
    fab=[('circle', (0, 0), 0.75), ('text', 'PORT', (0, 1.4), 0.5)],
    ref_at=(8.4, 0),
    silk=[('circle', (0, 0), 0.9)],
    label_side={1: 'below', 2: 'below', 3: 'below', 4: 'above', 5: 'above', 6: 'above'},
    height=4.0,
)

# ---------------------------------------------------------------- GY-521 (MPU-6050)
GY521 = dict(
    name='GY-521_MPU6050',
    desc='GY-521 MPU-6050 breakout, 8-pin header on the long edge.',
    pads=col(0, 0, ['VCC', 'GND', 'SCL', 'SDA', 'XDA', 'XCL', 'AD0', 'INT']),
    outline=('rect', -1.3, -1.24, 14.3, 19.0),
    fab=[('circle', (11.6, 1.2), 1.5), ('circle', (11.6, 16.5), 1.5)],
    silk=[('text', 'MPU6050', (7.5, 13.0), 0.9)],
    height=5.3,
)

# ---------------------------------------------------------------- the KY-023's joystick, bare (rev D)
# The PS2-style thumb joystick that sits on the KY-023, soldered straight to the carrier. Origin at
# the stick. Pads from two footprints for the same part, which agree except on the switch rows:
#   Adafruit 2-axis joystick breakout (joystick.brd, package JOYSTICK)
#   SparkFun-Electromechanical.lbr (package JOYSTICK, for COM-09032)
# Pots at 2.54 mm, 10.16 from the stick, mounting tabs at (+-7.62, +-6.6675): both agree.
# Switch legs at x +-3.175 in both; rows at 8.001 / 12.501 (Adafruit) and 7.62 / 12.7 (SparkFun):
# midpoints, 1.2 mm drills. A row is one switch terminal (the legs 6.35 apart are joined inside, as on
# a 6 x 6 tact switch); Adafruit wires B1A and B2A, which proves those two are the two terminals, so
# only those are wired here and B1B/B2B are soldered for strength.
# Heights are estimates (no maker drawing found); MEASURE: see JOY_* below and the README.
JOY_ROW1, JOY_ROW2 = round((12.501 + 12.7) / 2, 3), round((8.001 + 7.62) / 2, 3)
JOY_FRAME_H = 10.5          # MEASURE: seat to the top of the metal frame
JOY_PIVOT = 6.0             # MEASURE: seat to the gimbal's pivot (the pots' shaft height)
JOY_CAP = (20.0, 28.0)      # MEASURE: seat to the underside and the top of the thumb cap
JOY_CAP_R = 9.75            # MEASURE: the cap's radius
JOY_THROW = 25.0            # degrees from upright, any direction
JOYSTICK = dict(
    name='PS2_Joystick',
    desc='Bare PS2-style thumb joystick (the part on the KY-023), soldered to the carrier. Origin at the stick. '
         'H = the pot along the switch-free side, V = the pot beside it; ends to 3V3 and GND, wipers out.',
    pads=[(1, -2.54, 10.16, 'H1'), (2, 0.0, 10.16, 'H2'), (3, 2.54, 10.16, 'H3'),
          (4, -10.16, -2.54, 'V1'), (5, -10.16, 0.0, 'V2'), (6, -10.16, 2.54, 'V3'),
          (7, -3.175, -JOY_ROW1, 'SW1'), (8, 3.175, -JOY_ROW1, 'SW1b'),
          (9, -3.175, -JOY_ROW2, 'SW2'), (10, 3.175, -JOY_ROW2, 'SW2b'),
          (11, -7.62, -6.6675, 'MP'), (12, 7.62, -6.6675, 'MP'), (13, 7.62, 6.6675, 'MP'), (14, -7.62, 6.6675, 'MP')],
    drills={**{n: (1.0, 1.7) for n in range(1, 7)}, **{n: (1.2, 1.9) for n in range(7, 11)},
            **{n: (1.5, 2.4) for n in range(11, 15)}},
    label_side={n: None for n in range(1, 15)},
    outline=('rect', -11.2, -14.25, 9.55, 11.45),
    fab=[('circle', (0, 0), JOY_CAP_R), ('text', 'STICK', (0, 0), 1.0)],
    silk=[],
    ref_at=(0, 3.5),
    stick=(0.0, 0.0),
    height=JOY_FRAME_H,
)


def joystick_mech():
    """The joystick's solids for mech.py: frame, pot and switch housings, and the volume the
    stick and cap sweep through, as a stack of rings (the cap tilted JOY_THROW every way)."""
    import math
    feats = [('frame', (-8.2, -7.3, 8.2, 7.3), 0, JOY_FRAME_H),
             ('pot H', (-4.85, 7.3, 4.85, 11.45), 0, 9.0),
             ('pot V', (-11.2, -4.85, -7.3, 4.85), 0, 9.0),
             ('switch', (-5.6, -14.25, 5.6, -6.95), 0, 4.0)]
    # sample the stick (r 2.5 up to the cap) and the cap at every tilt up to the throw
    env = {}
    for k in range(0, int(JOY_THROW) + 1):
        t = math.radians(k)
        for h10 in range(0, int((JOY_CAP[1] - JOY_PIVOT) * 10) + 1):
            h = h10 / 10
            r = JOY_CAP_R if h >= JOY_CAP[0] - JOY_PIVOT else 2.5
            for rho in (-r, r):
                z = JOY_PIVOT + h * math.cos(t) - rho * math.sin(t)
                x = abs(rho * math.cos(t) + h * math.sin(t))
                if z < JOY_FRAME_H:
                    continue
                band = math.floor(z * 2) / 2                     # 0.5 mm bands
                env[band] = max(env.get(band, 0.0), x)
    for band in sorted(env):
        feats.append(('stick sweep', ('circle', 0.0, 0.0, round(env[band], 2)), band, band + 0.5, 'edge'))
    return feats


JOYSTICK['mech_features'] = joystick_mech()


# ---------------------------------------------------------------- MAX98357A (Adafruit 3006 layout)
MAX98357 = dict(
    name='MAX98357A_Breakout',
    desc='Header for the optional MAX98357A I2S amp breakout (Adafruit 3006 pin order). '
         'The module lies over -Y; its speaker terminal is at the far edge.',
    pads=row(0, 0, ['LRC', 'BCLK', 'DIN', 'GAIN', 'SD', 'GND', 'VIN']),
    outline=('rect', -1.27, -16.51, 16.51, 2.54),
    silk_outline=False,            # the module overhangs the board edge: outline on F.Fab only
    overhang=True,
    fab=[('text', 'MAX98357A (opt)', (7.62, -8), 0.8)],
    silk=[],
    ref_at=(-3.2, 0),
    label_side={i: 'down' for i in range(1, 8)},
    label_size=0.8,
    height=5.5,
    # the optional amp itself: hangs off the edge, speaker terminal (8.5 mm tall) at its far end
    mech_extra=[('amp', (-1.27, -16.51, 16.51, 2.54), 2.5, 5.5, 'edge'),
                ('terminal', (-1.27, -16.51, 16.51, -9.5), 5.5, 11.0, 'edge')],
)

# ---------------------------------------------------------------- SSD1306 0.96" I2C
OLED = dict(
    name='SSD1306_0.96in_I2C',
    desc='0.96" 128x64 SSD1306 I2C OLED module, 4-pin header on the top edge. Pin 1/2 are '
         'GND/VCC on most modules, VCC/GND on some: JP1/JP2 on the board pick the order.',
    pads=row(0, 0, ['P1', 'P2', 'SCL', 'SDA']),
    outline=('rect', -9.84, -1.5, 17.46, 26.3),
    holes=[(-7.84, 0.5, 2.5), (15.46, 0.5, 2.5), (-7.84, 24.3, 2.5), (15.46, 24.3, 2.5)],
    fab=[('rect', (3.81 - 13.35, 2.6), (3.81 + 13.35, 21.9)), ('rect', (-7.06, 4.5), (14.68, 15.4))],
    silk=[('text', 'OLED 0.96"', (3.81, 10.0), 1.2), ('text', 'SSD1306 128x64', (3.81, 12.2), 0.8)],
    ref_at=(3.81, 17.0),
    label_side={1: 'down', 2: 'down', 3: 'down', 4: 'down'},
    height=5.6,
)

def breakout(name, value, labels, desc='Breakout pads, 2.54 mm, for headers or wires later.', rows=False,
             back_labels=False, labels_right=False, skip_rows=()):
    """labels: list of pin labels in header order (1, 2, 3, ...), two columns: odd left, even right.
    Pin 1 is square. Labels sit outside the columns so they stay readable with a header fitted;
    rows=True puts one "odd even" label per row on the odd side (for rows along a board edge)."""
    n = len(labels)
    rows = (n + 1) // 2
    pads = []
    side = {}
    for i, lab in enumerate(labels):
        num = i + 1
        x = 0.0 if num % 2 else 2.54
        y = 2.54 * (i // 2)
        pads.append((num, x, y, lab))
        side[num] = 'left' if num % 2 else 'right'
    n_rows = (n + 1) // 2
    silk = []
    if rows:
        side = {num: None for num, *_ in pads}
        for r in range(n_rows):
            if r in skip_rows:             # labelled on the board instead (no room beside this row)
                continue
            odd = labels[2 * r]
            even = labels[2 * r + 1] if 2 * r + 1 < n else ''
            text = f'{odd} {even}'.strip()
            w = 0.85 * 0.8 * len(text)
            x = (2.54 + 1.27 + 0.8 + w / 2) if labels_right else (-1.27 - 0.8 - w / 2)
            silk.append(('text_back' if back_labels else 'text', text, (x, 2.54 * r), 0.8))
    return dict(name=name, value=value, desc=desc, pads=pads, label_side=side,
                outline=('rect', -1.27, -1.27, 3.81, 2.54 * n_rows - 1.27), silk_outline=True,
                ref_at=(1.27, -2.4), fab=[], silk=silk, height=0.0, breakout=True)


# On the Mini every corner hole would land behind a module on the back: no screws, no holes.
OLED_NOHOLES = dict(OLED, name='SSD1306_0.96in_I2C_NoHoles', holes=[], silk_outline=False,
                    desc=OLED['desc'] + ' No mounting holes: held by its header (Mini).')


# ---------------------------------------------------------------- modules hung under a board (rev D top)
# The module hangs under the board with its front (glass, sound hole) facing the board: header
# plastic between the two, pins up through the board. Seen from above, its pads land exactly where
# they would for the same module on top. KiCad mirrors a back footprint's Y before rotating, so these
# variants are drawn with Y negated: placed on the back at rot 0 they come out as the front-side ones.
def _flip_y(items):
    out = []
    for e in items:
        if e[0] == 'rect':
            (x0, y0), (x1, y1) = e[1], e[2]
            out.append(('rect', (x0, -y1), (x1, -y0)) + tuple(e[3:]))
        elif e[0] in ('circle', 'line'):
            out.append((e[0], (e[1][0], -e[1][1])) + ((e[2][0], -e[2][1]) if e[0] == 'line' else e[2],) + tuple(e[3:]))
        elif e[0] in ('text', 'text_back'):
            out.append((e[0], e[1], (e[2][0], -e[2][1])) + tuple(e[3:]))
        else:
            out.append(e)
    return out


def under(m, name, desc, **kw):
    o = m['outline']
    outline = ('rect', o[1], -o[4], o[3], -o[2]) if o[0] == 'rect' else ('circle', o[1], -o[2], o[3])
    u = dict(m, name=name, desc=desc, outline=outline,
             pads=[(n, x, -y, nm) for n, x, y, nm in m['pads']],
             holes=[(x, -y, d) for x, y, d in m.get('holes', ())],
             fab=_flip_y(m.get('fab', ())), silk=_flip_y(m.get('silk', ())),
             label_side={n: None for n, *_ in m['pads']},      # hidden under the module anyway
             under=True)
    if 'ref_at' in m:
        u['ref_at'] = (m['ref_at'][0], -m['ref_at'][1])
    u.update(kw)
    return u


# OLED under the top board, glass up through a window. The header goes in from the glass side
# (plastic between glass side and board), so the glass sits 2.5 - 1.5 = 1.0 mm under the board.
OLED_GLASS_T = 1.5
OLED_UNDER = under(OLED, 'SSD1306_0.96in_I2C_Under',
                   'SSD1306 0.96in OLED hung under the board, glass up through a window: header soldered '
                   'with its plastic on the glass side. Pin order as seen from above: P1 P2 SCL SDA.',
                   silk=[], holes=[], height=5.0,           # plastic 2.5, module PCB 1.2, parts and pin stubs 1.3
                   mech_extra=[('glass', (3.81 - 13.35, -21.9, 3.81 + 13.35, -2.6), 2.5 - OLED_GLASS_T, 2.5)])
# the window over the active area, module frame (pin 1 at 0,0, Y down as mounted), with margin for
# the spread between makers (the active area's offset from the header varies ~0.7 mm)
OLED_AA = (-7.06, 4.5, 14.68, 15.4)
OLED_WIN_MARGIN = 1.5

# INMP441 under the top board, sound hole up against a port drilled through the board. Fit it flush
# (the header plastic below the module, on the can side) so no gap leaks round the port.
MIC_PORT_D = 1.5
INMP441_UNDER = under(INMP441, 'INMP441_Round_Under',
                      'INMP441 round mic hung under the board, sound hole up over a 1.5 mm port through the '
                      'board. Fit it flush: header plastic on the can side.',
                      holes=[(0.0, 0.0, MIC_PORT_D)], silk=[('circle', (0, 0), 1.2)],
                      # flush: module PCB against the board, the can and the trimmed pin stubs below it
                      mech_features=[('body', ('circle', 0.0, 0.0, 7.0), 0.0, 1.0),
                                     ('can', (-2.36, -1.88, 2.36, 1.88), 1.0, 2.0),
                                     ('pin stubs', (-3.5, -4.8, 3.5, 4.8), 1.0, 2.5)])

MODULES = [SUPERMINI, PCM5102, INMP441, GY521, JOYSTICK, MAX98357, OLED, OLED_UNDER, INMP441_UNDER]
