"""Exporta os carros e personagens de assets/ para o Unreal (unreal/ArtSource/).

Uso: python tools/art/blender/export_unreal.py [vehicles] [characters] [textures] [hatch sedan ...] [m f]
(precisa do módulo bpy; ver tools/art/README.md). Sem argumentos, exporta tudo.

Lê os .glb que o jogo Godot usa (assets/vehicles/car_*.glb, assets/characters/human_*.glb)
e grava um .glb por peça, que o unreal/Scripts/build_content.py importa no editor:

  unreal/ArtSource/vehicles/<modelo>/<Parte>.glb   uma malha por parte, pivô na origem
  unreal/ArtSource/characters/<m|f>/Body.glb       esqueleto + corpo + todas as animações
  unreal/ArtSource/characters/<m|f>/<Peça>.glb     cada roupa/cabelo, mesmo esqueleto, sem animação
  unreal/Content/Data/art/vehicles_layout.json     pivôs das partes e medidas (espaço do Unreal)
  unreal/ArtSource/textures/leaf.png               cartão de folhas do M_Leaf (Nature.leaf_texture)

Conversão de eixos
------------------
Blender (dos .glb do jogo): metros, Z para cima, mão direita, frente = +Y, direita = +X.
Unreal: centímetros, Z para cima, mão esquerda, frente = +X, direita = +Y.

O importador glTF do Unreal (Interchange) faz UE = (gx, gz, gy) * 100 e o exportador glTF do
Blender (export_yup) faz g = (bx, bz, -by). Logo, antes de exportar giramos tudo -90° em Z
no Blender, b' = (by, -bx, bz), e no Unreal chega UE = (b'x, -b'y, b'z) = (by, bx, bz) * 100:
frente (+Y do Blender) vira +X, direita (+X) vira +Y, cima continua +Z. A rotação é própria
(sem espelhar), então normais, ordem dos triângulos e esqueleto continuam válidos; a troca de
mão fica por conta do importador. O JSON de layout usa a mesma fórmula (by, bx, bz) * 100.

Peças dos carros: cada malha é gravada com o vértice relativo ao seu pivô (nó de origem),
e a posição do pivô vai para o layout. Porta do motorista: malha door_FL_mesh, pivô no nó
pai Door_FL (dobradiça). Rodas: malhas Wheel_XX_spin, pivô no nó pai Wheel_XX (centro do cubo).

Personagens: seguem a convenção de malha esquelética do Unreal (a do manequim): frente +Y,
direita -X, raiz nos pés. Para isso o giro no Blender é de 180° em Z, b' = (-bx, -by, bz), e no
Unreal chega UE = (-bx, by, bz) * 100. O componente usa yaw -90 para olhar para +X do ator
(UVilaHumanoid::MeshYawOffset).
"""
import json
import math
import os
import sys

import bpy  # noqa: I001 (bpy precisa vir antes de mathutils/bmesh)
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SRC_VEH = os.path.join(ROOT, "assets", "vehicles")
SRC_CHAR = os.path.join(ROOT, "assets", "characters")
OUT = os.path.join(ROOT, "unreal", "ArtSource")
LAYOUT = os.path.join(ROOT, "unreal", "Content", "Data", "art", "vehicles_layout.json")

MODELS = ["hatch", "sedan", "pickup", "van"]
SEXES = ["m", "f"]
ANIMS = ["idle", "walk", "run", "talk", "work", "eat", "search", "sit", "lie", "knocked"]
SHAPES = ["heavy", "thin", "old", "face_a", "face_b"]
# Medidas nominais do CarVisual.gd (o Godot escala o modelo por tamanho_dos_dados / MODEL_LENGTH).
MODEL_LENGTH = {"hatch": 3.9, "sedan": 4.4, "pickup": 4.9, "van": 4.8}
WHEEL_RADIUS = {"hatch": 0.3, "sedan": 0.31, "pickup": 0.37, "van": 0.33}

# Carros: Blender (frente +Y) -> Blender girado (frente +X), que o Unreal lê como frente +X.
R = Matrix.Rotation(-math.pi / 2, 4, "Z")
# Personagens: giro de 180° -> o Unreal lê a frente como +Y (convenção do manequim).
R_CHAR = Matrix.Rotation(math.pi, 4, "Z")
CM = 100.0


def ue_vec(b):
    """Ponto do Blender original (metros) -> Unreal (cm): (by, bx, bz) * 100."""
    return [round(b[1] * CM, 3), round(b[0] * CM, 3), round(b[2] * CM, 3)]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_glb(path):
    bpy.ops.import_scene.gltf(filepath=path, bone_heuristic="BLENDER")
    # o importador cria uma esfera para o formato dos ossos; não faz parte do modelo
    for ob in list(bpy.data.objects):
        if ob.type == "MESH" and ob.name.startswith("Icosphere") and not ob.data.materials and ob.parent is None:
            bpy.data.objects.remove(ob, do_unlink=True)


def select_only(objs):
    bpy.context.view_layer.update()
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def set_active_color(me):
    if me.color_attributes:
        name = me.color_attributes[0].name
        me.color_attributes.active_color_name = name
        me.color_attributes.render_color_index = 0


def export_glb(path, objs, animations=False, skins=False, morph=False, colors=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    select_only(objs)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLB", use_selection=True, export_yup=True,
        export_apply=False, export_materials="EXPORT", export_texcoords=True, export_normals=True,
        export_vertex_color="ACTIVE" if colors else "NONE", export_all_vertex_colors=False,
        export_skins=skins, export_def_bones=False, export_rest_position_armature=True,
        export_morph=morph, export_morph_normal=morph,
        export_animations=animations, export_animation_mode="ACTIONS", export_force_sampling=True,
        export_reset_pose_bones=True, export_cameras=False, export_lights=False)
    return os.path.getsize(path)


# ----------------------------------------------------------------------------- carros
def vehicle_parts():
    """[(parte, objeto de malha, pivô em mundo)] conforme a hierarquia do .glb."""
    out = []
    for ob in bpy.context.scene.objects:
        if ob.type != "MESH":
            continue
        if ob.name == "door_FL_mesh":
            part, pivot = "Door_FL", ob.parent
        elif ob.name.endswith("_spin"):
            part, pivot = ob.name[:-5], ob.parent
        else:
            part, pivot = ob.name, ob
        if pivot is None:
            pivot = ob
        # pivô = o próprio objeto: rotação/escala dele ficam embutidas nos vértices
        pw = pivot.matrix_world
        rot = pw.to_quaternion()
        if pivot is not ob and abs(rot.angle) > 1e-4:
            print(f"  AVISO: pivô {pivot.name} tem rotação {rot.angle:.4f} rad; ignorada (layout usa só posição)")
        out.append((part, ob, pw.to_translation()))
    return out


def export_vehicle(model, layout):
    src = os.path.join(SRC_VEH, f"car_{model}.glb")
    reset()
    import_glb(src)
    parts = vehicle_parts()
    entry = {"parts": {}, "slots": {}}
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    total = 0
    exported = []
    for part, ob, pivot in parts:
        # vértices relativos ao pivô, já no espaço "frente +X"
        T = R @ Matrix.Translation(-pivot) @ ob.matrix_world
        me = ob.data.copy()
        me.name = part
        me.transform(T)
        me.update()
        set_active_color(me)
        new = bpy.data.objects.new(part, me)
        bpy.context.scene.collection.objects.link(new)
        exported.append((part, new, pivot))
        # limites em UE: pivô + vértices (o Unreal lê (x', -y', z') do Blender girado)
        co = np.empty(len(me.vertices) * 3, dtype=np.float64)
        me.vertices.foreach_get("co", co)
        co = co.reshape(-1, 3) + np.array(R @ pivot)   # pivô girado + local
        ue = np.stack([co[:, 0], -co[:, 1], co[:, 2]], axis=1) * CM
        lo = Vector(np.minimum(np.array(lo), ue.min(axis=0)))
        hi = Vector(np.maximum(np.array(hi), ue.max(axis=0)))
        entry["parts"][part] = {"location": ue_vec(pivot), "rotation": [0.0, 0.0, 0.0]}
        entry["slots"][part] = [m.name for m in me.materials if m]
    # os originais saem da cena antes de exportar (não entram por engano na seleção)
    for ob in list(bpy.context.scene.objects):
        if ob not in [e[1] for e in exported]:
            bpy.data.objects.remove(ob, do_unlink=True)
    for part, new, _ in exported:
        new.name = part        # o original já saiu: o nome fica sem sufixo .001
        new.data.name = part
        size = export_glb(os.path.join(OUT, "vehicles", model, f"{part}.glb"), [new])
        total += size
    wheel = next((e for e in exported if e[0] == "Wheel_FL"), None)
    if wheel:
        zs = [v.co.z for v in wheel[1].data.vertices]
        entry["wheel_radius"] = round((max(zs) - min(zs)) * 0.5 * CM, 2)
    entry["length"] = round(hi.x - lo.x, 2)
    entry["width"] = round(hi.y - lo.y, 2)
    entry["height"] = round(hi.z - lo.z, 2)
    entry["bounds_min"] = [round(c, 2) for c in lo]
    entry["bounds_max"] = [round(c, 2) for c in hi]
    entry["nominal_length"] = round(MODEL_LENGTH[model] * CM, 2)
    entry["nominal_wheel_radius"] = round(WHEEL_RADIUS[model] * CM, 2)
    layout[model] = entry
    print(f"{model}: {len(exported)} partes, {total / 1e6:.2f} MB, comprimento {entry['length']} cm, "
          f"largura {entry['width']} cm, altura {entry['height']} cm, roda {entry.get('wheel_radius')} cm", flush=True)
    return total


# ----------------------------------------------------------------------------- textura de folhas
def export_leaf_texture():
    """Porta de Nature.leaf_texture(): 4 raminhos de 13-17 folhas elípticas em 128x128 RGBA.
    O gerador aleatório é o do Python (semente 77), não o PCG do Godot: o desenho é
    equivalente, não idêntico pixel a pixel."""
    import random
    from PIL import Image
    size = 128
    img = np.zeros((size, size, 4), dtype=np.float32)
    rng = random.Random(77)
    for cell in range(4):
        ox, oy = (cell % 2) * 64, (cell // 2) * 64
        for _ in range(rng.randint(13, 17)):
            cx = ox + 32.0 + rng.uniform(-21.0, 21.0)
            cy = oy + 32.0 + rng.uniform(-21.0, 21.0)
            ang = rng.random() * math.tau
            rl = rng.uniform(6.0, 9.5)
            rw = rl * rng.uniform(0.42, 0.6)
            shade = rng.uniform(0.78, 1.0)
            for y in range(int(cy - rl), int(cy + rl) + 1):
                for x in range(int(cx - rl), int(cx + rl) + 1):
                    if x < ox or x >= ox + 64 or y < oy or y >= oy + 64:
                        continue
                    dx, dy = x - cx, y - cy
                    u = (dx * math.cos(ang) + dy * math.sin(ang)) / rl
                    v = (-dx * math.sin(ang) + dy * math.cos(ang)) / rw
                    r = u * u + v * v
                    if r <= 1.0:
                        vein = 1.0 - 0.18 * math.exp(-v * v * 40.0)
                        edge = min(max((1.0 - r) * 4.0, 0.0), 1.0)
                        if img[y, x, 3] < edge:
                            img[y, x] = (shade * vein, shade * vein, shade * vein, edge)
    path = os.path.join(OUT, "textures", "leaf.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray((img * 255.0 + 0.5).astype(np.uint8), "RGBA").save(path)
    print("textura:", path)
    return os.path.getsize(path)


# ----------------------------------------------------------------------------- personagens
def bone_world(arm, bone, action, frame):
    """Posição em mundo da cabeça de um osso num quadro de uma ação (para conferir a rotação)."""
    ad = arm.animation_data
    for t in ad.nla_tracks:
        t.mute = True
    ad.action = action
    if hasattr(ad, "action_slot") and action.slots:
        ad.action_slot = action.slots[0]
    bpy.context.scene.frame_set(frame)
    bpy.context.view_layer.update()
    p = arm.matrix_world @ arm.pose.bones[bone].head
    ad.action = None
    for t in ad.nla_tracks:
        t.mute = False
    return p.copy()


def rotate_character(arm, meshes):
    """Gira esqueleto (pose de descanso) e malhas (com shape keys) 180° em Z, sem mexer nas
    animações (que são locais aos ossos)."""
    select_only([arm])
    bpy.ops.object.mode_set(mode="EDIT")
    for eb in arm.data.edit_bones:
        eb.transform(R_CHAR, scale=False, roll=True)
    bpy.ops.object.mode_set(mode="OBJECT")
    for ob in meshes:
        ob.data.transform(R_CHAR, shape_keys=True)
        ob.data.update()


def export_character(sex):
    src = os.path.join(SRC_CHAR, f"human_{sex}.glb")
    reset()
    import_glb(src)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.parent == arm]
    for ob in [arm] + meshes:
        if not all(abs(a - b) < 1e-6 for ra, rb in zip(ob.matrix_world, Matrix.Identity(4)) for a, b in zip(ra, rb)):
            raise RuntimeError(f"{ob.name} tem transformação de objeto; o exportador espera identidade")
    actions = {a.name: a for a in bpy.data.actions}
    missing = [a for a in ANIMS if a not in actions]
    if missing:
        print(f"  AVISO: animações ausentes em human_{sex}.glb: {missing}")
    # referência para conferir a rotação: mão esquerda no quadro 10 do walk
    probe = ("hand_l", "walk", 10) if "hand_l" in arm.pose.bones and "walk" in actions else None
    before = bone_world(arm, probe[0], actions[probe[1]], probe[2]) if probe else None
    rotate_character(arm, meshes)
    if probe:
        after = bone_world(arm, probe[0], actions[probe[1]], probe[2])
        expect = R_CHAR @ before
        err = (after - expect).length
        print(f"  conferência da rotação ({probe[0]} em {probe[1]}:{probe[2]}): erro {err * 1000:.3f} mm")
        if err > 1e-3:
            raise RuntimeError("a rotação do esqueleto alterou a animação")
    # pose de descanso e shape keys zeradas para exportar (o Blender 5 importa o peso padrão)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    for ob in meshes:
        if ob.data.shape_keys:
            for kb in ob.data.shape_keys.key_blocks[1:]:
                kb.value = 0.0
    out_dir = os.path.join(OUT, "characters", sex)
    total = 0
    pieces = {}
    body = next((o for o in meshes if o.name == "Body"), None)
    if body is None:
        raise RuntimeError(f"human_{sex}.glb sem a malha Body")
    # Body: com todas as animações (o NLA importado referencia cada ação)
    size = export_glb(os.path.join(out_dir, "Body.glb"), [arm, body], animations=True, skins=True, morph=True, colors=False)
    total += size
    pieces["Body"] = size
    # demais peças: sem animação
    ad = arm.animation_data
    saved = []
    if ad:
        saved = [(t, t.mute) for t in ad.nla_tracks]
        for t in ad.nla_tracks:
            t.mute = True
        ad.action = None
    for ob in meshes:
        if ob is body:
            continue
        size = export_glb(os.path.join(out_dir, f"{ob.name}.glb"), [arm, ob], animations=False, skins=True,
                          morph=bool(ob.data.shape_keys), colors=False)
        total += size
        pieces[ob.name] = size
    for t, m in saved:
        t.mute = m
    print(f"{sex}: {len(pieces)} peças, {total / 1e6:.2f} MB ({', '.join(sorted(pieces))})", flush=True)
    return total


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    do_veh = not args or "vehicles" in args or any(a in MODELS for a in args)
    do_char = not args or "characters" in args or any(a in SEXES for a in args)
    do_tex = not args or "textures" in args
    models = [a for a in args if a in MODELS] or MODELS
    sexes = [a for a in args if a in SEXES] or SEXES
    total = 0
    if do_veh:
        layout = {}
        if os.path.exists(LAYOUT):
            with open(LAYOUT, encoding="utf-8") as f:
                layout = json.load(f)
        for m in models:
            total += export_vehicle(m, layout)
        os.makedirs(os.path.dirname(LAYOUT), exist_ok=True)
        with open(LAYOUT, "w", encoding="utf-8") as f:
            json.dump({k: layout[k] for k in MODELS if k in layout}, f, indent=1, ensure_ascii=False)
            f.write("\n")
        print("layout:", LAYOUT)
    if do_tex:
        total += export_leaf_texture()
    if do_char:
        for s in sexes:
            total += export_character(s)
    print(f"total exportado: {total / 1e6:.2f} MB em {OUT}")


if __name__ == "__main__":
    main()
