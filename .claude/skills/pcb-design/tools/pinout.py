#!/usr/bin/env python3
"""pinout.py: markdown pinout tables for every breakout, as seen from the component side."""
import _repo  # noqa: F401  (puts hardware/gen on the path)
import stack
import mini


def lab(n):
    return {'+3V3': '3V3', '+5V': '5V', 'GND': 'GND', None: 'nc'}.get(n, n.replace('GPIO', '') if n else 'nc')


def vertical(nets, title):
    """Placed at 0 degrees: pin 1 top left, rows of (odd, even)."""
    out = [f'**{title}** (top to bottom)', '', '| | | |', '| -: | :-: | :-: |']
    for r in range(len(nets) // 2):
        out.append(f'| {2 * r + 1} · {2 * r + 2} | {lab(nets[2 * r])} | {lab(nets[2 * r + 1])} |')
    return '\n'.join(out)


def horizontal(nets, title):
    """Placed at 270 degrees: pin 1 top right, odd pins on the top row."""
    n = len(nets) // 2
    top = [lab(nets[2 * c]) for c in reversed(range(n))]
    bot = [lab(nets[2 * c + 1]) for c in reversed(range(n))]
    return '\n'.join([f'**{title}** (left to right; pin 1 is top right)', '',
                      '| |' + ' |' * n, '| - |' + ' :-: |' * n,
                      '| top | ' + ' | '.join(top) + ' |', '| bottom | ' + ' | '.join(bot) + ' |'])


print(vertical(stack.EXP_A_NETS, 'Stack, EXP A'), end='\n\n')
print(horizontal(stack.EXP_B_NETS, 'Stack, EXP B'), end='\n\n')
print(vertical(mini.EXP_A_NETS, 'Mini, EXP A'), end='\n\n')
print(horizontal(mini.EXP_B_NETS, 'Mini, EXP B'))
