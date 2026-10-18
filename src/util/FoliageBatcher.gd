class_name FoliageBatcher
extends RefCounted
## Agrupa os cartões de folhas das árvores por bloco de 40 m (uma malha por bloco).
## Normais esféricas (do centro da copa para fora) dão à copa um volume suave.

var chunks: Dictionary = {}

func _st(p: Vector3) -> SurfaceTool:
	var k := Vector2i(int(floor(p.x / ChunkBatcher.CHUNK)), int(floor(p.z / ChunkBatcher.CHUNK)))
	if not chunks.has(k):
		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		chunks[k] = st
	return chunks[k]

## Copa de árvore: `clusters` = [[centro, raio], ...]; `tint` = cor das folhas.
func add_canopy(trunk_top: Vector3, clusters: Array, tint: Color, rng: RandomNumberGenerator, cards_per_cluster: int = 46, card_size: float = 1.0) -> void:
	var st := _st(trunk_top)
	var canopy_center := Vector3.ZERO
	for c in clusters:
		canopy_center += c[0]
	canopy_center /= clusters.size()
	for c in clusters:
		var center: Vector3 = c[0]
		var radius: Vector3 = c[1]
		for i in cards_per_cluster:
			var dir := Vector3(rng.randf_range(-1, 1), rng.randf_range(-0.7, 1), rng.randf_range(-1, 1)).normalized()
			var p := center + dir * radius * rng.randf_range(0.35, 1.0)
			var size := rng.randf_range(0.55, 0.9) * card_size
			var n := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
			var t := n.cross(Vector3.UP if absf(n.y) < 0.9 else Vector3.RIGHT).normalized()
			var b := n.cross(t).normalized()
			var cell := rng.randi() % 4
			var uv0 := Vector2((cell % 2) * 0.5, (cell / 2) * 0.5)
			var normal := (p - canopy_center + Vector3(0, 0.6, 0)).normalized()
			var shade := rng.randf_range(0.82, 1.12) * lerpf(0.8, 1.1, clampf((p.y - trunk_top.y) / 3.0 + 0.5, 0.0, 1.0))
			var col := Color(tint.r * shade, tint.g * shade, tint.b * shade, clampf(p.distance_to(trunk_top) / 3.0, 0.2, 1.0))
			var corners := [
				[p - t * size * 0.5 - b * size * 0.5, uv0 + Vector2(0, 0.5)],
				[p + t * size * 0.5 - b * size * 0.5, uv0 + Vector2(0.5, 0.5)],
				[p + t * size * 0.5 + b * size * 0.5, uv0 + Vector2(0.5, 0)],
				[p - t * size * 0.5 + b * size * 0.5, uv0],
			]
			for tri in [[0, 2, 1], [0, 3, 2]]:
				for k in tri:
					st.set_color(col)
					st.set_normal(normal)
					st.set_uv(corners[k][1])
					st.add_vertex(corners[k][0])

func build(parent: Node3D) -> void:
	var root := Node3D.new()
	root.name = "Foliage"
	parent.add_child(root)
	for k in chunks:
		var mi := MeshInstance3D.new()
		mi.mesh = chunks[k].commit()
		mi.material_override = Nature.leaf_material()
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		mi.name = "Foliage_%d_%d" % [k.x, k.y]
		root.add_child(mi)
