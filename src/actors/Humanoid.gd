class_name Humanoid
extends Node3D
## Personagem 3D: corpo e esqueleto do MakeHuman (CC0), roupas, cabelos e acessórios
## gerados por tools/art/blender/make_characters.py (assets/characters/human_<m|f>.glb).
## A aparência liga/desliga peças, aplica cores, morphs (peso, idade, rosto) e esconde
## a pele sob a roupa. Animações por esqueleto: idle, walk, run, sit, lie, knocked,
## work, talk, eat e search. Frente = -Z. A raiz fica nos pés.

const SCENES := {"m": "res://assets/characters/human_m.glb", "f": "res://assets/characters/human_f.glb"}
const TEX := "res://assets/textures/%s_%s.png"
const CHAR_TEX := "res://assets/characters/%s.png"
## Velocidade (m/s) para a qual cada ciclo foi animado.
const CYCLE_SPEED := {"walk": 1.7, "run": 4.4}
const FEMININE_HAIR := ["long", "bob", "bun", "ponytail"]
## Bits de região do corpo escondidos por cada peça (ver Anatomy.region no gerador).
const HIDE_BITS := {
	"Top_tee": (1 << 1) | (1 << 3),
	"Top_long": (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5),
	"Top_jacket": (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5),
	"Bottom_pants": (1 << 2) | (1 << 7) | (1 << 8) | (1 << 9),
	"Bottom_shorts": (1 << 2) | (1 << 7),
	"Bottom_skirt": (1 << 2),
	"Shoes": (1 << 11),
}
## Textura de tecido e escala de UV (as peças usam 1 unidade de UV = 0,5 m).
const FABRIC := {
	"Top_tee": ["cloth_knit", 2.0], "Top_long": ["cloth_knit", 2.0], "Top_jacket": ["cloth_canvas", 2.0],
	"Bottom_pants": ["cloth_denim", 2.0], "Bottom_shorts": ["cloth_denim", 2.0], "Bottom_skirt": ["cloth_canvas", 2.0],
	"Overalls": ["cloth_denim", 2.0], "Apron": ["cloth_canvas", 1.6], "Backpack": ["cloth_canvas", 1.6],
	"Cap": ["cloth_canvas", 1.6], "Beanie": ["cloth_knit", 3.0], "Shoes": ["leather", 1.7],
}

static var _scenes: Dictionary = {}
static var _mats: Dictionary = {}
static var _skin_shader: Shader
static var _textures: Dictionary = {}

var appearance: Dictionary = {}
var pose := "idle"
var speed := 0.0
var height_scale := 1.0
var lod_far := false:
	set(v):
		lod_far = v
		if anim:
			anim.speed_scale = 0.0 if v else _anim_rate()
var sex := "m"
var model: Node3D
var skeleton: Skeleton3D
var anim: AnimationPlayer
var meshes: Dictionary = {}
var _elderly := false

static func _scene(s: String) -> PackedScene:
	if not _scenes.has(s):
		_scenes[s] = load(SCENES[s])
	return _scenes[s]

static func _tex(path: String) -> Texture2D:
	if not _textures.has(path):
		_textures[path] = load(path) if ResourceLoader.exists(path) else null
	return _textures[path]

static func infer_sex(app: Dictionary) -> String:
	if app.has("sex"):
		return "f" if str(app.sex) == "f" else "m"
	return "f" if str(app.get("hair_style", "short")) in FEMININE_HAIR and not app.get("beard", false) else "m"

func build(app: Dictionary) -> void:
	appearance = app
	sex = infer_sex(app)
	height_scale = float(app.get("height", 1.0))
	_elderly = app.get("elderly", false)
	model = _scene(sex).instantiate()
	model.name = "Model"
	model.scale = Vector3.ONE * height_scale
	add_child(model)
	for mi in model.find_children("*", "MeshInstance3D", true, false):
		meshes[str(mi.name)] = mi
		mi.visible = false
	skeleton = model.find_children("*", "Skeleton3D", true, false)[0] if model.find_children("*", "Skeleton3D", true, false).size() > 0 else null
	var players := model.find_children("*", "AnimationPlayer", true, false)
	anim = players[0] if players.size() > 0 else null
	_dress(app)
	if anim:
		for n in anim.get_animation_list():
			if n in ["idle", "walk", "run", "talk", "work", "eat", "search", "sit", "lie", "knocked"]:
				anim.get_animation(n).loop_mode = Animation.LOOP_LINEAR
		anim.play("idle")
		anim.seek(randf() * 2.0, true)

## Escolhe e pinta as peças conforme a aparência.
func _dress(app: Dictionary) -> void:
	var skin := Color(app.get("skin", "#d9a98a"))
	var hair := Color(app.get("hair", "#2a1d14"))
	var shirt := Color(app.get("shirt", "#4477aa"))
	var pants := Color(app.get("pants", "#333344"))
	var shoes := Color(app.get("shoes", "#2a2420"))
	var top := "Top_jacket" if app.get("jacket", false) else ("Top_long" if app.get("top", "") == "long" else "Top_tee")
	var bottom: String = {"shorts": "Bottom_shorts", "skirt": "Bottom_skirt"}.get(str(app.get("bottom", "")), "Bottom_pants")
	var style := str(app.get("hair_style", "short"))
	var hair_mesh: String = {"bald": "", "short": "Hair_short", "slick": "Hair_slick", "messy": "Hair_messy", "curly": "Hair_curly",
		"long": "Hair_long", "bob": "Hair_bob", "bun": "Hair_bun", "ponytail": "Hair_ponytail"}.get(style, "Hair_short")
	var on: Dictionary = {"Body": skin, "Eyes": null, "Lashes": null, top: shirt, bottom: pants, "Shoes": shoes}
	if app.has("cap"):
		on["Cap"] = Color(app.cap)
		if hair_mesh in ["Hair_messy", "Hair_curly"]:
			hair_mesh = "Hair_short"
	if app.has("beanie"):
		on["Beanie"] = Color(app.beanie)
		hair_mesh = "" if hair_mesh in ["Hair_short", "Hair_slick", "Hair_messy", "Hair_curly"] else hair_mesh
	if hair_mesh != "":
		on[hair_mesh] = hair
	if app.get("beard", false):
		on["Beard"] = hair
	if app.get("mustache", false):
		on["Mustache"] = Color("6a6a6a") if _elderly else hair
	if app.has("backpack"):
		on["Backpack"] = Color(app.backpack)
	if app.has("apron"):
		on["Apron"] = Color(app.apron)
	if app.get("overalls", false):
		on["Overalls"] = pants
	var hide := 0
	for n in on:
		if HIDE_BITS.has(n):
			hide |= int(HIDE_BITS[n])
	for n in on:
		var mi: MeshInstance3D = meshes.get(n)
		if mi == null:
			continue
		mi.visible = true
		for i in mi.mesh.get_surface_count():
			var src := mi.mesh.surface_get_material(i)
			var mname := str(src.resource_name) if src else ""
			mi.set_surface_override_material(i, _material(n, mname, on[n], skin, hair, hide))
		_apply_shape(mi, app)

func _apply_shape(mi: MeshInstance3D, app: Dictionary) -> void:
	var b := float(app.get("build", 1.0))
	var vals := {
		"heavy": clampf((b - 1.0) / 0.2, 0.0, 1.0),
		"thin": clampf((1.0 - b) / 0.15, 0.0, 1.0),
		"old": 0.85 if _elderly else 0.0,
	}
	# variação de traços do rosto: estável por aparência
	var h := absi(hash(str(app.get("skin", "")) + str(app.get("hair", "")) + str(app.get("shirt", ""))))
	var skin := Color(app.get("skin", "#d9a98a"))
	vals["face_a"] = clampf((0.75 - skin.v) * 1.6, 0.0, 0.8) + float(h % 7) * 0.03
	vals["face_b"] = float(int(h / 7.0) % 5) * 0.12
	for k in vals:
		var idx := mi.find_blend_shape_by_name(k)
		if idx >= 0:
			mi.set_blend_shape_value(idx, vals[k])

func _material(piece: String, mname: String, tint, skin: Color, hair: Color, hide: int) -> Material:
	match mname:
		"skin":
			return _skin_material(skin, hair, hide)
		"eye":
			return _std("eye", Color.WHITE, CHAR_TEX % "eye", "", 1.0, 0.15)
		"lash":
			return _std("lash", Color(0.03, 0.025, 0.02), "", "", 1.0, 0.8)
		"metal":
			return _std("metal", Color(0.75, 0.75, 0.78), "", "", 1.0, 0.3, 0.9)
		"sole":
			return _std("sole", Color(0.86, 0.84, 0.8), TEX % ["rubber", "albedo"], TEX % ["rubber", "normal"], 2.0, 0.85)
		"strap":
			return _std("strap", Color(0.13, 0.13, 0.14), TEX % ["cloth_canvas", "albedo"], TEX % ["cloth_canvas", "normal"], 2.0, 0.85)
		"hair":
			return _std("hair", tint, TEX % ["hair", "albedo"], TEX % ["hair", "normal"], 1.0, 0.55)
	var c: Color = tint if tint is Color else Color.WHITE
	var fab: Array = FABRIC.get(piece, ["cloth_knit", 2.0])
	return _std(fab[0], c, TEX % [fab[0], "albedo"], TEX % [fab[0], "normal"], float(fab[1]), 0.9)

static func _std(kind: String, c: Color, albedo: String, normal: String, uv: float, rough: float, metal: float = 0.0) -> StandardMaterial3D:
	var key := "%s|%s|%.2f" % [kind, c.to_html(), uv]
	if _mats.has(key):
		return _mats[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	if albedo != "":
		m.albedo_texture = _tex(albedo)
	if normal != "" and _tex(normal):
		m.normal_enabled = true
		m.normal_texture = _tex(normal)
		m.normal_scale = 0.8
	m.uv1_scale = Vector3(uv, uv, uv)
	_mats[key] = m
	return m

static func _skin_material(skin: Color, hair: Color, hide: int) -> ShaderMaterial:
	var key := "skin|%s|%s|%d" % [skin.to_html(), hair.to_html(), hide]
	if _mats.has(key):
		return _mats[key]
	if _skin_shader == null:
		_skin_shader = Shader.new()
		_skin_shader.code = """
shader_type spatial;
uniform sampler2D detail : source_color, filter_linear_mipmap;
uniform sampler2D mask : filter_nearest;
uniform vec3 skin_color : source_color;
uniform vec3 hair_color : source_color;
uniform int hide = 0;
void fragment() {
	vec4 m = texture(mask, UV);
	int reg = int(round(m.r * 16.0));
	if (((hide >> reg) & 1) == 1) {
		discard;
	}
	vec3 c = skin_color * texture(detail, UV).rgb;
	c = mix(c, c * vec3(0.8, 0.52, 0.52), m.b * 0.6);
	c = mix(c, hair_color * 0.85, m.g * 0.9);
	ALBEDO = c;
	ROUGHNESS = 0.6;
	SPECULAR = 0.3;
}
"""
	var m := ShaderMaterial.new()
	m.shader = _skin_shader
	m.set_shader_parameter("detail", _tex(CHAR_TEX % "skin_detail"))
	m.set_shader_parameter("mask", _tex(CHAR_TEX % "skin_mask"))
	m.set_shader_parameter("skin_color", skin)
	m.set_shader_parameter("hair_color", hair)
	m.set_shader_parameter("hide", hide)
	_mats[key] = m
	return m

func set_pose(p: String) -> void:
	if p == pose:
		return
	pose = p
	if anim and anim.has_animation(p):
		var blend := 0.12 if p in ["walk", "run"] else 0.3
		if p in ["lie", "knocked", "sit"]:
			blend = 0.0
		anim.play(p, blend)
		anim.speed_scale = 0.0 if lod_far else _anim_rate()

func _anim_rate() -> float:
	var r := 0.8 if _elderly else 1.0
	if pose in CYCLE_SPEED:
		r = clampf(maxf(speed, 0.6) / float(CYCLE_SPEED[pose]), 0.5, 1.9)
	return r

func _process(_delta: float) -> void:
	if anim == null or lod_far or not is_visible_in_tree():
		return
	if pose in CYCLE_SPEED:
		anim.speed_scale = _anim_rate()
