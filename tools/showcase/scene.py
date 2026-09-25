"""Blender scene for the Samplooler showcase: the stack (exploding and reassembling) and the Mini
(turning and flipping), built from KiCad's VRML exports.

    blender -b -P scene.py -- <wrl dir> <shot: stack|mini> <out dir> [first last] [--preview] [--eevee]

--eevee: ~0.8 s/frame at 1080p on the RTX 3060 (Cycles, the default: ~8 s/frame). Frames are RGBA:
assemble.sh composites them over a dark gradient.

Board frame from the exports: x right, y = -(board y), z up, mm; everything here is metres.
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector, Euler

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vrml  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:]
WRL, SHOT, OUT = argv[0], argv[1], argv[2]
PREVIEW = '--preview' in argv
rng = [int(a) for a in argv[3:5]] if len(argv) >= 5 and argv[3].isdigit() else None

MM = 0.001
FPS = 24
GAP = 11.0                      # stack: socket + header plastic
T = 1.6                         # board thickness
BG = (0.012, 0.013, 0.016)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS


# ------------------------------------------------------------------ materials
MATS = {}


def material(key):
    if key in MATS:
        return MATS[key]
    col, tr, spec, shin = key
    m = bpy.data.materials.new(f'm{len(MATS)}')
    m.use_nodes = True
    bsdf = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    r, g, b = col
    metal = shin >= 0.6 and max(spec) >= 0.5
    rough = 0.28 if metal else max(0.25, min(0.75, 1.0 - shin))
    if (r, g, b) == (0.961, 0.961, 0.961):          # silkscreen
        rough = 0.55
    if g > r * 1.8 and g > b and tr > 0:            # solder mask: JLC green, satin
        rough, metal = 0.45, False
        r, g, b = 0.05, 0.33, 0.14
    bsdf.inputs['Base Color'].default_value = (r ** 2.2 * 1.0, g ** 2.2, b ** 2.2, 1.0) if not metal else (r, g, b, 1.0)
    bsdf.inputs['Metallic'].default_value = 1.0 if metal else 0.0
    bsdf.inputs['Roughness'].default_value = rough
    if not metal:
        bsdf.inputs['Specular IOR Level'].default_value = 0.25
    MATS[key] = m
    return m


def simple_material(name, color, metal=0.0, rough=0.4):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Base Color'].default_value = (*color, 1.0)
    bsdf.inputs['Metallic'].default_value = metal
    bsdf.inputs['Roughness'].default_value = rough
    return m


# ------------------------------------------------------------------ geometry
def mesh_object(name, tris, mat, parent):
    n = len(tris)
    verts = tris.reshape(-1, 3) * MM
    me = bpy.data.meshes.new(name)
    me.vertices.add(3 * n)
    me.vertices.foreach_set('co', verts.astype(np.float32).ravel())
    me.loops.add(3 * n)
    me.loops.foreach_set('vertex_index', np.arange(3 * n, dtype=np.int32))
    me.polygons.add(n)
    me.polygons.foreach_set('loop_start', np.arange(0, 3 * n, 3, dtype=np.int32))
    me.polygons.foreach_set('loop_total', np.full(n, 3, dtype=np.int32))
    me.update()
    me.validate()
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.parent = parent
    return ob


def board(name, path, parent=None):
    """An empty at the board's centre of thickness, with every material as one mesh."""
    e = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(e)
    e.parent = parent
    for k, tris in vrml.load(path).items():
        mesh_object(f'{name}.{len(e.children)}', tris, material(k), e)
    return e


def hex_prism(name, x, y, z0, z1, across=5.5, mat=None, parent=None, sides=6):
    r = across / 2 / math.cos(math.pi / sides) if sides == 6 else across / 2
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides, radius=r * MM, depth=(z1 - z0) * MM,
                                        location=(x * MM, y * MM, (z0 + z1) / 2 * MM))
    ob = bpy.context.active_object
    ob.name = name
    if sides == 6:
        ob.rotation_euler.z = math.pi / 6
    ob.data.materials.append(mat)
    ob.parent = parent
    if sides > 6:
        bpy.ops.object.shade_smooth()
    return ob


def bounds(objs):
    bpy.context.view_layer.update()
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for ob in objs:
        if ob.type != 'MESH':
            continue
        co = np.empty(len(ob.data.vertices) * 3, dtype=np.float32)
        ob.data.vertices.foreach_get('co', co)
        M = np.array(ob.matrix_world)
        w = co.reshape(-1, 3) @ M[:3, :3].T + M[:3, 3]
        lo, hi = np.minimum(lo, w.min(0)), np.maximum(hi, w.max(0))
    return lo, hi


# ------------------------------------------------------------------ world, lights, camera
def world():
    w = bpy.data.worlds.new('world')
    scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    env = nt.nodes.new('ShaderNodeTexEnvironment')
    env.image = bpy.data.images.load(os.path.join(bpy.utils.system_resource('DATAFILES'), 'studiolights', 'world', 'studio.exr'))
    bg_light = nt.nodes.new('ShaderNodeBackground')
    bg_light.inputs['Strength'].default_value = 0.12
    bg_cam = nt.nodes.new('ShaderNodeBackground')
    bg_cam.inputs['Color'].default_value = (*BG, 1.0)
    lp = nt.nodes.new('ShaderNodeLightPath')
    mix = nt.nodes.new('ShaderNodeMixShader')
    out = nt.nodes.new('ShaderNodeOutputWorld')
    nt.links.new(env.outputs['Color'], bg_light.inputs['Color'])
    nt.links.new(lp.outputs['Is Camera Ray'], mix.inputs['Fac'])
    nt.links.new(bg_light.outputs['Background'], mix.inputs[1])
    nt.links.new(bg_cam.outputs['Background'], mix.inputs[2])
    nt.links.new(mix.outputs['Shader'], out.inputs['Surface'])


def area_light(name, loc, target, power, size, color=(1, 1, 1)):
    d = bpy.data.lights.new(name, 'AREA')
    d.energy = power
    d.size = size
    d.color = color
    ob = bpy.data.objects.new(name, d)
    scene.collection.objects.link(ob)
    ob.location = loc
    look_at(ob, target)
    return ob


def look_at(ob, target):
    d = Vector(target) - ob.location
    ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def floor(z):
    bpy.ops.mesh.primitive_plane_add(size=4.0, location=(0, 0, z))
    ob = bpy.context.active_object
    ob.name = 'floor'
    m = simple_material('floor', (0.006, 0.0065, 0.008), 0.0, 0.55)
    ob.data.materials.append(m)
    return ob


def camera(lens=50):
    d = bpy.data.cameras.new('cam')
    d.lens = lens
    d.clip_start = 0.002
    d.clip_end = 20
    d.dof.use_dof = True
    d.dof.aperture_fstop = 22.0
    ob = bpy.data.objects.new('cam', d)
    scene.collection.objects.link(ob)
    scene.camera = ob
    return ob


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def orbit(cam, target, radius, elev, az, frame):
    """Camera on a sphere around target (angles in degrees), keyed at `frame`."""
    e, a = math.radians(elev), math.radians(az)
    cam.location = (target[0] + radius * math.cos(e) * math.sin(a),
                    target[1] - radius * math.cos(e) * math.cos(a),
                    target[2] + radius * math.sin(e))
    look_at(cam, target)
    cam.keyframe_insert('location', frame=frame)
    cam.keyframe_insert('rotation_euler', frame=frame)


# ------------------------------------------------------------------ shots
world()
steel = simple_material('standoff', (0.78, 0.74, 0.62), 1.0, 0.3)       # brass
screw = simple_material('screw', (0.55, 0.56, 0.58), 1.0, 0.25)
cam = camera(55)

if SHOT == 'stack':
    W, H = 90.0, 64.0
    holes = [(4, 4), (W - 4, 4), (4, H - 4), (W - 4, H - 4)]
    root = bpy.data.objects.new('stack', None)
    scene.collection.objects.link(root)
    bot = board('bottom', os.path.join(WRL, 'stack-bottom.wrl'), root)
    top = board('top', os.path.join(WRL, 'stack-top.wrl'), root)
    top.location.z = (GAP + T) * MM
    stand = []
    for i, (x, y) in enumerate(holes):
        stand.append(hex_prism(f'standoff{i}', x, -y, T / 2, T / 2 + GAP, 5.5, steel, root))
        hex_prism(f'screw_top{i}', x, -y, 0, 2.0, 5.5, screw, top, sides=32).location.z = (T / 2 + 1.0) * MM
        hex_prism(f'screw_bot{i}', x, -y, 0, 2.0, 5.5, screw, bot, sides=32).location.z = -(T / 2 + 1.0) * MM
    root.location = (-W / 2 * MM, H / 2 * MM, 0)
    lo, hi = bounds(bot.children + top.children)
    c = Vector((0, 0, (GAP / 2 + 2) * MM))
    area_light('key', (-0.18, -0.22, 0.30), c, 14, 0.25)
    area_light('fill', (0.25, -0.10, 0.12), c, 4, 0.3, (0.85, 0.9, 1.0))
    area_light('rim', (0.05, 0.30, 0.20), c, 12, 0.2, (1.0, 0.92, 0.85))
    # 0-150: orbit the closed stack; 150-210 lift the top board; 210-270 hold and look in;
    # 270-330 put it back; 330-360 settle
    N = 360
    K = 264 / 360
    for f in range(0, N + 1, 6):
        t = f / N
        orbit(cam, c, 0.23 - 0.03 * ease((f - 150) / 90) + 0.03 * ease((f - 280) / 60),
              32 + 12 * ease((f - 150) / 80) - 12 * ease((f - 270) / 60), -35 + 110 * ease(t), round(f * K))
    lift = 32.0
    for f, up, tilt in ((150, 0, 0), (210, lift, -14), (270, lift, -14), (330, 0, 0)):
        top.location.z = (GAP + T + up) * MM
        top.rotation_euler.x = math.radians(tilt)
        top.keyframe_insert('location', frame=round(f * K))
        top.keyframe_insert('rotation_euler', frame=round(f * K))
    scene.frame_start, scene.frame_end = 0, round(N * K)
    cam.data.dof.focus_object = root
    focus = bpy.data.objects.new('focus', None)
    scene.collection.objects.link(focus)
    focus.location = c
    cam.data.dof.focus_object = focus

elif SHOT == 'mini':
    W, H = 82.0, 56.0
    pivot = bpy.data.objects.new('pivot', None)
    scene.collection.objects.link(pivot)
    mini = board('mini', os.path.join(WRL, 'mini.wrl'), pivot)
    mini.location = (-W / 2 * MM, H / 2 * MM, 0)
    for i, (x, y) in enumerate([(4, 4), (W - 4, 4), (4, H - 4), (W - 4, H - 4)]):
        hex_prism(f'foot{i}', x, -y, -(T / 2 + 12.0), -T / 2, 5.5, steel, mini)
        hex_prism(f'screw{i}', x, -y, 0, 2.0, 5.5, screw, mini, sides=32).location.z = (T / 2 + 1.0) * MM
    pivot.location.z = 0.030
    lo, hi = bounds(mini.children)
    c = Vector((0, 0, 0.030))
    area_light('key', (-0.18, -0.22, 0.34), c, 14, 0.25)
    area_light('fill', (0.25, -0.10, 0.16), c, 4, 0.3, (0.85, 0.9, 1.0))
    area_light('rim', (0.05, 0.30, 0.24), c, 12, 0.2, (1.0, 0.92, 0.85))
    # 0-120 turn the Mini on its feet; 120-210 lift it and flip it over (the back: every module);
    # 210-270 hold, turning; 270-330 flip back and set it down
    N = 330
    K = 240 / 330
    keys = [(0, 0, 0, -30, 0), (120, 0, 0, 25, 0), (165, 0.035, 90, 40, 0), (210, 0.035, 180, 55, 0),
            (270, 0.035, 180, 95, 0), (315, 0.0, 360, 150, 0), (330, 0.0, 360, 150, 0)]
    for f, lift, flip, spin, _ in keys:
        pivot.location.z = 0.030 + lift
        pivot.rotation_euler = Euler((math.radians(flip), 0, math.radians(spin)), 'ZXY')
        pivot.keyframe_insert('location', frame=round(f * K))
        pivot.keyframe_insert('rotation_euler', frame=round(f * K))
    def lift_at(f):
        for (f0, l0, *_), (f1, l1, *_) in zip(keys, keys[1:]):
            if f0 <= f <= f1:
                return l0 + (l1 - l0) * ease((f - f0) / max(1, f1 - f0))
        return 0.0
    for f in range(0, N + 1, 3):
        orbit(cam, c + Vector((0, 0, 0.004 + 0.9 * lift_at(f))), 0.25, 28, -10, round(f * K))
    focus = bpy.data.objects.new('focus', None)
    scene.collection.objects.link(focus)
    focus.location = c
    cam.data.dof.focus_object = focus
    scene.frame_start, scene.frame_end = 0, round(N * K)

# ------------------------------------------------------------------ render
r = scene.render
EEVEE = '--eevee' in argv
if EEVEE:
    r.engine = 'BLENDER_EEVEE'
    ee = scene.eevee
    for k, v in (('taa_render_samples', 32), ('use_shadows', True), ('use_raytracing', True)):
        if hasattr(ee, k):
            setattr(ee, k, v)
else:
    r.engine = 'CYCLES'
r.resolution_x, r.resolution_y = (960, 540) if PREVIEW else (1920, 1080)
r.resolution_percentage = 100
r.image_settings.file_format = 'PNG'
r.image_settings.color_mode = 'RGBA'
r.film_transparent = True
scene.cycles.samples = 16 if PREVIEW else 40
scene.cycles.use_denoising = True
scene.view_settings.view_transform = 'AgX'
scene.view_settings.look = 'AgX - Medium High Contrast'
scene.view_settings.exposure = -0.2
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    for dev in ('OPTIX', 'CUDA'):
        try:
            prefs.compute_device_type = dev
            prefs.refresh_devices()
            gpus = [d for d in prefs.devices if d.type == dev]
            if gpus:
                for d in prefs.devices:
                    d.use = d.type == dev
                scene.cycles.device = 'GPU'
                print('render device', dev, [d.name for d in gpus])
                break
        except TypeError:
            continue
except Exception as ex:                      # noqa: BLE001
    print('GPU setup failed, CPU render:', ex)
if rng:
    scene.frame_start, scene.frame_end = rng
os.makedirs(OUT, exist_ok=True)
r.filepath = os.path.join(OUT, SHOT + '_')
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, SHOT + '.blend'))
bpy.ops.render.render(animation=True)
