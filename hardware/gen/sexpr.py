"""Minimal S-expression reader/writer for KiCad files.

Atoms come back as `Sym` (bare words) or `str` (quoted strings), so a round trip keeps
the quoting KiCad expects. Numbers stay as `Sym` text; we never do arithmetic on parsed files.
"""
import re


class Sym(str):
    """A bare (unquoted) atom."""


_TOKEN = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)


def parse(text):
    stack, cur = [], []
    pos = 0
    while True:
        m = _TOKEN.match(text, pos)
        if not m or m.end() == pos:
            break
        pos = m.end()
        lp, rp, q, a = m.groups()
        if lp:
            stack.append(cur)
            cur = []
        elif rp:
            done, cur = cur, stack.pop()
            cur.append(done)
        elif q is not None:
            cur.append(re.sub(r'\\(.)', lambda e: {'n': '\n', 't': '\t'}.get(e.group(1), e.group(1)), q))
        elif a is not None:
            cur.append(Sym(a))
    return cur[0] if len(cur) == 1 else cur


def _atom(x):
    if isinstance(x, Sym):
        return str(x)
    if isinstance(x, bool):
        return 'yes' if x else 'no'
    if isinstance(x, (int, float)):
        s = f'{x:.4f}'.rstrip('0').rstrip('.')
        return '0' if s in ('-0', '') else s
    return '"' + str(x).replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def dump(node, indent=0):
    if not isinstance(node, list):
        return _atom(node)
    # short lists of atoms stay on one line
    if all(not isinstance(c, list) for c in node):
        return '(' + ' '.join(_atom(c) for c in node) + ')'
    pad = '\t' * (indent + 1)
    head = []
    rest = list(node)
    while rest and not isinstance(rest[0], list):
        head.append(_atom(rest.pop(0)))
    out = '(' + ' '.join(head)
    for c in rest:
        out += '\n' + pad + dump(c, indent + 1)
    return out + '\n' + '\t' * indent + ')'


def find(node, key):
    """First child list whose head is `key`."""
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def find_all(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]
