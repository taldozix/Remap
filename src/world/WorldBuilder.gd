class_name WorldBuilder
extends RefCounted
## Constrói o bairro a partir de data/map/*.json: chão, ruas, calçadas, marcações,
## prédios (paredes com corte, telhado, piso, portas, janelas), mobília funcional,
## árvores, postes e mobiliário urbano. Depois gera a malha de navegação dos NPCs.

const WALL_T := 0.2
const DOOR_H := 2.15
const SIDEWALK_H := 0.12
const STOP_DIST := 8.2   # distância do centro do cruzamento até a faixa de retenção

const WOOD := Color("8b5e34")
const WOOD_DARK := Color("5e3d22")
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
	sidewalks.build(visual_root, "Sidewalks", 0.8, true, Mat.sidewalk())
	asphalt_b.build(visual_root, "Asphalt", 0.9, false, Mat.asphalt())
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
			deco.box(Vector3(s * (lim - 0.5), 0.6, i), Vector3(0.08, 1.2, 0.08), WOOD_DARK)
			deco.box(Vector3(i, 0.6, s * (lim - 0.5)), Vector3(0.08, 1.2, 0.08), WOOD_DARK)
		deco.box(Vector3(s * (lim - 0.5), 0.95, 0), Vector3(0.05, 0.08, lim * 2), WOOD)
		deco.box(Vector3(0, 0.95, s * (lim - 0.5)), Vector3(lim * 2, 0.08, 0.05), WOOD)

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
			_sidewalk(Rect2(x0, z0 + sw, sw, z1 - z0 - 2 * sw))
			_sidewalk(Rect2(x1 - sw, z0 + sw, sw, z1 - z0 - 2 * sw))
	_sidewalk(Rect2(x_min - sw, z_min - sw, x_max - x_min + 2 * sw, sw))
	_sidewalk(Rect2(x_min - sw, z_max, x_max - x_min + 2 * sw, sw))
	_sidewalk(Rect2(x_min - sw, z_min, sw, z_max - z_min))
	_sidewalk(Rect2(x_max, z_min, sw, z_max - z_min))
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
				_mesh(bm, Mat.asphalt(), Vector3((float(r[0]) + float(r[2])) * 0.5, 0.0, (float(r[1]) + float(r[3])) * 0.5)).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			"plaza", "path":
				var r2: Array = a.rect
				var bm2 := BoxMesh.new()
				bm2.size = Vector3(float(r2[2]) - float(r2[0]), 0.03, float(r2[3]) - float(r2[1]))
				_mesh(bm2, Mat.sidewalk(), Vector3((float(r2[0]) + float(r2[2])) * 0.5, 0.0, (float(r2[1]) + float(r2[3])) * 0.5)).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			"drive_lane":
				var pts: Array = a.points
				for i in pts.size() - 1:
					var p0 := Vector3(float(pts[i][0]), 0, float(pts[i][1]))
					var p1 := Vector3(float(pts[i + 1][0]), 0, float(pts[i + 1][1]))
					var len := p0.distance_to(p1)
					var bm3 := BoxMesh.new()
					bm3.size = Vector3(4.0, 0.024, len + 4.0)
					var mi := _mesh(bm3, Mat.asphalt(), (p0 + p1) * 0.5)
					mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
					mi.look_at(p1 + Vector3(0, 0, 0.0001), Vector3.UP)
					mi.position.y = 0.0

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
	_mesh(fm, Mat.floor_tiles(Color(style.get("floor", "#a08060")), planks), Vector3(0, 0.0, 0), node)
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
	if b.get("type", "") == "house":
		var pm := PrismMesh.new()
		pm.size = Vector3(d + 0.9, 1.5, w + 0.9)
		var roof := _mesh(pm, Mat.color(roof_col, 0.75), Vector3(0, h + 0.75, 0), node.roof)
		roof.rotation_degrees.y = 90
		var ch := BoxMesh.new()
		ch.size = Vector3(0.6, 1.4, 0.6)
		_mesh(ch, Mat.color(wall_col.darkened(0.25)), Vector3(w * 0.25, h + 1.2, -d * 0.18), node.roof)
	else:
		var sm := BoxMesh.new()
		sm.size = Vector3(w + 0.3, 0.25, d + 0.3)
		_mesh(sm, Mat.color(roof_col, 0.9), Vector3(0, h + 0.12, 0), node.roof)
		for s in [-1.0, 1.0]:
			var pa := BoxMesh.new()
			pa.size = Vector3(w + 0.3, 0.5, 0.15)
			_mesh(pa, Mat.color(trim_col), Vector3(0, h + 0.45, s * (d * 0.5 + 0.08)), node.roof)
			var pb := BoxMesh.new()
			pb.size = Vector3(0.15, 0.5, d + 0.3)
			_mesh(pb, Mat.color(trim_col), Vector3(s * (w * 0.5 + 0.08), h + 0.45, 0), node.roof)
		if b.get("type", "") in ["shop", "restaurant", "garage"]:
			var ac := BoxMesh.new()
			ac.size = Vector3(1.2, 0.7, 0.9)
			_mesh(ac, Mat.color(STEEL, 0.5, 0.4), Vector3(w * 0.2, h + 0.6, 0), node.roof)
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

func _build_wall_side(node: Building, b: Dictionary, side: String, w: float, d: float, h: float, openings: Array, wall_col: Color, trim_col: Color, accent: Color) -> void:
	var f := _side_frame(side, w, d)
	var full := MeshBatcher.new()
	var low := MeshBatcher.new()
	var panes := MeshBatcher.new()
	var half: float = f.len * 0.5
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
	for s in segs:
		var mid := (float(s[0]) + float(s[1])) * 0.5
		var len := float(s[1]) - float(s[0])
		if len <= 0.01:
			continue
		var center: Vector3 = f.fixed + f.axis * mid
		full.box(center + Vector3(0, h * 0.5, 0), Vector3(len, h, WALL_T), wall_col, f.rot)
		full.box(center + Vector3(0, 0.15, 0) + f.out * 0.02, Vector3(len, 0.3, WALL_T + 0.04), wall_col.darkened(0.3), f.rot)
		full.box(center + Vector3(0, h - 0.08, 0) + f.out * 0.02, Vector3(len, 0.16, WALL_T + 0.06), trim_col, f.rot)
		low.box(center + Vector3(0, 0.3, 0), Vector3(len, 0.6, WALL_T), wall_col.darkened(0.08), f.rot)
		add_collider(origin + center + Vector3(0, h * 0.5, 0), Vector3(len, h, WALL_T), f.rot)
	for cut in cuts:
		var dd: Dictionary = cut[2]
		var door_h := 3.4 if dd.get("rollup", false) else DOOR_H
		var mid2 := (float(cut[0]) + float(cut[1])) * 0.5
		var len2 := float(cut[1]) - float(cut[0])
		var center2: Vector3 = f.fixed + f.axis * mid2
		if h > door_h:
			full.box(center2 + Vector3(0, (h + door_h) * 0.5, 0), Vector3(len2, h - door_h, WALL_T), wall_col, f.rot)
			add_collider(origin + center2 + Vector3(0, (h + door_h) * 0.5, 0), Vector3(len2, h - door_h, WALL_T), f.rot)
		# batente
		full.box(center2 + Vector3(0, door_h + 0.05, 0), Vector3(len2 + 0.2, 0.1, WALL_T + 0.08), trim_col, f.rot)
		var door := Door.new()
		door.name = dd.id
		door.setup(dd, world, b.id, WALL_T, accent.darkened(0.15))
		door.position = center2
		door.rotation_degrees.y = f.rot
		node.add_child(door)
		doors[dd.id] = door
	# Janelas (vidros acendem à noite)
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
		var wc: Vector3 = f.fixed + f.axis * off
		panes.box(wc + Vector3(0, wy, 0), Vector3(ww, wh, WALL_T + 0.04), Color.WHITE, f.rot)
		full.box(wc + Vector3(0, wy - wh * 0.5 - 0.05, 0) + f.out * 0.05, Vector3(ww + 0.2, 0.1, WALL_T + 0.14), trim_col, f.rot)
		full.box(wc + Vector3(0, wy + wh * 0.5 + 0.04, 0), Vector3(ww + 0.16, 0.08, WALL_T + 0.08), trim_col, f.rot)
		if not is_low and b.get("type", "") == "house":
			for sgn in [-1.0, 1.0]:
				full.box(wc + f.axis * sgn * (ww * 0.5 + 0.22) + Vector3(0, wy, 0) + f.out * 0.12, Vector3(0.36, wh + 0.1, 0.05), accent, f.rot)
		if b.get("floors", 1) > 1:
			panes.box(wc + Vector3(0, wy + 3.1, 0), Vector3(ww, wh, WALL_T + 0.04), Color.WHITE, f.rot)
			full.box(wc + Vector3(0, wy + 3.1 - wh * 0.5 - 0.05, 0) + f.out * 0.05, Vector3(ww + 0.2, 0.1, WALL_T + 0.14), trim_col, f.rot)
	# Toldo na fachada de lojas
	if b.get("type", "") in ["shop", "restaurant"] and b.get("doors", []).size() > 0 and b.doors[0].side == side:
		var aw: Vector3 = f.fixed + f.out * 0.8 + Vector3(0, 2.75, 0)
		full.box(aw, Vector3(min(f.len - 1.0, 9.0), 0.12, 1.6), accent, f.rot)
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
		batch.box((a + e) * 0.5 + Vector3(0, h * 0.5, 0), Vector3(len, h, 0.12), Color(b.style.get("wall", "#dddddd")).lightened(0.1), rot)
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
	var B := func(p: Vector3, s: Vector3, c: Color) -> void:
		bt.add_box(xf * Transform3D(Basis(), p), s, c)
	match f.type:
		"bed":
			B.call(Vector3(0, 0.18, 0), Vector3(1.4, 0.36, 2.0), WOOD)
			B.call(Vector3(0, 0.43, 0.05), Vector3(1.3, 0.16, 1.85), WHITE)
			B.call(Vector3(0, 0.56, -0.68), Vector3(1.0, 0.12, 0.38), Color("f4f1ea"))
			B.call(Vector3(0, 0.53, 0.32), Vector3(1.34, 0.08, 1.25), accent)
			B.call(Vector3(0, 0.55, -0.98), Vector3(1.44, 1.1, 0.08), WOOD_DARK)
			return Vector3(1.4, 0.6, 2.0)
		"wardrobe":
			B.call(Vector3(0, 1.0, 0), Vector3(1.2, 2.0, 0.6), WOOD)
			B.call(Vector3(0, 1.0, -0.31), Vector3(0.02, 1.9, 0.02), WOOD_DARK)
			B.call(Vector3(0.08, 1.05, -0.32), Vector3(0.04, 0.2, 0.03), STEEL)
			B.call(Vector3(-0.08, 1.05, -0.32), Vector3(0.04, 0.2, 0.03), STEEL)
			return Vector3(1.2, 2.0, 0.6)
		"nightstand":
			B.call(Vector3(0, 0.25, 0), Vector3(0.45, 0.5, 0.45), WOOD)
			B.call(Vector3(0, 0.62, 0), Vector3(0.18, 0.25, 0.18), Color("f0d890"))
			return Vector3.ZERO
		"shower":
			B.call(Vector3(0, 0.03, 0), Vector3(0.95, 0.06, 0.95), WHITE)
			B.call(Vector3(0, 1.0, 0.47), Vector3(0.95, 2.0, 0.03), Color(0.75, 0.85, 0.9))
			B.call(Vector3(0, 1.9, -0.3), Vector3(0.05, 0.4, 0.05), STEEL)
			B.call(Vector3(0, 2.05, -0.2), Vector3(0.2, 0.04, 0.2), STEEL)
			return Vector3.ZERO
		"toilet":
			B.call(Vector3(0, 0.2, 0.05), Vector3(0.4, 0.4, 0.55), WHITE)
			B.call(Vector3(0, 0.55, -0.22), Vector3(0.42, 0.5, 0.18), WHITE)
			return Vector3(0.45, 0.8, 0.65)
		"fridge":
			B.call(Vector3(0, 0.9, 0), Vector3(0.7, 1.8, 0.7), WHITE)
			B.call(Vector3(0, 1.2, -0.36), Vector3(0.68, 0.02, 0.02), Color("cccccc"))
			B.call(Vector3(0.28, 1.45, -0.37), Vector3(0.04, 0.3, 0.04), STEEL)
			return Vector3(0.7, 1.8, 0.7)
		"stove":
			B.call(Vector3(0, 0.45, 0), Vector3(0.7, 0.9, 0.62), WHITE)
			B.call(Vector3(0, 0.91, 0), Vector3(0.66, 0.03, 0.58), DARK)
			for p in [Vector3(-0.16, 0.93, -0.13), Vector3(0.16, 0.93, -0.13), Vector3(-0.16, 0.93, 0.13), Vector3(0.16, 0.93, 0.13)]:
				B.call(p, Vector3(0.16, 0.02, 0.16), Color("555555"))
			B.call(Vector3(0, 0.45, -0.32), Vector3(0.5, 0.35, 0.02), Color("333333"))
			return Vector3(0.7, 0.95, 0.62)
		"sink":
			B.call(Vector3(0, 0.45, 0), Vector3(0.7, 0.9, 0.62), WOOD)
			B.call(Vector3(0, 0.91, 0), Vector3(0.72, 0.04, 0.64), Color("dddddd"))
			B.call(Vector3(0, 0.9, 0.05), Vector3(0.45, 0.04, 0.35), STEEL.darkened(0.3))
			B.call(Vector3(0, 1.1, 0.27), Vector3(0.04, 0.3, 0.04), STEEL)
			return Vector3(0.7, 0.95, 0.62)
		"table":
			B.call(Vector3(0, 0.74, 0), Vector3(1.2, 0.05, 0.8), WOOD)
			for p in [Vector3(-0.52, 0.36, -0.32), Vector3(0.52, 0.36, -0.32), Vector3(-0.52, 0.36, 0.32), Vector3(0.52, 0.36, 0.32)]:
				B.call(p, Vector3(0.06, 0.72, 0.06), WOOD_DARK)
			return Vector3(1.2, 0.78, 0.8)
		"chair":
			B.call(Vector3(0, 0.45, 0), Vector3(0.45, 0.05, 0.45), WOOD)
			B.call(Vector3(0, 0.75, 0.2), Vector3(0.45, 0.6, 0.05), WOOD)
			for p in [Vector3(-0.19, 0.22, -0.19), Vector3(0.19, 0.22, -0.19), Vector3(-0.19, 0.22, 0.19), Vector3(0.19, 0.22, 0.19)]:
				B.call(p, Vector3(0.04, 0.45, 0.04), WOOD_DARK)
			return Vector3.ZERO
		"sofa":
			B.call(Vector3(0, 0.22, 0.05), Vector3(2.0, 0.44, 0.85), accent.darkened(0.2))
			B.call(Vector3(0, 0.62, 0.38), Vector3(2.0, 0.6, 0.22), accent.darkened(0.3))
			B.call(Vector3(-0.95, 0.35, 0.05), Vector3(0.18, 0.4, 0.85), accent.darkened(0.35))
			B.call(Vector3(0.95, 0.35, 0.05), Vector3(0.18, 0.4, 0.85), accent.darkened(0.35))
			B.call(Vector3(-0.45, 0.46, -0.02), Vector3(0.85, 0.08, 0.65), accent)
			B.call(Vector3(0.45, 0.46, -0.02), Vector3(0.85, 0.08, 0.65), accent)
			return Vector3(2.0, 0.9, 0.9)
		"tv":
			B.call(Vector3(0, 0.25, 0), Vector3(1.2, 0.5, 0.4), WOOD_DARK)
			B.call(Vector3(0, 0.85, 0.05), Vector3(1.0, 0.6, 0.06), Color("111114"))
			return Vector3(1.2, 0.6, 0.4)
		"rug":
			B.call(Vector3(0, 0.035, 0), Vector3(2.0, 0.01, 1.4), accent.lightened(0.3))
			return Vector3.ZERO
		"plant":
			B.call(Vector3(0, 0.2, 0), Vector3(0.35, 0.4, 0.35), Color("a0522d"))
			bt.add_sphere(xf * Vector3(0, 0.7, 0), Vector3(0.35, 0.45, 0.35), Color("3f7a35"))
			return Vector3.ZERO
		"shelf":
			B.call(Vector3(0, 0.9, 0), Vector3(L, 1.8, 0.5), col.lightened(0.0) if f.has("color") else Color("d0d4d8"))
			for lvl in [0.35, 0.8, 1.25, 1.65]:
				var x := -L * 0.5 + 0.2
				while x < L * 0.5 - 0.2:
					var pc := Color.from_hsv(rng.randf(), 0.55, 0.85)
					var bh := rng.randf_range(0.18, 0.3)
					for side in [-1.0, 1.0]:
						B.call(Vector3(x, lvl + bh * 0.5, side * 0.27), Vector3(0.22, bh, 0.06), pc)
					x += rng.randf_range(0.25, 0.32)
			return Vector3(L, 1.8, 0.5)
		"fridge_display":
			B.call(Vector3(0, 1.0, 0), Vector3(L, 2.0, 0.7), Color("dfe6ea"))
			B.call(Vector3(0, 1.05, -0.36), Vector3(L - 0.1, 1.7, 0.02), Color(0.6, 0.85, 0.95))
			for i in int(L / 0.25):
				B.call(Vector3(-L * 0.5 + 0.15 + i * 0.25, 1.4, -0.3), Vector3(0.12, 0.25, 0.12), Color.from_hsv(rng.randf(), 0.6, 0.9))
				B.call(Vector3(-L * 0.5 + 0.15 + i * 0.25, 0.8, -0.3), Vector3(0.12, 0.25, 0.12), Color.from_hsv(rng.randf(), 0.6, 0.9))
			return Vector3(L, 2.0, 0.7)
		"counter":
			B.call(Vector3(0, 0.5, 0), Vector3(L, 1.0, 0.7), accent.darkened(0.1))
			B.call(Vector3(0, 1.02, 0), Vector3(L + 0.05, 0.05, 0.75), Color("e8e2d6"))
			if f.get("register", false):
				B.call(Vector3(L * 0.3, 1.15, 0), Vector3(0.4, 0.2, 0.35), DARK)
				B.call(Vector3(L * 0.3, 1.3, 0.05), Vector3(0.3, 0.18, 0.04), Color("2b3a4a"))
			return Vector3(L, 1.05, 0.7)
		"crate":
			B.call(Vector3(0, 0.3, 0), Vector3(0.7, 0.6, 0.7), Color("a07a4a"))
			B.call(Vector3(0.1, 0.8, 0.05), Vector3(0.55, 0.4, 0.55), Color("b48a55"))
			return Vector3(0.7, 1.0, 0.7)
		"chest":
			B.call(Vector3(0, 0.27, 0), Vector3(0.95, 0.54, 0.5), col if f.has("color") else WOOD)
			B.call(Vector3(0, 0.45, 0), Vector3(0.97, 0.06, 0.52), WOOD_DARK)
			B.call(Vector3(0, 0.38, -0.26), Vector3(0.1, 0.12, 0.03), Color("c8a040"))
			return Vector3(0.95, 0.56, 0.5)
		"grill":
			B.call(Vector3(0, 0.45, 0), Vector3(1.3, 0.9, 0.7), STEEL)
			B.call(Vector3(0, 0.92, 0), Vector3(1.2, 0.05, 0.6), DARK)
			B.call(Vector3(0, 1.6, 0.25), Vector3(1.3, 0.25, 0.3), STEEL.darkened(0.2))
			return Vector3(1.3, 0.95, 0.7)
		"fryer":
			B.call(Vector3(0, 0.45, 0), Vector3(0.9, 0.9, 0.7), STEEL)
			B.call(Vector3(-0.2, 0.9, 0), Vector3(0.35, 0.04, 0.5), Color("c9a227"))
			B.call(Vector3(0.2, 0.9, 0), Vector3(0.35, 0.04, 0.5), Color("c9a227"))
			B.call(Vector3(0, 1.05, -0.3), Vector3(0.6, 0.06, 0.04), DARK)
			return Vector3(0.9, 0.95, 0.7)
		"soda_machine":
			B.call(Vector3(0, 0.85, 0), Vector3(0.8, 1.7, 0.6), Color("c0392b"))
			B.call(Vector3(0, 1.3, -0.31), Vector3(0.6, 0.45, 0.02), WHITE)
			for i in 3:
				B.call(Vector3(-0.2 + i * 0.2, 0.95, -0.33), Vector3(0.08, 0.12, 0.08), STEEL)
			return Vector3(0.8, 1.7, 0.6)
		"drive_window":
			B.call(Vector3(0, 1.0, 0.0), Vector3(1.4, 0.06, 0.5), Color("e8e2d6"))
			B.call(Vector3(0, 0.5, 0.05), Vector3(1.4, 1.0, 0.4), accent.darkened(0.1))
			B.call(Vector3(0.4, 1.12, 0), Vector3(0.35, 0.18, 0.3), DARK)
			return Vector3(1.4, 1.0, 0.5)
		"time_clock":
			B.call(Vector3(0, 1.4, 0.05), Vector3(0.35, 0.45, 0.12), Color("3a4a5a"))
			B.call(Vector3(0, 1.48, -0.02), Vector3(0.22, 0.12, 0.02), Color("9fe0a0"))
			return Vector3.ZERO
		"desk":
			B.call(Vector3(0, 0.74, 0), Vector3(1.4, 0.05, 0.7), WOOD)
			B.call(Vector3(-0.6, 0.36, 0), Vector3(0.08, 0.72, 0.65), WOOD_DARK)
			B.call(Vector3(0.6, 0.36, 0), Vector3(0.08, 0.72, 0.65), WOOD_DARK)
			B.call(Vector3(0, 1.0, 0.15), Vector3(0.55, 0.38, 0.04), DARK)
			return Vector3(1.4, 0.78, 0.7)
		"kitchen_counter":
			B.call(Vector3(0, 0.45, 0), Vector3(L, 0.9, 0.6), STEEL)
			B.call(Vector3(0, 0.92, 0), Vector3(L, 0.04, 0.62), STEEL.lightened(0.2))
			for i in 3:
				B.call(Vector3(-L * 0.3 + i * L * 0.3, 1.0, 0), Vector3(0.3, 0.12, 0.25), Color("f3c26b"))
			return Vector3(L, 0.95, 0.6)
		"lift":
			B.call(Vector3(-1.3, 1.0, 0), Vector3(0.25, 2.0, 0.25), Color("d04a2a"))
			B.call(Vector3(1.3, 1.0, 0), Vector3(0.25, 2.0, 0.25), Color("d04a2a"))
			B.call(Vector3(0, 0.08, 0), Vector3(2.4, 0.08, 3.6), Color("444444"))
			return Vector3.ZERO
		"workbench":
			B.call(Vector3(0, 0.45, 0), Vector3(L, 0.9, 0.7), Color("6a6e74"))
			B.call(Vector3(0, 0.92, 0), Vector3(L, 0.05, 0.72), WOOD)
			B.call(Vector3(0, 1.5, 0.32), Vector3(L, 1.0, 0.05), Color("8a8e94"))
			return Vector3(L, 0.95, 0.7)
		"tire_stack":
			for i in 3:
				bt.add_cylinder(xf * Vector3(0, 0.12 + i * 0.24, 0), 0.32, 0.22, Color("1d1d1f"), 10)
			return Vector3(0.6, 0.7, 0.6)
		"note":
			B.call(Vector3(0, 0.775, 0), Vector3(0.22, 0.005, 0.3), Color("fffbea"))
			return Vector3.ZERO
	return Vector3.ZERO

# ------------------------------------------------------------------ objetos externos
func _build_prop(p: Dictionary) -> void:
	var pos := Vector3(float(p.pos[0]), 0, float(p.pos[1])) if p.has("pos") else Vector3.ZERO
	var rot := float(p.get("rot", 0))
	var xf := Transform3D(Basis(Vector3.UP, deg_to_rad(rot)), pos)
	var B := func(lp: Vector3, s: Vector3, c: Color) -> void:
		deco.add_box(xf * Transform3D(Basis(), lp), s, c)
	match p.type:
		"tree":
			var v := int(p.get("variant", 0))
			var tones := [Color("3d6e2f"), Color("4f7f2a"), Color("2f5f3a")]
			deco.add_cylinder(pos + Vector3(0, 1.2, 0), 0.16, 2.4, Color("5b3b22"), 7)
			var sc := 1.0 + (v - 1) * 0.15
			deco.add_sphere(pos + Vector3(0, 3.2 * sc, 0), Vector3(1.6, 1.4, 1.6) * sc, tones[v])
			deco.add_sphere(pos + Vector3(0.6, 3.9 * sc, 0.3), Vector3(1.1, 1.0, 1.1) * sc, tones[v].lightened(0.08))
			deco.add_sphere(pos + Vector3(-0.5, 3.7 * sc, -0.4), Vector3(1.0, 0.9, 1.0) * sc, tones[v].darkened(0.08))
			add_cyl_collider(pos + Vector3(0, 1.0, 0), 0.25, 2.0)
		"bench":
			B.call(Vector3(0, 0.45, 0), Vector3(1.8, 0.06, 0.5), WOOD)
			B.call(Vector3(0, 0.75, 0.24), Vector3(1.8, 0.4, 0.05), WOOD)
			for x in [-0.8, 0.8]:
				B.call(Vector3(x, 0.22, 0), Vector3(0.08, 0.45, 0.5), DARK)
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
			B.call(Vector3(0, 2.9, 0), Vector3(0.14, 5.8, 0.14), Color("3a3f45"))
			B.call(Vector3(0, 5.75, -0.7), Vector3(0.1, 0.1, 1.5), Color("3a3f45"))
			lamp_heads.add_box(xf * Transform3D(Basis(), Vector3(0, 5.62, -1.35)), Vector3(0.45, 0.16, 0.3), Color.WHITE)
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
			deco.add_cylinder(pos + Vector3(0, 0.45, 0), 0.3, 0.9, Color("2e6b3f"), 10)
			deco.add_cylinder(pos + Vector3(0, 0.92, 0), 0.32, 0.06, Color("244f31"), 10)
			add_cyl_collider(pos + Vector3(0, 0.45, 0), 0.3, 0.9)
			var fn2 := Furniture.new()
			fn2.name = p.container.id
			world.get_node("Props").add_child(fn2)
			fn2.position = pos
			fn2.setup({"type": "trash", "rot": rot, "container": p.container, "id": p.container.id}, world, "")
			furniture[p.container.id] = fn2
		"bus_stop":
			B.call(Vector3(0, 2.4, 0.3), Vector3(3.2, 0.1, 1.4), Color("5b6e80"))
			for x in [-1.5, 1.5]:
				B.call(Vector3(x, 1.2, 0.85), Vector3(0.08, 2.4, 0.08), Color("3a3f45"))
			B.call(Vector3(0, 1.3, 0.95), Vector3(3.0, 1.8, 0.04), Color(0.7, 0.85, 0.95))
			B.call(Vector3(0, 0.45, 0.6), Vector3(2.4, 0.06, 0.4), WOOD)
			B.call(Vector3(1.9, 1.5, 0.3), Vector3(0.06, 3.0, 0.06), Color("3a3f45"))
			B.call(Vector3(1.9, 2.8, 0.3), Vector3(0.5, 0.5, 0.04), Color("2a6fb0"))
		"doormat":
			B.call(Vector3(0, 0.02, 0), Vector3(1.0, 0.03, 0.6), Color("7a4a2a"))
			var fn3 := Furniture.new()
			fn3.name = str(p.id)
			world.get_node("Props").add_child(fn3)
			fn3.position = pos
			fn3.setup({"type": "doormat", "rot": rot, "id": p.id, "hidden_item": p.get("hidden_item", "")}, world, "")
			furniture[p.id] = fn3
		"pump":
			B.call(Vector3(0, 0.15, 0), Vector3(1.4, 0.3, 0.9), Color("cccccc"))
			B.call(Vector3(0, 1.0, 0), Vector3(0.7, 1.6, 0.5), Color("d6452f"))
			B.call(Vector3(0, 1.35, -0.26), Vector3(0.45, 0.35, 0.02), Color("1b2a3a"))
			B.call(Vector3(0.38, 1.0, 0), Vector3(0.06, 0.6, 0.06), DARK)
			add_collider(xf * Vector3(0, 0.9, 0), Vector3(0.8, 1.8, 0.6), rot)
			var fn4 := Furniture.new()
			fn4.name = str(p.id)
			world.get_node("Props").add_child(fn4)
			fn4.position = pos
			fn4.setup({"type": "pump", "rot": rot, "id": p.id}, world, "")
			furniture[p.id] = fn4
		"canopy":
			var s: Array = p.get("size", [12, 8])
			B.call(Vector3(0, 4.6, 0), Vector3(float(s[0]), 0.4, float(s[1])), Color("f2f2f2"))
			B.call(Vector3(0, 4.6, 0), Vector3(float(s[0]) + 0.1, 0.25, float(s[1]) + 0.1), Color("d6452f"))
			for x in [-1.0, 1.0]:
				for z in [-1.0, 1.0]:
					B.call(Vector3(x * (float(s[0]) * 0.5 - 0.6), 2.2, z * (float(s[1]) * 0.5 - 0.6)), Vector3(0.3, 4.4, 0.3), Color("dddddd"))
					add_collider(pos + Vector3(x * (float(s[0]) * 0.5 - 0.6), 2.2, z * (float(s[1]) * 0.5 - 0.6)), Vector3(0.3, 4.4, 0.3))
			lamp_heads.add_box(Transform3D(Basis(), pos + Vector3(0, 4.38, 0)), Vector3(float(s[0]) * 0.6, 0.06, 0.4), Color.WHITE)
		"fountain":
			deco.add_cylinder(pos + Vector3(0, 0.3, 0), 2.6, 0.6, Color("b8b2a6"), 18)
			deco.add_cylinder(pos + Vector3(0, 0.55, 0), 2.3, 0.1, Color("4a87b0"), 18)
			deco.add_cylinder(pos + Vector3(0, 1.1, 0), 0.35, 1.6, Color("b8b2a6"), 10)
			deco.add_cylinder(pos + Vector3(0, 1.9, 0), 0.9, 0.15, Color("b8b2a6"), 12)
			add_cyl_collider(pos + Vector3(0, 0.4, 0), 2.6, 0.8)
		"kiosk":
			B.call(Vector3(0, 1.2, 0), Vector3(2.2, 2.4, 1.8), Color("2a7f62"))
			B.call(Vector3(0, 2.5, 0), Vector3(2.6, 0.15, 2.2), Color("f2c14e"))
			B.call(Vector3(0, 1.3, -0.91), Vector3(1.6, 0.9, 0.02), Color("f7f3e8"))
			add_collider(xf * Vector3(0, 1.2, 0), Vector3(2.2, 2.4, 1.8), rot)
		"mailbox":
			B.call(Vector3(0, 0.55, 0), Vector3(0.08, 1.1, 0.08), DARK)
			B.call(Vector3(0, 1.15, 0), Vector3(0.45, 0.3, 0.3), Color("b03a2e"))
			if p.has("id"):
				_prop_furniture(p, pos, rot)
		"sale_sign":
			B.call(Vector3(-0.5, 0.7, 0), Vector3(0.07, 1.4, 0.07), Color("e8e4d8"))
			B.call(Vector3(0.5, 0.7, 0), Vector3(0.07, 1.4, 0.07), Color("e8e4d8"))
			B.call(Vector3(0, 1.25, 0), Vector3(1.3, 0.6, 0.05), Color("c0392b"))
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
			B.call(Vector3(0, 1.0, 0), Vector3(0.1, 2.0, 0.1), DARK)
			B.call(Vector3(0, 2.1, 0), Vector3(2.6, 0.6, 0.08), Color("2a3a4a"))
			var lbl := Label3D.new()
			lbl.text = str(p.get("text", ""))
			lbl.font_size = 48
			lbl.pixel_size = 0.006
			lbl.position = xf * Vector3(0, 2.1, 0.06)
			lbl.rotation_degrees.y = rot + 180.0
			world.get_node("Props").add_child(lbl)
		"hedge":
			var a := Vector3(float(p.from[0]), 0, float(p.from[1]))
			var e := Vector3(float(p.to[0]), 0, float(p.to[1]))
			var r2 := 0.0 if absf(a.z - e.z) < 0.01 else 90.0
			var len := a.distance_to(e)
			deco.box((a + e) * 0.5 + Vector3(0, 0.55, 0), Vector3(len, 1.1, 0.7), Color("3f6f33"), r2)
			add_collider((a + e) * 0.5 + Vector3(0, 0.55, 0), Vector3(len, 1.1, 0.7), r2)

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
