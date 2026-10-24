#!/usr/bin/env python3
"""Write the settings tables of docs/MANUAL.md from the param table in firmware/main/instrument.c, between the
<!-- tables --> and <!-- /tables --> markers, so the manual follows the code. Setting i is on MIDI CC 20 + i up to
43, then 70 + (i - 44); actions and the link's switches have no CC."""
import os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M = os.path.join(ROOT, 'firmware', 'main')

def labels(src):
    out = {}
    for m in re.finditer(r'static const char \*const (\w+)\[[^\]]*\]\s*=\s*\{([^}]*)\}', src):
        out[m.group(1)] = re.findall(r'"([^"]*)"', m.group(2))
    return out

def params(src):
    rows = []
    body = src[src.index('params[P_N]'):]
    body = body[:body.index('};') + 2]
    defs = dict(re.findall(r'#define (\w+)\s+(\([^\n]*?\))', src))
    for k, v in defs.items(): body = re.sub(r'\b%s\b' % k, v.replace(',', ' '), body)
    for m in re.finditer(r'\{\s*"([^"]*)",\s*"([^"]{4})",\s*([^,]+),\s*(PK_\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*"([^"]*)",\s*([^,]+),\s*(?:"([^"]*)"|NULL)\s*\}', body):
        rows.append(dict(name=m.group(1), tag=m.group(2), group=m.group(3).strip(), kind=m.group(4), flags=m.group(5),
                         min=m.group(6).strip(), max=m.group(7).strip(), unit=m.group(10), labels=m.group(11).strip(), help=m.group(12) or ''))
    return rows

def cc(i): return 20 + i if i < 44 else 70 + i - 44 if i < 94 else ''
def clean(v): return re.sub(r'f$', '', v.strip())

src = open(os.path.join(M, 'instrument.c')).read()
lab = labels(src)
rows = params(src)
names = re.findall(r'"([^"]*)"', re.search(r'groups\[\]\s*=\s*\{([^}]*)\}', src).group(1))
enums = re.search(r'enum \{ (G_\w+(?:, G_\w+)*) \};', src).group(1).split(', ')
out = []
for gname, genum in zip(names, enums):
    sel = [(i, r) for i, r in enumerate(rows) if r['group'] == genum and 'PF_HIDE' not in r['flags']]
    if not sel: continue
    out += ['', '### ' + gname, '', '| Setting | CC | |', '| - | - | - |']
    for i, r in sel:
        c = '' if 'PF_NOCC' in r['flags'] or r['kind'] == 'PK_ACT' else str(cc(i))
        what = r['help'].capitalize() if r['help'] else ''
        if r['kind'] == 'PK_ENUM' and r['labels'] in lab: what += ': ' + ', '.join(lab[r['labels']])
        elif r['kind'] == 'PK_NUM': what += ' (%s..%s%s)' % (clean(r['min']), clean(r['max']), (' ' + r['unit']) if r['unit'] else '')
        out.append('| %s | %s | %s |' % (r['name'], c, what))

man = os.path.join(ROOT, 'docs', 'MANUAL.md')
text = open(man).read()
a, b = text.index('<!-- tables -->'), text.index('<!-- /tables -->')
text = text[:a] + '<!-- tables -->\n' + '\n'.join(out).strip('\n') + '\n' + text[b:]
open(man, 'w').write(text)
print('wrote', sum(1 for l in out if l.startswith('| ') and not l.startswith('| Setting') and not l.startswith('| -')), 'settings into', man)
