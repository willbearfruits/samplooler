#!/usr/bin/env python3
"""Build the Samplooler boards from nothing: library, schematics, PCBs, routing, checks, fab files.

    python3 hardware/gen/build.py                 # every design
    python3 hardware/gen/build.py stack           # the two-board stack (bottom + top)
    python3 hardware/gen/build.py mini            # the one-board Mini
    python3 hardware/gen/build.py --check         # only the mechanical model
    python3 hardware/gen/build.py mini --no-route # placement only

Designs live in stack.py and mini.py; the steps they share are in kit.py.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pcbnew        # noqa: E402
import kit           # noqa: E402
import stack         # noqa: E402
import mini          # noqa: E402

VARIANTS = {'stack': stack, 'mini': mini}


def mech_report(name, variant):
    errors, trims, order = variant.mech_check()
    print(f'{name}: mechanical model: {len(errors)} collisions')
    for e in errors:
        print('    ', e)
    if trims:
        print(f'    trim flush: {", ".join(t.split("/", 1)[1] for t in trims)}')
    if order:
        print(f'    solder in this order: {" -> ".join(order)}')
    return errors


def build_board(spec, syms, route):
    d, sch_path, pcb_path = kit.paths(spec)
    kit.write_project(d, spec['project'], spec.get('rule_severities'))
    sch, y_end = kit.auto_sch(spec, syms, spec['groups'])
    fx = 20.0
    sch['flags'] = []
    for net in ('+5V', '+3V3', 'GND'):
        if any(net in p['nets'].values() for p in spec['parts'].values()):
            sch['flags'].append((net, (fx, y_end + 14.0)))
            fx += 30.0
    spec['sch'] = sch
    notes = []
    ny = y_end + 26.0
    for text in spec.get('notes', []):
        notes.append((text, (20.0, ny), 1.27))
        ny += 2.6 * (text.count('\n') + 1) + 2.0
    spec['notes_placed'] = notes
    sh = kit.schematic(dict(spec, notes=notes), syms)
    sh.save(sch_path, paper='A2' if ny > 270 else 'A3')
    errs, rpt = kit.erc(sch_path)
    nets = kit.netlist(sch_path)
    print(f'{spec["project"]}: ERC {len(errs)} findings')
    for e in errs[:30]:
        print('    ', e)
    bd = kit.make_board(spec, nets)
    spec['silk'](bd, spec)
    if spec.get('power'):
        spec['power'](bd)
    kit.fanout_gnd(bd, spec)
    pcbnew.SaveBoard(bd.path, bd.b, True)
    rep = kit.finish(bd, spec, route)
    rescued = 0
    for _ in range(3):                  # each pass can uncover the next pocket
        n = kit.rescue_gnd(bd.path)
        rescued += n
        if not n:
            break
    if rescued:
        rep['rescued'] = f'{rescued} GND pads/pockets tied to the planes'
    found, rpt = kit.drc(bd.path)
    print(f'{spec["project"]}: {rep}  DRC {len(found)} findings')
    for f in found[:40]:
        print('    ', f)
    print('    fab:', os.path.relpath(kit.fab_outputs(spec), kit.HW))
    return bd


def mating_check(bot, top, ref='J1'):
    """The top board's header must sit exactly over the bottom board's socket, pin for pin."""
    def pads(bd):
        fp = bd.fps[ref]
        return {p.GetNumber(): (round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3))
                for p in fp.Pads()}
    a, b = pads(bot), pads(top)
    bad = [n for n in a if a[n] != b.get(n)]
    print(f'stack: {ref} mating {"OK, pin for pin" if not bad else "MISMATCH on pins " + ", ".join(bad)}')
    return not bad


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('designs', nargs='*', choices=list(VARIANTS), default=[])
    ap.add_argument('--no-route', action='store_true')
    ap.add_argument('--check', action='store_true', help='mechanical model only')
    ap.add_argument('--force', action='store_true', help='build even if the mechanical model complains')
    args = ap.parse_args()
    chosen = args.designs or list(VARIANTS)

    dirs = [b['dir'] for v in VARIANTS.values() for b in v.BOARDS]
    locks = kit.locked_projects(dirs)
    if locks and not args.check:
        sys.exit('KiCad has these open: ' + ', '.join(os.path.relpath(f, kit.HW) for f in locks) +
                 '\nClose them in KiCad (without saving) and run again: the build rewrites every file.')

    # one shared library holds every design's parts
    syms = kit.build_library([m for v in VARIANTS.values() for m in v.LIB_EXTRA])
    failed = False
    for name in chosen:
        if mech_report(name, VARIANTS[name]) and not args.force:
            failed = True
    if args.check or failed:
        sys.exit(1 if failed else 0)
    for name in chosen:
        built = {spec['key']: build_board(spec, syms, not args.no_route) for spec in VARIANTS[name].BOARDS}
        if name == 'stack':
            mating_check(built['stack-bottom'], built['stack-top'])
