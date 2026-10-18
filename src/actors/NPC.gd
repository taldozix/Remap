class_name NPC
extends CharacterBody3D
## NPC com rotina diária (data/npcs.json): dorme, trabalha, compra, conversa, corre,
## pega ônibus, reage ao jogador, ao trânsito e à chuva, e testemunha crimes.
## Simulação em níveis: perto = física + animação; longe = deslocamento simplificado;
## dentro de prédios fechados = oculto (apenas a agenda avança).

var npc_id: String = ""
var def: Dictionary = {}
var world: Node = null
var visual: Humanoid
var display_name := ""
var is_ambient := false
var radius := 1.8

var state := "idle"        # moving, at_spot, talking, knocked, hidden
var entry: Dictionary = {}
var spot_id := ""
var spot: Dictionary = {}
var activity := ""
var walk_speed := 1.3
var lod_far := false

var path := PackedVector3Array()
var path_i := 0
var _decide_t := 0.0
var _snap_pending := true
var _activity_t := 0.0
var _sub := ""
var _stuck_t := 0.0
var _stuck_pos := Vector3.ZERO
var _jog_i := 0
var _knock_t := 0.0
var _hit_by_player := false
var _bark_cd := 0.0
var _reaction_cd := 0.0
var _chat_t := 0.0
var _bumps := 0
var _wait_player_t := 0.0
var talking := false
var bark_label: Label3D
var _bark_t := 0.0
var _shape: CollisionShape3D

func setup(p_id: String, p_def: Dictionary, p_world: Node) -> void:
	npc_id = p_id
	def = p_def
	world = p_world
	display_name = tr(p_def.get("name", p_id))
	walk_speed = float(p_def.get("walk_speed", 1.3))
	is_ambient = p_id.begins_with("pedestre")

func _ready() -> void:
	collision_layer = 8
	collision_mask = 1
	_shape = CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.28
	cap.height = 1.7
	_shape.shape = cap
	_shape.position = Vector3(0, 0.85, 0)
	add_child(_shape)
	visual = Humanoid.new()
	add_child(visual)
	visual.build(def.get("appearance", {}))
	bark_label = Label3D.new()
	bark_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	bark_label.no_depth_test = true
	bark_label.font_size = 34
	bark_label.pixel_size = 0.0055
	bark_label.outline_size = 12
	bark_label.position = Vector3(0, 2.25, 0)
	bark_label.width = 420
	bark_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bark_label.visible = false
	add_child(bark_label)
	add_to_group("interactable")
	add_to_group("npc")
	Clock.time_advanced.connect(_on_time_advanced)
	_decide_t = randf() * 0.5

func _on_time_advanced(_m: float, skipped: bool) -> void:
	if skipped:
		_snap_pending = true

# ------------------------------------------------------------------ agenda
func schedule_entry() -> Dictionary:
	var h := Clock.hour_f()
	for e in def.get("schedule", []):
		if Clock.is_hour_in(h, float(e.from), float(e.to)):
			return e
	return entry

func home_spot() -> String:
	for e in def.get("schedule", []):
		if str(e.activity).begins_with("sleep"):
			return str(e.place)
	return ""

func _effective_entry() -> Dictionary:
	var e := schedule_entry()
	if Game.world_state.weather.get("state", "") == "chuva" and npc_id != "tiao":
		var outdoor_acts := ["sit", "stand", "chat", "jog", "wander"]
		var sp: Dictionary = world.spots.get(str(e.get("place", "")), {})
		if e.get("activity", "") in outdoor_acts and sp.get("building", "x") == "" and home_spot() != "":
			return {"place": home_spot(), "activity": "shelter", "from": e.get("from", 0), "to": e.get("to", 0)}
	return e

func _decide() -> void:
	if state in ["talking", "knocked"]:
		return
	var e := _effective_entry()
	if e.is_empty():
		return
	var changed: bool = e.get("place", "") != entry.get("place", "") or e.get("activity", "") != entry.get("activity", "")
	if _snap_pending:
		_snap_pending = false
		entry = e
		activity = str(e.activity)
		_snap_to(_place_for(e))
		return
	if changed:
		if entry.get("activity", "") == "shelter" and Game.world_state.weather.get("state", "") != "chuva":
			pass
		elif e.get("activity", "") == "shelter":
			bark(_pick("rain"))
		entry = e
		activity = str(e.activity)
		_sub = ""
		_activity_t = 0.0
		go_to(_place_for(e))

func _place_for(e: Dictionary) -> String:
	if e.get("activity", "") == "jog":
		return "__jog"
	if e.get("activity", "") == "wander":
		var ws: Array = Data.map().get("wander_spots", [])
		return str(ws[randi() % ws.size()]) if not ws.is_empty() else ""
	return str(e.get("place", ""))

func _spot_data(id: String) -> Dictionary:
	if id == "__jog":
		var r: Array = Data.map().get("jog_route", [[0, 0]])
		var p: Array = r[_jog_i % r.size()]
		return {"pos": Vector3(float(p[0]), 0, float(p[1])), "rot": 0.0, "pose": "stand", "hidden": false, "building": ""}
	return world.spots.get(id, {})

func _snap_to(id: String) -> void:
	spot_id = id
	spot = _spot_data(id)
	if spot.is_empty():
		return
	if state == "hidden":
		_set_hidden(false)
	global_position = spot.pos
	path = PackedVector3Array()
	_arrive()

func go_to(id: String) -> void:
	spot_id = id
	spot = _spot_data(id)
	if spot.is_empty():
		return
	if state == "hidden":
		_set_hidden(false)
	visual.position = Vector3.ZERO
	_shape.disabled = false
	path = world.find_path(global_position, spot.pos)
	path_i = 0
	state = "moving"
	_stuck_t = 0.0
	_stuck_pos = global_position

func _arrive() -> void:
	state = "at_spot"
	velocity = Vector3.ZERO
	rotation.y = deg_to_rad(float(spot.get("rot", 0)))
	visual.position = Vector3.ZERO
	_shape.disabled = false
	if spot_id == "__jog":
		_jog_i += 1
		go_to("__jog")
		return
	if spot.get("hidden", false) and activity in ["sleep", "work", "leave", "shelter", "stand", "wander"]:
		if activity == "leave":
			_sub = "waiting_bus"
			_activity_t = 0.0
		else:
			_set_hidden(true)
			return
	_apply_pose()

func _apply_pose() -> void:
	var pose: String = spot.get("pose", "stand")
	match activity:
		"sleep", "sleep_bench":
			if pose == "lie" or activity == "sleep_bench":
				_lie_at_spot()
				return
			if spot.get("hidden", false):
				_set_hidden(true)
				return
			visual.set_pose("idle")
		"sit", "shelter":
			if pose == "sit":
				_sit_at_spot()
			else:
				visual.set_pose("idle")
		"work":
			visual.set_pose("work" if pose == "work" else "idle")
		"search":
			visual.set_pose("search")
		"eat":
			visual.set_pose("eat")
		_:
			if pose == "sit":
				_sit_at_spot()
			elif pose == "lie":
				_lie_at_spot()
			else:
				visual.set_pose("idle")

func _sit_at_spot() -> void:
	_shape.disabled = true
	visual.set_pose("sit")
	var fn: Furniture = spot.get("furniture")
	visual.position = Vector3(0, (fn.seat_height if fn else 0.45) - 0.47, 0)

func _lie_at_spot() -> void:
	_shape.disabled = true
	visual.set_pose("lie")
	var fn: Furniture = spot.get("furniture")
	if fn and fn.kind == "bed":
		global_position = fn.global_position + Basis(Vector3.UP, deg_to_rad(fn.facing)) * Vector3(0, 0, 0.85)
		rotation.y = deg_to_rad(fn.facing)
		visual.position = Vector3(0, 0.62, 0)
	else:
		# banco da praça: deita ao longo do assento
		rotation.y = deg_to_rad(float(spot.get("rot", 0)) + 90.0)
		global_position = spot.pos + Basis(Vector3.UP, rotation.y) * Vector3(0, 0, 0.85)
		visual.position = Vector3(0, 0.6, 0)

func _set_hidden(h: bool) -> void:
	visible = not h
	_shape.disabled = h
	state = "hidden" if h else state
	if not h and state == "hidden":
		state = "idle"

func is_sleeping() -> bool:
	return state == "hidden" or (state == "at_spot" and activity.begins_with("sleep"))

# ------------------------------------------------------------------ processamento
func _physics_process(delta: float) -> void:
	_bark_cd -= delta
	_reaction_cd -= delta
	if bark_label.visible:
		_bark_t -= delta
		bark_label.modulate.a = clampf(_bark_t, 0.0, 1.0)
		if _bark_t <= 0.0:
			bark_label.visible = false
	_decide_t -= delta
	if _decide_t <= 0.0:
		_decide_t = 0.5
		_decide()
		_update_lod()
		if state == "at_spot":
			_process_activity(0.5)
		if state in ["moving", "at_spot"] and not lod_far:
			_react_to_world()
	match state:
		"moving":
			_move(delta)
		"knocked":
			_knock_t -= delta
			if _knock_t <= 0.0:
				_get_up()
		"talking":
			var p: Node = world.player
			if p:
				var to: Vector3 = p.global_position - global_position
				rotation.y = lerp_angle(rotation.y, atan2(-to.x, -to.z), min(1.0, delta * 6.0))

func _update_lod() -> void:
	var p: Node = world.player
	if p == null:
		return
	var far_dist: float = world.npc_lod_distance()
	var far: bool = global_position.distance_to(p.global_position) > far_dist
	if far != lod_far:
		lod_far = far
		visual.lod_far = far

func _move(delta: float) -> void:
	if path.is_empty() or path_i >= path.size():
		_arrive()
		return
	var target := path[path_i]
	var to := target - global_position
	to.y = 0
	if to.length() < 0.35:
		path_i += 1
		if path_i >= path.size():
			_arrive()
		return
	var spd := walk_speed * (2.0 if spot_id == "__jog" else 1.0)
	if Game.world_state.weather.get("state", "") == "chuva":
		spd *= 1.25
	if Game.world_state.npcs.get(npc_id, {}).get("injured_until", 0.0) > Clock.total_minutes:
		spd *= 0.6
	var dir := to.normalized()
	# Espera o jogador sair da frente.
	var pl: Node = world.player
	if pl and pl.state != "driving":
		var tp: Vector3 = pl.global_position - global_position
		tp.y = 0
		if tp.length() < 0.9 and tp.normalized().dot(dir) > 0.6:
			_wait_player_t += delta
			velocity = Vector3.ZERO
			visual.set_pose("idle")
			if _wait_player_t > 1.2 and _bark_cd <= 0.0:
				bark(tr("Com licença..."))
			if _wait_player_t < 2.5:
				return
	_wait_player_t = 0.0
	if lod_far:
		# Simulação simplificada fora da vista: deslocamento direto e mais rápido,
		# para que a agenda continue pontual sem custo de física.
		global_position += dir * spd * 3.0 * delta
	else:
		velocity = dir * spd
		move_and_slide()
		visual.position.y = lerpf(visual.position.y, world.ground_height(global_position.x, global_position.z), min(1.0, delta * 10.0))
	rotation.y = lerp_angle(rotation.y, atan2(-dir.x, -dir.z), min(1.0, delta * 8.0))
	visual.set_pose("run" if spot_id == "__jog" else "walk")
	visual.speed = spd
	_stuck_t += delta
	if _stuck_t > 3.0:
		if global_position.distance_to(_stuck_pos) < 0.5:
			global_position = Vector3(target.x, global_position.y, target.z)
			path_i += 1
		_stuck_t = 0.0
		_stuck_pos = global_position

func _process_activity(dt: float) -> void:
	_activity_t += dt
	match activity:
		"shop":
			var shop_id: String = entry.get("shop", "")
			if _sub == "" and _activity_t > randf_range(4.0, 9.0):
				var queue := "mercadinho_fila" if world.spots.has("mercadinho_fila") and shop_id == "mercadinho" else spot_id
				if world.shop_unavailable_reason(shop_id, null) == "":
					_sub = "queue"
					go_to(queue)
				else:
					_sub = "done"
			elif _sub == "queue" and _activity_t > 3.0:
				var bought: String = Game.shops.npc_purchase(shop_id, Game.rng)
				if bought != "":
					bark(tr("Vou levar %s.") % Data.item_name(bought).to_lower())
					Audio.play_at("cash", global_position, -10.0)
				_sub = "done"
				go_to(str(entry.get("place", spot_id)))
		"leave":
			if _sub == "waiting_bus" and _activity_t > 20.0:
				_set_hidden(true)
		"chat":
			_chat_t -= dt
			if _chat_t <= 0.0:
				_chat_t = randf_range(9.0, 15.0)
				var partner: NPC = world.chat_partner(self)
				if partner and npc_id < partner.npc_id:
					var lines: Array = world.conversation_lines()
					bark(lines[0])
					partner.call_deferred("bark_later", lines[1], 2.8)
				visual.set_pose("talk" if partner else "idle")
		"wander":
			if _activity_t > randf_range(25.0, 60.0):
				_activity_t = 0.0
				go_to(_place_for(entry))

func bark_later(text: String, delay: float) -> void:
	await get_tree().create_timer(delay).timeout
	if is_instance_valid(self):
		bark(text)

## Reações: carro do jogador em alta velocidade, cheiro, chuva.
func _react_to_world() -> void:
	var p: Node = world.player
	if p == null or state == "hidden":
		return
	var d: float = global_position.distance_to(p.global_position)
	if p.state == "driving" and p.vehicle and d < 6.0 and absf(p.vehicle.speed) > 6.0 and _reaction_cd <= 0.0:
		_reaction_cd = 6.0
		bark(_pick("car_near"))
		if state == "moving" or not is_sleeping():
			var away: Vector3 = (global_position - p.global_position)
			away.y = 0
			var side := Vector3(-p.vehicle.global_transform.basis.z.z, 0, p.vehicle.global_transform.basis.z.x)
			if side.dot(away) < 0:
				side = -side
			global_position += side.normalized() * 1.2
	elif p.state != "driving" and d < 2.2 and Game.player.needs.get_need("higiene") >= 70.0 and _reaction_cd <= 0.0 and not is_sleeping():
		_reaction_cd = 45.0
		bark(_pick("smell"))
		if Game.relations.greeted_day.get("smell_" + npc_id, -1) != Clock.day():
			Game.relations.greeted_day["smell_" + npc_id] = Clock.day()
			Game.relations.change(npc_id, -1)

# ------------------------------------------------------------------ interação
func interact_point() -> Vector3:
	return global_position

func interact_title() -> String:
	var role: String = def.get("role", "")
	return "%s — %s" % [display_name, tr(role)] if role != "" else display_name

func get_actions(_player: Node) -> Array:
	if state == "hidden":
		return []
	if state == "knocked":
		return [Interactable.action("help", tr("Ajudar %s a levantar") % display_name)]
	if is_sleeping():
		return [Interactable.action("none", tr("%s está dormindo") % display_name, false)]
	return [
		Interactable.action("talk", tr("Conversar com %s") % display_name),
		Interactable.action("greet", tr("Cumprimentar")),
	]

func do_action(_player: Node, id: String) -> void:
	match id:
		"talk":
			world.start_dialogue(self)
		"greet":
			var greeted: int = Game.relations.greeted_day.get(npc_id, -1)
			var lines := "greet_night" if Clock.is_night() else "greet"
			if Game.relations.get_rel(npc_id) <= -30:
				bark(tr("Hum."))
			else:
				bark(_pick(lines))
			if greeted != Clock.day():
				Game.relations.greeted_day[npc_id] = Clock.day()
				Game.relations.change(npc_id, 1)
				Game.player.skills.add_xp("social", 3)
		"help":
			help_up()

func begin_talk() -> void:
	state = "talking"
	talking = true
	visual.set_pose("talk")
	velocity = Vector3.ZERO

func end_talk() -> void:
	talking = false
	state = "idle"
	entry = {}
	_decide()

func on_bumped(_by: Node) -> void:
	if _bark_cd > 0.0 or is_sleeping():
		return
	_bumps += 1
	bark(_pick("bump"))
	if _bumps >= 3:
		_bumps = 0
		Game.relations.change(npc_id, -2)

func has_door_access(door: Node) -> bool:
	if state != "moving":
		return false
	return not door.locked() or door.npc_has_key(npc_id)

func can_witness() -> bool:
	return visible and state not in ["knocked", "hidden"] and not is_sleeping()

# ------------------------------------------------------------------ atropelamento
func knock_down(from_dir: Vector3, severity: float, by_player: bool) -> void:
	if state in ["knocked", "hidden"]:
		return
	state = "knocked"
	_hit_by_player = by_player
	_knock_t = 9.0 + severity * 3.0
	_shape.disabled = true
	visual.set_pose("knocked")
	visual.position = Vector3(0, 0.12, 0)
	var d := from_dir
	d.y = 0
	if d.length() > 0.01:
		global_position += d.normalized() * min(3.0, 0.8 + severity * 0.5)
	bark(_pick("knocked"))
	var st: Dictionary = Game.world_state.npcs.get(npc_id, {})
	st["injured_until"] = Clock.total_minutes + 360.0
	Game.world_state.npcs[npc_id] = st
	if by_player:
		world.on_npc_hit(self, severity)

func help_up() -> void:
	if state != "knocked":
		return
	bark(_pick("help_thanks"))
	Game.relations.change(npc_id, 5)
	world.on_npc_helped(self)
	_get_up()

func _get_up() -> void:
	visual.position = Vector3.ZERO
	visual.set_pose("idle")
	_shape.disabled = false
	state = "idle"
	entry = {}
	if _hit_by_player:
		Game.relations.add_memory(npc_id, "atropelado", Clock.day())
	_hit_by_player = false

# ------------------------------------------------------------------ falas
func bark(text: String) -> void:
	if text == "" or not visible:
		return
	bark_label.text = text
	bark_label.visible = true
	bark_label.modulate.a = 1.0
	_bark_t = 4.0
	_bark_cd = 3.0
	Events.bark.emit(npc_id, text)

func _pick(key: String) -> String:
	var list: Array = Data.chatter().get(key, [])
	return tr(str(list[randi() % list.size()])) if not list.is_empty() else ""
