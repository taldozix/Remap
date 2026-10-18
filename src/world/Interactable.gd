class_name Interactable
extends Node3D
## Base de objetos com que o jogador interage. Qualquer nó no grupo "interactable"
## que implemente get_actions/do_action funciona (NPCs e veículos também).
## Ação: {"id": String, "label": String, "enabled": bool, "hint": String}

var title: String = ""
var world: Node = null       # GameWorld
var building_id: String = ""
var radius: float = 1.7

func _ready() -> void:
	add_to_group("interactable")

func interact_point() -> Vector3:
	return global_position

func interact_title() -> String:
	return title

func get_actions(_player: Node) -> Array:
	return []

func do_action(_player: Node, _action_id: String) -> void:
	pass

static func action(id: String, label: String, enabled: bool = true, hint: String = "") -> Dictionary:
	return {"id": id, "label": label, "enabled": enabled, "hint": hint}
