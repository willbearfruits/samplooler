#!/usr/bin/env python3
"""Pick the serial port to flash (see flash.sh). Prints it, or explains on stderr and exits non-zero."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import boards

def err(*a): print(*a, file=sys.stderr)
want = os.environ.get('BOARD', '').upper()
running = boards.boards()
if want: running = [b for b in running if want.replace(':', '') in b[2].upper().replace(':', '')]
if len(running) > 1:
    err('more than one board is running:', ', '.join(f'{b[0]} ({b[2]})' for b in running))
    err('pick one with BOARD=<serial>'); sys.exit(1)
if running:
    name, dev, serial = running[0]
    tail = boards.mac_tail(serial)
    err(f'{name} ({serial}): asking it to reboot into download mode')
    with open(dev, 'wb', buffering=0) as f: f.write(bytes([0xF0, 0x7D, ord('R'), 0xF7]))
    for _ in range(50):
        time.sleep(0.2)
        ports = [p for p in boards.jtag_ports() if tail and p.upper().find(tail.upper()) >= 0]
        if ports: print(ports[0]); sys.exit(0)
    err('it did not come back in download mode: hold BOOT while plugging it in, then run this again'); sys.exit(1)
ports = boards.jtag_ports()
if want: ports = [p for p in ports if want.replace(':', '') in p.upper().replace(':', '')]
if len(ports) == 1: print(ports[0]); sys.exit(0)
if not ports: err('no board found: plug one in (if it is not running one of ours, hold BOOT while plugging it in)')
else: err('more than one board in download mode:', ' '.join(ports), '(pick one with BOARD=<MAC tail>)')
sys.exit(1)
