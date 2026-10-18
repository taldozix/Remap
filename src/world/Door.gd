class_name Door
extends Interactable
## Porta com dobradiça, trancas, chaves e horário de funcionamento (lojas).
## NPCs com acesso abrem sozinhos ao se aproximar; o estado é salvo em WorldState.

var door_id: String = ""
var def: Dictionary = {}
var width := 1.1
var height := 2.15
var pivot: Node3D
var body: StaticBody3D
var is_open := false
var _target_angle := 0.0
var _npc_hold := 0.0
var _player_opened := false
var _check_t := 0.0
var swing_sign := 1.0
var rollup := false

func setup(p_def: Dictionary, p_world: Node, p_building: String, wall_thickness: float, color: Color) -> void:
	def = p_def
	door_id = p_def.id
	world = p_world
	building_id = p_building
	width = float(p_def.get("width", 1.1))
	rollup = bool(p_def.get("rollup", false))
	title = tr(p_def.get("label", "Porta"))
	height = 3.4 if rollup else 2.15
	pivot = Node3D.new()
	add_child(pivot)
	pivot.position = Vector3(-width * 0.5, 0, 0)
	body = StaticBody3D.new()
	body.collision_layer = 2
	body.collision_mask = 0
	pivot.add_child(body)
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(width, height, wall_thickness * 0.6)
	shape.shape = box
	shape.position = Vector3(width * 0.5, height * 0.5, 0)
	body.add_child(shape)
	var mesh := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = box.size
	mesh.mesh = bm
	mesh.position = shape.position
	if p_def.get("glass", false):
		mesh.material_override = Mat.glass(Color(0.6, 0.78, 0.9, 0.5))
		var frame := MeshInstance3D.new()
		var fm := BoxMesh.new()
		fm.size = Vector3(width, 0.12, wall_thickness * 0.7)
		frame.mesh = fm
		frame.material_override = Mat.color(Color(0.25, 0.25, 0.28), 0.4, 0.6)
		frame.position = Vector3(width * 0.5, 1.0, 0)
		body.add_child(frame)
	elif rollup:
		mesh.material_override = Mat.color(Color(0.62, 0.64, 0.66), 0.5, 0.5)
	else:
		mesh.material_override = Mat.color(color, 0.6)
		var knob := MeshInstance3D.new()
		var km := SphereMesh.new()
		km.radius = 0.045
		km.height = 0.09
		knob.mesh = km
		knob.material_override = Mat.color(Color(0.85, 0.75, 0.4), 0.3, 0.9)
		knob.position = Vector3(width - 0.12, 1.0, wall_thickness * 0.35)
		body.add_child(knob)
	body.add_child(mesh)
	radius = 2.0

func _ready() -> void:
	super._ready()
	var st: Dictionary = Game.world_state.doors.get(door_id, {})
	set_open(bool(st.get("open", false)), true)

func locked() -> bool:
	return Game.world_state.is_door_locked(door_id)

## Lojas: destrancadas no horário de funcionamento.
func _within_hours() -> bool:
	var h: Array = def.get("hours", [])
	return h.size() == 2 and Clock.hour_in(float(h[0]), float(h[1]))

func _process(delta: float) -> void:
	var cur := pivot.rotation.y if not rollup else pivot.position.y
	var goal := _target_angle
	if rollup:
		pivot.position.y = move_toward(pivot.position.y, goal, delta * 3.0)
	else:
		pivot.rotation.y = lerp_angle(cur, goal, min(1.0, delta * 9.0))
	_check_t -= delta
	if _check_t > 0.0:
		return
	_check_t = 0.25
	# Horário: lojas abrem e fecham as portas sozinhas (trancam ao fechar).
	if def.has("hours"):
		var should_unlock := _within_hours()
		if should_unlock == locked():
			Game.world_state.set_door(door_id, is_open, not should_unlock)
			if rollup:
				set_open(should_unlock)
	if rollup:
		return
	# NPCs com acesso abrem a porta ao chegar perto.
	var npc_near := false
	if world and world.has_method("npcs_near"):
		for n in world.npcs_near(global_position, 1.8):
			if n.has_door_access(self):
				npc_near = true
				break
	if npc_near:
		_npc_hold = 1.6
		if not is_open:
			set_open(true)
	elif _npc_hold > 0.0:
		_npc_hold -= 0.25
		if _npc_hold <= 0.0 and is_open and not _player_opened:
			set_open(false)

func set_open(open: bool, instant: bool = false) -> void:
	if open != is_open and not instant:
		Audio.play_at("door_open" if open else "door_close", global_position, -4.0)
	is_open = open
	if rollup:
		_target_angle = 3.2 if open else 0.0
		if instant:
			pivot.position.y = _target_angle
	else:
		_target_angle = deg_to_rad(95.0) * swing_sign if open else 0.0
		if instant:
			pivot.rotation.y = _target_angle
	body.collision_layer = 0 if (open and not rollup) or (rollup and open) else 2
	var st: Dictionary = Game.world_state.doors.get(door_id, {"locked": false})
	Game.world_state.set_door(door_id, open, bool(st.get("locked", false)))

## Quem tem a chave: dono(s) listados, funcionário (player_job) ou item-chave no inventário.
func player_has_key() -> bool:
	var keys: Array = def.get("keys", [])
	if keys.has("player"):
		return true
	if keys.has("owner") and Game.property.owns_or_rents(building_id):
		return true
	if keys.has("player_job") and Game.job.employed:
		return true
	for s in Game.player.inventory.items:
		if Data.item(s.id).get("opens", []).has(door_id):
			return true
	return false

## Chave "legítima" (dono/inquilino/funcionário), em oposição a uma chave achada.
func player_has_key_by_right() -> bool:
	var keys: Array = def.get("keys", [])
	return keys.has("player") or (keys.has("owner") and Game.property.owns_or_rents(building_id)) or (keys.has("player_job") and Game.job.employed)

func npc_has_key(npc_id: String) -> bool:
	var keys: Array = def.get("keys", [])
	return keys.has(npc_id) or (keys.has("pedestre") and npc_id.begins_with("pedestre"))

func get_actions(player: Node) -> Array:
	var out: Array = []
	var inside: bool = world.is_inside_building(player.global_position, building_id) if world else false
	if rollup:
		if player_has_key():
			out.append(action("toggle", tr("Fechar portão") if is_open else tr("Abrir portão")))
		return out
	if is_open:
		out.append(action("close", tr("Fechar")))
	else:
		if locked() and not inside:
			out.append(action("knock", tr("Bater na porta")))
			if player_has_key():
				out.append(action("unlock", tr("Destrancar e abrir")))
			else:
				out.append(action("locked", tr("Trancada"), false, tr("Você não tem a chave")))
		else:
			out.append(action("open", tr("Abrir")))
	if player_has_key() and not def.has("hours"):
		if locked():
			if is_open or inside:
				out.append(action("unlock_only", tr("Destrancar")))
		else:
			out.append(action("lock", tr("Trancar")))
	return out

func do_action(player: Node, id: String) -> void:
	match id:
		"open", "toggle":
			if rollup:
				set_open(not is_open)
				return
			if locked() and not world.is_inside_building(player.global_position, building_id):
				Audio.play_at("door_locked", global_position)
				return
			_player_opened = true
			_set_swing_from(player.global_position)
			set_open(true)
		"close":
			_player_opened = false
			set_open(false)
		"unlock":
			Game.world_state.set_door(door_id, is_open, false)
			_player_opened = true
			_set_swing_from(player.global_position)
			set_open(true)
			if not player_has_key_by_right() and building_id != "":
				world.on_player_used_key(building_id)
		"unlock_only":
			Game.world_state.set_door(door_id, is_open, false)
			Events.notify.emit(tr("Porta destrancada."), "info")
		"lock":
			if is_open:
				set_open(false)
			Game.world_state.set_door(door_id, false, true)
			Events.notify.emit(tr("Porta trancada."), "info")
		"knock":
			Audio.play_at("door_locked", global_position, 2.0, 0.6)
			world.on_knock(building_id)

## Abre para o lado oposto de quem empurra (evita bater no jogador).
func _set_swing_from(pos: Vector3) -> void:
	var local := to_local(pos)
	swing_sign = -1.0 if local.z > 0.0 else 1.0
