class_name CarVisual
extends Node3D
## Carroceria estilizada montada com primitivas (hatch, sedã, van, picape).
## Frente = -Z. Expõe rodas (giram/esterçam), porta do motorista e luzes.

var wheels: Array[Node3D] = []
var front_wheels: Array[Node3D] = []
var driver_door: Node3D
var brake_mat: StandardMaterial3D
var head_mat: StandardMaterial3D
var body_mesh: MeshInstance3D
var smoke: CPUParticles3D

func build(kind: String, size: Array, color: Color) -> void:
	var w := float(size[0])
	var h := float(size[1])
	var l := float(size[2])
	var paint := Mat.color(color, 0.35, 0.25)
	var glass := Mat.color(Color(0.16, 0.22, 0.28), 0.1, 0.5)
	var trim := Mat.color(Color(0.12, 0.12, 0.13), 0.6)
	var lower_h := h * 0.45
	body_mesh = _box(Vector3(w, lower_h, l), Vector3(0, 0.32 + lower_h * 0.5, 0), paint)
	match kind:
		"van":
			_box(Vector3(w * 0.98, h - lower_h - 0.1, l * 0.82), Vector3(0, 0.32 + lower_h + (h - lower_h - 0.1) * 0.5, l * 0.08), paint)
			_box(Vector3(w * 0.9, (h - lower_h) * 0.5, 0.05), Vector3(0, 0.32 + lower_h + (h - lower_h) * 0.4, -l * 0.33), glass)
			for s in [-1.0, 1.0]:
				_box(Vector3(0.04, (h - lower_h) * 0.4, l * 0.18), Vector3(s * w * 0.495, 0.32 + lower_h + (h - lower_h) * 0.45, -l * 0.22), glass)
		"pickup":
			_box(Vector3(w * 0.96, h - lower_h, l * 0.38), Vector3(0, 0.32 + lower_h + (h - lower_h) * 0.5, -l * 0.08), paint)
			_box(Vector3(w * 0.9, (h - lower_h) * 0.6, 0.05), Vector3(0, 0.32 + lower_h + (h - lower_h) * 0.45, -l * 0.27), glass)
			_box(Vector3(w * 0.92, 0.35, l * 0.4), Vector3(0, 0.32 + lower_h + 0.1, l * 0.28), trim)
		_:
			var cab_l := l * (0.5 if kind == "hatch" else 0.48)
			var cab_z := l * (0.06 if kind == "hatch" else 0.02)
			_box(Vector3(w * 0.9, h - lower_h, cab_l), Vector3(0, 0.32 + lower_h + (h - lower_h) * 0.5, cab_z), paint)
			_box(Vector3(w * 0.84, (h - lower_h) * 0.7, 0.05), Vector3(0, 0.32 + lower_h + (h - lower_h) * 0.45, cab_z - cab_l * 0.5 - 0.01), glass)
			_box(Vector3(w * 0.84, (h - lower_h) * 0.65, 0.05), Vector3(0, 0.32 + lower_h + (h - lower_h) * 0.45, cab_z + cab_l * 0.5 + 0.01), glass)
			for s in [-1.0, 1.0]:
				_box(Vector3(0.04, (h - lower_h) * 0.6, cab_l * 0.85), Vector3(s * w * 0.451, 0.32 + lower_h + (h - lower_h) * 0.45, cab_z), glass)
	# Para-choques, faróis e lanternas
	_box(Vector3(w * 1.01, 0.16, 0.12), Vector3(0, 0.35, -l * 0.5), trim)
	_box(Vector3(w * 1.01, 0.16, 0.12), Vector3(0, 0.35, l * 0.5), trim)
	head_mat = StandardMaterial3D.new()
	head_mat.albedo_color = Color(1, 0.98, 0.9)
	head_mat.emission_enabled = true
	head_mat.emission = Color(1, 0.95, 0.8)
	head_mat.emission_energy_multiplier = 0.3
	brake_mat = StandardMaterial3D.new()
	brake_mat.albedo_color = Color(0.6, 0.05, 0.05)
	brake_mat.emission_enabled = true
	brake_mat.emission = Color(1, 0.1, 0.05)
	brake_mat.emission_energy_multiplier = 0.2
	for s in [-1.0, 1.0]:
		_box(Vector3(0.32, 0.14, 0.04), Vector3(s * w * 0.34, 0.32 + lower_h * 0.7, -l * 0.5 - 0.01), head_mat)
		_box(Vector3(0.3, 0.12, 0.04), Vector3(s * w * 0.36, 0.32 + lower_h * 0.7, l * 0.5 + 0.01), brake_mat)
	# Rodas
	var wb := l * 0.32
	for zs in [-wb, wb]:
		for s in [-1.0, 1.0]:
			var pivot := Node3D.new()
			pivot.position = Vector3(s * (w * 0.5 - 0.12), 0.32, zs)
			add_child(pivot)
			var tire := MeshInstance3D.new()
			var cm := CylinderMesh.new()
			cm.top_radius = 0.32
			cm.bottom_radius = 0.32
			cm.height = 0.24
			cm.radial_segments = 12
			tire.mesh = cm
			tire.rotation_degrees.z = 90
			tire.material_override = Mat.color(Color(0.08, 0.08, 0.09), 0.9)
			pivot.add_child(tire)
			var hub := MeshInstance3D.new()
			var hm := CylinderMesh.new()
			hm.top_radius = 0.17
			hm.bottom_radius = 0.17
			hm.height = 0.26
			hm.radial_segments = 8
			hub.mesh = hm
			hub.rotation_degrees.z = 90
			hub.material_override = Mat.color(Color(0.7, 0.72, 0.75), 0.3, 0.8)
			pivot.add_child(hub)
			wheels.append(pivot)
			if zs < 0:
				front_wheels.append(pivot)
	# Porta do motorista (lado esquerdo) — abre na entrada/saída.
	driver_door = Node3D.new()
	driver_door.position = Vector3(-w * 0.5, 0, -l * 0.12)
	add_child(driver_door)
	var dm := MeshInstance3D.new()
	var db := BoxMesh.new()
	db.size = Vector3(0.05, lower_h * 0.9, l * 0.26)
	dm.mesh = db
	dm.material_override = paint
	dm.position = Vector3(-0.01, 0.32 + lower_h * 0.5, l * 0.13)
	driver_door.add_child(dm)
	# Fumaça de motor avariado
	smoke = CPUParticles3D.new()
	smoke.emitting = false
	smoke.amount = 24
	smoke.lifetime = 1.8
	smoke.position = Vector3(0, 0.32 + lower_h, -l * 0.42)
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

func _box(size: Vector3, pos: Vector3, mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.material_override = mat
	mi.position = pos
	add_child(mi)
	return mi

func animate(speed: float, steer: float, delta: float) -> void:
	for w in wheels:
		w.get_child(0).rotation.x -= speed * delta / 0.32
	for w in front_wheels:
		w.rotation.y = steer

func set_lights(head: bool, braking: bool) -> void:
	head_mat.emission_energy_multiplier = 2.5 if head else 0.3
	brake_mat.emission_energy_multiplier = 3.0 if braking else (1.0 if head else 0.2)

func set_door(open: bool) -> void:
	var tw := create_tween()
	tw.tween_property(driver_door, "rotation:y", deg_to_rad(-65.0) if open else 0.0, 0.25)
