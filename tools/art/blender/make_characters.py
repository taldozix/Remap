"""Gera os personagens do jogo (assets/characters/human_<m|f>.glb) a partir dos dados
CC0 do MakeHuman/MPFB2: corpo com morphs, esqueleto "game_engine", roupas, cabelos,
acessórios, texturas e animações.

Uso: MPFB_DATA=<mpfb2>/src/mpfb/data python tools/art/blender/make_characters.py [m|f] [--preview]

Saída por sexo: um .glb com Skeleton, a malha do corpo (shape keys: heavy, thin, old,
face_a, face_b) e uma malha por peça de roupa/cabelo/acessório (o jogo liga e desliga
conforme a aparência), além das animações (idle, walk, run, sit, lie, knocked, work,
talk, eat, search, interact).
"""
import math
import os
import sys

import bpy  # noqa: I001
import bmesh
import numpy as np
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mhbase as mb  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(ROOT, "assets", "characters")
RACES = ("african", "asian", "caucasian")


# ----------------------------------------------------------------------------- corpo
class Base:
    """Malha base com morphs aplicados (todas as 19158 posições, coordenadas do Blender)."""

    def __init__(self, sex):
        self.sex = sex
        g = "male" if sex == "m" else "female"
        self.V, self.VT, self.F, self.FT, self.G = mb.load_obj()
        n = len(self.V)
        self.groups = mb.vertex_groups()
        mix = {"african": 0.3, "asian": 0.3, "caucasian": 0.4}
        race = sum(mb.target_delta(f"macrodetails/{r}-{g}-young", n) * w for r, w in mix.items())
        prop = mb.target_delta(f"macrodetails/proportions/{g}-young-averagemuscle-averageweight-idealproportions", n) \
            if os.path.exists(os.path.join(mb.DATA, "targets", "macrodetails", "proportions", f"{g}-young-averagemuscle-averageweight-idealproportions.target.gz")) else 0
        self.base_obj = self.V + race + prop * 0.5
        # deltas das shape keys (espaço OBJ)
        self.keys = {
            "heavy": mb.target_delta(f"macrodetails/universal-{g}-young-averagemuscle-maxweight", n) * 0.8,
            "thin": mb.target_delta(f"macrodetails/universal-{g}-young-averagemuscle-minweight", n) * 0.8,
            "old": sum(mb.target_delta(f"macrodetails/{r}-{g}-old", n) * w for r, w in mix.items()) - race,
            "face_a": (mb.target_delta(f"macrodetails/african-{g}-young", n) - race) * 0.7,
            "face_b": (mb.target_delta(f"macrodetails/asian-{g}-young", n) - race) * 0.7,
        }
        self.B = mb.obj_to_blender(self.base_obj)
        body = self.groups["body"]
        self.ground = self.B[body, 2].min()
        self.B[:, 2] -= self.ground
        self.keyB = {k: mb.obj_to_blender(self.base_obj + d) - np.array([0, 0, self.ground]) - self.B for k, d in self.keys.items()}

    def faces_of(self, group):
        return [i for i, g in enumerate(self.G) if g == group]

    def joint(self, name):
        return self.B[self.groups[name]].mean(axis=0)


def make_mesh(name, verts, faces, uvs=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    if uvs is not None:
        uvl = me.uv_layers.new(name="UVMap")
        k = 0
        for p in me.polygons:
            for li in p.loop_indices:
                uvl.data[li].uv = uvs[k]
                k += 1
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for p in me.polygons:
        p.use_smooth = True
    return ob


def submesh(base, face_ids, name, with_uv=True, verts=None):
    """Extrai faces da malha base; devolve objeto e o mapa índice base -> local."""
    P = base.B if verts is None else verts
    used = sorted({v for i in face_ids for v in base.F[i]})
    remap = {v: k for k, v in enumerate(used)}
    faces = [[remap[v] for v in base.F[i]] for i in face_ids]
    uvs = None
    if with_uv:
        uvs = [tuple(base.VT[t]) for i in face_ids for t in base.FT[i]]
    ob = make_mesh(name, P[used], faces, uvs)
    return ob, np.array(used)


def add_shape_keys(ob, base, used):
    ob.shape_key_add(name="Basis", from_mix=False)
    for k, d in base.keyB.items():
        sk = ob.shape_key_add(name=k, from_mix=False)
        sk.value = 0.0
        co = base.B[used] + d[used]
        sk.data.foreach_set("co", co.reshape(-1))


# ----------------------------------------------------------------------------- esqueleto
def build_armature(base):
    rig = mb.rig("game_engine")
    arm = bpy.data.armatures.new("Skeleton")
    ob = bpy.data.objects.new("Armature", arm)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")

    def pos(spec):
        if spec["strategy"] == "CUBE":
            return base.joint(spec["cube_name"])
        return base.B[spec["vertex_indices"]].mean(axis=0)

    # Posições no espaço original (frente -Y) para respeitar a rolagem do rig MPFB;
    # depois gira-se a matriz 180° em Z (o personagem passa a olhar para +Y).
    R180 = Matrix.Rotation(math.pi, 4, "Z")
    ebs = {}
    for name, b in rig.items():
        h = Vector(pos(b["head"]))
        t = Vector(pos(b["tail"]))
        if (t - h).length < 1e-3:
            t = h + Vector((0, 0, 0.05))
        eb = arm.edit_bones.new(name)
        eb.head = R180 @ h
        eb.tail = R180 @ t
        eb.roll = float(b.get("roll", 0.0))
        m = R180 @ eb.matrix
        length = eb.length
        eb.matrix = m
        eb.length = length
        ebs[name] = eb
    for name, b in rig.items():
        if b.get("parent"):
            ebs[name].parent = ebs[b["parent"]]
            ebs[name].use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    return ob


_WEIGHTS = None


def all_weights():
    global _WEIGHTS
    if _WEIGHTS is None:
        _WEIGHTS = mb.weights("game_engine")
    return _WEIGHTS


def assign_weights(ob, used):
    lookup = {int(v): k for k, v in enumerate(used)}
    for bone, pairs in all_weights().items():
        vg = ob.vertex_groups.new(name=bone)
        for vi, wt in pairs:
            k = lookup.get(int(vi))
            if k is not None and wt > 1e-4:
                vg.add([k], float(wt), "REPLACE")


def attach(ob, arm):
    mod = ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    ob.parent = arm


def skin(ob, arm, base, used):
    assign_weights(ob, used)
    attach(ob, arm)


# ----------------------------------------------------------------------------- partes do corpo
HAND_BONES = {"hand", "thumb", "index", "middle", "ring", "pinky"}


def bone_kind(bone):
    if not bone:
        return "torso"
    root = bone.split("_")[0]
    if root in HAND_BONES:
        return "hand"
    return {"lowerarm": "forearm", "upperarm": "uarm", "foot": "foot", "ball": "foot", "calf": "calf",
            "thigh": "thigh", "head": "head", "neck": "head"}.get(root, "torso")


class Anatomy:
    """Classificação por vértice (osso dominante + parâmetros ao longo dos membros)."""

    def __init__(self, base):
        n = len(base.B)
        best = np.zeros(n)
        dom = np.empty(n, dtype=object)
        dom[:] = ""
        for bone, pairs in all_weights().items():
            for vi, wt in pairs:
                if wt > best[vi]:
                    best[vi] = wt
                    dom[vi] = bone
        self.dom = dom
        self.kind = np.array([bone_kind(b) for b in dom], dtype=object)
        J = base.joint
        self.belt = J("joint-spine-4")[2] + 0.045
        self.knee = J("joint-l-knee")[2]
        self.ankle = J("joint-l-ankle")[2]
        self.neck = J("joint-neck")[2]
        self.chest = J("joint-spine-1")[2]
        self.head = J("joint-head")[2]
        P = base.B
        side = np.where(P[:, 0] < 0, "l", "r")
        # o personagem olha para +Y: o lado esquerdo dele fica em -X
        self.tu = np.zeros(n)
        self.tf = np.zeros(n)
        for sd in ("l", "r"):
            sh, el, wr = J(f"joint-{sd}-shoulder"), J(f"joint-{sd}-elbow"), J(f"joint-{sd}-hand")
            m = side == sd
            a = el - sh
            self.tu[m] = (P[m] - sh) @ a / (a @ a)
            b = wr - el
            self.tf[m] = (P[m] - el) @ b / (b @ b)
        self.P = P

    def region(self, i):
        """Região do corpo (bits usados pelo shader para esconder pele sob a roupa)."""
        k = self.kind[i]
        z = self.P[i, 2]
        if k == "hand":
            return 6
        if k == "forearm":
            return 5
        if k == "uarm":
            return 3 if self.tu[i] < 0.5 else 4
        if k == "foot" or z < self.ankle + 0.02:
            return 11
        if k == "calf":
            return 9 if z > self.ankle + 0.09 else 10
        if k == "thigh":
            return 7 if z > self.knee + 0.17 else 8
        if k == "head":
            return 0
        return 1 if z > self.belt + 0.02 else 2


REGION_BITS = {"head": 0, "torso": 1, "pelvis": 2, "uarm_hi": 3, "uarm_lo": 4, "forearm": 5, "hand": 6,
               "thigh_hi": 7, "thigh_lo": 8, "calf": 9, "ankle": 10, "foot": 11}


def mb_rounded_box(name, center, size, mat, bevel=0.02):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = size
    bpy.ops.object.transform_apply(scale=True)
    mod = ob.modifiers.new("bev", "BEVEL")
    mod.width = bevel
    mod.segments = 3
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)
    ob.data.materials.append(get_mat(mat))
    for p in ob.data.polygons:
        p.use_smooth = True
    return ob


def smoothstep_np(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def face_pick(base, group, pred):
    """Faces do grupo cujo centro satisfaz pred(índices dos vértices)."""
    out = []
    for i in base.faces_of(group):
        if pred(base.F[i]):
            out.append(i)
    return out


def majority(anat, vids):
    ks = [anat.kind[v] for v in vids]
    return max(set(ks), key=ks.count)


def smart_uv(ob, tile_m=0.5):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.01, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    me = ob.data
    # densidade constante: 1 unidade de UV = tile_m metros
    area3 = sum(p.area for p in me.polygons)
    uv = me.uv_layers.active.data
    area_uv = 0.0
    for p in me.polygons:
        pts = [uv[li].uv for li in p.loop_indices]
        for k in range(1, len(pts) - 1):
            a, b, c = pts[0], pts[k], pts[k + 1]
            area_uv += abs((b - a).cross(c - a)) * 0.5
    if area_uv > 0:
        sc = math.sqrt(area3 / area_uv) / tile_m
        for d in uv:
            d.uv = d.uv * sc


def relax_boundary(me, iters=6, amount=0.6):
    """Suaviza bordas abertas (tira o serrilhado da seleção de faces)."""
    bm = bmesh.new()
    bm.from_mesh(me)
    bverts = [v for v in bm.verts if v.is_boundary]
    for _ in range(iters):
        new = {}
        for v in bverts:
            nb = [e.other_vert(v) for e in v.link_edges if e.is_boundary]
            if len(nb) == 2:
                avg = (nb[0].co + nb[1].co) / 2
                new[v] = v.co.lerp(avg, amount)
        for v, c in new.items():
            v.co = c
    bm.to_mesh(me)
    bm.free()


def smooth_mesh(ob, factor=0.6, repeat=8):
    mod = ob.modifiers.new("sm", "SMOOTH")
    mod.factor = factor
    mod.iterations = repeat
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def piece(base, arm, face_ids, name, mat, offset=0.006, solid=0.004, tile_m=0.5, flatten=None, smooth=0, hems=True, hem_r=0.0045):
    """Peça de roupa a partir de faces da malha base (corpo ou ajudantes), com pesos,
    morphs, bordas suavizadas e barras (bainhas) como geometria."""
    ob, used = submesh(base, face_ids, name, with_uv=False)
    me = ob.data
    me.update()
    nrm = np.array([v.normal for v in me.vertices])
    disp = nrm * offset
    if flatten is not None:
        disp = disp + flatten(base.B[used], nrm)
    co = base.B[used] + disp
    me.vertices.foreach_set("co", co.reshape(-1))
    if smooth:
        smooth_mesh(ob, 0.5, smooth)
    relax_boundary(me)
    me.update()
    assign_weights(ob, used)
    # deslocamento final de cada vértice em relação à malha base (vale para os morphs)
    final = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", final)
    final = final.reshape(-1, 3)
    loops = boundary_loops(ob) if hems else []
    n = len(used)
    if solid > 0:
        mod = ob.modifiers.new("solid", "SOLIDIFY")
        mod.thickness = solid
        mod.offset = -1
        mod.use_rim = True
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.modifier_apply(modifier=mod.name)
    total = len(me.vertices)
    reps = total // n if total % n == 0 else 1
    smart_uv(ob, tile_m)
    me.materials.append(get_mat(mat))
    ob.shape_key_add(name="Basis", from_mix=False)
    base_co = np.empty(total * 3)
    me.vertices.foreach_get("co", base_co)
    base_co = base_co.reshape(-1, 3)
    for k, d in base.keyB.items():
        sk = ob.shape_key_add(name=k, from_mix=False)
        sk.value = 0.0
        dd = np.tile(d[used], (reps, 1)) if reps > 1 else d[used]
        if len(dd) != total:
            dd = np.zeros((total, 3))
        sk.data.foreach_set("co", (base_co + dd).reshape(-1))
    attach(ob, arm)
    # barras/golas: tubos ao longo das bordas, com pesos e morphs copiados da peça
    if loops:
        tubes = []
        for i, lp in enumerate(loops):
            pts = smooth_loop(lp, 3)
            tubes.append(tube_along(pts, hem_r, f"{name}_hem{i}", mat, True, 6, 0.8))
        hem = join_into(tubes[0], tubes[1:]) if len(tubes) > 1 else tubes[0]
        smart_uv(hem, tile_m)
        transfer_skin(hem, ob, arm, base)
        ob = join_into(ob, [hem])
        ob.name = name
    return ob


def smooth_loop(pts, iters=3):
    pts = [Vector(p) for p in pts]
    n = len(pts)
    for _ in range(iters):
        pts = [(pts[i - 1] + pts[i] * 2 + pts[(i + 1) % n]) / 4 for i in range(n)]
    return pts


def get_mat(name):
    m = bpy.data.materials.get(name)
    if m is None:
        cols = {"skin": (0.75, 0.55, 0.45), "top": (0.25, 0.4, 0.6), "bottom": (0.2, 0.22, 0.3), "shoe": (0.15, 0.12, 0.1),
                "sole": (0.8, 0.78, 0.74), "hair": (0.1, 0.07, 0.05), "cap": (0.6, 0.15, 0.1), "bag": (0.3, 0.25, 0.2),
                "apron": (0.85, 0.82, 0.75), "metal": (0.7, 0.7, 0.72), "eye": (0.9, 0.9, 0.9), "lash": (0.02, 0.02, 0.02),
                "belt": (0.12, 0.08, 0.05), "strap": (0.15, 0.15, 0.16)}
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*cols.get(name, (0.5, 0.5, 0.5)), 1)
    return m


def boundary_loops(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    edges = [e for e in bm.edges if e.is_boundary]
    adj = {}
    for e in edges:
        a, b = e.verts[0].index, e.verts[1].index
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    co = {v.index: v.co.copy() for v in bm.verts}
    bm.free()
    loops, seen = [], set()
    for start in adj:
        if start in seen:
            continue
        loop = [start]
        seen.add(start)
        prev, cur = None, start
        while True:
            nxt = [x for x in adj[cur] if x != prev and x not in seen]
            if not nxt:
                break
            prev, cur = cur, nxt[0]
            loop.append(cur)
            seen.add(cur)
        if len(loop) > 4:
            loops.append([co[i] for i in loop])
    return loops


def tube_along(points, radius, name, mat, closed=True, sides=6, flat=1.0):
    """Tubo (gola, cós, alça) passando pelos pontos; devolve objeto sem pesos."""
    pts = [Vector(p) for p in points]
    n = len(pts)
    verts, faces = [], []
    cen = sum(pts, Vector()) / n
    for i, p in enumerate(pts):
        t = (pts[(i + 1) % n] - pts[i - 1]) if closed else (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)])
        t.normalize()
        out = (p - cen)
        out = (out - t * out.dot(t)).normalized()
        up = t.cross(out).normalized()
        for k in range(sides):
            a = 2 * math.pi * k / sides
            verts.append(p + out * math.cos(a) * radius + up * math.sin(a) * radius * flat)
    rng = n if closed else n - 1
    for i in range(rng):
        j = (i + 1) % n
        for k in range(sides):
            k2 = (k + 1) % sides
            faces.append((i * sides + k, i * sides + k2, j * sides + k2, j * sides + k))
    ob = make_mesh(name, verts, faces)
    ob.data.materials.append(get_mat(mat))
    return ob


def transfer_skin(ob, src, arm, base):
    """Pesos e morphs copiados do vértice mais próximo da peça de origem (para detalhes)."""
    import mathutils
    sme = src.data
    kd = mathutils.kdtree.KDTree(len(sme.vertices))
    for v in sme.vertices:
        kd.insert(v.co, v.index)
    kd.balance()
    names = [g.name for g in src.vertex_groups]
    for nm in names:
        ob.vertex_groups.new(name=nm)
    wtab = {}
    for v in sme.vertices:
        wtab[v.index] = [(src.vertex_groups[g.group].name, g.weight) for g in v.groups]
    near = []
    for v in ob.data.vertices:
        _, idx, _ = kd.find(v.co)
        near.append(idx)
        for nm, w in wtab[idx]:
            ob.vertex_groups[nm].add([v.index], w, "REPLACE")
    if src.data.shape_keys:
        ob.shape_key_add(name="Basis", from_mix=False)
        sk_src = src.data.shape_keys.key_blocks
        basis = sk_src["Basis"]
        for kb in sk_src[1:]:
            sk = ob.shape_key_add(name=kb.name, from_mix=False)
            sk.value = 0.0
            for v, idx in zip(ob.data.vertices, near):
                sk.data[v.index].co = v.co + (kb.data[idx].co - basis.data[idx].co)
    attach(ob, arm)
    return ob


def join_into(target, objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.join()
    return target


# ----------------------------------------------------------------------------- cabelo e rosto
class Face:
    def __init__(self, base, anat):
        self.e = (base.joint("joint-l-eye") + base.joint("joint-r-eye")) / 2
        # joint-mouth é o pivô da mandíbula (dentro da cabeça); a boca fica ~7,4 cm
        # abaixo dos olhos, na frente do rosto.
        self.m = self.e + np.array([0.0, -0.01, -0.074])
        self.anat = anat
        self.P = base.B

    def local(self, v):
        p = self.P[v]
        return p[..., 0], p[..., 1] - self.e[1], p[..., 2] - self.e[2]

    def scalp(self, v, front=0.045, side=0.008, back=-0.075):
        x, dy, dz = self.local(v)
        ax = abs(x)
        if dy > 0.0:
            return dz > front - 0.45 * max(0.0, ax - 0.025)
        if dy > -0.06:
            return dz > side
        return dz > back


def noise3(p, scale, seed=1):
    """Ruído suave 3D barato (soma de senos) para cabelo bagunçado/cacheado."""
    r = np.random.default_rng(seed)
    out = np.zeros(len(p))
    for _ in range(6):
        k = r.normal(size=3) * scale
        ph = r.random() * 6.28
        out += np.sin(p @ k + ph)
    return out / 6


def hair_uv(ob, crown):
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    uv = me.uv_layers.active.data
    for p in me.polygons:
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            ang = math.atan2(co.x, co.y - crown[1])
            u = ang / math.pi * 1.2
            v = (crown[2] - co.z) * 3.0 + (Vector((co.x, co.y - crown[1], 0)).length * 2.0)
            uv[li].uv = (u, v)


def rigid_to(ob, arm, bone="head"):
    vg = ob.vertex_groups.new(name=bone)
    vg.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    attach(ob, arm)


def largest_component(face_ids, base):
    """Mantém só o maior conjunto conexo de faces (remove ilhas soltas)."""
    vf = {}
    for f in face_ids:
        for v in base.F[f]:
            vf.setdefault(v, []).append(f)
    seen, best = set(), []
    for f in face_ids:
        if f in seen:
            continue
        comp, stack = [], [f]
        seen.add(f)
        while stack:
            c = stack.pop()
            comp.append(c)
            for v in base.F[c]:
                for g in vf[v]:
                    if g not in seen:
                        seen.add(g)
                        stack.append(g)
        if len(comp) > len(best):
            best = comp
    return best


def ring_distance(me, maxr=6):
    """Distância em anéis de arestas até a borda aberta (0 na borda)."""
    bm = bmesh.new()
    bm.from_mesh(me)
    dist = {v.index: (0 if v.is_boundary else maxr) for v in bm.verts}
    frontier = [v for v in bm.verts if v.is_boundary]
    r = 0
    while frontier and r < maxr:
        r += 1
        nxt = []
        for v in frontier:
            for e in v.link_edges:
                o = e.other_vert(v)
                if dist[o.index] > r:
                    dist[o.index] = r
                    nxt.append(o)
        frontier = nxt
    bm.free()
    return np.array([dist[i] for i in range(len(me.vertices))], dtype=float)


def hair_cap(base, arm, face, name, offset, lump=0.0, lump_scale=40.0, front=0.045, side=0.008, back=-0.075, seed=1, taper=3.0):
    def ok(i):
        x, dy, dz = face.local([i])
        if abs(x[0]) > 0.073 and -0.075 < dz[0] < 0.018 and -0.115 < dy[0] < -0.035:
            return False   # orelhas
        return face.scalp([i], front, side, back)
    ids = face_pick(base, "body", lambda v: face.anat.kind[v[0]] == "head" and all(ok(i) for i in v))
    ids = largest_component(ids, base)
    ob, used = submesh(base, ids, name, with_uv=False)
    me = ob.data
    me.update()
    relax_boundary(me, 4, 0.5)
    me.update()
    nrm = np.array([v.normal for v in me.vertices])
    co0 = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co0)
    P = co0.reshape(-1, 3)
    k = np.clip(ring_distance(me) / taper, 0.15, 1.0) if taper else 1.0
    off = (offset + ((noise3(P, lump_scale, seed) * 0.5 + 0.5) * lump if lump else 0.0)) * k
    co = P + nrm * np.atleast_1d(off)[:, None]
    me.vertices.foreach_set("co", co.reshape(-1))
    return ob


def extend_down(ob, length, flare, rows=6, only_back=False, eye=None, taper=0.0):
    """Estende a borda de baixo do casco de cabelo para baixo (cabelo comprido/chanel)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.verts.ensure_lookup_table()
    edges = [e for e in bm.edges if e.is_boundary]
    if only_back and eye is not None:
        edges = [e for e in edges if all(v.co.y - eye[1] < 0.03 for v in e.verts)]
    cen = Vector((eye[0], eye[1] - 0.03, 0)) if eye is not None else Vector()
    geom = edges
    for r in range(rows):
        res = bmesh.ops.extrude_edge_only(bm, edges=geom)
        newv = [g for g in res["geom"] if isinstance(g, bmesh.types.BMVert)]
        for v in newv:
            out = Vector((v.co.x - cen.x, v.co.y - cen.y, 0))
            if out.length > 1e-6:
                out.normalize()
            v.co.z -= length / rows
            v.co += out * (flare / rows) - out * taper / rows
        geom = [g for g in res["geom"] if isinstance(g, bmesh.types.BMEdge)]
    bm.to_mesh(ob.data)
    bm.free()


def solidify_apply(ob, t):
    mod = ob.modifiers.new("solid", "SOLIDIFY")
    mod.thickness = t
    mod.offset = -1
    mod.use_rim = True
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def ellipsoid(name, center, radii, mat, segs=16, rings=10):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=rings, radius=1, location=center)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = radii
    bpy.ops.object.transform_apply(scale=True)
    ob.data.materials.append(get_mat(mat))
    for p in ob.data.polygons:
        p.use_smooth = True
    return ob


def hair_styles(base, arm, anat, want):
    face = Face(base, anat)
    e = face.e
    crown = Vector((0, e[1] - 0.03, e[2] + 0.13))
    out = {}

    def finish(ob, name, thick=0.006):
        if thick:
            solidify_apply(ob, thick)
        ob.data.materials.clear()
        ob.data.materials.append(get_mat("hair"))
        for p in ob.data.polygons:
            p.use_smooth = True
        hair_uv(ob, crown)
        rigid_to(ob, arm)
        ob.name = name
        out[name] = ob

    if want("Hair_short"):
        finish(hair_cap(base, arm, face, "Hair_short", 0.009, 0.003, 60, seed=3), "Hair_short")
    if want("Hair_slick"):
        finish(hair_cap(base, arm, face, "Hair_slick", 0.006, 0.0, front=0.05), "Hair_slick", 0.004)
    if want("Hair_messy"):
        finish(hair_cap(base, arm, face, "Hair_messy", 0.016, 0.02, 45, front=0.035, seed=7), "Hair_messy")
    if want("Hair_curly"):
        finish(hair_cap(base, arm, face, "Hair_curly", 0.028, 0.03, 70, front=0.035, side=0.0, back=-0.06, seed=11), "Hair_curly", 0.01)
    if want("Hair_long"):
        ob = hair_cap(base, arm, face, "Hair_long", 0.012, 0.004, 50, front=0.04, side=-0.02, back=-0.09, seed=5)
        extend_down(ob, 0.30, 0.045, rows=7, only_back=True, eye=e)
        finish(ob, "Hair_long", 0.008)
    if want("Hair_bob"):
        ob = hair_cap(base, arm, face, "Hair_bob", 0.014, 0.004, 50, front=0.04, side=-0.02, back=-0.09, seed=6)
        extend_down(ob, 0.08, 0.03, rows=4, only_back=True, eye=e)
        finish(ob, "Hair_bob", 0.008)
    for style in ("bun", "ponytail"):
        nm = "Hair_" + style
        if not want(nm):
            continue
        cap = hair_cap(base, arm, face, nm, 0.007, 0.0, front=0.05, seed=9)
        if style == "bun":
            extra = ellipsoid("bun", (0, e[1] - 0.12, e[2] + 0.085), (0.045, 0.04, 0.042), "hair")
        else:
            extra = ellipsoid("tail", (0, e[1] - 0.135, e[2] - 0.06), (0.03, 0.03, 0.11), "hair")
            tie = ellipsoid("tie", (0, e[1] - 0.122, e[2] + 0.02), (0.022, 0.018, 0.012), "hair")
            extra = join_into(extra, [tie])
        cap = join_into(cap, [extra])
        finish(cap, nm, 0.004)
    # barba e bigode (rentes ao rosto)
    m = face.m

    def beard_pred(v):
        if face.anat.kind[v[0]] != "head":
            return False
        x, dy, dz = face.local(v)
        x, dy, dz = abs(x).mean(), dy.mean(), dz.mean()
        mz = m[2] - e[2]
        lips = x < 0.027 and mz - 0.014 < dz < mz + 0.009
        return dy > -0.05 and mz - 0.052 < dz and not lips and ((x < 0.04 and dz < mz + 0.012) or (x >= 0.04 and dz < mz + 0.03 - (x - 0.04) * 0.2))

    def stache_pred(v):
        x, dy, dz = face.local(v)
        x, dy, dz = abs(x).mean(), dy.mean(), dz.mean()
        mz = m[2] - e[2]
        return dy > 0.0 and x < 0.034 and mz + 0.007 < dz < mz + 0.024

    for nm, pred, off in (("Beard", beard_pred, 0.005), ("Mustache", stache_pred, 0.004)):
        if not want(nm):
            continue
        ob, used = submesh(base, largest_component(face_pick(base, "body", pred), base), nm, with_uv=False)
        me = ob.data
        me.update()
        relax_boundary(me, 5, 0.6)
        me.update()
        nrm = np.array([v.normal for v in me.vertices])
        me.vertices.foreach_set("co", (base.B[used] + nrm * off).reshape(-1))
        finish(ob, nm, 0.003)
    # bonés e gorros
    if want("Cap"):
        cap = hair_cap(base, arm, face, "Cap", 0.022, 0.0, front=0.055, side=0.03, back=0.0)
        solidify_apply(cap, 0.004)
        bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=1, depth=0.006, location=(0, e[1] + 0.07, e[2] + 0.06))
        brim = bpy.context.active_object
        brim.scale = (0.085, 0.075, 1)
        brim.rotation_euler = (math.radians(-8), 0, 0)
        bpy.ops.object.transform_apply(scale=True, rotation=True)
        bm = bmesh.new()
        bm.from_mesh(brim.data)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.y < -0.02], context="VERTS")
        bm.to_mesh(brim.data)
        bm.free()
        btn = ellipsoid("btn", (0, crown[1] + 0.01, crown[2] + 0.04), (0.008, 0.008, 0.005), "cap")
        cap = join_into(cap, [brim, btn])
        cap.data.materials.clear()
        cap.data.materials.append(get_mat("cap"))
        smart_uv(cap, 0.3)
        rigid_to(cap, arm)
        out["Cap"] = cap
    if want("Beanie"):
        bn = hair_cap(base, arm, face, "Beanie", 0.026, 0.0, front=0.055, side=0.0, back=-0.04)
        solidify_apply(bn, 0.006)
        bn.data.materials.append(get_mat("cap"))
        smart_uv(bn, 0.15)
        rigid_to(bn, arm)
        out["Beanie"] = bn
    # olhos e cílios (ajudantes da malha base)
    if want("Eyes"):
        eyes, used = submesh(base, base.faces_of("helper-l-eye") + base.faces_of("helper-r-eye"), "Eyes", with_uv=False)
        eyes.data.materials.append(get_mat("eye"))
        me = eyes.data
        me.uv_layers.new(name="UVMap")
        uv = me.uv_layers.active.data
        cl, cr = base.joint("joint-l-eye"), base.joint("joint-r-eye")
        for p in me.polygons:
            for li in p.loop_indices:
                co = me.vertices[me.loops[li].vertex_index].co
                c = cl if co.x < 0 else cr
                uv[li].uv = ((co.x - c[0]) / 0.026 + 0.5, (co.z - c[2]) / 0.026 + 0.5)
        add_keys_from(eyes, base, used)
        rigid_to(eyes, arm)
        out["Eyes"] = eyes
        lash_ids = []
        for g in ("helper-l-eyelashes-1", "helper-r-eyelashes-1", "helper-l-eyelashes-2", "helper-r-eyelashes-2"):
            lash_ids += base.faces_of(g)
        if lash_ids:
            lashes, used = submesh(base, lash_ids, "Lashes", with_uv=False)
            lashes.data.materials.append(get_mat("lash"))
            add_keys_from(lashes, base, used)
            rigid_to(lashes, arm)
            out["Lashes"] = lashes
    return out


def add_keys_from(ob, base, used):
    ob.shape_key_add(name="Basis", from_mix=False)
    for k, d in base.keyB.items():
        sk = ob.shape_key_add(name=k, from_mix=False)
        sk.value = 0.0
        sk.data.foreach_set("co", (base.B[used] + d[used]).reshape(-1))


# ----------------------------------------------------------------------------- texturas de pele e olhos
def rasterize(base, face_ids, values, size):
    """Pinta valores por vértice no espaço UV (interpolação baricêntrica). values: (n, c)."""
    C = values.shape[1]
    img = np.zeros((size, size, C))
    cov = np.zeros((size, size))
    for fi in face_ids:
        vs, ts = base.F[fi], base.FT[fi]
        for tri in ((0, 1, 2), (0, 2, 3)) if len(vs) == 4 else ((0, 1, 2),):
            uv = np.array([base.VT[ts[k]] for k in tri]) * size
            uv[:, 1] = size - uv[:, 1]
            val = np.array([values[vs[k]] for k in tri])
            x0, y0 = np.floor(uv.min(0)).astype(int) - 1
            x1, y1 = np.ceil(uv.max(0)).astype(int) + 1
            x0, y0 = max(x0, 0), max(y0, 0)
            x1, y1 = min(x1, size - 1), min(y1, size - 1)
            if x1 < x0 or y1 < y0:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            a, b, c = uv
            den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(den) < 1e-9:
                continue
            w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / den
            w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / den
            w2 = 1 - w0 - w1
            m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
            if not m.any():
                continue
            v = w0[..., None] * val[0] + w1[..., None] * val[1] + w2[..., None] * val[2]
            sub = img[y0:y1 + 1, x0:x1 + 1]
            sub[m] = v[m]
            cov[y0:y1 + 1, x0:x1 + 1][m] = 1
    # dilata para cobrir as bordas das ilhas (evita costuras com mipmaps)
    for _ in range(8):
        empty = cov == 0
        if not empty.any():
            break
        acc = np.zeros_like(img)
        cnt = np.zeros_like(cov)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            acc += np.roll(np.roll(img * cov[..., None], dy, 0), dx, 1)
            cnt += np.roll(np.roll(cov, dy, 0), dx, 1)
        fill = empty & (cnt > 0)
        img[fill] = acc[fill] / cnt[fill][:, None]
        cov[fill] = 1
    return img


def skin_textures(base, anat, size=1024):
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    P = base.B
    n = len(P)
    face = Face(base, anat)
    e, m = face.e, face.m
    x, dy, dz = P[:, 0], P[:, 1] - e[1], P[:, 2] - e[2]
    ax = np.abs(x)
    head = anat.kind == "head"
    front = dy > -0.03
    # sobrancelhas: arco acima de cada olho
    cl = base.joint("joint-l-eye")
    ex = abs(cl[0])
    u = ax - ex
    brow_c = 0.019 + 0.004 * np.cos(np.clip(u / 0.03, -1.5, 1.5)) - 0.004 * (u > 0.012) * (u - 0.012) / 0.012
    thick = np.clip(0.0045 - 0.0018 * np.clip(u / 0.02, 0, 1), 0.002, 0.005)
    brow = np.clip(1 - np.abs(dz - brow_c) / thick, 0, 1) * ((u > -0.024) & (u < 0.03)) * front * head
    brow = np.clip(brow * 1.6, 0, 1)
    # lábios
    lz = P[:, 2] - (e[2] - 0.077)
    lips = np.clip(1 - ((x / 0.025) ** 2 + (lz / 0.011) ** 2), 0, 1) * (dy > -0.02) * head
    lips = np.clip(lips * 3.0, 0, 1)
    # cor de detalhe (multiplica o tom de pele)
    det = np.ones((n, 3))
    def blob(cx, cz, rx, rz, side_front=True):
        g = np.exp(-(((ax - cx) / rx) ** 2 + ((dz - cz) / rz) ** 2))
        return g * (front if side_front else 1) * head
    cheeks = blob(0.042, -0.045, 0.02, 0.02)
    nose = np.exp(-((x / 0.012) ** 2 + ((dz + 0.04) / 0.012) ** 2)) * (dy > 0.015) * head
    ears = (ax > 0.07) * (dz > -0.07) * (dz < 0.02) * (dy < -0.04) * (dy > -0.11) * head
    sockets = np.exp(-(((ax - ex) / 0.018) ** 2 + ((dz + 0.002) / 0.012) ** 2)) * front * head
    red = np.clip(cheeks * 0.6 + nose * 0.5 + ears * 0.5, 0, 1)
    det *= (1 - red[:, None] * np.array([0.0, 0.12, 0.12]))
    det *= (1 - sockets[:, None] * np.array([0.18, 0.2, 0.15]))
    # articulações e extremidades levemente avermelhadas
    for jn in ("joint-l-knee", "joint-r-knee", "joint-l-elbow", "joint-r-elbow"):
        j = base.joint(jn)
        g = np.exp(-np.sum((P - j) ** 2, axis=1) / 0.03 ** 2)
        det *= (1 - g[:, None] * np.array([0.0, 0.08, 0.08]))
    hands = (anat.kind == "hand")
    det[hands] *= np.array([1.0, 0.95, 0.94])
    det = np.clip(det, 0, 1)
    region = np.array([anat.region(i) for i in range(n)], dtype=float)
    body_faces = base.faces_of("body")
    vals = np.concatenate([det, region[:, None] / 16.0, brow[:, None], lips[:, None]], axis=1)
    img = rasterize(base, body_faces, vals, size)
    # poros e manchas sutis
    sys.path.insert(0, os.path.join(ROOT, "tools", "art"))
    from texlib import spectral
    pores = spectral(size, 301, 0.3, 300) - 0.5
    mott = spectral(size, 302, 2.0, 6) - 0.5
    detail = img[..., :3] * (1 + 0.035 * pores[..., None] + 0.04 * mott[..., None])
    # pelos da sobrancelha: listras finas
    hairs = spectral(size, 303, 0.5, 200, aniso=(0.2, 1.0))
    browm = np.clip(img[..., 4] * (0.75 + 0.5 * hairs), 0, 1)
    to8 = lambda a: (np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)
    Image.fromarray(to8(detail)).save(os.path.join(OUT, "skin_detail.png"), optimize=True)
    # região: arredonda (sem interpolação entre regiões)
    reg = np.round(img[..., 3] * 16.0) / 16.0
    mask = np.stack([reg, browm, np.clip(img[..., 5], 0, 1)], -1)
    Image.fromarray(to8(mask)).save(os.path.join(OUT, "skin_mask.png"))
    # olho: esclera, íris com raios, pupila
    s = 256
    yy, xx = np.mgrid[0:s, 0:s] / s - 0.5
    r = np.sqrt(xx ** 2 + yy ** 2)
    ang = np.arctan2(yy, xx)
    sclera = np.stack([np.full_like(r, 0.9), np.full_like(r, 0.87), np.full_like(r, 0.83)], -1)
    sclera *= (1 - 0.15 * np.clip((r - 0.3) / 0.2, 0, 1))[..., None]
    rays = 0.75 + 0.25 * np.sin(ang * 37) * np.sin(ang * 11 + 1)
    iris = np.array([0.32, 0.2, 0.11]) * (0.6 + 0.6 * rays[..., None] * (0.6 + 0.8 * r[..., None] / 0.22))
    t_iris = np.clip((0.215 - r) / 0.012, 0, 1)[..., None]
    eye = sclera * (1 - t_iris) + iris * t_iris
    ring = np.exp(-((r - 0.205) / 0.012) ** 2)[..., None]
    eye *= (1 - 0.6 * ring)
    pupil = np.clip((0.085 - r) / 0.01, 0, 1)[..., None]
    eye = eye * (1 - pupil) + 0.02 * pupil
    Image.fromarray(to8(eye)).save(os.path.join(OUT, "eye.png"))


# ----------------------------------------------------------------------------- poses
def bone_order(arm):
    out = []

    def rec(b):
        out.append(b.name)
        for c in b.children:
            rec(c)
    for b in arm.data.bones:
        if b.parent is None:
            rec(b)
    return out


def V3(x):
    return Vector(x).normalized()


def apply_pose(arm, spec, frame=None):
    """spec: {osso: {"aim": dir, "rot": [(eixo, graus), ...], "lrot": [(eixo local, graus)], "loc": desloc.}}
    Rotações em eixos do mundo do personagem (X = direita, Y = frente, Z = cima),
    aplicadas depois do movimento do pai. Grava quadros-chave se frame for dado."""
    rest = {b.name: b.matrix_local.copy() for b in arm.data.bones}
    posed = {}
    for name in bone_order(arm):
        b = arm.data.bones[name]
        pb = arm.pose.bones[name]
        par = b.parent.name if b.parent else None
        M0 = (posed[par] @ rest[par].inverted() @ rest[name]) if par else rest[name].copy()
        sp = spec.get(name, {})
        h = M0.translation.copy()
        R = Matrix.Identity(4)
        if "aim" in sp:
            cur = (M0.to_3x3() @ Vector((0, 1, 0))).normalized()
            R = cur.rotation_difference(V3(sp["aim"])).to_matrix().to_4x4()
        for axis, deg in sp.get("lrot", []):
            ax = (R.to_3x3() @ M0.to_3x3() @ Vector(axis)).normalized()
            R = Matrix.Rotation(math.radians(deg), 4, ax) @ R
        for axis, deg in sp.get("rot", []):
            R = Matrix.Rotation(math.radians(deg), 4, Vector(axis)) @ R
        M = Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ M0
        if "loc" in sp:
            M = Matrix.Translation(Vector(sp["loc"])) @ M
        posed[name] = M
        pb.rotation_mode = "QUATERNION"
        pb.matrix_basis = ((posed[par] @ rest[par].inverted() @ rest[name]) if par else rest[name]).inverted() @ M
        if frame is not None:
            pb.keyframe_insert("rotation_quaternion", frame=frame)
            pb.keyframe_insert("location", frame=frame)


def fingers(curl, thumb=None, sides=("l", "r")):
    sp = {}
    for sd in sides:
        for f in ("index", "middle", "ring", "pinky"):
            for k in (1, 2, 3):
                sp[f"{f}_0{k}_{sd}"] = {"lrot": [((1, 0, 0), curl * (0.7 if k == 1 else 1.0))]}
        t = curl * 0.4 if thumb is None else thumb
        for k in (2, 3):
            sp[f"thumb_0{k}_{sd}"] = {"lrot": [((1, 0, 0), t)]}
    return sp


def stance(arms_out=0.0, elbow=12.0):
    """Postura neutra: braços ao lado do corpo (o rig vem em pose A)."""
    sp = {
        "upperarm_l": {"aim": (-0.2 - arms_out, 0.03, -1)},
        "upperarm_r": {"aim": (0.2 + arms_out, 0.03, -1)},
        "lowerarm_l": {"aim": (-0.1, math.tan(math.radians(elbow)), -1)},
        "lowerarm_r": {"aim": (0.1, math.tan(math.radians(elbow)), -1)},
        "hand_l": {"aim": (-0.04, math.tan(math.radians(elbow + 4)), -1)},
        "hand_r": {"aim": (0.04, math.tan(math.radians(elbow + 4)), -1)},
    }
    sp.update(fingers(25))
    return sp


def merge(*specs):
    out = {}
    for sp in specs:
        for k, v in sp.items():
            d = out.setdefault(k, {})
            for kk, vv in v.items():
                if kk in ("rot", "lrot"):
                    d[kk] = d.get(kk, []) + list(vv)
                else:
                    d[kk] = vv
    return out


# ----------------------------------------------------------------------------- animações
X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)


def bump(p, c, w):
    d = (p - c + 0.5) % 1.0 - 0.5
    return math.exp(-(d / w) ** 2)


def cos2(p):
    return math.cos(2 * math.pi * p)


def sin2(p):
    return math.sin(2 * math.pi * p)


def legs_gait(p, run=False):
    sp = {}
    for side, off in (("l", 0.0), ("r", 0.5)):
        q = (p + off) % 1.0
        if run:
            thigh = 38 * cos2(q) + 8
            flex = 18 + 95 * bump(q, 0.68, 0.17) + 22 * bump(q, 0.1, 0.08)
        else:
            thigh = 23 * cos2(q) + 2
            flex = 5 + 55 * bump(q, 0.72, 0.13) + 12 * bump(q, 0.1, 0.08)
        foot = -(thigh - flex) + 10 * bump(q, 0.0, 0.06) - (30 if run else 22) * bump(q, 0.48, 0.07) + 8 * bump(q, 0.85, 0.1)
        if run and 0.55 < q < 0.95:
            foot = -(thigh - flex) * 0.6 + 10
        sp[f"thigh_{side}"] = {"rot": [(X, thigh)]}
        sp[f"calf_{side}"] = {"rot": [(X, -flex)]}
        sp[f"foot_{side}"] = {"rot": [(X, foot)]}
        sp[f"ball_{side}"] = {"rot": [(X, 15 * bump(q, 0.45, 0.07))]}
    return sp


def gait(p, run=False, elderly=False):
    sp = stance(elbow=(78 if run else 18))
    sp = merge(sp, legs_gait(p, run))
    a = 42 if run else 17
    sp = merge(sp, {
        "upperarm_l": {"rot": [(X, -a * cos2(p) + (8 if run else 0))]},
        "upperarm_r": {"rot": [(X, a * cos2(p) + (8 if run else 0))]},
        "lowerarm_l": {"rot": [(X, 8 * max(0, -cos2(p)) * (2 if run else 1))]},
        "lowerarm_r": {"rot": [(X, 8 * max(0, cos2(p)) * (2 if run else 1))]},
    })
    bob = (0.045 if run else 0.022)
    sp["pelvis"] = {"loc": (0.012 * sin2(p) * (0.5 if run else 1), 0, -0.03 - bob * 0.5 + bob * 0.5 * (1 - math.cos(4 * math.pi * p)) / 2 * 2 - (0.05 if run else 0)),
                    "rot": [(Z, -6 * cos2(p) * (1.3 if run else 1)), (Y, 3 * sin2(p))]}
    lean = 11 if run else (8 if elderly else 2)
    # ossos que apontam para cima: rotação negativa em X inclina para a frente
    sp["spine_01"] = {"rot": [(X, -lean * 0.4), (Z, 3 * cos2(p))]}
    sp["spine_02"] = {"rot": [(X, -lean * 0.3), (Z, 3 * cos2(p))]}
    sp["spine_03"] = {"rot": [(X, -lean * 0.3 - (1.5 if not run else 0) * math.cos(4 * math.pi * p))]}
    sp["head"] = {"rot": [(X, lean * 0.6), (Z, 1.5 * cos2(p))]}
    return sp


def breathing(t, amp=1.0):
    b = math.sin(2 * math.pi * t)
    return {"spine_02": {"rot": [(X, -0.8 * b * amp)]}, "spine_03": {"rot": [(X, -1.0 * b * amp)]},
            "clavicle_l": {"rot": [(Y, 0.8 * b * amp)]}, "clavicle_r": {"rot": [(Y, -0.8 * b * amp)]}}


def idle(t):
    sp = stance()
    shift = math.sin(2 * math.pi * t)
    sp = merge(sp, breathing(t * 2), {
        "pelvis": {"loc": (0.012 * shift, 0, -0.004), "rot": [(Y, 1.8 * shift)]},
        "thigh_l": {"rot": [(Y, -1.8 * shift), (X, 1.0)]}, "thigh_r": {"rot": [(Y, -1.8 * shift), (X, -1.0)]},
        "calf_l": {"rot": [(X, -2 - 2 * max(0, -shift))]}, "calf_r": {"rot": [(X, -2 - 2 * max(0, shift))]},
        "foot_l": {"rot": [(X, 1)]}, "foot_r": {"rot": [(X, 1)]},
        "spine_01": {"rot": [(Y, -1.2 * shift)]},
        "head": {"rot": [(Z, 9 * math.sin(2 * math.pi * t * 1.0 + 1.0) * bump(t, 0.6, 0.25)), (X, 2 * math.sin(2 * math.pi * t * 2))]},
        "upperarm_l": {"rot": [(X, 1.5 * math.sin(2 * math.pi * t * 2 + 0.5))]}, "upperarm_r": {"rot": [(X, 1.5 * math.sin(2 * math.pi * t * 2 + 1.0))]},
    })
    return sp


def talk(t):
    sp = idle(t)
    g = math.sin(2 * math.pi * t * 3)
    return merge(sp, {
        "upperarm_r": {"rot": [(X, 22 + 8 * g), (Y, -6)]},
        "lowerarm_r": {"rot": [(X, 55 + 15 * math.sin(2 * math.pi * t * 4))]},
        "hand_r": {"rot": [(Z, 20 * math.sin(2 * math.pi * t * 3 + 1))]},
        "upperarm_l": {"rot": [(X, 8 + 6 * math.sin(2 * math.pi * t * 2 + 2))]},
        "lowerarm_l": {"rot": [(X, 30 + 10 * math.sin(2 * math.pi * t * 2 + 2))]},
        "head": {"rot": [(X, 4 * math.sin(2 * math.pi * t * 4)), (Z, 8 * math.sin(2 * math.pi * t))]},
    })


def work(t):
    sp = idle(t * 0.5)
    a = math.sin(2 * math.pi * t * 2)
    b = math.sin(2 * math.pi * t * 2 + 1.7)
    return merge(sp, {
        "spine_01": {"rot": [(X, -6)]}, "spine_02": {"rot": [(X, -4)]},
        "upperarm_l": {"rot": [(X, 38 + 8 * a), (Y, 8)]}, "upperarm_r": {"rot": [(X, 38 + 8 * b), (Y, -8)]},
        "lowerarm_l": {"rot": [(X, 55 + 10 * b), (Y, 10)]}, "lowerarm_r": {"rot": [(X, 55 + 10 * a), (Y, -10)]},
        "hand_l": {"rot": [(X, -15)]}, "hand_r": {"rot": [(X, -15)]},
        "head": {"rot": [(X, -14)]},
    })


def eat(t):
    sp = idle(t)
    k = 0.5 - 0.5 * math.cos(2 * math.pi * t)
    up = smooth01(k)
    return merge(sp, {
        "upperarm_r": {"rot": [(X, 15 + 45 * up), (Z, 22 * up)]},
        "lowerarm_r": {"rot": [(X, 50 + 108 * up), (Z, 30 * up)]},
        "hand_r": {"rot": [(X, 20 * up)]},
        "upperarm_l": {"rot": [(X, 20)]}, "lowerarm_l": {"rot": [(X, 60)]},
        "head": {"rot": [(X, 6 * up - 4)]},
    })


def smooth01(x):
    x = min(1, max(0, x))
    return x * x * (3 - 2 * x)


def search(t):
    sp = idle(t * 0.5)
    a = math.sin(2 * math.pi * t * 2)
    return merge(sp, {
        "pelvis": {"loc": (0, -0.03, -0.03)},
        "spine_01": {"rot": [(X, -18)]}, "spine_02": {"rot": [(X, -14)]}, "spine_03": {"rot": [(X, -10)]},
        "thigh_l": {"rot": [(X, 8)]}, "thigh_r": {"rot": [(X, 8)]},
        "calf_l": {"rot": [(X, -14)]}, "calf_r": {"rot": [(X, -14)]},
        "foot_l": {"rot": [(X, 6)]}, "foot_r": {"rot": [(X, 6)]},
        "upperarm_l": {"rot": [(X, 45 + 10 * a), (Y, 10)]}, "upperarm_r": {"rot": [(X, 45 - 10 * a), (Y, -10)]},
        "lowerarm_l": {"rot": [(X, 25 + 15 * a)]}, "lowerarm_r": {"rot": [(X, 25 - 15 * a)]},
        "head": {"rot": [(X, -14), (Z, 10 * math.sin(2 * math.pi * t))]},
    })


def sit(t):
    sp = stance(elbow=50)
    sp = merge(sp, breathing(t * 2, 0.8), {
        "pelvis": {"loc": (0, -0.04, -0.46), "rot": [(X, -6)]},
        "thigh_l": {"rot": [(X, 92), (Y, 3)]}, "thigh_r": {"rot": [(X, 92), (Y, -3)]},
        "calf_l": {"rot": [(X, -90)]}, "calf_r": {"rot": [(X, -90)]},
        "foot_l": {"rot": [(X, 2)]}, "foot_r": {"rot": [(X, 2)]},
        "spine_01": {"rot": [(X, 4)]}, "spine_02": {"rot": [(X, 2)]},
        "upperarm_l": {"rot": [(X, 28), (Y, 6)]}, "upperarm_r": {"rot": [(X, 28), (Y, -6)]},
        "lowerarm_l": {"rot": [(X, 25)]}, "lowerarm_r": {"rot": [(X, 25)]},
        "head": {"rot": [(Z, 6 * math.sin(2 * math.pi * t)), (X, 2)]},
    })
    return sp


def lie(t, knocked=False):
    """Deitado de costas, cabeça para a frente (+Y), pés na origem."""
    sp = stance(elbow=10)
    pel = base_pelvis_head
    tz = 0.0 if knocked else 0.11
    sp = merge(sp, breathing(t * 1.5, 0.6), {
        "pelvis": {"rot": [(X, 90), (Z, 180)], "loc": (0, 0.92 - pel[1], tz - pel[2])},
        "thigh_l": {"rot": [(Y, -4)]}, "thigh_r": {"rot": [(Y, 4)]},
        "foot_l": {"rot": [(Y, -8)]}, "foot_r": {"rot": [(Y, 8)]},
        "upperarm_l": {"rot": [(Z, 0)]},
    })
    if knocked:
        sp = merge(sp, {
            "thigh_r": {"lrot": [((1, 0, 0), 35)]}, "calf_r": {"lrot": [((1, 0, 0), -60)]},
            "upperarm_r": {"lrot": [((0, 0, 1), 50)]}, "lowerarm_r": {"lrot": [((1, 0, 0), 30)]},
            "head": {"lrot": [((0, 0, 1), 25)]},
        })
    return sp


base_pelvis_head = (0, 0, 0.93)

ANIMS = {
    # nome: (função(fase), quadros, ciclo)
    "idle": (idle, 120), "walk": (lambda p: gait(p), 32), "run": (lambda p: gait(p, True), 22),
    "talk": (talk, 90), "work": (work, 60), "eat": (eat, 90), "search": (search, 60),
    "sit": (sit, 120), "lie": (lambda t: lie(t), 120), "knocked": (lambda t: lie(t, True), 60),
}


def bake_animations(arm, names=None):
    global base_pelvis_head
    base_pelvis_head = tuple(arm.data.bones["pelvis"].head_local)
    if arm.animation_data is None:
        arm.animation_data_create()
    for name, (fn, frames) in ANIMS.items():
        if names and name not in names:
            continue
        act = bpy.data.actions.new(name)
        act.use_fake_user = True
        arm.animation_data.action = act
        step = 2 if frames > 40 else 1
        for f in list(range(0, frames, step)) + [frames]:
            apply_pose(arm, fn(f / frames), f)
        track = arm.animation_data.nla_tracks.new()
        track.name = name
        strip = track.strips.new(name, 0, act)
        strip.name = name
        arm.animation_data.action = None
    # pose de repouso neutra
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)


def wardrobe(base, arm, anat, only=None):
    P, tu, tf = anat.P, anat.tu, anat.tf
    parts = {}

    def want(n):
        return only is None or n in only

    def zc(v):
        return P[v, 2].mean()

    def tee(v):
        k = majority(anat, v)
        if k == "torso":
            return anat.belt - 0.06 < zc(v) < anat.neck - 0.01
        return k == "uarm" and tu[v].mean() < 0.62

    def longs(v):
        k = majority(anat, v)
        if k == "torso":
            return anat.belt - 0.06 < zc(v) < anat.neck - 0.01
        return k == "uarm" or (k == "forearm" and tf[v].mean() < 0.96)

    def pants(v):
        k = majority(anat, v)
        z = zc(v)
        if k == "torso":
            return z < anat.belt + 0.03
        return k == "thigh" or (k == "calf" and z > anat.ankle + 0.05)

    def shorts(v):
        k = majority(anat, v)
        z = zc(v)
        if k == "torso":
            return z < anat.belt + 0.03
        return k == "thigh" and z > anat.knee + 0.11

    def shoes(v):
        k = majority(anat, v)
        return k == "foot" or (k == "calf" and zc(v) < anat.ankle + 0.05)

    if want("Top_tee"):
        parts["Top_tee"] = piece(base, arm, face_pick(base, "helper-tights", tee), "Top_tee", "top", 0.013, 0.004)
    if want("Top_long"):
        parts["Top_long"] = piece(base, arm, face_pick(base, "helper-tights", longs), "Top_long", "top", 0.011, 0.004)
    if want("Top_jacket"):
        parts["Top_jacket"] = piece(base, arm, face_pick(base, "helper-tights", lambda v: longs(v) or (majority(anat, v) == "torso" and zc(v) > anat.belt - 0.14 and zc(v) < anat.neck + 0.02)), "Top_jacket", "top", 0.024, 0.006)
    def straight_leg(p, nrm=None):
        """Calça reta: empurra a perna para fora até um raio mínimo que cresce para baixo."""
        d = np.zeros_like(p)
        for sd, sx in (("l", -1), ("r", 1)):
            hip = base.joint(f"joint-{sd}-upper-leg")
            ank = base.joint(f"joint-{sd}-ankle")
            m = (p[:, 0] * sx > 0.015) & (p[:, 2] < anat.belt - 0.12)
            t = np.clip((hip[2] - p[m, 2]) / (hip[2] - ank[2]), 0, 1)
            cx = hip[0] + (ank[0] - hip[0]) * t
            cy = hip[1] + (ank[1] - hip[1]) * t
            rx, ry = p[m, 0] - cx, p[m, 1] - cy
            r = np.sqrt(rx ** 2 + ry ** 2) + 1e-6
            want_r = 0.058 + 0.008 * t
            push = np.clip(want_r - r, 0, 0.05) * smoothstep_np(t, 0.25, 0.6)
            d[m, 0] = rx / r * push
            d[m, 1] = ry / r * push
        return d

    if want("Bottom_pants"):
        parts["Bottom_pants"] = piece(base, arm, face_pick(base, "helper-tights", pants), "Bottom_pants", "bottom", 0.007, 0.004, flatten=straight_leg, hem_r=0.006)
    if want("Bottom_shorts"):
        parts["Bottom_shorts"] = piece(base, arm, face_pick(base, "helper-tights", shorts), "Bottom_shorts", "bottom", 0.008, 0.004)
    if want("Bottom_skirt"):
        parts["Bottom_skirt"] = piece(base, arm, face_pick(base, "helper-skirt", lambda v: zc(v) > anat.knee + 0.04), "Bottom_skirt", "bottom", 0.006, 0.004)
    if want("Shoes"):
        def sole(p, nrm):
            d = np.zeros_like(p)
            low = (p[:, 2] < 0.05) & (nrm[:, 2] < -0.2)
            d[low, 2] = -p[low, 2] * 0.9
            # bico arredondado: puxa as pontas dos dedos para um volume único
            toe = p[:, 1] > p[:, 1].max() - 0.06
            d[toe, 2] += 0.004
            return d
        parts["Shoes"] = piece(base, arm, face_pick(base, "body", shoes), "Shoes", "shoe", 0.011, 0.004, flatten=sole, smooth=25, hems=True, hem_r=0.006)
        sh = parts["Shoes"]
        sh.data.materials.append(get_mat("sole"))
        for p in sh.data.polygons:
            if p.center.z < 0.022:
                p.material_index = 1
    tights = [i for i in base.faces_of("helper-tights")]
    T = base.B

    def surface_point(x, z, front, push=0.018):
        """Ponto sobre a malha de ajuda (meia-calça) mais próximo de (x, z) na frente/atrás."""
        ids = base.groups["helper-tights"]
        P = T[ids]
        side = P[:, 1] > np.median(P[np.abs(P[:, 2] - z) < 0.03, 1]) if front else P[:, 1] < np.median(P[np.abs(P[:, 2] - z) < 0.03, 1])
        cand = np.where(side)[0]
        d = (P[cand, 0] - x) ** 2 + (P[cand, 2] - z) ** 2
        k = cand[np.argmin(d)]
        p = P[k].copy()
        p[1] += push if front else -push
        return Vector(p)

    if want("Top_jacket") and "Top_jacket" in parts:
        jk = parts["Top_jacket"]
        zs = np.linspace(anat.belt - 0.13, anat.neck - 0.03, 14)
        pts = [surface_point(0.0, z, True, 0.03) for z in zs]
        zip_ob = tube_along(pts, 0.004, "zip", "metal", closed=False, sides=5)
        transfer_skin(zip_ob, jk, arm, base)
        pocket = []
        for sx in (-1, 1):
            c = surface_point(sx * 0.1, anat.belt - 0.06, True, 0.032)
            pocket.append(c)
        flaps = []
        for i, c in enumerate(pocket):
            bpy.ops.mesh.primitive_cube_add(size=1, location=c)
            fl = bpy.context.active_object
            fl.scale = (0.11, 0.008, 0.012)
            bpy.ops.object.transform_apply(scale=True)
            fl.data.materials.append(get_mat("top"))
            flaps.append(fl)
        fl = join_into(flaps[0], flaps[1:])
        transfer_skin(fl, jk, arm, base)
        parts["Top_jacket"] = join_into(jk, [zip_ob, fl])
        parts["Top_jacket"].name = "Top_jacket"
    if want("Overalls"):
        def bib(v):
            k = majority(anat, v)
            p = P[v].mean(axis=0)
            return k == "torso" and anat.belt - 0.02 < p[2] < anat.chest + 0.02 and abs(p[0]) < 0.12 and p[1] > 0.0
        ob = piece(base, arm, face_pick(base, "helper-tights", bib), "Overalls", "bottom", 0.02, 0.004, hem_r=0.005)
        straps = []
        for sx in (-1, 1):
            path = [surface_point(sx * 0.09, anat.chest + 0.0, True, 0.022), surface_point(sx * 0.1, anat.chest + 0.07, True, 0.02),
                    surface_point(sx * 0.11, anat.neck - 0.02, True, 0.012), surface_point(sx * 0.11, anat.neck - 0.02, False, 0.012),
                    surface_point(sx * 0.07, anat.chest - 0.02, False, 0.02), surface_point(sx * 0.03, anat.belt + 0.06, False, 0.02)]
            st = tube_along(smooth_loop(path, 1) if False else path, 0.008, f"strap{sx}", "bottom", closed=False, sides=6, flat=0.35)
            straps.append(st)
        st = join_into(straps[0], straps[1:])
        transfer_skin(st, parts.get("Top_tee") or ob, arm, base)
        btns = []
        for sx in (-1, 1):
            btns.append(ellipsoid(f"btn{sx}", tuple(surface_point(sx * 0.09, anat.chest - 0.005, True, 0.026)), (0.009, 0.004, 0.009), "metal", 10, 6))
        b = join_into(btns[0], btns[1:])
        transfer_skin(b, ob, arm, base)
        parts["Overalls"] = join_into(ob, [st, b])
        parts["Overalls"].name = "Overalls"
    if want("Apron"):
        rows, cols = 10, 9
        z0, z1 = anat.knee + 0.12, anat.chest - 0.03
        verts, faces = [], []
        for r in range(rows):
            z = z0 + (z1 - z0) * r / (rows - 1)
            w = 0.19 if z < anat.belt else 0.13
            for c in range(cols):
                x = -w + 2 * w * c / (cols - 1)
                p = surface_point(x * 0.85, max(z, anat.belt - 0.1), True, 0.026)
                p.x = x
                p.z = z
                verts.append(p)
        for r in range(rows - 1):
            for c in range(cols - 1):
                a = r * cols + c
                faces.append((a, a + 1, a + cols + 1, a + cols))
        ap = make_mesh("Apron", verts, faces)
        solidify_apply(ap, 0.004)
        ap.data.materials.append(get_mat("apron"))
        smart_uv(ap, 0.4)
        tie = tube_along([surface_point(x, anat.belt + 0.01, True, 0.03) for x in np.linspace(-0.16, 0.16, 7)], 0.006, "tie", "apron", closed=False)
        neck = tube_along([surface_point(-0.12, z1, True, 0.027), surface_point(-0.08, anat.neck - 0.01, True, 0.015), surface_point(0.08, anat.neck - 0.01, True, 0.015), surface_point(0.12, z1, True, 0.027)], 0.005, "nk", "apron", closed=False)
        ap = join_into(ap, [tie, neck])
        vg = ap.vertex_groups.new(name="spine_01")
        vg.add(list(range(len(ap.data.vertices))), 1.0, "REPLACE")
        attach(ap, arm)
        parts["Apron"] = ap
    if want("Backpack"):
        sp = base.joint("joint-spine-2")
        c = (0, sp[1] - 0.17, sp[2] + 0.02)
        bag = mb_rounded_box("Backpack", c, (0.3, 0.13, 0.4), "bag", 0.05)
        pocket = mb_rounded_box("bp_pocket", (0, c[1] - 0.075, c[2] - 0.08), (0.22, 0.05, 0.16), "bag", 0.025)
        flap = mb_rounded_box("bp_flap", (0, c[1] - 0.01, c[2] + 0.19), (0.29, 0.14, 0.05), "bag", 0.02)
        straps = []
        for sx in (-1, 1):
            path = [Vector((sx * 0.09, c[1] + 0.05, c[2] + 0.17)), surface_point(sx * 0.1, anat.neck - 0.02, True, 0.02) * 0 + Vector((sx * 0.1, sp[1] + 0.0, anat.neck - 0.0)),
                    surface_point(sx * 0.11, anat.chest + 0.04, True, 0.03), surface_point(sx * 0.12, anat.chest - 0.08, True, 0.03),
                    Vector((sx * 0.14, sp[1] - 0.05, anat.chest - 0.14)), Vector((sx * 0.12, c[1] + 0.05, c[2] - 0.17))]
            straps.append(tube_along(path, 0.012, f"bstrap{sx}", "strap", closed=False, sides=6, flat=0.3))
        bag = join_into(bag, [pocket, flap] + straps)
        vg = bag.vertex_groups.new(name="spine_03")
        vg.add(list(range(len(bag.data.vertices))), 1.0, "REPLACE")
        attach(bag, arm)
        smart_uv(bag, 0.4)
        parts["Backpack"] = bag
    parts.update(hair_styles(base, arm, anat, want))
    return parts


def build(sex, only=None):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    base = Base(sex)
    arm = build_armature(base)
    body, used = submesh(base, base.faces_of("body"), "Body")
    add_shape_keys(body, base, used)
    skin(body, arm, base, used)
    body.data.materials.append(get_mat("skin"))
    anat = Anatomy(base)
    parts = {"Body": body}
    parts.update(wardrobe(base, arm, anat, only))
    return base, arm, parts


def preview(path, objs, cam_loc=(1.6, 3.4, 1.5), target=(0, 0, 0.95), lens=55):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 24
    sc.render.resolution_x = 600
    sc.render.resolution_y = 800
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.6, 0.65, 0.72, 1)
    sc.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(45), 0, math.radians(30))
    sc.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    cam.data.lens = lens
    sc.camera = cam
    cam.location = cam_loc
    cam.rotation_euler = (Vector(target) - Vector(cam_loc)).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def export(sex, parts, arm):
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    for o in parts.values():
        o.select_set(True)
    path = os.path.join(OUT, f"human_{sex}.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=False,
                              export_skins=True, export_morph=True, export_morph_normal=False,
                              export_animations=True, export_animation_mode="NLA_TRACKS",
                              export_yup=True, export_materials="EXPORT", export_vertex_color="ACTIVE",
                              export_all_vertex_colors=False)
    return path


def main():
    sexes = [a for a in sys.argv[1:] if not a.startswith("--")] or ["m", "f"]
    for sex in sexes:
        only = None
        for a in sys.argv:
            if a.startswith("--only="):
                only = a[7:].split(",")
        base, arm, parts = build(sex, only)
        if "--skin" in sys.argv and sex == "m":
            skin_textures(base, Anatomy(base))
            print("texturas de pele geradas", flush=True)
        if "--export" in sys.argv:
            if sex == "f":
                for k in ("Beard", "Mustache"):
                    if k in parts:
                        bpy.data.objects.remove(parts.pop(k), do_unlink=True)
            for o in parts.values():
                if not o.data.uv_layers:
                    smart_uv(o, 0.3)   # o Godot precisa de UV para gerar tangentes
            bake_animations(arm)
            tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in parts.values())
            print(sex, "peças:", len(parts), "triângulos:", tris, flush=True)
            print("exportado", export(sex, parts, arm), flush=True)
        if "--pose" in sys.argv:
            apply_pose(arm, stance())
            bpy.context.view_layer.update()
        for a in sys.argv:
            if a.startswith("--anim="):
                nm, fr = a[7:].split(":")
                global base_pelvis_head
                base_pelvis_head = tuple(arm.data.bones["pelvis"].head_local)
                apply_pose(arm, ANIMS[nm][0](float(fr)))
                bpy.context.view_layer.update()
        if "--preview" in sys.argv:
            d = os.environ.get("PREVIEW_DIR", "/tmp")
            cl = os.environ.get("CAM")
            if cl:
                preview(os.path.join(d, f"char_{sex}.png"), parts, cam_loc=tuple(float(x) for x in cl.split(",")))
            else:
                preview(os.path.join(d, f"char_{sex}.png"), parts)
            eye = base.joint("joint-l-eye")
            cam = bpy.context.scene.camera
            cam.location = (0.25, eye[1] + 0.55, eye[2] + 0.05)
            cam.rotation_euler = (Vector((0, eye[1], eye[2] - 0.03)) - cam.location).to_track_quat("-Z", "Y").to_euler()
            bpy.context.scene.render.filepath = os.path.join(d, f"char_{sex}_head.png")
            bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()
