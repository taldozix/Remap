extends Node
## Estado central da partida. Possui os sistemas de simulação (lógica pura) e faz a
## ponte entre eles: o tempo passa → necessidades, comida, trabalho, aluguel, multas.
## O nó do mundo (GameWorld) se registra em `world` para consultas espaciais.

const SAVE_VERSION := 2

var player: PlayerState
var world_state: WorldState
var quests: QuestSystem
var relations: Relations
var law: LawSystem
var job: DriveThruJob
var shops: ShopSystem
var property: PropertySystem
var delivery: DeliveryJob
var director: EventDirector
var flags: Dictionary = {}
var rng := RandomNumberGenerator.new()
var world: Node = null          # GameWorld ativo (ou null no menu)
var pending_load: Dictionary = {}  # dados carregados aguardando o mundo ser construído
var _last_day_processed := 0

func _ready() -> void:
	Clock.time_advanced.connect(_on_time_advanced)
	Clock.day_changed.connect(_on_day_changed)
	Clock.hour_changed.connect(_on_hour_changed)
	Events.inventory_changed.connect(func(owner_id): if owner_id == "player" and quests: quests.check_advances())
	new_game()

func new_game() -> void:
	player = PlayerState.new()
	world_state = WorldState.new()
	quests = QuestSystem.new()
	relations = Relations.new()
	law = LawSystem.new()
	job = DriveThruJob.new()
	shops = ShopSystem.new()
	property = PropertySystem.new()
	delivery = DeliveryJob.new()
	director = EventDirector.new()
	flags = {}
	rng.randomize()
	var start: Dictionary = Data.map().get("start", {})
	world_state.init_from_map(Data.map())
	shops.init_from_data()
	property.init_default()
	player.money = int(start.get("money", 60))
	var sp: Array = start.get("player_pos", [-44.5, -18.0])
	player.position = Vector3(float(sp[0]), 0.0, float(sp[1]))
	player.needs.v = {"fome": 25.0, "sede": 20.0, "sono": 8.0, "fadiga": 0.0, "higiene": 10.0}
	for item_id in start.get("player_items", {}):
		player.inventory.add(item_id, int(start.player_items[item_id]), 0.0, false)
	for item_id in start.get("player_equipped", []):
		player.equip(item_id)
	Clock.set_time(1, int(start.get("hour", 7)), int(start.get("minute", 30)))
	_last_day_processed = Clock.day()
	shops.last_restock_day = Clock.day()
	pending_load = {}

## "owner" no mapa = quem for dono/inquilino do prédio segundo o sistema de propriedades.
func is_mine(owner_id: String, building_id: String) -> bool:
	if owner_id == "player":
		return true
	return owner_id == "owner" and property.owns_or_rents(building_id)

## Temperatura do ar (°C): mais fresco de madrugada, quente à tarde; chuva e nuvens esfriam.
func ambient_temperature() -> float:
	var h := Clock.hour_f()
	var t := 23.0 + 5.5 * sin((h - 9.0) / 24.0 * TAU)
	match str(world_state.weather.get("state", "limpo")):
		"chuva": t -= 5.0
		"nublado": t -= 2.0
	return t

func environment() -> Dictionary:
	var outdoors := true
	if world and world.has_method("player_outdoors"):
		outdoors = world.call("player_outdoors")
	return {
		"ambient": ambient_temperature() if outdoors else 24.5,
		"outdoors": outdoors,
		"raining": world_state.weather.get("state", "") == "chuva",
		"warmth": player.warmth(),
		"rain_protect": player.rain_protected(),
	}

# ------------------------------------------------------------------ flags
func has_flag(f: String) -> bool:
	return flags.has(f)

func set_flag(f: String) -> void:
	if not flags.has(f):
		flags[f] = Clock.day()
		Events.flags_changed.emit()

func clear_flag(f: String) -> void:
	if flags.erase(f):
		Events.flags_changed.emit()

func after_rules_applied() -> void:
	quests.check_advances()

# ------------------------------------------------------------------ simulação
func _on_time_advanced(minutes: float, skipped: bool) -> void:
	if world == null:
		return
	var hours := minutes / 60.0
	var activity := player.activity
	player.needs.tick(hours, activity, player.skills.level("condicionamento"), environment())
	player.inventory.tick_age(hours)
	world_state.tick_age(hours)
	delivery.tick(minutes, Clock.total_minutes)
	director.tick(minutes)
	if job.on_shift:
		var in_area: bool = world.call("is_player_in_job_area") if world.has_method("is_player_in_job_area") else true
		var summary := job.tick(minutes, Clock.hour_f(), in_area)
		if not summary.is_empty():
			_on_shift_ended(summary)
	Events.needs_changed.emit()
	# Desmaio só durante o jogo em tempo real (não em saltos de tempo, como dormir).
	if player.needs.fainted:
		player.needs.fainted = false
		if not skipped and world.has_method("on_player_fainted"):
			world.call_deferred("on_player_fainted")

func _on_shift_ended(summary: Dictionary) -> void:
	player.inventory.remove_where(func(s): return Data.item(s.id).get("job_item", false))
	if int(summary.get("total", 0)) > 0:
		player.add_money(int(summary.total), "Turno no Brasa Burger")
	var why := ""
	match summary.get("reason", ""):
		"fechamento": why = tr("O drive-thru fechou. Turno encerrado.")
		"abandono": why = tr("Você saiu do trabalho no meio do turno.")
	if why != "":
		Events.notify.emit(why, "info")
	if world and world.has_method("on_shift_ended"):
		world.call("on_shift_ended", summary)

func clock_out_player() -> Dictionary:
	var summary := job.clock_out("normal")
	if not summary.is_empty():
		_on_shift_ended(summary)
	return summary

func _on_hour_changed(hour: int) -> void:
	if world == null:
		return
	if hour == 8:
		var fines := law.collect_fines()
		if not fines.is_empty():
			player.add_money(-int(fines.amount), "Multa")
			Events.request_message.emit(tr("Notificação da Guarda Municipal"),
				tr("Você recebeu uma multa de R$ %d por: %s.\nTestemunhas registraram a ocorrência.") % [int(fines.amount), ", ".join(fines.reasons)])

func _on_day_changed(day: int) -> void:
	if world == null or day <= _last_day_processed:
		return
	_last_day_processed = day
	shops.restock(day)
	delivery.new_day()
	world_state.daily_refill(rng)
	if Clock.weekday() == 6:
		for msg in property.charge_rent(player):
			Events.notify.emit(msg, "money")

# ------------------------------------------------------------------ crimes
## Registra um crime com testemunhas reais (NPCs próximos com linha de visão).
func report_crime(type: String, victim: String, victim_only: bool = false, pos = null) -> Dictionary:
	var witnesses: Array = []
	if victim_only:
		witnesses = [victim] if victim != "" else []
	elif world and world.has_method("find_witnesses"):
		var p: Vector3 = pos if pos != null else player.position
		witnesses = world.call("find_witnesses", p, 14.0)
	var entry := law.report(type, victim, witnesses, Clock.day(), relations)
	if witnesses.is_empty():
		Events.notify.emit(tr("Ninguém viu."), "info")
	else:
		var names: Array[String] = []
		for w in witnesses:
			names.append(tr(Data.npc(w).get("name", w)))
		Events.notify.emit(tr("%s viu o que você fez!") % ", ".join(names), "bad")
		if job.employed and witnesses.has(Data.job("brasa").get("employer", "")):
			job.fire(tr("Sandra viu você cometendo um crime."))
	return entry

# ------------------------------------------------------------------ veículos
func is_vehicle_near_spot(vehicle_id: String, spot: String, radius: float) -> bool:
	if world and world.has_method("is_vehicle_near_spot"):
		return world.call("is_vehicle_near_spot", vehicle_id, spot, radius)
	return false

func buy_vehicle(vehicle_id: String) -> bool:
	var v: Dictionary = world_state.vehicles.get(vehicle_id, {})
	if not v.has("for_sale"):
		return false
	var price := int(v.for_sale.price)
	if not player.spend(price, "Compra de veículo"):
		Events.notify.emit(tr("Dinheiro insuficiente (R$ %d).") % price, "bad")
		return false
	v.owner = "player"
	v.locked = false
	v.erase("for_sale")
	if world and world.has_method("refresh_vehicle"):
		world.call("refresh_vehicle", vehicle_id)
	Events.notify.emit(tr("Veículo comprado: %s. A chave é sua.") % tr(Data.vehicle(v.type).get("name", vehicle_id)), "good")
	return true

func vehicle_sale_value(vehicle_id: String) -> int:
	var v: Dictionary = world_state.vehicles.get(vehicle_id, {})
	return int(float(Data.vehicle(v.get("type", "")).get("price", 800)) * 0.5 * (1.0 - float(v.get("damage", 0.0)) / 200.0))

func sell_vehicle(vehicle_id: String) -> int:
	var v: Dictionary = world_state.vehicles.get(vehicle_id, {})
	if v.get("owner", "") != "player":
		return 0
	var value := vehicle_sale_value(vehicle_id)
	v.owner = "rafael"
	v.locked = true
	v.for_sale = {"price": int(float(Data.vehicle(v.type).get("price", 800)) * 0.8), "seller": "rafael"}
	player.add_money(value, "Venda de veículo")
	if world and world.has_method("refresh_vehicle"):
		world.call("refresh_vehicle", vehicle_id)
	return value

func repair_vehicle(vehicle_id: String, cost_per_point: int) -> void:
	var v: Dictionary = world_state.vehicles.get(vehicle_id, {})
	if v.is_empty():
		return
	var cost := int(ceil(float(v.damage) * cost_per_point))
	if cost <= 0:
		Events.notify.emit(tr("O carro não precisa de conserto."), "info")
		return
	if not player.spend(cost, "Conserto na oficina"):
		Events.notify.emit(tr("Dinheiro insuficiente para o conserto (R$ %d).") % cost, "bad")
		return
	v.damage = 0.0
	Clock.advance(60.0)
	if world and world.has_method("refresh_vehicle"):
		world.call("refresh_vehicle", vehicle_id)
	Events.notify.emit(tr("Carro consertado."), "good")

# ------------------------------------------------------------------ salvamento
func to_dict() -> Dictionary:
	if world and world.has_method("sync_to_state"):
		world.call("sync_to_state")
	return {
		"version": SAVE_VERSION,
		"clock": Clock.to_dict(),
		"player": player.to_dict(),
		"world": world_state.to_dict(),
		"quests": quests.to_dict(),
		"relations": relations.to_dict(),
		"law": law.to_dict(),
		"job": job.to_dict(),
		"shops": shops.to_dict(),
		"property": property.to_dict(),
		"delivery": delivery.to_dict(),
		"director": director.to_dict(),
		"flags": flags.duplicate(),
	}

func from_dict(d: Dictionary) -> void:
	new_game()
	Clock.from_dict(d.get("clock", {}))
	player.from_dict(d.get("player", {}))
	world_state.from_dict(d.get("world", {}))
	quests.from_dict(d.get("quests", {}))
	relations.from_dict(d.get("relations", {}))
	law.from_dict(d.get("law", {}))
	job.from_dict(d.get("job", {}))
	shops.from_dict(d.get("shops", {}))
	property.from_dict(d.get("property", {}))
	delivery.from_dict(d.get("delivery", {}))
	director.from_dict(d.get("director", {}))
	flags = d.get("flags", {}).duplicate()
	_last_day_processed = Clock.day()
