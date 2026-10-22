#!/usr/bin/env python3
"""Write an instrument's IRAM linker fragment from what its audio path really calls.

The S3 runs code from flash through a cache that shares the SPI bus with PSRAM: the sound's code must sit in
internal RAM (docs/CORE.md, "The audio rules"). Mapping whole files there (`layer_drums (noflash)`) also takes
their screens, setups and tables, and internal RAM (shared between code and data on the S3) runs out once an
instrument has several big files. This finds, in the compiled objects, every function and constant table reachable
from the audio entry points (through calls and the literal pools that hold their addresses), and maps exactly
those: `object:symbol (noflash)` puts a function's code in IRAM and a table's data in DRAM.

    python3 tools/iram_lf.py <build dir> <out .lf> <mapping name> object:function [object:function ...]
    python3 tools/iram_lf.py <build dir> <.lf>        again, with the entry points written in that .lf's first line

e.g. python3 tools/iram_lf.py firmware/build firmware/main/band_iram.lf band_audio instrument:audio layer_drums:d_audio

Calls through pointers (layer_t.audio, INST.audio) are not followed: name every entry point. Functions outside
libmain (libm, the ROM, ESP-IDF) are left where ESP-IDF puts them; the ones the audio path reaches are listed
at the end as a warning when they live in flash, so a per-sample call there can be spotted.
"""
import glob, os, re, subprocess, sys

OBJDUMP = 'xtensa-esp32s3-elf-objdump'

def read_obj(obj):
    """The object's relocations per section, section sizes, and its symbols: {function: section}, {data: section}."""
    out = subprocess.run([OBJDUMP, '-rht', obj], capture_output=True, text=True, check=True).stdout
    relocs, sizes, funcs, datas, cur, mode = {}, {}, {}, {}, None, None
    for line in out.splitlines():
        if line.startswith('Sections:'): mode = 'sec'; continue
        if line.startswith('SYMBOL TABLE:'): mode = 'sym'; continue
        m = re.match(r'RELOCATION RECORDS FOR \[(.+)\]:', line)
        if m: mode = 'rel'; cur = m.group(1); relocs.setdefault(cur, []); continue
        if mode == 'sec':
            m = re.match(r'\s*\d+\s+(\S+)\s+([0-9a-f]+)\s', line)
            if m: sizes[m.group(1)] = int(m.group(2), 16)
        elif mode == 'sym':
            m = re.match(r'[0-9a-f]+\s+[lg!]\s+(\w)?\s*(\S+)\s+[0-9a-f]+\s+(\S+)$', line)
            if not m: continue
            kind, sec, name = m.group(1), m.group(2), m.group(3)
            if kind == 'F': funcs[name] = sec
            elif kind == 'O': datas[name] = sec
        elif mode == 'rel':
            m = re.match(r'[0-9a-f]+\s+R_XTENSA_\S+\s+(\S+)', line)
            if m: relocs[cur].append(re.sub(r'[+-]0x[0-9a-f]+$', '', m.group(1)))
    return relocs, sizes, funcs, datas

def literal_of(sec):
    if sec.startswith('.text.'): return '.literal.' + sec[len('.text.'):]
    return sec + '.literal'

def main(argv):
    if len(argv) == 3 and os.path.isfile(argv[2]):
        head = open(argv[2]).read()
        m = re.search(r"entry points: (.+)", head); n = re.search(r"\[mapping:(\w+)\]", head)
        if not m or not n: print(f'{argv[2]} was not written by this tool: give the entry points'); return 2
        argv = argv[:3] + [n.group(1)] + m.group(1).split()
    if len(argv) < 5:
        print(__doc__); return 2
    build, out, name, roots = argv[1], argv[2], argv[3], argv[4:]
    objdir = os.path.join(build, 'esp-idf', 'main', 'CMakeFiles', '__idf_main.dir')
    objs = {}
    for p in glob.glob(os.path.join(objdir, '**', '*.c.obj'), recursive=True):
        objs[os.path.basename(p)[:-len('.c.obj')]] = p
    info = {o: read_obj(p) for o, p in objs.items()}
    gfunc = {}                                         # a global function's object
    for o, (rel, sz, fn, da) in info.items():
        for f in fn: gfunc.setdefault(f, set()).add(o)
    want, data, outside, seen = set(), set(), set(), set()
    todo = []
    for r in roots:
        o, f = r.split(':')
        if o not in info: print(f'no object {o} in {objdir}'); return 1
        todo.append((o, f))
    while todo:
        o, f = todo.pop()
        if (o, f) in seen: continue
        seen.add((o, f))
        rel, sz, fn, da = info[o]
        sec = fn.get(f)
        if sec is None: continue
        if sec.startswith('.text.'): want.add((o, f))   # (functions already in .iram1 need no entry)
        for s in (sec, literal_of(sec)):
            for t in rel.get(s, []):
                if t in sz:                                 # a section: what is in it
                    if t.startswith('.rodata.') and not t.startswith('.rodata.str'):
                        data.add((o, t[len('.rodata.'):]))
                    elif t.startswith('.text.') or t.startswith('.iram1'):
                        for g, gs in fn.items():
                            if gs == t: todo.append((o, g))
                    continue
                if t in fn: todo.append((o, t)); continue
                if t in da:
                    if da[t].startswith('.rodata.'): data.add((o, da[t][len('.rodata.'):]))
                    continue
                others = [x for x in gfunc.get(t, ()) if x != o]
                if others:
                    for x in others: todo.append((x, t))
                elif not t.startswith('.') and not any(t in i[3] for i in info.values()):
                    outside.add(t)
    code = sum(info[o][1].get(info[o][2][f], 0) + info[o][1].get(literal_of(info[o][2][f]), 0) for o, f in want)
    tables = sum(info[o][1].get(f'.rodata.{f}', 0) for o, f in data)
    lines = [f'# Generated by tools/iram_lf.py from the audio path\'s entry points: {" ".join(roots)}',
             f'# {len(want)} functions ({code} bytes of code to IRAM), {len(data)} tables ({tables} bytes to DRAM); functions marked',
             '# IRAM_ATTR are there already. Rebuild, then run it again after changing what the sound calls.',
             f'[mapping:{name}]', 'archive: libmain.a', 'entries:']
    # ldgen takes plain identifiers only: GCC's clones (f$part$0, f$constprop$0) cannot be named. A clone of a function
    # marked IRAM_ATTR stays in IRAM with it; one that is not is reported, to be marked in the source if it runs per sample.
    ok = lambda s: re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', s)
    skipped = sorted(f'{o}:{f}' for o, f in want | data if not ok(f))
    for o, f in sorted(want):
        if ok(f): lines.append(f'    {o}:{f} (noflash)')
    for o, f in sorted(data):
        if ok(f): lines.append(f'    {o}:{f} (noflash)')
    open(out, 'w').write('\n'.join(lines) + '\n')
    print(f'{out}: {len(want)} functions, {code} bytes of code; {len(data)} tables, {tables} bytes')
    if skipped: print('left in flash (clones and local tables; mark the function IRAM_ATTR if it runs per sample):', ' '.join(skipped))
    libs = sorted(s for s in outside if not s.startswith('__') and s not in ('memset', 'memcpy', 'memmove'))
    if libs: print('called outside libmain (left where ESP-IDF puts them):', ' '.join(libs))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
