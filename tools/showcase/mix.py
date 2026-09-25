#!/usr/bin/env python3
"""mix.py <work>: the soundtrack, and a spectrogram to check it by eye.

Until the record press the room is heard (the mic, dry); from then on the box's line out, which
passes the mic through while recording. Master: gentle level, a fade out at the end, 48 kHz stereo WAV.
"""
import json
import os
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw

W = sys.argv[1]
T = json.load(open(os.path.join(W, 'timeline.json')))
SR = T['sr']
mic = np.fromfile(os.path.join(W, 'mic.f32'), dtype=np.float32)
dev = np.fromfile(os.path.join(W, 'out.f32'), dtype=np.float32)
n = int(T['total'] * SR)
mic, dev = np.pad(mic, (0, max(0, n - len(mic))))[:n], np.pad(dev, (0, max(0, n - len(dev))))[:n]
t = np.arange(n) / SR
x = np.where(t < T['t_rec'], mic, dev).astype(np.float64)

# master: soft knee above -6 dBFS, normalise to -1 dBFS, fades
x = np.tanh(x * 1.2) / 1.2
x *= 0.89 / max(1e-9, np.max(np.abs(x)))
fade_in = np.clip(t / 0.4, 0, 1)
end = T['total']
fade_out = np.clip((end - 0.3 - t) / 2.5, 0, 1)
x *= fade_in * fade_out

# 48 kHz: linear interpolation is fine at this oversampling ratio for a check; ffmpeg does the real one
x.astype(np.float32).tofile(os.path.join(W, 'soundtrack.f32'))
with wave.open(os.path.join(W, 'soundtrack.wav'), 'wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype('<i2').tobytes())

# spectrogram, 0..8 kHz, log magnitude, sections and captions marked
N, H = 1024, 320
win = np.hanning(N)
frames = (len(x) - N) // H
spec = np.empty((N // 2, frames), dtype=np.float32)
for i in range(frames):
    spec[:, i] = np.abs(np.fft.rfft(x[i * H:i * H + N] * win))[:N // 2]
db = 20 * np.log10(spec + 1e-6)
db = np.clip((db - db.max() + 80) / 80, 0, 1)
top = int(8000 / (SR / 2) * (N // 2))
img = (db[:top][::-1] * 255).astype(np.uint8)
im = Image.fromarray(img).resize((1800, 360)).convert('RGB')
rms = np.array([np.sqrt(np.mean(x[i:i + 3200] ** 2)) for i in range(0, len(x) - 3200, 3200)])
lvl = 20 * np.log10(rms + 1e-6)
canvas = Image.new('RGB', (1800, 520), (12, 12, 16))
canvas.paste(im, (0, 0))
d = ImageDraw.Draw(canvas)
px = lambda sec: int(sec / end * 1800)
for s in T['sections']:
    d.line([(px(s['start']), 0), (px(s['start']), 520)], fill=(90, 200, 90))
    d.text((px(s['start']) + 3, 364), s['name'], fill=(120, 230, 120))
for c in T['captions']:
    d.text((px(c[0]) + 2, 380 + (T['captions'].index(c) % 3) * 12), c[2][:16], fill=(230, 200, 90))
pts = [(px(i * 0.1), 515 - int((v + 60) * 1.6)) for i, v in enumerate(lvl)]
d.line(pts, fill=(90, 160, 255), width=1)
canvas.save(os.path.join(W, 'spectrogram.png'))
print('peak %.2f dBFS, loudest 0.1 s %.1f dBFS rms, quietest (after 1 s) %.1f' %
      (20 * np.log10(np.max(np.abs(x))), lvl.max(), lvl[10:-30].min()))
