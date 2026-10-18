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
uniform vec3 base_a : source_color = vec3(0.13, 0.22, 0.07);
uniform vec3 base_b : source_color = vec3(0.22, 0.30, 0.10);
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
