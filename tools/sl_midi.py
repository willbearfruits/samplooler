#!/usr/bin/env python3
"""Talk to the Samplooler over its raw MIDI port (no extra Python packages).

  sl_midi.py log            print the device log (SysEx 7D 4C lines) and other MIDI it sends
  sl_midi.py reboot         ask the device to reboot into download mode (then run idf.py flash)
  sl_midi.py cc N V         send controller N value V
  sl_midi.py note N [on|off]
"""
import glob, os, sys, time

def port():
    for card in glob.glob('/proc/asound/card*/midi0'):
        if 'Samplooler' in open(card).read():
            n = card.split('card')[1].split('/')[0]
            return f'/dev/snd/midiC{n}D0'
    sys.exit('no Samplooler MIDI port found')

def send(data):
    with open(port(), 'wb', buffering=0) as f: f.write(bytes(data))

def log():
    with open(port(), 'rb', buffering=0) as f:
        sx = None; st = 0; d = []
        while True:
            b = f.read(1)
            if not b: continue
            b = b[0]
            if b == 0xF0: sx = bytearray(); continue
            if b == 0xF7:
                if sx is not None and len(sx) >= 2 and sx[0] == 0x7D and sx[1] == ord('L'):
                    print(sx[2:].decode('ascii', 'replace'), flush=True)
                sx = None; continue
            if sx is not None: sx.append(b); continue
            if b & 0x80: st = b; d = []; continue
            d.append(b)
            if len(d) == 2:
                t = st & 0xF0
                name = {0x90: 'note on', 0x80: 'note off', 0xB0: 'cc'}.get(t, hex(t))
                print(f'  <- {name} {d[0]} {d[1]}', flush=True); d = []

a = sys.argv[1:]
if not a or a[0] == 'log': log()
elif a[0] == 'reboot': send([0xF0, 0x7D, ord('R'), 0xF7]); print('sent reboot-to-download')
elif a[0] == 'cc': send([0xB0, int(a[1]) & 0x7F, int(a[2]) & 0x7F])
elif a[0] == 'note': send([0x90 if (len(a) < 3 or a[2] == 'on') else 0x80, int(a[1]) & 0x7F, 100])
else: print(__doc__)
