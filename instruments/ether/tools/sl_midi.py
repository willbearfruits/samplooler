#!/usr/bin/env python3
"""Talk to the instrument over its raw MIDI port (no extra Python packages). It finds the port by the
instrument's name (boards.py), so it never writes to a TWOFOLD or anything else.

  sl_midi.py log            print the device log (SysEx 7D 4C lines) and other MIDI it sends
  sl_midi.py status         ask for the status and crash report (SysEx 7D 53) and print the reply;
                            a backtrace is decoded against firmware/build/<name>.elf when addr2line is there
  sl_midi.py reboot         ask the device to reboot into download mode (then run idf.py flash)
  sl_midi.py cc N V         send controller N value V
  sl_midi.py note N [on|off]
"""
import glob, os, re, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import boards

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def own_name():
    m = re.search(r'set\(INSTRUMENT_NAME\s+"([^"]+)"', open(os.path.join(ROOT, 'firmware', 'main', 'CMakeLists.txt')).read())
    return m.group(1) if m else None

_port = None
def port():
    global _port
    if not _port:
        want = os.environ.get('BOARD')
        bs = boards.boards()
        if want: bs = [b for b in bs if want in b[2]]
        mine = [b for b in bs if b[0] == own_name()]
        b = (mine or bs)
        if len(b) != 1: boards.find(own_name())
        _port = b[0][1]
    return _port

def send(data):
    with open(port(), 'wb', buffering=0) as f: f.write(bytes(data))

def decode_backtrace(line):
    """Print the file:line of each PC in a 'bt 0x...' crash line, if the toolchain and the ELF are at hand."""
    pcs = re.findall(r'0x4[0-9a-f]{7}', line)
    if not pcs: return
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    elf = (glob.glob(os.path.join(root, 'firmware', 'build', '*.elf')) or [''])[0]
    tool = shutil.which('xtensa-esp32s3-elf-addr2line')
    if not os.path.exists(elf) or not tool: return
    out = subprocess.run([tool, '-pfiaC', '-e', elf] + pcs, capture_output=True, text=True).stdout
    for l in out.splitlines(): print('    ' + l)

def log(until=None, want=None):
    with open(port(), 'rb', buffering=0) as f:
        sx = None; st = 0; d = []
        t0 = time.time()
        while until is None or time.time() - t0 < until:
            b = f.read(1)
            if not b: continue
            b = b[0]
            if b == 0xF0: sx = bytearray(); continue
            if b == 0xF7:
                if sx is not None and len(sx) >= 2 and sx[0] == 0x7D and sx[1] == ord('L'):
                    text = sx[2:].decode('ascii', 'replace')
                    if want is None or want in text:
                        print(text, flush=True)
                        if 'crash:' in text: decode_backtrace(text)
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
elif a[0] == 'status':
    import threading
    threading.Thread(target=lambda: (time.sleep(0.3), send([0xF0, 0x7D, ord('S'), 0xF7])), daemon=True).start()
    log(until=2.0, want='diag:')
elif a[0] == 'reboot': send([0xF0, 0x7D, ord('R'), 0xF7]); print('sent reboot-to-download')
elif a[0] == 'cc': send([0xB0, int(a[1]) & 0x7F, int(a[2]) & 0x7F])
elif a[0] == 'note': send([0x90 if (len(a) < 3 or a[2] == 'on') else 0x80, int(a[1]) & 0x7F, 100])
else: print(__doc__)
