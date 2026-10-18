class_name Nature
extends RefCounted
## Recursos da direção de arte "diorama dourado" (ver docs/DIRECAO_ARTE.md):
## textura de folhas gerada por código, shaders de grama, folhagem e terra batida.
## Tudo procedural e original — substituível por atlas pintados à mão no futuro.

static var _leaf_tex: ImageTexture
static var _grass_mat: ShaderMaterial
static var _leaf_mat: ShaderMaterial
static var _dirt_mat: ShaderMaterial

## Atlas 2x2 de folhas (silhuetas levemente diferentes) com transparência.
static func leaf_texture() -> ImageTexture:
	if _leaf_tex:
		return _leaf_tex
	var size := 128
	var img := Image.create(size, size, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var rng := RandomNumberGenerator.new()
	rng.seed = 77
	for cell in 4:
		var ox := (cell % 2) * 64
		var oy := (cell / 2) * 64
		# Cada célula: um raminho com 13-17 folhas elípticas.
		var leaves := rng.randi_range(13, 17)
		for i in leaves:
			var cx := ox + 32.0 + rng.randf_range(-21.0, 21.0)
			var cy := oy + 32.0 + rng.randf_range(-21.0, 21.0)
			var ang := rng.randf() * TAU
			var rl := rng.randf_range(6.0, 9.5)
			var rw := rl * rng.randf_range(0.42, 0.6)
			var shade := rng.randf_range(0.78, 1.0)
			for y in range(int(cy - rl), int(cy + rl) + 1):
				for x in range(int(cx - rl), int(cx + rl) + 1):
					if x < ox or x >= ox + 64 or y < oy or y >= oy + 64:
						continue
					var dx := x - cx
					var dy := y - cy
					var u := (dx * cos(ang) + dy * sin(ang)) / rl
					var v := (-dx * sin(ang) + dy * cos(ang)) / rw
					var r := u * u + v * v
					if r <= 1.0:
						var vein := 1.0 - 0.18 * exp(-v * v * 40.0)
						var edge := clampf((1.0 - r) * 4.0, 0.0, 1.0)
						var c := Color(shade * vein, shade * vein, shade * vein, edge)
						var prev := img.get_pixel(x, y)
						if prev.a < c.a:
							img.set_pixel(x, y, c)
	img.generate_mipmaps()
	_leaf_tex = ImageTexture.create_from_image(img)
	return _leaf_tex

## Grama em tufos: cor da raiz à ponta, variação por instância, vento.
static func grass_material() -> ShaderMaterial:
	if _grass_mat:
		return _grass_mat
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
render_mode cull_disabled, diffuse_lambert_wrap;
uniform vec3 root_color : source_color = vec3(0.075, 0.11, 0.045);
uniform vec3 tip_color : source_color = vec3(0.27, 0.34, 0.14);
uniform float wind_strength = 0.18;
uniform float wetness = 0.0;
varying float tipness;
void vertex() {
	tipness = UV.y;
	vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float t = TIME;
	float gust = sin(t * 0.6 + wp.x * 0.05) * 0.5 + 0.5;
	float sway = sin(t * 1.8 + wp.x * 0.45 + wp.z * 0.31) * (0.6 + gust * 0.8);
	float h = tipness * tipness;
	VERTEX.x += sway * wind_strength * h;
	VERTEX.z += sway * wind_strength * 0.5 * h;
	// Normais apontando para cima: a grama ilumina como um tapete macio.
	NORMAL = normalize(mix(NORMAL, vec3(0.0, 1.0, 0.0), 0.75));
}
void fragment() {
	vec3 c = mix(root_color, tip_color, smoothstep(0.0, 1.0, tipness)) * COLOR.rgb * 1.3;
	ALBEDO = c * (1.0 - wetness * 0.2);
	ROUGHNESS = mix(0.95, 0.55, wetness);
	SPECULAR = 0.2;
	AO = mix(0.45, 1.0, tipness);
	AO_LIGHT_AFFECT = 0.6;
}
"""
	_grass_mat = ShaderMaterial.new()
	_grass_mat.shader = sh
	return _grass_mat

## Folhagem: cartões de folhas com recorte alfa, normais esféricas (volume suave),
## balanço ao vento e sombras recortadas (luz salpicada no chão).
static func leaf_material() -> ShaderMaterial:
	if _leaf_mat:
		return _leaf_mat
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
render_mode cull_disabled, diffuse_lambert_wrap;
uniform sampler2D leaf_tex : source_color, filter_linear_mipmap, repeat_disable;
uniform float wind_strength = 0.07;
void vertex() {
	vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float s = sin(TIME * 1.3 + wp.x * 0.3 + wp.z * 0.2 + wp.y * 0.6);
	float f = sin(TIME * 4.1 + wp.x * 2.1 + wp.y * 3.0) * 0.3;
	VERTEX += vec3(s + f, f * 0.5, s * 0.6) * wind_strength * COLOR.a;
}
void fragment() {
	if (!FRONT_FACING) { NORMAL = -NORMAL; }
	vec4 t = texture(leaf_tex, UV);
	ALBEDO = t.rgb * COLOR.rgb;
	ALPHA = t.a;
	ALPHA_SCISSOR_THRESHOLD = 0.45;
	ROUGHNESS = 0.85;
	SPECULAR = 0.25;
}
"""
	_leaf_mat = ShaderMaterial.new()
	_leaf_mat.shader = sh
	_leaf_mat.set_shader_parameter("leaf_tex", leaf_texture())
	return _leaf_mat

## Terra batida com pedrinhas, sulcos e borda orgânica (recorte por ruído).
static func dirt_material() -> ShaderMaterial:
	if _dirt_mat:
		return _dirt_mat
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
uniform vec3 dirt : source_color = vec3(0.47, 0.33, 0.22);
uniform vec3 dirt_dark : source_color = vec3(0.30, 0.21, 0.14);
uniform float wetness = 0.0;
varying vec3 wpos;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p); vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	// UV.x: 0..1 ao longo da largura (bordas recortadas), UV.y: comprimento.
	float edge = min(UV.x, 1.0 - UV.x);
	float n = vnoise(wpos.xz * 1.3) * 0.18 + vnoise(wpos.xz * 4.0) * 0.07;
	if (edge < 0.06 + n) { discard; }
	float rut = smoothstep(0.08, 0.0, abs(UV.x - 0.3)) + smoothstep(0.08, 0.0, abs(UV.x - 0.7));
	float grain = vnoise(wpos.xz * 9.0);
	vec2 cell = floor(wpos.xz * 6.0);
	vec2 local = fract(wpos.xz * 6.0) - 0.5;
	float pebble = step(0.93, hash(cell)) * smoothstep(0.32, 0.18, length(local + (vec2(hash(cell + 3.1), hash(cell + 7.7)) - 0.5) * 0.4));
	vec3 c = mix(dirt, dirt_dark, grain * 0.5 + rut * 0.35);
	c = mix(c, vec3(0.58, 0.52, 0.45) * (0.8 + hash(cell + 1.0) * 0.3), pebble * 0.8);
	ALBEDO = c * (1.0 - wetness * 0.35);
	ROUGHNESS = mix(0.95, 0.35, wetness) - pebble * 0.2;
}
"""
	_dirt_mat = ShaderMaterial.new()
	_dirt_mat.shader = sh
	return _dirt_mat

static func set_wetness(w: float) -> void:
	for m in [_grass_mat, _dirt_mat]:
		if m:
			m.set_shader_parameter("wetness", w)
