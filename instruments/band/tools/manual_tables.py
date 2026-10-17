#!/usr/bin/env python3
"""Write the settings tables of docs/MANUAL.md from the param tables in the C files (the layers' and the host's),
between the <!-- tables --> and <!-- /tables --> markers, so the manual follows the code. Each setting's MIDI CC
comes from its slot: the layers have 26 slots each (DRUMS 0, BASS 26, LEAD 52, KEYS 78, BREATH 104, HOWL 130), the host
follows at 156; slot p is on CC 20 + p up to 43, then 70 + (p - 44) up to 93; the rest have no CC."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M = os.path.join(ROOT, 'firmware', 'main')

def labels(src):
    out = {}
    for m in re.finditer(r'static const char \*const (\w+)\[[^\]]*\]\s*=\s*\{([^}]*)\}', src):
        out[m.group(1)] = re.findall(r'"([^"]*)"', m.group(2))
    return out

def params(src):
    """[(name, tag, group, kind, flags, min, max, unit, labels name, help)] in table order (designated or not)"""
    rows = []
    body = src[src.index('params[') :] if 'params[' in src else src
    for m in re.finditer(r'\{\s*"([^"]*)",\s*"([^"]{4})",\s*([^,]+),\s*(PK_\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*"([^"]*)",\s*([^,]+),\s*(?:"([^"]*)"|NULL)\s*\}', body):
        rows.append(dict(name=m.group(1), tag=m.group(2), group=m.group(3).strip(), kind=m.group(4), flags=m.group(5),
                         min=m.group(6).strip(), max=m.group(7).strip(), unit=m.group(10), labels=m.group(11).strip(), help=m.group(12) or ''))
    return rows

def cc(slot): return 20 + slot if slot < 44 else 70 + slot - 44 if slot < 94 else ''

def clean(v): return re.sub(r'f$', '', v.strip())

def table(rows, lab, base, page_of):
    lines = []
    for page in page_of:
        sel = [(i, r) for i, r in enumerate(rows) if page_of[page](r)]
        if not sel: continue
        lines += ['', '**' + page + '**', '', '| Setting | CC | |', '| - | - | - |']
        for i, r in sel:
            if 'PF_HIDE' in r['flags']: continue
            c = '' if 'PF_NOCC' in r['flags'] or r['kind'] == 'PK_ACT' else str(cc(base + i))
            what = r['help'].capitalize() if r['help'] else ''
            if r['kind'] == 'PK_ENUM' and r['labels'] in lab: what += ': ' + ', '.join(lab[r['labels']])
            elif r['kind'] == 'PK_NUM': what += ' (%s..%s%s)' % (clean(r['min']), clean(r['max']), (' ' + r['unit']) if r['unit'] else '')
            lines.append('| %s | %s | %s |' % (r['name'], c, what))
    return lines

out = []
layers = [('DRUMS', 'layer_drums.c', 0), ('BASS', 'layer_bass.c', 26), ('LEAD', 'layer_lead.c', 52), ('KEYS', 'layer_keys.c', 78),
          ('BREATH', 'layer_breath.c', 104), ('HOWL', 'layer_howl.c', 130)]
for name, fn, base in layers:
    src = open(os.path.join(M, fn)).read()
    lab = labels(src)
    rows = params(src)
    pages = re.search(r'pages\[2\]\s*=\s*\{\s*"([^"]*)",\s*"([^"]*)"', src)
    p0, p1 = (pages.group(1), pages.group(2)) if pages else (name, name + ' 2')
    out += ['', '### ' + name] + table(rows, lab, base, {p0: lambda r: r['group'] in ('0',), p1: lambda r: r['group'] not in ('0',)})
src = open(os.path.join(M, 'instrument.c')).read()
lab = labels(src)
host = src[src.index('host_params[H_N]'):]
rows = params(host[:host.index('};') + 2])
groups = {'SONG': 'G_SONG', 'MIX': 'G_MIX', 'FX': 'G_FX', 'LOOPS': 'G_LOOPS', 'LINK': 'G_LINK'}
out += ['', '### SONG, MIX, FX, LOOPS, LINK'] + table(rows, lab, 156, {k: (lambda g: lambda r: r['group'] == g)(v) for k, v in groups.items()})

man = os.path.join(ROOT, 'docs', 'MANUAL.md')
text = open(man).read()
a, b = text.index('<!-- tables -->'), text.index('<!-- /tables -->')
text = text[:a] + '<!-- tables -->\n' + '\n'.join(out).strip('\n') + '\n' + text[b:]
open(man, 'w').write(text)
print('wrote', sum(1 for l in out if l.startswith('| ') and not l.startswith('| Setting') and not l.startswith('| -')), 'settings into', man)
