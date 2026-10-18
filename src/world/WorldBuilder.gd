class_name WorldBuilder
extends RefCounted
## Constrói o bairro a partir de data/map/*.json: chão, ruas, calçadas, marcações,
## prédios (paredes com corte, telhado, piso, portas, janelas), mobília funcional,
## árvores, postes e mobiliário urbano. Depois gera a malha de navegação dos NPCs.

const WALL_T := 0.2
const DOOR_H := 2.15
const SIDEWALK_H := 0.12
const STOP_DIST := 8.2   # distância do centro do cruzamento até a faixa de retenção

const WOOD := Color("e6cba8")       # tintas sobre a textura de madeira
const WOOD_DARK := Color("a98663")
const WHITE := Color("eeeeea")
const STEEL := Color("b8bcc2")
const DARK := Color("2a2a2e")

var world: Node3D
var map: Dictionary
var static_body: StaticBody3D
var visual_root: Node3D
var deco := ChunkBatcher.new()
var markings := ChunkBatcher.new()
var lamp_heads := ChunkBatcher.new()
var sidewalks := ChunkBatcher.new()
var asphalt_b := ChunkBatcher.new()
var curbs := ChunkBatcher.new()
var foliage := FoliageBatcher.new()
var sidewalk_rects: Array[Rect2] = []
var spots: Dictionary = {}
var buildings: Dictionary = {}
var doors: Dictionary = {}
var furniture: Dictionary = {}
var streetlights: Array[OmniLight3D] = []
var rng := RandomNumberGenerator.new()

func build(p_world: Node3D, p_map: Dictionary, nav_region: NavigationRegion3D) -> void:
	world = p_world
	map = p_map
	rng.seed = 1337
	static_body = StaticBody3D.new()
	static_body.name = "WorldStatic"
	static_body.collision_layer = 1
	static_body.collision_mask = 0
	nav_region.add_child(static_body)
	visual_root = Node3D.new()
	visual_root.name = "Visual"
	world.add_child(visual_root)
	_build_ground()
	_build_roads()
	_build_areas()
	for b in map.get("buildings", []):
		_build_building(b)
	for p in map.get("props", []):
		_build_prop(p)
	for sid in map.get("spots", {}):
		var s: Dictionary = map.spots[sid]
		_add_spot(sid, Vector3(float(s.pos[0]), 0, float(s.pos[1])), float(s.get("rot", 0)), s.get("pose", "stand"), bool(s.get("hidden", false)), "")
	deco.build(visual_root, "Deco", 0.85)
	foliage.build(visual_root)
	_build_street_furniture()
	sidewalks.build(visual_root, "Sidewalks", 0.8, true, Mat.named("sidewalk"))
	asphalt_b.build(visual_root, "Asphalt", 0.9, false, Mat.named("asphalt"))
	curbs.build(visual_root, "Curbs", 0.85, false)
	markings.build(visual_root, "Markings", 0.6, false)
	lamp_heads.build(visual_root, "LampHeads", 0.3, false, Mat.lamps())
	_bake_navigation(nav_region)

func add_collider(center: Vector3, size: Vector3, rot_deg: float = 0.0) -> void:
	var cs := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	cs.shape = box
	cs.transform = Transform3D(Basis(Vector3.UP, deg_to_rad(rot_deg)), center)
	static_body.add_child(cs)

func add_cyl_collider(center: Vector3, radius: float, height: float) -> void:
	var cs := CollisionShape3D.new()
	var c := CylinderShape3D.new()
	c.radius = radius
	c.height = height
	cs.shape = c
	cs.position = center
	static_body.add_child(cs)

func _mesh(mesh: Mesh, mat: Material, pos: Vector3, parent: Node3D = null) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.position = pos
	(parent if parent else visual_root).add_child(mi)
	return mi

## Altura do piso (calçadas são 12 cm mais altas) — usada pelo visual de pessoas e carros.
func ground_height(x: float, z: float) -> float:
	for r in sidewalk_rects:
		if x >= r.position.x and x <= r.end.x and z >= r.position.y and z <= r.end.y:
			return SIDEWALK_H
	return 0.0

# ------------------------------------------------------------------ chão e ruas
func _build_ground() -> void:
	var b: Array = map.get("bounds", [-85, -85, 85, 85])
	var size := Vector2(float(b[2]) - float(b[0]) + 60.0, float(b[3]) - float(b[1]) + 60.0)
	var tiles := int(ceil(size.x / ChunkBatcher.CHUNK))
	var tile := size.x / tiles
	for i in tiles:
		for j in tiles:
			var pm := PlaneMesh.new()
			pm.size = Vector2(tile, tile)
			var g := _mesh(pm, Mat.grass(), Vector3(-size.x * 0.5 + tile * (i + 0.5), 0, -size.y * 0.5 + tile * (j + 0.5)))
			g.name = "Ground_%d_%d" % [i, j]
			g.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_collider(Vector3(0, -0.5, 0), Vector3(size.x, 1.0, size.y))
	# Limites do mundo: cercas e paredes invisíveis.
	var lim := float(b[2]) - 1.0
	for s in [-1.0, 1.0]:
		add_collider(Vector3(s * lim, 2, 0), Vector3(1, 4, lim * 2))
		add_collider(Vector3(0, 2, s * lim), Vector3(lim * 2, 4, 1))
		for i in range(-int(lim), int(lim), 3):
			deco.box(Vector3(s * (lim - 0.5), 0.6, i), Vector3(0.08, 1.2, 0.08), WOOD_DARK, 0.0, "wood_out")
			deco.box(Vector3(i, 0.6, s * (lim - 0.5)), Vector3(0.08, 1.2, 0.08), WOOD_DARK, 0.0, "wood_out")
		deco.long_box(Vector3(s * (lim - 0.5), 0.95, 0), Vector3(0.05, 0.08, lim * 2), WOOD, "wood_out")
		deco.long_box(Vector3(0, 0.95, s * (lim - 0.5)), Vector3(lim * 2, 0.08, 0.05), WOOD, "wood_out")
		deco.long_box(Vector3(s * (lim - 0.5), 0.45, 0), Vector3(0.05, 0.08, lim * 2), WOOD, "wood_out")
		deco.long_box(Vector3(0, 0.45, s * (lim - 0.5)), Vector3(lim * 2, 0.08, 0.05), WOOD, "wood_out")

func _build_roads() -> void:
	var r: Dictionary = map.roads
	var xs: Array = r.xs
	var zs: Array = r.zs
	var hw := float(r.width) * 0.5
	var sw := float(r.sidewalk)
	var x_min := float(xs[0]) - hw
	var x_max := float(xs[-1]) + hw
	var z_min := float(zs[0]) - hw
	var z_max := float(zs[-1]) + hw
	# Trechos entre cruzamentos + quadrados dos cruzamentos (sem sobreposição).
	for z in zs:
		for i in xs.size() - 1:
			var a0 := float(xs[i]) + hw
			var a1 := float(xs[i + 1]) - hw
			asphalt_b.long_box(Vector3((a0 + a1) * 0.5, 0.0, float(z)), Vector3(a1 - a0, 0.02, hw * 2), Color.WHITE)
	for x in xs:
		for i in zs.size() - 1:
			var a := float(zs[i]) + hw
			var bz := float(zs[i + 1]) - hw
			asphalt_b.long_box(Vector3(float(x), 0.0, (a + bz) * 0.5), Vector3(hw * 2, 0.02, bz - a), Color.WHITE)
		for z2 in zs:
			asphalt_b.box(Vector3(float(x), 0.0, float(z2)), Vector3(hw * 2, 0.02, hw * 2), Color.WHITE)
	# Calçadas: anel em volta de cada quarteirão + anel externo.
	for i in xs.size() - 1:
		for j in zs.size() - 1:
			var x0 := float(xs[i]) + hw
			var x1 := float(xs[i + 1]) - hw
			var z0 := float(zs[j]) + hw
			var z1 := float(zs[j + 1]) - hw
			_sidewalk(Rect2(x0, z0, x1 - x0, sw))
			_sidewalk(Rect2(x0, z1 - sw, x1 - x0, sw))
			_curb_rect(x0, z0, x1, z1)
			_sidewalk(Rect2(x0, z0 + sw, sw, z1 - z0 - 2 * sw))
			_sidewalk(Rect2(x1 - sw, z0 + sw, sw, z1 - z0 - 2 * sw))
	_sidewalk(Rect2(x_min - sw, z_min - sw, x_max - x_min + 2 * sw, sw))
	_sidewalk(Rect2(x_min - sw, z_max, x_max - x_min + 2 * sw, sw))
	_sidewalk(Rect2(x_min - sw, z_min, sw, z_max - z_min))
	_sidewalk(Rect2(x_max, z_min, sw, z_max - z_min))
	_curb_rect(x_min, z_min, x_max, z_max)
	# Marcações: linha central tracejada amarela, bordas brancas, retenção e faixas de pedestre.
	var yellow := Color(0.93, 0.76, 0.2)
	var white := Color(0.92, 0.92, 0.9)
	for z in zs:
		for i in xs.size() - 1:
			_road_markings(Vector3(float(xs[i]), 0, float(z)), Vector3(float(xs[i + 1]), 0, float(z)), hw, yellow, white)
	for x in xs:
		for i in zs.size() - 1:
			_road_markings(Vector3(float(x), 0, float(zs[i])), Vector3(float(x), 0, float(zs[i + 1])), hw, yellow, white)

func _sidewalk(rect: Rect2) -> void:
	if rect.size.x <= 0.01 or rect.size.y <= 0.01:
		return
	sidewalk_rects.append(rect)
	sidewalks.long_box(Vector3(rect.get_center().x, SIDEWALK_H * 0.5, rect.get_center().y), Vector3(rect.size.x, SIDEWALK_H, rect.size.y), Color.WHITE)

## Meio-fio de concreto em volta de um retângulo (borda da calçada voltada para a rua).
func _curb_rect(x0: float, z0: float, x1: float, z1: float) -> void:
	var c := Color(0.8, 0.79, 0.76)
	var t := 0.14
	curbs.long_box(Vector3((x0 + x1) * 0.5, SIDEWALK_H * 0.5 + 0.005, z0 + t * 0.5), Vector3(x1 - x0, SIDEWALK_H + 0.01, t), c, "curb")
	curbs.long_box(Vector3((x0 + x1) * 0.5, SIDEWALK_H * 0.5 + 0.005, z1 - t * 0.5), Vector3(x1 - x0, SIDEWALK_H + 0.01, t), c, "curb")
	curbs.long_box(Vector3(x0 + t * 0.5, SIDEWALK_H * 0.5 + 0.005, (z0 + z1) * 0.5), Vector3(t, SIDEWALK_H + 0.01, z1 - z0 - 2 * t), c, "curb")
	curbs.long_box(Vector3(x1 - t * 0.5, SIDEWALK_H * 0.5 + 0.005, (z0 + z1) * 0.5), Vector3(t, SIDEWALK_H + 0.01, z1 - z0 - 2 * t), c, "curb")

func _road_markings(a: Vector3, b: Vector3, hw: float, yellow: Color, white: Color) -> void:
	var d := (b - a).normalized()
	var right := Vector3(-d.z, 0, d.x)
	var length := a.distance_to(b)
	var rot := rad_to_deg(atan2(-d.x, -d.z))
	var y := 0.013
	var t := STOP_DIST + 1.0
	while t < length - STOP_DIST - 1.0:
		markings.box(a + d * (t + 1.0) + Vector3(0, y, 0), Vector3(0.14, 0.004, 2.0), yellow, rot)
		t += 4.5
	var inner := length - 2 * 4.5
	for s in [-1.0, 1.0]:
		markings.box((a + b) * 0.5 + right * s * (hw - 0.35) + Vector3(0, y, 0), Vector3(0.1, 0.004, inner), white, rot)
	# Faixas de pedestre e linhas de retenção nas duas pontas.
	for end in [[a, d], [b, -d]]:
		var c: Vector3 = end[0]
		var dir: Vector3 = end[1]
		var r2 := Vector3(-dir.z, 0, dir.x)
		for k in range(-4, 5):
			markings.box(c + dir * 6.1 + r2 * (k * 0.95) + Vector3(0, y, 0), Vector3(0.5, 0.004, 2.4), white, rot)
		markings.box(c + dir * (STOP_DIST - 0.4) + r2 * (-hw * 0.5) + Vector3(0, y, 0), Vector3(hw - 0.2, 0.004, 0.35), white, rot + 90.0)

func _build_areas() -> void:
	for a in map.get("areas", []):
		match a.type:
			"asphalt":
				var r: Array = a.rect
				var bm := BoxMesh.new()
				bm.size = Vector3(float(r[2]) - float(r[0]), 0.02, float(r[3]) - float(r[1]))
				_mesh(bm, Mat.named("asphalt"), Vector3((float(r[0]) + float(r[2])) * 0.5, 0.0, (float(r[1]) + float(r[3])) * 0.5)).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				_parking_lines(r)
			"plaza", "path":
				var r2: Array = a.rect
				var bm2 := BoxMesh.new()
				bm2.size = Vector3(float(r2[2]) - float(r2[0]), 0.03, float(r2[3]) - float(r2[1]))
				_mesh(bm2, Mat.named("plaza" if a.type == "plaza" else "sidewalk"), Vector3((float(r2[0]) + float(r2[2])) * 0.5, 0.0, (float(r2[1]) + float(r2[3])) * 0.5)).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			"trail":
				var tp: Array = a.points
				var tw := float(a.get("width", 2.2))
				for i in tp.size() - 1:
					var q0 := Vector3(float(tp[i][0]), 0, float(tp[i][1]))
					var q1 := Vector3(float(tp[i + 1][0]), 0, float(tp[i + 1][1]))
					var pm := PlaneMesh.new()
					pm.size = Vector2(tw, q0.distance_to(q1) + tw * 0.6)
					var tm := _mesh(pm, Nature.dirt_material(), (q0 + q1) * 0.5)
					tm.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
					tm.look_at((q0 + q1) * 0.5 + (q1 - q0).normalized(), Vector3.UP)
					tm.position.y = 0.012 + i * 0.0005
			"drive_lane":
				var pts: Array = a.points
				for i in pts.size() - 1:
					var p0 := Vector3(float(pts[i][0]), 0, float(pts[i][1]))
					var p1 := Vector3(float(pts[i + 1][0]), 0, float(pts[i + 1][1]))
					var len := p0.distance_to(p1)
					var bm3 := BoxMesh.new()
					bm3.size = Vector3(4.0, 0.024, len + 4.0)
					var mi := _mesh(bm3, Mat.named("asphalt"), (p0 + p1) * 0.5)
					mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
					mi.look_at(p1 + Vector3(0, 0, 0.0001), Vector3.UP)
					mi.position.y = 0.0

## Vagas pintadas nos estacionamentos (linhas ao longo do lado maior).
func _parking_lines(r: Array) -> void:
	var x0 := float(r[0])
	var z0 := float(r[1])
	var x1 := float(r[2])
	var z1 := float(r[3])
	var white := Color(0.88, 0.88, 0.85)
	if x1 - x0 >= z1 - z0:
		var x := x0 + 0.3
		while x <= x1 - 0.3:
			markings.box(Vector3(x, 0.013, z0 + 2.6), Vector3(0.1, 0.004, 4.6), white)
			x += 2.6
	else:
		var z := z0 + 0.3
		while z <= z1 - 0.3:
			markings.box(Vector3(x0 + 2.6, 0.013, z), Vector3(4.6, 0.004, 0.1), white)
			z += 2.6

## Mobiliário urbano decorativo: postes de energia com fiação, hidrantes e bueiros.
## Só objetos fora das faixas de circulação (junto ao meio-fio, longe de cruzamentos,
## entradas de garagem e portas).
func _build_street_furniture() -> void:
	var r: Dictionary = map.roads
	var xs: Array = r.xs
	var zs: Array = r.zs
	var hw := float(r.width) * 0.5
	var blocked: Array[Vector3] = []
	for a in map.get("areas", []):
		if a.type == "drive_lane":
			for q in a.points:
				blocked.append(Vector3(float(q[0]), 0, float(q[1])))
		elif a.type == "asphalt":
			var rr: Array = a.rect
			for t in 5:
				blocked.append(Vector3(lerpf(float(rr[0]), float(rr[2]), t / 4.0), 0, lerpf(float(rr[1]), float(rr[3]), t / 4.0)))
	for b in map.get("buildings", []):
		for dd in b.get("doors", []):
			blocked.append(Vector3(float(b.center[0]), 0, float(b.center[1])) + {"n": Vector3(0, 0, -1), "s": Vector3(0, 0, 1), "e": Vector3(1, 0, 0), "w": Vector3(-1, 0, 0)}[dd.side] * (float(b.size[1 if dd.side in ["n", "s"] else 0]) * 0.5) + (Vector3(float(dd.get("offset", 0)), 0, 0) if dd.side in ["n", "s"] else Vector3(0, 0, float(dd.get("offset", 0)))))
	for p in map.get("props", []):
		if p.has("pos"):
			blocked.append(Vector3(float(p.pos[0]), 0, float(p.pos[1])))
	var free := func(q: Vector3, rad: float) -> bool:
		for bq in blocked:
			if Vector2(bq.x - q.x, bq.z - q.z).length() < rad:
				return false
		for x in xs:
			for z in zs:
				if absf(q.x - float(x)) < hw + 4.0 and absf(q.z - float(z)) < hw + 4.0:
					return false
		return true
	var wood_pole := Color(0.62, 0.6, 0.56)
	var wire := Color(0.08, 0.08, 0.09)
	# postes ao longo do lado norte de cada rua horizontal, a 35 cm do meio-fio
	for z in zs:
		var pz := float(z) - hw - 0.35
		var tops: Array = []
		var x := float(xs[0]) - 20.0
		while x <= float(xs[-1]) + 20.0:
			var q := Vector3(x, 0, pz)
			if free.call(q, 3.5) and absf(x) < 84.0:
				deco.add_cylinder(q + Vector3(0, 3.75, 0), 0.12, 7.5, wood_pole, 10, "concrete")
				deco.box(q + Vector3(0, 7.1, 0.0), Vector3(1.6, 0.1, 0.1), WOOD_DARK, 0.0, "wood_out")
				for k in [-0.7, 0.0, 0.7]:
					deco.add_cylinder(q + Vector3(k, 7.22, 0), 0.04, 0.14, Color(0.85, 0.85, 0.8), 6, "")
				deco.box(q + Vector3(0, 5.6, 0.18), Vector3(0.45, 0.6, 0.3), Color(0.45, 0.47, 0.48), 0.0, "metal")
				add_cyl_collider(q + Vector3(0, 1.5, 0), 0.14, 3.0)
				tops.append(q + Vector3(0, 7.28, 0))
			x += 22.0
		for i in tops.size() - 1:
			var a0: Vector3 = tops[i]
			var a1: Vector3 = tops[i + 1]
			if a0.distance_to(a1) > 50.0:
				continue
			for k in [-0.7, 0.0, 0.7]:
				_wire(a0 + Vector3(k, 0, 0), a1 + Vector3(k, 0, 0), 0.55, wire)
	# hidrantes perto das esquinas e bueiros nas pistas
	for x in xs:
		for z in zs:
			var hq := Vector3(float(x) + hw + 5.0, SIDEWALK_H, float(z) + hw + 0.5)
			if true:
				var red := Color(0.72, 0.18, 0.14)
				deco.add_cylinder(hq + Vector3(0, 0.3, 0), 0.11, 0.6, red, 12, "metal")
				deco.add_sphere(hq + Vector3(0, 0.62, 0), Vector3(0.12, 0.09, 0.12), red, 4, 10, "metal")
				deco.add_tube(hq + Vector3(-0.18, 0.4, 0), hq + Vector3(0.18, 0.4, 0), 0.05, red.darkened(0.2), 8, "metal")
			for d in [Vector3(1, 0, 0), Vector3(0, 0, 1)]:
				var mq: Vector3 = Vector3(float(x), 0.011, float(z)) + d * 16.0 + Vector3(d.z, 0, d.x) * 1.8
				markings.add_cylinder(mq, 0.36, 0.006, Color(0.22, 0.22, 0.23), 16, "steel")

func _wire(a: Vector3, b: Vector3, sag: float, col: Color) -> void:
	var n := 8
	var prev := a
	for i in range(1, n + 1):
		var t := float(i) / n
		var q := a.lerp(b, t) - Vector3(0, sag * 4.0 * t * (1.0 - t), 0)
		deco.add_tube(prev, q, 0.012, col, 4, "")
		prev = q

# ------------------------------------------------------------------ prédios
func _build_building(b: Dictionary) -> void:
	var c := Vector3(float(b.center[0]), 0, float(b.center[1]))
	var w := float(b.size[0])
	var d := float(b.size[1])
	var h := float(b.get("height", 3.0))
	var style: Dictionary = b.get("style", {})
	var wall_col := Color(style.get("wall", "#dddddd"))
	var trim_col := Color(style.get("trim", "#ffffff"))
	var roof_col := Color(style.get("roof", "#884444"))
	var accent := Color(style.get("accent", "#446688"))
	var node := Building.new()
	node.name = b.id
	node.building_id = b.id
	node.def = b
	node.rect = Rect2(c.x - w * 0.5, c.z - d * 0.5, w, d)
	node.height = h
	node.position = c
	world.get_node("Buildings").add_child(node)
	buildings[b.id] = node
	# Piso interno
	var fm := BoxMesh.new()
	fm.size = Vector3(w - 0.05, 0.05, d - 0.05)
	var planks: bool = b.get("type", "") in ["house", "shop"] and b.id != "mercadinho"
	var fcol := Color(style.get("floor", "#a08060"))
	var fmat := "wood_floor" if planks else ("concrete" if b.get("type", "") == "garage" else "porcelain")
	var floor_mi := _mesh(fm, Mat.surface(Mat.SURFACES[fmat][0], {"tint": fcol.lerp(Color.WHITE, 0.45) if planks else fcol.lerp(Color.WHITE, 0.6), "outdoor": 0.0}), Vector3(0, 0.0, 0), node)
	floor_mi.name = "Floor"
	# Paredes
	var openings := {"n": [], "s": [], "e": [], "w": []}
	for door in b.get("doors", []):
		openings[door.side].append([float(door.get("offset", 0)), float(door.get("width", 1.1)), door])
	for side in ["n", "s", "e", "w"]:
		_build_wall_side(node, b, side, w, d, h, openings[side], wall_col, trim_col, accent)
	# Telhado
	node.roof = Node3D.new()
	node.roof.name = "Roof"
	node.add_child(node.roof)
	var rb := MeshBatcher.new()
	if b.get("type", "") == "house":
		_gable_roof(rb, b, w, d, h, roof_col, wall_col, trim_col)
	else:
		_flat_roof(rb, b, w, d, h, roof_col, trim_col)
	var rmi := rb.build(0.8)
	rmi.name = "RoofMesh"
	node.roof.add_child(rmi)
	# Luz interna (noite)
	if not b.get("closed_interior", false):
		node.lamp = OmniLight3D.new()
		node.lamp.position = Vector3(0, h - 0.5, 0)
		node.lamp.omni_range = max(w, d) * 0.85
		node.lamp.light_energy = 1.1
		node.lamp.light_color = Color(1.0, 0.86, 0.66)
		node.lamp.visible = false
		node.add_child(node.lamp)
	# Colisões das paredes internas e mobília
	var interior := MeshBatcher.new()
	for f in b.get("furniture", []):
		_build_furniture(node, b, f, interior, accent)
	var imi := interior.build(0.7)
	imi.name = "Interior"
	node.add_child(imi)
	# Placa do estabelecimento
	if style.has("sign"):
		var fside: String = b.doors[0].side if b.get("doors", []).size() > 0 else "s"
		_add_sign(node, fside, w, d, h, str(style.sign), accent)
	for sid in b.get("spots", {}):
		var s: Dictionary = b.spots[sid]
		_add_spot(sid, c + Vector3(float(s.pos[0]), 0, float(s.pos[1])), float(s.get("rot", 0)), s.get("pose", "stand"), bool(s.get("hidden", false)), b.id)

func _side_frame(side: String, w: float, d: float) -> Dictionary:
	# Retorna eixo ao longo da parede, posição fixa, comprimento e rotação.
	match side:
		"n": return {"axis": Vector3(1, 0, 0), "fixed": Vector3(0, 0, -d * 0.5), "len": w + WALL_T, "rot": 0.0, "out": Vector3(0, 0, -1)}
		"s": return {"axis": Vector3(1, 0, 0), "fixed": Vector3(0, 0, d * 0.5), "len": w + WALL_T, "rot": 0.0, "out": Vector3(0, 0, 1)}
		"e": return {"axis": Vector3(0, 0, 1), "fixed": Vector3(w * 0.5, 0, 0), "len": d - WALL_T, "rot": 90.0, "out": Vector3(1, 0, 0)}
	return {"axis": Vector3(0, 0, 1), "fixed": Vector3(-w * 0.5, 0, 0), "len": d - WALL_T, "rot": 90.0, "out": Vector3(-1, 0, 0)}

## Acabamento externo do prédio: [material, tinta]. Pode ser fixado em style.facade.
func _facade(b: Dictionary) -> Array:
	var wall_col := Color(b.get("style", {}).get("wall", "#dddddd"))
	var f: String = b.get("style", {}).get("facade", "")
	if f == "":
		f = {"house": "plaster", "shop": "brick", "apartment": "brick", "garage": "concrete", "restaurant": "plaster"}.get(b.get("type", ""), "plaster")
	match f:
		"brick":
			return ["brick", wall_col.lerp(Color.WHITE, 0.72)]
		"concrete":
			return ["concrete", wall_col.lerp(Color.WHITE, 0.35)]
		"siding":
			return ["siding", wall_col]
	return ["plaster", wall_col]

func _build_wall_side(node: Building, b: Dictionary, side: String, w: float, d: float, h: float, openings: Array, wall_col: Color, trim_col: Color, accent: Color) -> void:
	var f := _side_frame(side, w, d)
	var full := MeshBatcher.new()
	var low := MeshBatcher.new()
	var panes := MeshBatcher.new()
	var half: float = f.len * 0.5
	var fac := _facade(b)
	var ext: String = fac[0]
	var ext_col: Color = fac[1]
	var in_col := wall_col.lerp(Color.WHITE, 0.55)
	var skin := 0.03
	var skin_off: Vector3 = f.out * (WALL_T * 0.5 + skin * 0.5)
	var extra := 0.07 if side in ["n", "s"] else 0.0   # cantos cobertos pelo revestimento
	# Intervalos sólidos entre aberturas de porta
	var cuts: Array = []
	for o in openings:
		cuts.append([float(o[0]) - float(o[1]) * 0.5, float(o[0]) + float(o[1]) * 0.5, o[2]])
	cuts.sort_custom(func(a, bb): return a[0] < bb[0])
	var segs: Array = []
	var cur := -half
	for cut in cuts:
		if cut[0] > cur:
			segs.append([cur, cut[0]])
		cur = cut[1]
	if cur < half:
		segs.append([cur, half])
	var origin: Vector3 = node.position
	for i in segs.size():
		var sg: Array = segs[i]
		var mid := (float(sg[0]) + float(sg[1])) * 0.5
		var len := float(sg[1]) - float(sg[0])
		if len <= 0.01:
			continue
		var e0 := extra if i == 0 and is_equal_approx(float(sg[0]), -half) else 0.0
		var e1 := extra if i == segs.size() - 1 and is_equal_approx(float(sg[1]), half) else 0.0
		var center: Vector3 = f.fixed + f.axis * mid
		var sk_center: Vector3 = center + f.axis * (e1 - e0) * 0.5
		full.box(center + Vector3(0, h * 0.5, 0), Vector3(len, h, WALL_T), in_col, f.rot, "plaster_in")
		full.box(sk_center + skin_off + Vector3(0, h * 0.5, 0), Vector3(len + e0 + e1, h, skin), ext_col, f.rot, ext)
		# rodapé de concreto e cimalha
		full.box(sk_center + Vector3(0, 0.2, 0) + f.out * (WALL_T * 0.5 + skin + 0.015), Vector3(len + e0 + e1, 0.4, 0.03), Color(0.62, 0.6, 0.57), f.rot, "concrete")
		full.box(sk_center + Vector3(0, h - 0.09, 0) + f.out * (WALL_T * 0.5 + skin + 0.02), Vector3(len + e0 + e1, 0.18, 0.05), trim_col, f.rot, "concrete")
		low.box(center + Vector3(0, 0.3, 0), Vector3(len, 0.6, WALL_T), in_col, f.rot, "plaster_in")
		low.box(sk_center + skin_off + Vector3(0, 0.3, 0), Vector3(len + e0 + e1, 0.6, skin), ext_col, f.rot, ext)
		add_collider(origin + center + Vector3(0, h * 0.5, 0), Vector3(len, h, WALL_T), f.rot)
	for cut in cuts:
		var dd: Dictionary = cut[2]
		var door_h := 3.4 if dd.get("rollup", false) else DOOR_H
		var mid2 := (float(cut[0]) + float(cut[1])) * 0.5
		var len2 := float(cut[1]) - float(cut[0])
		var center2: Vector3 = f.fixed + f.axis * mid2
		if h > door_h:
			full.box(center2 + Vector3(0, (h + door_h) * 0.5, 0), Vector3(len2, h - door_h, WALL_T), in_col, f.rot, "plaster_in")
			full.box(center2 + skin_off + Vector3(0, (h + door_h) * 0.5, 0), Vector3(len2, h - door_h, skin), ext_col, f.rot, ext)
			add_collider(origin + center2 + Vector3(0, (h + door_h) * 0.5, 0), Vector3(len2, h - door_h, WALL_T), f.rot)
		# batente e soleira
		var jamb := trim_col.lerp(Color(0.85, 0.83, 0.78), 0.3)
		full.box(center2 + Vector3(0, door_h + 0.06, 0), Vector3(len2 + 0.16, 0.12, WALL_T + skin * 2 + 0.06), jamb, f.rot, "concrete")
		for sgn in [-1.0, 1.0]:
			full.box(center2 + f.axis * sgn * (len2 * 0.5 + 0.035) + Vector3(0, door_h * 0.5, 0), Vector3(0.07, door_h, WALL_T + skin * 2 + 0.04), jamb, f.rot, "concrete")
		full.box(center2 + Vector3(0, 0.015, 0) + f.out * 0.12, Vector3(len2 + 0.16, 0.03, 0.4), Color(0.7, 0.68, 0.64), f.rot, "concrete")
		var door := Door.new()
		door.name = dd.id
		door.setup(dd, world, b.id, WALL_T, accent.darkened(0.15))
		door.position = center2
		door.rotation_degrees.y = f.rot
		node.add_child(door)
		doors[dd.id] = door
	# Janelas: vidro (acende à noite), caixilho com travessas, peitoril e verga
	var floors := int(b.get("floors", 1))
	for win in b.get("windows", []):
		if win.side != side:
			continue
		var off := float(win.get("offset", 0))
		var ww := float(win.get("width", 1.2))
		var is_low: bool = win.get("low", false)
		var wh := 1.7 if is_low else 1.1
		var wy := 1.25 if is_low else 1.55
		if win.get("drive", false):
			wh = 1.0
			wy = 1.35
		for fl in range(floors):
			var oy := 3.1 * fl
			var wc: Vector3 = f.fixed + f.axis * off + Vector3(0, oy, 0)
			_window(full, panes, wc, f, ww, wh, wy, trim_col, is_low, b, accent, fl == 0)
	# Toldo na fachada de lojas
	if b.get("type", "") in ["shop", "restaurant"] and b.get("doors", []).size() > 0 and b.doors[0].side == side:
		var aw: Vector3 = f.fixed + f.out * 0.85 + Vector3(0, 2.78, 0)
		var aw_len: float = min(f.len - 1.0, 9.0)
		full.add_box(Transform3D(Basis(Vector3.UP, deg_to_rad(f.rot)) * Basis(Vector3.RIGHT, deg_to_rad(-14.0) * (1.0 if side in ["s", "e"] else -1.0)), aw), Vector3(aw_len, 0.05, 1.65), accent, "canvas")
		full.box(aw + f.out * 0.8 + Vector3(0, -0.28, 0), Vector3(aw_len, 0.22, 0.02), accent.darkened(0.15), f.rot, "canvas")
		for sgn in [-1.0, 1.0]:
			full.add_tube(aw + f.axis * sgn * aw_len * 0.48 + f.out * 0.78 + Vector3(0, -0.15, 0), f.fixed + f.axis * sgn * aw_len * 0.48 + f.out * (WALL_T * 0.5 + skin) + Vector3(0, 3.1, 0), 0.015, Color(0.3, 0.3, 0.32), 5, "steel")
	var cont := Node3D.new()
	cont.name = "Wall_" + side
	node.add_child(cont)
	cont.add_child(full.build(0.85))
	var pm := panes.build(0.1, false)
	pm.material_override = Mat.windows()
	cont.add_child(pm)
	var lmi := low.build(0.85)
	lmi.name = "WallLow_" + side
	lmi.visible = false
	node.add_child(lmi)
	node.side_full[side] = cont
	node.side_low[side] = lmi

func _window(full: MeshBatcher, panes: MeshBatcher, wc: Vector3, f: Dictionary, ww: float, wh: float, wy: float, trim_col: Color, is_low: bool, b: Dictionary, accent: Color, ground: bool) -> void:
	var depth := WALL_T + 0.08
	var frame_col := trim_col.lerp(Color(0.92, 0.91, 0.88), 0.4)
	var c := wc + Vector3(0, wy, 0)
	panes.box(c, Vector3(ww, wh, 0.03), Color.WHITE, f.rot)
	var fo: Vector3 = f.out * 0.0
	var t := 0.06
	full.box(c + Vector3(0, wh * 0.5 + t * 0.5, 0) + fo, Vector3(ww + t * 2, t, depth), frame_col, f.rot, "metal")
	full.box(c - Vector3(0, wh * 0.5 + t * 0.5, 0) + fo, Vector3(ww + t * 2, t, depth), frame_col, f.rot, "metal")
	for sgn in [-1.0, 1.0]:
		full.box(c + f.axis * sgn * (ww * 0.5 + t * 0.5) + fo, Vector3(t, wh, depth), frame_col, f.rot, "metal")
	# travessas: vertical no meio e horizontal no terço superior (janela de correr)
	if ww > 0.9:
		full.box(c, Vector3(0.04, wh, 0.07), frame_col, f.rot, "metal")
	if not is_low:
		full.box(c + Vector3(0, wh * 0.18, 0), Vector3(ww, 0.035, 0.06), frame_col, f.rot, "metal")
	# peitoril saliente e verga
	full.box(c - Vector3(0, wh * 0.5 + t + 0.03, 0) + f.out * (WALL_T * 0.5 + 0.08), Vector3(ww + 0.3, 0.06, 0.22), Color(0.72, 0.7, 0.66), f.rot, "concrete")
	full.box(c + Vector3(0, wh * 0.5 + t + 0.06, 0) + f.out * (WALL_T * 0.5 + 0.04), Vector3(ww + 0.24, 0.12, 0.08), Color(0.66, 0.64, 0.6), f.rot, "concrete")
	if not is_low and b.get("type", "") == "house" and ground:
		for sgn in [-1.0, 1.0]:
			# venezianas de madeira com ripas
			var sc: Vector3 = c + f.axis * sgn * (ww * 0.5 + 0.26) + f.out * (WALL_T * 0.5 + 0.06)
			full.box(sc, Vector3(0.4, wh + 0.12, 0.04), accent, f.rot, "wood_out")
			for k in 6:
				full.box(sc + Vector3(0, -wh * 0.42 + k * wh * 0.17, 0) + f.out * 0.025, Vector3(0.34, 0.03, 0.015), accent.darkened(0.2), f.rot, "wood_out")
	if is_low and b.get("type", "") in ["shop", "restaurant"]:
		full.box(c - Vector3(0, wh * 0.5 + 0.25, 0) + f.out * (WALL_T * 0.5 + 0.02), Vector3(ww + 0.12, 0.5, 0.05), Color(0.35, 0.36, 0.38), f.rot, "steel")

func _gable_roof(rb: MeshBatcher, b: Dictionary, w: float, d: float, h: float, roof_col: Color, wall_col: Color, trim_col: Color) -> void:
	# cumeeira ao longo de X; águas para ±Z; beiral de 45 cm
	var ov := 0.45
	var x0 := -w * 0.5 - ov
	var x1 := w * 0.5 + ov
	var z0 := -d * 0.5 - ov
	var z1 := d * 0.5 + ov
	var rise := 1.5
	var eave := h - ov * rise / (d * 0.5 + ov) + 0.05
	var top := h + rise * (d * 0.5) / (d * 0.5 + ov) + 0.05
	var tile_col := roof_col.lerp(Color(0.62, 0.32, 0.22), 0.55)
	rb.add_quad([Vector3(x0, eave, z0), Vector3(x1, eave, z0), Vector3(x1, top, 0), Vector3(x0, top, 0)], tile_col, "roof_tiles", Vector3(0, 1, -1))
	rb.add_quad([Vector3(x1, eave, z1), Vector3(x0, eave, z1), Vector3(x0, top, 0), Vector3(x1, top, 0)], tile_col, "roof_tiles", Vector3(0, 1, 1))
	# espessura das telhas na borda e forro do beiral (visto de baixo)
	rb.add_quad([Vector3(x0, eave - 0.06, z0), Vector3(x1, eave - 0.06, z0), Vector3(x1, top - 0.06, 0), Vector3(x0, top - 0.06, 0)], trim_col, "wood_out", Vector3(0, -1, 0))
	rb.add_quad([Vector3(x1, eave - 0.06, z1), Vector3(x0, eave - 0.06, z1), Vector3(x0, top - 0.06, 0), Vector3(x1, top - 0.06, 0)], trim_col, "wood_out", Vector3(0, -1, 0))
	for zz in [z0, z1]:
		rb.box(Vector3(0, eave - 0.06, zz), Vector3(x1 - x0 + 0.04, 0.16, 0.04), trim_col, 0.0, "wood_out")
	# cumeeira (peças de capa)
	rb.add_tube(Vector3(x0 - 0.02, top + 0.02, 0), Vector3(x1 + 0.02, top + 0.02, 0), 0.09, tile_col.darkened(0.12), 8, "roof_tiles")
	# empenas (triângulos de parede nas laterais) e testeiras inclinadas
	var fac := _facade(b)
	for sx in [-1.0, 1.0]:
		var xx: float = sx * (w * 0.5 + WALL_T * 0.5 + 0.03)
		var p := [Vector3(xx, h - 0.02, -d * 0.5 - 0.1), Vector3(xx, h - 0.02, d * 0.5 + 0.1), Vector3(xx, top - 0.05, 0)]
		if sx < 0:
			p = [p[1], p[0], p[2]]
		rb.add_tri(p, fac[1], fac[0], Vector3(sx, 0, 0))
		for zs in [-1.0, 1.0]:
			rb.add_tube(Vector3(sx * (x1 - 0.01), eave - 0.02, zs * (z1)), Vector3(sx * (x1 - 0.01), top - 0.02, 0), 0.05, trim_col, 4, "wood_out")
	# chaminé de tijolo
	rb.box(Vector3(w * 0.25, top - 0.3, -d * 0.18), Vector3(0.55, 1.5, 0.55), Color(0.95, 0.9, 0.86), 0.0, "brick")
	rb.box(Vector3(w * 0.25, top + 0.48, -d * 0.18), Vector3(0.65, 0.08, 0.65), Color(0.6, 0.58, 0.55), 0.0, "concrete")

func _flat_roof(rb: MeshBatcher, b: Dictionary, w: float, d: float, h: float, roof_col: Color, trim_col: Color) -> void:
	var garage: bool = b.get("type", "") == "garage"
	rb.box(Vector3(0, h + 0.1, 0), Vector3(w + 0.26, 0.2, d + 0.26), roof_col.lerp(Color(0.6, 0.6, 0.6), 0.65), 0.0, "roof_fiber" if garage else "roof_flat")
	var par_col := trim_col.lerp(Color(0.75, 0.73, 0.7), 0.5)
	for sg in [-1.0, 1.0]:
		rb.box(Vector3(0, h + 0.42, sg * (d * 0.5 + 0.06)), Vector3(w + 0.3, 0.5, 0.18), par_col, 0.0, "concrete")
		rb.box(Vector3(sg * (w * 0.5 + 0.06), h + 0.42, 0), Vector3(0.18, 0.5, d + 0.3), par_col, 0.0, "concrete")
		rb.box(Vector3(0, h + 0.69, sg * (d * 0.5 + 0.06)), Vector3(w + 0.38, 0.05, 0.26), par_col.darkened(0.1), 0.0, "concrete")
		rb.box(Vector3(sg * (w * 0.5 + 0.06), h + 0.69, 0), Vector3(0.26, 0.05, d + 0.38), par_col.darkened(0.1), 0.0, "concrete")
	# detalhes de laje: condensadoras, exaustores, alçapão, tubulações e caixa d'água
	var r := RandomNumberGenerator.new()
	r.seed = hash(b.id)
	var units := 1 + int(w * d / 45.0)
	for i in units:
		var p := Vector3(r.randf_range(-w * 0.32, w * 0.32), h + 0.2, r.randf_range(-d * 0.3, d * 0.3))
		rb.box(p + Vector3(0, 0.4, 0), Vector3(1.0, 0.8, 0.75), Color(0.84, 0.84, 0.82), 0.0, "metal")
		rb.add_cylinder(p + Vector3(0, 0.81, 0), 0.3, 0.03, Color(0.2, 0.2, 0.22), 14, "steel")
		rb.box(p + Vector3(0, 0.12, 0), Vector3(1.1, 0.24, 0.85), Color(0.55, 0.55, 0.55), 0.0, "concrete")
		rb.add_tube(p + Vector3(0.5, 0.3, 0), p + Vector3(0.5 + r.randf_range(0.6, 1.4), 0.15, 0), 0.04, Color(0.3, 0.3, 0.32), 6, "steel")
	for i in 2 + int(w / 6.0):
		var vp := Vector3(r.randf_range(-w * 0.4, w * 0.4), h + 0.2, r.randf_range(-d * 0.4, d * 0.4))
		rb.add_cylinder(vp + Vector3(0, 0.25, 0), 0.12, 0.5, Color(0.7, 0.7, 0.72), 10, "steel")
		rb.add_cylinder(vp + Vector3(0, 0.52, 0), 0.2, 0.05, Color(0.6, 0.6, 0.62), 10, "steel")
	var hp := Vector3(-w * 0.3, h + 0.2, d * 0.28)
	rb.box(hp + Vector3(0, 0.15, 0), Vector3(0.9, 0.3, 0.9), Color(0.5, 0.5, 0.48), 0.0, "concrete")
	rb.box(hp + Vector3(0, 0.32, 0), Vector3(0.8, 0.05, 0.8), Color(0.32, 0.34, 0.36), 0.0, "metal")
	if not garage and w * d > 60.0:
		var tp := Vector3(w * 0.3, h + 0.2, -d * 0.25)
		rb.add_cylinder(tp + Vector3(0, 0.75, 0), 0.75, 1.5, Color(0.25, 0.42, 0.62), 16, "metal")
		rb.add_cylinder(tp + Vector3(0, 1.55, 0), 0.6, 0.12, Color(0.22, 0.38, 0.56), 16, "metal")
	if b.get("type", "") in ["shop", "restaurant", "garage"]:
		# exaustor de cozinha / caixa técnica
		rb.box(Vector3(w * 0.2, h + 0.6, d * 0.1), Vector3(1.2, 0.7, 0.9), Color(0.78, 0.79, 0.8), 0.0, "steel")

func _add_sign(node: Building, side: String, w: float, d: float, h: float, text: String, accent: Color) -> void:
	var f := _side_frame(side, w, d)
	var board := BoxMesh.new()
	board.size = Vector3(min(f.len - 0.6, 7.0), 0.75, 0.12)
	var pos: Vector3 = f.fixed + f.out * 0.18 + Vector3(0, h + 0.05, 0)
	var mi := _mesh(board, Mat.color(accent, 0.6), pos, node.side_full[side])
	mi.rotation_degrees.y = f.rot
	var lbl := Label3D.new()
	lbl.text = text
	lbl.font_size = 64
	lbl.pixel_size = 0.0065
	lbl.outline_size = 0
	lbl.modulate = Color(1, 0.97, 0.88)
	lbl.position = pos + f.out * 0.08
	lbl.rotation_degrees.y = f.rot + (180.0 if (side == "n" or side == "w") else 0.0)
	if side == "e":
		lbl.rotation_degrees.y = 90.0
	elif side == "w":
		lbl.rotation_degrees.y = -90.0
	node.side_full[side].add_child(lbl)

# ------------------------------------------------------------------ mobília
const INTERACTIVE := ["bed", "wardrobe", "fridge", "chest", "crate", "stove", "sink", "shower", "sofa", "chair",
	"note", "counter", "grill", "fryer", "soda_machine", "drive_window", "time_clock"]

func _build_furniture(node: Building, b: Dictionary, f: Dictionary, batch: MeshBatcher, accent: Color) -> void:
	if f.type == "partition":
		var a := Vector3(float(f.from[0]), 0, float(f.from[1]))
		var e := Vector3(float(f.to[0]), 0, float(f.to[1]))
		var len := a.distance_to(e) + 0.12
		var rot := 0.0 if absf(a.z - e.z) < 0.01 else 90.0
		var h := float(b.get("height", 3.0)) - 0.05
		batch.box((a + e) * 0.5 + Vector3(0, h * 0.5, 0), Vector3(len, h, 0.12), Color(b.style.get("wall", "#dddddd")).lerp(Color.WHITE, 0.55), rot, "plaster_in")
		add_collider(node.position + (a + e) * 0.5 + Vector3(0, h * 0.5, 0), Vector3(len, h, 0.12), rot)
		return
	var lp := Vector3(float(f.pos[0]), 0, float(f.pos[1]))
	var rot_deg := float(f.get("rot", 0))
	var xf := Transform3D(Basis(Vector3.UP, deg_to_rad(rot_deg)), lp)
	var col_size := _furniture_visual(batch, f, xf, accent)
	if col_size != Vector3.ZERO:
		add_collider(node.position + xf * Vector3(0, col_size.y * 0.5, 0), col_size, rot_deg)
	var interactive: bool = INTERACTIVE.has(f.type) and (f.type != "crate" or f.has("container")) and (f.type != "counter" or f.has("shop"))
	var fn: Furniture = null
	if interactive:
		fn = Furniture.new()
		fn.name = str(f.get("id", f.type))
		node.add_child(fn)
		fn.position = lp
		fn.setup(f, world, b.id)
		if f.has("id"):
			furniture[f.id] = fn
		if f.has("container"):
			furniture[f.container.id] = fn
	if f.has("spot"):
		var wp: Vector3 = node.position + lp
		var fwd := Basis(Vector3.UP, deg_to_rad(rot_deg)) * Vector3(0, 0, -1)
		match f.type:
			"bed": _add_spot(f.spot, wp, rot_deg, "lie", false, b.id, fn)
			"sofa", "chair": _add_spot(f.spot, wp + fwd * 0.12, rot_deg, "sit", false, b.id, fn)
			"table": _add_spot(f.spot, wp - fwd * 0.95, rot_deg, "eat", false, b.id, fn)
			_: _add_spot(f.spot, wp, rot_deg, "stand", false, b.id, fn)

## Desenha a mobília com caixas coloridas. Retorna o tamanho da colisão (ZERO = sem).
func _furniture_visual(bt: MeshBatcher, f: Dictionary, xf: Transform3D, accent: Color) -> Vector3:
	var L := float(f.get("len", 1.0))
	var col := Color(f.get("color", "#8b5e34"))
	var B := func(p: Vector3, s: Vector3, c: Color, m: String) -> void:
		bt.add_box(xf * Transform3D(Basis(), p), s, c, m)
	match f.type:
		"bed":
			B.call(Vector3(0, 0.18, 0), Vector3(1.4, 0.36, 2.0), WOOD, "wood")
			B.call(Vector3(0, 0.43, 0.05), Vector3(1.3, 0.16, 1.85), WHITE, "fabric")
			B.call(Vector3(0, 0.56, -0.68), Vector3(1.0, 0.12, 0.38), Color("f4f1ea"), "")
			B.call(Vector3(0, 0.53, 0.32), Vector3(1.34, 0.08, 1.25), accent, "fabric")
			B.call(Vector3(0, 0.55, -0.98), Vector3(1.44, 1.1, 0.08), WOOD_DARK, "wood")
			return Vector3(1.4, 0.6, 2.0)
		"wardrobe":
			B.call(Vector3(0, 1.0, 0), Vector3(1.2, 2.0, 0.6), WOOD, "wood")
			B.call(Vector3(0, 1.0, -0.31), Vector3(0.02, 1.9, 0.02), WOOD_DARK, "wood")
			B.call(Vector3(0.08, 1.05, -0.32), Vector3(0.04, 0.2, 0.03), STEEL, "steel")
			B.call(Vector3(-0.08, 1.05, -0.32), Vector3(0.04, 0.2, 0.03), STEEL, "steel")
			return Vector3(1.2, 2.0, 0.6)
		"nightstand":
			B.call(Vector3(0, 0.25, 0), Vector3(0.45, 0.5, 0.45), WOOD, "wood")
			B.call(Vector3(0, 0.62, 0), Vector3(0.18, 0.25, 0.18), Color("f0d890"), "")
			return Vector3.ZERO
		"shower":
			B.call(Vector3(0, 0.03, 0), Vector3(0.95, 0.06, 0.95), WHITE, "")
			B.call(Vector3(0, 1.0, 0.47), Vector3(0.95, 2.0, 0.03), Color(0.75, 0.85, 0.9), "")
			B.call(Vector3(0, 1.9, -0.3), Vector3(0.05, 0.4, 0.05), STEEL, "steel")
			B.call(Vector3(0, 2.05, -0.2), Vector3(0.2, 0.04, 0.2), STEEL, "steel")
			return Vector3.ZERO
		"toilet":
			B.call(Vector3(0, 0.2, 0.05), Vector3(0.4, 0.4, 0.55), WHITE, "")
			B.call(Vector3(0, 0.55, -0.22), Vector3(0.42, 0.5, 0.18), WHITE, "")
			return Vector3(0.45, 0.8, 0.65)
		"fridge":
			B.call(Vector3(0, 0.9, 0), Vector3(0.7, 1.8, 0.7), WHITE, "metal")
			B.call(Vector3(0, 1.2, -0.36), Vector3(0.68, 0.02, 0.02), Color("cccccc"), "")
			B.call(Vector3(0.28, 1.45, -0.37), Vector3(0.04, 0.3, 0.04), STEEL, "steel")
			return Vector3(0.7, 1.8, 0.7)
		"stove":
			B.call(Vector3(0, 0.45, 0), Vector3(0.7, 0.9, 0.62), WHITE, "metal")
			B.call(Vector3(0, 0.91, 0), Vector3(0.66, 0.03, 0.58), DARK, "metal")
			for p in [Vector3(-0.16, 0.93, -0.13), Vector3(0.16, 0.93, -0.13), Vector3(-0.16, 0.93, 0.13), Vector3(0.16, 0.93, 0.13)]:
				B.call(p, Vector3(0.16, 0.02, 0.16), Color("555555"), "")
			B.call(Vector3(0, 0.45, -0.32), Vector3(0.5, 0.35, 0.02), Color("333333"), "")
			return Vector3(0.7, 0.95, 0.62)
		"sink":
			B.call(Vector3(0, 0.45, 0), Vector3(0.7, 0.9, 0.62), WOOD, "wood")
			B.call(Vector3(0, 0.91, 0), Vector3(0.72, 0.04, 0.64), Color("dddddd"), "concrete")
			B.call(Vector3(0, 0.9, 0.05), Vector3(0.45, 0.04, 0.35), STEEL.darkened(0.3), "steel")
			B.call(Vector3(0, 1.1, 0.27), Vector3(0.04, 0.3, 0.04), STEEL, "steel")
			return Vector3(0.7, 0.95, 0.62)
		"table":
			B.call(Vector3(0, 0.74, 0), Vector3(1.2, 0.05, 0.8), WOOD, "wood")
			for p in [Vector3(-0.52, 0.36, -0.32), Vector3(0.52, 0.36, -0.32), Vector3(-0.52, 0.36, 0.32), Vector3(0.52, 0.36, 0.32)]:
				B.call(p, Vector3(0.06, 0.72, 0.06), WOOD_DARK, "wood")
			return Vector3(1.2, 0.78, 0.8)
		"chair":
			B.call(Vector3(0, 0.45, 0), Vector3(0.45, 0.05, 0.45), WOOD, "wood")
			B.call(Vector3(0, 0.75, 0.2), Vector3(0.45, 0.6, 0.05), WOOD, "wood")
			for p in [Vector3(-0.19, 0.22, -0.19), Vector3(0.19, 0.22, -0.19), Vector3(-0.19, 0.22, 0.19), Vector3(0.19, 0.22, 0.19)]:
				B.call(p, Vector3(0.04, 0.45, 0.04), WOOD_DARK, "wood")
			return Vector3.ZERO
		"sofa":
			B.call(Vector3(0, 0.22, 0.05), Vector3(2.0, 0.44, 0.85), accent.darkened(0.2), "fabric")
			B.call(Vector3(0, 0.62, 0.38), Vector3(2.0, 0.6, 0.22), accent.darkened(0.3), "fabric")
			B.call(Vector3(-0.95, 0.35, 0.05), Vector3(0.18, 0.4, 0.85), accent.darkened(0.35), "fabric")
			B.call(Vector3(0.95, 0.35, 0.05), Vector3(0.18, 0.4, 0.85), accent.darkened(0.35), "fabric")
			B.call(Vector3(-0.45, 0.46, -0.02), Vector3(0.85, 0.08, 0.65), accent, "fabric")
			B.call(Vector3(0.45, 0.46, -0.02), Vector3(0.85, 0.08, 0.65), accent, "fabric")
			return Vector3(2.0, 0.9, 0.9)
		"tv":
			B.call(Vector3(0, 0.25, 0), Vector3(1.2, 0.5, 0.4), WOOD_DARK, "wood")
			B.call(Vector3(0, 0.85, 0.05), Vector3(1.0, 0.6, 0.06), Color("111114"), "metal")
			return Vector3(1.2, 0.6, 0.4)
		"rug":
			B.call(Vector3(0, 0.035, 0), Vector3(2.0, 0.01, 1.4), accent.lightened(0.3), "fabric")
			return Vector3.ZERO
		"plant":
			B.call(Vector3(0, 0.2, 0), Vector3(0.35, 0.4, 0.35), Color("a0522d"), "")
			bt.add_sphere(xf * Vector3(0, 0.7, 0), Vector3(0.35, 0.45, 0.35), Color("3f6a35"))
			return Vector3.ZERO
		"shelf":
			B.call(Vector3(0, 0.9, 0), Vector3(L, 1.8, 0.5), col.lightened(0.0) if f.has("color") else Color("d0d4d8"), "metal")
			for lvl in [0.35, 0.8, 1.25, 1.65]:
				var x := -L * 0.5 + 0.2
				while x < L * 0.5 - 0.2:
					var pc := Color.from_hsv(rng.randf(), 0.55, 0.85)
					var bh := rng.randf_range(0.18, 0.3)
					for side in [-1.0, 1.0]:
						B.call(Vector3(x, lvl + bh * 0.5, side * 0.27), Vector3(0.22, bh, 0.06), pc, "metal")
					x += rng.randf_range(0.25, 0.32)
			return Vector3(L, 1.8, 0.5)
		"fridge_display":
			B.call(Vector3(0, 1.0, 0), Vector3(L, 2.0, 0.7), Color("dfe6ea"), "metal")
			B.call(Vector3(0, 1.05, -0.36), Vector3(L - 0.1, 1.7, 0.02), Color(0.6, 0.85, 0.95), "")
			for i in int(L / 0.25):
				B.call(Vector3(-L * 0.5 + 0.15 + i * 0.25, 1.4, -0.3), Vector3(0.12, 0.25, 0.12), Color.from_hsv(rng.randf(), 0.6, 0.9), "")
				B.call(Vector3(-L * 0.5 + 0.15 + i * 0.25, 0.8, -0.3), Vector3(0.12, 0.25, 0.12), Color.from_hsv(rng.randf(), 0.6, 0.9), "")
			return Vector3(L, 2.0, 0.7)
		"counter":
			B.call(Vector3(0, 0.5, 0), Vector3(L, 1.0, 0.7), accent.darkened(0.1), "metal")
			B.call(Vector3(0, 1.02, 0), Vector3(L + 0.05, 0.05, 0.75), Color("e8e2d6"), "concrete")
			if f.get("register", false):
				B.call(Vector3(L * 0.3, 1.15, 0), Vector3(0.4, 0.2, 0.35), DARK, "metal")
				B.call(Vector3(L * 0.3, 1.3, 0.05), Vector3(0.3, 0.18, 0.04), Color("2b3a4a"), "metal")
			return Vector3(L, 1.05, 0.7)
		"crate":
			B.call(Vector3(0, 0.3, 0), Vector3(0.7, 0.6, 0.7), Color("a07a4a"), "wood")
			B.call(Vector3(0.1, 0.8, 0.05), Vector3(0.55, 0.4, 0.55), Color("b48a55"), "wood")
			return Vector3(0.7, 1.0, 0.7)
		"chest":
			B.call(Vector3(0, 0.27, 0), Vector3(0.95, 0.54, 0.5), col if f.has("color") else WOOD, "wood")
			B.call(Vector3(0, 0.45, 0), Vector3(0.97, 0.06, 0.52), WOOD_DARK, "wood")
			B.call(Vector3(0, 0.38, -0.26), Vector3(0.1, 0.12, 0.03), Color("c8a040"), "wood")
			return Vector3(0.95, 0.56, 0.5)
		"grill":
			B.call(Vector3(0, 0.45, 0), Vector3(1.3, 0.9, 0.7), STEEL, "steel")
			B.call(Vector3(0, 0.92, 0), Vector3(1.2, 0.05, 0.6), DARK, "metal")
			B.call(Vector3(0, 1.6, 0.25), Vector3(1.3, 0.25, 0.3), STEEL.darkened(0.2), "steel")
			return Vector3(1.3, 0.95, 0.7)
		"fryer":
			B.call(Vector3(0, 0.45, 0), Vector3(0.9, 0.9, 0.7), STEEL, "steel")
			B.call(Vector3(-0.2, 0.9, 0), Vector3(0.35, 0.04, 0.5), Color("c9a227"), "")
			B.call(Vector3(0.2, 0.9, 0), Vector3(0.35, 0.04, 0.5), Color("c9a227"), "")
			B.call(Vector3(0, 1.05, -0.3), Vector3(0.6, 0.06, 0.04), DARK, "metal")
			return Vector3(0.9, 0.95, 0.7)
		"soda_machine":
			B.call(Vector3(0, 0.85, 0), Vector3(0.8, 1.7, 0.6), Color("c0392b"), "metal")
			B.call(Vector3(0, 1.3, -0.31), Vector3(0.6, 0.45, 0.02), WHITE, "metal")
			for i in 3:
				B.call(Vector3(-0.2 + i * 0.2, 0.95, -0.33), Vector3(0.08, 0.12, 0.08), STEEL, "steel")
			return Vector3(0.8, 1.7, 0.6)
		"drive_window":
			B.call(Vector3(0, 1.0, 0.0), Vector3(1.4, 0.06, 0.5), Color("e8e2d6"), "concrete")
			B.call(Vector3(0, 0.5, 0.05), Vector3(1.4, 1.0, 0.4), accent.darkened(0.1), "metal")
			B.call(Vector3(0.4, 1.12, 0), Vector3(0.35, 0.18, 0.3), DARK, "metal")
			return Vector3(1.4, 1.0, 0.5)
		"time_clock":
			B.call(Vector3(0, 1.4, 0.05), Vector3(0.35, 0.45, 0.12), Color("3a4a5a"), "metal")
			B.call(Vector3(0, 1.48, -0.02), Vector3(0.22, 0.12, 0.02), Color("9fe0a0"), "metal")
			return Vector3.ZERO
		"desk":
			B.call(Vector3(0, 0.74, 0), Vector3(1.4, 0.05, 0.7), WOOD, "wood")
			B.call(Vector3(-0.6, 0.36, 0), Vector3(0.08, 0.72, 0.65), WOOD_DARK, "wood")
			B.call(Vector3(0.6, 0.36, 0), Vector3(0.08, 0.72, 0.65), WOOD_DARK, "wood")
			B.call(Vector3(0, 1.0, 0.15), Vector3(0.55, 0.38, 0.04), DARK, "metal")
			return Vector3(1.4, 0.78, 0.7)
		"kitchen_counter":
			B.call(Vector3(0, 0.45, 0), Vector3(L, 0.9, 0.6), STEEL, "steel")
			B.call(Vector3(0, 0.92, 0), Vector3(L, 0.04, 0.62), STEEL.lightened(0.2), "steel")
			for i in 3:
				B.call(Vector3(-L * 0.3 + i * L * 0.3, 1.0, 0), Vector3(0.3, 0.12, 0.25), Color("f3c26b"), "metal")
			return Vector3(L, 0.95, 0.6)
		"lift":
			B.call(Vector3(-1.3, 1.0, 0), Vector3(0.25, 2.0, 0.25), Color("d04a2a"), "metal")
			B.call(Vector3(1.3, 1.0, 0), Vector3(0.25, 2.0, 0.25), Color("d04a2a"), "metal")
			B.call(Vector3(0, 0.08, 0), Vector3(2.4, 0.08, 3.6), Color("444444"), "metal")
			return Vector3.ZERO
		"workbench":
			B.call(Vector3(0, 0.45, 0), Vector3(L, 0.9, 0.7), Color("6a6e74"), "metal")
			B.call(Vector3(0, 0.92, 0), Vector3(L, 0.05, 0.72), WOOD, "wood")
			B.call(Vector3(0, 1.5, 0.32), Vector3(L, 1.0, 0.05), Color("8a8e94"), "metal")
			return Vector3(L, 0.95, 0.7)
		"tire_stack":
			for i in 3:
				bt.add_cylinder(xf * Vector3(0, 0.12 + i * 0.24, 0), 0.32, 0.22, Color("2a2a2c"), 14, "rubber")
			return Vector3(0.6, 0.7, 0.6)
		"note":
			B.call(Vector3(0, 0.775, 0), Vector3(0.22, 0.005, 0.3), Color("fffbea"), "")
			return Vector3.ZERO
	return Vector3.ZERO

# ------------------------------------------------------------------ objetos externos
func _build_prop(p: Dictionary) -> void:
	var pos := Vector3(float(p.pos[0]), 0, float(p.pos[1])) if p.has("pos") else Vector3.ZERO
	var rot := float(p.get("rot", 0))
	var xf := Transform3D(Basis(Vector3.UP, deg_to_rad(rot)), pos)
	var B := func(lp: Vector3, s: Vector3, c: Color, m: String) -> void:
		deco.add_box(xf * Transform3D(Basis(), lp), s, c, m)
	var T := func(a: Vector3, e: Vector3, r: float, c: Color, m: String) -> void:
		deco.add_tube(xf * a, xf * e, r, c, 8, m)
	var iron := Color(0.24, 0.26, 0.27)
	match p.type:
		"tree":
			_build_tree(pos, int(p.get("variant", 0)))
		"bench":
			for k in 3:
				B.call(Vector3(0, 0.45, -0.18 + k * 0.17), Vector3(1.8, 0.04, 0.13), WOOD, "wood_out")
			for k in 2:
				B.call(Vector3(0, 0.62 + k * 0.17, 0.25), Vector3(1.8, 0.13, 0.035), WOOD, "wood_out")
			for x in [-0.75, 0.75]:
				B.call(Vector3(x, 0.22, -0.05), Vector3(0.06, 0.44, 0.06), iron, "metal")
				B.call(Vector3(x, 0.22, 0.2), Vector3(0.06, 0.44, 0.06), iron, "metal")
				B.call(Vector3(x, 0.42, 0.0), Vector3(0.05, 0.05, 0.5), iron, "metal")
				T.call(Vector3(x, 0.42, 0.24), Vector3(x, 0.9, 0.27), 0.025, iron, "metal")
			add_collider(xf * Vector3(0, 0.4, 0.05), Vector3(1.8, 0.8, 0.55), rot)
			var fn := Furniture.new()
			fn.name = str(p.get("id", "banco"))
			world.get_node("Props").add_child(fn)
			fn.position = pos
			fn.setup({"type": "bench", "rot": rot, "id": p.get("id", "")}, world, "")
			if p.has("spot"):
				var fwd := Basis(Vector3.UP, deg_to_rad(rot)) * Vector3(0, 0, -1)
				_add_spot(p.spot, pos + fwd * 0.12, rot, "sit", false, "", fn)
		"streetlight":
			var pole := Color(0.3, 0.33, 0.33)
			B.call(Vector3(0, 0.2, 0), Vector3(0.36, 0.4, 0.36), Color(0.6, 0.58, 0.55), "concrete")
			deco.add_cylinder(xf * Vector3(0, 2.0, 0), 0.085, 3.6, pole, 10, "metal")
			deco.add_cylinder(xf * Vector3(0, 4.6, 0), 0.06, 1.8, pole, 10, "metal")
			T.call(Vector3(0, 5.4, 0), Vector3(0, 5.8, -0.4), 0.045, pole, "metal")
			T.call(Vector3(0, 5.8, -0.4), Vector3(0, 5.75, -1.25), 0.04, pole, "metal")
			B.call(Vector3(0, 5.7, -1.35), Vector3(0.5, 0.12, 0.32), pole.darkened(0.2), "metal")
			lamp_heads.add_box(xf * Transform3D(Basis(), Vector3(0, 5.62, -1.35)), Vector3(0.42, 0.05, 0.26), Color.WHITE)
			add_cyl_collider(pos + Vector3(0, 1.5, 0), 0.12, 3.0)
			var l := OmniLight3D.new()
			l.position = xf * Vector3(0, 5.3, -1.35)
			l.omni_range = 13.0
			l.light_energy = 1.4
			l.light_color = Color(1.0, 0.82, 0.55)
			l.visible = false
			world.get_node("Lights").add_child(l)
			streetlights.append(l)
		"trash":
			var green := Color(0.42, 0.55, 0.45)
			deco.add_cylinder(pos + Vector3(0, 0.45, 0), 0.3, 0.9, green, 16, "metal")
			deco.add_cylinder(pos + Vector3(0, 0.88, 0), 0.32, 0.06, green.darkened(0.2), 16, "metal")
			deco.add_cylinder(pos + Vector3(0, 0.93, 0), 0.24, 0.06, green.darkened(0.3), 16, "metal")
			deco.add_cylinder(pos + Vector3(0, 0.03, 0), 0.31, 0.06, iron, 16, "metal")
			add_cyl_collider(pos + Vector3(0, 0.45, 0), 0.3, 0.9)
			var fn2 := Furniture.new()
			fn2.name = p.container.id
			world.get_node("Props").add_child(fn2)
			fn2.position = pos
			fn2.setup({"type": "trash", "rot": rot, "container": p.container, "id": p.container.id}, world, "")
			furniture[p.container.id] = fn2
		"bus_stop":
			var fr := Color(0.38, 0.42, 0.45)
			B.call(Vector3(0, 2.45, 0.3), Vector3(3.3, 0.08, 1.5), Color(0.75, 0.76, 0.77), "steel")
			for x in [-1.5, 1.5]:
				for z in [-0.3, 0.9]:
					B.call(Vector3(x, 1.22, z), Vector3(0.07, 2.44, 0.07), fr, "metal")
			B.call(Vector3(0, 1.35, 0.92), Vector3(3.0, 1.9, 0.02), Color.WHITE, "glass")
			B.call(Vector3(0, 0.28, 0.92), Vector3(3.0, 0.06, 0.05), fr, "metal")
			B.call(Vector3(0, 2.3, 0.92), Vector3(3.0, 0.06, 0.05), fr, "metal")
			B.call(Vector3(0, 0.46, 0.66), Vector3(2.4, 0.04, 0.36), WOOD, "wood_out")
			for x in [-1.0, 0.0, 1.0]:
				B.call(Vector3(x, 0.23, 0.66), Vector3(0.05, 0.46, 0.3), fr, "metal")
			B.call(Vector3(1.9, 1.5, 0.3), Vector3(0.06, 3.0, 0.06), fr, "metal")
			B.call(Vector3(1.9, 2.8, 0.3), Vector3(0.55, 0.55, 0.04), Color(0.2, 0.42, 0.62), "metal")
		"doormat":
			B.call(Vector3(0, 0.015, 0), Vector3(1.0, 0.025, 0.6), Color(0.55, 0.4, 0.3), "fabric")
			var fn3 := Furniture.new()
			fn3.name = str(p.id)
			world.get_node("Props").add_child(fn3)
			fn3.position = pos
			fn3.setup({"type": "doormat", "rot": rot, "id": p.id, "hidden_item": p.get("hidden_item", "")}, world, "")
			furniture[p.id] = fn3
		"pump":
			B.call(Vector3(0, 0.15, 0), Vector3(1.4, 0.3, 0.9), Color(0.7, 0.7, 0.68), "concrete")
			B.call(Vector3(0, 1.0, 0), Vector3(0.7, 1.6, 0.5), Color(0.8, 0.3, 0.22), "metal")
			B.call(Vector3(0, 1.82, 0), Vector3(0.74, 0.08, 0.54), Color(0.9, 0.9, 0.88), "metal")
			B.call(Vector3(0, 1.35, -0.26), Vector3(0.45, 0.35, 0.02), Color(0.1, 0.16, 0.22), "")
			T.call(Vector3(0.38, 1.25, 0), Vector3(0.45, 0.6, -0.1), 0.025, Color(0.1, 0.1, 0.1), "rubber")
			B.call(Vector3(0.38, 1.0, 0), Vector3(0.08, 0.3, 0.1), iron, "metal")
			add_collider(xf * Vector3(0, 0.9, 0), Vector3(0.8, 1.8, 0.6), rot)
			var fn4 := Furniture.new()
			fn4.name = str(p.id)
			world.get_node("Props").add_child(fn4)
			fn4.position = pos
			fn4.setup({"type": "pump", "rot": rot, "id": p.id}, world, "")
			furniture[p.id] = fn4
		"canopy":
			var s: Array = p.get("size", [12, 8])
			B.call(Vector3(0, 4.6, 0), Vector3(float(s[0]), 0.4, float(s[1])), Color(0.92, 0.92, 0.9), "metal")
			B.call(Vector3(0, 4.6, 0), Vector3(float(s[0]) + 0.1, 0.25, float(s[1]) + 0.1), Color(0.8, 0.3, 0.22), "metal")
			for x in [-1.0, 1.0]:
				for z in [-1.0, 1.0]:
					B.call(Vector3(x * (float(s[0]) * 0.5 - 0.6), 2.2, z * (float(s[1]) * 0.5 - 0.6)), Vector3(0.3, 4.4, 0.3), Color(0.85, 0.85, 0.83), "metal")
					add_collider(pos + Vector3(x * (float(s[0]) * 0.5 - 0.6), 2.2, z * (float(s[1]) * 0.5 - 0.6)), Vector3(0.3, 4.4, 0.3))
			lamp_heads.add_box(Transform3D(Basis(), pos + Vector3(0, 4.38, 0)), Vector3(float(s[0]) * 0.6, 0.06, 0.4), Color.WHITE)
		"fountain":
			var stone := Color(0.82, 0.8, 0.76)
			deco.add_cylinder(pos + Vector3(0, 0.3, 0), 2.6, 0.6, stone, 32, "concrete")
			deco.add_cylinder(pos + Vector3(0, 0.62, 0), 2.62, 0.06, stone.darkened(0.1), 32, "concrete")
			deco.add_cylinder(pos + Vector3(0, 0.56, 0), 2.35, 0.04, Color.WHITE, 32, "water")
			deco.add_cylinder(pos + Vector3(0, 1.1, 0), 0.35, 1.6, stone, 16, "concrete")
			deco.add_cylinder(pos + Vector3(0, 1.9, 0), 0.9, 0.15, stone, 20, "concrete")
			deco.add_cylinder(pos + Vector3(0, 1.97, 0), 0.75, 0.02, Color.WHITE, 20, "water")
			add_cyl_collider(pos + Vector3(0, 0.4, 0), 2.6, 0.8)
		"kiosk":
			B.call(Vector3(0, 1.2, 0), Vector3(2.2, 2.4, 1.8), Color(0.45, 0.62, 0.52), "corrugated")
			B.call(Vector3(0, 2.5, 0), Vector3(2.6, 0.15, 2.2), Color(0.9, 0.75, 0.38), "metal")
			B.call(Vector3(0, 1.3, -0.91), Vector3(1.6, 0.9, 0.02), Color(0.96, 0.94, 0.88), "")
			B.call(Vector3(0, 0.85, -1.0), Vector3(1.8, 0.05, 0.25), WOOD, "wood_out")
			add_collider(xf * Vector3(0, 1.2, 0), Vector3(2.2, 2.4, 1.8), rot)
		"mailbox":
			B.call(Vector3(0, 0.55, 0), Vector3(0.07, 1.1, 0.07), iron, "metal")
			B.call(Vector3(0, 1.15, 0), Vector3(0.45, 0.3, 0.3), Color(0.72, 0.28, 0.22), "metal")
			B.call(Vector3(0, 1.32, 0), Vector3(0.47, 0.04, 0.32), Color(0.6, 0.22, 0.18), "metal")
			if p.has("id"):
				_prop_furniture(p, pos, rot)
		"sale_sign":
			B.call(Vector3(-0.5, 0.7, 0), Vector3(0.07, 1.4, 0.07), WOOD, "wood_out")
			B.call(Vector3(0.5, 0.7, 0), Vector3(0.07, 1.4, 0.07), WOOD, "wood_out")
			B.call(Vector3(0, 1.25, 0), Vector3(1.3, 0.6, 0.05), Color(0.75, 0.25, 0.2), "metal")
			var sl := Label3D.new()
			sl.text = tr("VENDE-SE")
			sl.font_size = 48
			sl.pixel_size = 0.006
			sl.outline_size = 8
			sl.position = xf * Vector3(0, 1.25, -0.04)
			sl.rotation_degrees.y = rot
			world.get_node("Props").add_child(sl)
			_prop_furniture(p, pos, rot)
		"sign":
			B.call(Vector3(0, 1.0, 0), Vector3(0.08, 2.0, 0.08), iron, "metal")
			B.call(Vector3(0, 2.1, 0), Vector3(2.6, 0.6, 0.06), Color(0.2, 0.36, 0.28), "metal")
			var lbl := Label3D.new()
			lbl.text = str(p.get("text", ""))
			lbl.font_size = 48
			lbl.pixel_size = 0.006
			lbl.position = xf * Vector3(0, 2.1, 0.04)
			lbl.rotation_degrees.y = rot + 180.0
			world.get_node("Props").add_child(lbl)
		"hedge":
			var a := Vector3(float(p.from[0]), 0, float(p.from[1]))
			var e := Vector3(float(p.to[0]), 0, float(p.to[1]))
			var r2 := 0.0 if absf(a.z - e.z) < 0.01 else 90.0
			var len := a.distance_to(e)
			# canteiro de concreto com arbustos de folhas
			deco.box((a + e) * 0.5 + Vector3(0, 0.15, 0), Vector3(len + 0.2, 0.3, 0.9), Color(0.7, 0.68, 0.64), r2, "concrete")
			deco.box((a + e) * 0.5 + Vector3(0, 0.31, 0), Vector3(len, 0.02, 0.7), Color(0.35, 0.26, 0.18), r2, "dirt")
			var hr := RandomNumberGenerator.new()
			hr.seed = int(a.x * 31.0 + a.z * 17.0)
			var n := int(len / 0.9)
			for i in n + 1:
				var c := a.lerp(e, (i + 0.5) / float(n + 1)) + Vector3(0, 0.75, 0)
				foliage.add_canopy(c - Vector3(0, 0.5, 0), [[c, Vector3(0.55, 0.5, 0.45) * hr.randf_range(0.9, 1.15)]], Color(0.26, 0.36, 0.17).lerp(Color(0.33, 0.42, 0.2), hr.randf()), hr, 26, 0.55)
			add_collider((a + e) * 0.5 + Vector3(0, 0.55, 0), Vector3(len, 1.1, 0.8), r2)

## Árvore: tronco afunilado, galhos e copa de cartões de folhas (paleta de outono).
const LEAF_TINTS := [Color(0.30, 0.44, 0.17), Color(0.62, 0.62, 0.22), Color(0.88, 0.68, 0.24)]

func _build_tree(pos: Vector3, variant: int) -> void:
	var trng := RandomNumberGenerator.new()
	trng.seed = int(pos.x * 73.0 + pos.z * 191.0)
	var sc := trng.randf_range(1.2, 1.75)
	var h := 2.6 * sc
	var bark := Color(0.52, 0.45, 0.38).lerp(Color(0.85, 0.82, 0.76), 0.6 if variant == 2 else 0.0)
	# tronco levemente inclinado, raízes e galhos até cada maço da copa
	var lean := Vector3(trng.randf_range(-0.25, 0.25), 0, trng.randf_range(-0.25, 0.25))
	var top := pos + Vector3(0, h, 0) + lean
	deco.add_tube(pos + Vector3(0, -0.1, 0), top, 0.17 * sc, bark, 9, "wood_out")
	for i in 4:
		var ra := TAU * i / 4.0 + trng.randf() * 0.6
		deco.add_tube(pos + Vector3(0, 0.25, 0), pos + Vector3(cos(ra), -0.05, sin(ra)) * 0.55 * sc, 0.07 * sc, bark, 6, "wood_out")
	var clusters: Array = [[top + Vector3(0, 1.4, 0) * sc, Vector3(1.55, 1.2, 1.55) * sc]]
	var n := 5 + trng.randi() % 3
	for i in n:
		var a := trng.randf() * 0.5 + i * TAU / n
		var off := Vector3(cos(a), 0, sin(a)) * trng.randf_range(1.0, 1.7) * sc
		var cpos := top + off + Vector3(0, trng.randf_range(0.2, 1.6) * sc, 0)
		clusters.append([cpos, Vector3(1.0, 0.8, 1.0) * sc * trng.randf_range(0.8, 1.15)])
		var mid := top.lerp(cpos, 0.55) + Vector3(0, 0.2, 0)
		deco.add_tube(top - Vector3(0, 0.3, 0), mid, 0.08 * sc, bark, 6, "wood_out")
		deco.add_tube(mid, cpos, 0.045 * sc, bark, 5, "wood_out")
	deco.add_tube(top - Vector3(0, 0.2, 0), top + Vector3(0, 1.3, 0) * sc, 0.09 * sc, bark, 6, "wood_out")
	# Maioria verde; ipês amarelos como destaque
	var pick: int = [0, 0, 0, 0, 0, 1, 2][trng.randi() % 7]
	var tint: Color = LEAF_TINTS[pick].lerp(LEAF_TINTS[min(pick + 1, 2)], trng.randf() * 0.15)
	foliage.add_canopy(top, clusters, tint, trng, 54)
	add_cyl_collider(pos + Vector3(0, 1.0, 0), 0.25, 2.0)

func _prop_furniture(p: Dictionary, pos: Vector3, rot: float) -> void:
	var fn := Furniture.new()
	fn.name = str(p.id)
	world.get_node("Props").add_child(fn)
	fn.position = pos
	var d := p.duplicate()
	d.rot = rot
	fn.setup(d, world, "")
	furniture[p.id] = fn

func _add_spot(id: String, pos: Vector3, rot: float, pose: String, hidden: bool, building: String, fn: Furniture = null) -> void:
	spots[id] = {"pos": pos, "rot": rot, "pose": pose, "hidden": hidden, "building": building, "furniture": fn}

# ------------------------------------------------------------------ navegação
func _bake_navigation(region: NavigationRegion3D) -> void:
	var nm := NavigationMesh.new()
	nm.agent_radius = 0.35
	nm.agent_height = 1.7
	nm.agent_max_climb = 0.3
	nm.cell_size = 0.25
	nm.cell_height = 0.25
	nm.geometry_parsed_geometry_type = NavigationMesh.PARSED_GEOMETRY_STATIC_COLLIDERS
	nm.geometry_collision_mask = 1
	nm.geometry_source_geometry_mode = NavigationMesh.SOURCE_GEOMETRY_ROOT_NODE_CHILDREN
	var b: Array = map.get("bounds", [-85, -85, 85, 85])
	nm.filter_baking_aabb = AABB(Vector3(float(b[0]), -1, float(b[1])), Vector3(float(b[2]) - float(b[0]), 6, float(b[3]) - float(b[1])))
	region.navigation_mesh = nm
	region.bake_navigation_mesh(false)
