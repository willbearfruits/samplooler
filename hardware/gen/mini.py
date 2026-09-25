"""The Mini: one 82 x 56 mm board, no joystick, parts on both faces.

front (faces you)   OLED, microphone, encoder, record button, Qwiic, breakout pads
back                ESP32-S3 SuperMini (with pogo pins to its underside pads), DAC, IMU,
                    header for the optional amp; four M3 standoffs are the feet
USB-C and the jack still exit the nose (top) edge. The SuperMini hangs upside down, so its
BOOT and RST buttons face down and need no access holes.

Controls differ from the stack: no joystick, so the record button (GPIO3, the joystick's
old click pin) records, and the encoder does menus the C3 way (hold = menu, tap = next).
The IMU sits upside down with +X still to the nose: pitch is unchanged, roll comes out
mirrored (ROLL_SIGN -1 in the firmware).
"""
import pcbnew
import modules as M
import parts as P
from geometry import at
import kit

W, H = 82.0, 56.0
HOLES = [(4.0, 4.0), (W - 4.0, 4.0), (4.0, H - 4.0), (W - 4.0, H - 4.0)]

# back: footprints mirrored (KiCad: local (x, y) -> R(rot) . (x, -y))
U1_O = (66.71, 2.09)            # SuperMini, back rot 180: USB-C at the nose, bbox x 50.0..68.1
U2_O = (9.5, 15.35)             # DAC, back rot 0: jack at the nose, bbox x 8.0..39.7
U4_O = (40.8, 40.3)             # IMU, back rot 90: chip +X to the nose, bbox x 21.8..42.0, y 26..41.6
J2_O = (3.0, 19.0)              # amp header, back rot 270: LRC at the top, the amp hangs off the left edge
# front
OLED_PIN1 = (41.0, 5.0)         # the OLED's pins land in the gap between the DAC and the SuperMini
MIC = (14.0, 24.0)
REC = (13.5, 39.0)              # record button centre
SHAFT = (60.0, 39.0)            # encoder shaft
QWIIC = (16.0, 3.3)             # Qwiic, facing the nose
C2_AT = (40.8, 43.4)            # IMU decoupling, just below its VCC/GND pins, wired to them
EXP_B_PIN1 = (50.8, 50.5)       # EXP B runs left from here along the bottom edge

POGO = M.SUPERMINI.get('pogo', [])
EXITS = kit.supermini_exits()   # {pad: (x, y) where its escape line ends}
# Two breakouts, so no line has to cross another on its way:
#   EXP A, right edge: TX, RX, GPIO1, GPIO2, straight across from the top pins of the TX column.
#       That column's other pins (REC, encoder, BCLK) leave east too and head south: nothing
#       else goes that way.
#   EXP B, bottom edge: all 20 underside pads. Their escape lines (kit.supermini_escape) end in
#       a row below the module and come down together; the module is mirrored (board x =
#       U1_O[0] - x), so left to right on the header = largest module x first, the order the
#       lines arrive in. Power sits at the right end, where the 5V/3V3 risers come down.
EXP_A_NETS = ['GPIO43', 'GPIO44', 'GPIO1', 'GPIO2', 'GND', 'GND']
_below = sorted(POGO, key=lambda p: -EXITS[p[0]][0])
_b_seq = [p[3] for p in _below] + ['+5V', '+3V3', 'GND', 'GND']
EXP_B_NETS = kit.right_to_left(_b_seq)
SPARE = [n for n in EXP_A_NETS + EXP_B_NETS if n.startswith('GPIO')]
assert len(SPARE) == 24


def _label(net):
    return {None: 'nc', '+3V3': '3V3', '+5V': '5V'}.get(net, net.replace('GPIO', '') if net else 'nc')


EXP_A = M.breakout(f'EXP_A_2x{len(EXP_A_NETS) // 2}_Mini', 'EXP A', [_label(n) for n in EXP_A_NETS], rows=True,
                   labels_right=True)
EXP_B = M.breakout(f'EXP_B_2x{len(EXP_B_NETS) // 2}_Mini', 'EXP B', [_label(n) for n in EXP_B_NETS], rows=True)
LIB_EXTRA = [EXP_A, EXP_B, M.OLED_NOHOLES]

U1_NETS = {1: 'GPIO43', 2: 'GPIO44', 3: 'GPIO1', 4: 'GPIO2', 5: 'REC', 6: 'ENC_A', 7: 'ENC_B', 8: 'ENC_SW',
           9: 'I2S_BCLK', 10: '+5V', 11: 'GND', 12: '+3V3', 13: 'I2C_SCL', 14: 'I2C_SDA', 15: 'AMP_SD',
           16: 'I2S_DIN', 17: 'I2S_DOUT', 18: 'I2S_WS'}
U1_NETS.update({p[0]: p[3] for p in POGO})

PARTS = {
    'U1': P.mod(M.SUPERMINI, U1_NETS),
    'U2': P.mod(M.PCM5102, {1: 'GND', 2: 'I2S_BCLK', 3: 'I2S_DOUT', 4: 'I2S_WS', 5: 'GND', 6: '+5V',
                            7: 'DAC_FLT', 8: 'DAC_DEMP', 9: 'DAC_XSMT', 10: 'DAC_FMT', 11: 'DAC_3V3',
                            12: 'GND', 13: None, 14: 'GND', 15: None}, value='PCM5102A'),
    'U3': P.mod(M.INMP441, {1: 'I2S_BCLK', 2: 'I2S_WS', 3: 'GND', 4: 'I2S_DIN', 5: '+3V3', 6: 'GND'}, value='INMP441'),
    'U4': P.mod(M.GY521, {1: '+3V3', 2: 'GND', 3: 'I2C_SCL', 4: 'I2C_SDA', 5: None, 6: None, 7: 'GND', 8: None},
                value='MPU6050'),
    'U5': P.mod(M.OLED_NOHOLES, {1: 'OLED_P1', 2: 'OLED_P2', 3: 'I2C_SCL', 4: 'I2C_SDA'}, value='OLED 0.96in'),
    # the lugs are mechanical: under the encoder the EXP B lines wall their pour pocket in
    'SW1': P.encoder({'A': 'ENC_A', 'B': 'ENC_B', 'C': 'GND', 'S1': 'ENC_SW', 'S2': 'GND', 'MP': None}, shaft=20.0),
    'SW2': P.button12({'1': 'REC', '2': 'GND'}),
    'J2': P.mod(M.MAX98357, {1: 'I2S_WS', 2: 'I2S_BCLK', 3: 'I2S_DOUT', 4: None, 5: 'AMP_SD', 6: 'GND', 7: '+5V'},
                value='MAX98357A (opt)'),
    'J3': P.qwiic(),
    'J4': P.breakout(EXP_A, {i + 1: n for i, n in enumerate(EXP_A_NETS)}),
    'J5': P.breakout(EXP_B, {i + 1: n for i, n in enumerate(EXP_B_NETS)}),
    'JP1': P.strap('FLT', 'DAC_FLT'), 'JP2': P.strap('DEMP', 'DAC_DEMP'),
    'JP3': P.strap('XSMT', 'DAC_XSMT'), 'JP4': P.strap('FMT', 'DAC_FMT'),
    'JP5': P.oled_order('OLED pin 1', 'OLED_P1', 'GND'),
    'JP6': P.oled_order('OLED pin 2', 'OLED_P2', '+3V3'),
    'C1': P.cap('10u', '+5V'), 'C2': P.cap('100n', '+3V3'), 'C3': P.cap('100n', '+3V3'),
    'C4': P.cap('10u', '+3V3'), 'C5': P.cap('100n', '+3V3'),
    'R1': P.res('4k7', '+3V3', 'I2C_SDA', dnp=True), 'R2': P.res('4k7', '+3V3', 'I2C_SCL', dnp=True),
    'H1': P.hole(), 'H2': P.hole(), 'H3': P.hole(), 'H4': P.hole(),
}

PLACE = {
    'U1': (*U1_O, 180, 'back'), 'U2': (*U2_O, 0, 'back'), 'U4': (*U4_O, 90, 'back'), 'J2': (*J2_O, 270, 'back'),
    'U5': (*OLED_PIN1, 0, 'front'), 'U3': (*MIC, 0, 'front'),
    'SW1': (SHAFT[0] - 7.5, SHAFT[1] - 2.5, 0, 'front'), 'SW2': (REC[0] - 6.25, REC[1] - 2.5, 0, 'front'),
    'J3': (*QWIIC, 180, 'front'), 'J4': (72.0, 9.0, 0, 'front'), 'J5': (*EXP_B_PIN1, 270, 'front'),
    'JP1': (28.0, 20.2, 0, 'back'), 'JP3': (33.1, 20.2, 0, 'back'),
    'JP2': (30.55, 23.2, 0, 'back'), 'JP4': (35.65, 23.2, 0, 'back'),
    'JP5': (26.5, 34.5, 0, 'front'), 'JP6': (33.0, 34.5, 0, 'front'),
    'C1': (9.0, 19.8, 90, 'back'), 'C2': (*C2_AT, 180, 'back'), 'C3': (22.5, 27.0, 90, 'front'),
    'C4': (38.0, 33.6, 0, 'front'), 'C5': (38.0, 35.9, 0, 'front'),
    'R1': (44.5, 21.0, 270, 'back'), 'R2': (47.0, 21.0, 270, 'back'),
    'H1': (*HOLES[0], 0, 'front'), 'H2': (*HOLES[1], 0, 'front'),
    'H3': (*HOLES[2], 0, 'front'), 'H4': (*HOLES[3], 0, 'front'),
}
REF_AT = {'JP1': None, 'JP2': None, 'JP3': None, 'JP4': None, 'JP5': None, 'JP6': None, 'J3': None, 'SW1': None,
          'J2': None, 'J4': None, 'J5': None,
          'SW2': None, 'H1': None, 'H2': None, 'H3': None, 'H4': None,
          'C1': (-1.9, 0), 'C2': (0, 1.6), 'C3': (1.7, 0), 'C4': (2.6, 0), 'C5': (2.6, 0), 'R1': (0, 2.4), 'R2': (0, 2.4)}


def silk(bd, S):
    F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
    jack_x = at(U2_O, M.PCM5102['jack'], 0, 'back')[0]
    usb_x = at(U1_O, (M.SM_ROW / 2, 0), 180, 'back')[0]
    kit.silk(bd, 'LINE OUT', (jack_x, 1.6), 0.9)
    kit.silk(bd, 'USB-C', (usb_x, 1.6), 0.9)
    kit.silk(bd, f'rev {kit.REV}', (24.0, 7.6), 0.9)
    kit.silk(bd, 'QWIIC', (QWIIC[0], QWIIC[1] + 4.3), 0.8)
    kit.silk(bd, 'MIC', (5.6, MIC[1]), 0.9, rot=90)
    kit.silk(bd, 'REC', (REC[0], REC[1] + 7.3), 1.0)
    kit.silk(bd, 'SAMPLOOLER mini', (64.5, 52.6), 1.4, bold=True)
    kit.silk(bd, 'EXP A', (73.3, 6.6), 0.9)
    kit.silk(bd, 'EXP B', (EXP_B_PIN1[0] - (len(EXP_B_NETS) // 2 - 1) * 2.54 - 3.7, EXP_B_PIN1[1] + 1.27), 0.9)
    kit.silk(bd, 'turn: value   hold: menu', (SHAFT[0] + 2.0, 47.8), 0.8)
    kit.silk(bd, 'OLED pins', (29.75, 32.7), 0.8)
    kit.silk(bd, 'VCC GND: cut 1-2, bridge 2-3', (31.0, 37.8), 0.8)
    # back: the module side
    kit.silk(bd, 'SAMPLOOLER mini', (59.0, 27.5), 1.6, B, bold=True)
    kit.silk(bd, f'rev {kit.REV}  {kit.DATE}', (59.0, 29.6), 1.0, B)
    kit.silk(bd, 'feet: M3 x 12 standoffs', (59.0, 31.3), 0.8, B)
    kit.silk(bd, 'straps: 1-2 H, 2-3 L', (31.8, 24.9), 0.8, B)
    kit.silk(bd, 'IMU +X to nose', (32.0, 36.5), 0.8, B)
    kit.silk(bd, 'JLCJLCJLCJLC', (14.0, 48.0), 1.0, B)
    kit.silk(bd, 'AMP (opt)', (4.2, 16.4), 0.8, B)


def power(bd):
    """The SuperMini's escape, and C2 wired straight onto the IMU's VCC and GND pins: the EXP B
    lines fan out right under them and would leave the cap's pour pocket cut off."""
    kit.supermini_fanout(bd, 'U1', U1_O, 180, 'back')
    vcc, gnd = at(U4_O, (0, 0), 90, 'back'), at(U4_O, (0, 2.54), 90, 'back')     # GY-521 pins 1, 2
    c_vcc, c_gnd = (C2_AT[0] + 0.95, C2_AT[1]), (C2_AT[0] - 0.95, C2_AT[1])       # rot 180 on the back
    kit.track(bd, '+3V3', 'B.Cu', [c_vcc, (c_vcc[0], vcc[1] + 1.4), vcc], 0.5)
    kit.track(bd, 'GND', 'B.Cu', [c_gnd, (c_gnd[0], gnd[1] + 1.6), gnd], 0.5)
    kit.lock_all(bd)


MINI = dict(
    key='mini', project='samplooler-mini', dir='mini',
    title='Samplooler mini: one board, no joystick',
    comments=('Front: OLED, mic, encoder, REC button, Qwiic, breakout. Back: SuperMini, DAC, IMU, amp header',
              'REC on GPIO3; encoder hold = menu; IMU mounted upside down (ROLL_SIGN -1)'),
    parts=PARTS, size=(W, H), holes=HOLES, place=PLACE, ref_at=REF_AT,
    # two-sided on purpose: pins of back modules come through under front ones and vice versa.
    # KiCad's generic check doesn't know heights or solder order; mech.py checks both instead.
    rule_severities={'pth_inside_courtyard': 'ignore', 'npth_inside_courtyard': 'ignore'},
    keepouts=[dict(a=(U1_O[0] - M.SM_ROW + 1.3, U1_O[1] + 18.4), b=(U1_O[0] - 1.3, U1_O[1] + 21.91),
                   tracks=False, vias=False, name='antenna')],
    silk=silk, power=power,
    stitch_avoid=[],
    models={'SW1': '${KIPRJMOD}/../lib/samplooler.3dshapes/EC11_Vertical_Knob_20.wrl'},
    groups=[('ESP32-S3 SuperMini (back; pogo pins reach the pads underneath)', ['U1']),
            ('DAC  (GY-PCM5102, back)', ['U2', 'JP1', 'JP2', 'JP3', 'JP4']),
            ('Microphone  (INMP441, front, sound hole up)', ['U3', 'C3']),
            ('Tilt  (MPU-6050, back, upside down)', ['U4', 'C2']),
            ('OLED  (front)', ['U5', 'JP5', 'JP6', 'C4', 'C5']),
            ('Encoder', ['SW1']), ('Record', ['SW2']),
            ('Amp header  (optional)', ['J2']), ('Qwiic', ['J3']),
            ('Breakout A  (right edge)', ['J4']), ('Breakout B  (bottom edge)', ['J5']),
            ('Decoupling, I2C pull-ups', ['C1', 'R1', 'R2']),
            ('Feet  (M3 standoffs)', ['H1', 'H2', 'H3', 'H4'])],
    notes=['No joystick: the record button (GPIO3, the joystick\'s old click pin) records and the encoder\n'
           'handles menus the C3 way (hold = menu, tap = next / tilt). GPIO1 and GPIO2 (ADC1) are free.',
           'The MPU-6050 sits upside down on the back with +X still pointing at the nose: pitch (speed) is\n'
           'unchanged, roll comes out mirrored (ROLL_SIGN -1).',
           'JP1-JP4 stand in for the DAC board\'s H1L-H4L bridges: leave OPEN if those are set; otherwise\n'
           'FLT=L, DEMP=L, XSMT=H, FMT=L. JP5/JP6 pick the OLED pin order (bridged for GND-VCC-SCL-SDA).',
           'R1, R2 are not fitted: the OLED and GY-521 boards carry I2C pull-ups.'],
)

BOARDS = [MINI]


def mech_check():
    import mech
    bd = kit.mech_board(MINI, 0.0)
    errors, trims = mech.check([bd])
    order, hidden, loop = mech.assembly_order(bd)
    return errors + loop, trims, order
