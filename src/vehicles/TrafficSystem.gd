class_name TrafficSystem
extends Node3D
## Trânsito: grafo de faixas gerado das ruas do mapa (mão direita, como no Brasil), semáforo no cruzamento central, preferência por ordem de chegada
## nos demais, carros que seguem o da frente, param para pedestres e buzinam.
## Também envia clientes para a fila do drive-thru durante o turno do jogador.

const LANE_SPEED := 11.0
const TURN_SPEED := 5.5
const GREEN := 16.0
const YELLOW := 3.0

var world: Node = null
var map: Dictionary
var nodes: Dictionary = {}     # "x,z" -> {"pos", "signal", "occupant", "occ_t"}
var edges: Array = []          # {"a","b","dir","start","end","axis"}
var out_edges: Dictionary = {}
var cars: Array = []
var phase := 0.0
var target_count := 7
var _spawn_t := 0.0
var _next_id := 1
var _lamps: Array = []         # [{"axis", "mats": [r, y, g]}]
var rng := RandomNumberGenerator.new()
var drive_path := PackedVector3Array()
var window_index := 9
var join_edge := -1

func setup(p_world: Node, p_map: Dictionary) -> void:
	world = p_world
	map = p_map
	rng.randomize()
	var r: Dictionary = map.roads
	var off := float(r.get("lane_offset", 1.75))
	for x in r.xs:
		for z in r.zs:
			nodes[_key(x, z)] = {"pos": Vector3(float(x), 0, float(z)), "signal": false, "occupant": null, "occ_t": 0.0}
	for s in r.get("signals", []):
		if nodes.has(_key(s[0], s[1])):
			nodes[_key(s[0], s[1])].signal = true
			_build_signal(nodes[_key(s[0], s[1])].pos)
	for i in r.xs.size():
		for j in r.zs.size():
			var a := _key(r.xs[i], r.zs[j])
			if i + 1 < r.xs.size():
				_add_edge_pair(a, _key(r.xs[i + 1], r.zs[j]), off)
			if j + 1 < r.zs.size():
				_add_edge_pair(a, _key(r.xs[i], r.zs[j + 1]), off)
	var dt: Dictionary = map.get("drivethru", {})
	for p in dt.get("path", []):
		drive_path.append(Vector3(float(p[0]), 0, float(p[1])))
	window_index = int(dt.get("window_index", 9))
	var j: Dictionary = dt.get("join", {})
	if not j.is_empty():
		join_edge = find_edge(_key(j.from[0], j.from[1]), _key(j.to[0], j.to[1]))
	target_count = int(round(7.0 * float(Settings.get_value("graphics", "traffic_density"))))
	Settings.changed.connect(func(_s): target_count = int(round(7.0 * float(Settings.get_value("graphics", "traffic_density")))))
	for k in target_count:
		spawn_random(true)

func _key(x, z) -> String:
	return "%d,%d" % [int(x), int(z)]

func _add_edge_pair(a: String, b: String, off: float) -> void:
	for pair in [[a, b], [b, a]]:
		var pa: Vector3 = nodes[pair[0]].pos
		var pb: Vector3 = nodes[pair[1]].pos
		var d := (pb - pa).normalized()
		var right := Vector3(-d.z, 0, d.x)
		var e := {
			"a": pair[0], "b": pair[1], "dir": d,
			"start": pa + d * WorldBuilder.STOP_DIST + right * off,
			"end": pb - d * WorldBuilder.STOP_DIST + right * off,
			"axis": "ew" if absf(d.x) > 0.5 else "ns",
		}
		edges.append(e)
		if not out_edges.has(pair[0]):
			out_edges[pair[0]] = []
		out_edges[pair[0]].append(edges.size() - 1)

func find_edge(a: String, b: String) -> int:
	for i in edges.size():
		if edges[i].a == a and edges[i].b == b:
			return i
	return -1

func choose_next(ei: int) -> int:
	var e: Dictionary = edges[ei]
	var options: Array = []
	for o in out_edges.get(e.b, []):
		if edges[o].b != e.a:
			options.append(o)
	if options.is_empty():
		return find_edge(e.b, e.a)
	return options[rng.randi() % options.size()]

## Curva entre o fim de uma faixa e o início da próxima (Bézier quadrática).
func connector(e1: int, e2: int) -> PackedVector3Array:
	var a: Vector3 = edges[e1].end
	var b: Vector3 = edges[e2].start
	var d1: Vector3 = edges[e1].dir
	var c := a + d1 * (b - a).dot(d1)
	if d1.dot(edges[e2].dir) > 0.99:
		c = (a + b) * 0.5
	var out := PackedVector3Array()
	for i in range(1, 9):
		var t := i / 8.0
		out.append(a.lerp(c, t).lerp(c.lerp(b, t), t))
	return out

# ------------------------------------------------------------------ semáforo
func light_state(axis: String) -> String:
	var cycle := 2.0 * (GREEN + YELLOW)
	var t := fmod(phase, cycle)
	var ew := "green" if t < GREEN else ("yellow" if t < GREEN + YELLOW else "red")
	var ns := "red" if t < GREEN + YELLOW else ("green" if t < 2.0 * GREEN + YELLOW else "yellow")
	return ew if axis == "ew" else ns

func can_enter(car: Node, node_key: String, axis: String, dist: float) -> bool:
	var n: Dictionary = nodes[node_key]
	if n.signal:
		var s := light_state(axis)
		if s == "red" or (s == "yellow" and dist > 3.0):
			return false
	var occ = n.occupant
	if occ != null and is_instance_valid(occ) and occ != car and float(n.occ_t) < 7.0:
		if not (n.signal and occ.get("cur_axis") == axis):
			return false
	n.occupant = car
	n.occ_t = 0.0
	return true

func release(car: Node, node_key: String) -> void:
	if nodes.has(node_key) and nodes[node_key].occupant == car:
		nodes[node_key].occupant = null

func _build_signal(center: Vector3) -> void:
	var hw := 4.5 + 1.2
	var heads := [
		{"pos": Vector3(hw, 0, hw), "facing": 180.0, "axis": "ns"},
		{"pos": Vector3(-hw, 0, -hw), "facing": 0.0, "axis": "ns"},
		{"pos": Vector3(hw, 0, -hw), "facing": -90.0, "axis": "ew"},
		{"pos": Vector3(-hw, 0, hw), "facing": 90.0, "axis": "ew"},
	]
	for h in heads:
		var root := Node3D.new()
		root.position = center + h.pos
		root.rotation_degrees.y = h.facing
		add_child(root)
		var pole := MeshInstance3D.new()
		var pm := CylinderMesh.new()
		pm.top_radius = 0.07
		pm.bottom_radius = 0.09
		pm.height = 3.4
		pole.mesh = pm
		pole.position = Vector3(0, 1.7, 0)
		pole.material_override = Mat.textured("metal_painted", Color(0.3, 0.33, 0.33))
		root.add_child(pole)
		var box := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(0.35, 0.95, 0.25)
		box.mesh = bm
		box.position = Vector3(0, 3.2, 0)
		box.material_override = Mat.textured("metal_painted", Color(0.14, 0.15, 0.16))
		root.add_child(box)
		for i in 3:
			var visor := MeshInstance3D.new()
			var vm := BoxMesh.new()
			vm.size = Vector3(0.24, 0.03, 0.14)
			visor.mesh = vm
			visor.position = Vector3(0, 3.62 - i * 0.29, -0.2)
			visor.material_override = box.material_override
			root.add_child(visor)
		var mats: Array = []
		var cols := [Color(1, 0.15, 0.1), Color(1, 0.75, 0.1), Color(0.2, 1, 0.35)]
		for i in 3:
			var m := StandardMaterial3D.new()
			m.albedo_color = cols[i] * 0.3
			m.emission_enabled = true
			m.emission = cols[i]
			m.emission_energy_multiplier = 0.0
			var lamp := MeshInstance3D.new()
			var sm := SphereMesh.new()
			sm.radius = 0.09
			sm.height = 0.18
			lamp.mesh = sm
			lamp.material_override = m
			lamp.position = Vector3(0, 3.5 - i * 0.29, -0.14)
			root.add_child(lamp)
			mats.append(m)
		_lamps.append({"axis": h.axis, "mats": mats})

# ------------------------------------------------------------------ carros
func spawn_random(initial: bool = false) -> TrafficCar:
	for attempt in 12:
		var ei := rng.randi() % edges.size()
		var e: Dictionary = edges[ei]
		var t := rng.randf_range(0.0, 0.7) if initial else 0.0
		var pos: Vector3 = e.start.lerp(e.end, t)
		if world.player and pos.distance_to(world.player.global_position) < (25.0 if initial else 40.0):
			continue
		if not _clear(pos, 9.0):
			continue
		return _spawn(ei, pos, false)
	return null

func spawn_customer() -> TrafficCar:
	if join_edge < 0:
		return null
	var origin := find_edge(_key(-60, 0), _key(0, 0))
	for ei in [origin, join_edge]:
		if ei < 0:
			continue
		var pos: Vector3 = edges[ei].start
		if _clear(pos, 9.0):
			var c := _spawn(ei, pos, true)
			return c
	return null

func _clear(pos: Vector3, r: float) -> bool:
	for c in cars:
		if is_instance_valid(c) and c.global_position.distance_to(pos) < r:
			return false
	for v in world.vehicles.values():
		if v.global_position.distance_to(pos) < r:
			return false
	return true

func _spawn(ei: int, pos: Vector3, customer: bool) -> TrafficCar:
	var types := ["sedan", "brisa", "picape", "vanza", "sedan"]
	var colors := [Color("c94c4c"), Color("e8e4d8"), Color("3b5b8c"), Color("2f2f33"), Color("9fb7c9"), Color("d9a441"), Color("5d7a4a")]
	var car := TrafficCar.new()
	car.car_id = "trafego_%d" % _next_id
	_next_id += 1
	car.sys = self
	car.world = world
	car.vtype = types[rng.randi() % types.size()]
	car.color = colors[rng.randi() % colors.size()]
	car.customer = customer
	add_child(car)
	car.start_on_edge(ei, pos)
	cars.append(car)
	return car

func remove_car(car: Node) -> void:
	for k in nodes:
		release(car, k)
	cars.erase(car)
	if car.customer and car._order_state == "":
		Game.job.customers_requested = max(0, Game.job.customers_requested - 1)
	car.queue_free()

func _physics_process(delta: float) -> void:
	phase += delta
	for k in nodes:
		nodes[k].occ_t = float(nodes[k].occ_t) + delta
	for l in _lamps:
		var s := light_state(l.axis)
		l.mats[0].emission_energy_multiplier = 3.0 if s == "red" else 0.0
		l.mats[1].emission_energy_multiplier = 3.0 if s == "yellow" else 0.0
		l.mats[2].emission_energy_multiplier = 3.0 if s == "green" else 0.0
	_spawn_t -= delta
	if _spawn_t <= 0.0:
		_spawn_t = 2.0
		cars = cars.filter(func(c): return is_instance_valid(c))
		var normal := cars.filter(func(c): return not c.customer).size()
		if normal < target_count:
			spawn_random()
		elif normal > target_count:
			for c in cars:
				if not c.customer and world.player and c.global_position.distance_to(world.player.global_position) > 50.0:
					remove_car(c)
					break
		# Clientes do drive-thru
		if Game.job.wants_customer():
			if spawn_customer():
				Game.job.customer_dispatched(Game.rng)

## Obstáculos que os motoristas respeitam: outros carros, veículos do jogador, pedestres.
func obstacles() -> Array:
	var out: Array = []
	for c in cars:
		if is_instance_valid(c):
			out.append({"node": c, "pos": c.global_position, "half": 2.3, "width": 1.0, "player": false})
	for v in world.vehicles.values():
		out.append({"node": v, "pos": v.global_position, "half": 2.4, "width": 1.0, "player": v.driver != null})
	var p: Node = world.player
	if p and p.state != "driving":
		out.append({"node": p, "pos": p.global_position, "half": 0.4, "width": 0.4, "player": true})
	for n in world.npcs.values():
		if n.visible and n.state != "hidden":
			out.append({"node": n, "pos": n.global_position, "half": 0.4, "width": 0.4, "player": false})
	return out
