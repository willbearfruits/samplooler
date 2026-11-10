#!/usr/bin/env python3
"""Write the settings tables of docs/MANUAL.md from the param table in firmware/main/instrument.c, between the
<!-- tables --> and <!-- /tables --> markers, so the manual follows the code. A setting's MIDI CC comes from its
place in the table: CC 20 + p up to 43, then 70 + (p - 44)."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M = os.path.join(ROOT, 'firmware', 'main')
CONSOLES = ['BEEPER', 'ATARI', 'AY', 'NES', 'GAME BOY', 'C64', 'AMIGA', 'MEGA']

def split(row):
    """the fields of a C initializer row, commas inside quotes kept"""
    out, cur, q = [], '', False
    for ch in row:
        if ch == '"': q = not q
        if ch == ',' and not q: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def main():
    src = open(os.path.join(M, 'instrument.c')).read()
    song = open(os.path.join(M, 'inst_song.c')).read()
    labels = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
              for m in re.finditer(r'static const char \*const (\w+)\[[^\]]*\]\s*=\s*\{([^}]*)\}', src)}
    labels['con_l'] = CONSOLES
    labels['scale_l'] = re.findall(r'\[PX_SC_\w+\]\s*=\s*\{\s*"([^"]+)"', song)
    for k in ('lsnd_l', 'bsnd_l', 'csnd_l', 'dkit_l'): labels[k] = ['1', '2', '3', '4']
    groups = labels['groups']
    genum = re.search(r'enum \{ (G_TUNE[^}]*)\}', src).group(1)
    gidx = {g.strip(): i for i, g in enumerate(genum.split(',')) if g.strip() and g.strip() != 'G_N'}
    penum = re.search(r'enum \{\s*(P_CONSOLE.*?)P_N\s*\}', src, re.S).group(1)
    pidx = {p.strip(): i for i, p in enumerate(x for x in penum.replace('\n', ' ').split(',') if x.strip())}
    rows = []
    for m in re.finditer(r'^\s*\[(P_\w+)\]\s*=\s*\{(.*)\},\s*$', src, re.M):
        f = split(m.group(2).replace('ACT,', 'PK_ACT, PF_NOCC,'))
        name, tag, grp, kind, flags, lo, hi, step, dflt, unit, lab, hlp = f[:12]
        rows.append(dict(p=pidx[m.group(1)], name=name.strip('"'), tag=tag.strip('"'), group=gidx[grp], kind=kind, flags=flags,
                         lo=lo, hi=hi, dflt=dflt, unit=unit.strip('"'), labels=lab, help=hlp.strip('"') if hlp != 'NULL' else ''))
    rows.sort(key=lambda r: r['p'])
    out = []
    for gi, g in enumerate(groups):
        sel = [r for r in rows if r['group'] == gi and 'PF_HIDE' not in r['flags']]
        if not sel: continue
        out += ['', '**' + g + '**', '', '| Setting | Values | CC | |', '| - | - | - | - |']
        for r in sel:
            cc = '' if 'PF_NOCC' in r['flags'] or r['kind'] == 'PK_ACT' else str(20 + r['p'] if r['p'] < 44 else 70 + r['p'] - 44)
            if r['kind'] == 'PK_ACT': vals = 'press to do it'
            elif r['kind'] == 'PK_ENUM': vals = ', '.join(labels.get(r['labels'], ['?']))
            else: vals = f"{r['lo']} to {r['hi']}{(' ' + r['unit']) if r['unit'] else ''}"
            what = r['help'].capitalize().replace(' pal', ' PAL').replace(' i ', ' I ')
            if r['labels'] in ('lsnd_l', 'bsnd_l', 'csnd_l', 'dkit_l'): vals = "the console's own four (below)"
            out.append(f"| {r['name']} | {vals} | {cc} | {what} |")
    text = '\n'.join(out) + '\n'
    path = os.path.join(ROOT, 'docs', 'MANUAL.md')
    doc = open(path).read()
    a, b = doc.index('<!-- tables -->') + len('<!-- tables -->'), doc.index('<!-- /tables -->')
    new = doc[:a] + '\n' + text + '\n' + doc[b:]
    if '--check' in sys.argv:
        if new != doc: print('docs/MANUAL.md is out of date: run tools/manual_tables.py'); return 1
        print('docs/MANUAL.md is up to date'); return 0
    open(path, 'w').write(new)
    print(f'{len(rows)} settings in {len(groups)} pages')
    return 0

if __name__ == '__main__':
    sys.exit(main())
