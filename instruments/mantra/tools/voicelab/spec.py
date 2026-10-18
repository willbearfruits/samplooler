#!/usr/bin/env python3
"""spec.py in.wav out.png [t0 t1 [fmax]]: a spectrogram (and the waveform) to look at."""
import sys, wave, numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
w = wave.open(sys.argv[1]); sr = w.getframerate(); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) / 32768.0
t0 = float(sys.argv[3]) if len(sys.argv) > 3 else 0; t1 = float(sys.argv[4]) if len(sys.argv) > 4 else len(x) / sr
fmax = float(sys.argv[5]) if len(sys.argv) > 5 else 8000
x = x[int(t0 * sr):int(t1 * sr)]
fig, ax = plt.subplots(2, 1, figsize=(14, 7), gridspec_kw={"height_ratios": [1, 3]}, sharex=True)
ax[0].plot(np.arange(len(x)) / sr + t0, x, lw=0.4); ax[0].set_ylim(-max(1e-3, abs(x).max()), max(1e-3, abs(x).max()))
n = 1024; hop = 64; win = np.hanning(n)
frames = [x[i:i + n] * win for i in range(0, len(x) - n, hop)]
S = 20 * np.log10(np.abs(np.fft.rfft(frames, axis=1)).T + 1e-9)
S -= S.max()
ax[1].imshow(S, origin="lower", aspect="auto", extent=[t0, t0 + len(frames) * hop / sr, 0, sr / 2], vmin=-80, vmax=0, cmap="magma")
ax[1].set_ylim(0, fmax); ax[1].set_xlabel("s"); ax[1].set_ylabel("Hz")
plt.tight_layout(); plt.savefig(sys.argv[2], dpi=70)
