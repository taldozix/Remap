"""Modela a frota de carros do jogo no Blender (bpy) e exporta glTF para assets/vehicles/.

Uso: python tools/art/blender/make_cars.py [tipo ...] [--preview]
(precisa do módulo bpy; ver tools/art/README.md)

Cada carroceria é um loft de seções transversais suaves (perfil lateral, linha de
cintura, teto, estreitamento da estufa). Caixas de roda são recortadas por booleano,
e vidros, faróis, lanternas, grade, placas, frisos e plásticos são definidos por zonas
sobre a superfície. Portas viram objetos próprios (a do motorista abre no jogo).
Rodas têm pneu com banda de rodagem, aro com raios, disco e pinça de freio.
A oclusão ambiente é assada nas cores de vértice (Cycles).

Convenções do arquivo exportado (o jogo depende delas):
  - frente = +Y no Blender (-Z no Godot), origem no chão, centro do comprimento;
  - nós Wheel_FL/FR/RL/RR no centro do cubo de roda (filho *_spin gira);
  - nó Door_FL com pivô na dobradiça (porta do motorista, lado esquerdo);
  - materiais com nomes fixos: paint, glass, trim, chrome, plastic, headlight,
    taillight, indicator, interior, plate, tire, rim, brake, gap, well, bedliner.
"""
import math
import os
import sys

import bpy  # noqa: I001 (bpy precisa vir antes de bmesh)
import bmesh
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(ROOT, "assets", "vehicles")
TEX = os.path.join(ROOT, "assets", "textures")

# ----------------------------------------------------------------------------- especificações
# f = posição relativa no comprimento (0 = traseira, 1 = frente). Alturas em metros.
CARS = {
    "hatch": {
        "L": 3.9, "W": 1.74,
        "bottom": [(0, 0.44), (0.025, 0.3), (0.07, 0.2), (0.93, 0.2), (0.975, 0.3), (1, 0.42)],
        "belt": [(0, 0.86), (0.02, 0.93), (0.06, 0.97), (0.55, 0.92), (0.72, 0.87), (0.9, 0.79), (0.97, 0.72), (1, 0.6)],
        "top": [(0, 0.86), (0.03, 0.99), (0.11, 1.4), (0.16, 1.46), (0.47, 1.47), (0.52, 1.43), (0.72, 0.89), (0.74, 0.86), (1, 0.6)],
        "half_w": 0.87, "roof_w": 0.66, "crown": 0.04, "hood_crown": 0.03,
        "wheel_r": 0.3, "wheel_w": 0.19, "axles": (-1.2, 1.25), "rim": "spoke6",
        "doors": [("L", 0.03, 0.86), ("L", -0.8, -0.02), ("R", 0.03, 0.86), ("R", -0.8, -0.02)],
        "pillar_b": 0.0, "side_glass": (-1.5, 0.95), "rear_glass": "hatch",
        "hood": (0.98, 1.86), "trunk": None, "hatch_lid": True,
    },
    "sedan": {
        "L": 4.4, "W": 1.8,
        "bottom": [(0, 0.44), (0.025, 0.3), (0.06, 0.2), (0.94, 0.2), (0.975, 0.3), (1, 0.42)],
        "belt": [(0, 0.88), (0.02, 0.95), (0.05, 0.99), (0.24, 0.98), (0.6, 0.92), (0.9, 0.8), (0.97, 0.72), (1, 0.6)],
        "top": [(0, 0.88), (0.02, 0.96), (0.05, 1.0), (0.22, 1.0), (0.25, 1.02), (0.36, 1.36), (0.42, 1.43), (0.55, 1.43), (0.6, 1.38), (0.7, 0.93), (0.72, 0.9), (1, 0.6)],
        "half_w": 0.9, "roof_w": 0.68, "crown": 0.04, "hood_crown": 0.035,
        "wheel_r": 0.31, "wheel_w": 0.2, "axles": (-1.32, 1.38), "rim": "spoke5",
        "doors": [("L", 0.04, 0.88), ("L", -0.86, 0.0), ("R", 0.04, 0.88), ("R", -0.86, 0.0)],
        "pillar_b": 0.02, "side_glass": (-1.03, 0.95), "rear_glass": "sedan",
        "hood": (0.98, 2.1), "trunk": (-2.12, -1.15), "hatch_lid": False,
    },
    "pickup": {
        "L": 4.9, "W": 1.9,
        "bottom": [(0, 0.58), (0.02, 0.4), (0.05, 0.34), (0.95, 0.34), (0.98, 0.42), (1, 0.55)],
        "belt": [(0, 1.04), (0.015, 1.08), (0.4, 1.1), (0.66, 1.07), (0.9, 0.98), (0.97, 0.9), (1, 0.76)],
        "top": [(0, 1.04), (0.015, 1.08), (0.395, 1.1), (0.405, 1.7), (0.42, 1.74), (0.6, 1.76), (0.64, 1.72), (0.72, 1.1), (0.73, 1.07), (1, 0.76)],
        "half_w": 0.95, "roof_w": 0.76, "crown": 0.03, "hood_crown": 0.03,
        "wheel_r": 0.37, "wheel_w": 0.25, "axles": (-1.5, 1.55), "rim": "spoke6_chunky",
        "doors": [("L", 0.36, 1.05), ("L", -0.45, 0.32), ("R", 0.36, 1.05), ("R", -0.45, 0.32)],
        "pillar_b": 0.34, "side_glass": (-0.47, 1.1), "rear_glass": "cab",
        "hood": (1.15, 2.4), "trunk": None, "hatch_lid": False,
        "bed": (-2.38, -0.55, 0.8), "tailgate": True,
    },
    "van": {
        "L": 4.8, "W": 1.95,
        "bottom": [(0, 0.46), (0.02, 0.32), (0.05, 0.3), (0.95, 0.3), (0.98, 0.36), (1, 0.48)],
        "belt": [(0, 1.1), (0.75, 1.1), (0.86, 1.06), (0.94, 0.96), (0.985, 0.82), (1, 0.66)],
        "top": [(0, 1.98), (0.01, 2.0), (0.76, 2.0), (0.8, 1.95), (0.88, 1.32), (0.9, 1.08), (0.92, 1.03), (1, 0.66)],
        "half_w": 0.975, "roof_w": 0.92, "crown": 0.03, "hood_crown": 0.03,
        "wheel_r": 0.33, "wheel_w": 0.21, "axles": (-1.45, 1.62), "rim": "steel",
        "doors": [("L", 0.58, 1.22), ("R", 0.58, 1.22), ("R", -0.55, 0.52, "slide")],
        "pillar_b": None, "side_glass": (0.6, 1.4), "rear_glass": "van",
        "hood": (1.75, 2.38), "trunk": None, "hatch_lid": False, "rear_doors": True,
    },
}


# ----------------------------------------------------------------------------- utilidades
def profile(keys, f, smooth=0.01):
    ks = np.array(keys, dtype=float)
    fine = np.linspace(0, 1, 801)
    v = np.interp(fine, ks[:, 0], ks[:, 1])
    if smooth > 0:
        sig = smooth * 800
        rad = int(sig * 3) + 1
        k = np.exp(-0.5 * (np.arange(-rad, rad + 1) / sig) ** 2)
        k /= k.sum()
        pad = np.pad(v, rad, mode="edge")
        v = np.convolve(pad, k, mode="valid")
    return np.interp(f, fine, v)


def catmull(points, samples_per_seg):
    """Curva Catmull-Rom centrípeta aberta pelos pontos (lista de (x, z))."""
    p = [np.array(points[0]) * 2 - np.array(points[1])] + [np.array(q) for q in points] + [np.array(points[-1]) * 2 - np.array(points[-2])]
    out = []
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        n = samples_per_seg[i - 1]
        for s in range(n):
            t = s / n
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(p[-2])
    return out


def new_mat(name, color, rough=0.5, metal=0.0, emission=None, alpha=1.0, double=False):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emission:
        b.inputs["Emission Color"].default_value = (*emission, 1.0)
        b.inputs["Emission Strength"].default_value = 1.0
    if alpha < 1.0:
        b.inputs["Alpha"].default_value = alpha
        m.surface_render_method = "BLENDED"
    m.use_backface_culling = not double
    return m


def materials():
    return {
        "paint": new_mat("paint", (0.6, 0.6, 0.62), 0.28, 0.35),
        "glass": new_mat("glass", (0.08, 0.1, 0.12), 0.05, 0.0, alpha=0.62, double=True),
        "trim": new_mat("trim", (0.03, 0.03, 0.035), 0.55),
        "chrome": new_mat("chrome", (0.85, 0.85, 0.87), 0.12, 1.0),
        "plastic": new_mat("plastic", (0.05, 0.05, 0.055), 0.75),
        "headlight": new_mat("headlight", (0.9, 0.9, 0.88), 0.08, 0.2, emission=(1.0, 0.96, 0.85)),
        "taillight": new_mat("taillight", (0.55, 0.04, 0.04), 0.15, 0.0, emission=(1.0, 0.08, 0.04)),
        "indicator": new_mat("indicator", (0.9, 0.45, 0.05), 0.15, 0.0, emission=(1.0, 0.5, 0.05)),
        "interior": new_mat("interior", (0.07, 0.065, 0.06), 0.9),
        "plate": new_mat("plate", (0.9, 0.9, 0.86), 0.5),
        "plate_band": new_mat("plate_band", (0.05, 0.15, 0.55), 0.5),
        "tire": new_mat("tire", (0.035, 0.035, 0.038), 0.92),
        "rim": new_mat("rim", (0.7, 0.71, 0.73), 0.25, 1.0),
        "rim_dark": new_mat("rim_dark", (0.12, 0.12, 0.13), 0.35, 0.8),
        "rim_steel": new_mat("rim_steel", (0.22, 0.22, 0.23), 0.5, 0.6),
        "brake": new_mat("brake", (0.35, 0.33, 0.32), 0.45, 0.9),
        "caliper": new_mat("caliper", (0.45, 0.1, 0.08), 0.4, 0.2),
        "gap": new_mat("gap", (0.012, 0.012, 0.014), 0.8),
        "well": new_mat("well", (0.02, 0.02, 0.022), 0.95),
        "bedliner": new_mat("bedliner", (0.04, 0.04, 0.045), 0.9),
        "seat": new_mat("seat", (0.11, 0.1, 0.095), 0.95),
        "mirror": new_mat("mirror", (0.8, 0.82, 0.85), 0.02, 1.0),
    }


def mesh_object(name, verts, faces, mat_names, mats, face_mats=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for mn in mat_names:
        me.materials.append(mats[mn])
    if face_mats is not None:
        for p, mi in zip(me.polygons, face_mats):
            p.material_index = mi
    return ob


def apply_mods(ob):
    bpy.context.view_layer.objects.active = ob
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def smooth_sharp(ob, angle=42):
    for p in ob.data.polygons:
        p.use_smooth = True
    ob.data.set_sharp_from_angle(angle=math.radians(angle))


def box_uv(ob, scale=1.0):
    """Projeção em caixa (1 unidade de UV = 1 m / scale) para texturas tileáveis."""
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    uv = me.uv_layers.active.data
    for p in me.polygons:
        n = p.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if ax == 0:
                u, v = co.y, co.z
            elif ax == 1:
                u, v = co.x, co.z
            else:
                u, v = co.x, co.y
            uv[li].uv = (u * scale, v * scale)


# ----------------------------------------------------------------------------- carroceria
def section_points(w, zb, belt, H, roof_w, crown, hood_crown, tuck):
    """Meia seção (x >= 0) do centro de baixo até o centro do teto, como pontos-chave."""
    g = min(1.0, H / 0.14)
    zmid = zb + (belt - zb) * 0.55
    rb = min(0.09, (belt - zb) * 0.25)
    wg = (w - 0.1) + (roof_w - (w - 0.1)) * g
    roof_z = belt + H
    pts = [
        (0.0, zb), (w * 0.55, zb), (w - rb * 0.7, zb + rb * 0.25), (w, zb + rb * 1.2),
        (w + 0.008, zmid), (w - tuck, belt - 0.03), (w - tuck - 0.02, belt + 0.004),
        (wg, belt + H * 0.82), (wg - 0.05 * g - 0.03, roof_z + crown * 0.5 * g),
        (wg * 0.5, roof_z + (crown if g > 0.5 else hood_crown) * 0.85),
        (0.0, roof_z + (crown * g + hood_crown * (1 - g))),
    ]
    return pts


SEG = [5, 2, 3, 5, 4, 2, 6, 3, 3, 4]   # amostras por segmento da meia seção


def build_body(spec, mats):
    L, W = spec["L"], spec["W"]
    hw = spec["half_w"]
    # Estações ao longo do comprimento: regulares no meio e bem densas nas pontas,
    # onde a seção encolhe para formar bico e traseira arredondados.
    r_end = 0.11 / L * 1.6
    ends = (1 - np.cos(np.linspace(0, np.pi / 2, 22))) * r_end
    mid = np.linspace(r_end, 1 - r_end, 110)[1:-1]
    f = np.unique(np.concatenate([ends, mid, 1 - ends]))
    zb = profile(spec["bottom"], f, 0.008)
    belt = profile(spec["belt"], f, 0.01)
    top = profile(spec["top"], f, 0.006)
    H = np.maximum(top - belt, 0.0)
    rings = []
    for i, fi in enumerate(f):
        # arredondamento das pontas em planta (superelipse)
        e = min(fi, 1 - fi)
        k = 1.0
        if e < r_end:
            q = 1 - e / r_end
            k = max(0.04, (1 - q ** 2.6) ** (1 / 2.6))
        w = hw * k
        roof_w = min(spec["roof_w"], w - 0.06)
        pts = section_points(w, zb[i], belt[i], H[i], roof_w, spec["crown"], spec["hood_crown"], 0.035)
        half = catmull(pts, SEG)
        y = (fi - 0.5) * L
        ring = [(p[0], y, p[1]) for p in half]
        mirror = [(-p[0], y, p[1]) for p in reversed(half[1:-1])]
        rings.append(ring + mirror)
    M = len(rings[0])
    verts = [v for r in rings for v in r]
    faces = []
    for i in range(len(rings) - 1):
        for j in range(M):
            a = i * M + j
            b = i * M + (j + 1) % M
            c = (i + 1) * M + (j + 1) % M
            d = (i + 1) * M + j
            faces.append((a, d, c, b))
    # tampas (leque com vértice central) nas pontas
    for end, rev in ((0, False), (len(rings) - 1, True)):
        cen = np.mean(np.array(rings[end]), axis=0)
        ci = len(verts)
        verts.append(tuple(cen))
        for j in range(M):
            a = end * M + j
            b = end * M + (j + 1) % M
            faces.append((ci, b, a) if not rev else (ci, a, b))
    ob = mesh_object("Body", verts, faces, ["paint"], mats)
    # normal para fora
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    prof = {"f": f, "zb": zb, "belt": belt, "top": top, "H": H}
    return ob, prof


def boolean_cut(ob, cutter, solver="EXACT"):
    mod = ob.modifiers.new("cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    mod.solver = solver
    apply_mods(ob)
    bpy.data.objects.remove(cutter, do_unlink=True)


def cylinder_x(name, center, radius, length, segs=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=segs, radius=radius, depth=length, location=center, rotation=(0, math.pi / 2, 0))
    ob = bpy.context.active_object
    ob.name = name
    return ob


def cube(name, center, size):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    ob = bpy.context.active_object
    ob.scale = size
    bpy.ops.object.transform_apply(scale=True)
    ob.name = name
    return ob


class Prof:
    def __init__(self, spec, p):
        self.spec = spec
        self.p = p
        self.L = spec["L"]

    def at(self, key, y):
        f = y / self.L + 0.5
        return float(np.interp(f, self.p["f"], self.p[key]))


def landmarks(spec, prof):
    """Pontos de referência da silhueta: topo/base do para-brisa e do vidro traseiro,
    altura da cintura no bico e na traseira."""
    L = spec["L"]
    ys = (prof["f"] - 0.5) * L
    top, H = prof["top"], prof["H"]
    roof = np.where(top > top.max() - 0.03)[0]
    y_rt, y_wt = ys[roof.min()], ys[roof.max()]
    after = np.where((ys > y_wt) & (H < 0.02))[0]
    before = np.where((ys < y_rt) & (H < 0.02))[0]
    P = Prof(spec, prof)
    return {
        "y_wt": y_wt, "y_rt": y_rt,
        "y_wb": ys[after.min()] if len(after) else L / 2,
        "y_rb": ys[before.max()] if len(before) else -L / 2,
        "bf": P.at("belt", L / 2 - 0.12), "br": P.at("belt", -L / 2 + 0.1),
        "zbf": P.at("zb", L / 2 - 0.15), "zbr": P.at("zb", -L / 2 + 0.15),
    }


def classify(ob, spec, prof, mats):
    """Atribui materiais por zona e devolve conjuntos de faces das portas."""
    names = list(mats.keys())
    me = ob.data
    for n in names:
        if n not in [m.name for m in me.materials if m]:
            me.materials.append(mats[n])
    idx = {m.name: i for i, m in enumerate(me.materials) if m}
    P = Prof(spec, prof)
    L, hw = spec["L"], spec["half_w"]
    r = spec["wheel_r"]
    axles = spec["axles"]
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    door_regions = []
    for d in spec["doors"]:
        door_regions.append(d)

    def in_arch(co, pad):
        for ay in axles:
            if (co.y - ay) ** 2 + (co.z - r) ** 2 < (r + 0.05 + pad) ** 2:
                return True
        return False

    def door_inside(co, d):
        side, y0, y1 = d[0], d[1], d[2]
        if (side == "L" and co.x > -0.25) or (side == "R" and co.x < 0.25):
            return False
        zb = P.at("zb", co.y)
        topz = P.at("top", co.y)
        beltz = P.at("belt", co.y)
        top_lim = topz - 0.035 if P.at("H", co.y) > 0.1 else beltz + 0.01
        return y0 < co.y < y1 and zb + 0.07 < co.z < top_lim and not in_arch(co, 0.035)

    door_faces = [[] for _ in door_regions]
    seam_lines = []  # funções g(co) cujo zero é um friso
    hood = spec.get("hood")
    if hood:
        seam_lines.append(("hood_back", lambda co: co.y - hood[0], lambda co: co.z > P.at("belt", co.y) - 0.06 and abs(co.x) < hw - 0.08))
        seam_lines.append(("hood_side", lambda co: abs(co.x) - (hw - 0.11), lambda co: co.y > hood[0] and co.z > P.at("belt", co.y) - 0.035 and co.y < L / 2 - 0.12))
    trunk = spec.get("trunk")
    if trunk:
        seam_lines.append(("trunk_front", lambda co: co.y - trunk[1], lambda co: co.z > P.at("belt", co.y) - 0.04 and abs(co.x) < hw - 0.08))
        seam_lines.append(("trunk_side", lambda co: abs(co.x) - (hw - 0.1), lambda co: co.y < trunk[1] and co.z > P.at("belt", co.y) - 0.03 and co.y > -L / 2 + 0.1))
        seam_lines.append(("trunk_back", lambda co: co.z - (P.at("belt", co.y) - 0.2), lambda co: co.y < -L / 2 + 0.25 and abs(co.x) < hw - 0.12))
    if spec.get("hatch_lid"):
        seam_lines.append(("hatch_side", lambda co: abs(co.x) - (hw - 0.09), lambda co: co.y < -L / 2 + 0.3 and co.z > P.at("zb", co.y) + 0.32))
        seam_lines.append(("hatch_low", lambda co: co.z - (P.at("zb", co.y) + 0.32), lambda co: co.y < -L / 2 + 0.25 and abs(co.x) < hw - 0.09))
    if spec.get("rear_doors"):
        seam_lines.append(("rear_mid", lambda co: co.x, lambda co: co.y < -L / 2 + 0.2 and co.z > P.at("zb", co.y) + 0.12 and co.z < 1.95))
        seam_lines.append(("rear_side", lambda co: abs(co.x) - (hw - 0.07), lambda co: co.y < -L / 2 + 0.2 and co.z > P.at("zb", co.y) + 0.12 and co.z < 1.95))
    if spec.get("tailgate"):
        seam_lines.append(("tail_side", lambda co: abs(co.x) - (hw - 0.06), lambda co: co.y < -L / 2 + 0.15 and co.z > P.at("zb", co.y) + 0.2))
        seam_lines.append(("tail_low", lambda co: co.z - (P.at("zb", co.y) + 0.22), lambda co: co.y < -L / 2 + 0.15 and abs(co.x) < hw - 0.06))

    lm = landmarks(spec, prof)
    side_glass = spec["side_glass"]
    pb = spec.get("pillar_b")
    rear_glass = spec["rear_glass"]
    for fc in bm.faces:
        c = fc.calc_center_median()
        n = fc.normal
        zb = P.at("zb", c.y)
        beltz = P.at("belt", c.y)
        Hh = P.at("H", c.y)
        topz = P.at("top", c.y)
        mat = "paint"
        ax = abs(c.x)
        front = c.y > 0
        # caixa de roda (superfície criada pelo booleano)
        for ay in axles:
            dist = math.hypot(c.y - ay, c.z - r)
            if abs(dist - (r + 0.05)) < 0.02 and ax < hw - 0.005:
                mat = "well"
        if mat == "well":
            fc.material_index = idx[mat]
            continue
        if spec.get("bed") and c.z < spec["bed"][2] + 1.0:
            by0, by1, bz = spec["bed"]
            if by0 - 0.01 < c.y < by1 + 0.01 and ax < hw - 0.08 and c.z > bz - 0.01 and c.z < P.at("belt", c.y) - 0.005:
                fc.material_index = idx["bedliner" if n.z > 0.7 else "paint"]
                continue
        # estufa: vidros e colunas
        if Hh > 0.04 and c.z > beltz + 0.015:
            if abs(n.x) > 0.5:
                margin_top = topz - c.z
                if side_glass[0] < c.y < side_glass[1] and margin_top > 0.05 and c.z > beltz + 0.035 and Hh > 0.16:
                    mat = "glass"
                    if pb is not None and abs(c.y - pb) < 0.045:
                        mat = "trim"
                    if rear_glass == "van" and c.y < 0.55:
                        mat = "paint"
                elif margin_top > 0.02 and c.z > beltz + 0.02 and side_glass[0] - 0.05 < c.y < side_glass[1] + 0.05 and Hh > 0.12:
                    mat = "trim"
            elif front and lm["y_wt"] - 0.03 < c.y < lm["y_wb"] + 0.01 and n.z > 0.0:
                if lm["y_wt"] + 0.03 < c.y < lm["y_wb"] - 0.035 and abs(n.x) < 0.3 and n.y > 0.1:
                    mat = "glass"
                elif abs(n.x) < 0.6:
                    mat = "trim"
            elif not front and rear_glass != "van" and lm["y_rb"] - 0.01 < c.y < lm["y_rt"] + 0.03 and n.y < -0.1:
                if lm["y_rb"] + 0.035 < c.y < lm["y_rt"] - 0.03 and abs(n.x) < 0.3 and c.z > beltz + 0.06:
                    mat = "glass"
                elif abs(n.x) < 0.6 and c.z > beltz + 0.03:
                    mat = "trim"
            elif rear_glass == "van" and n.y < -0.5 and c.y < -L / 2 + 0.2:
                if 1.3 < c.z < 1.85 and 0.08 < ax < hw - 0.16:
                    mat = "glass"
        # para-choques e partes baixas em plástico
        if mat == "paint":
            if n.y > 0.15 and c.y > L / 2 - 0.45 and c.z < lm["zbf"] + 0.05:
                mat = "plastic"
            elif n.y < -0.15 and c.y < -L / 2 + 0.45 and c.z < lm["zbr"] + 0.07:
                mat = "plastic"
            elif abs(n.x) > 0.5 and c.z < zb + 0.06:
                mat = "plastic"
            elif n.z < -0.5:
                mat = "plastic"
        fc.material_index = idx[mat]
    # frisos e portas (testes por vértice: face cruzando a fronteira = friso)
    for fc in bm.faces:
        if fc.material_index in (idx["well"],):
            continue
        cos = [v.co for v in fc.verts]
        c = fc.calc_center_median()
        for di, d in enumerate(door_regions):
            ins = [door_inside(co, d) for co in cos]
            if all(ins):
                door_faces[di].append(fc.index)
            elif any(ins) and len(cos) > 3 and fc.material_index not in (idx["glass"],):
                fc.material_index = idx["gap"]
        for name, g, cond in seam_lines:
            if len(cos) == 3 or not cond(c):
                continue
            vals = [g(co) for co in cos]
            if min(vals) < 0 < max(vals) and fc.material_index in (idx["paint"],):
                fc.material_index = idx["gap"]
    bm.to_mesh(me)
    bm.free()
    return door_faces


def separate_faces(ob, face_ids, name):
    """Copia as faces para um novo objeto e as remove do original."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    sel = set(face_ids)
    nb = bmesh.new()
    vmap = {}
    for i in face_ids:
        fc = bm.faces[i]
        vs = []
        for v in fc.verts:
            if v.index not in vmap:
                vmap[v.index] = nb.verts.new(v.co)
            vs.append(vmap[v.index])
        nf = nb.faces.new(vs)
        nf.material_index = fc.material_index
        nf.smooth = True
    nme = bpy.data.meshes.new(name)
    nb.to_mesh(nme)
    nb.free()
    for m in me.materials:
        nme.materials.append(m)
    nob = bpy.data.objects.new(name, nme)
    bpy.context.scene.collection.objects.link(nob)
    bmesh.ops.delete(bm, geom=[bm.faces[i] for i in sel], context="FACES_ONLY")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(me)
    bm.free()
    return nob


def thicken(ob, t=0.012):
    mod = ob.modifiers.new("solid", "SOLIDIFY")
    mod.thickness = t
    mod.offset = -1
    mod.use_rim = True
    apply_mods(ob)


# ----------------------------------------------------------------------------- peças
def rounded_box(name, center, size, mats, mat, bevel=0.02, segs=3):
    ob = cube(name, center, size)
    mod = ob.modifiers.new("bev", "BEVEL")
    mod.width = bevel
    mod.segments = segs
    apply_mods(ob)
    ob.data.materials.append(mats[mat])
    smooth_sharp(ob, 50)
    return ob


def lathe(name, profile_pts, segs, mats, mat_per_seg, axis_x=True):
    """Gira um perfil (raio, x) em torno do eixo X. mat_per_seg: material por segmento do perfil."""
    verts, faces, fmat = [], [], []
    n = len(profile_pts)
    for s in range(segs):
        a = 2 * math.pi * s / segs
        for (rr, xx) in profile_pts:
            verts.append((xx, math.cos(a) * rr, math.sin(a) * rr))
    for s in range(segs):
        s2 = (s + 1) % segs
        for k in range(n - 1):
            a = s * n + k
            b = s * n + k + 1
            c = s2 * n + k + 1
            d = s2 * n + k
            faces.append((a, b, c, d))
            fmat.append(mat_per_seg[k])
    used = sorted(set(mat_per_seg), key=mat_per_seg.index)
    ob = mesh_object(name, verts, faces, used, mats, [used.index(m) for m in fmat])
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    smooth_sharp(ob, 35)
    return ob


def join(objs, name):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    ob = bpy.context.active_object
    ob.name = name
    return ob


def make_wheel(spec, mats, name):
    R = spec["wheel_r"]
    Wt = spec["wheel_w"]
    rim_r = R * 0.62
    style = spec["rim"]
    h = Wt / 2
    chunky = "chunky" in style
    side_bulge = 0.012 if not chunky else 0.02
    # pneu: banda, ombro, flanco, talão
    tire_prof = [
        (rim_r + 0.005, h * 0.9), (rim_r + 0.03, h + side_bulge * 0.5), (R - 0.05, h + side_bulge), (R - 0.012, h * 0.92),
        (R, h * 0.6), (R, -h * 0.6), (R - 0.012, -h * 0.92), (R - 0.05, -h - side_bulge), (rim_r + 0.03, -h - side_bulge * 0.5), (rim_r + 0.005, -h * 0.9),
    ]
    tire = lathe(name + "_tire", tire_prof, 64, mats, ["tire"] * 9)
    # sulcos da banda de rodagem: anéis finos e blocos (geometria leve)
    parts = [tire]
    grooves = []
    nb = 32 if not chunky else 24
    for s in range(nb):
        a = 2 * math.pi * (s + 0.5) / nb
        for side in (-1, 1):
            off = side * h * (0.28 if not chunky else 0.32)
            bl = cube(f"{name}_tread{s}{side}", (off + side * 0.0, math.cos(a) * (R + 0.002), math.sin(a) * (R + 0.002)), (h * (0.5 if not chunky else 0.55), 0.012 if not chunky else 0.03, 0.03 if not chunky else 0.045))
            bl.rotation_euler = (a + math.pi / 2, 0, 0)
            bpy.ops.object.transform_apply(rotation=True)
            bl.data.materials.append(mats["tire"])
            grooves.append(bl)
    parts += grooves
    # aro: barril + face
    rim_mat = {"spoke5": "rim", "spoke6": "rim_dark", "spoke6_chunky": "rim", "steel": "rim_steel"}[style]
    barrel = lathe(name + "_barrel", [(rim_r, h * 0.85), (rim_r - 0.012, h * 0.75), (rim_r - 0.02, -h * 0.6), (rim_r - 0.005, -h * 0.85)], 48, mats, [rim_mat] * 3)
    parts.append(barrel)
    face_x = h * 0.55
    if style == "steel":
        disc = lathe(name + "_disc", [(rim_r - 0.01, face_x + 0.01), (rim_r * 0.75, face_x - 0.01), (rim_r * 0.55, face_x - 0.012), (rim_r * 0.35, face_x + 0.005), (0.0, face_x + 0.01)], 40, mats, [rim_mat] * 4)
        parts.append(disc)
        for s in range(8):
            a = 2 * math.pi * s / 8
            hole = cylinder_x(f"{name}_hole{s}", (face_x - 0.005, math.cos(a) * rim_r * 0.65, math.sin(a) * rim_r * 0.65), 0.022, 0.03, 12)
            hole.data.materials.append(mats["gap"])
            parts.append(hole)
        cap = lathe(name + "_cap", [(rim_r * 0.33, face_x + 0.006), (rim_r * 0.3, face_x + 0.025), (0.0, face_x + 0.03)], 32, mats, ["chrome"] * 2)
        parts.append(cap)
    else:
        nsp = {"spoke5": 5, "spoke6": 6, "spoke6_chunky": 6}[style]
        hub = lathe(name + "_hub", [(rim_r * 0.3, face_x - 0.02), (rim_r * 0.3, face_x + 0.01), (rim_r * 0.12, face_x + 0.025), (0.0, face_x + 0.025)], 32, mats, [rim_mat, rim_mat, "chrome"])
        parts.append(hub)
        lip = lathe(name + "_lip", [(rim_r + 0.004, face_x + 0.012), (rim_r - 0.022, face_x + 0.012), (rim_r - 0.024, face_x - 0.01)], 48, mats, [rim_mat] * 2)
        parts.append(lip)
        double = style == "spoke5"
        for s in range(nsp):
            for k in ((-1, 1) if double else (0,)):
                a = 2 * math.pi * s / nsp + k * 0.12
                width = (0.025 if double else (0.05 if "chunky" in style else 0.038))
                sp = cube(f"{name}_sp{s}{k}", (face_x - 0.003, math.cos(a) * rim_r * 0.62, math.sin(a) * rim_r * 0.62), (0.03, rim_r * 0.72, width))
                sp.rotation_euler = (a - math.pi / 2, 0, 0)
                sp.location = (face_x - 0.003, math.cos(a) * rim_r * 0.62, math.sin(a) * rim_r * 0.62)
                bpy.ops.object.transform_apply(rotation=True)
                mod = sp.modifiers.new("bev", "BEVEL")
                mod.width = 0.006
                mod.segments = 2
                apply_mods(sp)
                sp.data.materials.append(mats[rim_mat])
                parts.append(sp)
        for s in range(5 if nsp == 5 else 6):
            a = 2 * math.pi * (s + 0.5) / (5 if nsp == 5 else 6)
            nut = cylinder_x(f"{name}_nut{s}", (face_x + 0.018, math.cos(a) * rim_r * 0.2, math.sin(a) * rim_r * 0.2), 0.011, 0.02, 8)
            nut.data.materials.append(mats["chrome"])
            parts.append(nut)
    # freio: disco e pinça (visíveis entre os raios)
    disc = lathe(name + "_brake", [(rim_r * 0.88, 0.0), (rim_r * 0.88, -0.022), (rim_r * 0.35, -0.022), (rim_r * 0.35, 0.0)], 32, mats, ["brake"] * 3)
    parts.append(disc)
    cal = rounded_box(name + "_caliper", (0.0, 0.0, rim_r * 0.72), (0.05, 0.14, 0.06), mats, "caliper", 0.012, 2)
    cal.location = (0.0, -rim_r * 0.5, rim_r * 0.55)
    parts.append(cal)
    wheel = join(parts, name)
    for p in wheel.data.polygons:
        p.use_smooth = True
    wheel.data.set_sharp_from_angle(angle=math.radians(35))
    return wheel


def make_interior(spec, prof, mats):
    P = Prof(spec, prof)
    L, hw = spec["L"], spec["half_w"]
    parts = []
    y_wind = spec["side_glass"][1] - 0.1
    y_back = spec["side_glass"][0] + 0.15
    if spec["rear_glass"] == "van":
        y_back = 0.45
    zf = P.at("zb", 0) + 0.12
    beltz = P.at("belt", 0)
    tub = cube("tub", (0, (y_wind + y_back) / 2, (zf + beltz) / 2), (hw * 2 - 0.12, y_wind - y_back, beltz - zf))
    tub.data.materials.append(mats["interior"])
    bm = bmesh.new()
    bm.from_mesh(tub.data)
    bmesh.ops.reverse_faces(bm, faces=bm.faces)
    bm.to_mesh(tub.data)
    bm.free()
    parts.append(tub)
    dash = rounded_box("dash", (0, y_wind - 0.12, beltz - 0.05), (hw * 2 - 0.16, 0.3, 0.2), mats, "interior", 0.04)
    parts.append(dash)
    rows = [y_wind - 0.85]
    if spec["rear_glass"] != "van" and y_wind - y_back > 1.6:
        rows.append(y_wind - 1.65)
    for ri, ry in enumerate(rows):
        xs = (-hw * 0.45, hw * 0.45) if ri == 0 else (0.0,)
        for sx in xs:
            wseat = 0.5 if ri == 0 else hw * 1.6
            parts.append(rounded_box(f"seat{ri}{sx}", (sx, ry, zf + 0.2), (wseat, 0.5, 0.14), mats, "seat", 0.05))
            parts.append(rounded_box(f"back{ri}{sx}", (sx, ry - 0.24, zf + 0.5), (wseat, 0.12, 0.6), mats, "seat", 0.05))
            if ri == 0:
                parts.append(rounded_box(f"head{ri}{sx}", (sx, ry - 0.26, zf + 0.88), (0.26, 0.1, 0.18), mats, "seat", 0.04))
    bpy.ops.mesh.primitive_torus_add(major_radius=0.18, minor_radius=0.018, location=(-hw * 0.45, y_wind - 0.32, beltz + 0.05), rotation=(math.radians(65), 0, 0))
    sw = bpy.context.active_object
    sw.data.materials.append(mats["trim"])
    parts.append(sw)
    return join(parts, "Interior")


def make_mirror(spec, prof, mats, side):
    P = Prof(spec, prof)
    hw = spec["half_w"]
    y = spec["side_glass"][1] - 0.12
    if spec["rear_glass"] == "van":
        y = spec["side_glass"][1] - 0.1
    beltz = P.at("belt", y)
    x = side * (hw + 0.07)
    m = rounded_box(f"mirror{side}", (x, y, beltz + 0.1), (0.15, 0.08, 0.11), mats, "paint", 0.03, 3)
    stalk = rounded_box(f"stalk{side}", (side * (hw - 0.0), y + 0.01, beltz + 0.05), (0.07, 0.06, 0.04), mats, "paint", 0.012, 2)
    glass = cube(f"mglass{side}", (x, y - 0.042, beltz + 0.1), (0.12, 0.004, 0.085))
    glass.data.materials.append(mats["mirror"])
    return join([m, stalk, glass], f"Mirror_{'L' if side < 0 else 'R'}")


def make_handles(spec, prof, mats, door, side):
    P = Prof(spec, prof)
    hw = spec["half_w"]
    y = door[1] + 0.12 if len(door) < 4 else door[2] - 0.1
    beltz = P.at("belt", y)
    x = side * (hw + 0.012)
    return rounded_box(f"handle{side}{y:.2f}", (x, y, beltz - 0.08), (0.025, 0.14, 0.03), mats, "trim", 0.008, 2)


def make_wipers(spec, prof, mats):
    P = Prof(spec, prof)
    y0 = landmarks(spec, prof)["y_wb"] - 0.02
    z = P.at("belt", y0) + 0.025
    parts = []
    for sx in (-0.35, 0.25):
        w = cube(f"wiper{sx}", (sx, y0, z), (0.6, 0.02, 0.015))
        w.rotation_euler = (0, math.radians(5), math.radians(8))
        w.data.materials.append(mats["trim"])
        parts.append(w)
    return join(parts, "Wipers")


def make_bed(spec, prof, mats):
    by0, by1, bz = spec["bed"]
    hw = spec["half_w"]
    parts = []
    # frisos/canaletas no assoalho da caçamba
    for i in range(7):
        x = -hw + 0.2 + i * (2 * hw - 0.4) / 6
        rib = cube(f"rib{i}", (x, (by0 + by1) / 2, bz + 0.012), (0.04, by1 - by0 - 0.06, 0.025))
        rib.data.materials.append(mats["bedliner"])
        parts.append(rib)
    rail = rounded_box("rollbar", (0, by1 - 0.02, 1.32), (hw * 2 - 0.12, 0.05, 0.05), mats, "trim", 0.015)
    parts.append(rail)
    for s in (-1, 1):
        parts.append(rounded_box(f"rollpost{s}", (s * (hw - 0.07), by1 - 0.02, 1.2), (0.05, 0.05, 0.26), mats, "trim", 0.015))
    return join(parts, "BedDetail")


def surface_hit(body, x, z, front):
    L = 20.0
    o = Vector((x, L if front else -L, z))
    d = Vector((0, -1 if front else 1, 0))
    ok, loc, nor, _ = body.ray_cast(o, d)
    if not ok:
        return None, None
    return loc, nor


def oriented_box(name, loc, nor, size, mats, mat, sink, bevel=0.015, segs=2):
    from mathutils import Matrix
    yv = nor.normalized()
    xv = Vector((0, 0, 1)).cross(yv).normalized()
    zv = xv.cross(yv)
    ob = rounded_box(name, (0, 0, 0), size, mats, mat, bevel, segs)
    rot = Matrix((xv, yv, zv)).transposed().to_4x4()
    ob.matrix_world = Matrix.Translation(loc - yv * sink) @ rot
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.transform_apply(location=True, rotation=True)
    return ob


def on_surface(body, name, x, z, front, size, mats, mat, sink=None, bevel=0.015, flat=False):
    loc, nor = surface_hit(body, x, z, front)
    if loc is None:
        return None
    if flat:
        nor = Vector((0, 1 if front else -1, 0))
    return oriented_box(name, loc, nor, size, mats, mat, size[1] * 0.45 if sink is None else sink, bevel)


LIGHTS = {
    # farol (largura, altura), lanterna (largura, altura, x relativo, z relativo à cintura traseira)
    "hatch": {"head": (0.3, 0.1), "tail": (0.13, 0.26, 0.86, 0.12), "grille": 0.55, "third": True},
    "sedan": {"head": (0.34, 0.1), "tail": (0.34, 0.11, 0.72, -0.07), "grille": 0.62, "third": False, "fog": True},
    "pickup": {"head": (0.34, 0.15), "tail": (0.1, 0.32, 0.92, -0.2), "grille": 0.75, "third": True, "fog": True, "chrome_bumper": True},
    "van": {"head": (0.3, 0.15), "tail": (0.12, 0.34, 0.9, -0.42), "grille": 0.7, "third": True},
}


def make_lights_and_details(body, spec, prof, mats, kind):
    lm = landmarks(spec, prof)
    hw = spec["half_w"]
    L = spec["L"]
    cfg = LIGHTS[kind]
    bf, br, zbf, zbr = lm["bf"], lm["br"], lm["zbf"], lm["zbr"]
    lights, det = [], []
    hwid, hh = cfg["head"]
    for s in (-1, 1):
        x = s * (hw * 0.95 - hwid * 0.5 - 0.03)
        z = bf - hh * 0.5 - 0.015
        o = on_surface(body, f"housing{s}", x, z, True, (hwid + 0.03, 0.05, hh + 0.03), mats, "trim", 0.03, 0.02)
        if o: det.append(o)
        o = on_surface(body, f"head{s}", x, z, True, (hwid, 0.05, hh), mats, "headlight", 0.018, 0.02)
        if o: lights.append(o)
        o = on_surface(body, f"ind{s}", s * (hw * 0.95 - 0.04), z - hh * 0.25, True, (0.05, 0.05, hh * 0.45), mats, "indicator", 0.016, 0.01)
        if o: lights.append(o)
        if cfg.get("fog"):
            o = on_surface(body, f"fog{s}", s * hw * 0.68, zbf + 0.12, True, (0.11, 0.04, 0.06), mats, "headlight", 0.022, 0.012)
            if o: lights.append(o)
        # lanterna
        tw, th, tx, tz = cfg["tail"]
        x = s * (hw * tx)
        x = s * min(abs(x), hw * 0.97 - tw * 0.5)
        z = br + tz
        o = on_surface(body, f"tailh{s}", x, z, False, (tw + 0.025, 0.05, th + 0.025), mats, "trim", 0.03, 0.02)
        if o: det.append(o)
        o = on_surface(body, f"tail{s}", x, z, False, (tw, 0.05, th), mats, "taillight", 0.017, 0.02)
        if o: lights.append(o)
        o = on_surface(body, f"tind{s}", x, z - th * 0.32, False, (tw * 0.92, 0.052, th * 0.22), mats, "indicator", 0.016, 0.01)
        if o: lights.append(o)
        # repetidor lateral no para-lama
        y_rep = spec["axles"][1] - spec["wheel_r"] - 0.15
        rep = rounded_box(f"rep{s}", (s * (hw + 0.004), y_rep, bf - 0.05), (0.012, 0.06, 0.025), mats, "indicator", 0.005, 1)
        lights.append(rep)
        # escapamento
        if s > 0:
            ex = cylinder_x(f"exh", (0, 0, 0), 0.03, 0.18, 12)
            ex.rotation_euler = (0, 0, math.pi / 2)
            ex.location = (hw * 0.55, -L / 2 + 0.05, zbr - 0.02)
            ex.data.materials.append(mats["chrome"])
            det.append(ex)
    # grade com frisos e emblema
    gw = hw * cfg["grille"]
    gz = bf - 0.065
    g = on_surface(body, "grille", 0, gz, True, (gw, 0.05, 0.1), mats, "gap", 0.03, 0.015)
    if g:
        det.append(g)
        for i in range(3):
            sl = on_surface(body, f"slat{i}", 0, gz - 0.03 + i * 0.03, True, (gw - 0.03, 0.05, 0.01), mats, "chrome" if kind == "pickup" else "trim", 0.022, 0.003)
            if sl: det.append(sl)
        em = on_surface(body, "emblem", 0, gz, True, (0.06, 0.05, 0.04), mats, "chrome", 0.02, 0.012)
        if em: det.append(em)
    o = on_surface(body, "intake", 0, zbf + 0.11, True, (hw * 1.0, 0.05, 0.07), mats, "gap", 0.03, 0.02)
    if o: det.append(o)
    # placas (padrão de placa branca com faixa azul, sem textos reais)
    for front, z in ((True, zbf + 0.24), (False, (br - 0.27) if kind in ("hatch", "sedan") else zbr + 0.32)):
        p = on_surface(body, f"plate{front}", 0, z, front, (0.4, 0.03, 0.13), mats, "plate", 0.01, 0.006, flat=True)
        if p:
            det.append(p)
            b = on_surface(body, f"band{front}", 0, z + 0.05, front, (0.4, 0.032, 0.025), mats, "plate_band", 0.008, 0.003, flat=True)
            if b: det.append(b)
    if cfg.get("third"):
        topz = max(prof["top"])
        o = on_surface(body, "third", 0, topz - 0.06, False, (0.3, 0.04, 0.035), mats, "taillight", 0.015, 0.01)
        if o: lights.append(o)
    if cfg.get("chrome_bumper"):
        o = on_surface(body, "rbumper", 0, zbr + 0.06, False, (hw * 2.0, 0.12, 0.13), mats, "chrome", 0.04, 0.02)
        if o: det.append(o)
        o = on_surface(body, "fbumper", 0, zbf + 0.08, True, (hw * 1.9, 0.08, 0.12), mats, "chrome", 0.035, 0.02)
        if o: det.append(o)
    out = {}
    if lights:
        out["Lights"] = join(lights, "Lights")
    if det:
        out["Details"] = join(det, "Details")
    return out


def bake_ao(objs, samples=48):
    """Assa oclusão ambiente em cores de vértice (atributo 'Col')."""
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    # chão para sombra de contato
    bpy.ops.mesh.primitive_plane_add(size=12, location=(0, 0, 0))
    ground = bpy.context.active_object
    for ob in objs:
        if ob.type != "MESH" or len(ob.data.polygons) == 0:
            continue
        for o in bpy.context.selected_objects:
            o.select_set(False)
        me = ob.data
        if "Col" not in me.color_attributes:
            me.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
        me.color_attributes.active_color = me.color_attributes["Col"]
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.bake(type="AO", target="VERTEX_COLORS")
    bpy.data.objects.remove(ground, do_unlink=True)
    # suaviza e limita o contraste (AO moderado)
    for ob in objs:
        if ob.type != "MESH" or "Col" not in ob.data.color_attributes:
            continue
        ca = ob.data.color_attributes["Col"]
        for d in ca.data:
            v = d.color[0]
            v = 0.35 + 0.65 * v
            d.color = (v, v, v, 1.0)


def build_car(kind):
    spec = CARS[kind]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = materials()
    body, prof = build_body(spec, mats)
    r = spec["wheel_r"]
    for i, ay in enumerate(spec["axles"]):
        boolean_cut(body, cylinder_x(f"arch{i}", (0, ay, r), r + 0.05, spec["W"] + 0.4, 56))
    if spec.get("bed"):
        by0, by1, bz = spec["bed"]
        hw = spec["half_w"]
        boolean_cut(body, cube("bedcut", (0, (by0 + by1) / 2, bz + 0.6), (2 * hw - 0.16, by1 - by0, 1.2)), "FLOAT")
    door_faces = classify(body, spec, prof, mats)
    doors = []
    for di, d in enumerate(spec["doors"]):
        if di == 0:
            dob = separate_faces(body, door_faces[di], "door_FL_mesh")
            doors.append((d, dob))
    smooth_sharp(body, 40)
    box_uv(body)
    parts = {"Body": body}
    interior = make_interior(spec, prof, mats)
    parts["Interior"] = interior
    parts["Mirror_L"] = make_mirror(spec, prof, mats, -1)
    parts["Mirror_R"] = make_mirror(spec, prof, mats, 1)
    parts["Wipers"] = make_wipers(spec, prof, mats)
    handles = []
    for di, d in enumerate(spec["doors"]):
        side = -1 if d[0] == "L" else 1
        hdl = make_handles(spec, prof, mats, d, side)
        if di == 0:
            hdl.name = "handle_driver"
        handles.append(hdl)
    if spec.get("bed"):
        parts["BedDetail"] = make_bed(spec, prof, mats)
    bpy.context.view_layer.update()
    parts.update(make_lights_and_details(body, spec, prof, mats, kind))
    # porta do motorista com espessura e pivô na dobradiça
    d, door = doors[0]
    thicken(door, 0.03)
    smooth_sharp(door, 40)
    door = join([door, handles[0]], "door_FL_mesh")
    hinge = Vector((-spec["half_w"] + 0.02, d[2], 0.0))
    door_root = bpy.data.objects.new("Door_FL", None)
    bpy.context.scene.collection.objects.link(door_root)
    door_root.location = hinge
    door.parent = door_root
    door.location -= hinge
    others = [h for h in handles[1:]]
    if others:
        parts["Handles"] = join(others, "Handles")
    # rodas
    wheels = []
    for ay, tag in ((spec["axles"][1], "F"), (spec["axles"][0], "R")):
        for side, st in ((-1, "L"), (1, "R")):
            w = make_wheel(spec, mats, f"Wheel_{tag}{st}_spin")
            if side < 0:
                w.rotation_euler = (0, 0, math.pi)
                bpy.ops.object.select_all(action="DESELECT")
                w.select_set(True)
                bpy.context.view_layer.objects.active = w
                bpy.ops.object.transform_apply(rotation=True)
            root = bpy.data.objects.new(f"Wheel_{tag}{st}", None)
            bpy.context.scene.collection.objects.link(root)
            root.location = (side * (spec["half_w"] - spec["wheel_w"] * 0.5 - 0.02), ay, r)
            w.parent = root
            wheels.append(w)
    allmesh = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    for o in allmesh:
        if not o.data.uv_layers:
            box_uv(o)   # o Godot precisa de UV para gerar tangentes
    bake_ao(allmesh, 32)
    return parts


def stats():
    tris = 0
    for o in bpy.context.scene.objects:
        if o.type == "MESH":
            tris += sum(len(p.vertices) - 2 for p in o.data.polygons)
    return tris


def export(kind):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, f"car_{kind}.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_apply=True,
                              export_vertex_color="ACTIVE", export_all_vertex_colors=False,
                              export_yup=True, export_materials="EXPORT")
    return path


def preview(kind, out_png):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 24
    sc.render.resolution_x = 900
    sc.render.resolution_y = 520
    for mname, col in (("paint", (0.55, 0.08, 0.06)),):
        m = bpy.data.materials[mname]
        m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*col, 1)
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.62, 0.72, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.8
    sc.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(40), math.radians(20), math.radians(30))
    sc.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=30)
    g = bpy.context.active_object
    gm = new_mat("ground_prev", (0.3, 0.3, 0.3), 0.9)
    g.data.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    cam.data.lens = 50
    sc.camera = cam
    shots = [((5.0, 6.0, 3.0), "a"), ((-6.0, -6.0, 3.5), "b"), ((3.0, 5.0, 9.5), "c")]
    for loc, tag in shots:
        cam.location = loc
        d = Vector((0, 0, 0.7)) - Vector(loc)
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = out_png.replace(".png", f"_{tag}.png")
        bpy.ops.render.render(write_still=True)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    kinds = args or list(CARS)
    for kind in kinds:
        build_car(kind)
        print(kind, "triângulos:", stats(), flush=True)
        print("exportado", export(kind), flush=True)
        if "--preview" in sys.argv:
            preview(kind, os.path.join(os.environ.get("PREVIEW_DIR", "/tmp"), f"car_{kind}.png"))


if __name__ == "__main__":
    main()
