class_name GrassField
extends Node3D
## Grama densa em tufos (MultiMesh em blocos de 20 m, com distância de visibilidade).
## Uma grade de ocupação (0,5 m) marca ruas, calçadas, prédios, trilhas e praças,
## onde não nasce grama. A densidade vem das opções gráficas.

const CELL := 0.5
const CHUNK := 20.0
const DENSITY := [0.0, 2.5, 5.0, 8.0]   # tufos por m² (desligado, baixa, média, alta)

var builder: WorldBuilder
var _grid: PackedByteArray
var _origin := Vector2.ZERO
var _w := 0
var _h := 0
var _tuft: ArrayMesh
var instances := 0

func build(p_builder: WorldBuilder, level: int) -> void:
	builder = p_builder
	for c in get_children():
		c.queue_free()
	instances = 0
	if level <= 0:
		return
	if _grid.is_empty():
		_rasterize()
	if _tuft == null:
		_tuft = _make_tuft()
	var density: float = DENSITY[clampi(level, 0, 3)]
	var spacing := 1.0 / sqrt(density)
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242
	var noise := FastNoiseLite.new()
	noise.seed = 9
	noise.frequency = 0.045
	var b: Array = builder.map.get("bounds", [-85, -85, 85, 85])
	var x0 := float(b[0]) + 1.0
	var z0 := float(b[1]) + 1.0
	var x1 := float(b[2]) - 1.0
	var z1 := float(b[3]) - 1.0
	var cx := x0
	while cx < x1:
		var cz := z0
		while cz < z1:
			_build_chunk(Rect2(cx, cz, min(CHUNK, x1 - cx), min(CHUNK, z1 - cz)), spacing, rng, noise)
			cz += CHUNK
		cx += CHUNK

func _build_chunk(r: Rect2, spacing: float, rng: RandomNumberGenerator, noise: FastNoiseLite) -> void:
	var xforms: Array[Transform3D] = []
	var colors: Array[Color] = []
	var z := r.position.y
	while z < r.end.y:
		var x := r.position.x
		while x < r.end.x:
			var px := x + rng.randf_range(0.0, spacing)
			var pz := z + rng.randf_range(0.0, spacing)
			if not blocked(px, pz):
				var n := noise.get_noise_2d(px, pz) * 0.5 + 0.5
				# Prados mais altos e amarelados em manchas; perto de bordas, mais baixa.
				var tall := lerpf(0.8, 1.45, n) * rng.randf_range(0.85, 1.15)
				var t := Transform3D(Basis(Vector3.UP, rng.randf() * TAU).scaled(Vector3(1.0, tall, 1.0)), Vector3(px, 0.0, pz))
				xforms.append(t)
				var dry := clampf(n * 1.3 - 0.55 + rng.randf_range(-0.1, 0.1), 0.0, 1.0)
				var tone := rng.randf_range(0.85, 1.12)
				colors.append(Color(tone, tone, tone).lerp(Color(1.35, 1.12, 0.6), dry * 0.55))
			x += spacing
		z += spacing
	if xforms.is_empty():
		return
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.mesh = _tuft
	mm.instance_count = xforms.size()
	for i in xforms.size():
		mm.set_instance_transform(i, xforms[i])
		mm.set_instance_color(i, colors[i])
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	mmi.material_override = Nature.grass_material()
	mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mmi.visibility_range_end = 70.0
	mmi.visibility_range_end_margin = 8.0
	mmi.visibility_range_fade_mode = GeometryInstance3D.VISIBILITY_RANGE_FADE_SELF
	add_child(mmi)
	instances += xforms.size()

## Tufo: 11 lâminas afinando até a ponta, levemente inclinadas. UV.y = 0 na raiz, 1 na ponta.
func _make_tuft() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	for i in 11:
		var a := rng.randf() * TAU
		var base := Vector3(cos(a), 0, sin(a)) * rng.randf_range(0.0, 0.2)
		var yaw := rng.randf() * TAU
		var side := Vector3(cos(yaw), 0, sin(yaw))
		var lean := Vector3(-sin(yaw), 0, cos(yaw)) * rng.randf_range(0.05, 0.16) + base.normalized() * 0.06
		var h := rng.randf_range(0.26, 0.48)
		var w := rng.randf_range(0.03, 0.05)
		var mid := base + lean * 0.4 + Vector3(0, h * 0.55, 0)
		var tip := base + lean + Vector3(0, h, 0)
		var n := side.cross(Vector3.UP).normalized()
		var verts := [
			[base - side * w, Vector2(0, 0)], [base + side * w, Vector2(1, 0)],
			[mid + side * w * 0.65, Vector2(1, 0.55)], [mid - side * w * 0.65, Vector2(0, 0.55)],
			[tip, Vector2(0.5, 1.0)],
		]
		for tri in [[0, 1, 2], [0, 2, 3], [3, 2, 4]]:
			for k in tri:
				st.set_normal(n)
				st.set_uv(verts[k][1])
				st.add_vertex(verts[k][0])
	return st.commit()

# ------------------------------------------------------------------ ocupação
func blocked(x: float, z: float) -> bool:
	var ix := int((x - _origin.x) / CELL)
	var iz := int((z - _origin.y) / CELL)
	if ix < 0 or iz < 0 or ix >= _w or iz >= _h:
		return true
	return _grid[iz * _w + ix] != 0

func _mark_rect(r: Rect2, margin: float = 0.0) -> void:
	var a := Vector2i(int((r.position.x - margin - _origin.x) / CELL), int((r.position.y - margin - _origin.y) / CELL))
	var e := Vector2i(int(ceil((r.end.x + margin - _origin.x) / CELL)), int(ceil((r.end.y + margin - _origin.y) / CELL)))
	for iz in range(max(0, a.y), min(_h, e.y)):
		for ix in range(max(0, a.x), min(_w, e.x)):
			_grid[iz * _w + ix] = 1

func _mark_segment(p0: Vector2, p1: Vector2, half_width: float) -> void:
	var minp := Vector2(min(p0.x, p1.x), min(p0.y, p1.y)) - Vector2.ONE * half_width
	var maxp := Vector2(max(p0.x, p1.x), max(p0.y, p1.y)) + Vector2.ONE * half_width
	for iz in range(max(0, int((minp.y - _origin.y) / CELL)), min(_h, int((maxp.y - _origin.y) / CELL) + 1)):
		for ix in range(max(0, int((minp.x - _origin.x) / CELL)), min(_w, int((maxp.x - _origin.x) / CELL) + 1)):
			var c := Vector2(_origin.x + (ix + 0.5) * CELL, _origin.y + (iz + 0.5) * CELL)
			if Geometry2D.get_closest_point_to_segment(c, p0, p1).distance_to(c) <= half_width:
				_grid[iz * _w + ix] = 1

func _rasterize() -> void:
	var b: Array = builder.map.get("bounds", [-85, -85, 85, 85])
	_origin = Vector2(float(b[0]), float(b[1]))
	_w = int((float(b[2]) - float(b[0])) / CELL) + 1
	_h = int((float(b[3]) - float(b[1])) / CELL) + 1
	_grid = PackedByteArray()
	_grid.resize(_w * _h)
	var roads: Dictionary = builder.map.roads
	var hw := float(roads.width) * 0.5
	for z in roads.zs:
		_mark_rect(Rect2(float(roads.xs[0]) - hw, float(z) - hw, float(roads.xs[-1]) - float(roads.xs[0]) + hw * 2, hw * 2))
	for x in roads.xs:
		_mark_rect(Rect2(float(x) - hw, float(roads.zs[0]) - hw, hw * 2, float(roads.zs[-1]) - float(roads.zs[0]) + hw * 2))
	for r in builder.sidewalk_rects:
		_mark_rect(r, 0.15)
	for bid in builder.buildings:
		_mark_rect(builder.buildings[bid].rect, 0.5)
	for a in builder.map.get("areas", []):
		if a.has("rect") and a.type != "park":
			var rr: Array = a.rect
			_mark_rect(Rect2(float(rr[0]), float(rr[1]), float(rr[2]) - float(rr[0]), float(rr[3]) - float(rr[1])), 0.2)
		if a.has("points"):
			var hwid := 2.3 if a.type == "drive_lane" else float(a.get("width", 2.0)) * 0.5 - 0.15
			var pts: Array = a.points
			for i in pts.size() - 1:
				_mark_segment(Vector2(float(pts[i][0]), float(pts[i][1])), Vector2(float(pts[i + 1][0]), float(pts[i + 1][1])), hwid)
	for p in builder.map.get("props", []):
		match p.get("type", ""):
			"fountain":
				_mark_segment(Vector2(float(p.pos[0]), float(p.pos[1])), Vector2(float(p.pos[0]), float(p.pos[1])), 2.9)
			"canopy", "bus_stop", "kiosk", "pump":
				_mark_segment(Vector2(float(p.pos[0]), float(p.pos[1])), Vector2(float(p.pos[0]), float(p.pos[1])), 3.0 if p.type != "canopy" else 7.0)
