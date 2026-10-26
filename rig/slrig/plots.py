"""PNG plots in the Samplooler app's look (app/ui/app.css: black ground, warm ink, the instruments' colours). Built on
matplotlib's Figure and its Agg canvas directly: no pyplot, no window, safe in the daemon's threads. Every function
returns the PNG's path.

The series colours are the app's, in a fixed order checked for colour-blind separation on the panel colour (the
dataviz validator: adjacent pairs pass protan/deutan and normal vision; they are lighter than its band, which is the
app's choice of pastels on black)."""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from matplotlib.figure import Figure

from . import analysis

BG, PANEL, INK, MUTE, DIM, LINE = '#060606', '#0d0d0c', '#ece8de', '#9a968c', '#5e5b55', '#232220'
BAND = '#161614'                              # a test step's shade
BAD = '#ef5b5b'                               # status only: clicks, saturation
SERIES = ('#f2c94c', '#a990f0', '#5fd9cf', '#f29b50')   # yellow, violet, teal, orange: always in this order
AXIS = {'x': '#f29b50', 'y': '#5fd9cf', 'z': '#a990f0'}   # device x, y, z
DASHES = ('-', (0, (5, 2)), (0, (2, 2)), (0, (1, 3)))      # A/B lines that sit on each other stay visible
EVENT_GROUPS = {'enc': SERIES[0], 'rec': SERIES[3], 'tilt': SERIES[2], 'roll': SERIES[2], 'joy': SERIES[1],
                'freeze': SERIES[1], 'menu': SERIES[1], 'take': SERIES[3]}
FONT = {'family': 'DejaVu Sans', 'size': 9}


def _fig(w: float, h: float) -> Figure:
    fig = Figure(figsize=(w, h), dpi=120, facecolor=BG)
    return fig


def _style(ax, ylabel: str = '') -> None:
    ax.set_facecolor(PANEL)
    for s in ('top', 'right'):
        ax.spines[s].set_visible(False)
    for s in ('left', 'bottom'):
        ax.spines[s].set_color(LINE)
    ax.tick_params(colors=MUTE, labelsize=8, length=3, width=0.6)
    ax.grid(True, axis='y', color=LINE, linewidth=0.6)
    ax.set_axisbelow(True)
    if ylabel:
        ax.set_ylabel(ylabel, color=MUTE, fontsize=8.5)


def _legend(ax, loc: str = 'upper left') -> None:
    h, lab = ax.get_legend_handles_labels()
    if len(h) >= 2:
        lg = ax.legend(loc=loc, ncol=len(h), frameon=False, fontsize=8, handlelength=1.6, columnspacing=1.2,
                       borderaxespad=0.2)
        for t in lg.get_texts():
            t.set_color(MUTE)


def _title(fig: Figure, title: str | None, sub: str | None = None) -> None:
    if title:
        fig.text(0.012, 0.995, title, color=INK, fontsize=11.5, ha='left', va='top', fontweight='bold')
    if sub:
        fig.text(0.988, 0.995, sub, color=MUTE, fontsize=8.5, ha='right', va='top')


def _envelope(t: np.ndarray, y: np.ndarray, n: int = 2500) -> tuple[np.ndarray, np.ndarray]:
    """Long series cut to about 2n points, keeping each bin's minimum and maximum so peaks survive."""
    if len(t) <= 2 * n:
        return t, y
    k = len(t) // n
    m = (len(t) // k) * k
    tb, yb = t[:m].reshape(-1, k), y[:m].reshape(-1, k)
    lo, hi = yb.min(1), yb.max(1)
    tt = np.repeat(tb[:, k // 2], 2)
    yy = np.column_stack([lo, hi]).ravel()
    return tt, yy


def _save(fig: Figure, out_png) -> str:
    out = Path(out_png)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, facecolor=BG, dpi=120)
    return str(out)


def _no_data(ax, text: str) -> None:
    ax.text(0.5, 0.5, text, transform=ax.transAxes, ha='center', va='center', color=DIM, fontsize=9)


# ---------------------------------------------------------------- a session
PANELS = {
    'acc': 'acc (g)', 'gyro': 'gyro (deg/s)', 'angles': 'angle (deg)', 'speed': 'speed (x)', 'fx': 'roll fx (0..1)',
    'joy': 'joystick', 'yaw': 'yaw (deg)', 'levels': 'level (0..1)',
}


def plot_session(sess, fields=('acc', 'gyro', 'angles', 'speed'), t0=None, t1=None, out_png=None,
                 title: str | None = None) -> str:
    """Stacked panels over time (acc, gyro, angles, speed, fx, joy, yaw, levels), the guided test's steps shaded, and the
    gesture events as ticks along the bottom. sess is a Session or a session folder."""
    if not isinstance(sess, analysis.Session):
        sess = analysis.load_session(sess)
    fields = [f for f in fields if f in PANELS] or ['acc', 'gyro', 'angles', 'speed']
    mt, st = sess.motion['t'], sess.state.get('t', np.zeros(0))
    lo = t0 if t0 is not None else min([x[0] for x in (mt, st) if len(x)] or [0.0])
    hi = t1 if t1 is not None else max([x[-1] for x in (mt, st) if len(x)] or [1.0])
    if hi <= lo:
        hi = lo + 1.0
    mm, sm = analysis._win(mt, lo, hi), analysis._win(st, lo, hi) if len(st) else np.zeros(0, dtype=bool)
    evs = [e for e in sess.events if lo <= float(e.get('t', 0)) <= hi]
    n = len(fields)
    ratios = [1.0] * n + ([0.5] if evs else [])
    fig = _fig(11, 1.75 * n + (0.9 if evs else 0) + 0.55)
    gs = fig.add_gridspec(len(ratios), 1, height_ratios=ratios, hspace=0.16, left=0.07, right=0.93 if evs else 0.985,
                          top=0.93, bottom=0.09 if evs else 0.08)
    axes = [fig.add_subplot(gs[0])]
    for i in range(1, len(ratios)):
        axes.append(fig.add_subplot(gs[i], sharex=axes[0]))
    steps = [s for s in sess.steps() if s.get('t1', 0) >= lo and s.get('t0', 0) <= hi]

    def line(ax, t, y, label, color, lw=1.1, alpha=1.0, ls='-'):
        tt, yy = _envelope(t, y)
        ax.plot(tt, yy, color=color, lw=lw, alpha=alpha, ls=ls, label=label)

    for ax, f in zip(axes, fields):
        _style(ax, PANELS[f])
        for i, s in enumerate(steps):
            if i % 2 == 0:
                ax.axvspan(s['t0'], s['t1'], color=BAND, zorder=0, lw=0)
        if f in ('acc', 'gyro'):
            if not mm.any():
                _no_data(ax, 'no motion recorded')
                continue
            data = sess.acc if f == 'acc' else sess.gyro
            for k, a in enumerate(analysis.AXES):
                line(ax, mt[mm], data[mm, k], a, AXIS[a], lw=0.9)
            if f == 'gyro' and sess.raw is not None:
                lim = 32768 / sess.lsb[1]
                if np.abs(data[mm]).max() > 0.9 * lim:
                    ax.axhline(lim, color=BAD, lw=0.8, ls=':')
                    ax.axhline(-lim, color=BAD, lw=0.8, ls=':', label='range')
        elif f == 'angles':
            have = False
            for k, (nm, c) in enumerate((('pitch', SERIES[0]), ('roll', SERIES[1]))):
                y = sess.col(nm)
                if y is not None and sm.any():
                    line(ax, st[sm], y[sm], nm, c, lw=1.4)
                    have = True
            if mm.any():
                p, r = analysis.acc_angles(sess.acc[mm] - sess.acc_offset_dev())
                line(ax, mt[mm], p, 'pitch, gravity alone', SERIES[0], lw=0.7, alpha=0.45)
                line(ax, mt[mm], r, 'roll, gravity alone', SERIES[1], lw=0.7, alpha=0.45)
                have = True
            if not have:
                _no_data(ax, 'no angles recorded')
        else:
            cols = {'speed': (('speed_cmd', 'asked'), ('speed_now', 'now')), 'fx': (('delay', 'delay'), ('stutter', 'stutter')),
                    'joy': (('joy_x', 'x'), ('joy_y', 'y')), 'yaw': (('yaw', 'yaw'),),
                    'levels': (('in_peak', 'in'), ('out_peak', 'out'))}[f]
            have = False
            for k, (c, lab) in enumerate(cols):
                y = sess.col(c)
                if y is not None and sm.any():
                    line(ax, st[sm], y[sm], lab, SERIES[[0, 2][k] if f == 'speed' else k], lw=1.3)
                    have = True
            if f == 'speed' and have:
                ax.axhline(0, color=DIM, lw=0.6)
            if not have:
                _no_data(ax, f'no {f} recorded')
        _legend(ax)
        if ax is not axes[-1]:
            ax.tick_params(labelbottom=False)
    if steps:
        for s in steps:
            axes[0].text((s['t0'] + s['t1']) / 2, 1.02, s.get('label', ''), transform=axes[0].get_xaxis_transform(),
                         ha='center', va='bottom', color=MUTE, fontsize=7.5, clip_on=False)
    if evs:
        ax = axes[-1]
        ax.set_facecolor(BG)
        for s in ('top', 'right', 'left'):
            ax.spines[s].set_visible(False)
        ax.spines['bottom'].set_color(LINE)
        ax.set_yticks([])
        ax.tick_params(colors=MUTE, labelsize=8)
        seen = {}
        labelled = len(evs) <= 30
        for e in evs:
            g = str(e.get('name', '')).split('.')[0]
            c = EVENT_GROUPS.get(g, DIM)
            ax.vlines(float(e['t']), 0, 0.3 if labelled else 1, color=c, lw=1.0)
            seen[g] = c
            if labelled:                     # the name, standing on its tick
                ax.text(float(e['t']), 0.34, str(e.get('name', '')), color=MUTE, fontsize=6.5, rotation=90,
                        ha='center', va='bottom')
        ax.set_ylim(0, 1)
        h = [ax.plot([], [], color=c, lw=2)[0] for c in seen.values()]
        lg = ax.legend(h, list(seen), loc='upper left', bbox_to_anchor=(1.0, 1.0), frameon=False, fontsize=7.5,
                       handlelength=1.0)
        for t in lg.get_texts():
            t.set_color(MUTE)
        ax.text(-0.01, 0.5, 'events', transform=ax.transAxes, ha='right', va='center', color=MUTE, fontsize=8)
    axes[-1].set_xlabel('time (s)', color=MUTE, fontsize=8.5)
    axes[0].set_xlim(lo, hi)
    _title(fig, title or sess.name, (sess.meta.get('test') or {}).get('name'))
    out = out_png or (sess.path / f'plot-{"-".join(fields)}.png')
    return _save(fig, out)


# ---------------------------------------------------------------- the curve
def plot_curve(curve, out_png, live=None, title: str | None = None) -> str:
    """The tilt to speed curve and the roll to delay and stutter curves (the `curve` command's JSON, a dict, a JSON
    string or a file), the dead zones shaded, full marked, detents dotted. live: {"pitch", "speed", "roll"} or
    (pitch, speed) puts the board's current point on it."""
    if isinstance(curve, (str, Path)):
        s = str(curve)
        curve = json.loads(s) if s.lstrip().startswith('{') else json.loads(Path(s).read_text())
    tl, ro = curve.get('tilt', {}), curve.get('roll', {})
    fig = _fig(11, 4.2)
    gs = fig.add_gridspec(1, 2, width_ratios=[1.35, 1], wspace=0.18, left=0.065, right=0.985, top=0.86, bottom=0.14)
    a1, a2 = fig.add_subplot(gs[0]), fig.add_subplot(gs[1])
    if isinstance(live, (tuple, list)):
        live = {'pitch': live[0], 'speed': live[1]}
    live = live or {}

    _style(a1, 'speed (x)')
    ang, spd = np.asarray(tl.get('angle', []), float), np.asarray(tl.get('speed', []), float)
    dead, full = float(tl.get('dead', 0)), float(tl.get('full', 45))
    a1.axvspan(-dead, dead, color=BAND, lw=0, zorder=0)
    for x in (-full, full):
        a1.axvline(x, color=DIM, lw=0.8, ls=':')
    a1.axhline(0, color=DIM, lw=0.6)
    if len(ang):
        a1.plot(ang, spd, color=SERIES[0], lw=2.0, label='speed')
        det = np.asarray(tl.get('detent', np.zeros(len(ang))), float)
        k = det > 0
        if k.any():
            a1.plot(ang[k], spd[k], ls='none', marker='o', ms=4, color=SERIES[1], label='detent holds')
        a1.text(full, a1.get_ylim()[1], ' full', color=MUTE, fontsize=8, va='top')
        a1.text(0, a1.get_ylim()[0], 'dead', color=MUTE, fontsize=8, ha='center', va='bottom')
    if live.get('pitch') is not None and live.get('speed') is not None:
        a1.plot([live['pitch']], [live['speed']], marker='o', ms=8, color=INK, mec=BG, mew=2, ls='none', label='now')
        a1.annotate(f'{live["speed"]:.2f}x', (live['pitch'], live['speed']), textcoords='offset points', xytext=(8, 6),
                    color=INK, fontsize=8.5)
    a1.set_xlabel('pitch (deg, + nose down)', color=MUTE, fontsize=8.5)
    a1.set_xlim(-90, 90)
    _legend(a1)
    a1.set_title('tilt to speed', color=INK, fontsize=9.5, loc='left')

    _style(a2, 'amount (0..1)')
    rang = np.asarray(ro.get('angle', []), float)
    rdead, rfull = float(ro.get('dead', 15)), float(ro.get('full', 45))
    a2.axvspan(-rdead, rdead, color=BAND, lw=0, zorder=0)
    for x in (-rfull, rfull):
        a2.axvline(x, color=DIM, lw=0.8, ls=':')
    if len(rang):
        a2.plot(rang, ro.get('delay', []), color=SERIES[1], lw=2.0, label='delay (left)')
        a2.plot(rang, ro.get('stutter', []), color=SERIES[2], lw=2.0, label='stutter (right)')
    if live.get('roll') is not None:
        r = float(live['roll'])
        y = np.interp(r, rang, np.asarray(ro.get('delay' if r < 0 else 'stutter', [0] * len(rang)), float)) if len(rang) else 0
        a2.plot([r], [y], marker='o', ms=8, color=INK, mec=BG, mew=2, ls='none', label='now')
    a2.set_xlabel('roll (deg, + right edge down)', color=MUTE, fontsize=8.5)
    a2.set_xlim(-90, 90)
    a2.set_ylim(-0.05, 1.1)
    _legend(a2)
    a2.set_title('roll to effects', color=INK, fontsize=9.5, loc='left')
    _title(fig, title or 'The curves', f'dead {dead:g} deg, full {full:g} deg; roll dead {rdead:g}, full {rfull:g}')
    return _save(fig, out_png)


# ---------------------------------------------------------------- replays side by side
def _read_series(src) -> dict:
    if isinstance(src, dict):
        return {k: np.asarray(v) for k, v in src.items()}
    p = Path(src)
    if p.is_dir():
        s = analysis.load_session(p)
        d = dict(s.state)
        if 'speed_cmd' in d:
            d.setdefault('speed', d['speed_cmd'])
        return d
    d = analysis._read_csv(p)
    if 'speed' in d:
        d.setdefault('speed_cmd', d['speed'])
    return d


def plot_replays(replays: dict, out_png, fields=('pitch', 'speed'), title: str | None = None) -> str:
    """The same gesture through different tuning: {label: replay CSV (twin --calc replay), session folder or dict of
    columns}, one panel per field, one line per label (at most four, in the fixed colour order)."""
    items = list(replays.items())[:4]
    data = [(lab, _read_series(src)) for lab, src in items]
    units = {'pitch': 'deg', 'roll': 'deg', 'yaw': 'deg', 'speed': 'x', 'u': '', 'delay': '0..1', 'stutter': '0..1'}
    fig = _fig(11, 1.9 * len(fields) + 0.6)
    gs = fig.add_gridspec(len(fields), 1, hspace=0.16, left=0.07, right=0.985, top=0.92, bottom=0.1)
    axes = []
    for i, f in enumerate(fields):
        ax = fig.add_subplot(gs[i], sharex=axes[0] if axes else None)
        axes.append(ax)
        _style(ax, f'{f} ({units.get(f, "")})' if units.get(f) else f)
        have = False
        for k, (lab, d) in enumerate(data):
            if f in d and 't' in d and len(d['t']):
                tt, yy = _envelope(np.asarray(d['t'], float), np.asarray(d[f], float))
                ax.plot(tt, yy, color=SERIES[k], lw=1.4 if k == 0 else 1.2, label=lab, ls=DASHES[k])
                have = True
        if not have:
            _no_data(ax, f'no {f} in these replays')
        _legend(ax)
        if i < len(fields) - 1:
            ax.tick_params(labelbottom=False)
    axes[-1].set_xlabel('time (s)', color=MUTE, fontsize=8.5)
    _title(fig, title or 'Replays', ', '.join(lab for lab, _ in data))
    return _save(fig, out_png)


# ---------------------------------------------------------------- audio
def plot_levels(x, sr: int | None = None, out_png=None, title: str | None = None, clicks=None, speed=None,
                window: float = 0.01) -> str:
    """The audio's peak and rms level over time (dBFS per 10 ms), clicks marked, and optionally the speed measured from
    a tone (speed_from_tone's result) below. x is samples (with sr) or a WAV path."""
    from . import audio_analysis as au
    if isinstance(x, (str, Path)):
        x, sr = au.read_wav(x)
    x = np.asarray(x, dtype=np.float64)
    chans = [x] if x.ndim == 1 else [x[:, c] for c in range(min(2, x.shape[1]))]
    has_speed = speed is not None and len(speed.get('t', [])) > 0
    fig = _fig(11, 3.0 + (1.9 if has_speed else 0))
    gs = fig.add_gridspec(2 if has_speed else 1, 1, height_ratios=[1.4, 1] if has_speed else [1], hspace=0.16,
                          left=0.07, right=0.985, top=0.88 if not has_speed else 0.91, bottom=0.14 if not has_speed else 0.1)
    ax = fig.add_subplot(gs[0])
    _style(ax, 'level (dBFS)')
    w = max(1, int(sr * window))
    names = ['', ''] if len(chans) == 1 else ['left ', 'right ']
    for c, (ch, nm) in enumerate(zip(chans, names)):
        k = len(ch) // w
        if k < 1:
            continue
        b = np.nan_to_num(ch[:k * w]).reshape(k, w)
        tt = (np.arange(k) + 0.5) * w / sr
        pk = 20 * np.log10(np.maximum(np.abs(b).max(1), 1e-5))
        rms = 20 * np.log10(np.maximum(np.sqrt((b * b).mean(1)), 1e-5))
        ax.plot(tt, pk, color=SERIES[2 * c], lw=1.0, label=f'{nm}peak')
        ax.plot(tt, rms, color=SERIES[2 * c + 1], lw=1.0, label=f'{nm}rms')
    ax.set_ylim(-96, 3)
    for ci, cl in enumerate(clicks or []):
        ax.axvline(float(cl['t']), color=BAD, lw=0.9, alpha=0.9, label='click' if ci == 0 else None)
    ax.set_xlim(0, len(chans[0]) / sr)
    _legend(ax)
    if has_speed:
        ax.tick_params(labelbottom=False)
        a2 = fig.add_subplot(gs[1], sharex=ax)
        _style(a2, 'speed (x)')
        a2.plot(np.asarray(speed['t']), np.asarray(speed['speed'], float), color=SERIES[0], lw=1.3, label='from the tone')
        if speed.get('median') is not None:
            a2.axhline(speed['median'], color=DIM, lw=0.8, ls='--', label=f'median {speed["median"]:.3f}x')
        _legend(a2)
        a2.set_xlabel('time (s)', color=MUTE, fontsize=8.5)
    else:
        ax.set_xlabel('time (s)', color=MUTE, fontsize=8.5)
    n_cl = len(clicks or [])
    _title(fig, title or 'Audio levels', f'{len(chans[0]) / sr:.1f} s at {sr} Hz' +
           (f', {n_cl} click{"" if n_cl == 1 else "s"}' if clicks is not None else ''))
    return _save(fig, out_png or 'levels.png')
