class_name CarVisual
extends Node3D
## Visual dos carros: modelos 3D próprios (assets/vehicles/car_<tipo>.glb, gerados por
## tools/art/blender/make_cars.py) com carroceria, vidros, faróis, interior e rodas
## detalhadas. Frente = -Z. Expõe rodas (giram/esterçam), porta do motorista e luzes.
## A pintura é recolorida por instância; faróis e lanternas acendem por material.

const MODELS := {
	"hatch": "res://assets/vehicles/car_hatch.glb",
	"sedan": "res://assets/vehicles/car_sedan.glb",
	"pickup": "res://assets/vehicles/car_pickup.glb",
	"van": "res://assets/vehicles/car_van.glb",
}
## Comprimento dos modelos exportados (m), para ajustar ao tamanho dos dados.
const MODEL_LENGTH := {"hatch": 3.9, "sedan": 4.4, "pickup": 4.9, "van": 4.8}
const WHEEL_RADIUS := {"hatch": 0.3, "sedan": 0.31, "pickup": 0.37, "van": 0.33}

static var _scenes: Dictionary = {}
static var _shared: Dictionary = {}   # materiais compartilhados ajustados (vidro, pneu…)
static var _paints: Dictionary = {}
static var _shadow_mat: ShaderMaterial

var wheels: Array[Node3D] = []        # pivôs (o filho 0 gira)
var front_wheels: Array[Node3D] = []
var driver_door: Node3D
var brake_mat: StandardMaterial3D
var head_mat: StandardMaterial3D
var body_mesh: MeshInstance3D
var smoke: CPUParticles3D
var wheel_radius := 0.32

static func _scene(kind: String) -> PackedScene:
	if not MODELS.has(kind):
		kind = "sedan"
	if not _scenes.has(kind):
		_scenes[kind] = load(MODELS[kind])
	return _scenes[kind]

func build(kind: String, size: Array, color: Color) -> void:
	if not MODELS.has(kind):
		kind = "sedan"
	var model: Node3D = _scene(kind).instantiate()
	model.name = "Model"
	var s := float(size[2]) / float(MODEL_LENGTH[kind])
	model.scale = Vector3.ONE * s
	add_child(model)
	wheel_radius = float(WHEEL_RADIUS[kind]) * s
	head_mat = _light_material(Color(1.0, 0.95, 0.82), 0.3)
	brake_mat = _light_material(Color(1.0, 0.08, 0.04), 0.2)
	_setup_meshes(model, color)
	for tag in ["FL", "FR", "RL", "RR"]:
		var pivot: Node3D = model.find_child("Wheel_" + tag, true, false)
		if pivot == null or pivot.get_child_count() == 0:
			continue
		wheels.append(pivot)
		if tag.begins_with("F"):
			front_wheels.append(pivot)
	driver_door = model.find_child("Door_FL", true, false)
	body_mesh = model.find_child("Body", true, false) as MeshInstance3D
	_add_contact_shadow(float(size[0]), float(size[2]))
	_add_smoke(float(size[1]), float(size[2]))

func _setup_meshes(node: Node, color: Color) -> void:
	if node is MeshInstance3D:
		var mi := node as MeshInstance3D
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		for i in mi.mesh.get_surface_count():
			var src := mi.mesh.surface_get_material(i) as StandardMaterial3D
			if src == null:
				continue
			mi.set_surface_override_material(i, _material_for(src, color))
	for c in node.get_children():
		_setup_meshes(c, color)

## Converte o material importado no material do jogo (cacheado sempre que possível).
func _material_for(src: StandardMaterial3D, color: Color) -> Material:
	var name := src.resource_name
	match name:
		"paint":
			var key := color.to_html()
			if not _paints.has(key):
				var m := src.duplicate() as StandardMaterial3D
				m.albedo_color = color
				m.metallic = 0.35
				m.roughness = 0.3
				m.metallic_specular = 0.6
				m.clearcoat_enabled = true
				m.clearcoat = 0.8
				m.clearcoat_roughness = 0.12
				m.vertex_color_use_as_albedo = true
				_paints[key] = m
			return _paints[key]
		"headlight", "indicator":
			return head_mat if name == "headlight" else _shared_mat(src)
		"taillight":
			return brake_mat
	return _shared_mat(src)

static func _shared_mat(src: StandardMaterial3D) -> Material:
	var name := src.resource_name
	if _shared.has(name):
		return _shared[name]
	var m := src.duplicate() as StandardMaterial3D
	m.vertex_color_use_as_albedo = true
	match name:
		"glass":
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			m.albedo_color = Color(0.07, 0.09, 0.11, 0.62)
			m.metallic = 0.4
			m.roughness = 0.04
			m.vertex_color_use_as_albedo = false
		"tire", "plastic", "bedliner", "seat", "interior":
			m.roughness = 0.9
		"indicator":
			m.emission_enabled = true
			m.emission = Color(1.0, 0.5, 0.05)
			m.emission_energy_multiplier = 0.15
	_shared[name] = m
	return m

func _light_material(c: Color, energy: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c.lerp(Color.WHITE, 0.15) if c.r > 0.9 and c.g > 0.9 else c * 0.6
	m.roughness = 0.1
	m.emission_enabled = true
	m.emission = c
	m.emission_energy_multiplier = energy
	return m

## Sombra de contato suave sob o carro (assenta o modelo no chão em qualquer renderizador).
func _add_contact_shadow(w: float, l: float) -> void:
	if _shadow_mat == null:
		_shadow_mat = ShaderMaterial.new()
		var sh := Shader.new()
		sh.code = """
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_back, shadows_disabled;
uniform float strength = 0.55;
void fragment() {
	vec2 d = abs(UV - 0.5) * 2.0;
	float r = length(max(d - vec2(0.55, 0.75), 0.0) / vec2(0.45, 0.25));
	float a = (1.0 - smoothstep(0.0, 1.0, r)) * strength;
	ALBEDO = vec3(0.0);
	ALPHA = a;
}
"""
		_shadow_mat.shader = sh
	var q := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(w + 0.5, l + 0.6)
	q.mesh = pm
	q.material_override = _shadow_mat
	q.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	q.position.y = 0.025
	q.name = "ContactShadow"
	add_child(q)

func _add_smoke(h: float, l: float) -> void:
	smoke = CPUParticles3D.new()
	smoke.emitting = false
	smoke.amount = 24
	smoke.lifetime = 1.8
	smoke.position = Vector3(0, h * 0.65, -l * 0.38)
	smoke.direction = Vector3(0, 1, 0)
	smoke.spread = 25
	smoke.initial_velocity_min = 0.6
	smoke.initial_velocity_max = 1.2
	smoke.gravity = Vector3(0, 0.4, 0)
	smoke.scale_amount_min = 0.4
	smoke.scale_amount_max = 0.9
	var sm := SphereMesh.new()
	sm.radius = 0.25
	sm.height = 0.5
	smoke.mesh = sm
	var smat := StandardMaterial3D.new()
	smat.albedo_color = Color(0.25, 0.25, 0.25, 0.45)
	smat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	smat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	smoke.material_override = smat
	add_child(smoke)

func animate(speed: float, steer: float, delta: float) -> void:
	for w in wheels:
		w.get_child(0).rotation.x -= speed * delta / wheel_radius
	for w in front_wheels:
		w.rotation.y = steer

func set_lights(head: bool, braking: bool) -> void:
	head_mat.emission_energy_multiplier = 3.0 if head else 0.3
	brake_mat.emission_energy_multiplier = 3.5 if braking else (1.2 if head else 0.2)

func set_door(open: bool) -> void:
	if driver_door == null:
		return
	var tw := create_tween()
	tw.tween_property(driver_door, "rotation:y", deg_to_rad(-65.0) if open else 0.0, 0.25)
