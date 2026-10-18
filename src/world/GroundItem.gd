class_name GroundItem
extends Interactable
## Item largado no chão (persistente em WorldState.ground_items).

var entry: Dictionary = {}

func setup(e: Dictionary, p_world: Node) -> void:
	entry = e
	world = p_world
	title = "%s x%d" % [Data.item_name(e.id), int(e.qty)]
	radius = 1.4
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(0.3, 0.18, 0.25)
	mi.mesh = bm
	mi.position.y = 0.09
	var cat: String = Data.item(e.id).get("category", "")
	var col := Color("c9a76a")
	match cat:
		"comida": col = Color("e0b050")
		"bebida": col = Color("5fa8d3")
		"valor": col = Color("d4af37")
		"missao": col = Color("e05050")
	# sacola/pacote de papel pardo com faixa colorida da categoria
	mi.material_override = Mat.textured("cloth_canvas", Color("c8a878"), 0.6, 0.9)
	add_child(mi)
	var band := MeshInstance3D.new()
	var bb := BoxMesh.new()
	bb.size = Vector3(0.31, 0.05, 0.26)
	band.mesh = bb
	band.position.y = 0.12
	band.material_override = Mat.textured("metal_painted", col, 1.0, 0.6)
	add_child(band)

## Itens no chão são pequenos: ganham do móvel ou da pessoa ao lado no foco.
func focus_priority() -> float:
	return 0.6

func get_actions(_player: Node) -> Array:
	return [action("take", tr("Pegar %s") % title)]

func do_action(_player: Node, _id: String) -> void:
	world.pick_ground_item(self)
