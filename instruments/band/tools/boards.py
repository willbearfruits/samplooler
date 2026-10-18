"""The family's boards on this computer: their MIDI ports (named after the firmware they run) and their
USB serials (two letters, then the last four bytes of the chip's MAC). A TWOFOLD is never one of them:
its name is not on the list and nothing here writes to a port it did not find by name."""
import glob, os, re

FAMILY = ('KNOCK', 'HUFF', 'HOWL', 'SNIFF', 'CHIRP', 'ETHER', 'JAM', 'BAND', 'SING', 'WRECK', 'MANTRA', 'DEMO', 'Samplooler Family', 'Samplooler')
NEVER = ('TWOFOLD',)

def boards():
    """[(name, midi_device, serial or '')] for every family board running its firmware."""
    out = []
    for card in sorted(glob.glob('/proc/asound/card*/midi0')):
        try: first = open(card).readline().strip()
        except OSError: continue
        if any(x in first for x in NEVER): continue
        name = next((f for f in FAMILY if first == f or first.startswith(f + ' ')), None)
        if not name: continue
        n = card.split('card')[1].split('/')[0]
        serial = ''
        try: serial = open(f'/sys/class/sound/card{n}/device/../serial').read().strip()
        except OSError: pass
        out.append((name, f'/dev/snd/midiC{n}D0', serial))
    return out

def find(name=None):
    """The board running `name` (or the only family board there is); exits with a message otherwise."""
    bs = boards()
    if name: bs = [b for b in bs if b[0] == name] or bs
    if len(bs) == 1: return bs[0]
    if not bs: raise SystemExit('no family board on USB (a Samplooler or one of the library firmwares)')
    raise SystemExit('more than one board: ' + ', '.join(f'{b[0]} {b[2]}' for b in bs) + ' (unplug the others or set BOARD=<serial>)')

def mac_tail(serial):
    """'KN-727600D0' -> '72:76:00:D0'"""
    m = re.search(r'([0-9A-F]{8})$', serial or '')
    return ':'.join(m.group(1)[i:i + 2] for i in range(0, 8, 2)) if m else None

def jtag_ports():
    return sorted(glob.glob('/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*'))
