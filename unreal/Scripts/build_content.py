"""Gera o conteúdo /Game/Vila do Vila Aurora dentro do Unreal Editor (UE 5.5, Python).

Uso (ver Scripts/README.md):
  Editor: Tools > Execute Python Script... > Scripts/build_content.py
  Linha de comando:
    UnrealEditor-Cmd VilaAurora.uproject -run=pythonscript -script="<caminho>/Scripts/build_content.py [--force] [--only=etapa,...]"
  Etapas: textures, mpc, materials, surfaces, vehicles, characters, level (padrão: todas, nessa ordem).
  --force refaz o que já existe (materiais e instâncias são reaproveitados e reescritos, para não
  quebrar referências; malhas e personagens são reimportados).

Idempotente: sem --force, cada asset que já existe é pulado.

Entradas (relativas ao repositório):
  assets/textures/<set>_{albedo,normal,orm}.png + textures.json (tile_m)
  assets/characters/{skin_detail,skin_mask,eye}.png
  unreal/ArtSource/** (gerado por tools/art/blender/export_unreal.py)
Saída: o contrato descrito em Source/VilaAurora/Public/VilaAssets.h.
"""
import json
import os
import re
import sys

import unreal

# ============================================================================ contrato
# Parâmetros de cada material-mestre. Os nomes são os que UVilaAssets (VilaAssets.cpp) e os
# atores usam; não renomeie sem mudar o C++. s = escalar, v = vetor (RGBA linear), t = textura.
MATERIAL_PARAMS = {
    "M_Surface": {  # projeção em caixa no espaço do mundo (Mat.SURFACE_SHADER)
        "t": {"Albedo": "sRGB", "Normal": "normal (verde invertido na importação)", "ORM": "linear (R=AO, G=rugosidade, B=metal)"},
        "s": {"TileMeters": 1.0, "Macro": 0.1, "Detile": 0.0, "SwapTop": 0.0, "Outdoor": 1.0,
              "NormalStrength": 1.0, "RoughBias": 0.0, "UseCustomTint": 0.0},
        "v": {"Tint": (1, 1, 1, 1)},
        "notes": "UseCustomTint=1 multiplica pela PerInstanceCustomData 0..2. Molhado = MPC_Vila.Wetness * Outdoor * (topo).",
    },
    "M_SurfaceLocal": {  # triplanar no espaço do objeto (Mat.textured)
        "t": {"Albedo": "sRGB", "Normal": "normal", "ORM": "linear"},
        "s": {"TileMeters": 1.0, "Scale": 1.0, "Roughness": 1.0, "Metallic": 0.0},
        "v": {"Tint": (1, 1, 1, 1)},
        "notes": "Repetição = TileMeters * Scale metros. Roughness/Metallic multiplicam o ORM (como no ORMMaterial3D do Godot).",
    },
    "M_Color": {"s": {"Roughness": 0.85, "Metallic": 0.0, "UseCustomTint": 0.0, "UseVertexColor": 0.0},
                "v": {"Color": (0.5, 0.5, 0.5, 1)}},
    "M_Emissive": {"s": {"Intensity": 1.5}, "v": {"Color": (1, 1, 1, 1)}},
    "M_Glass": {"s": {"Opacity": 0.45, "Roughness": 0.08, "Metallic": 0.2}, "v": {"Tint": (0.55, 0.72, 0.85, 1)}},
    "M_CarPaint": {"s": {"Dirt": 0.0, "Metallic": 0.35, "Roughness": 0.3, "ClearCoat": 0.8, "ClearCoatRoughness": 0.12},
                   "v": {"Color": (0.6, 0.6, 0.62, 1), "DirtColor": (0.16, 0.12, 0.08, 1)},
                   "notes": "Cor de vértice (AO assada) multiplica a base."},
    "M_Skin": {"t": {"Detail": "sRGB (skin_detail)", "Mask": "linear sem mip, filtro nearest (skin_mask)"},
               "s": {"HideBits": 0.0, "Roughness": 0.6, "Specular": 0.3},
               "v": {"SkinTone": (0.69, 0.4, 0.25, 1), "HairColor": (0.023, 0.012, 0.007, 1)},
               "notes": "Região = round(Mask.R*16); some o pixel se o bit da região estiver em HideBits (Humanoid.HIDE_BITS)."},
    "M_Cloth": {"t": {"Albedo": "sRGB", "Normal": "normal", "ORM": "linear"},
                "s": {"UVScale": 2.0, "UseTexture": 1.0, "UseORM": 0.0, "NormalStrength": 0.8, "Roughness": 0.9, "Metallic": 0.0},
                "v": {"Color": (1, 1, 1, 1)}},
    "M_Hair": {"t": {"Albedo": "sRGB", "Normal": "normal"}, "s": {"UVScale": 1.0, "Roughness": 0.55},
               "v": {"Color": (0.023, 0.012, 0.007, 1)}},
    "M_Eye": {"t": {"Albedo": "sRGB (eye.png)"}, "s": {"Roughness": 0.15}, "v": {"Color": (1, 1, 1, 1)}},
    "M_Leaf": {"t": {"LeafTex": "sRGB + alfa (ArtSource/textures/leaf.png)"}, "s": {"WindStrength": 0.07},
               "v": {"Color": (1, 1, 1, 1)},
               "notes": "Vento = MPC Wind; afinamento da copa entre câmera e MPC Focus (w=1 liga)."},
    "M_Grass": {"s": {"WindStrength": 0.18, "FadeStart": 6200.0, "FadeEnd": 7000.0},
                "v": {"RootColor": (0.0068, 0.0116, 0.0035, 1), "TipColor": (0.06, 0.095, 0.018, 1), "Color": (1, 1, 1, 1)},
                "notes": "UV0.y = 0 na raiz e 1 na ponta (como no Godot)."},
    "M_Water": {"s": {"Opacity": 0.85, "Roughness": 0.05, "Metallic": 0.3, "Ripple": 0.15}, "v": {"Color": (0.022, 0.055, 0.073, 1)}},
    "M_Windows": {"s": {"Intensity": 1.4, "NightDriven": 1.0, "Roughness": 0.1, "Metallic": 0.3},
                  "v": {"Color": (0.15, 0.27, 0.39, 1), "EmissiveColor": (1.0, 0.57, 0.17, 1)},
                  "notes": "Emissivo = EmissiveColor * Intensity * (NightDriven ? MPC Night : 1)."},
    "M_Rain": {"s": {"Opacity": 0.35, "Speed": 2.5, "Tiling": 30.0}, "v": {"Color": (0.7, 0.75, 0.85, 1)}},
    "M_Outline": {"s": {"Width": 2.0}, "v": {"OutlineColor": (1.0, 0.75, 0.25, 1)},
                  "notes": "Pós-processo: contorno onde CustomStencil == 1 (r.CustomDepth=3 no DefaultEngine.ini)."},
}
# MPC_Vila: escalares globais (UVilaAssets::SetGlobal) + vetor Focus (opcional, M_Leaf).
MPC_SCALARS = {"Wetness": 0.0, "Night": 0.0, "Wind": 0.5, "Snow": 0.0}
MPC_VECTORS = {"Focus": (0.0, 0.0, -100000.0, 0.0)}

ANIMS = ["idle", "walk", "run", "talk", "work", "eat", "search", "sit", "lie", "knocked"]
MODELS = ["hatch", "sedan", "pickup", "van"]
SEXES = ["m", "f"]

MAT = "/Game/Vila/Materials"
TEX = "/Game/Vila/Textures"
TMP = "/Game/Vila/_Import"

# ============================================================================ ambiente
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def _project_dir():
    try:
        return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    except Exception:
        return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


PROJECT = os.path.normpath(_project_dir())
REPO = os.path.normpath(os.path.join(PROJECT, ".."))
ASSETS = os.path.join(REPO, "assets")
ART = os.path.join(PROJECT, "ArtSource")
DATA = os.path.join(PROJECT, "Content", "Data")


def _args():
    a = list(sys.argv[1:])
    extra = os.environ.get("VILA_BUILD_ARGS", "")
    if extra:
        a += extra.split()
    return a


ARGS = _args()
FORCE = "--force" in ARGS
ONLY = None
for _a in ARGS:
    if _a.startswith("--only="):
        ONLY = set(x.strip() for x in _a[len("--only="):].split(",") if x.strip())

STATS = {"criados": [], "pulados": [], "avisos": [], "erros": []}


def log(msg):
    unreal.log("[Vila] " + msg)


def warn(msg):
    STATS["avisos"].append(msg)
    unreal.log_warning("[Vila] " + msg)


def error(msg):
    STATS["erros"].append(msg)
    unreal.log_error("[Vila] " + msg)


def step(name):
    return ONLY is None or name in ONLY


def srgb(c):
    """Cor do Godot (Color(), sRGB 0..1) -> linear, como os uniformes source_color."""
    def f(x):
        return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
    return tuple(f(x) for x in c[:3]) + ((c[3],) if len(c) > 3 else (1.0,))


def lc(c):
    c = tuple(c) + (1.0,) * (4 - len(c))
    return unreal.LinearColor(c[0], c[1], c[2], c[3])


def setp(obj, name, value, quiet=False):
    """set_editor_property tolerante (nomes de propriedade variam entre versões)."""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        if not quiet:
            warn(f"{obj.get_name()}: não consegui definir '{name}': {e}")
        return False


def exists(path):
    return EAL.does_asset_exist(path)


def load(path):
    return EAL.load_asset(path) if exists(path) else None


def save(asset):
    if asset:
        EAL.save_loaded_asset(asset, only_if_is_dirty=False)


def split(path):
    d, n = path.rsplit("/", 1)
    return d, n


def create(path, cls, factory):
    d, n = split(path)
    if not EAL.does_directory_exist(d):
        EAL.make_directory(d)
    return AT.create_asset(n, d, cls, factory)


def enum(owner, *names):
    """Primeiro membro existente de um enum do unreal (os nomes mudaram entre versões)."""
    for n in names:
        if hasattr(owner, n):
            return getattr(owner, n)
    raise AttributeError(f"{owner}: nenhum de {names}")


# ============================================================================ importação
def import_file(filename, dest_dir, dest_name=None, options=None, replace=True):
    """Importa um arquivo com AssetImportTask (vai pelo Interchange no 5.5) e devolve a lista
    de caminhos de assets criados."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", dest_dir)
    if dest_name:
        task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace)
    task.set_editor_property("save", False)
    if options is not None:
        task.set_editor_property("options", options)
    AT.import_asset_tasks([task])
    paths = [str(p) for p in (task.get_editor_property("imported_object_paths") or [])]
    if EAL.does_directory_exist(dest_dir):
        for p in EAL.list_assets(dest_dir, recursive=True, include_folder=False):
            p = str(p)
            if p not in paths:
                paths.append(p)
    return paths


def pkg(path):
    """'/Game/a/B.B' -> '/Game/a/B'."""
    p = str(path)
    if "." in p.rsplit("/", 1)[-1]:
        p = p.rsplit(".", 1)[0]
    return p


def move(src, dst):
    src, dst = pkg(src), pkg(dst)
    if src == dst:
        return True
    if exists(dst):
        EAL.delete_asset(dst)
    d, _ = split(dst)
    if not EAL.does_directory_exist(d):
        EAL.make_directory(d)
    ok = EAL.rename_asset(src, dst)
    if not ok:
        error(f"falha ao mover {src} -> {dst}")
    return ok


def clean_tmp(path=TMP):
    if EAL.does_directory_exist(path):
        EAL.delete_directory(path)


def interchange_pipeline(skeletal=False, skeleton=None, animations=False):
    """Pipeline genérico do Interchange para AssetImportTask.options. Propriedades ajustadas
    com tolerância: se algum nome não existir na versão, o padrão do motor vale."""
    try:
        p = unreal.InterchangeGenericAssetsPipeline()
    except Exception as e:
        warn(f"InterchangeGenericAssetsPipeline indisponível ({e}); usando a importação padrão")
        return None
    try:
        common = p.get_editor_property("common_meshes_properties")
        skel = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
        mesh = p.get_editor_property("mesh_pipeline")
        anim = p.get_editor_property("animation_pipeline")
    except Exception as e:
        warn(f"pipeline do Interchange com outra estrutura ({e}); usando os padrões dele")
        return p
    if skeletal:
        setp(common, "force_all_mesh_as_type", enum(unreal.InterchangeForceMeshType, "IFMT_SKELETAL_MESH", "IFMT_SkeletalMesh"), True)
        setp(mesh, "import_skeletal_meshes", True, True)
        setp(mesh, "import_morph_targets", True, True)
        setp(mesh, "create_physics_asset", False, True)
        setp(skel, "import_only_animations", False, True)
        if skeleton is not None:
            setp(skel, "skeleton", skeleton)
        setp(anim, "import_animations", animations, True)
    else:
        setp(common, "force_all_mesh_as_type", enum(unreal.InterchangeForceMeshType, "IFMT_STATIC_MESH", "IFMT_StaticMesh"), True)
        setp(mesh, "import_static_meshes", True, True)
        setp(mesh, "combine_static_meshes", True, True)
        setp(mesh, "build_nanite", False, True)
        setp(anim, "import_animations", False, True)
    return p


# ============================================================================ texturas
def tex_meta():
    path = os.path.join(ASSETS, "textures", "textures.json")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def configure_texture(t, kind):
    if kind == "A":
        setp(t, "srgb", True)
        setp(t, "compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif kind == "N":
        setp(t, "srgb", False)
        setp(t, "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        setp(t, "lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP, True)
        # normais do Godot são estilo OpenGL (+Y); o Unreal usa DirectX (-Y)
        setp(t, "flip_green_channel", True)
    elif kind == "ORM":
        setp(t, "srgb", False)
        setp(t, "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    elif kind == "MASK":
        # códigos de região no R: sem compressão, sem mip, amostragem pontual
        setp(t, "srgb", False)
        setp(t, "compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        setp(t, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        setp(t, "filter", unreal.TextureFilter.TF_NEAREST)


def import_texture(filename, name, kind):
    path = f"{TEX}/{name}"
    if exists(path) and not FORCE:
        STATS["pulados"].append(path)
        return load(path)
    if not os.path.exists(filename):
        error(f"arquivo ausente: {filename}")
        return None
    paths = import_file(filename, TEX, name)
    if not exists(path):
        # o Interchange ignorou destination_name: procura o recém-importado e renomeia
        base = os.path.splitext(os.path.basename(filename))[0]
        cand = [p for p in paths if split(pkg(p))[1] == base]
        if cand:
            move(cand[0], path)
    t = load(path)
    if not t:
        error(f"textura não importada: {path}")
        return None
    configure_texture(t, kind)
    save(t)
    STATS["criados"].append(path)
    return t


def build_textures():
    log("texturas...")
    for s in sorted(tex_meta()):
        for suffix, kind, src in (("A", "A", "albedo"), ("N", "N", "normal"), ("ORM", "ORM", "orm")):
            import_texture(os.path.join(ASSETS, "textures", f"{s}_{src}.png"), f"T_{s}_{suffix}", kind)
    chars = os.path.join(ASSETS, "characters")
    import_texture(os.path.join(chars, "skin_detail.png"), "T_skin_detail", "A")
    import_texture(os.path.join(chars, "skin_mask.png"), "T_skin_mask", "MASK")
    import_texture(os.path.join(chars, "eye.png"), "T_eye", "A")
    import_texture(os.path.join(ART, "textures", "leaf.png"), "T_leaf", "A")


def T(name):
    return load(f"{TEX}/{name}")


# ============================================================================ MPC
def build_mpc():
    path = f"{MAT}/MPC_Vila"
    mpc = load(path)
    if mpc and not FORCE:
        STATS["pulados"].append(path)
        return mpc
    if not mpc:
        mpc = create(path, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    scalars = []
    for n, v in MPC_SCALARS.items():
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        scalars.append(p)
    vectors = []
    for n, v in MPC_VECTORS.items():
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", lc(v))
        vectors.append(p)
    # só reescreve se faltar algum parâmetro (trocar a lista recria os Ids e quebra os materiais)
    have = set(str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters"))
    have |= set(str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters"))
    if not set(MPC_SCALARS) | set(MPC_VECTORS) <= have:
        mpc.set_editor_property("scalar_parameters", scalars)
        mpc.set_editor_property("vector_parameters", vectors)
    save(mpc)
    STATS["criados"].append(path)
    return mpc


# ============================================================================ grafo de material
SAMPLER = unreal.MaterialSamplerType
OUT_T = unreal.CustomMaterialOutputType
HLSL_T = {OUT_T.CMOT_FLOAT1: "float", OUT_T.CMOT_FLOAT2: "float2", OUT_T.CMOT_FLOAT3: "float3", OUT_T.CMOT_FLOAT4: "float4"}
MP = unreal.MaterialProperty

# Ruído do Mat.gd (NOISE_FN), como struct local (o nó Custom não aceita funções soltas).
NOISE = """
struct VilaNoise {
	float h(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
	float n(float2 p) {
		float2 i = floor(p); float2 f = frac(p);
		float2 u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(h(i), h(i + float2(1.0, 0.0)), u.x), lerp(h(i + float2(0.0, 1.0)), h(i + float2(1.0, 1.0)), u.x), u.y);
	}
	float3 dn(float2 g) { float2 xy = g * 2.0 - 1.0; return float3(xy, sqrt(saturate(1.0 - dot(xy, xy)))); }
};
VilaNoise VN;
"""


def sampler_for(tex):
    """Mesmo critério de UMaterialExpressionTextureBase::GetSamplerTypeForTexture."""
    cs = tex.get_editor_property("compression_settings")
    if cs == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return SAMPLER.SAMPLERTYPE_NORMAL
    if cs == unreal.TextureCompressionSettings.TC_MASKS:
        return SAMPLER.SAMPLERTYPE_MASKS
    if cs == unreal.TextureCompressionSettings.TC_GRAYSCALE:
        return SAMPLER.SAMPLERTYPE_GRAYSCALE if tex.get_editor_property("srgb") else SAMPLER.SAMPLERTYPE_LINEAR_GRAYSCALE
    return SAMPLER.SAMPLERTYPE_COLOR if tex.get_editor_property("srgb") else SAMPLER.SAMPLERTYPE_LINEAR_COLOR


class Custom:
    """Nó Custom com várias saídas. Tenta um nó só (AdditionalOutputs); se o editor não
    ligar alguma saída pelo nome, cria um nó de saída única para ela (mesmo código)."""

    def __init__(self, g, body, inputs, outputs, desc):
        self.g, self.body, self.inputs, self.outputs, self.desc = g, body, inputs, outputs, desc
        self.main = outputs[0][0]
        self.single = {}
        code = NOISE + f"{HLSL_T[outputs[0][1]]} {self.main} = 0;\n" + body + f"\nreturn {self.main};"
        self.node = self._make(code, outputs[0][1], outputs[1:])

    def _make(self, code, out_type, extra):
        n = self.g.node(unreal.MaterialExpressionCustom, x=-500)
        n.set_editor_property("description", self.desc)
        n.set_editor_property("output_type", out_type)
        ins = []
        for name, _src, _out in self.inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        n.set_editor_property("inputs", ins)
        outs = []
        for name, t in extra:
            co = unreal.CustomOutput()
            co.set_editor_property("output_name", name)
            co.set_editor_property("output_type", t)
            outs.append(co)
        if outs:
            n.set_editor_property("additional_outputs", outs)
        n.set_editor_property("code", code)
        for name, src, out in self.inputs:
            if not MEL.connect_material_expressions(src, out, n, name):
                error(f"{self.g.name}/{self.desc}: entrada {name} não conectou")
        return n

    def _single(self, name):
        if name not in self.single:
            t = dict(self.outputs)[name]
            decl = "".join(f"{HLSL_T[ot]} {on} = 0;\n" for on, ot in self.outputs)
            code = NOISE + decl + self.body + f"\nreturn {name};"
            self.single[name] = self._make(code, t, [])
        return self.single[name]

    def _try(self, fn, name):
        names = ["", "return", "Return"] if name == self.main else [name]
        for n in names:
            if fn(self.node, n):
                return True
        return fn(self._single(name), "")

    def to_prop(self, name, prop):
        if not self._try(lambda node, out: MEL.connect_material_property(node, out, prop), name):
            error(f"{self.g.name}: saída {name} -> {prop} não conectou")

    def to(self, name, expr, input_name):
        if not self._try(lambda node, out: MEL.connect_material_expressions(node, out, expr, input_name), name):
            error(f"{self.g.name}: saída {name} -> {input_name} não conectou")


class Graph:
    def __init__(self, mat, name):
        self.m, self.name, self.y = mat, name, 0

    def node(self, cls, x=-1100, **props):
        e = MEL.create_material_expression(self.m, cls, x, self.y)
        self.y += 140
        for k, v in props.items():
            setp(e, k, v)
        return e

    def scalar(self, name, default=None):
        if default is None:
            default = MATERIAL_PARAMS[self.name]["s"][name]
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(default))

    def vector(self, name, default=None):
        if default is None:
            default = MATERIAL_PARAMS[self.name]["v"][name]
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=lc(default))

    def texobj(self, name, tex):
        e = self.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name)
        if tex:
            setp(e, "texture", tex)
            setp(e, "sampler_type", sampler_for(tex))
        else:
            error(f"{self.name}: textura padrão de {name} ausente (rode a etapa textures)")
        return e

    def const(self, v):
        return self.node(unreal.MaterialExpressionConstant, r=float(v))

    def mpc(self, name):
        e = self.node(unreal.MaterialExpressionCollectionParameter)
        setp(e, "collection", load(f"{MAT}/MPC_Vila"))
        setp(e, "parameter_name", name)   # PostEditChange resolve o ParameterId pelo nome
        return e

    def uv(self, index=0):
        return self.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=index)

    def wpos(self):
        return self.node(unreal.MaterialExpressionWorldPosition)

    def vnormal(self):
        return self.node(unreal.MaterialExpressionVertexNormalWS)

    def vcolor(self):
        return self.node(unreal.MaterialExpressionVertexColor)

    def time(self):
        return self.node(unreal.MaterialExpressionTime)

    def campos(self):
        return self.node(unreal.MaterialExpressionCameraPositionWS)

    def custom_data(self, i):
        e = self.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=i)
        setp(e, "const_default_value", 1.0, True)
        return e

    def link(self, a, aout, b, bin_):
        if not MEL.connect_material_expressions(a, aout, b, bin_):
            error(f"{self.name}: ligação {bin_} falhou")

    def out(self, a, prop, aout=""):
        if not MEL.connect_material_property(a, aout, prop):
            error(f"{self.name}: saída para {prop} falhou")

    def custom(self, body, inputs, outputs, desc="Vila"):
        return Custom(self, body, inputs, outputs, desc)


def begin_material(name, blend=None, shading=None, two_sided=False, tangent_normal=True, domain=None, clip=None):
    """Cria (ou limpa, com --force) /Game/Vila/Materials/<name>. None = pular."""
    path = f"{MAT}/{name}"
    mat = load(path)
    if mat and not FORCE:
        STATS["pulados"].append(path)
        return None
    if mat:
        MEL.delete_all_material_expressions(mat)
    else:
        mat = create(path, unreal.Material, unreal.MaterialFactoryNew())
    if domain is not None:
        setp(mat, "material_domain", domain)
    setp(mat, "blend_mode", blend if blend is not None else unreal.BlendMode.BLEND_OPAQUE)
    setp(mat, "shading_model", shading if shading is not None else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    setp(mat, "two_sided", two_sided)
    setp(mat, "tangent_space_normal", tangent_normal)
    if clip is not None:
        setp(mat, "opacity_mask_clip_value", clip)
    if domain is None:
        # usos: malhas estáticas, HISM, esqueléticas com morphs, malhas procedurais
        for flag in ("used_with_skeletal_mesh", "used_with_morph_targets", "used_with_instanced_static_meshes"):
            setp(mat, flag, True, True)
    return Graph(mat, name)


def end_material(g):
    try:
        MEL.layout_material_expressions(g.m)
    except Exception:
        pass
    MEL.recompile_material(g.m)
    save(g.m)
    STATS["criados"].append(f"{MAT}/{g.name}")
    log(f"material {g.name}")


# ============================================================================ mestres
SURFACE_BODY = """
// Porta de Mat.SURFACE_SHADER. Godot (x, y, z) = UE (X, Z, Y) / 100.
float3 p = WPos / 100.0;
float2 gxz = p.xy;
float gy = p.z;
float3 n = normalize(VNrm);
float3 an = abs(n);
float2 uv; float3 tu; float3 tv;
if (an.z >= an.x && an.z >= an.y) {
	if (SwapTop > 0.5) { uv = float2(p.y, -p.x); tu = float3(0, 1, 0); tv = float3(-1, 0, 0); }
	else { uv = p.xy; tu = float3(1, 0, 0); tv = float3(0, 1, 0); }
} else if (an.x >= an.y) {
	float s = n.x >= 0.0 ? 1.0 : -1.0;
	uv = float2(-p.y * s, -p.z); tu = float3(0, -s, 0); tv = float3(0, 0, -1);
} else {
	float s = n.y >= 0.0 ? 1.0 : -1.0;
	uv = float2(p.x * s, -p.z); tu = float3(s, 0, 0); tv = float3(0, 0, -1);
}
uv /= max(TileMeters, 0.0001);
float4 a = Texture2DSample(Albedo, AlbedoSampler, uv);
float2 nm = Texture2DSample(Normal, NormalSampler, uv).rg;
float3 orm = Texture2DSample(ORM, ORMSampler, uv).rgb;
// detile: segunda amostra em outra escala, misturada por ruído (amostrada sempre: sem gradiente divergente)
float2 uv2 = uv * 0.61 + float2(0.37, 0.71);
float k = Detile > 0.5 ? smoothstep(0.35, 0.65, VN.n(gxz * 0.11 + gy * 0.07)) : 0.0;
a = lerp(a, Texture2DSample(Albedo, AlbedoSampler, uv2), k);
nm = lerp(nm, Texture2DSample(Normal, NormalSampler, uv2).rg, k);
orm = lerp(orm, Texture2DSample(ORM, ORMSampler, uv2).rgb, k);
float m = VN.n(gxz * 0.07 + gy * 0.05) * 0.6 + VN.n(gxz * 0.6 + gy * 0.4) * 0.4;
float3 tint = Tint.rgb * lerp(float3(1, 1, 1), float3(CD0, CD1, CD2), UseCustomTint);
float3 col = a.rgb * tint * VCol.rgb * (1.0 + (m - 0.5) * Macro * 2.0);
float up = smoothstep(0.3, 0.9, n.z);
float wet = Wetness * Outdoor * lerp(0.35, 1.0, up);
BaseColorOut = col * (1.0 - wet * 0.3);
RoughOut = clamp(lerp(orm.g + RoughBias, 0.12, wet * 0.85), 0.04, 1.0);
MetalOut = orm.b;
AOOut = orm.r;
// normal em espaço do mundo (textura em convenção DirectX após flip_green na importação)
float2 nxy = (nm * 2.0 - 1.0) * (1.0 - wet * 0.6);
float nz = sqrt(saturate(1.0 - dot(nxy, nxy)));
float3 nw = tu * nxy.x + tv * nxy.y + n * nz;
NormalOut = normalize(lerp(n, nw, NormalStrength));
"""

SURFACE_LOCAL_BODY = """
// Porta de Mat.textured: triplanar no espaço do objeto (não escorrega em peças que se movem).
float3 p = LPos / 100.0 / max(TileMeters * Scale, 0.0001);
float3 n = normalize(LNrm);
float3 w = abs(n); w /= max(w.x + w.y + w.z, 0.0001);
float sx = n.x >= 0.0 ? 1.0 : -1.0;
float sy = n.y >= 0.0 ? 1.0 : -1.0;
float2 uz = p.xy;
float2 ux = float2(-p.y * sx, -p.z);
float2 uy = float2(p.x * sy, -p.z);
float4 a = Texture2DSample(Albedo, AlbedoSampler, uz) * w.z + Texture2DSample(Albedo, AlbedoSampler, ux) * w.x + Texture2DSample(Albedo, AlbedoSampler, uy) * w.y;
float3 orm = Texture2DSample(ORM, ORMSampler, uz).rgb * w.z + Texture2DSample(ORM, ORMSampler, ux).rgb * w.x + Texture2DSample(ORM, ORMSampler, uy).rgb * w.y;
float3 nz = VN.dn(Texture2DSample(Normal, NormalSampler, uz).rg);
float3 nx = VN.dn(Texture2DSample(Normal, NormalSampler, ux).rg);
float3 ny = VN.dn(Texture2DSample(Normal, NormalSampler, uy).rg);
float3 nl = (float3(1, 0, 0) * nz.x + float3(0, 1, 0) * nz.y + n * nz.z) * w.z
          + (float3(0, -sx, 0) * nx.x + float3(0, 0, -1) * nx.y + n * nx.z) * w.x
          + (float3(sy, 0, 0) * ny.x + float3(0, 0, -1) * ny.y + n * ny.z) * w.y;
BaseColorOut = a.rgb * Tint.rgb;
RoughOut = saturate(orm.g * Roughness);
MetalOut = orm.b * Metallic;
AOOut = orm.r;
NormalLocal = normalize(nl);
"""


def m_surface():
    g = begin_material("M_Surface", tangent_normal=False)
    if not g:
        return
    ins = [("Albedo", g.texobj("Albedo", T("T_concrete_A")), ""), ("Normal", g.texobj("Normal", T("T_concrete_N")), ""),
           ("ORM", g.texobj("ORM", T("T_concrete_ORM")), ""), ("WPos", g.wpos(), ""), ("VNrm", g.vnormal(), ""),
           ("VCol", g.vcolor(), ""), ("Tint", g.vector("Tint"), "")]
    for s in ("TileMeters", "Macro", "Detile", "SwapTop", "Outdoor", "NormalStrength", "RoughBias", "UseCustomTint"):
        ins.append((s, g.scalar(s), ""))
    for i in range(3):
        ins.append((f"CD{i}", g.custom_data(i), ""))
    ins.append(("Wetness", g.mpc("Wetness"), ""))
    c = g.custom(SURFACE_BODY, ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("RoughOut", OUT_T.CMOT_FLOAT1), ("MetalOut", OUT_T.CMOT_FLOAT1),
                                     ("AOOut", OUT_T.CMOT_FLOAT1), ("NormalOut", OUT_T.CMOT_FLOAT3)], "VilaSurface")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    c.to_prop("RoughOut", MP.MP_ROUGHNESS)
    c.to_prop("MetalOut", MP.MP_METALLIC)
    c.to_prop("AOOut", MP.MP_AMBIENT_OCCLUSION)
    c.to_prop("NormalOut", MP.MP_NORMAL)
    end_material(g)


def m_surface_local():
    g = begin_material("M_SurfaceLocal", tangent_normal=False)
    if not g:
        return
    lpos = g.node(unreal.MaterialExpressionTransformPosition)
    setp(lpos, "transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    setp(lpos, "transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    g.link(g.wpos(), "", lpos, "Input")
    lnrm = g.node(unreal.MaterialExpressionTransform)
    setp(lnrm, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    setp(lnrm, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    g.link(g.vnormal(), "", lnrm, "Input")
    ins = [("Albedo", g.texobj("Albedo", T("T_wood_A")), ""), ("Normal", g.texobj("Normal", T("T_wood_N")), ""),
           ("ORM", g.texobj("ORM", T("T_wood_ORM")), ""), ("LPos", lpos, ""), ("LNrm", lnrm, ""), ("Tint", g.vector("Tint"), "")]
    for s in ("TileMeters", "Scale", "Roughness", "Metallic"):
        ins.append((s, g.scalar(s), ""))
    c = g.custom(SURFACE_LOCAL_BODY, ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("RoughOut", OUT_T.CMOT_FLOAT1), ("MetalOut", OUT_T.CMOT_FLOAT1),
                                           ("AOOut", OUT_T.CMOT_FLOAT1), ("NormalLocal", OUT_T.CMOT_FLOAT3)], "VilaSurfaceLocal")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    c.to_prop("RoughOut", MP.MP_ROUGHNESS)
    c.to_prop("MetalOut", MP.MP_METALLIC)
    c.to_prop("AOOut", MP.MP_AMBIENT_OCCLUSION)
    to_world = g.node(unreal.MaterialExpressionTransform)
    setp(to_world, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    setp(to_world, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    c.to("NormalLocal", to_world, "Input")
    g.out(to_world, MP.MP_NORMAL)
    end_material(g)


def m_color():
    g = begin_material("M_Color")
    if not g:
        return
    ins = [("Color", g.vector("Color"), ""), ("UseCustomTint", g.scalar("UseCustomTint"), ""),
           ("UseVertexColor", g.scalar("UseVertexColor"), ""), ("VCol", g.vcolor(), "")]
    for i in range(3):
        ins.append((f"CD{i}", g.custom_data(i), ""))
    c = g.custom("BaseColorOut = Color.rgb * lerp(float3(1, 1, 1), float3(CD0, CD1, CD2), UseCustomTint)"
                 " * lerp(float3(1, 1, 1), VCol.rgb, UseVertexColor);", ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3)], "VilaColor")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    g.out(g.scalar("Metallic"), MP.MP_METALLIC)
    end_material(g)


def m_emissive():
    g = begin_material("M_Emissive")
    if not g:
        return
    col = g.vector("Color")
    mul = g.node(unreal.MaterialExpressionMultiply, x=-500)
    g.link(col, "", mul, "A")
    g.link(g.scalar("Intensity"), "", mul, "B")
    g.out(col, MP.MP_BASE_COLOR)
    g.out(mul, MP.MP_EMISSIVE_COLOR)
    g.out(g.const(0.1), MP.MP_ROUGHNESS)
    end_material(g)


def m_glass():
    g = begin_material("M_Glass", blend=unreal.BlendMode.BLEND_TRANSLUCENT, two_sided=True)
    if not g:
        return
    setp(g.m, "translucency_lighting_mode", enum(unreal.TranslucencyLightingMode, "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"), True)
    g.out(g.vector("Tint"), MP.MP_BASE_COLOR)
    g.out(g.scalar("Opacity"), MP.MP_OPACITY)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    g.out(g.scalar("Metallic"), MP.MP_METALLIC)
    end_material(g)


def m_carpaint():
    g = begin_material("M_CarPaint", shading=unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    if not g:
        return
    ins = [("Color", g.vector("Color"), ""), ("DirtColor", g.vector("DirtColor"), ""), ("Dirt", g.scalar("Dirt"), ""),
           ("Roughness", g.scalar("Roughness"), ""), ("ClearCoat", g.scalar("ClearCoat"), ""), ("VCol", g.vcolor(), "")]
    body = """
float d = saturate(Dirt);
BaseColorOut = lerp(Color.rgb, DirtColor.rgb, d * 0.75) * VCol.rgb;
RoughOut = lerp(Roughness, 0.85, d);
CoatOut = ClearCoat * (1.0 - d * 0.8);
"""
    c = g.custom(body, ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("RoughOut", OUT_T.CMOT_FLOAT1), ("CoatOut", OUT_T.CMOT_FLOAT1)], "VilaCarPaint")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    c.to_prop("RoughOut", MP.MP_ROUGHNESS)
    c.to_prop("CoatOut", MP.MP_CUSTOM_DATA0)
    g.out(g.scalar("ClearCoatRoughness"), MP.MP_CUSTOM_DATA1)
    g.out(g.scalar("Metallic"), MP.MP_METALLIC)
    end_material(g)


def m_skin():
    g = begin_material("M_Skin", blend=unreal.BlendMode.BLEND_MASKED, shading=unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE, clip=0.5)
    if not g:
        return
    ins = [("Detail", g.texobj("Detail", T("T_skin_detail")), ""), ("Mask", g.texobj("Mask", T("T_skin_mask")), ""),
           ("UV", g.uv(), ""), ("SkinTone", g.vector("SkinTone"), ""), ("HairColor", g.vector("HairColor"), ""),
           ("HideBits", g.scalar("HideBits"), "")]
    body = """
// Porta do shader de pele do Humanoid.gd (região = Anatomy.region no gerador).
float4 m = Texture2DSample(Mask, MaskSampler, UV);
uint reg = (uint)round(m.r * 16.0);
uint bits = (uint)round(max(HideBits, 0.0));
VisibleOut = ((bits >> reg) & 1u) != 0u ? 0.0 : 1.0;
float3 c = SkinTone.rgb * Texture2DSample(Detail, DetailSampler, UV).rgb;
c = lerp(c, c * float3(0.8, 0.52, 0.52), m.b * 0.6);
c = lerp(c, HairColor.rgb * 0.85, m.g * 0.9);
BaseColorOut = c;
"""
    c = g.custom(body, ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("VisibleOut", OUT_T.CMOT_FLOAT1)], "VilaSkin")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    c.to_prop("VisibleOut", MP.MP_OPACITY_MASK)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    g.out(g.scalar("Specular"), MP.MP_SPECULAR)
    end_material(g)


def m_cloth():
    g = begin_material("M_Cloth")
    if not g:
        return
    ins = [("Albedo", g.texobj("Albedo", T("T_cloth_knit_A")), ""), ("Normal", g.texobj("Normal", T("T_cloth_knit_N")), ""),
           ("ORM", g.texobj("ORM", T("T_cloth_knit_ORM")), ""), ("UV", g.uv(), ""), ("Color", g.vector("Color"), "")]
    for s in ("UVScale", "UseTexture", "UseORM", "NormalStrength", "Roughness", "Metallic"):
        ins.append((s, g.scalar(s), ""))
    body = """
// Humanoid._std: cor * textura de tecido, UV * escala, normal 0,8.
float2 uv = UV * UVScale;
float3 a = Texture2DSample(Albedo, AlbedoSampler, uv).rgb;
float3 orm = Texture2DSample(ORM, ORMSampler, uv).rgb;
float3 nt = VN.dn(Texture2DSample(Normal, NormalSampler, uv).rg);
BaseColorOut = Color.rgb * lerp(float3(1, 1, 1), a, UseTexture);
NormalOut = normalize(lerp(float3(0, 0, 1), nt, saturate(NormalStrength * UseTexture)));
RoughOut = lerp(Roughness, orm.g, UseORM * UseTexture);
MetalOut = Metallic;
"""
    c = g.custom(body, ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("NormalOut", OUT_T.CMOT_FLOAT3), ("RoughOut", OUT_T.CMOT_FLOAT1),
                             ("MetalOut", OUT_T.CMOT_FLOAT1)], "VilaCloth")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    c.to_prop("NormalOut", MP.MP_NORMAL)
    c.to_prop("RoughOut", MP.MP_ROUGHNESS)
    c.to_prop("MetalOut", MP.MP_METALLIC)
    end_material(g)


def m_hair():
    g = begin_material("M_Hair", blend=unreal.BlendMode.BLEND_MASKED, two_sided=True, clip=0.4)
    if not g:
        return
    ins = [("Albedo", g.texobj("Albedo", T("T_hair_A")), ""), ("Normal", g.texobj("Normal", T("T_hair_N")), ""),
           ("UV", g.uv(), ""), ("Color", g.vector("Color"), ""), ("UVScale", g.scalar("UVScale"), "")]
    body = """
float2 uv = UV * UVScale;
float4 a = Texture2DSample(Albedo, AlbedoSampler, uv);
BaseColorOut = Color.rgb * a.rgb;
NormalOut = normalize(lerp(float3(0, 0, 1), VN.dn(Texture2DSample(Normal, NormalSampler, uv).rg), 0.8));
MaskOut = a.a;
"""
    c = g.custom(body, ins, [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("NormalOut", OUT_T.CMOT_FLOAT3), ("MaskOut", OUT_T.CMOT_FLOAT1)], "VilaHair")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    c.to_prop("NormalOut", MP.MP_NORMAL)
    c.to_prop("MaskOut", MP.MP_OPACITY_MASK)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    end_material(g)


def m_eye():
    g = begin_material("M_Eye")
    if not g:
        return
    ins = [("Albedo", g.texobj("Albedo", T("T_eye")), ""), ("UV", g.uv(), ""), ("Color", g.vector("Color"), "")]
    c = g.custom("BaseColorOut = Color.rgb * Texture2DSample(Albedo, AlbedoSampler, UV).rgb;", ins,
                 [("BaseColorOut", OUT_T.CMOT_FLOAT3)], "VilaEye")
    c.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    end_material(g)


LEAF_PIXEL = """
// Nature.leaf_material: cartão de folhas com recorte alfa e copa rala entre câmera e foco.
float4 t = Texture2DSample(LeafTex, LeafTexSampler, UV);
float fade = 0.0;
if (Focus.w > 0.5) {
	float3 seg = Focus.xyz - CamPos;
	float k = saturate(dot(WPos - CamPos, seg) / max(dot(seg, seg), 1.0));
	float d = length(WPos - (CamPos + seg * k)) / 100.0;
	fade = (1.0 - smoothstep(1.6, 3.2, d)) * step(0.05, k) * (1.0 - step(0.97, k));
}
float dither = frac(sin(dot(floor(Parameters.SvPosition.xy), float2(12.9898, 78.233))) * 43758.5453);
MaskOut = (fade * 0.85 > dither) ? 0.0 : t.a;
BaseColorOut = t.rgb * Color.rgb * VCol.rgb;
"""
LEAF_WPO = """
float3 g = float3(WPos.x, WPos.z, WPos.y) / 100.0;   // espaço Godot, metros
float s = sin(Time * 1.3 + g.x * 0.3 + g.z * 0.2 + g.y * 0.6);
float f = sin(Time * 4.1 + g.x * 2.1 + g.y * 3.0) * 0.3;
float3 o = float3(s + f, f * 0.5, s * 0.6) * WindStrength * (0.5 + Wind) * VCA;
WPOOut = float3(o.x, o.z, o.y) * 100.0;
"""


def m_leaf():
    g = begin_material("M_Leaf", blend=unreal.BlendMode.BLEND_MASKED, shading=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE,
                       two_sided=True, clip=0.45)
    if not g:
        return
    vc = g.vcolor()
    color = g.vector("Color")
    pix = g.custom(LEAF_PIXEL, [("LeafTex", g.texobj("LeafTex", T("T_leaf")), ""), ("UV", g.uv(), ""), ("VCol", vc, ""),
                                ("Color", color, ""), ("WPos", g.wpos(), ""), ("CamPos", g.campos(), ""),
                                ("Focus", g.mpc("Focus"), "")],
                   [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("MaskOut", OUT_T.CMOT_FLOAT1)], "VilaLeaf")
    pix.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    pix.to_prop("MaskOut", MP.MP_OPACITY_MASK)
    wpo = g.custom(LEAF_WPO, [("WPos", g.wpos(), ""), ("Time", g.time(), ""), ("VCA", vc, "A"),
                              ("WindStrength", g.scalar("WindStrength"), ""), ("Wind", g.mpc("Wind"), "")],
                   [("WPOOut", OUT_T.CMOT_FLOAT3)], "VilaLeafWind")
    wpo.to_prop("WPOOut", MP.MP_WORLD_POSITION_OFFSET)
    g.out(g.const(0.85), MP.MP_ROUGHNESS)
    g.out(g.const(0.25), MP.MP_SPECULAR)
    sss = g.node(unreal.MaterialExpressionMultiply, x=-300)
    g.link(color, "", sss, "A")
    g.link(g.const(0.35), "", sss, "B")
    g.out(sss, MP.MP_SUBSURFACE_COLOR)
    end_material(g)


GRASS_PIXEL = """
// Nature.grass_material: raiz -> ponta, normais puxadas para cima, some ao longe (visibility_range 70 m).
float tip = UV.y;
float3 c = lerp(RootColor.rgb, TipColor.rgb, smoothstep(0.0, 1.0, tip)) * VCol.rgb * 1.3 * Color.rgb;
BaseColorOut = c * (1.0 - Wetness * 0.2);
RoughOut = lerp(0.95, 0.55, Wetness);
AOOut = lerp(0.45, 1.0, tip);
NormalOut = normalize(lerp(normalize(VNrm), float3(0, 0, 1), 0.75));
float dist = length(WPos - CamPos);
float fade = 1.0 - smoothstep(FadeStart, FadeEnd, dist);
float dither = frac(sin(dot(floor(Parameters.SvPosition.xy), float2(12.9898, 78.233))) * 43758.5453);
MaskOut = fade > dither ? 1.0 : 0.0;
"""
GRASS_WPO = """
float tip = UV.y;
float3 g = float3(WPos.x, WPos.z, WPos.y) / 100.0;
float gust = sin(Time * 0.6 + g.x * 0.05) * 0.5 + 0.5;
float sway = sin(Time * 1.8 + g.x * 0.45 + g.z * 0.31) * (0.6 + gust * 0.8);
float k = sway * WindStrength * (0.5 + Wind) * tip * tip;
WPOOut = float3(k, k * 0.5, 0.0) * 100.0;   // Godot x -> UE X, Godot z -> UE Y
"""


def m_grass():
    g = begin_material("M_Grass", blend=unreal.BlendMode.BLEND_MASKED, two_sided=True, tangent_normal=False, clip=0.5)
    if not g:
        return
    uv = g.uv()
    wp = g.wpos()
    pix = g.custom(GRASS_PIXEL, [("UV", uv, ""), ("VCol", g.vcolor(), ""), ("RootColor", g.vector("RootColor"), ""),
                                 ("TipColor", g.vector("TipColor"), ""), ("Color", g.vector("Color"), ""),
                                 ("Wetness", g.mpc("Wetness"), ""), ("VNrm", g.vnormal(), ""), ("WPos", wp, ""),
                                 ("CamPos", g.campos(), ""), ("FadeStart", g.scalar("FadeStart"), ""), ("FadeEnd", g.scalar("FadeEnd"), "")],
                   [("BaseColorOut", OUT_T.CMOT_FLOAT3), ("RoughOut", OUT_T.CMOT_FLOAT1), ("AOOut", OUT_T.CMOT_FLOAT1),
                    ("NormalOut", OUT_T.CMOT_FLOAT3), ("MaskOut", OUT_T.CMOT_FLOAT1)], "VilaGrass")
    pix.to_prop("BaseColorOut", MP.MP_BASE_COLOR)
    pix.to_prop("RoughOut", MP.MP_ROUGHNESS)
    pix.to_prop("AOOut", MP.MP_AMBIENT_OCCLUSION)
    pix.to_prop("NormalOut", MP.MP_NORMAL)
    pix.to_prop("MaskOut", MP.MP_OPACITY_MASK)
    wpo = g.custom(GRASS_WPO, [("UV", uv, ""), ("WPos", wp, ""), ("Time", g.time(), ""),
                               ("WindStrength", g.scalar("WindStrength"), ""), ("Wind", g.mpc("Wind"), "")],
                   [("WPOOut", OUT_T.CMOT_FLOAT3)], "VilaGrassWind")
    wpo.to_prop("WPOOut", MP.MP_WORLD_POSITION_OFFSET)
    g.out(g.const(0.2), MP.MP_SPECULAR)
    end_material(g)


def m_water():
    g = begin_material("M_Water", blend=unreal.BlendMode.BLEND_TRANSLUCENT)
    if not g:
        return
    setp(g.m, "translucency_lighting_mode", enum(unreal.TranslucencyLightingMode, "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"), True)
    body = """
// marolas leves: gradiente de dois ruídos em movimento (normal em espaço tangente)
float2 p = WPos.xy / 100.0;
float2 e = float2(0.05, 0.0);
float2 q1 = p * 1.7 + float2(Time * 0.21, Time * 0.13);
float2 q2 = p * 3.1 - float2(Time * 0.17, -Time * 0.11);
float h0 = VN.n(q1) + 0.5 * VN.n(q2);
float hx = VN.n(q1 + e.xy) + 0.5 * VN.n(q2 + e.xy);
float hy = VN.n(q1 + e.yx) + 0.5 * VN.n(q2 + e.yx);
NormalOut = normalize(float3((h0 - hx) * Ripple * 20.0, (h0 - hy) * Ripple * 20.0, 1.0));
"""
    c = g.custom(body, [("WPos", g.wpos(), ""), ("Time", g.time(), ""), ("Ripple", g.scalar("Ripple"), "")],
                 [("NormalOut", OUT_T.CMOT_FLOAT3)], "VilaWater")
    c.to_prop("NormalOut", MP.MP_NORMAL)
    g.out(g.vector("Color"), MP.MP_BASE_COLOR)
    g.out(g.scalar("Opacity"), MP.MP_OPACITY)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    g.out(g.scalar("Metallic"), MP.MP_METALLIC)
    end_material(g)


def m_windows():
    g = begin_material("M_Windows")
    if not g:
        return
    c = g.custom("EmisOut = EmissiveColor.rgb * Intensity * lerp(1.0, Night, NightDriven);",
                 [("EmissiveColor", g.vector("EmissiveColor"), ""), ("Intensity", g.scalar("Intensity"), ""),
                  ("NightDriven", g.scalar("NightDriven"), ""), ("Night", g.mpc("Night"), "")],
                 [("EmisOut", OUT_T.CMOT_FLOAT3)], "VilaWindows")
    c.to_prop("EmisOut", MP.MP_EMISSIVE_COLOR)
    g.out(g.vector("Color"), MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness"), MP.MP_ROUGHNESS)
    g.out(g.scalar("Metallic"), MP.MP_METALLIC)
    end_material(g)


def m_rain():
    g = begin_material("M_Rain", blend=unreal.BlendMode.BLEND_TRANSLUCENT, shading=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    if not g:
        return
    body = """
// riscos de chuva caindo (colunas com fase aleatória)
float2 uv = UV * float2(Tiling, Tiling * 0.25);
uv.y -= Time * Speed;
float colId = floor(uv.x);
float r = VN.h(float2(colId, 7.0));
float y = frac(uv.y + r * 7.0);
float streak = smoothstep(0.0, 0.04, y) * (1.0 - smoothstep(0.04, 0.3, y));
float w = 1.0 - smoothstep(0.03, 0.1, abs(frac(uv.x) - 0.5));
AlphaOut = streak * w * step(0.35, r) * Opacity;
"""
    c = g.custom(body, [("UV", g.uv(), ""), ("Time", g.time(), ""), ("Tiling", g.scalar("Tiling"), ""),
                        ("Speed", g.scalar("Speed"), ""), ("Opacity", g.scalar("Opacity"), "")],
                 [("AlphaOut", OUT_T.CMOT_FLOAT1)], "VilaRain")
    c.to_prop("AlphaOut", MP.MP_OPACITY)
    g.out(g.vector("Color"), MP.MP_EMISSIVE_COLOR)
    end_material(g)


def m_outline():
    g = begin_material("M_Outline", domain=unreal.MaterialDomain.MD_POST_PROCESS, shading=unreal.MaterialShadingModel.MSM_UNLIT)
    if not g:
        return
    scene = g.node(unreal.MaterialExpressionSceneTexture)
    setp(scene, "scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    sten = g.node(unreal.MaterialExpressionSceneTexture)
    setp(sten, "scene_texture_id", unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    body = """
// contorno de 'Width' px em volta do que tem CustomStencil == 1 (25 = PPI_CustomStencil)
float2 uv = GetDefaultSceneTextureUV(Parameters, 25);
float2 px = View.BufferSizeAndInvSize.zw * Width;
float selfOn = abs(Stencil.r - 1.0) < 0.5 ? 1.0 : 0.0;
float nb = 0.0;
float2 dirs[8] = { float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1), float2(0.7, 0.7), float2(-0.7, 0.7), float2(0.7, -0.7), float2(-0.7, -0.7) };
for (int i = 0; i < 8; i++) {
	float s = SceneTextureLookup(uv + dirs[i] * px, 25, false).r;
	nb = max(nb, abs(s - 1.0) < 0.5 ? 1.0 : 0.0);
}
float edge = nb * (1.0 - selfOn);
ColorOut = lerp(Scene.rgb, OutlineColor.rgb, edge * OutlineColor.a);
"""
    c = g.custom(body, [("Scene", scene, "Color"), ("Stencil", sten, "Color"), ("Width", g.scalar("Width"), ""),
                        ("OutlineColor", g.vector("OutlineColor"), "")],
                 [("ColorOut", OUT_T.CMOT_FLOAT3)], "VilaOutline")
    c.to_prop("ColorOut", MP.MP_EMISSIVE_COLOR)
    end_material(g)


MASTERS = [m_surface, m_surface_local, m_color, m_emissive, m_glass, m_carpaint, m_skin, m_cloth, m_hair, m_eye,
           m_leaf, m_grass, m_water, m_windows, m_rain, m_outline]


def build_materials():
    log("materiais-mestre...")
    for fn in MASTERS:
        try:
            fn()
        except Exception as e:
            error(f"{fn.__name__}: {e}")


# ============================================================================ instâncias
def make_mi(path, parent_name, scalars=None, vectors=None, textures=None):
    mi = load(path)
    if mi and not FORCE:
        STATS["pulados"].append(path)
        return mi
    parent = load(f"{MAT}/{parent_name}")
    if not parent:
        error(f"{path}: mestre {parent_name} ausente")
        return None
    if not mi:
        mi = create(path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    else:
        MEL.clear_all_material_instance_parameters(mi)
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, lc(v))
    for k, v in (textures or {}).items():
        t = T(v) if isinstance(v, str) else v
        if t:
            MEL.set_material_instance_texture_parameter_value(mi, k, t)
        else:
            warn(f"{path}: textura {v} ausente")
    MEL.update_material_instance(mi)
    save(mi)
    STATS["criados"].append(path)
    return mi


def build_surfaces():
    """MI_<set> (filho de M_Surface) com as texturas e o tile_m de textures.json."""
    log("instâncias de superfície...")
    for s, meta in sorted(tex_meta().items()):
        make_mi(f"{MAT}/Surfaces/MI_{s}", "M_Surface", {"TileMeters": float(meta.get("tile_m", 1.0))}, {},
                {"Albedo": f"T_{s}_A", "Normal": f"T_{s}_N", "ORM": f"T_{s}_ORM"})


# ============================================================================ veículos
VMAT = "/Game/Vila/Vehicles/Materials"
# make_cars.materials(): cores lineares (Principled do Blender), rugosidade, metal.
# A rugosidade de tire/plastic/bedliner/seat/interior vira 0,9 como em CarVisual._shared_mat.
CAR_COLORS = {
    "trim": ((0.03, 0.03, 0.035), 0.55, 0.0), "chrome": ((0.85, 0.85, 0.87), 0.12, 1.0),
    "plastic": ((0.05, 0.05, 0.055), 0.9, 0.0), "interior": ((0.07, 0.065, 0.06), 0.9, 0.0),
    "plate": ((0.9, 0.9, 0.86), 0.5, 0.0), "plate_band": ((0.05, 0.15, 0.55), 0.5, 0.0),
    "rim": ((0.7, 0.71, 0.73), 0.25, 1.0), "rim_dark": ((0.12, 0.12, 0.13), 0.35, 0.8),
    "rim_steel": ((0.22, 0.22, 0.23), 0.5, 0.6), "brake": ((0.35, 0.33, 0.32), 0.45, 0.9),
    "caliper": ((0.45, 0.1, 0.08), 0.4, 0.2), "gap": ((0.012, 0.012, 0.014), 0.8, 0.0),
    "well": ((0.02, 0.02, 0.022), 0.95, 0.0), "bedliner": ((0.04, 0.04, 0.045), 0.9, 0.0),
    "seat": ((0.11, 0.1, 0.095), 0.9, 0.0), "mirror": ((0.8, 0.82, 0.85), 0.02, 1.0),
}


def vehicle_materials():
    m = {}
    m["paint"] = make_mi(f"{VMAT}/MI_CarPaint", "M_CarPaint")
    # CarVisual: vidro Color(0.07, 0.09, 0.11, 0.62), metal 0,4, rugosidade 0,04
    m["glass"] = make_mi(f"{VMAT}/MI_CarGlass", "M_Glass", {"Opacity": 0.62, "Roughness": 0.04, "Metallic": 0.4},
                         {"Tint": srgb((0.07, 0.09, 0.11))})
    # CarVisual._light_material / indicator (energias de luz apagada; o ator acende pelo MID)
    m["headlight"] = make_mi(f"{VMAT}/MI_Headlight", "M_Emissive", {"Intensity": 0.3}, {"Color": srgb((1.0, 0.95, 0.82))})
    m["taillight"] = make_mi(f"{VMAT}/MI_Taillight", "M_Emissive", {"Intensity": 0.2}, {"Color": srgb((1.0, 0.08, 0.04))})
    m["indicator"] = make_mi(f"{VMAT}/MI_Indicator", "M_Emissive", {"Intensity": 0.15}, {"Color": srgb((1.0, 0.5, 0.05))})
    # pneu: textura de borracha em espaço do objeto (a roda gira; no mundo a textura escorregaria)
    m["tire"] = make_mi(f"{VMAT}/MI_CarTire", "M_SurfaceLocal", {"TileMeters": 0.3, "Scale": 1.0, "Roughness": 1.0, "Metallic": 0.0},
                        {"Tint": (0.045, 0.045, 0.05)}, {"Albedo": "T_rubber_A", "Normal": "T_rubber_N", "ORM": "T_rubber_ORM"})
    for name, (col, rough, metal) in CAR_COLORS.items():
        m[name] = make_mi(f"{VMAT}/MI_Car_{name}", "M_Color", {"Roughness": rough, "Metallic": metal, "UseVertexColor": 1.0},
                          {"Color": col})
    return m


def slot_key(name):
    n = str(name).lower()
    n = re.sub(r"^(mi_|m_|mat_|material_)", "", n)
    n = re.sub(r"(\.\d+|_\d+)$", "", n)
    return n


def assign_static(sm, mats, label):
    slots = sm.get_editor_property("static_materials")
    for i, s in enumerate(slots):
        names = [s.get_editor_property("material_slot_name"), s.get_editor_property("imported_material_slot_name")]
        mi = s.get_editor_property("material_interface")
        if mi:
            names.append(mi.get_name())
        key = next((slot_key(n) for n in names if n and slot_key(n) in mats), None)
        if key and mats[key]:
            sm.set_material(i, mats[key])
        else:
            warn(f"{label}: slot sem material conhecido {[str(n) for n in names]}")


def import_static(glb, final_path, mats):
    if exists(final_path) and not FORCE:
        STATS["pulados"].append(final_path)
        return load(final_path)
    tmp = f"{TMP}/{split(final_path)[1]}"
    clean_tmp(tmp)
    paths = import_file(glb, tmp, options=interchange_pipeline(skeletal=False))
    meshes = [p for p in paths if isinstance(EAL.load_asset(p), unreal.StaticMesh)]
    if not meshes:
        error(f"nenhuma StaticMesh em {glb}")
        clean_tmp(tmp)
        return None
    if len(meshes) > 1:
        warn(f"{glb}: {len(meshes)} malhas; usando {meshes[0]}")
    move(meshes[0], final_path)
    sm = load(final_path)
    assign_static(sm, mats, final_path)
    save(sm)
    clean_tmp(tmp)
    STATS["criados"].append(final_path)
    return sm


def build_vehicles():
    log("veículos...")
    mats = vehicle_materials()
    layout = {}
    lp = os.path.join(DATA, "art", "vehicles_layout.json")
    if os.path.exists(lp):
        with open(lp, encoding="utf-8") as f:
            layout = json.load(f)
    for model in MODELS:
        d = os.path.join(ART, "vehicles", model)
        if not os.path.isdir(d):
            error(f"faltam as peças de {model} em {d} (rode export_unreal.py)")
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".glb"):
                continue
            part = fn[:-4]
            sm = import_static(os.path.join(d, fn), f"/Game/Vila/Vehicles/{model}/SM_{part}", mats)
            if sm and part == "Body" and model in layout:
                # conferência de escala e eixos (frente +X): comprimento ~ layout
                box = sm.get_bounding_box()
                size = box.max - box.min
                exp = layout[model].get("length", 0.0)
                if size.x < size.y:
                    error(f"SM_Body de {model}: mais largo em Y que em X; a frente não está em +X (ver export_unreal.py)")
                elif exp and abs(size.x - exp) > 0.1 * exp:
                    warn(f"SM_Body de {model}: comprimento {size.x:.0f} cm, esperado ~{exp:.0f} cm (escala?)")
                else:
                    log(f"{model}: SM_Body {size.x:.0f} x {size.y:.0f} x {size.z:.0f} cm")


# ============================================================================ personagens
CMAT = "/Game/Vila/Characters/Materials"
# Humanoid.FABRIC: peça -> (conjunto de tecido, escala de UV)
FABRIC = {
    "Top_tee": ("cloth_knit", 2.0), "Top_long": ("cloth_knit", 2.0), "Top_jacket": ("cloth_canvas", 2.0),
    "Bottom_pants": ("cloth_denim", 2.0), "Bottom_shorts": ("cloth_denim", 2.0), "Bottom_skirt": ("cloth_canvas", 2.0),
    "Overalls": ("cloth_denim", 2.0), "Apron": ("cloth_canvas", 1.6), "Backpack": ("cloth_canvas", 1.6),
    "Cap": ("cloth_canvas", 1.6), "Beanie": ("cloth_knit", 3.0), "Shoes": ("leather", 1.7),
}


def cloth_mi(tex_set, uv, color=(1, 1, 1), rough=0.9, name=None):
    name = name or f"MI_Cloth_{tex_set}_{str(uv).replace('.', '_')}"
    return make_mi(f"{CMAT}/{name}", "M_Cloth", {"UVScale": uv, "UseTexture": 1.0, "UseORM": 0.0, "NormalStrength": 0.8,
                                                 "Roughness": rough, "Metallic": 0.0},
                   {"Color": color}, {"Albedo": f"T_{tex_set}_A", "Normal": f"T_{tex_set}_N", "ORM": f"T_{tex_set}_ORM"})


def character_material(piece, slot, cache):
    """Humanoid._material: mesmo critério por nome do material."""
    key = slot if slot in ("skin", "eye", "lash", "metal", "sole", "strap", "hair") else f"cloth:{piece}"
    if key in cache:
        return cache[key]
    if slot == "skin":
        m = make_mi(f"{CMAT}/MI_Skin", "M_Skin", {}, {}, {"Detail": "T_skin_detail", "Mask": "T_skin_mask"})
    elif slot == "eye":
        m = make_mi(f"{CMAT}/MI_Eye", "M_Eye", {"Roughness": 0.15}, {"Color": (1, 1, 1)}, {"Albedo": "T_eye"})
    elif slot == "lash":
        m = make_mi(f"{CMAT}/MI_Lash", "M_Color", {"Roughness": 0.8, "Metallic": 0.0}, {"Color": srgb((0.03, 0.025, 0.02))})
    elif slot == "metal":
        m = make_mi(f"{CMAT}/MI_CharMetal", "M_Color", {"Roughness": 0.3, "Metallic": 0.9}, {"Color": srgb((0.75, 0.75, 0.78))})
    elif slot == "sole":
        m = cloth_mi("rubber", 2.0, srgb((0.86, 0.84, 0.8)), 0.85, "MI_Sole")
    elif slot == "strap":
        m = cloth_mi("cloth_canvas", 2.0, srgb((0.13, 0.13, 0.14)), 0.85, "MI_Strap")
    elif slot == "hair":
        m = make_mi(f"{CMAT}/MI_Hair", "M_Hair", {"UVScale": 1.0, "Roughness": 0.55}, {},
                    {"Albedo": "T_hair_A", "Normal": "T_hair_N"})
    else:
        tex_set, uv = FABRIC.get(piece, ("cloth_knit", 2.0))
        m = cloth_mi(tex_set, uv)
    cache[key] = m
    return m


def assign_skeletal(skm, piece, cache):
    mats = list(skm.get_editor_property("materials"))
    for i, s in enumerate(mats):
        names = [s.get_editor_property("material_slot_name"), s.get_editor_property("imported_material_slot_name")]
        mi = s.get_editor_property("material_interface")
        if mi:
            names.append(mi.get_name())
        slot = next((slot_key(n) for n in names if n and str(n) != "None"), "")
        m = character_material(piece, slot, cache)
        if m:
            s.set_editor_property("material_interface", m)
            mats[i] = s
    try:
        skm.set_editor_property("materials", mats)
    except Exception as e:
        error(f"{skm.get_name()}: não consegui trocar os materiais ({e})")


def anim_of(asset_name):
    toks = [t for t in re.split(r"[^a-z0-9]+", asset_name.lower()) if t]
    for t in reversed(toks):
        if t in ANIMS:
            return t
    return None


def import_character(sex, cache):
    base = f"/Game/Vila/Characters/{sex}"
    src = os.path.join(ART, "characters", sex)
    if not os.path.isdir(src):
        error(f"faltam as peças de {sex} em {src} (rode export_unreal.py)")
        return
    body_path, skel_path = f"{base}/SK_Body", f"{base}/SKEL_Human"
    if FORCE:
        for p in EAL.list_assets(base, recursive=True, include_folder=False) if EAL.does_directory_exist(base) else []:
            if "/Materials/" not in str(p):
                EAL.delete_asset(pkg(p))
    # corpo: esqueleto + animações
    if exists(body_path) and exists(skel_path):
        STATS["pulados"].append(body_path)
    else:
        tmp = f"{TMP}/{sex}_Body"
        clean_tmp(tmp)
        paths = import_file(os.path.join(src, "Body.glb"), tmp, options=interchange_pipeline(skeletal=True, animations=True))
        loaded = [(p, EAL.load_asset(p)) for p in paths]
        skm = next((p for p, a in loaded if isinstance(a, unreal.SkeletalMesh)), None)
        skel = next((p for p, a in loaded if isinstance(a, unreal.Skeleton)), None)
        if not skm or not skel:
            error(f"{sex}/Body.glb: esqueleto ou malha não importados ({paths})")
            return
        move(skel, skel_path)
        move(skm, body_path)
        for p, a in loaded:
            if isinstance(a, unreal.AnimSequence):
                an = anim_of(split(pkg(p))[1])
                if an:
                    move(p, f"{base}/Anims/A_{an}")
                else:
                    warn(f"animação sem nome conhecido: {p}")
            elif isinstance(a, unreal.PhysicsAsset):
                move(p, f"{base}/PHYS_Body")
        sk = load(body_path)
        assign_skeletal(sk, "Body", cache)
        save(sk)
        save(load(skel_path))
        for an in ANIMS:
            a = load(f"{base}/Anims/A_{an}")
            if a:
                save(a)
            else:
                error(f"{sex}: animação {an} não importada")
        clean_tmp(tmp)
        STATS["criados"].append(body_path)
    skeleton = load(skel_path)
    # demais peças: mesmo esqueleto, sem animação
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".glb") or fn == "Body.glb":
            continue
        piece = fn[:-4]
        path = f"{base}/SK_{piece}"
        if exists(path):
            STATS["pulados"].append(path)
            continue
        tmp = f"{TMP}/{sex}_{piece}"
        clean_tmp(tmp)
        paths = import_file(os.path.join(src, fn), tmp, options=interchange_pipeline(skeletal=True, skeleton=skeleton))
        loaded = [(p, EAL.load_asset(p)) for p in paths]
        skm = next((p for p, a in loaded if isinstance(a, unreal.SkeletalMesh)), None)
        if not skm:
            error(f"{sex}/{fn}: malha não importada")
            clean_tmp(tmp)
            continue
        for p, a in loaded:
            if isinstance(a, unreal.Skeleton):
                # a opção de esqueleto não foi respeitada: mantém um esqueleto próprio (SetLeaderPose
                # funciona por nome de osso), mas avisa
                warn(f"{sex}/{piece}: importou com esqueleto próprio em vez de SKEL_Human")
                move(p, f"{base}/Skeletons/SKEL_{piece}")
            elif isinstance(a, unreal.PhysicsAsset):
                move(p, f"{base}/Skeletons/PHYS_{piece}")
        move(skm, path)
        sk = load(path)
        assign_skeletal(sk, piece, cache)
        save(sk)
        clean_tmp(tmp)
        STATS["criados"].append(path)


def build_characters():
    log("personagens...")
    cache = {}
    for sex in SEXES:
        import_character(sex, cache)


# ============================================================================ nível
def map_bounds():
    try:
        with open(os.path.join(DATA, "map", "vila_aurora.json"), encoding="utf-8") as f:
            b = json.load(f).get("bounds", [-85, -85, 85, 85])
        return [float(x) for x in b]
    except Exception:
        return [-85.0, -85.0, 85.0, 85.0]


def find_actor(label, cls):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in sub.get_all_level_actors():
        if isinstance(a, cls) and a.get_actor_label() == label:
            return a
    return None


def spawn(cls, label, loc):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    a = find_actor(label, cls)
    if a and not FORCE:
        return a, False
    if a:
        sub.destroy_actor(a)
    a = sub.spawn_actor_from_class(cls, loc, unreal.Rotator(0, 0, 0))
    a.set_actor_label(label)
    return a, True


def build_level():
    log("nível...")
    path = "/Game/Vila/Maps/Vila"
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if exists(path):
        les.load_level(path)
    elif not les.new_level(path):
        error("não consegui criar /Game/Vila/Maps/Vila (rode esta etapa pelo editor)")
        return
    x0, z0, x1, z1 = map_bounds()
    # NavMeshBoundsVolume: mapa inteiro, altura 0..30 m (Godot [x, z] -> UE (X, Y))
    center = unreal.Vector((x0 + x1) * 50.0, (z0 + z1) * 50.0, 1500.0)
    half = unreal.Vector((x1 - x0) * 50.0, (z1 - z0) * 50.0, 1500.0)
    nav, created = spawn(unreal.NavMeshBoundsVolume, "VilaNavBounds", center)
    if created:
        nav.set_actor_scale3d(unreal.Vector(1, 1, 1))
        _, ext = nav.get_actor_bounds(False)
        if ext.x > 1 and ext.y > 1 and ext.z > 1:
            nav.set_actor_scale3d(unreal.Vector(half.x / ext.x, half.y / ext.y, half.z / ext.z))
        else:
            # pincel sem geometria: escala supondo o cubo padrão de 200 uu
            nav.set_actor_scale3d(unreal.Vector(half.x / 100.0, half.y / 100.0, half.z / 100.0))
            warn("NavMeshBoundsVolume sem pincel; escala estimada (confira no editor)")
    # PostProcessVolume global com o contorno
    ppv, created = spawn(unreal.PostProcessVolume, "VilaPostProcess", unreal.Vector(0, 0, 0))
    setp(ppv, "unbound", True)
    setp(ppv, "priority", 0.0, True)
    outline = load(f"{MAT}/M_Outline")
    if outline:
        settings = ppv.get_editor_property("settings")
        wb = unreal.WeightedBlendable()
        wb.set_editor_property("weight", 1.0)
        wb.set_editor_property("object", outline)
        blend = settings.get_editor_property("weighted_blendables")
        blend.set_editor_property("array", [wb])
        settings.set_editor_property("weighted_blendables", blend)
        ppv.set_editor_property("settings", settings)
    else:
        warn("M_Outline ausente: PostProcessVolume sem contorno")
    # World Settings: GameMode do projeto (o DefaultEngine.ini já define o global)
    try:
        sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        ws = next((a for a in sub.get_all_level_actors() if isinstance(a, unreal.WorldSettings)), None)
        gm = unreal.load_class(None, "/Script/VilaAurora.VilaGameMode")
        if ws and gm:
            setp(ws, "default_game_mode", gm, True)
    except Exception as e:
        warn(f"World Settings não ajustado: {e}")
    les.save_current_level()
    STATS["criados"].append(path)


# ============================================================================ principal
def main():
    log(f"projeto: {PROJECT}  force={FORCE}  etapas={sorted(ONLY) if ONLY else 'todas'}")
    steps = [("textures", build_textures), ("mpc", build_mpc), ("materials", build_materials),
             ("surfaces", build_surfaces), ("vehicles", build_vehicles), ("characters", build_characters),
             ("level", build_level)]
    for name, fn in steps:
        if not step(name):
            continue
        try:
            fn()
        except Exception as e:
            import traceback
            error(f"etapa {name}: {e}\n{traceback.format_exc()}")
    clean_tmp()
    EAL.save_directory("/Game/Vila", only_if_is_dirty=True, recursive=True)
    log("=" * 60)
    log(f"criados/atualizados: {len(STATS['criados'])}   pulados (já existiam): {len(STATS['pulados'])}")
    for w in STATS["avisos"]:
        log(f"AVISO: {w}")
    for e in STATS["erros"]:
        log(f"ERRO: {e}")
    log("concluído" if not STATS["erros"] else f"concluído com {len(STATS['erros'])} erro(s)")


main()
