class_name Mat
extends RefCounted
## Biblioteca de materiais (com cache) e shaders estilizados do mundo.

static var _cache: Dictionary = {}
static var _shaders: Dictionary = {}
static var window_material: StandardMaterial3D
static var lamp_material: StandardMaterial3D

static func color(c, roughness: float = 0.85, metallic: float = 0.0) -> StandardMaterial3D:
	var col: Color = c if c is Color else Color(str(c))
	var key := "%s|%.2f|%.2f" % [col.to_html(), roughness, metallic]
	if _cache.has(key):
		return _cache[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = col
	m.roughness = roughness
	m.metallic = metallic
	_cache[key] = m
	return m

static func vertex_color(roughness: float = 0.85) -> StandardMaterial3D:
	var key := "vc|%.2f" % roughness
	if _cache.has(key):
		return _cache[key]
	var m := StandardMaterial3D.new()
	m.vertex_color_use_as_albedo = true
	m.roughness = roughness
	_cache[key] = m
	return m

static func emissive(c: Color, energy: float = 1.5) -> StandardMaterial3D:
	var key := "em|%s|%.2f" % [c.to_html(), energy]
	if _cache.has(key):
		return _cache[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.emission_enabled = true
	m.emission = c
	m.emission_energy_multiplier = energy
	_cache[key] = m
	return m

static func glass(c: Color = Color(0.55, 0.72, 0.85, 0.45)) -> StandardMaterial3D:
	var key := "glass|%s" % c.to_html()
	if _cache.has(key):
		return _cache[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.roughness = 0.08
	m.metallic = 0.2
	_cache[key] = m
	return m

## Vidro das janelas: acende à noite (controlado pelo ciclo dia/noite).
static func windows() -> StandardMaterial3D:
	if window_material == null:
		window_material = StandardMaterial3D.new()
		window_material.albedo_color = Color(0.42, 0.55, 0.66)
		window_material.roughness = 0.1
		window_material.metallic = 0.3
		window_material.emission_enabled = true
		window_material.emission = Color(1.0, 0.78, 0.45)
		window_material.emission_energy_multiplier = 0.0
	return window_material

static func water() -> StandardMaterial3D:
	if _cache.has("water"):
		return _cache["water"]
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.16, 0.26, 0.3, 0.85)
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.roughness = 0.05
	m.metallic = 0.3
	_cache["water"] = m
	return m

static func lamps() -> StandardMaterial3D:
	if lamp_material == null:
		lamp_material = StandardMaterial3D.new()
		lamp_material.albedo_color = Color(0.95, 0.93, 0.85)
		lamp_material.emission_enabled = true
		lamp_material.emission = Color(1.0, 0.85, 0.6)
		lamp_material.emission_energy_multiplier = 0.0
	return lamp_material

static func set_night(amount: float) -> void:
	windows().emission_energy_multiplier = amount * 1.4
	lamps().emission_energy_multiplier = amount * 4.0

static func _shader(name: String, code: String) -> Shader:
	if not _shaders.has(name):
		var s := Shader.new()
		s.code = code
		_shaders[name] = s
	return _shaders[name]

const NOISE_FN := """
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p); vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
"""

## Grama com variação de tom por posição no mundo (sem texturas externas).
static func grass() -> ShaderMaterial:
	if _cache.has("grass"):
		return _cache["grass"]
	var m := ShaderMaterial.new()
	m.shader = _shader("grass", """
shader_type spatial;
uniform vec3 base_a : source_color = vec3(0.12, 0.17, 0.07);
uniform vec3 base_b : source_color = vec3(0.2, 0.25, 0.11);
uniform float wetness = 0.0;
varying vec3 wpos;
""" + NOISE_FN + """
void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	float n = vnoise(wpos.xz * 0.15) * 0.6 + vnoise(wpos.xz * 1.3) * 0.3 + hash(floor(wpos.xz * 6.0)) * 0.1;
	ALBEDO = mix(base_a, base_b, n) * (1.0 - wetness * 0.25);
	ROUGHNESS = mix(0.95, 0.6, wetness);
}
""")
	_cache["grass"] = m
	return m

## Asfalto com granulação e brilho quando molhado (chuva).
static func asphalt() -> ShaderMaterial:
	if _cache.has("asphalt"):
		return _cache["asphalt"]
	var m := ShaderMaterial.new()
	m.shader = _shader("asphalt", """
shader_type spatial;
uniform vec3 base : source_color = vec3(0.14, 0.145, 0.16);
uniform float wetness = 0.0;
varying vec3 wpos;
""" + NOISE_FN + """
void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	float grain = hash(floor(wpos.xz * 14.0)) * 0.06 + vnoise(wpos.xz * 0.4) * 0.05;
	float patch = smoothstep(0.62, 0.7, vnoise(wpos.xz * 0.08)) * 0.04;
	ALBEDO = (base + grain - patch) * (1.0 - wetness * 0.35);
	ROUGHNESS = mix(0.9, 0.15, wetness);
	METALLIC = wetness * 0.1;
}
""")
	_cache["asphalt"] = m
	return m

## Calçada de ladrilhos (padrão em grade gerado no shader).
static func sidewalk() -> ShaderMaterial:
	if _cache.has("sidewalk"):
		return _cache["sidewalk"]
	var m := ShaderMaterial.new()
	m.shader = _shader("sidewalk", """
shader_type spatial;
uniform vec3 base : source_color = vec3(0.56, 0.54, 0.51);
uniform float wetness = 0.0;
varying vec3 wpos;
""" + NOISE_FN + """
void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	vec2 g = fract(wpos.xz / 0.8);
	float line = step(0.94, max(g.x, g.y));
	float tone = hash(floor(wpos.xz / 0.8)) * 0.06;
	ALBEDO = (base - line * 0.12 + tone) * (1.0 - wetness * 0.25);
	ROUGHNESS = mix(0.85, 0.3, wetness);
}
""")
	_cache["sidewalk"] = m
	return m

## Piso interior: tábuas de madeira ou porcelanato conforme a cor.
static func floor_tiles(c: Color, planks: bool) -> ShaderMaterial:
	var key := "floor|%s|%s" % [c.to_html(), planks]
	if _cache.has(key):
		return _cache[key]
	var m := ShaderMaterial.new()
	m.shader = _shader("floor", """
shader_type spatial;
uniform vec3 base : source_color;
uniform bool planks = true;
varying vec3 wpos;
""" + NOISE_FN + """
void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	vec3 col = base;
	if (planks) {
		float row = floor(wpos.z / 0.22);
		float off = hash(vec2(row, 3.0)) * 2.0;
		float seam = step(0.93, fract(wpos.z / 0.22)) + step(0.985, fract((wpos.x + off) / 1.4));
		col = base * (0.9 + hash(vec2(row, floor((wpos.x + off) / 1.4))) * 0.18) - seam * 0.08;
		col += (vnoise(wpos.xz * vec2(1.0, 18.0)) - 0.5) * 0.04;
	} else {
		vec2 g = fract(wpos.xz / 0.6);
		col = base - step(0.96, max(g.x, g.y)) * 0.1 + hash(floor(wpos.xz / 0.6)) * 0.03;
	}
	ALBEDO = col * 0.88;
	ROUGHNESS = planks ? 0.7 : 0.5;
}
""")
	m.set_shader_parameter("base", c)
	m.set_shader_parameter("planks", planks)
	_cache[key] = m
	return m

static func set_wetness(w: float) -> void:
	for k in ["grass", "asphalt", "sidewalk"]:
		if _cache.has(k):
			_cache[k].set_shader_parameter("wetness", w)
	for m in _surfaces.values():
		m.set_shader_parameter("wetness", w)

# ------------------------------------------------------------------ superfícies PBR
## Texturas PBR do projeto (assets/textures, geradas por tools/art/make_textures.py)
## projetadas em caixa no espaço do mundo: dispensa UV na geometria procedural e mantém
## a escala física (metros por repetição em textures.json). A cor de vértice tinge.
static var _surfaces: Dictionary = {}
static var _tex_meta: Dictionary = {}
static var _tex_cache: Dictionary = {}

const SURFACE_SHADER := """
shader_type spatial;
render_mode world_vertex_coords;
uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D orm_tex : filter_linear_mipmap_anisotropic, repeat_enable;
uniform float tile = 1.0;
uniform vec3 tint : source_color = vec3(1.0);
uniform float macro = 0.1;
uniform float normal_strength = 1.0;
uniform float wetness = 0.0;
uniform float outdoor = 1.0;
uniform bool detile = false;
uniform bool swap_top = false;
uniform float rough_bias = 0.0;
varying vec3 wpos;
varying vec3 wnrm;
""" + NOISE_FN + """
void vertex() {
	wpos = VERTEX;
	wnrm = NORMAL;
	vec3 n = abs(NORMAL);
	if (n.y >= n.x && n.y >= n.z) {
		UV = swap_top ? vec2(VERTEX.z, -VERTEX.x) : VERTEX.xz;
		TANGENT = swap_top ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
		BINORMAL = swap_top ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, -1.0);
	} else if (n.x >= n.z) {
		float s = sign(NORMAL.x);
		UV = vec2(-VERTEX.z * s, -VERTEX.y);
		TANGENT = vec3(0.0, 0.0, -s);
		BINORMAL = vec3(0.0, 1.0, 0.0);
	} else {
		float s = sign(NORMAL.z);
		UV = vec2(VERTEX.x * s, -VERTEX.y);
		TANGENT = vec3(s, 0.0, 0.0);
		BINORMAL = vec3(0.0, 1.0, 0.0);
	}
	UV /= tile;
}
void fragment() {
	vec2 uv = UV;
	vec4 a = texture(albedo_tex, uv);
	vec3 nm = texture(normal_tex, uv).rgb;
	vec3 orm = texture(orm_tex, uv).rgb;
	if (detile) {
		// segunda amostra em escala/deslocamento diferentes, misturada por ruído (quebra a repetição)
		vec2 uv2 = uv * 0.61 + vec2(0.37, 0.71);
		float k = smoothstep(0.35, 0.65, vnoise(wpos.xz * 0.11 + wpos.y * 0.07));
		a = mix(a, texture(albedo_tex, uv2), k);
		nm = mix(nm, texture(normal_tex, uv2).rgb, k);
		orm = mix(orm, texture(orm_tex, uv2).rgb, k);
	}
	float m = vnoise(wpos.xz * 0.07 + wpos.y * 0.05) * 0.6 + vnoise(wpos.xz * 0.6 + wpos.y * 0.4) * 0.4;
	vec3 col = a.rgb * tint * COLOR.rgb * (1.0 + (m - 0.5) * macro * 2.0);
	float up = smoothstep(0.3, 0.9, wnrm.y);
	float wet = wetness * outdoor * mix(0.35, 1.0, up);
	ALBEDO = col * (1.0 - wet * 0.3);
	ROUGHNESS = clamp(mix(orm.g + rough_bias, 0.12, wet * 0.85), 0.04, 1.0);
	METALLIC = orm.b;
	AO = orm.r;
	AO_LIGHT_AFFECT = 0.35;
	NORMAL_MAP = mix(nm, vec3(0.5, 0.5, 1.0), wet * 0.6);
	NORMAL_MAP_DEPTH = normal_strength;
}
"""

static func _texture(path: String) -> Texture2D:
	if not _tex_cache.has(path):
		_tex_cache[path] = load(path) if ResourceLoader.exists(path) else null
	return _tex_cache[path]

static func tile_of(name: String) -> float:
	if _tex_meta.is_empty():
		var f := FileAccess.open("res://assets/textures/textures.json", FileAccess.READ)
		if f:
			_tex_meta = JSON.parse_string(f.get_as_text())
	return float(_tex_meta.get(name, {}).get("tile_m", 1.0))

## Material de superfície: nome do conjunto de texturas + opções.
## opts: tint (Color), tile (m), macro, detile, swap_top, outdoor, normal, rough.
static func surface(name: String, opts: Dictionary = {}) -> ShaderMaterial:
	var key := name + "|" + str(opts)
	if _surfaces.has(key):
		return _surfaces[key]
	var m := ShaderMaterial.new()
	m.shader = _shader("surface", SURFACE_SHADER)
	m.set_shader_parameter("albedo_tex", _texture("res://assets/textures/%s_albedo.png" % name))
	m.set_shader_parameter("normal_tex", _texture("res://assets/textures/%s_normal.png" % name))
	m.set_shader_parameter("orm_tex", _texture("res://assets/textures/%s_orm.png" % name))
	m.set_shader_parameter("tile", float(opts.get("tile", tile_of(name))))
	m.set_shader_parameter("tint", opts.get("tint", Color.WHITE))
	m.set_shader_parameter("macro", float(opts.get("macro", 0.1)))
	m.set_shader_parameter("detile", bool(opts.get("detile", false)))
	m.set_shader_parameter("swap_top", bool(opts.get("swap_top", false)))
	m.set_shader_parameter("outdoor", float(opts.get("outdoor", 1.0)))
	m.set_shader_parameter("normal_strength", float(opts.get("normal", 1.0)))
	m.set_shader_parameter("rough_bias", float(opts.get("rough", 0.0)))
	_surfaces[key] = m
	return m

## Atalhos com as escolhas da direção de arte para cada uso.
const SURFACES := {
	"asphalt": ["asphalt", {"detile": true, "macro": 0.18}],
	"sidewalk": ["sidewalk", {"detile": true, "macro": 0.12, "tint": Color(0.8, 0.79, 0.77)}],
	"plaza": ["pedra_portuguesa", {"macro": 0.08, "tint": Color(0.88, 0.87, 0.85)}],
	"concrete": ["concrete", {"detile": true, "macro": 0.15}],
	"curb": ["concrete", {"tile": 1.0, "macro": 0.1}],
	"plaster": ["plaster", {"detile": true, "macro": 0.1}],
	"plaster_in": ["plaster", {"macro": 0.05, "outdoor": 0.0}],
	"brick": ["brick", {"macro": 0.12}],
	"siding": ["wood_siding", {"macro": 0.08}],
	"roof_tiles": ["roof_tiles", {"macro": 0.15}],
	"roof_tiles_z": ["roof_tiles", {"macro": 0.15, "swap_top": true}],
	"roof_flat": ["roof_flat", {"detile": true, "macro": 0.2}],
	"roof_fiber": ["roof_fiber", {"macro": 0.12}],
	"roof_fiber_z": ["roof_fiber", {"macro": 0.12, "swap_top": true}],
	"wood_floor": ["wood_floor", {"outdoor": 0.0}],
	"porcelain": ["porcelain", {"outdoor": 0.0}],
	"wall_tiles": ["wall_tiles", {"outdoor": 0.0}],
	"wood": ["wood", {"tile": 0.8}],
	"wood_out": ["wood", {"tile": 0.8, "rough": 0.15}],
	"fabric": ["fabric", {"outdoor": 0.0, "tile": 0.35}],
	"metal": ["metal_painted", {"tile": 0.8}],
	"steel": ["metal_brushed", {"tile": 0.6}],
	"corrugated": ["corrugated", {"tile": 1.0}],
	"canvas": ["cloth_canvas", {"tile": 0.4}],
	"dirt": ["dirt", {"detile": true}],
	"rubber": ["rubber", {"tile": 0.3}],
}

## Textura PBR em material padrão com projeção triplanar no espaço do objeto: para
## peças que se movem (portas, carrinhos), sem a textura "escorregar".
static func textured(name: String, c: Color = Color.WHITE, scale: float = 1.0, rough: float = -1.0) -> BaseMaterial3D:
	var key := "tx|%s|%s|%.2f|%.2f" % [name, c.to_html(), scale, rough]
	if _cache.has(key):
		return _cache[key]
	var m := ORMMaterial3D.new()
	m.albedo_color = c
	m.albedo_texture = _texture("res://assets/textures/%s_albedo.png" % name)
	m.normal_enabled = true
	m.normal_texture = _texture("res://assets/textures/%s_normal.png" % name)
	m.orm_texture = _texture("res://assets/textures/%s_orm.png" % name)
	if rough >= 0.0:
		m.roughness = rough
	m.uv1_triplanar = true
	var t := 1.0 / (tile_of(name) * scale)
	m.uv1_scale = Vector3(t, t, t)
	_cache[key] = m
	return m

static func named(key: String) -> ShaderMaterial:
	var d: Array = SURFACES.get(key, [key, {}])
	return surface(d[0], d[1])
