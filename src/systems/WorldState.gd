class_name WorldState
extends RefCounted
## Estado persistente do mundo: conteúdo de contêineres, portas, itens no chão,
## veículos, NPCs (ferimentos) e clima. Os nós da cena leem e escrevem aqui.

var containers: Dictionary = {}   # id -> Inventory
var container_meta: Dictionary = {}  # id -> {"owner", "name", "private", "building"}
var doors: Dictionary = {}        # id -> {"open": bool, "locked": bool}
var ground_items: Array = []      # [{"id","qty","age","pos":[x,y,z]}]
var vehicles: Dictionary = {}     # id -> {"type","pos","rot","fuel","damage","owner","locked","color"}
var npcs: Dictionary = {}         # id -> {"injured_until": minutos, ...}
var searched: Dictionary = {}     # id de objeto vasculhado -> dia
var weather: Dictionary = {"state": "limpo", "until": 0.0}

## Popula o estado inicial a partir do mapa (novo jogo).
func init_from_map(map: Dictionary) -> void:
	containers.clear()
	container_meta.clear()
	doors.clear()
	ground_items.clear()
	vehicles.clear()
	npcs.clear()
	searched.clear()
	for b in map.get("buildings", []):
		for d in b.get("doors", []):
			doors[d.id] = {"open": false, "locked": bool(d.get("locked", false))}
		for f in b.get("furniture", []):
			if f.has("container"):
				_register_container(f.container, b.get("id", ""))
	for p in map.get("props", []):
		if p.has("container"):
			_register_container(p.container, "")
	for v in map.get("vehicles", []):
		var vd: Dictionary = Data.vehicle(v.type)
		vehicles[v.id] = {
			"type": v.type, "pos": [float(v.pos[0]), 0.0, float(v.pos[1])], "rot": float(v.get("rot", 0)),
			"fuel": float(v.get("fuel", vd.get("fuel_capacity", 40))), "damage": float(v.get("damage", 0)),
			"owner": str(v.get("owner", "")), "locked": bool(v.get("locked", true)), "color": str(v.get("color", "#aaaaaa")),
		}
		if v.has("for_sale"):
			vehicles[v.id]["for_sale"] = v.for_sale.duplicate()
		var trunk := Inventory.new(float(vd.get("trunk_capacity", 30)), "trunk_" + v.id)
		containers["trunk_" + v.id] = trunk
		container_meta["trunk_" + v.id] = {"owner": str(v.get("owner", "")), "name": tr("Porta-malas"), "private": true, "building": ""}
		for item_id in v.get("trunk", {}):
			trunk.add(item_id, int(v.trunk[item_id]), 0.0, false)

func _register_container(c: Dictionary, building: String) -> void:
	var inv := Inventory.new(float(c.get("capacity", 20)), c.id)
	inv.refrigerated = bool(c.get("refrigerated", false))
	for item_id in c.get("contents", {}):
		inv.add(item_id, int(c.contents[item_id]), 0.0, false)
	containers[c.id] = inv
	container_meta[c.id] = {
		"owner": str(c.get("owner", "")), "name": str(c.get("name", "Contêiner")),
		"private": bool(c.get("private", c.get("owner", "") != "")), "building": building,
		"refill": c.get("refill", {}),
	}

func container(id: String) -> Inventory:
	return containers.get(id, null)

func meta(id: String) -> Dictionary:
	return container_meta.get(id, {})

func is_door_locked(id: String) -> bool:
	return bool(doors.get(id, {}).get("locked", false))

func set_door(id: String, open: bool, locked: bool) -> void:
	doors[id] = {"open": open, "locked": locked}

func drop_item(id: String, qty: int, age: float, pos: Vector3) -> void:
	ground_items.append({"id": id, "qty": qty, "age": age, "pos": [pos.x, pos.y, pos.z]})

## Envelhece comida em todos os contêineres e no chão.
func tick_age(hours: float) -> void:
	for id in containers:
		containers[id].tick_age(hours)
	for g in ground_items:
		if Inventory.is_perishable(g.id):
			g.age = float(g.get("age", 0.0)) + hours

## Lixeiras e afins se renovam diariamente (latinhas recicláveis etc.).
func daily_refill(rng: RandomNumberGenerator) -> void:
	for id in container_meta:
		var refill: Dictionary = container_meta[id].get("refill", {})
		for item_id in refill:
			var inv: Inventory = containers[id]
			var max_qty := int(refill[item_id])
			if inv.count(item_id) < max_qty:
				inv.add(item_id, rng.randi_range(0, max_qty - inv.count(item_id)), 0.0, true)

func to_dict() -> Dictionary:
	var c := {}
	for id in containers:
		c[id] = containers[id].to_dict()
	return {
		"containers": c, "doors": doors.duplicate(true), "ground_items": ground_items.duplicate(true),
		"vehicles": vehicles.duplicate(true), "npcs": npcs.duplicate(true), "searched": searched.duplicate(),
		"weather": weather.duplicate(),
	}

## Carrega por cima do estado inicial do mapa: novos contêineres/portas adicionados
## ao mapa depois do salvamento continuam existindo (compatibilidade de versões).
func from_dict(d: Dictionary) -> void:
	init_from_map(Data.map())
	var c: Dictionary = d.get("containers", {})
	for id in c:
		if containers.has(id):
			containers[id].from_dict(c[id])
	var ds: Dictionary = d.get("doors", {})
	for id in ds:
		if doors.has(id):
			doors[id] = {"open": bool(ds[id].get("open", false)), "locked": bool(ds[id].get("locked", false))}
	ground_items = d.get("ground_items", []).duplicate(true)
	var vs: Dictionary = d.get("vehicles", {})
	for id in vs:
		if vehicles.has(id):
			if not vs[id].has("for_sale"):
				vehicles[id].erase("for_sale")
			for k in vs[id]:
				vehicles[id][k] = vs[id][k]
	npcs = d.get("npcs", {}).duplicate(true)
	searched = d.get("searched", {}).duplicate()
	weather = d.get("weather", weather).duplicate()
