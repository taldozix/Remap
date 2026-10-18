class_name Player
extends CharacterBody3D
## Controle do jogador: movimento relativo à câmera, correr, interação contextual,
## ações com duração (barra de progresso), sentar/deitar e entrar/sair de veículos.

const WALK_SPEED := 2.4
const RUN_SPEED := 5.2

var world: Node = null
var visual: Humanoid
var state := "free"          # free, sitting, lying, driving, sleeping
var seat: Furniture = null
var vehicle: Node = null
var timed: Dictionary = {}   # {"label", "t", "dur", "cb", "sound"}
var focus: Node = null
var focus_actions: Array = []
var input_locked := false
var _scan_t := 0.0
var _step_t := 0.0
var _bump_t := 0.0
var _run_dist := 0.0
var _shape: CollisionShape3D

func _ready() -> void:
	collision_layer = 4
	collision_mask = 1 | 2 | 8 | 16
	floor_snap_length = 0.3
	_shape = CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.3
	cap.height = 1.75
	_shape.shape = cap
	_shape.position = Vector3(0, 0.875, 0)
	add_child(_shape)
	visual = Humanoid.new()
	add_child(visual)
	visual.build(Game.player.appearance)
	add_to_group("player")

func _physics_process(delta: float) -> void:
	_bump_t -= delta
	if state == "driving":
		if vehicle:
			global_position = vehicle.global_position
		Game.player.activity = "drive"
		return
	if not timed.is_empty():
		_process_timed(delta)
	var input := Vector2.ZERO
	if not input_locked and world and not world.ui_blocking():
		input = Input.get_vector("move_left", "move_right", "move_up", "move_down")
	if state in ["sitting", "lying", "sleeping"]:
		Game.player.activity = "sleep" if state == "sleeping" else "rest"
		return
	var needs: Needs = Game.player.needs
	var running := Input.is_action_pressed("run") and needs.can_run() and not Game.player.is_overloaded() and input.length() > 0.1 and not input_locked
	var spd := (RUN_SPEED * (1.0 + 0.03 * Game.player.skills.level("condicionamento")) if running else WALK_SPEED) * needs.speed_multiplier()
	if Game.player.is_overloaded():
		spd *= 0.6
	var dir := Vector3.ZERO
	if input.length() > 0.05:
		var yaw: float = world.camera_yaw() if world else 0.0
		dir = (Basis(Vector3.UP, yaw) * Vector3(input.x, 0, input.y))
		dir.y = 0
		dir = dir.normalized() * min(1.0, input.length())
		if not timed.is_empty():
			_cancel_timed()
	velocity.x = dir.x * spd
	velocity.z = dir.z * spd
	if not is_on_floor():
		velocity.y -= 9.8 * delta
	else:
		velocity.y = -0.5
	move_and_slide()
	var hv := Vector2(velocity.x, velocity.z).length()
	if hv > 0.2:
		rotation.y = lerp_angle(rotation.y, atan2(-dir.x, -dir.z), min(1.0, delta * 12.0))
		visual.set_pose("run" if running else "walk")
		visual.speed = hv
		_step_t -= delta
		if _step_t <= 0.0:
			_step_t = 0.3 if running else 0.48
			Audio.play_at("step", global_position, -16.0, randf_range(0.85, 1.15))
		if running:
			_run_dist += hv * delta
			if _run_dist > 25.0:
				_run_dist = 0.0
				Game.player.skills.add_xp("condicionamento", 2.0)
	elif timed.is_empty():
		visual.set_pose("idle")
	var on_shift_work: bool = Game.job.on_shift and world and world.is_player_in_job_area()
	if running:
		Game.player.activity = "run"
	elif not timed.is_empty() or on_shift_work:
		Game.player.activity = "work"
	elif hv > 0.2:
		Game.player.activity = "walk"
	else:
		Game.player.activity = "idle"
	# Esbarrões em NPCs
	for i in get_slide_collision_count():
		var c := get_slide_collision(i).get_collider()
		if c and c.has_method("on_bumped") and _bump_t <= 0.0:
			_bump_t = 2.0
			c.on_bumped(self)
	if world:
		visual.position.y = lerpf(visual.position.y, world.ground_height(global_position.x, global_position.z), min(1.0, delta * 12.0))

func _process(delta: float) -> void:
	_scan_t -= delta
	if _scan_t <= 0.0:
		_scan_t = 0.1
		_scan_interactables()

# ------------------------------------------------------------------ interação
func _scan_interactables() -> void:
	focus = null
	focus_actions = []
	if state == "driving" or world == null:
		if state == "driving" and vehicle:
			focus = vehicle
			focus_actions = vehicle.get_driver_actions(self)
		return
	if state in ["sitting", "lying"]:
		focus = seat
		focus_actions = _seated_actions()
		return
	var best: Node = null
	var best_score := 1e9
	var fwd := -global_transform.basis.z
	var space := get_world_3d().direct_space_state
	for n in get_tree().get_nodes_in_group("interactable"):
		if not is_instance_valid(n) or n == self or not n.is_visible_in_tree():
			continue
		var p: Vector3 = n.interact_point()
		var r: float = n.get("radius") if n.get("radius") != null else 1.6
		var diff := p - global_position
		diff.y = 0
		var d := diff.length()
		if d > r:
			continue
		var score := d - (diff.normalized().dot(fwd) * 0.7 if d > 0.05 else 0.7)
		if score >= best_score:
			continue
		var q := PhysicsRayQueryParameters3D.create(global_position + Vector3(0, 1.1, 0), p + Vector3(0, 1.0, 0), 1)
		var hit := space.intersect_ray(q)
		if not hit.is_empty() and hit.position.distance_to(p + Vector3(0, 1.0, 0)) > 0.95:
			continue
		best = n
		best_score = score
	if best:
		focus = best
		focus_actions = best.get_actions(self)

func _seated_actions() -> Array:
	var out := [Interactable.action("stand", tr("Levantar"))]
	out.append(Interactable.action("wait", tr("Esperar 1 hora")))
	if state == "lying" and seat and seat.kind == "bed" and seat.owner_id == "player":
		out.append(Interactable.action("sleep", tr("Dormir...")))
	return out

func _unhandled_input(event: InputEvent) -> void:
	if world == null or world.ui_blocking() or input_locked:
		return
	if event.is_action_pressed("interact"):
		_do_focus_action(-1)
	elif event.is_action_pressed("vehicle"):
		if state == "driving":
			exit_vehicle()
		else:
			var v: Node = world.nearest_vehicle(global_position, 3.2)
			if v:
				var acts: Array = v.get_actions(self)
				for a in acts:
					if a.enabled and a.id in ["enter", "breakin"]:
						v.do_action(self, a.id)
						break
	else:
		for i in 4:
			if event.is_action_pressed("option_%d" % (i + 1)):
				_do_focus_action(i)

func _do_focus_action(index: int) -> void:
	if not timed.is_empty():
		return
	_scan_interactables()
	if focus == null or focus_actions.is_empty():
		return
	var act: Dictionary = {}
	if index < 0:
		for a in focus_actions:
			if a.enabled:
				act = a
				break
	elif index < focus_actions.size():
		act = focus_actions[index]
	if act.is_empty() or not act.enabled:
		if not act.is_empty() and act.get("hint", "") != "":
			Events.notify.emit(act.hint, "bad")
		return
	if focus == seat and state in ["sitting", "lying"]:
		match act.id:
			"stand": stand_up()
			"wait": _wait_hour()
			"sleep": Events.request_sleep.emit(seat.item_id, 1.0)
		return
	Audio.play("ui_click", -12.0)
	focus.do_action(self, act.id)

## Executa uma ação programaticamente (usado pelo teste de aceitação).
func perform(target: Node, action_id: String) -> void:
	target.do_action(self, action_id)

# ------------------------------------------------------------------ ações com duração
func start_timed(label: String, seconds: float, cb: Callable, sound: String = "") -> void:
	timed = {"label": label, "t": 0.0, "dur": max(0.1, seconds), "cb": cb}
	visual.set_pose("work")
	if sound != "":
		Audio.play_at(sound, global_position, -8.0)

func _process_timed(delta: float) -> void:
	timed.t = float(timed.t) + delta
	if float(timed.t) >= float(timed.dur):
		var cb: Callable = timed.cb
		timed = {}
		visual.set_pose("idle")
		if cb.is_valid():
			cb.call()

func _cancel_timed() -> void:
	timed = {}
	Events.notify.emit(tr("Ação cancelada."), "info")

func timed_progress() -> float:
	return clampf(float(timed.get("t", 0)) / float(timed.get("dur", 1)), 0.0, 1.0) if not timed.is_empty() else 0.0

# ------------------------------------------------------------------ sentar, deitar
func sit_on(f: Furniture) -> void:
	seat = f
	state = "sitting"
	_shape.disabled = true
	velocity = Vector3.ZERO
	var t := f.seat_transform()
	global_position = Vector3(t.origin.x, 0.0, t.origin.z)
	rotation.y = deg_to_rad(f.facing)
	visual.set_pose("sit")
	visual.position = Vector3(0, f.seat_height - 0.47, 0)

func rest_on_bed(f: Furniture) -> void:
	seat = f
	state = "lying"
	_shape.disabled = true
	velocity = Vector3.ZERO
	rotation.y = deg_to_rad(f.facing)
	global_position = f.global_position + Basis(Vector3.UP, rotation.y) * Vector3(0, 0, 0.85)
	visual.set_pose("lie")
	visual.position = Vector3(0, 0.62, 0)

func stand_up() -> void:
	if seat:
		var out := seat.global_position + seat.forward() * (0.75 if seat.kind != "bed" else 1.3)
		if seat.kind == "bed":
			out = seat.global_position + Basis(Vector3.UP, deg_to_rad(seat.facing)) * Vector3(1.2, 0, 0.2)
		global_position = Vector3(out.x, 0.05, out.z)
	seat = null
	state = "free"
	_shape.disabled = false
	visual.position = Vector3.ZERO
	visual.set_pose("idle")

func _wait_hour() -> void:
	Game.player.activity = "rest"
	Clock.advance(60.0)
	Events.notify.emit(tr("Uma hora se passou."), "info")

func begin_sleep() -> void:
	state = "sleeping"
	visual.set_pose("lie")

func end_sleep() -> void:
	if seat:
		state = "lying"
	else:
		state = "free"

# ------------------------------------------------------------------ veículos
func enter_vehicle(v: Node) -> void:
	if not timed.is_empty():
		_cancel_timed()
	vehicle = v
	state = "driving"
	_shape.disabled = true
	visual.visible = false
	velocity = Vector3.ZERO
	Game.player.vehicle_id = v.vehicle_id
	v.set_driver(self)
	Events.player_vehicle_changed.emit(v)

func exit_vehicle() -> void:
	if vehicle == null:
		return
	if absf(vehicle.speed) > 3.0:
		Events.notify.emit(tr("Pare o carro antes de sair."), "bad")
		return
	var spot: Vector3 = vehicle.exit_position()
	vehicle.set_driver(null)
	global_position = spot
	rotation.y = vehicle.rotation.y
	vehicle = null
	state = "free"
	_shape.disabled = false
	visual.visible = true
	Game.player.vehicle_id = ""
	Events.player_vehicle_changed.emit(null)

func sync_to_state() -> void:
	var p := global_position
	if state == "driving" and vehicle:
		p = vehicle.global_position
	elif state in ["sitting", "lying", "sleeping"] and seat:
		var out := seat.global_position + seat.forward() * 0.8
		p = Vector3(out.x, 0, out.z)
	Game.player.position = p
	Game.player.rotation_y = rotation.y
