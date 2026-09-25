"""The stack: two 90 x 64 mm boards, 11 mm apart on M3 standoffs.

bottom  every module, the joystick and the encoder (both reach up through the top board),
        pogo pins to the SuperMini's underside pads, breakout rows for every spare GPIO, Qwiic
top     the OLED and the microphone; openings for the stick, the encoder shaft, BOOT and RST
They meet through a 2x4 socket (bottom) and a header fitted under the top board: I2C for the
OLED and I2S for the mic.

Board frame: origin top-left, +X right, +Y toward the player. The top edge is the nose:
USB-C, the jack and the mic face it; the MPU-6050's +X axis points at it.
"""
import pcbnew
import modules as M
import parts as P
from geometry import at, origin_for
import kit

W, H = 90.0, 64.0
HOLES = [(4.0, 4.0), (W - 4.0, 4.0), (4.0, H - 4.0), (W - 4.0, H - 4.0)]
GAP = 11.0                     # socket 8.5 + header plastic 2.5
STICK = (22.0, 44.0)
STICK_HOLE = 27.0
SHAFT = (70.0, 46.0)           # encoder shaft, on the bottom board
SHAFT_HOLE = 9.0
LINK = (47.0, 45.0)
EXP_B_PIN1 = (64.5, 58.3)      # EXP B runs left from here along the bottom edge
ENC_SHAFT_LEN = 25.0           # EC11 shaft length from its PCB: reaches ~12 mm above the top board
ACCESS_D = 4.5

# ---------------------------------------------------------------- where the modules go
U1_O = (64.29, 2.09)                          # SuperMini, USB-C at the nose edge
U2_O = (39.2, 15.35)                          # DAC, rot 180: jack at the nose edge
U4_O = (37.74, 41.4)                          # IMU, rot 90: chip +X to the nose
U5_O = origin_for(M.KY023['stick'], 270, 'front', STICK)
SW1_O = (SHAFT[0] - 7.5, SHAFT[1] - 2.5)
BOOT = at(U1_O, M.SUPERMINI['boot'], 0)
RST = at(U1_O, M.SUPERMINI['rst'], 0)
OLED_PIN1 = (41.19, 5.0)
MIC = (23.0, 12.0)

# ---------------------------------------------------------------- spare GPIOs to the breakouts
# The underside pads leave the SuperMini on a fixed escape (kit.supermini_escape) that ends in a
# row just below the module. Pads on the module's left half go to EXP B on the bottom edge, the
# right half to EXP A on the right edge, and each header takes its lines in the order they arrive,
# so no two cross on the way in: EXP B left to right (TX/RX come down the far left of the module),
# EXP A top to bottom starting with the rightmost line. Power sits where its risers arrive.
POGO = M.SUPERMINI.get('pogo', [])             # [(pad, x, y, 'GPIOnn')]
EXITS = kit.supermini_exits()                  # {pad: (x, y) where its escape line ends}
EXP_A_N = 7                                    # 2 x 7 on the right edge, below the SuperMini
EXP_B_N = 7                                    # 2 x 7 along the bottom edge
_left = sorted((p for p in POGO if p[1] < M.SM_ROW / 2), key=lambda p: EXITS[p[0]][0])
_right = sorted((p for p in POGO if p[1] >= M.SM_ROW / 2), key=lambda p: -EXITS[p[0]][0])
EXP_A_NETS = [p[3] for p in _right] + ['+3V3', 'GND']
_b_seq = ['GPIO43', 'GPIO44'] + [p[3] for p in _left] + ['+5V', 'GND', '+3V3', 'GND']


EXP_B_NETS = kit.right_to_left(_b_seq)
SPARE = [n for n in EXP_A_NETS + EXP_B_NETS if n and n.startswith('GPIO')]
assert len(EXP_A_NETS) == 2 * EXP_A_N and len(EXP_B_NETS) == 2 * EXP_B_N


def _label(net):
    return {None: 'nc', '+3V3': '3V3', '+5V': '5V'}.get(net, net.replace('GPIO', '') if net else 'nc')


EXP_A = M.breakout('EXP_A_2x7_Stack', 'EXP A', [_label(n) for n in EXP_A_NETS], rows=True)
EXP_B = M.breakout('EXP_B_2x7_Stack', 'EXP B', [_label(n) for n in EXP_B_NETS], rows=True, skip_rows=(0,))
LIB_EXTRA = [EXP_A, EXP_B]

# ---------------------------------------------------------------- bottom board
# GND on the bottom row: every signal comes in from above, and would wall in a GND pin on top
LINK_NETS = {1: '+3V3', 2: 'I2S_DIN', 3: 'I2C_SDA', 4: 'I2C_SCL', 5: 'I2S_BCLK', 6: 'I2S_WS', 7: 'GND', 8: 'GND'}
U1_NETS = {1: 'GPIO43', 2: 'GPIO44', 3: 'JOY_Y', 4: 'JOY_X', 5: 'JOY_SW', 6: 'ENC_A', 7: 'ENC_B', 8: 'ENC_SW',
           9: 'I2S_BCLK', 10: '+5V', 11: 'GND', 12: '+3V3', 13: 'I2C_SCL', 14: 'I2C_SDA', 15: 'AMP_SD',
           16: 'I2S_DIN', 17: 'I2S_DOUT', 18: 'I2S_WS'}
U1_NETS.update({p[0]: p[3] for p in POGO})

BOTTOM_PARTS = {
    'U1': P.mod(M.SUPERMINI, U1_NETS),
    'U2': P.mod(M.PCM5102, {1: 'GND', 2: 'I2S_BCLK', 3: 'I2S_DOUT', 4: 'I2S_WS', 5: 'GND', 6: '+5V',
                            7: 'DAC_FLT', 8: 'DAC_DEMP', 9: 'DAC_XSMT', 10: 'DAC_FMT', 11: 'DAC_3V3',
                            12: 'GND', 13: None, 14: 'GND', 15: None}, value='PCM5102A'),
    'U4': P.mod(M.GY521, {1: '+3V3', 2: 'GND', 3: 'I2C_SCL', 4: 'I2C_SDA', 5: None, 6: None, 7: 'GND', 8: None},
                value='MPU6050'),
    'U5': P.mod(M.KY023, {1: 'GND', 2: '+3V3', 3: 'JOY_X', 4: 'JOY_Y', 5: 'JOY_SW'}, value='KY-023'),
    'SW1': P.encoder({'A': 'ENC_A', 'B': 'ENC_B', 'C': 'GND', 'S1': 'ENC_SW', 'S2': 'GND', 'MP': 'GND'},
                     shaft=ENC_SHAFT_LEN),
    'J1': P.link(P.FP_SOCKET_2x4, 'To top board', LINK_NETS),
    'J2': P.mod(M.MAX98357, {1: 'I2S_WS', 2: 'I2S_BCLK', 3: 'I2S_DOUT', 4: None, 5: 'AMP_SD', 6: 'GND', 7: '+5V'},
                value='MAX98357A (opt)'),
    'J3': P.qwiic(),
    'J4': P.breakout(EXP_A, {i + 1: n for i, n in enumerate(EXP_A_NETS)}),
    'J5': P.breakout(EXP_B, {i + 1: n for i, n in enumerate(EXP_B_NETS)}),
    'JP1': P.strap('FLT', 'DAC_FLT'), 'JP2': P.strap('DEMP', 'DAC_DEMP'),
    'JP3': P.strap('XSMT', 'DAC_XSMT', low=False), 'JP4': P.strap('FMT', 'DAC_FMT'),
    'C1': P.cap('10u', '+5V'), 'C2': P.cap('10u', '+3V3'), 'C3': P.cap('100n', '+3V3'),
    'R1': P.res('4k7', '+3V3', 'I2C_SDA', dnp=True), 'R2': P.res('4k7', '+3V3', 'I2C_SCL', dnp=True),
    'H1': P.hole(), 'H2': P.hole(), 'H3': P.hole(), 'H4': P.hole(),
}

BOTTOM_PLACE = {
    'U1': (*U1_O, 0, 'front'), 'U2': (*U2_O, 180, 'front'), 'U4': (*U4_O, 90, 'front'),
    'U5': (*U5_O, 270, 'front'), 'SW1': (*SW1_O, 0, 'front'), 'J1': (*LINK, 0, 'front'),
    'J2': (3.0, 36.0, 90, 'front'), 'J3': (78.8, 60.7, 0, 'front'),
    'J4': (84.6, 36.0, 0, 'front'), 'J5': (*EXP_B_PIN1, 270, 'front'),
    'JP1': (32.725, 20.4, 0, 'front'), 'JP3': (27.645, 20.4, 0, 'front'),
    'JP2': (30.185, 23.4, 0, 'front'), 'JP4': (25.105, 23.4, 0, 'front'),
    'C1': (42.7, 2.65, 0, 'front'), 'C2': (50.6, 45.0, 0, 'front'), 'C3': (37.7, 45.3, 270, 'front'),
    'R1': (40.4, 48.6, 270, 'front'), 'R2': (40.4, 52.2, 270, 'front'),
    'H1': (*HOLES[0], 0, 'front'), 'H2': (*HOLES[1], 0, 'front'),
    'H3': (*HOLES[2], 0, 'front'), 'H4': (*HOLES[3], 0, 'front'),
}
BOTTOM_REF_AT = {'C1': (0, 1.7), 'C2': (0, 1.7), 'C3': (1.7, 0), 'R1': (-1.7, 0), 'R2': (-1.7, 0),
                 'JP1': None, 'JP2': None, 'JP3': None, 'JP4': None, 'J1': None, 'J2': None, 'J3': None, 'J4': None, 'J5': None,
                 'SW1': None, 'H1': None, 'H2': None, 'H3': None, 'H4': None}


def bottom_power(bd):
    """The SuperMini's pogo pads and its 5V/3V3 get a fixed escape (kit.supermini_fanout);
    Freerouting joins the ends to the breakouts, the DAC, the amp and the rest."""
    kit.supermini_fanout(bd, 'U1', U1_O, 0, 'front')


def bottom_silk(bd, S):
    F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
    for (x, name) in ((32.725, 'FLT'), (27.645, 'XSMT')):
        kit.silk(bd, name, (x, 18.6), 0.8)
    for (x, name) in ((30.185, 'DEMP'), (25.105, 'FMT')):
        kit.silk(bd, name, (x, 25.9), 0.8)
    kit.silk(bd, 'DAC straps', (9.6, 19.6), 0.8, justify='left')
    kit.silk(bd, '1-2 = H  2-3 = L', (9.6, 21.1), 0.8, justify='left')
    kit.silk(bd, 'open if set on', (9.6, 22.6), 0.8, justify='left')
    kit.silk(bd, 'the DAC board', (9.6, 24.1), 0.8, justify='left')
    kit.silk(bd, 'chip +X to nose', (47.0, 25.9), 0.8)
    kit.arrow_up(bd, 54.0, 25.6)
    kit.silk(bd, 'AMP (opt)', (3.0, 38.2), 0.8)
    kit.silk(bd, 'QWIIC', (78.8, 56.3), 0.8)
    for k, name in enumerate(_label(n) for n in EXP_B_NETS[:2]):    # EXP B pins 1, 2: the encoder sits above
        kit.silk(bd, name, (EXP_B_PIN1[0] + 1.3, EXP_B_PIN1[1] + 2.54 * k), 0.8, justify='left')
    kit.silk(bd, 'EXP A', (85.87, 33.6), 0.8)
    kit.silk(bd, 'EXP B', (EXP_B_PIN1[0] - 6 * 2.54 - 3.8, EXP_B_PIN1[1] + 1.27), 0.8)
    kit.silk(bd, 'R1 R2: fit only if', (34.4, 54.6), 0.8, justify='left')
    kit.silk(bd, 'no module has them', (34.4, 55.9), 0.8, justify='left')
    kit.silk(bd, 'SAMPLOOLER', (47.0, 31.2), 3.0, B, bold=True)
    kit.silk(bd, f'stack bottom  rev {kit.REV}  {kit.DATE}', (47.0, 34.7), 1.2, B)
    kit.silk(bd, 'M3 x 11 standoffs, 2x4 socket 8.5 mm', (47.0, 36.9), 1.0, B)
    kit.silk(bd, 'willbearfruits / samplooler', (47.0, 38.9), 1.0, B)
    kit.silk(bd, 'JLCJLCJLCJLC', (21.0, 45.0), 1.0, B)


BOTTOM = dict(
    key='stack-bottom', project='samplooler-stack-bottom', dir='stack-bottom',
    title='Samplooler stack, bottom board: modules',
    comments=('ESP32-S3 SuperMini carrier: DAC, IMU, joystick, encoder, amp header, breakouts',
              'Mates with samplooler-stack-top through J1 (2x4, 11 mm)'),
    parts=BOTTOM_PARTS, size=(W, H), holes=HOLES, place=BOTTOM_PLACE, ref_at=BOTTOM_REF_AT,
    keepouts=[dict(a=(U1_O[0] + 0.6 + 0.7, U1_O[1] + 18.4), b=(U1_O[0] + M.SM_ROW - 0.6 - 0.7, U1_O[1] + 21.91),
                   tracks=False, vias=False, name='antenna')],
    silk=bottom_silk, power=bottom_power,
    groups=[('ESP32-S3 SuperMini (pogo pins reach the pads underneath)', ['U1']),
            ('DAC  (GY-PCM5102, line out)', ['U2', 'JP1', 'JP2', 'JP3', 'JP4']),
            ('Tilt  (GY-521 / MPU-6050, I2C 0x68)', ['U4']),
            ('Joystick  (KY-023)', ['U5']),
            ('Encoder  (EC11, shaft up through the top board)', ['SW1']),
            ('To the top board  (OLED on I2C, mic on I2S)', ['J1']),
            ('Amp header  (optional MAX98357A)', ['J2']),
            ('Qwiic', ['J3']),
            ('Breakouts  (every spare GPIO)', ['J4', 'J5']),
            ('Decoupling, I2C pull-ups', ['C1', 'C2', 'C3', 'R1', 'R2']),
            ('Mounting', ['H1', 'H2', 'H3', 'H4'])],
    notes=['USB-C on the SuperMini powers everything: its 5V pin feeds the DAC and the amp, its 3.3 V\n'
           'regulator feeds the IMU, joystick, top board (OLED, mic), Qwiic and the breakouts.',
           'JP1-JP4 stand in for the H1L-H4L bridges on the back of the DAC board, out of reach once it is\n'
           'mounted. Leave them OPEN if those are set; otherwise FLT=L, DEMP=L, XSMT=H (unmute), FMT=L.\n'
           'H is DAC_3V3, the module\'s own 3.3 V regulator, never the board 3V3.',
           'The joystick\'s "+5V" pin goes to 3V3: the ESP32 ADC reads 0-3.3 V. The IMU\'s +X axis points at\n'
           'the nose (the USB / jack edge). AD0 to GND: address 0x68.',
           'EXP A / EXP B carry every GPIO the design leaves free, including the SuperMini\'s underside pads\n'
           '(reached with pogo pins). 45 and 46 are strapping pins; 48 drives the module\'s RGB LED.',
           'R1, R2 are not fitted: the OLED and GY-521 boards carry I2C pull-ups.'],
    models={'SW1': '${KIPRJMOD}/../lib/samplooler.3dshapes/EC11_Vertical_Knob.wrl'},
    stitch_avoid=[((U1_O[0], 20.0), (U1_O[0] + M.SM_ROW, 24.5))],
)

# ---------------------------------------------------------------- top board
TOP_PARTS = {
    'J1': P.link(P.FP_HEADER_2x4, 'To bottom board', LINK_NETS),
    'U1': P.with_below(P.mod(M.OLED, {1: 'OLED_P1', 2: 'OLED_P2', 3: 'I2C_SCL', 4: 'I2C_SDA'}, value='OLED 0.96in'),
                       [('M2 screw', ('circle', hx, hy, 2.5), 4.0) for hx, hy, d in M.OLED['holes']]),
    'JP1': P.oled_order('OLED pin 1', 'OLED_P1', 'GND'),
    'JP2': P.oled_order('OLED pin 2', 'OLED_P2', '+3V3'),
    'U2': P.mod(M.INMP441, {1: 'I2S_BCLK', 2: 'I2S_WS', 3: 'GND', 4: 'I2S_DIN', 5: '+3V3', 6: 'GND'}, value='INMP441'),
    'C1': P.cap('10u', '+3V3'), 'C2': P.cap('100n', '+3V3'), 'C3': P.cap('100n', '+3V3'),
    'H1': P.hole(), 'H2': P.hole(), 'H3': P.hole(), 'H4': P.hole(),
}
TOP_PLACE = {
    'J1': (*LINK, 180, 'back'),
    'U1': (*OLED_PIN1, 0, 'front'), 'U2': (*MIC, 0, 'front'),
    'JP1': (38.6, 36.4, 0, 'front'), 'JP2': (45.4, 36.4, 0, 'front'),
    'C1': (51.4, 36.4, 90, 'front'), 'C2': (54.4, 36.4, 90, 'front'), 'C3': (MIC[0], MIC[1] + 9.6, 0, 'front'),
    'H1': (*HOLES[0], 0, 'front'), 'H2': (*HOLES[1], 0, 'front'),
    'H3': (*HOLES[2], 0, 'front'), 'H4': (*HOLES[3], 0, 'front'),
}
TOP_REF_AT = {'C1': (0, 2.3), 'C2': (0, 2.3), 'C3': (2.6, 0), 'JP1': None, 'JP2': None, 'J1': (-1.27, -2.6),
              'H1': None, 'H2': None, 'H3': None, 'H4': None}


def top_silk(bd, S):
    B = pcbnew.B_SilkS
    jack_x = at(U2_O, M.PCM5102['jack'], 180)[0]
    usb_x = U1_O[0] + M.SM_ROW / 2
    for x, name in ((jack_x, 'LINE OUT'), (usb_x, 'USB-C')):
        kit.silk(bd, name, (x, 2.2), 1.0)
    kit.silk(bd, 'MIC', (MIC[0], 3.2), 1.0)
    kit.silk(bd, 'BOOT', (BOOT[0], BOOT[1] + 3.6), 0.9)
    kit.silk(bd, 'RST', (RST[0], RST[1] + 3.6), 0.9)
    kit.silk(bd, 'tilt the nose down: faster', (45.0, 33.0), 0.8)
    kit.arrow_up(bd, 62.0, 32.7)
    sx, sy = STICK
    kit.silk(bd, 'click: tilt   hold: menu', (sx + 1.5, sy + STICK_HOLE / 2 + 2.2), 0.9)
    kit.silk(bd, 'turn: value', (SHAFT[0], SHAFT[1] + 8.4), 0.9)
    kit.silk(bd, 'hold: record', (SHAFT[0], SHAFT[1] + 10.0), 0.9)
    kit.silk(bd, 'OLED pin order', (36.6, 34.6), 0.8, justify='left')
    kit.silk(bd, 'GND VCC: as is', (36.6, 38.7), 0.8, justify='left')
    kit.silk(bd, 'VCC GND: cut 1-2, bridge 2-3', (36.6, 40.1), 0.8, justify='left')
    kit.silk(bd, 'SAMPLOOLER', (45.0, 57.5), 2.4, bold=True)
    kit.silk(bd, f'rev {kit.REV}', (45.0, 60.6), 0.9)
    kit.silk(bd, 'SAMPLOOLER stack top', (45.0, 22.0), 1.6, B, bold=True)
    kit.silk(bd, f'rev {kit.REV}  {kit.DATE}', (45.0, 24.4), 1.0, B)
    kit.silk(bd, 'J1 header goes on THIS side, pins down', (45.0, 41.0), 0.9, B)
    kit.silk(bd, 'JLCJLCJLCJLC', (72.0, 60.0), 1.0, B)


TOP = dict(
    key='stack-top', project='samplooler-stack-top', dir='stack-top',
    title='Samplooler stack, top board: panel',
    comments=('OLED and microphone; openings for the joystick, the encoder, BOOT and RST',
              'Mates with samplooler-stack-bottom through J1 (header on the underside)'),
    parts=TOP_PARTS, size=(W, H), holes=HOLES, place=TOP_PLACE, ref_at=TOP_REF_AT,
    cutouts=[(*STICK, STICK_HOLE), (*SHAFT, SHAFT_HOLE), (*BOOT, ACCESS_D), (*RST, ACCESS_D)],
    silk=top_silk, power=None,
    groups=[('From the bottom board  (2x4 header, under this board)', ['J1']),
            ('OLED  (SSD1306 0.96in, I2C 0x3C)', ['U1', 'JP1', 'JP2', 'C1', 'C2']),
            ('Microphone  (INMP441, I2S, sound hole up)', ['U2', 'C3']),
            ('Mounting', ['H1', 'H2', 'H3', 'H4'])],
    notes=['Most 4-pin OLEDs are GND-VCC-SCL-SDA: JP1/JP2 come bridged 1-2 for that. For a VCC-GND-SCL-SDA\n'
           'module cut both 1-2 bridges and solder 2-3 on both.',
           'The mic\'s L/R goes to GND: its data lands in the left I2S slot (the firmware takes either).'],
    stitch_avoid=[((STICK[0] - 14, STICK[1] - 14), (STICK[0] + 14, STICK[1] + 14))],
)

BOARDS = [BOTTOM, TOP]


def mech_check():
    import mech
    bot = kit.mech_board(BOTTOM, 0.0)
    top = kit.mech_board(TOP, GAP + mech.PCB_T,
                         sweeps=[('U5', STICK, 13.0, GAP + mech.PCB_T + 0.3, GAP + mech.PCB_T + 12.0)])
    errors, trims = mech.check([bot, top], allowed=[('stack-bottom/J1', 'stack-top/J1')])
    return errors, trims, []
