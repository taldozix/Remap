class_name Building
extends Node3D
## Prédio com interior acessível. Paredes de cada lado têm duas versões (inteira e
## baixa) para o corte estilo Project Zomboid quando o jogador está dentro ou atrás.

var building_id: String = ""
var def: Dictionary = {}
var rect := Rect2()            # área no plano XZ (x, z)
var height := 3.0
var side_full: Dictionary = {}  # "n"/"s"/"e"/"w" -> MeshInstance3D
var side_low: Dictionary = {}
var roof: Node3D
var lamp: OmniLight3D
var cut := false
var _cut_sides: Array = []

const NORMALS := {"n": Vector3(0, 0, -1), "s": Vector3(0, 0, 1), "e": Vector3(1, 0, 0), "w": Vector3(-1, 0, 0)}

func contains(p: Vector3, margin: float = 0.0) -> bool:
	return p.x >= rect.position.x - margin and p.x <= rect.end.x + margin and p.z >= rect.position.y - margin and p.z <= rect.end.y + margin

## Corta as paredes voltadas para a câmera (direção `view_dir` = da câmera para a cena).
func set_cut(on: bool, view_dir: Vector3) -> void:
	var sides: Array = []
	if on:
		for s in NORMALS:
			if NORMALS[s].dot(-view_dir) > 0.15:
				sides.append(s)
	if on == cut and sides == _cut_sides:
		return
	cut = on
	_cut_sides = sides
	for s in side_full:
		var c: bool = sides.has(s)
		side_full[s].visible = not c
		if side_low.has(s):
			side_low[s].visible = c
	if roof:
		roof.visible = not on

func set_night(amount: float) -> void:
	if lamp:
		lamp.visible = amount > 0.3
