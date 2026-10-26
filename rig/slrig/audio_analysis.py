"""Audio measurements (numpy only) for the board's USB output: levels, clicks, NaN and dead runs, and the pitch of a known
tone, which gives the tape's real speed. Samples are floats, full scale 1.0; mono (N,) or (N, channels)."""
from __future__ import annotations

import math
import struct
from pathlib import Path

import numpy as np


def _db(x: float, floor: float = -150.0) -> float:
    return round(max(20.0 * math.log10(x), floor), 2) if x > 0 else floor


# ---------------------------------------------------------------- files
def read_wav(path) -> tuple[np.ndarray, int]:
    """A WAV file as float64 in -1..1, (N,) for mono or (N, channels), and the sample rate. PCM 8/16/24/32-bit and
    32/64-bit float (WAVE_FORMAT_IEEE_FLOAT, which the stdlib's wave cannot read), WAVE_FORMAT_EXTENSIBLE too."""
    b = Path(path).read_bytes()
    if b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise ValueError(f'{path}: not a RIFF/WAVE file')
    fmt, data, i = None, None, 12
    while i + 8 <= len(b):
        cid, size = b[i:i + 4], struct.unpack('<I', b[i + 4:i + 8])[0]
        body = b[i + 8:i + 8 + size]
        if cid == b'fmt ':
            tag, ch, sr, _, _, bits = struct.unpack('<HHIIHH', body[:16])
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack('<H', body[24:26])[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b'data':
            data = body
        i += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError(f'{path}: no fmt or data chunk')
    tag, ch, sr, bits = fmt
    w = bits // 8
    n = len(data) // (w * ch)
    data = data[:n * w * ch]
    if tag == 3:
        x = np.frombuffer(data, dtype='<f4' if bits == 32 else '<f8').astype(np.float64)
    elif tag == 1 and bits == 8:
        x = (np.frombuffer(data, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, dtype='<i2').astype(np.float64) / 32768.0
    elif tag == 1 and bits == 24:
        u = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = u[:, 0] | (u[:, 1] << 8) | (u[:, 2] << 16)
        x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / float(1 << 23)
    elif tag == 1 and bits == 32:
        x = np.frombuffer(data, dtype='<i4').astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f'{path}: format {tag}, {bits} bits is not read here')
    x = x.reshape(-1, ch)
    return (x[:, 0].copy() if ch == 1 else x), int(sr)


def write_wav(path, x, sr: int, bits: int = 16) -> Path:
    """Write float samples (-1..1) as 16-bit PCM (bits=16) or 32-bit float (bits=32)."""
    x = np.asarray(x, dtype=np.float64)
    x2 = x.reshape(len(x), -1)
    ch = x2.shape[1]
    if bits == 32:
        tag, data = 3, x2.astype('<f4').tobytes()
    else:
        tag, data = 1, np.clip(np.round(x2 * 32768.0), -32768, 32767).astype('<i2').tobytes()
    w = bits // 8
    hdr = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' +
           struct.pack('<IHHIIHH', 16, tag, ch, sr, sr * ch * w, ch * w, bits) + b'data' + struct.pack('<I', len(data)))
    p = Path(path)
    p.write_bytes(hdr + data)
    return p


# ---------------------------------------------------------------- levels
def _channels(x) -> list[np.ndarray]:
    x = np.asarray(x, dtype=np.float64)
    return [x] if x.ndim == 1 else [x[:, c] for c in range(x.shape[1])]


def levels(x, sr: int, window: float = 0.05, clip_at: float = 0.999) -> dict:
    """Peak and rms (dBFS), the noise floor (the rms of the quietest 10 % of 50 ms windows), the DC offset and the clipped
    samples (|x| >= clip_at). A dict for mono; {"channels": [dict, ...]} for several channels."""
    chans = _channels(x)
    out = []
    for c in chans:
        c = c[np.isfinite(c)]
        if len(c) == 0:
            out.append({'n': 0})
            continue
        w = max(1, int(sr * window))
        k = len(c) // w
        if k >= 1:
            p = (c[:k * w].reshape(k, w) ** 2).mean(1)
            q = np.sort(p)[:max(1, k // 10)]
            floor = _db(math.sqrt(float(q.mean())))
        else:
            floor = _db(math.sqrt(float((c ** 2).mean())))
        a = np.abs(c)
        out.append({'n': len(c), 'secs': round(len(c) / sr, 3), 'peak_dbfs': _db(float(a.max())),
                    'rms_dbfs': _db(math.sqrt(float((c ** 2).mean()))), 'noise_floor_dbfs': floor,
                    'dc': round(float(c.mean()), 6), 'clipped': int((a >= clip_at).sum())})
    return out[0] if len(out) == 1 else {'channels': out}


# ---------------------------------------------------------------- clicks
def clicks(x, sr: int, k: float = 10.0, floor: float = 0.002, merge: float = 0.002, block: int = 256) -> list[dict]:
    """Discontinuities: samples whose second difference stands k times above the local level (the median |second
    difference| of the blocks around it) and above `floor`. A smooth signal's second difference is small; a jump, a
    dropped sample or a step stands out. Returns [{"t", "size", "dbfs"}], detections within `merge` s joined."""
    c = _channels(x)[0]
    c = np.nan_to_num(c)
    if len(c) < 3 * block:
        return []
    d2 = np.abs(c[2:] - 2 * c[1:-1] + c[:-2])
    nb = len(d2) // block
    med = np.median(d2[:nb * block].reshape(nb, block), axis=1)
    if len(d2) > nb * block:
        med = np.append(med, np.median(d2[nb * block:]))
    pad = np.pad(med, 4, mode='edge')
    local = np.median(np.lib.stride_tricks.sliding_window_view(pad, 9), axis=1)
    lvl = np.repeat(local, block)[:len(d2)]
    thr = np.maximum(k * lvl, floor)
    hits = np.nonzero(d2 > thr)[0]
    out, last = [], -1e9
    gap = int(merge * sr)
    for i in hits:
        if i - last > gap:
            out.append({'i': int(i + 1), 'size': float(d2[i])})
        elif d2[i] > out[-1]['size']:
            out[-1].update(i=int(i + 1), size=float(d2[i]))
        last = i
    return [{'t': round(o['i'] / sr, 5), 'size': round(o['size'], 5), 'dbfs': _db(o['size'])} for o in out]


# ---------------------------------------------------------------- bad samples
def bad_samples(x, sr: int | None = None, min_run: int = 64) -> dict:
    """NaN and inf counts, and runs of exactly zero (or one stuck value) at least min_run samples long: the dead
    stretches a broken buffer leaves. Times in seconds when sr is given, else sample indices."""
    c = np.asarray(_channels(x)[0], dtype=np.float64)
    nan, inf = int(np.isnan(c).sum()), int(np.isinf(c).sum())

    def runs(mask):
        if not mask.any():
            return []
        d = np.diff(np.concatenate([[0], mask.astype(np.int8), [0]]))
        s, e = np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]
        keep = (e - s) >= min_run
        return list(zip(s[keep], e[keep] - s[keep]))
    zeros = runs(c == 0)
    same = np.concatenate([[False], c[1:] == c[:-1]])
    stuck = [(s - 1, n + 1) for s, n in runs(same & (c != 0)) if n + 1 >= min_run]
    unit = (lambda i: round(float(i) / sr, 5)) if sr else (lambda i: int(i))
    return {'n': len(c), 'nan': nan, 'inf': inf,
            'zero_runs': [{'at': unit(s), 'len': int(n)} for s, n in zeros],
            'stuck_runs': [{'at': unit(s), 'len': int(n)} for s, n in stuck],
            'zero_samples': int(sum(n for _, n in zeros))}


# ---------------------------------------------------------------- pitch
def pitch_track(x, sr: int, fmin: float = 50.0, fmax: float = 2000.0, hop: float = 0.02, thresh: float = 0.15,
                gate_db: float = -60.0) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """YIN: the fundamental of each frame. Returns t (s, frame centres), f0 (Hz, NaN where unvoiced or too quiet) and
    confidence (1 - the normalised difference at the chosen lag, 0..1)."""
    c = np.nan_to_num(np.asarray(_channels(x)[0], dtype=np.float64))
    tmax = int(math.ceil(sr / fmin))
    tmin = max(2, int(math.floor(sr / fmax)))
    W = max(tmax, int(0.02 * sr))              # the integration window
    L = W + tmax + 2
    h = max(1, int(round(hop * sr)))
    if len(c) < L:
        return np.zeros(0), np.zeros(0), np.zeros(0)
    starts = np.arange(0, len(c) - L + 1, h)
    nfft = 1 << int(math.ceil(math.log2(L + W)))
    t_out, f_out, c_out = [], [], []
    gate = 10 ** (gate_db / 20.0)
    for b0 in range(0, len(starts), 256):     # in chunks, so long files stay small in memory
        st = starts[b0:b0 + 256]
        fr = np.lib.stride_tricks.sliding_window_view(c, L)[st]
        a = np.zeros((len(st), nfft))
        a[:, :W] = fr[:, :W]
        cross = np.fft.irfft(np.conj(np.fft.rfft(a, nfft)) * np.fft.rfft(fr, nfft), nfft)[:, :tmax + 2]
        cs = np.concatenate([np.zeros((len(st), 1)), np.cumsum(fr * fr, axis=1)], axis=1)
        tau = np.arange(tmax + 2)
        e0 = cs[:, W][:, None]
        et = cs[:, tau + W] - cs[:, tau]
        d = np.maximum(e0 + et - 2 * cross, 0.0)
        d[:, 0] = 0
        cum = np.cumsum(d[:, 1:], axis=1)
        dn = np.ones_like(d)
        dn[:, 1:] = d[:, 1:] * np.arange(1, tmax + 2) / np.maximum(cum, 1e-20)
        rms = np.sqrt(e0[:, 0] / W)
        for j in range(len(st)):
            row = dn[j]
            seg = row[tmin:tmax + 1]
            below = np.nonzero(seg < thresh)[0]
            if len(below):
                i = int(below[0]) + tmin
                while i + 1 <= tmax and row[i + 1] < row[i]:
                    i += 1                     # down to the bottom of this dip
            else:
                i = int(np.argmin(seg)) + tmin
            conf = float(max(0.0, 1.0 - row[i]))
            if 1 <= i < tmax + 1:              # a parabola through the raw difference refines the lag
                y0, y1, y2 = d[j, i - 1], d[j, i], d[j, i + 1]
                den = y0 - 2 * y1 + y2
                frac = 0.5 * (y0 - y2) / den if abs(den) > 1e-20 else 0.0
                lag = i + max(-1.0, min(1.0, frac))
            else:
                lag = float(i)
            ok = rms[j] > gate and len(below) > 0
            t_out.append((st[j] + W / 2.0) / sr)
            f_out.append(sr / lag if ok else np.nan)
            c_out.append(conf if ok else 0.0)
    return np.array(t_out), np.array(f_out), np.array(c_out)


def speed_from_tone(x, sr: int, tone_hz: float, smin: float = 0.12, smax: float = 8.5, hop: float = 0.02,
                    min_conf: float = 0.8) -> dict:
    """A tone of known pitch played through the tape: its pitch over time gives the real speed (f0 / tone_hz).
    Backwards sounds the same as forwards, so the speed is its size. Returns t and speed (arrays, NaN where no pitch),
    and the median, the quartiles and the share of frames with a pitch."""
    t, f0, conf = pitch_track(x, sr, fmin=tone_hz * smin, fmax=min(tone_hz * smax, sr / 2.5), hop=hop)
    sp = f0 / float(tone_hz)
    sp[conf < min_conf] = np.nan
    ok = np.isfinite(sp)
    out = {'t': t, 'speed': sp, 'voiced': round(float(ok.mean()), 3) if len(ok) else 0.0}
    if ok.any():
        q1, med, q3 = np.percentile(sp[ok], [25, 50, 75])
        out.update(median=round(float(med), 5), iqr=[round(float(q1), 5), round(float(q3), 5)],
                   iqr_cents=round(1200 * math.log2(q3 / q1), 2) if q1 > 0 else None)
    else:
        out.update(median=None, iqr=None, iqr_cents=None)
    return out
