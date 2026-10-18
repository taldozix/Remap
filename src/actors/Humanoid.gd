class_name Humanoid
extends Node3D
## Personagem estilizado montado com primitivas e animado proceduralmente
## (andar, correr, sentar, deitar, trabalhar, conversar, caído).
## Frente = -Z. A raiz fica nos pés.

var appearance: Dictionary = {}
var pose := "idle"        # idle, walk, run, sit, lie, knocked, work, talk, eat, search
var speed := 0.0          # m/s (para cadência da caminhada)
var height_scale := 1.0
var _phase := 0.0
var _t := 0.0
var lod_far := false

var body: Node3D
var hips: Node3D
var torso: Node3D
var head: Node3D
var arm_l: Node3D
var arm_r: Node3D
var leg_l: Node3D
var leg_r: Node3D
var knee_l: Node3D
var knee_r: Node3D
var forearm_l: Node3D
var forearm_r: Node3D

const LIE_BASIS := Basis(Vector3(-1, 0, 0), Vector3(0, 0, -1), Vector3(0, -1, 0))

func build(app: Dictionary) -> void:
	appearance = app
	height_scale = float(app.get("height", 1.0))
	var build_w := float(app.get("build", 1.0))
	var skin := Color(app.get("skin", "#d9a98a"))
	var shirt := Color(app.get("shirt", "#4477aa"))
	var pants := Color(app.get("pants", "#333344"))
	var hair := Color(app.get("hair", "#2a1d14"))
	var shoes := Color(app.get("shoes", "#2a2420"))
	body = Node3D.new()
	body.scale = Vector3(build_w, height_scale, build_w)
	add_child(body)
	hips = Node3D.new()
	hips.position = Vector3(0, 0.92, 0)
	body.add_child(hips)
	torso = Node3D.new()
	hips.add_child(torso)
	var t := _part(CylinderMesh.new(), shirt, torso, Vector3(0, 0.3, 0))
	(t.mesh as CylinderMesh).top_radius = 0.2
	(t.mesh as CylinderMesh).bottom_radius = 0.17
	(t.mesh as CylinderMesh).height = 0.6
	t.scale = Vector3(1.0, 1.0, 0.62)
	var pelvis := _part(CylinderMesh.new(), pants, hips, Vector3(0, -0.02, 0))
	(pelvis.mesh as CylinderMesh).top_radius = 0.175
	(pelvis.mesh as CylinderMesh).bottom_radius = 0.17
	(pelvis.mesh as CylinderMesh).height = 0.16
	pelvis.scale = Vector3(1.0, 1.0, 0.65)
	if app.has("apron"):
		var ap := _part(BoxMesh.new(), Color(app.apron), torso, Vector3(0, 0.12, -0.115))
		(ap.mesh as BoxMesh).size = Vector3(0.3, 0.5, 0.02)
	if app.has("backpack"):
		var bp := _part(BoxMesh.new(), Color(app.backpack), torso, Vector3(0, 0.3, 0.17))
		(bp.mesh as BoxMesh).size = Vector3(0.3, 0.38, 0.14)
	if app.get("jacket", false):
		var col := _part(BoxMesh.new(), shirt.darkened(0.25), torso, Vector3(0, 0.6, 0.0))
		(col.mesh as BoxMesh).size = Vector3(0.34, 0.07, 0.24)
	if app.get("overalls", false):
		for x in [-0.09, 0.09]:
			var st := _part(BoxMesh.new(), pants, torso, Vector3(x, 0.35, -0.115))
			(st.mesh as BoxMesh).size = Vector3(0.05, 0.45, 0.02)
	# Pescoço e cabeça
	var neck := _part(CylinderMesh.new(), skin, torso, Vector3(0, 0.64, 0))
	(neck.mesh as CylinderMesh).top_radius = 0.06
	(neck.mesh as CylinderMesh).bottom_radius = 0.07
	(neck.mesh as CylinderMesh).height = 0.1
	head = Node3D.new()
	head.position = Vector3(0, 0.79, 0)
	torso.add_child(head)
	var hm := _part(SphereMesh.new(), skin, head, Vector3.ZERO)
	(hm.mesh as SphereMesh).radius = 0.13
	(hm.mesh as SphereMesh).height = 0.28
	hm.scale = Vector3(0.95, 1.05, 1.0)
	for x in [-0.045, 0.045]:
		var e := _part(SphereMesh.new(), Color("1b1410"), head, Vector3(x, 0.02, -0.118))
		(e.mesh as SphereMesh).radius = 0.018
		(e.mesh as SphereMesh).height = 0.036
	var nose := _part(BoxMesh.new(), skin.darkened(0.08), head, Vector3(0, -0.02, -0.13))
	(nose.mesh as BoxMesh).size = Vector3(0.03, 0.05, 0.03)
	_build_hair(app.get("hair_style", "short"), hair)
	if app.get("beard", false):
		var bd := _part(SphereMesh.new(), hair, head, Vector3(0, -0.07, -0.05))
		(bd.mesh as SphereMesh).radius = 0.1
		(bd.mesh as SphereMesh).height = 0.12
		bd.scale = Vector3(1.05, 0.8, 0.9)
	if app.get("mustache", false):
		var mu := _part(BoxMesh.new(), Color("6a6a6a"), head, Vector3(0, -0.055, -0.125))
		(mu.mesh as BoxMesh).size = Vector3(0.08, 0.02, 0.02)
	if app.has("cap"):
		var cap := _part(SphereMesh.new(), Color(app.cap), head, Vector3(0, 0.07, 0))
		(cap.mesh as SphereMesh).radius = 0.135
		(cap.mesh as SphereMesh).height = 0.14
		(cap.mesh as SphereMesh).is_hemisphere = true
		var brim := _part(BoxMesh.new(), Color(app.cap), head, Vector3(0, 0.07, -0.15))
		(brim.mesh as BoxMesh).size = Vector3(0.2, 0.02, 0.12)
	if app.has("beanie"):
		var bn := _part(SphereMesh.new(), Color(app.beanie), head, Vector3(0, 0.05, 0))
		(bn.mesh as SphereMesh).radius = 0.14
		(bn.mesh as SphereMesh).height = 0.16
		(bn.mesh as SphereMesh).is_hemisphere = true
	# Braços (ombro → cotovelo → mão)
	arm_l = _limb(torso, Vector3(-0.235, 0.55, 0), shirt, 0.055, 0.3)
	arm_r = _limb(torso, Vector3(0.235, 0.55, 0), shirt, 0.055, 0.3)
	forearm_l = _limb(arm_l, Vector3(0, -0.3, 0), skin, 0.045, 0.28)
	forearm_r = _limb(arm_r, Vector3(0, -0.3, 0), skin, 0.045, 0.28)
	for fa in [forearm_l, forearm_r]:
		var hand := _part(SphereMesh.new(), skin, fa, Vector3(0, -0.31, 0))
		(hand.mesh as SphereMesh).radius = 0.05
		(hand.mesh as SphereMesh).height = 0.1
	# Pernas (quadril → joelho → pé)
	leg_l = _limb(hips, Vector3(-0.095, -0.04, 0), pants, 0.08, 0.44)
	leg_r = _limb(hips, Vector3(0.095, -0.04, 0), pants, 0.08, 0.44)
	knee_l = _limb(leg_l, Vector3(0, -0.44, 0), pants, 0.07, 0.42)
	knee_r = _limb(leg_r, Vector3(0, -0.44, 0), pants, 0.07, 0.42)
	for k in [knee_l, knee_r]:
		var shoe := _part(BoxMesh.new(), shoes, k, Vector3(0, -0.43, -0.04))
		(shoe.mesh as BoxMesh).size = Vector3(0.11, 0.07, 0.25)

func _build_hair(style: String, hair: Color) -> void:
	match style:
		"bald":
			return
		"short", "slick":
			var h := _part(SphereMesh.new(), hair, head, Vector3(0, 0.035, 0.012))
			(h.mesh as SphereMesh).radius = 0.138
			(h.mesh as SphereMesh).height = 0.2
			(h.mesh as SphereMesh).is_hemisphere = true
			h.scale = Vector3(1.0, 0.8 if style == "slick" else 1.0, 1.05)
		"curly", "messy":
			var c := _part(SphereMesh.new(), hair, head, Vector3(0, 0.06, 0.01))
			(c.mesh as SphereMesh).radius = 0.16
			(c.mesh as SphereMesh).height = 0.24
			c.scale = Vector3(1.05, 0.85, 1.05)
		"long", "bob":
			var top := _part(SphereMesh.new(), hair, head, Vector3(0, 0.035, 0.015))
			(top.mesh as SphereMesh).radius = 0.142
			(top.mesh as SphereMesh).height = 0.2
			(top.mesh as SphereMesh).is_hemisphere = true
			var back := _part(BoxMesh.new(), hair, head, Vector3(0, -0.08 if style == "long" else -0.02, 0.07))
			(back.mesh as BoxMesh).size = Vector3(0.27, 0.32 if style == "long" else 0.18, 0.14)
		"bun", "ponytail":
			var top2 := _part(SphereMesh.new(), hair, head, Vector3(0, 0.035, 0.012))
			(top2.mesh as SphereMesh).radius = 0.14
			(top2.mesh as SphereMesh).height = 0.2
			(top2.mesh as SphereMesh).is_hemisphere = true
			var bun := _part(SphereMesh.new(), hair, head, Vector3(0, 0.1 if style == "bun" else -0.02, 0.13))
			(bun.mesh as SphereMesh).radius = 0.065
			(bun.mesh as SphereMesh).height = 0.13
			if style == "ponytail":
				bun.scale = Vector3(0.8, 1.8, 0.8)

func _part(mesh: Mesh, c: Color, parent: Node3D, pos: Vector3) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = Mat.color(c, 0.8)
	mi.position = pos
	parent.add_child(mi)
	return mi

func _limb(parent: Node3D, pivot_pos: Vector3, c: Color, radius: float, length: float) -> Node3D:
	var pivot := Node3D.new()
	pivot.position = pivot_pos
	parent.add_child(pivot)
	var cap := CapsuleMesh.new()
	cap.radius = radius
	cap.height = length + radius * 2.0
	cap.radial_segments = 8
	cap.rings = 2
	_part(cap, c, pivot, Vector3(0, -length * 0.5, 0))
	return pivot

func set_pose(p: String) -> void:
	if p == pose:
		return
	pose = p
	_reset_limbs()

func _reset_limbs() -> void:
	body.transform = Transform3D(Basis().scaled(Vector3(float(appearance.get("build", 1.0)), height_scale, float(appearance.get("build", 1.0)))), Vector3.ZERO)
	for n in [hips, torso, head, arm_l, arm_r, leg_l, leg_r, knee_l, knee_r, forearm_l, forearm_r]:
		n.rotation = Vector3.ZERO
	hips.position = Vector3(0, 0.92, 0)

func _process(delta: float) -> void:
	if body == null or lod_far or not is_visible_in_tree():
		return
	_t += delta
	var elderly: bool = appearance.get("elderly", false)
	match pose:
		"walk", "run":
			var running := pose == "run"
			var stride := 1.4 if running else 1.15
			_phase += delta * max(speed, 0.8) / stride * PI
			var a := (0.85 if running else 0.5) * (0.7 if elderly else 1.0)
			var s := sin(_phase)
			leg_l.rotation.x = s * a
			leg_r.rotation.x = -s * a
			knee_l.rotation.x = -max(0.0, -s) * a * 1.3 - (0.15 if running else 0.0)
			knee_r.rotation.x = -max(0.0, s) * a * 1.3 - (0.15 if running else 0.0)
			arm_l.rotation.x = -s * a * 0.8
			arm_r.rotation.x = s * a * 0.8
			forearm_l.rotation.x = 0.25 + (0.9 if running else 0.15)
			forearm_r.rotation.x = 0.25 + (0.9 if running else 0.15)
			hips.position.y = 0.92 + absf(cos(_phase)) * (0.06 if running else 0.025)
			torso.rotation.x = 0.18 if running else (0.15 if elderly else 0.03)
			arm_l.rotation.z = 0.08
			arm_r.rotation.z = -0.08
		"idle", "talk", "eat", "search":
			hips.position.y = 0.92
			torso.rotation.x = (0.12 if elderly else 0.0) + sin(_t * 1.6) * 0.012
			arm_l.rotation.z = 0.1
			arm_r.rotation.z = -0.1
			leg_l.rotation.x = 0.0
			leg_r.rotation.x = 0.0
			knee_l.rotation.x = 0.0
			knee_r.rotation.x = 0.0
			arm_l.rotation.x = 0.0
			arm_r.rotation.x = 0.0
			forearm_l.rotation.x = 0.0
			forearm_r.rotation.x = 0.0
			if pose == "talk":
				arm_r.rotation.x = -0.4 + sin(_t * 3.0) * 0.25
				forearm_r.rotation.x = -0.9
				head.rotation.y = sin(_t * 0.9) * 0.15
			elif pose == "eat":
				arm_r.rotation.x = -0.6 + absf(sin(_t * 1.5)) * -0.5
				forearm_r.rotation.x = -1.6
			elif pose == "search":
				torso.rotation.x = 0.55
				arm_l.rotation.x = -0.9 + sin(_t * 4.0) * 0.2
				arm_r.rotation.x = -0.9 - sin(_t * 4.0) * 0.2
		"work":
			hips.position.y = 0.92
			torso.rotation.x = 0.1
			arm_l.rotation.x = -0.75 + sin(_t * 5.0) * 0.12
			arm_r.rotation.x = -0.75 - sin(_t * 5.0 + 1.0) * 0.12
			forearm_l.rotation.x = -0.7
			forearm_r.rotation.x = -0.7
			leg_l.rotation.x = 0.0
			leg_r.rotation.x = 0.0
			knee_l.rotation.x = 0.0
			knee_r.rotation.x = 0.0
		"sit":
			hips.position.y = 0.47
			leg_l.rotation.x = PI * 0.5
			leg_r.rotation.x = PI * 0.5
			knee_l.rotation.x = -PI * 0.5
			knee_r.rotation.x = -PI * 0.5
			arm_l.rotation.x = -0.35
			arm_r.rotation.x = -0.35
			forearm_l.rotation.x = -0.6
			forearm_r.rotation.x = -0.6
			torso.rotation.x = -0.05 + sin(_t * 1.6) * 0.01
		"lie", "knocked":
			body.basis = LIE_BASIS.scaled(Vector3(float(appearance.get("build", 1.0)), height_scale, float(appearance.get("build", 1.0))))
			body.position = Vector3(0, 0.0, 0.0)
			arm_l.rotation.z = 0.15
			arm_r.rotation.z = -0.15
			if pose == "knocked":
				knee_l.rotation.x = -0.6
				arm_r.rotation.x = -0.8
