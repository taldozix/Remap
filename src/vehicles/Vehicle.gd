class_name Vehicle
extends CharacterBody3D
## Veículo dirigível (e estacionado). Física arcade em modelo bicicleta: aceleração,
## frenagem, ré, inércia, derrapagem com freio de mão, colisões com dano, consumo de
## combustível, buzina, faróis e atropelamento. Estado salvo em WorldState.vehicles.

var vehicle_id := ""
var data: Dictionary = {}     # data/vehicles.json
var world: Node = null
var visual: CarVisual
var driver: Node = null
var speed := 0.0
var lat_speed := 0.0
var steer := 0.0
var fuel := 0.0
var damage := 0.0
var radius := 2.8
var _engine: AudioStreamPlayer3D
var _dist_acc := 0.0
var _hit_cd := 0.0
var _headlights: Array[SpotLight3D] = []
var _hit_area: Area3D

func setup(id: String, state: Dictionary, p_world: Node) -> void:
	vehicle_id = id
	world = p_world
	data = Data.vehicle(state.type)
	fuel = float(state.get("fuel", 20))
	damage = float(state.get("damage", 0))

func _ready() -> void:
	collision_layer = 16
	collision_mask = 1 | 16
	motion_mode = CharacterBody3D.MOTION_MODE_FLOATING
	var st: Dictionary = Game.world_state.vehicles[vehicle_id]
	var size: Array = data.get("size", [1.8, 1.4, 4.2])
	var cs := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(float(size[0]), float(size[1]) * 0.8, float(size[2]))
	cs.shape = box
	cs.position = Vector3(0, 0.3 + float(size[1]) * 0.4, 0)
	add_child(cs)
	visual = CarVisual.new()
	add_child(visual)
	visual.build(data.get("kind", "sedan"), size, Color(st.get("color", "#888888")))
	_hit_area = Area3D.new()
	_hit_area.collision_layer = 0
	_hit_area.collision_mask = 8
	var hs := CollisionShape3D.new()
	var hb := BoxShape3D.new()
	hb.size = Vector3(float(size[0]) + 0.3, 1.2, float(size[2]) + 0.6)
	hs.shape = hb
	hs.position = Vector3(0, 0.8, 0)
	_hit_area.add_child(hs)
	add_child(_hit_area)
	_engine = AudioStreamPlayer3D.new()
	_engine.stream = Audio.stream("engine")
	_engine.bus = "SFX"
	_engine.unit_size = 5.0
	_engine.volume_db = -10.0
	add_child(_engine)
	for s in [-1.0, 1.0]:
		var l := SpotLight3D.new()
		l.position = Vector3(s * float(size[0]) * 0.32, 0.6, -float(size[2]) * 0.5)
		l.spot_range = 22.0
		l.spot_angle = 32.0
		l.light_energy = 3.0
		l.light_color = Color(1, 0.95, 0.85)
		l.rotation_degrees.x = -8.0
		l.visible = false
		add_child(l)
		_headlights.append(l)
	add_to_group("interactable")
	add_to_group("vehicle")
	_update_damage_fx()

func display_name() -> String:
	return tr(data.get("name", "Veículo"))

func fuel_capacity() -> float:
	return float(data.get("fuel_capacity", 40))

func owner_id() -> String:
	return str(Game.world_state.vehicles[vehicle_id].get("owner", ""))

func is_players() -> bool:
	return owner_id() == "player" or Game.has_flag("roubado_" + vehicle_id)

func interact_point() -> Vector3:
	return global_transform * Vector3(-float(data.size[0]) * 0.5 - 0.4, 0, -0.3)

func interact_title() -> String:
	var who := tr("seu") if owner_id() == "player" else tr(Data.npc(owner_id()).get("name", "?"))
	return "%s (%s)" % [display_name(), who]

func get_actions(_player: Node) -> Array:
	var out: Array = []
	if is_players():
		out.append(Interactable.action("enter", tr("Entrar e dirigir [%s]") % Settings.primary_key_text("vehicle"), damage < 100.0, tr("Motor destruído — precisa de guincho/oficina")))
		out.append(Interactable.action("trunk", tr("Abrir porta-malas")))
	else:
		out.append(Interactable.action("locked", tr("Trancado (pertence a %s)") % tr(Data.npc(owner_id()).get("name", "?")), false))
		out.append(Interactable.action("breakin", tr("Arrombar a porta (crime)")))
	return out

func get_driver_actions(_player: Node) -> Array:
	return [Interactable.action("exit", tr("Sair do veículo [%s]") % Settings.primary_key_text("vehicle"))]

func do_action(player: Node, id: String) -> void:
	match id:
		"enter":
			if damage >= 100.0:
				return
			visual.set_door(true)
			Audio.play_at("door_close", global_position, -6.0, 1.3)
			player.enter_vehicle(self)
			await get_tree().create_timer(0.35).timeout
			if is_instance_valid(visual):
				visual.set_door(false)
		"trunk":
			Events.request_container.emit("trunk_" + vehicle_id, tr("Porta-malas — %s") % display_name())
		"breakin":
			player.start_timed(tr("Forçando a fechadura..."), 4.0 * Game.player.needs.work_multiplier(), _break_in)
		"exit":
			player.exit_vehicle()

func _break_in() -> void:
	Audio.play_at("door_locked", global_position, 4.0)
	Game.set_flag("roubado_" + vehicle_id)
	Game.report_crime("furto_veiculo", owner_id(), false, global_position)
	Events.notify.emit(tr("Porta aberta. Este carro agora está com você — mas não é seu."), "bad")

func set_driver(d: Node) -> void:
	driver = d
	if d:
		_engine.play()
	else:
		_engine.stop()
		speed = 0.0
		lat_speed = 0.0
		save_state()

func exit_position() -> Vector3:
	var size: Array = data.size
	var space := get_world_3d().direct_space_state
	for off in [Vector3(-float(size[0]) * 0.5 - 0.7, 0, -0.3), Vector3(float(size[0]) * 0.5 + 0.7, 0, -0.3), Vector3(0, 0, float(size[2]) * 0.5 + 1.0), Vector3(0, 0, -float(size[2]) * 0.5 - 1.0)]:
		var p: Vector3 = global_transform * off
		p.y = 0.05
		var q := PhysicsShapeQueryParameters3D.new()
		var sph := SphereShape3D.new()
		sph.radius = 0.35
		q.shape = sph
		q.transform = Transform3D(Basis(), p + Vector3(0, 0.9, 0))
		q.collision_mask = 1 | 2 | 16
		q.exclude = [get_rid()]
		if space.intersect_shape(q, 1).is_empty():
			return p
	return global_transform * Vector3(-float(size[0]) * 0.5 - 0.7, 0, 0)

func _physics_process(delta: float) -> void:
	_hit_cd -= delta
	var throttle := 0.0
	var brake := 0.0
	var steer_in := 0.0
	var handbrake := false
	var driving: bool = driver != null and world and not world.ui_blocking()
	if driving:
		throttle = Input.get_action_strength("move_up")
		brake = Input.get_action_strength("move_down")
		steer_in = Input.get_action_strength("move_left") - Input.get_action_strength("move_right")
		handbrake = Input.is_action_pressed("handbrake")
		if Input.is_action_just_pressed("horn"):
			Audio.play_at("horn", global_position, 2.0)
			world.on_horn(global_position)
	elif absf(speed) < 0.05 and absf(lat_speed) < 0.05:
		return
	var engine_ok := fuel > 0.0 and damage < 100.0 and driver != null
	var max_speed := float(data.max_speed) * (1.0 - clampf((damage - 50.0) / 100.0, 0.0, 0.5))
	var accel := float(data.accel)
	if not engine_ok:
		throttle = 0.0
	# Longitudinal
	if throttle > 0.0 and speed >= -0.5:
		speed += accel * throttle * delta * (1.0 - clampf(speed / max_speed, 0.0, 1.0) * 0.85)
	if brake > 0.0:
		if speed > 0.5:
			speed -= float(data.brake) * brake * delta
		elif engine_ok:
			speed = max(-float(data.reverse_speed), speed - accel * 0.6 * brake * delta)
	if throttle == 0.0 and brake == 0.0 or not engine_ok:
		speed = move_toward(speed, 0.0, (0.8 + 0.015 * speed * speed) * delta * (3.0 if driver == null else 1.0))
	if handbrake:
		speed = move_toward(speed, 0.0, 8.0 * delta)
	speed = clampf(speed, -float(data.reverse_speed), max_speed)
	# Direção (habilidade melhora a resposta)
	var skill := Game.player.skills.level("direcao")
	var max_steer := float(data.steer) * (1.0 - 0.55 * clampf(absf(speed) / max_speed, 0.0, 1.0))
	steer = move_toward(steer, steer_in * max_steer, (2.2 + skill * 0.15) * delta)
	var yaw_rate := speed / float(data.wheelbase) * tan(steer)
	rotation.y += yaw_rate * delta
	# Aderência lateral: com freio de mão o carro escorrega.
	var grip := float(data.grip) * (0.25 if handbrake else 1.0) * (1.0 + skill * 0.03)
	lat_speed += -yaw_rate * speed * 0.08 * delta * (3.0 if handbrake else 1.0)
	lat_speed = lerpf(lat_speed, 0.0, min(1.0, grip * delta))
	var fwd := -global_transform.basis.z
	var right := global_transform.basis.x
	var before := fwd * speed + right * lat_speed
	velocity = before
	move_and_slide()
	global_position.y = 0.0
	# Colisões
	for i in get_slide_collision_count():
		var c := get_slide_collision(i)
		var n := c.get_normal()
		n.y = 0
		var impact := -before.dot(n.normalized()) if n.length() > 0.01 else 0.0
		if impact > 2.5 and _hit_cd <= 0.0:
			_hit_cd = 0.6
			_on_crash(impact, c.get_collider())
		speed *= 0.9 if impact < 2.5 else 0.25
	# Pedestres
	if absf(speed) > 1.5:
		for b in _hit_area.get_overlapping_bodies():
			if b.has_method("knock_down") and b.state not in ["knocked", "hidden"]:
				if absf(speed) > 3.0:
					b.knock_down(fwd * signf(speed), absf(speed) / 3.0, driver != null)
					speed *= 0.7
					Audio.play_at("crash", global_position, -6.0, 1.4)
				else:
					b.on_bumped(self)
	# Combustível, distância e habilidade
	var moved := absf(speed) * delta
	if driver:
		fuel = max(0.0, fuel - (moved / 1000.0) * float(data.fuel_per_km) * 40.0 * (0.4 + throttle * 0.6) - delta * 0.0004)
		if fuel <= 0.0 and throttle > 0.0 and _hit_cd <= 0.0:
			_hit_cd = 3.0
			Events.notify.emit(tr("Sem combustível!"), "bad")
		_dist_acc += moved
		if _dist_acc > 100.0:
			_dist_acc = 0.0
			Game.player.skills.add_xp("direcao", 3.0)
			Game.player.stats["km_dirigidos"] = float(Game.player.stats.get("km_dirigidos", 0.0)) + 0.1
		_engine.pitch_scale = 0.55 + absf(speed) / max_speed * 1.3
		_engine.volume_db = -12.0 + throttle * 4.0
	visual.animate(speed, steer, delta)
	var night := Clock.is_night()
	visual.set_lights(night and driver != null, brake > 0.0 and speed > 0.3)
	for l in _headlights:
		l.visible = night and driver != null
	if world:
		visual.position.y = lerpf(visual.position.y, world.ground_height(global_position.x, global_position.z), min(1.0, delta * 8.0))

func _on_crash(impact: float, other: Object) -> void:
	var skill := Game.player.skills.level("direcao")
	var dmg := (impact - 2.5) * 4.0 * (1.0 - skill * 0.04)
	damage = clampf(damage + dmg, 0.0, 100.0)
	Audio.play_at("crash", global_position, clampf(impact * 1.5 - 12.0, -12.0, 4.0))
	if driver and impact > 9.0:
		Game.player.needs.consume({"saude": -(impact - 9.0) * 3.0})
		Events.notify.emit(tr("Você se machucou na batida!"), "bad")
	if other and other.has_method("on_hit_by_player") and driver:
		other.on_hit_by_player(impact)
	if damage >= 100.0:
		Events.notify.emit(tr("O motor morreu. O carro precisa de conserto."), "bad")
	elif dmg > 8.0 and driver:
		Events.notify.emit(tr("Batida! Dano do veículo: %d%%") % int(damage), "bad")
	_update_damage_fx()
	save_state()

func _update_damage_fx() -> void:
	if visual and visual.smoke:
		visual.smoke.emitting = damage >= 65.0

func save_state() -> void:
	var st: Dictionary = Game.world_state.vehicles.get(vehicle_id, {})
	st.pos = [global_position.x, 0.0, global_position.z]
	st.rot = rad_to_deg(rotation.y)
	st.fuel = fuel
	st.damage = damage
	Game.world_state.vehicles[vehicle_id] = st

func refresh_from_state() -> void:
	var st: Dictionary = Game.world_state.vehicles.get(vehicle_id, {})
	fuel = float(st.get("fuel", fuel))
	damage = float(st.get("damage", damage))
	_update_damage_fx()
