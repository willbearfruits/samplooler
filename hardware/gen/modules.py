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
    silk=[('text', 'ESP32-S3', (SM_ROW / 2, 9.5), 1.0), ('text', 'SuperMini', (SM_ROW / 2, 11.2), 0.8),
          ('text', 'USB', (SM_ROW / 2, 0.6), 0.8)],
    # keep copper pour away from the antenna end (module-local rect)
    antenna=((0.6, 18.4), (SM_ROW - 0.6, 21.91)),
)
SUPERMINI_PINS = {  # pad -> symbol pin name
    1: 'GPIO43/TX', 2: 'GPIO44/RX', 3: 'GPIO1', 4: 'GPIO2', 5: 'GPIO3', 6: 'GPIO4', 7: 'GPIO5', 8: 'GPIO6',
    9: 'GPIO7', 10: '5V', 11: 'GND', 12: '3V3', 13: 'GPIO13', 14: 'GPIO12', 15: 'GPIO11', 16: 'GPIO10',
    17: 'GPIO9', 18: 'GPIO8'}

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
)

# ---------------------------------------------------------------- GY-521 (MPU-6050)
GY521 = dict(
    name='GY-521_MPU6050',
    desc='GY-521 MPU-6050 breakout, 8-pin header on the long edge.',
    pads=col(0, 0, ['VCC', 'GND', 'SCL', 'SDA', 'XDA', 'XCL', 'AD0', 'INT']),
    outline=('rect', -1.3, -1.24, 14.3, 19.0),
    fab=[('circle', (11.6, 1.2), 1.5), ('circle', (11.6, 16.5), 1.5)],
    silk=[('text', 'MPU6050', (7.5, 13.0), 0.9)],
)

# ---------------------------------------------------------------- KY-023 joystick
KY023 = dict(
    name='KY-023_Joystick',
    desc='KY-023 joystick module, straight 5-pin header (replace the right-angle one), '
         'M3 holes for 2.5 mm spacers. Power it from 3V3: the ESP32 ADC is 3.3 V.',
    pads=col(0, 0, ['GND', '+5V', 'VRx', 'VRy', 'SW']),
    outline=('rect', -1.5, -8.0, 32.5, 18.0),
    holes=[(3.0, -5.25, 3.3), (29.5, -5.25, 3.3), (3.0, 15.25, 3.3), (29.5, 15.25, 3.3)],
    fab=[('circle', (15.5, 4.1), 8.0), ('text', 'STICK', (15.5, 4.1), 1.0)],
    silk=[('text', 'KY-023', (15.5, 12.5), 1.0), ('text', '+5V pin = 3V3', (15.5, 14.4), 0.8)],
    stick=(15.5, 4.1),
)

# ---------------------------------------------------------------- MAX98357A (Adafruit 3006 layout)
MAX98357 = dict(
    name='MAX98357A_Breakout',
    desc='Header for the optional MAX98357A I2S amp breakout (Adafruit 3006 pin order). '
         'The module lies over -Y; its speaker terminal is at the far edge.',
    pads=row(0, 0, ['LRC', 'BCLK', 'DIN', 'GAIN', 'SD', 'GND', 'VIN']),
    outline=('rect', -1.27, -16.51, 16.51, 2.54),
    silk_outline=False,            # the module overhangs the board edge: outline on F.Fab only
    fab=[('text', 'MAX98357A (opt)', (7.62, -8), 0.8)],
    silk=[],
    ref_at=(-3.2, 0),
    label_side={i: 'down' for i in range(1, 8)},
    label_size=0.8,
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
)

MODULES = [SUPERMINI, PCM5102, INMP441, GY521, KY023, MAX98357, OLED]
