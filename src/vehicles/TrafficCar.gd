class_name TrafficCar
extends AnimatableBody3D
## Carro do trânsito (cinemático). Segue faixas e curvas do grafo, respeita semáforo
## e preferência, mantém distância, para para pedestres, buzina para quem atrapalha.
## Clientes do drive-thru desviam para a fila, fazem o pedido na janela e voltam à rua.

var sys: TrafficSystem
var world: Node
var car_id := ""
var vtype := "sedan"
var color := Color.WHITE
var customer := false
var visual: CarVisual
var points := PackedVector3Array()
var idx := 0
var mode := "edge"           # edge, turn, custom
var edge := -1
var next_edge := -1
var cur_axis := ""
var speed := 0.0
var _granted := false
var _stopped_t := 0.0
var _horn_cd := 0.0
var _order_state := ""       # "", "ordered", "leaving"
var _bark: Label3D
var _bark_t := 0.0
var damage := 0.0
var length := 4.4

func _ready() -> void:
	collision_layer = 16
	collision_mask = 0
	sync_to_physics = false
	var data := Data.vehicle(vtype)
	var size: Array = data.get("size", [1.8, 1.4, 4.4])
	length = float(size[2])
	var cs := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(float(size[0]), float(size[1]) * 0.8, float(size[2]))
	cs.shape = box
	cs.position = Vector3(0, 0.3 + float(size[1]) * 0.4, 0)
	add_child(cs)
	visual = CarVisual.new()
	add_child(visual)
	visual.build(data.get("kind", "sedan"), size, color)
	_bark = Label3D.new()
	_bark.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_bark.no_depth_test = true
	_bark.font_size = 34
	_bark.pixel_size = 0.0055
	_bark.outline_size = 12
	_bark.position = Vector3(0, 2.4, 0)
	_bark.visible = false
	add_child(_bark)
	add_to_group("traffic")

func start_on_edge(ei: int, pos: Vector3) -> void:
	edge = ei
	mode = "edge"
	cur_axis = sys.edges[ei].axis
	points = PackedVector3Array([pos, sys.edges[ei].end])
	idx = 1
	global_position = pos
	_face(sys.edges[ei].dir, 1.0)
	speed = 6.0

func _face(dir: Vector3, w: float) -> void:
	if dir.length() < 0.01:
		return
	var target := atan2(-dir.x, -dir.z)
	rotation.y = lerp_angle(rotation.y, target, w)

func _physics_process(delta: float) -> void:
	_horn_cd -= delta
	if _bark.visible:
		_bark_t -= delta
		if _bark_t <= 0.0:
			_bark.visible = false
	if idx >= points.size():
		_on_points_end()
		return
	var target_speed := LANE_SPEED_FOR(mode)
	var pos := global_position
	var seg_target := points[idx]
	# Desvio para o drive-thru quando passa pelo ponto de entrada.
	if customer and mode == "edge" and edge == sys.join_edge and sys.drive_path.size() > 0 and _order_state == "":
		if pos.x >= sys.drive_path[0].x - 0.6 and pos.x < sys.drive_path[0].x + 3.0:
			mode = "custom"
			points = sys.drive_path.duplicate()
			idx = 1
			return
	# Faixa de retenção / pedido de passagem no cruzamento.
	if mode == "edge":
		var dist_end := pos.distance_to(sys.edges[edge].end)
		if dist_end < 14.0 and not _granted:
			if next_edge < 0:
				var to_join: bool = customer and sys.join_edge >= 0 and sys.edges[edge].b == sys.edges[sys.join_edge].a
				next_edge = sys.join_edge if to_join else sys.choose_next(edge)
			if sys.can_enter(self, sys.edges[edge].b, cur_axis, dist_end):
				_granted = true
			else:
				target_speed = min(target_speed, sqrt(max(0.0, 2.0 * 5.0 * (dist_end - 0.6))))
	# Janela do drive-thru
	if mode == "custom" and customer:
		var wpos := sys.drive_path[sys.window_index]
		var dw := pos.distance_to(wpos)
		if idx <= sys.window_index and _order_state != "leaving":
			if dw < 9.0:
				target_speed = min(target_speed, sqrt(max(0.0, 2.0 * 3.0 * (dw - 0.2))))
			if dw < 0.6:
				_at_window()
				target_speed = 0.0 if _order_state != "leaving" else target_speed
		else:
			target_speed = min(target_speed, 4.0)
	# Obstáculos à frente
	var fwd := -global_transform.basis.z
	var right := global_transform.basis.x
	var look := 5.0 + speed * 1.8
	var blocker: Dictionary = {}
	var gap := 1e9
	for o in sys.obstacles():
		if o.node == self:
			continue
		var rel: Vector3 = o.pos - pos
		rel.y = 0
		var along := rel.dot(fwd)
		if along <= 0.0 or along > look + length:
			continue
		if absf(rel.dot(right)) > 1.1 + float(o.width):
			continue
		var g := along - length * 0.5 - float(o.half)
		if g < gap:
			gap = g
			blocker = o
	if not blocker.is_empty():
		target_speed = min(target_speed, sqrt(max(0.0, 2.0 * 6.0 * (gap - 1.8))))
	# Aceleração/frenagem
	if speed < target_speed:
		speed = min(target_speed, speed + 3.0 * delta)
	else:
		speed = max(target_speed, speed - 8.0 * delta)
	if speed < 0.1:
		_stopped_t += delta
		if not blocker.is_empty() and blocker.player and _stopped_t > 3.0 and _horn_cd <= 0.0:
			_horn_cd = 5.0
			Audio.play_at("horn", pos, -2.0, rng_pitch())
			_say(Data.chatter().get("horn", ["Anda!"]).pick_random())
		if _stopped_t > 30.0 and world.player and pos.distance_to(world.player.global_position) > 45.0:
			sys.remove_car(self)
			return
	else:
		_stopped_t = 0.0
	# Avança pelo polígono de pontos
	var step := speed * delta
	var p := pos
	while step > 0.0 and idx < points.size():
		var to := points[idx] - p
		to.y = 0
		var d := to.length()
		if d <= step:
			p = points[idx]
			step -= d
			idx += 1
		else:
			p += to / d * step
			step = 0.0
	var mv := p - pos
	if mv.length() > 0.001:
		_face(mv, min(1.0, delta * 8.0))
	global_position = Vector3(p.x, 0, p.z)
	visual.animate(speed, 0.0, delta)
	visual.set_lights(Clock.is_night(), target_speed < speed - 0.5)
	visual.position.y = lerpf(visual.position.y, world.ground_height(p.x, p.z), min(1.0, delta * 8.0))

func LANE_SPEED_FOR(m: String) -> float:
	match m:
		"turn": return TrafficSystem.TURN_SPEED
		"custom": return 4.5
	return TrafficSystem.LANE_SPEED

func rng_pitch() -> float:
	return randf_range(0.9, 1.1)

func _on_points_end() -> void:
	match mode:
		"edge":
			if not _granted:
				return
			if next_edge < 0:
				next_edge = sys.choose_next(edge)
			mode = "turn"
			points = sys.connector(edge, next_edge)
			idx = 0
		"turn":
			sys.release(self, sys.edges[edge].b)
			edge = next_edge
			next_edge = -1
			_granted = false
			cur_axis = sys.edges[edge].axis
			mode = "edge"
			points = PackedVector3Array([global_position, sys.edges[edge].end])
			idx = 1
		"custom":
			# Volta para a avenida como um carro comum.
			if customer:
				Game.job.customer_left(car_id)
			customer = false
			edge = sys.join_edge
			cur_axis = sys.edges[edge].axis
			_granted = false
			mode = "edge"
			points = PackedVector3Array([global_position, sys.edges[edge].end])
			idx = 1

func _at_window() -> void:
	var status := Game.job.status_of(car_id)
	if _order_state == "":
		if Game.job.on_shift:
			Game.job.create_order(car_id, Game.rng)
			_order_state = "ordered"
			Audio.play_at("order", global_position, 0.0)
			_say(tr("Boa tarde! Quero %s.") % DriveThruJob.order_text(Game.job.order_for(car_id).get("items", {})))
		else:
			Game.job.customers_requested = max(0, Game.job.customers_requested - 1)
			_order_state = "leaving"
			_say(tr("Ué, fechado?"))
	elif _order_state == "ordered":
		match status:
			"done":
				_order_state = "leaving"
				_say(tr("Valeu! Até a próxima."))
			"failed":
				_order_state = "leaving"
				_say(tr("Demorou demais! Nunca mais volto."))
				Audio.play_at("horn", global_position, 0.0)
			"none":
				_order_state = "leaving"

func on_hit_by_player(impact: float) -> void:
	damage += impact
	_say(tr("Ô, barbeiro! Olha o que você fez!"))
	Audio.play_at("horn", global_position, 2.0)
	if impact > 6.0:
		Game.law.report("batida", "", ["motorista_" + car_id], Clock.day(), Game.relations)
		Events.notify.emit(tr("O motorista anotou sua placa. Você será multado."), "bad")

func _say(text: String) -> void:
	_bark.text = text
	_bark.visible = true
	_bark_t = 3.5
