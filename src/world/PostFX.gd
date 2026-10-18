class_name PostFX
extends CanvasLayer
## Pós-processamento da direção "diorama dourado": desfoque de miniatura (tilt-shift)
## em faixas acima e abaixo do foco, gradação de cor quente/fria e vinheta.
## Fica abaixo da interface (camada 5 < HUD 10), então o HUD nunca desfoca.

var rect: ColorRect
var mat: ShaderMaterial

func _ready() -> void:
	layer = 5
	rect = ColorRect.new()
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	mat = ShaderMaterial.new()
	var sh := Shader.new()
	sh.code = """
shader_type canvas_item;
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;
uniform float focus_center = 0.52;
uniform float focus_width = 0.16;
uniform float blur = 1.0;
uniform float grading = 1.0;
uniform float vignette = 0.32;
uniform vec3 shadow_tint : source_color = vec3(0.86, 0.95, 1.02);
uniform vec3 highlight_tint : source_color = vec3(1.0, 1.0, 1.0);
uniform float saturation = 1.08;
const vec2 TAPS[12] = vec2[](
	vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
	vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
	vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));
void fragment() {
	vec2 uv = SCREEN_UV;
	float d = clamp((abs(uv.y - focus_center) - focus_width) / max(0.001, 0.5 - focus_width), 0.0, 1.0);
	d = d * d * (3.0 - 2.0 * d);
	float radius = d * blur;
	vec3 col = textureLod(screen_tex, uv, 0.0).rgb;
	if (radius > 0.02) {
		float lod = radius * 2.2;
		vec2 px = SCREEN_PIXEL_SIZE * radius * 9.0;
		vec3 acc = textureLod(screen_tex, uv, lod).rgb;
		for (int i = 0; i < 12; i++) {
			acc += textureLod(screen_tex, uv + TAPS[i] * px, lod).rgb;
		}
		col = acc / 13.0;
	}
	float l = dot(col, vec3(0.299, 0.587, 0.114));
	vec3 graded = mix(col * shadow_tint, col * highlight_tint, smoothstep(0.15, 0.85, l));
	graded = mix(vec3(l), graded, saturation);
	graded = (graded - 0.5) * 1.05 + 0.5;
	col = mix(col, graded, grading);
	vec2 c = uv - 0.5;
	col *= 1.0 - vignette * dot(c, c) * 2.0;
	COLOR = vec4(col, 1.0);
}
"""
	mat.shader = sh
	rect.material = mat
	add_child(rect)
	apply_settings()
	Settings.changed.connect(func(_s): apply_settings())

## Humor da cena conforme hora e clima (chamado pelo ciclo dia/noite):
## dourado só no amanhecer/entardecer, neutro de dia, frio à noite, cinza na chuva.
func set_mood(highlight: Color, shadow: Color, saturation: float) -> void:
	mat.set_shader_parameter("highlight_tint", Vector3(highlight.r, highlight.g, highlight.b))
	mat.set_shader_parameter("shadow_tint", Vector3(shadow.r, shadow.g, shadow.b))
	mat.set_shader_parameter("saturation", saturation)

func apply_settings() -> void:
	var ts: int = Settings.get_value("graphics", "tilt_shift")
	var grading: bool = Settings.get_value("graphics", "color_grading")
	mat.set_shader_parameter("blur", [0.0, 0.7, 1.25][clampi(ts, 0, 2)])
	mat.set_shader_parameter("grading", 1.0 if grading else 0.0)
	mat.set_shader_parameter("vignette", 0.32 if grading else 0.0)
	rect.visible = ts > 0 or grading
