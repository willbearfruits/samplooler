"""Part constructors shared by every board: symbol, footprint, value, nets, mechanics.

A part is a dict:
    lib      symbol lib_id           fp      footprint lib_id
    value    str                     nets    {pin: net or None}
    fields   extra symbol fields     dnp / in_bom
    mech     mechanical spec for mech.py (features, tail, trim)
"""
import modules as M

LCSC = {'10u': 'C15850', '100n': 'C49678', '4k7': 'C17673'}
FP_C = 'Capacitor_SMD:C_0805_2012Metric'
FP_R = 'Resistor_SMD:R_0805_2012Metric'
FP_HOLE = 'MountingHole:MountingHole_3.2mm_M3_Pad_Via'
FP_SJ_OPEN = 'Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm'
FP_SJ_B12 = 'Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm_NumberLabels'
FP_EC11 = 'Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm'
FP_SOCKET_2x4 = 'Connector_PinSocket_2.54mm:PinSocket_2x04_P2.54mm_Vertical'
FP_HEADER_2x4 = 'Connector_PinHeader_2.54mm:PinHeader_2x04_P2.54mm_Vertical'
FP_QWIIC = 'Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal'
FP_BUTTON12 = 'Button_Switch_THT:SW_PUSH-12mm'

SMD = {'features': [('body', (-1.3, -0.9, 1.3, 0.9), 0, 1.3)]}
HDR = 2.5          # every module sits on 2.5 mm of header plastic
TAIL = 4.4         # untrimmed long header pin under the carrier


def mech_module(m, height, extra=()):
    """A breakout module on its headers: body from the header plastic up, pins through the carrier."""
    from geometry import outline_box
    if 'mech_features' in m:                  # modules that don't sit on 2.5 mm of plastic
        return {'features': list(m['mech_features']) + list(extra), 'tail': TAIL, 'trim': True}
    o = m['outline']
    body = ('circle', o[1], o[2], o[3]) if o[0] == 'circle' else outline_box(m)
    flags = 'edge' if m.get('overhang') else ''
    return {'features': [('body', body, HDR, height, flags)] + list(extra), 'tail': TAIL, 'trim': True}


def cap(value, a, b='GND'):
    return dict(lib='Device:C', fp=FP_C, value=value, nets={'1': a, '2': b}, fields={'LCSC': LCSC[value]}, mech=SMD)


def res(value, a, b, dnp=False):
    return dict(lib='Device:R', fp=FP_R, value=value, nets={'1': a, '2': b}, fields={'LCSC': LCSC[value]},
                dnp=dnp, mech=SMD)


def hole():
    return dict(lib='Mechanical:MountingHole_Pad', fp=FP_HOLE, value='M3', nets={'1': 'GND'}, in_bom=False, mech={})


def strap(value, net, low=True):
    """3-pad jumper: 1 = the module's own 3.3 V (H), 2 = the strap pin, 3 = GND (L). Open by default.
    low=False leaves pad 3 unconnected, for a strap whose L setting nobody wants (XSMT L = mute)."""
    return dict(lib='Jumper:SolderJumper_3_Open', fp=FP_SJ_OPEN, value=value, in_bom=False,
                nets={'1': 'DAC_3V3', '2': net, '3': 'GND' if low else None}, mech={})


def oled_order(value, pin, first):
    """Pre-bridged 1-2: pin 1 of the OLED to `first`; cut and bridge 2-3 for the other order."""
    other = '+3V3' if first == 'GND' else 'GND'
    return dict(lib='Jumper:SolderJumper_3_Bridged12', fp=FP_SJ_B12, value=value, in_bom=False,
                nets={'1': first, '2': pin, '3': other}, mech={})


def mod(m, nets, value=None, height=None, extra=()):
    return dict(lib='samplooler:' + m['name'], fp='samplooler:' + m['name'], value=value or m['name'],
                nets={str(k): v for k, v in nets.items()},
                mech=mech_module(m, height or m['height'], extra or m.get('mech_extra', ())))


def encoder(nets, shaft=20.0, knob=True):
    """EC11 with push switch. Origin = pad A, shaft at (7.5, 2.5). `shaft` = length from the PCB."""
    feats = [('body', (1.5, -3.5, 13.5, 8.5), 0, 6.8),
             ('bushing', ('circle', 7.5, 2.5, 3.5), 6.8, 11.5),
             ('shaft', ('circle', 7.5, 2.5, 3.0), 11.5, shaft)]
    if knob:
        feats.append(('knob', ('circle', 7.5, 2.5, 8.0), shaft - 12.0, shaft + 2.0))
    return dict(lib='Device:RotaryEncoder_Switch_MP', fp=FP_EC11, value=f'EC11, {shaft:g} mm shaft', nets=nets,
                mech={'features': feats, 'tail': 2.0, 'trim': False})


def button12(nets):
    """12 x 12 mm tactile switch with a round cap. Pads (0,0) (12.5,0) (0,5) (12.5,5)."""
    return dict(lib='Switch:SW_Push', fp=FP_BUTTON12, value='12 mm tactile + cap', nets=nets,
                mech={'features': [('body', (0.25, -3.5, 12.25, 8.5), 0, 7.3),
                                   ('cap', ('circle', 6.25, 2.5, 6.0), 7.3, 12.0)], 'tail': 2.0, 'trim': False})


def link(fp, value, nets, back=False):
    """The 2x4 board-to-board connector: socket below (8.5 mm), header under the top board."""
    if fp == FP_SOCKET_2x4:
        mech = {'features': [('socket', (-3.81, -1.27, 1.27, 8.89), 0, 8.5)], 'tail': 1.5, 'trim': True}
    else:
        mech = {'features': [('header', (-1.27, -1.27, 3.81, 8.89), 0, 2.5),
                             ('pins', (-1.27, -1.27, 3.81, 8.89), 2.5, 8.5)], 'tail': 1.5, 'trim': True}
    return dict(lib='Connector_Generic:Conn_02x04_Odd_Even', fp=fp, value=value,
                nets={str(k): v for k, v in nets.items()}, mech=mech)


def qwiic():
    """JST-SH 4-pin (Qwiic / STEMMA QT): 1 GND, 2 3V3, 3 SDA, 4 SCL."""
    return dict(lib='Connector_Generic_MountingPin:Conn_01x04_MountingPin', fp=FP_QWIIC, value='Qwiic I2C',
                nets={'1': 'GND', '2': '+3V3', '3': 'I2C_SDA', '4': 'I2C_SCL', 'MP': 'GND'},
                mech={'features': [('body', (-3.0, -2.8, 3.0, 2.6), 0, 2.95, 'edge')]})


def breakout(m, nets):
    """Plated pads for later headers; keep 2.5 mm above them clear for header plastic."""
    from geometry import outline_box
    return dict(lib='samplooler:' + m['name'], fp='samplooler:' + m['name'], value=m['value'],
                nets={str(k): v for k, v in nets.items()},
                mech={'features': [('future header', outline_box(m), 0, 2.5)]})


def with_below(part, below):
    """Things that stick out behind the board under this part (screws, nuts): (name, shape, depth)."""
    part = dict(part)
    part['mech'] = dict(part['mech'], below=list(below))
    return part
