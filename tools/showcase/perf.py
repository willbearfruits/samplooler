"""The performance shot: the assembled stack, tilted and rolled exactly as the score drives the
twin, its OLED showing the twin's screen frame by frame.

    blender -b -P perf.py -- <work> <wrl dir> <out dir> [first last] [--preview]
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402
import common as C  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:]
WORK, WRL, OUT = argv[0], argv[1], argv[2]
PREVIEW = '--preview' in argv
rng = [int(a) for a in argv[3:5]] if len(argv) >= 5 and argv[3].isdigit() else None
T = json.load(open(os.path.join(WORK, 'timeline.json')))
angles = T['angles']
N = len(angles)

scene = C.reset(T['fps'])
C.world(0.12)
W, H, GAP, PCB = 90.0, 64.0, 11.0, 1.6

pivot = bpy.data.objects.new('pivot', None)           # rotates about the stack's centre
scene.collection.objects.link(pivot)
root = bpy.data.objects.new('stack', None)
scene.collection.objects.link(root)
root.parent = pivot
root.location = (-W / 2 * C.MM, H / 2 * C.MM, -(GAP / 2) * C.MM)
bot, _ = C.board('bottom', os.path.join(WRL, 'stack-bottom.wrl'), root)
top, top_cols = C.board('top', os.path.join(WRL, 'stack-top.wrl'), root)
top.location.z = (GAP + PCB) * C.MM
screen = top_cols.get(C.SCREEN_COLOR)
if screen:
    C.screen_material(screen, os.path.join(WORK, 'oled-perf'), N)
else:
    print('WARNING: no OLED screen mesh found')
brass = C.simple_material('standoff', (0.78, 0.74, 0.62), 1.0, 0.3)
steel = C.simple_material('screw', (0.55, 0.56, 0.58), 1.0, 0.25)
for i, (x, y) in enumerate([(4, 4), (W - 4, 4), (4, H - 4), (W - 4, H - 4)]):
    C.hex_prism(f'standoff{i}', x, -y, PCB / 2, PCB / 2 + GAP, 5.5, brass, root)
    C.hex_prism(f'screw_top{i}', x, -y, 0, 2.0, 5.5, steel, top, sides=32).location.z = (PCB / 2 + 1.0) * C.MM
    C.hex_prism(f'screw_bot{i}', x, -y, 0, 2.0, 5.5, steel, bot, sides=32).location.z = -(PCB / 2 + 1.0) * C.MM

# the box follows the score: pitch > 0 is nose down (the far, USB edge), roll < 0 is the left edge down
for f, (p, r) in enumerate(angles):
    pivot.rotation_euler = (-math.radians(p), math.radians(r), 0.0)
    pivot.keyframe_insert('rotation_euler', frame=f)
for fc in pivot.animation_data.action.fcurves if hasattr(pivot.animation_data.action, 'fcurves') else []:
    for kp in fc.keyframe_points:
        kp.interpolation = 'LINEAR'

c = Vector((0, 0.004, 0.004))
C.area_light('key', (-0.18, -0.24, 0.30), c, 14, 0.25)
C.area_light('fill', (0.25, -0.12, 0.14), c, 4, 0.3, (0.85, 0.9, 1.0))
C.area_light('rim', (0.05, 0.30, 0.22), c, 12, 0.2, (1.0, 0.92, 0.85))
cam = C.camera(50)
for f in range(0, N + 1, 12):
    u = f / N
    C.orbit(cam, c, 0.29 - 0.02 * C.ease(u), 58 - 6 * u, -14 + 24 * C.ease(u), f)   # the player's view, from above

C.render_setup(os.path.join(OUT, 'perf_'), 0, N - 1, PREVIEW)
if rng:
    scene.frame_start, scene.frame_end = rng
os.makedirs(OUT, exist_ok=True)
bpy.ops.render.render(animation=True)
