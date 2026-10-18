class_name PlayerState
extends RefCounted
## Estado persistente do personagem: necessidades, inventário, dinheiro, habilidades e posição.

const BASE_CAPACITY := 12.0

var name: String = "Davi"
var appearance: Dictionary = {"skin": "#c68e6a", "hair": "#2b1d12", "hair_style": "short", "shirt": "#3f7f9f", "pants": "#2f3a4a", "height": 1.0, "build": 1.0}
var needs := Needs.new()
var inventory := Inventory.new(BASE_CAPACITY, "player")
var skills := Skills.new()
var money: int = 0
var position := Vector3.ZERO
var rotation_y: float = 0.0
var vehicle_id: String = ""
var equipped: Dictionary = {}   # slot -> item id (ex.: "costas": "mochila", "camisa": "camiseta_azul")
var traits: Array = []          # ids de data/traits.json escolhidos na criação
var activity: String = "idle"   # atividade atual, usada pela simulação de necessidades
var stats: Dictionary = {"km_dirigidos": 0.0, "pedidos_entregues": 0, "dinheiro_ganho": 0}

func capacity() -> float:
	var cap := BASE_CAPACITY
	for slot in equipped:
		cap += float(Data.item(equipped[slot]).get("capacity_bonus", 0.0))
	return cap

func refresh_capacity() -> void:
	inventory.capacity = capacity()
	needs.mods = trait_mods()

## Soma dos modificadores dos traços (multiplicadores multiplicam; booleanos valem se algum tiver).
func trait_mods() -> Dictionary:
	var out := {}
	for t in traits:
		var m: Dictionary = Data.trait_def(t).get("mods", {})
		for k in m:
			if typeof(m[k]) == TYPE_BOOL:
				out[k] = bool(out.get(k, false)) or bool(m[k])
			else:
				out[k] = float(out.get(k, 1.0)) * float(m[k])
	return out

func apply_traits(ids: Array) -> void:
	traits = []
	for t in ids:
		if not Data.trait_def(t).is_empty() and not traits.has(t):
			traits.append(t)
			var start: Dictionary = Data.trait_def(t).get("skills", {})
			for sk in start:
				skills.xp[sk] = max(float(skills.xp.get(sk, 0.0)), float(Skills.THRESHOLDS[int(start[sk])]))
	refresh_capacity()

## Isolamento térmico somado das roupas vestidas.
func warmth() -> float:
	var w := 0.0
	for slot in equipped:
		w += float(Data.item(equipped[slot]).get("warmth", 0.0))
	return w

func rain_protected() -> bool:
	for slot in equipped:
		if Data.item(equipped[slot]).get("rain_protect", false):
			return true
	return false

## Aparência final: cores das roupas vestidas sobrepõem a base escolhida na criação.
func visual_appearance() -> Dictionary:
	var a := appearance.duplicate()
	if equipped.has("camisa"):
		a.shirt = Data.item(equipped.camisa).get("color", a.get("shirt", "#4477aa"))
	if equipped.has("agasalho"):
		a.shirt = Data.item(equipped.agasalho).get("color", a.shirt)
		a.jacket = true
	if equipped.has("chuva"):
		a.shirt = Data.item(equipped.chuva).get("color", a.shirt)
		a.jacket = true
	if equipped.has("cabeca"):
		a.cap = Data.item(equipped.cabeca).get("color", "#333333")
	if equipped.has("costas"):
		a.backpack = Data.item(equipped.costas).get("color", "#5a4a3a")
	return a

func is_overloaded() -> bool:
	return inventory.weight() > capacity()

func add_money(amount: int, reason: String = "") -> void:
	if amount == 0:
		return
	money += amount
	if amount > 0:
		stats["dinheiro_ganho"] = int(stats.get("dinheiro_ganho", 0)) + amount
	Events.money_changed.emit(money, amount)
	if reason != "":
		Events.notify.emit("%sR$ %d — %s" % ["+" if amount > 0 else "-", abs(amount), tr(reason)], "money")

func can_afford(amount: int) -> bool:
	return money >= amount

func spend(amount: int, reason: String = "") -> bool:
	if amount < 0 or money < amount:
		return false
	add_money(-amount, reason)
	return true

func equip(item_id: String) -> bool:
	var slot: String = Data.item(item_id).get("slot", "")
	if slot == "" or not inventory.has(item_id):
		return false
	equipped[slot] = item_id
	refresh_capacity()
	Events.appearance_changed.emit()
	return true

func unequip(slot: String) -> void:
	equipped.erase(slot)
	refresh_capacity()
	Events.appearance_changed.emit()

func is_equipped(item_id: String) -> bool:
	return equipped.values().has(item_id)

func to_dict() -> Dictionary:
	return {
		"name": name,
		"appearance": appearance.duplicate(),
		"needs": needs.to_dict(),
		"inventory": inventory.to_dict(),
		"skills": skills.to_dict(),
		"money": money,
		"position": [position.x, position.y, position.z],
		"rotation_y": rotation_y,
		"vehicle_id": vehicle_id,
		"equipped": equipped.duplicate(),
		"traits": traits.duplicate(),
		"stats": stats.duplicate(),
	}

func from_dict(d: Dictionary) -> void:
	name = str(d.get("name", name))
	appearance = d.get("appearance", appearance).duplicate()
	needs.from_dict(d.get("needs", {}))
	inventory.from_dict(d.get("inventory", {}))
	skills.from_dict(d.get("skills", {}))
	money = int(d.get("money", 0))
	var p: Array = d.get("position", [0, 0, 0])
	position = Vector3(float(p[0]), float(p[1]), float(p[2]))
	rotation_y = float(d.get("rotation_y", 0.0))
	vehicle_id = str(d.get("vehicle_id", ""))
	equipped = d.get("equipped", {}).duplicate()
	traits = d.get("traits", []).duplicate()
	for slot in equipped.keys():
		if not inventory.has(equipped[slot]):
			equipped.erase(slot)
	stats = d.get("stats", stats).duplicate()
	refresh_capacity()
