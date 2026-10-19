#!/usr/bin/env python3
"""Exports the firmware's pixel fonts (firmware/main/fonts/*.txt, TWOFOLD's faces) to ui/fonts.js, for the
window's pixel lettering. Run it after a glyph changes."""
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', '..', 'firmware', 'main', 'fonts')
OUT = os.path.join(HERE, '..', 'ui', 'fonts.js')


def face(name):
    meta, glyphs, cur, rows = {}, {}, None, []
    for line in open(os.path.join(FONTS, f'{name}.txt'), encoding='utf-8'):
        line = line.rstrip('\n')
        if cur is not None and line and set(line) <= set('#.'):
            rows.append(line)                                    # a glyph row: '#' lit, '.' unlit
            continue
        if line.startswith('#') or not line:
            continue
        if line.startswith('@glyph'):
            if cur is not None:
                glyphs[cur] = rows
            cur, rows = line.split(' ', 1)[1], []
            if cur.startswith('0x'):
                cur = None                                       # the firmware's private symbols
            continue
        if line.startswith('@'):
            k, v = (line[1:].split(' ', 1) + [''])[:2]
            meta[k] = v
    if cur is not None:
        glyphs[cur] = rows
    return {'height': int(meta['height']), 'spacing': int(meta['spacing']), 'space': int(meta['space']), 'glyphs': glyphs}


out = {n: face(n) for n in ('small', 'med', 'big')}
with open(OUT, 'w') as f:
    f.write("// The firmware's pixel fonts (firmware/main/fonts), exported by tools/fonts.py.\n")
    f.write('window.PXFONTS = ' + json.dumps(out, separators=(',', ':')) + ';\n')
print('ui/fonts.js:', ', '.join(f"{k} {len(v['glyphs'])} glyphs" for k, v in out.items()))
