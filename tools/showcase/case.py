"""The C3 build: the printed case from enclosure/samplooler.scad, turning; the lid lifts to show the
modules and closes again. The C3's 72x40 OLED shows a screen from the UI simulator.

    blender -b -P case.py -- <case stl dir> <c3 screen png> <out dir> [first last] [--preview]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Vector  # noqa: E402
import common as C  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:]
STL, SCREEN, OUT = argv[0], argv[1], argv[2]
PREVIEW = '--preview' in argv
rng = [int(a) for a in argv[3:5]] if len(argv) >= 5 and argv[3].isdigit() else None
FPS, N = 24, 192

scene = C.reset(FPS)
C.world(0.14)
pla = C.simple_material('pla', (0.40, 0.035, 0.012), 0.0, 0.5, 0.35)      # the scad's preview red-orange, linear
mats = {
    'shell': pla, 'lid': pla, 'plunger': C.simple_material('plunger', (0.9, 0.88, 0.84), 0.0, 0.5, 0.3),
    'dac': C.simple_material('dac', (0.10, 0.015, 0.30), 0.0, 0.45),
    'imu': C.simple_material('imu', (0.008, 0.05, 0.28), 0.0, 0.45),
    'esp': C.simple_material('esp', (0.05, 0.06, 0.07), 0.0, 0.4),
    'buttons': C.simple_material('buttons', (0.02, 0.02, 0.02), 0.0, 0.5),
    'mic': C.simple_material('mic', (0.02, 0.18, 0.08), 0.0, 0.45),
}
turn = bpy.data.objects.new('turn', None)
scene.collection.objects.link(turn)
case = bpy.data.objects.new('case', None)
scene.collection.objects.link(case)
case.parent = turn
lid_group = bpy.data.objects.new('lid_group', None)
scene.collection.objects.link(lid_group)
lid_group.parent = case
objs = {}
for name in ('shell', 'lid', 'plunger', 'dac', 'imu', 'esp', 'oled', 'buttons', 'mic'):
    bpy.ops.wm.stl_import(filepath=os.path.join(STL, name + '.stl'))
    ob = bpy.context.selected_objects[0]
    ob.name = name
    ob.scale = (C.MM, C.MM, C.MM)
    ob.parent = lid_group if name in ('lid', 'plunger') else case
    if name != 'oled':
        ob.data.materials.clear()
        ob.data.materials.append(mats[name])
        bpy.context.view_layer.objects.active = ob
        if name in ('shell', 'lid', 'plunger', 'mic', 'buttons'):
            bpy.ops.object.shade_auto_smooth(angle=math.radians(35))
    objs[name] = ob

# the C3 screen: a planar-mapped emissive panel on the OLED block
oled = objs['oled']
me = oled.data
co = np.empty(len(me.vertices) * 3, dtype=np.float32)
me.vertices.foreach_get('co', co)
co = co.reshape(-1, 3)
lo, hi = co.min(0), co.max(0)
uv = me.uv_layers.new(name='screen')
for loop in me.loops:
    x, y, _ = co[loop.vertex_index]
    # the panel's long side runs along x; the screen reads from the player's side
    uv.data[loop.index].uv = ((x - lo[0]) / (hi[0] - lo[0]), (y - lo[1]) / (hi[1] - lo[1]))
m = bpy.data.materials.new('c3screen')
m.use_nodes = True
nt = m.node_tree
for nd in list(nt.nodes):
    nt.nodes.remove(nd)
tex = nt.nodes.new('ShaderNodeTexImage')
tex.image = bpy.data.images.load(SCREEN)
tex.interpolation = 'Closest'
em = nt.nodes.new('ShaderNodeEmission')
em.inputs['Strength'].default_value = 2.0
out = nt.nodes.new('ShaderNodeOutputMaterial')
nt.links.new(tex.outputs['Color'], em.inputs['Color'])
nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
me.materials.clear()
me.materials.append(m)

# centre the case on the turntable
bpy.context.view_layer.update()
pts = []
for ob in (objs['shell'], objs['lid']):
    for v in ob.data.vertices[:]:
        pts.append(ob.matrix_world @ v.co)
a = np.array([tuple(p) for p in pts])
lo, hi = a.min(0), a.max(0)
case.location = (-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2])
size = float(max(hi - lo))

# animation: a slow half turn; the lid lifts off between 1.6 s and 2.8 s, back down 5.9 .. 7.1 s
for f, z in ((0, 0), (int(1.6 * FPS), 0), (int(2.8 * FPS), 0.022), (int(5.9 * FPS), 0.022), (int(7.1 * FPS), 0), (N, 0)):
    lid_group.location.z = z
    lid_group.keyframe_insert('location', frame=f)
for f in range(0, N + 1, 6):
    turn.rotation_euler.z = math.radians(-35 + 95 * C.ease(f / N))
    turn.keyframe_insert('rotation_euler', frame=f)

c = Vector((0, 0, 0.02))
C.area_light('key', (-0.16, -0.2, 0.26), c, 6, 0.25)
C.area_light('fill', (0.22, -0.1, 0.12), c, 2, 0.3, (0.85, 0.9, 1.0))
C.area_light('rim', (0.05, 0.25, 0.2), c, 7, 0.2, (1.0, 0.92, 0.85))
cam = C.camera(60)
for f in range(0, N + 1, 12):
    C.orbit(cam, c, size * 3.4 - 0.012 * C.ease(f / N), 34, -12, f)

C.render_setup(os.path.join(OUT, 'case_'), 0, N - 1, PREVIEW)
if rng:
    scene.frame_start, scene.frame_end = rng
os.makedirs(OUT, exist_ok=True)
bpy.ops.render.render(animation=True)
