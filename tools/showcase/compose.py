#!/usr/bin/env python3
"""compose.py <root>/work <out dir> [first last]: every frame of the showcase, 1920x1080, 24 fps.

Sections come from timeline.json and crossfade into each other. Sources:
  perf     frames-perf/perf_####.png (RGBA) + captions, live readout, the twin's screen as an inset
  stack    <pcb frames>/stack_####.png, mini: <pcb frames>/mini_####.png (the hardware video's shots)
  copper   <copper dir>/*.png, slow push in
  case     frames-case/case_####.png
  software site/full.png scrolled
  title, end: cards drawn here
"""
import csv
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W_ = sys.argv[1]
OUT = sys.argv[2]
FIRST = int(sys.argv[3]) if len(sys.argv) > 3 else 0
LAST = int(sys.argv[4]) if len(sys.argv) > 4 else None
ROOT = os.path.dirname(os.path.abspath(W_))          # make_showcase.sh's work dir
PCB = os.environ.get('PCB_FRAMES', os.path.join(ROOT, 'pcb'))
COPPER = os.environ.get('COPPER', os.path.join(ROOT, 'copper'))
T = json.load(open(os.path.join(W_, 'timeline.json')))
FPS, X = T['fps'], T['crossfade']
SEC = {s['name']: s for s in T['sections']}
W, H = 1920, 1080

F = '/usr/share/fonts/TTF/'
font = lambda name, size: ImageFont.truetype(F + name, size)
BOLD, REG, LIGHT, MONO = 'Roboto-Bold.ttf', 'Roboto-Regular.ttf', 'Roboto-Light.ttf', 'JetBrainsMonoNerdFont-Regular.ttf'

yy, xx = np.mgrid[0:H, 0:W]
r = np.sqrt(((xx - W / 2) / (W * 0.62)) ** 2 + ((yy - H * 0.45) / (H * 0.62)) ** 2).clip(0, 1)
c0, c1 = np.array([27, 31, 38]), np.array([5, 6, 8])
BG = Image.fromarray((c0 * (1 - r[..., None]) + c1 * r[..., None]).astype(np.uint8)).convert('RGBA')
band = np.zeros((H, W, 4), np.uint8)
g = np.clip((np.arange(H) - (H - 300)) / 300, 0, 1) ** 1.3
band[..., 3] = (g * 225)[:, None].astype(np.uint8)
band[..., :3] = (3, 3, 4)
BAND = Image.fromarray(band)


def load(path, cache={}):
    if path not in cache:
        if len(cache) > 64:
            cache.clear()
        cache[path] = Image.open(path).convert('RGBA') if os.path.exists(path) else None
    return cache[path]


def ease(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def caption(img, headline, detail, alpha=1.0, hud=None):
    """Lower third: a dark band, a headline and a detail line, faded by alpha."""
    if alpha <= 0:
        return img
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    layer.alpha_composite(BAND)
    d = ImageDraw.Draw(layer)
    d.text((92, H - 178), headline, font=font(BOLD, 56), fill=(242, 242, 242, 255))
    d.text((94, H - 104), detail, font=font(REG, 32), fill=(184, 192, 200, 255))
    if hud:
        d.text((W - 92, H - 104), hud, font=font(MONO, 30), fill=(127, 208, 138, 255), anchor='ra')
    if alpha < 1:
        a = np.array(layer)
        a[..., 3] = (a[..., 3] * alpha).astype(np.uint8)
        layer = Image.fromarray(a)
    img.alpha_composite(layer)
    return img


def fade_alpha(t, a, b, d=0.45):
    return min(ease((t - a) / d), ease((b - t) / d))


# ------------------------------------------------------------------ the twin's screen and state
raw = np.fromfile(os.path.join(W_, 'oled.raw'), dtype=np.uint8).reshape(-1, 64, 128)
top = np.zeros((64, 128), bool)
top[:16] = True
ON = np.where(top[..., None], np.array([255, 200, 40]), np.array([90, 170, 255]))
OFF = np.where(top[..., None], np.array([22, 18, 8]), np.array([6, 10, 22]))
ui = list(csv.DictReader(open(os.path.join(W_, 'ui.csv')), delimiter=' '))


def screen(t, k=3):
    i = min(max(int(t * 20) - 1, 0), len(raw) - 1)
    rgb = np.where(raw[i].astype(bool)[..., None], ON, OFF).astype(np.float32)
    rgb = np.repeat(np.repeat(rgb, k, 0), k, 1)
    rgb[k - 1::k] *= 0.6
    rgb[:, k - 1::k] *= 0.6
    return Image.fromarray(rgb.astype(np.uint8)).convert('RGBA'), ui[i]


# ------------------------------------------------------------------ sections
def title(lt):
    img = BG.copy()
    d = ImageDraw.Draw(img)
    a = ease(lt / 0.8)
    col = lambda c: tuple(int(v * a) for v in c)
    d.text((W / 2, H / 2 - 70), 'SAMPLOOLER', font=font(BOLD, 150), fill=col((242, 242, 242)), anchor='mm')
    d.text((W / 2, H / 2 + 50), 'a looper you play by tilting it', font=font(LIGHT, 50), fill=col((154, 163, 173)), anchor='mm')
    d.text((W / 2, H / 2 + 130), 'hardware  ·  firmware  ·  sound  ·  software', font=font(MONO, 28),
           fill=col((95, 107, 118)), anchor='mm')
    return img


def perf(lt):
    t = SEC['perf']['start'] + lt
    img = BG.copy()
    fr = load(os.path.join(ROOT, 'frames-perf', f'perf_{min(int(round(lt * FPS)), len(T["angles"]) - 1):04d}.png'))
    if fr:
        img.alpha_composite(fr)
    scr, st = screen(t)
    bez = Image.new('RGBA', (scr.width + 28, scr.height + 28), (12, 12, 14, 235))
    bez.alpha_composite(scr, (14, 14))
    img.alpha_composite(bez, (W - bez.width - 70, 60))
    ImageDraw.Draw(img).text((W - 70 - bez.width / 2, 60 + bez.height + 12), 'the firmware\'s own screen code, live',
                             font=font(REG, 22), fill=(140, 148, 156, 255), anchor='ma')
    for (a, b, head, det) in T['captions']:
        if a - 0.5 <= t <= b + 0.5:
            sp = float(st['speed_now'])
            state = int(st['state'])
            hud = None
            if state == 2:
                arrow = '▶' if sp >= 0 else '◀'
                hud = f"tilt {float(st['pitch']):+5.1f}°   roll {float(st['roll']):+5.1f}°   {arrow} {abs(sp):4.2f}×"
            elif state == 1:
                hud = '● REC'
            caption(img, head, det, fade_alpha(t, a, b), hud)
    return img


def frames_shot(prefix, n, captions):
    def render(lt):
        img = BG.copy()
        fr = load(os.path.join(PCB, f'{prefix}_{min(int(round(lt * FPS)), n - 1):04d}.png'))
        if fr:
            img.alpha_composite(fr)
        for a, b, head, det in captions:
            caption(img, head, det, fade_alpha(lt, a, b))
        return img
    return render


def copper(lt):
    boards = ['stack-bottom', 'stack-top', 'mini']
    seg = SEC['copper']['dur'] / 3
    k = min(int(lt / seg), 2)
    u = lt - k * seg
    im = load(os.path.join(COPPER, boards[k] + '.png'))
    img = Image.new('RGBA', (W, H), (6, 8, 11, 255))
    if im:
        z = 1.0 + 0.06 * u / seg
        h = int(960 * z)
        w = int(im.width * h / im.height)
        img.alpha_composite(im.resize((w, h), Image.LANCZOS), ((W - w) // 2, (H - h) // 2 - 20))
    a = min(ease(u / 0.35) if k > 0 else 1.0, ease((seg - u) / 0.35) if k < 2 else 1.0)
    if a < 1:
        img = Image.blend(Image.new('RGBA', (W, H), (6, 8, 11, 255)), img, a)
    return caption(img, 'Every net routed', 'a fixed escape under the SuperMini, the breakouts ordered to match, '
                   'GND poured on both layers', fade_alpha(lt, 0.2, SEC['copper']['dur'] - 0.3))


def case(lt):
    img = BG.copy()
    fr = load(os.path.join(ROOT, 'frames-case', f'case_{min(int(round(lt * FPS)), 191):04d}.png'))
    if fr:
        img.alpha_composite(fr)
    return caption(img, 'The C3 build', 'the same firmware on an ESP32-C3 in a printed case: 16 kHz, 72×40 screen',
                   fade_alpha(lt, 0.3, SEC['case']['dur'] - 0.3))


SITE = None


def software(lt):
    global SITE
    if SITE is None:
        SITE = Image.open(os.path.join(ROOT, 'site', 'full.png')).convert('RGBA')
        SITE = SITE.resize((W, int(SITE.height * W / SITE.width)), Image.LANCZOS)
    dur = SEC['software']['dur']
    span = 3900
    y = int(span * ease(lt / dur))
    img = SITE.crop((0, y, W, y + H))
    caps = [(0.2, 4.8, 'willbearfruits.github.io/samplooler', 'flash it from the browser, or try the engine right there on the page'),
            (5.0, dur - 0.2, 'Plug it into a computer', 'the S3 is a USB MIDI device and sound card; a VST3 plugin mirrors every setting')]
    for a, b, head, det in caps:
        caption(img, head, det, fade_alpha(lt, a, b))
    return img


def end(lt):
    img = BG.copy()
    d = ImageDraw.Draw(img)
    a = ease(lt / 0.6)
    col = lambda c: tuple(int(v * a) for v in c)
    d.text((W / 2, 210), 'SAMPLOOLER', font=font(BOLD, 110), fill=col((242, 242, 242)), anchor='mm')
    lines = [('firmware', 'ESP-IDF, one tree for the S3 and the C3, tape and grain engines'),
             ('hardware', 'two PCB designs generated in Python: ERC 0, DRC 0, every net routed'),
             ('case', 'printable, from one OpenSCAD file'),
             ('software', 'browser flasher and demo, VST3 plugin, a UI simulator'),
             ('this video', 'the sound is the firmware\'s own DSP, run on a computer')]
    y = 370
    for k, v in lines:
        d.text((560, y), k, font=font(MONO, 30), fill=col((127, 208, 138)), anchor='ra')
        d.text((600, y), v, font=font(REG, 32), fill=col((200, 206, 212)), anchor='la')
        y += 70
    d.text((W / 2, 860), 'willbearfruits / samplooler', font=font(LIGHT, 34), fill=col((110, 120, 130)), anchor='mm')
    return img


RENDER = {
    'title': title, 'perf': perf,
    'stack': frames_shot('stack', 265, [(0.3, 4.2, 'Inside: two boards', 'the stack, 11 mm apart on standoffs, joined by a 2×4 socket'),
                                        (4.6, 10.3, 'Bottom board: every module', 'SuperMini on pogo pins, PCM5102 DAC, MPU-6050, joystick, encoder')]),
    'mini': frames_shot('mini', 241, [(0.3, 3.9, 'The Mini', 'one 82 × 56 mm board, parts on both faces, no joystick'),
                                      (4.3, 9.4, 'Its back: the modules', 'the build works out the order to solder them in')]),
    'copper': copper, 'case': case, 'software': software, 'end': end,
}


def frame(i):
    t = i / FPS
    active = [s for s in T['sections'] if s['start'] - 1e-6 <= t < s['start'] + s['dur']]
    imgs = []
    for s in active:
        lt = t - s['start']
        im = RENDER[s['name']](lt).convert('RGBA')
        w = 1.0
        if lt < X and s['start'] > 0:
            w = ease(lt / X)
        imgs.append((im, w))
    if not imgs:
        return Image.new('RGB', (W, H))
    out = imgs[0][0]
    for im, w in imgs[1:]:
        out = Image.blend(out, im, w)
    if t > T['total'] - 1.2:
        out = Image.blend(Image.new('RGBA', (W, H), (0, 0, 0, 255)), out, max(0.0, (T['total'] - t) / 1.2))
    return out.convert('RGB')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    n = int(T['total'] * FPS)
    last = n - 1 if LAST is None else min(LAST, n - 1)
    for i in range(FIRST, last + 1):
        frame(i).save(os.path.join(OUT, f'{i:05d}.png'), compress_level=1)
    print(f'{last - FIRST + 1} frames')
