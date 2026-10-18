class_name ChunkBatcher
extends RefCounted
## Distribui geometria estática em blocos de CHUNK metros (um MeshBatcher por bloco,
## com uma superfície por material). Melhora o descarte por visibilidade e respeita o
## limite de luzes por objeto do renderizador de compatibilidade (postes à noite).

const CHUNK := 40.0

var chunks: Dictionary = {}

func _batch(p: Vector3) -> MeshBatcher:
	var k := Vector2i(int(floor(p.x / CHUNK)), int(floor(p.z / CHUNK)))
	if not chunks.has(k):
		chunks[k] = MeshBatcher.new()
	return chunks[k]

func add_box(xform: Transform3D, size: Vector3, col: Color, mat: String = "") -> void:
	_batch(xform.origin).add_box(xform, size, col, mat)

func box(center: Vector3, size: Vector3, col: Color, rot_deg: float = 0.0, mat: String = "") -> void:
	_batch(center).box(center, size, col, rot_deg, mat)

func add_cylinder(center: Vector3, radius: float, height: float, col: Color, segments: int = 8, mat: String = "") -> void:
	_batch(center).add_cylinder(center, radius, height, col, segments, mat)

func add_tube(a: Vector3, b: Vector3, radius: float, col: Color, segments: int = 6, mat: String = "") -> void:
	_batch((a + b) * 0.5).add_tube(a, b, radius, col, segments, mat)

func add_sphere(center: Vector3, radius: Vector3, col: Color, rings: int = 5, segments: int = 8, mat: String = "") -> void:
	_batch(center).add_sphere(center, radius, col, rings, segments, mat)

func add_quad(p: Array, col: Color, mat: String = "") -> void:
	_batch((p[0] + p[2]) * 0.5).add_quad(p, col, mat)

## Caixa longa alinhada aos eixos, quebrada em pedaços que cabem nos blocos.
func long_box(center: Vector3, size: Vector3, col: Color, mat: String = "") -> void:
	var along_x := size.x >= size.z
	var length := size.x if along_x else size.z
	var pieces := int(ceil(length / CHUNK))
	var step := length / pieces
	for i in pieces:
		var off := -length * 0.5 + step * (i + 0.5)
		var c := center + (Vector3(off, 0, 0) if along_x else Vector3(0, 0, off))
		var s := Vector3(step, size.y, size.z) if along_x else Vector3(size.x, size.y, step)
		box(c, s, col, 0.0, mat)

func build(parent: Node3D, name: String, roughness: float = 0.85, shadows: bool = true, mat: Material = null) -> void:
	var root := Node3D.new()
	root.name = name
	parent.add_child(root)
	for k in chunks:
		var mi: MeshInstance3D = chunks[k].build(roughness, shadows)
		if mat:
			mi.material_override = mat
		mi.name = "%s_%d_%d" % [name, k.x, k.y]
		root.add_child(mi)
