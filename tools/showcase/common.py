"""Blender helpers shared by the showcase shots: KiCad VRML boards as meshes, materials, lights,
camera, EEVEE setup. Units: metres; board frame from the exports (x right, y = -board y, z up)."""
import math
import os
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vrml  # noqa: E402

MM = 0.001
SCREEN_COLOR = (0.05, 0.25, 0.45)        # the OLED's active area in models.py

scene = None
MATS = {}


def reset(fps=24):
    global scene
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = fps
    MATS.clear()
    return scene


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
    bsdf.inputs['Base Color'].default_value = (r ** 2.2, g ** 2.2, b ** 2.2, 1.0) if not metal else (r, g, b, 1.0)
    bsdf.inputs['Metallic'].default_value = 1.0 if metal else 0.0
    bsdf.inputs['Roughness'].default_value = rough
    if not metal:
        bsdf.inputs['Specular IOR Level'].default_value = 0.25
    MATS[key] = m
    return m


def simple_material(name, color, metal=0.0, rough=0.4, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Base Color'].default_value = (*color, 1.0)
    bsdf.inputs['Metallic'].default_value = metal
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Specular IOR Level'].default_value = spec
    return m


def mesh_object(name, tris, mat, parent, scale=MM):
    n = len(tris)
    verts = tris.reshape(-1, 3) * scale
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
    """An empty holding one mesh per material; returns (empty, {colour: object})."""
    e = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(e)
    e.parent = parent
    by_col = {}
    for k, tris in vrml.load(path).items():
        ob = mesh_object(f'{name}.{len(e.children)}', tris, material(k), e)
        by_col.setdefault(k[0], ob)
    return e, by_col


def screen_material(obj, frames_dir, n_frames, strength=2.2):
    """Turn the OLED's active area into an emissive screen showing an image sequence (0001.png..)."""
    me = obj.data
    co = np.empty(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    lo, hi = co.min(0), co.max(0)
    uv = me.uv_layers.new(name='screen')
    for loop in me.loops:
        x, y, _ = co[loop.vertex_index]
        uv.data[loop.index].uv = ((x - lo[0]) / (hi[0] - lo[0]), (y - lo[1]) / (hi[1] - lo[1]))
    m = bpy.data.materials.new('oled')
    m.use_nodes = True
    nt = m.node_tree
    for nd in list(nt.nodes):
        nt.nodes.remove(nd)
    tex = nt.nodes.new('ShaderNodeTexImage')
    img = bpy.data.images.load(os.path.join(frames_dir, '0001.png'))
    img.source = 'SEQUENCE'
    tex.image = img
    tex.interpolation = 'Closest'
    tex.image_user.frame_duration = n_frames
    tex.image_user.frame_start = 0
    tex.image_user.frame_offset = 0
    tex.image_user.use_auto_refresh = True
    em = nt.nodes.new('ShaderNodeEmission')
    em.inputs['Strength'].default_value = strength
    glass = nt.nodes.new('ShaderNodeBsdfPrincipled')
    glass.inputs['Base Color'].default_value = (0.0, 0.0, 0.0, 1.0)
    glass.inputs['Roughness'].default_value = 0.08
    add = nt.nodes.new('ShaderNodeAddShader')
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    nt.links.new(tex.outputs['Color'], em.inputs['Color'])
    nt.links.new(em.outputs['Emission'], add.inputs[0])
    nt.links.new(glass.outputs['BSDF'], add.inputs[1])
    nt.links.new(add.outputs['Shader'], out.inputs['Surface'])
    me.materials.clear()
    me.materials.append(m)


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


def world(strength=0.12):
    w = bpy.data.worlds.new('world')
    scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    for nd in list(nt.nodes):
        nt.nodes.remove(nd)
    env = nt.nodes.new('ShaderNodeTexEnvironment')
    env.image = bpy.data.images.load(os.path.join(bpy.utils.system_resource('DATAFILES'), 'studiolights', 'world', 'studio.exr'))
    bg = nt.nodes.new('ShaderNodeBackground')
    bg.inputs['Strength'].default_value = strength
    out = nt.nodes.new('ShaderNodeOutputWorld')
    nt.links.new(env.outputs['Color'], bg.inputs['Color'])
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])


def look_at(ob, target):
    d = Vector(target) - ob.location
    ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def area_light(name, loc, target, power, size, color=(1, 1, 1)):
    d = bpy.data.lights.new(name, 'AREA')
    d.energy, d.size, d.color = power, size, color
    ob = bpy.data.objects.new(name, d)
    scene.collection.objects.link(ob)
    ob.location = loc
    look_at(ob, target)
    return ob


def camera(lens=55):
    d = bpy.data.cameras.new('cam')
    d.lens = lens
    d.clip_start, d.clip_end = 0.002, 20
    ob = bpy.data.objects.new('cam', d)
    scene.collection.objects.link(ob)
    scene.camera = ob
    return ob


def orbit(cam, target, radius, elev, az, frame):
    e, a = math.radians(elev), math.radians(az)
    cam.location = (target[0] + radius * math.cos(e) * math.sin(a),
                    target[1] - radius * math.cos(e) * math.cos(a),
                    target[2] + radius * math.sin(e))
    look_at(cam, target)
    cam.keyframe_insert('location', frame=frame)
    cam.keyframe_insert('rotation_euler', frame=frame)


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def render_setup(out_prefix, first, last, preview=False, eevee=True):
    r = scene.render
    if eevee:
        r.engine = 'BLENDER_EEVEE'
        ee = scene.eevee
        for k, v in (('taa_render_samples', 32), ('use_shadows', True), ('use_raytracing', True)):
            if hasattr(ee, k):
                setattr(ee, k, v)
    else:
        r.engine = 'CYCLES'
        scene.cycles.samples = 40
        scene.cycles.use_denoising = True
    r.resolution_x, r.resolution_y = (960, 540) if preview else (1920, 1080)
    r.resolution_percentage = 100
    r.image_settings.file_format = 'PNG'
    r.image_settings.color_mode = 'RGBA'
    r.film_transparent = True
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.look = 'AgX - Medium High Contrast'
    scene.view_settings.exposure = -0.2
    scene.frame_start, scene.frame_end = first, last
    r.filepath = out_prefix
