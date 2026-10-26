"""The command registry (the Carapace pattern): every action of the rig is a named command with typed arguments. The
viewer, the CLI, OSC and the MCP server all run the same commands through Rig.run(), so the user and Claude always
act on, and see, the same bench. Arguments come from the type hints, their help from the docstring's Args section."""
from __future__ import annotations

import inspect
import re
import typing
from dataclasses import dataclass, field
from typing import Any, Callable


class RigError(Exception):
    """A command's refusal: a code, a plain message, and a hint at what to do instead."""

    def __init__(self, code: str, message: str, hint: str = ''):
        super().__init__(message)
        self.code, self.message, self.hint = code, message, hint


@dataclass
class Command:
    name: str
    fn: Callable
    summary: str
    args: dict[str, dict] = field(default_factory=dict)
    group: str = ''
    slow: bool = False                    # may take seconds (a recording, a test): runs off the socket's thread
    examples: list[dict] = field(default_factory=list)

    def schema(self) -> dict:
        props, req = {}, []
        for n, a in self.args.items():
            p = {k: v for k, v in a.items() if k in ('type', 'description', 'enum', 'default', 'items')}
            props[n] = p
            if a.get('required'):
                req.append(n)
        return {'name': self.name, 'summary': self.summary, 'slow': self.slow,
                'arguments': {'type': 'object', 'properties': props, 'required': req}, 'examples': self.examples}


REGISTRY: dict[str, Command] = {}

_JSON = {int: 'integer', float: 'number', str: 'string', bool: 'boolean', dict: 'object', list: 'array'}


def _json_type(tp) -> dict:
    origin = typing.get_origin(tp)
    if origin is typing.Union or str(origin) == "<class 'types.UnionType'>":
        inner = [a for a in typing.get_args(tp) if a is not type(None)]
        return _json_type(inner[0]) if inner else {}
    if origin is typing.Literal:
        vals = list(typing.get_args(tp))
        return {'type': _JSON.get(type(vals[0]), 'string'), 'enum': vals}
    if origin in (list, tuple):
        return {'type': 'array'}
    if origin is dict:
        return {'type': 'object'}
    return {'type': _JSON.get(tp, 'string')} if tp in _JSON else {}


def _doc_args(doc: str) -> tuple[str, dict[str, str]]:
    summary, args = (doc or '').strip(), {}
    m = re.search(r'\n\s*Args:\s*\n', summary)
    if m:
        body = summary[m.end():]
        summary = summary[:m.start()].strip()
        cur = None
        for line in body.splitlines():
            mm = re.match(r'\s{2,}(\w+):\s*(.*)', line)
            if mm:
                cur = mm.group(1)
                args[cur] = mm.group(2).strip()
            elif cur and line.strip():
                args[cur] += ' ' + line.strip()
    return ' '.join(summary.split()), args


def command(name: str, group: str = '', slow: bool = False, examples: list[dict] | None = None):
    """Register fn(rig, **args) as a command."""
    def deco(fn: Callable):
        hints = typing.get_type_hints(fn)
        summary, docs = _doc_args(fn.__doc__ or '')
        args = {}
        for pname, p in list(inspect.signature(fn).parameters.items())[1:]:
            a = _json_type(hints.get(pname, str))
            a['description'] = docs.get(pname, '')
            if p.default is inspect.Parameter.empty:
                a['required'] = True
            elif p.default is not None:
                a['default'] = p.default
            a['_type'] = hints.get(pname, str)
            args[pname] = a
        REGISTRY[name] = Command(name, fn, summary, args, group or name.split('.')[0], slow, examples or [])
        return fn
    return deco


def _coerce(v: Any, a: dict, name: str) -> Any:
    t = a.get('type')
    if v is None:
        return None
    try:
        if t == 'integer':
            return int(float(v)) if isinstance(v, str) else int(v)
        if t == 'number':
            return float(v)
        if t == 'boolean':
            if isinstance(v, str):
                return v.lower() in ('1', 'true', 'yes', 'on')
            return bool(v)
        if t == 'string':
            return str(v)
    except (TypeError, ValueError):
        raise RigError('cmd.invalid_arguments', f"'{name}' should be {t}, got {v!r}")
    if 'enum' in a and v not in a['enum']:
        raise RigError('cmd.invalid_arguments', f"'{name}' should be one of {a['enum']}, got {v!r}")
    return v


def bind(cmd: Command, args: dict) -> dict:
    args = dict(args or {})
    unknown = set(args) - set(cmd.args)
    if unknown:
        raise RigError('cmd.invalid_arguments', f"{cmd.name} has no argument {', '.join(sorted(unknown))}",
                       f"its arguments: {', '.join(cmd.args) or 'none'}")
    out = {}
    for n, a in cmd.args.items():
        if n in args:
            out[n] = _coerce(args[n], a, n)
        elif a.get('required'):
            raise RigError('cmd.invalid_arguments', f"{cmd.name} needs '{n}'", a.get('description', ''))
    return out


def get(name: str) -> Command:
    if name not in REGISTRY:
        close = [n for n in REGISTRY if name.split('.')[0] == n.split('.')[0]] or sorted(REGISTRY)[:12]
        raise RigError('cmd.unknown', f"no command '{name}'", 'try: ' + ', '.join(close))
    return REGISTRY[name]


def agents_md() -> str:
    """The command reference, from the registry (rig/AGENTS.md is written from this; it never drifts)."""
    lines = ['# Samplooler Rig: commands', '',
             'Generated by `slrig agents-md` from the registry. Every command answers `{"ok": true, "result": ...}` or',
             '`{"ok": false, "error": {"code", "message", "hint"}}`. The viewer, the CLI (`slrig run CMD k=v ...`), OSC',
             'and the MCP server (`rig_run`) all run these.', '']
    groups: dict[str, list[Command]] = {}
    for c in REGISTRY.values():
        groups.setdefault(c.group, []).append(c)
    for g in sorted(groups):
        lines += [f'## {g}', '']
        for c in sorted(groups[g], key=lambda c: c.name):
            lines.append(f'### `{c.name}`' + (' (slow)' if c.slow else ''))
            lines.append('')
            lines.append(c.summary)
            if c.args:
                lines += ['', '| argument | type | default | what |', '|---|---|---|---|']
                for n, a in c.args.items():
                    d = 'required' if a.get('required') else repr(a.get('default', None))
                    lines.append(f"| `{n}` | {a.get('type', '')} | {d} | {a.get('description', '')} |")
            for ex in c.examples:
                lines += ['', f'Example: `{ex}`']
            lines.append('')
    return '\n'.join(lines)
