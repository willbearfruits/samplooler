#!/usr/bin/env python3
"""Write the settings tables of docs/MANUAL.md from the param table in firmware/main/instrument.c, between the
<!-- tables --> and <!-- /tables --> markers, so the manual follows the code. Param p is on MIDI CC 20 + p up
to 43, then 70 + (p - 44)."""
import os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'firmware', 'main', 'instrument.c')).read()
labels = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
          for m in re.finditer(r'static const char \*const (\w+)\[[^\]]*\]\s*=\s*\{([^}]*)\}', src)}
groups = re.search(r'groups\[G_N\]\s*=\s*\{([^}]*)\}', src)
gnames = re.findall(r'"([^"]*)"', groups.group(1))
genum = re.search(r'enum \{ (G_VOICE[^}]*)\}', src).group(1).replace(' ', '').split(',')
body = src[src.index('params[P_N]'):]
body = body[:body.index('};')]
rows = []
for m in re.finditer(r'\[\w+\]\s*=\s*\{\s*"([^"]*)",\s*"([^"]{4})",\s*(\w+),\s*(PK_\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*"([^"]*)",\s*([^,]+),\s*(?:"([^"]*)"|NULL)\s*\}', body):
    rows.append(dict(name=m.group(1), tag=m.group(2), group=m.group(3), kind=m.group(4), flags=m.group(5),
                     min=m.group(6).strip(), max=m.group(7).strip(), unit=m.group(10), labels=m.group(11).strip(), help=m.group(12) or ''))
cc = lambda p: 20 + p if p < 44 else 70 + p - 44 if p < 94 else None
clean = lambda v: re.sub(r'f$', '', v)
out = []
for gi, g in enumerate(genum):
    if g == 'G_N': continue
    sel = [(i, r) for i, r in enumerate(rows) if r['group'] == g]
    if not sel: continue
    out += ['', '**' + gnames[gi] + '**', '', '| Setting | CC | |', '| - | - | - |']
    for i, r in sel:
        c = '' if 'PF_NOCC' in r['flags'] or r['kind'] == 'PK_ACT' else str(cc(i))
        what = r['help'].capitalize()
        if r['kind'] == 'PK_ENUM' and r['labels'] in labels: what += ': ' + ', '.join(labels[r['labels']])
        elif r['kind'] == 'PK_NUM': what += ' (%s..%s%s)' % (clean(r['min']), clean(r['max']), (' ' + r['unit']) if r['unit'] else '')
        if 'SYS' in r['flags']: what += '. Kept when the voice changes'
        out.append('| %s | %s | %s |' % (r['name'], c, what))
man = os.path.join(ROOT, 'docs', 'MANUAL.md')
text = open(man).read()
a, b = text.index('<!-- tables -->'), text.index('<!-- /tables -->')
text = text[:a] + '<!-- tables -->\n' + '\n'.join(out).strip('\n') + '\n' + text[b:]
open(man, 'w').write(text)
print('wrote', len(rows), 'settings into', man)
