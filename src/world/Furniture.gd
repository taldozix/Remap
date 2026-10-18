class_name Furniture
extends Interactable
## Móveis e equipamentos com função real: camas, contêineres, fogão, pia, chuveiro,
## assentos, balcões de loja, estações do drive-thru, relógio de ponto, bomba de
## combustível e o tapete do Marcos. O visual é gerado pelo WorldBuilder.

var kind: String = ""
var def: Dictionary = {}
var item_id: String = ""      # id único (cama_jogador, estacao_chapa...)
var owner_id: String = ""
var container_id: String = ""
var facing := 0.0             # graus (direção da frente do móvel)
var seat_height := 0.45
var status_label: Label3D

const TITLES := {
	"bed": "Cama", "wardrobe": "Guarda-roupa", "fridge": "Geladeira", "chest": "Baú", "crate": "Caixas",
	"trash": "Lixeira", "stove": "Fogão", "sink": "Pia", "shower": "Chuveiro", "sofa": "Sofá",
	"chair": "Cadeira", "bench": "Banco", "note": "Bilhete", "counter": "Balcão", "grill": "Chapa",
	"fryer": "Fritadeira", "soda_machine": "Máquina de refrigerante", "drive_window": "Janela do drive-thru",
	"time_clock": "Relógio de ponto", "pump": "Bomba de combustível", "doormat": "Tapete",
}

func setup(p_def: Dictionary, p_world: Node, p_building: String) -> void:
	def = p_def
	kind = p_def.type
	world = p_world
	building_id = p_building
	item_id = str(p_def.get("id", ""))
	owner_id = str(p_def.get("owner", ""))
	facing = float(p_def.get("rot", 0))
	if p_def.has("container"):
		container_id = p_def.container.id
		owner_id = str(p_def.container.get("owner", ""))
		title = tr(p_def.container.get("name", TITLES.get(kind, kind)))
	else:
		title = tr(TITLES.get(kind, kind))
	if kind == "counter":
		title = tr(Data.shop(p_def.get("shop", "")).get("name", "Balcão"))
	if kind == "bench":
		seat_height = 0.45
	elif kind == "sofa":
		seat_height = 0.42
	radius = 1.9 if kind in ["counter", "drive_window", "pump", "bed"] else 1.6
	if kind in ["grill", "fryer"]:
		status_label = Label3D.new()
		status_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		status_label.position = Vector3(0, 1.6, 0)
		status_label.font_size = 40
		status_label.pixel_size = 0.006
		status_label.outline_size = 10
		status_label.no_depth_test = true
		add_child(status_label)

func forward() -> Vector3:
	return Basis(Vector3.UP, deg_to_rad(facing)) * Vector3(0, 0, -1)

## Ponto de uso: um pouco à frente do móvel.
func interact_point() -> Vector3:
	var off := 0.5
	if kind in ["counter"]:
		off = 0.75
	if kind in ["doormat", "pump", "trash", "bench", "sofa", "chair", "note", "bed", "drive_window"]:
		off = 0.0
	return global_position + forward() * off

func seat_transform() -> Transform3D:
	var t := Transform3D(Basis(Vector3.UP, deg_to_rad(facing)), global_position + forward() * 0.12)
	t.origin.y = 0.0
	return t

func _process(_delta: float) -> void:
	if status_label == null:
		return
	var st: Dictionary = Game.job.stations.get(def.get("station", ""), {})
	var s: String = st.get("state", "idle")
	if not Game.job.on_shift or s == "idle":
		status_label.text = ""
		return
	match s:
		"cooking":
			status_label.text = "%d%%" % int(Game.job.station_progress(def.station) * 100)
			status_label.modulate = Color(1, 0.85, 0.3)
		"ready":
			status_label.text = tr("PRONTO!")
			status_label.modulate = Color(0.4, 1, 0.4)
		"burnt":
			status_label.text = tr("QUEIMOU")
			status_label.modulate = Color(1, 0.3, 0.3)

func _is_mine() -> bool:
	return owner_id == "player" or owner_id == ""

func _job_actions_allowed() -> bool:
	return Game.job.on_shift

func get_actions(player: Node) -> Array:
	var out: Array = []
	match kind:
		"bed":
			if owner_id == "player":
				out.append(action("sleep", tr("Dormir...")))
				out.append(action("lie", tr("Deitar e descansar")))
			else:
				out.append(action("none", tr("Dormir"), false, tr("Não é a sua cama")))
		"wardrobe", "fridge", "chest", "crate":
			if container_id != "":
				var label := tr("Abrir %s") % title
				if owner_id != "player" and owner_id != "":
					label += " " + tr("(pertence a %s)") % tr(Data.npc(owner_id).get("name", owner_id))
				out.append(action("open", label))
		"trash":
			out.append(action("open", tr("Vasculhar a lixeira")))
		"stove":
			var can_cook := player_has_cookable()
			out.append(action("cook", tr("Preparar macarrão (precisa de água)"), can_cook != "", tr("Você precisa de macarrão instantâneo e água")))
		"sink":
			out.append(action("drink", tr("Beber água da torneira")))
			out.append(action("wash", tr("Lavar o rosto e as mãos")))
		"shower":
			out.append(action("shower", tr("Tomar banho"), _is_mine(), tr("Não é o seu banheiro")))
		"sofa", "chair", "bench":
			out.append(action("sit", tr("Sentar")))
		"note":
			out.append(action("read", tr("Ler bilhete")))
		"counter":
			var reason: String = world.shop_unavailable_reason(def.get("shop", ""), self)
			out.append(action("shop", tr("Comprar / vender"), reason == "", reason))
		"grill", "fryer":
			out += _station_actions()
		"soda_machine":
			out.append(action("soda", tr("Servir refrigerante no copo"), _job_actions_allowed(), tr("Só durante o turno")))
		"drive_window":
			var o: Dictionary = Game.job.front_order()
			if not Game.job.on_shift:
				out.append(action("none", tr("Janela do drive-thru"), false, tr("Só durante o turno")))
			elif o.is_empty():
				out.append(action("none", tr("Nenhum cliente na janela"), false))
			else:
				out.append(action("deliver", tr("Entregar pedido #%d (falta: %s)") % [o.id, DriveThruJob.order_text(o.remaining)]))
		"time_clock":
			if Game.job.on_shift:
				out.append(action("clock_out", tr("Bater ponto (saída)")))
			else:
				var ok := Game.job.employed
				out.append(action("clock_in", tr("Bater ponto (entrada)"), ok, tr("Você não trabalha aqui") if not Game.job.fired else tr("Você foi demitido(a)")))
		"pump":
			var v: Node = world.player_vehicle_near(global_position, 6.0)
			var price := 6
			if v == null:
				out.append(action("none", tr("Abastecer (R$ %d/L)") % price, false, tr("Estacione o seu carro ao lado da bomba")))
			else:
				var need: float = v.fuel_capacity() - v.fuel
				out.append(action("fuel", tr("Abastecer %s (R$ %d/L, faltam %.0f L)") % [v.display_name(), price, need], need > 0.5))
		"doormat":
			out.append(action("search", tr("Olhar debaixo do tapete")))
	return out

func _station_actions() -> Array:
	var out: Array = []
	if not Game.job.on_shift:
		return [action("none", title, false, tr("Só durante o turno"))]
	var s: String = def.station
	var st: Dictionary = Game.job.stations.get(s, {})
	var product := Data.item_name(Data.job("brasa").stations[s].product)
	match st.get("state", "idle"):
		"idle": out.append(action("start", tr("Começar a preparar: %s") % product))
		"cooking": out.append(action("none", tr("Preparando... %d%%") % int(Game.job.station_progress(s) * 100), false))
		"ready": out.append(action("collect", tr("Retirar %s") % product))
		"burnt": out.append(action("collect", tr("Jogar fora o que queimou")))
	return out

func player_has_cookable() -> String:
	var inv: Inventory = Game.player.inventory
	for s in inv.items:
		var cook: Dictionary = Data.item(s.id).get("cook", {})
		if cook.is_empty():
			continue
		var ok := true
		for need in cook.get("needs", {}):
			if not inv.has(need, int(cook.needs[need])):
				ok = false
		if ok:
			return s.id
	return ""

func _job_time(key: String) -> float:
	var base := float(Data.job("brasa").get("actions", {}).get(key, 1.5))
	var skill := Game.player.skills.level("culinaria")
	return base * Game.player.needs.work_multiplier() * (1.0 - 0.05 * skill)

func do_action(player: Node, id: String) -> void:
	match id:
		"sleep":
			Events.request_sleep.emit(item_id, 1.0)
		"lie":
			player.rest_on_bed(self)
		"open":
			if kind == "trash" and not Game.world_state.searched.has(container_id):
				Game.world_state.searched[container_id] = Clock.day()
			Events.request_container.emit(container_id, title)
		"cook":
			var item := player_has_cookable()
			if item != "":
				player.start_timed(tr("Cozinhando..."), 4.0, _cook.bind(item), "sizzle")
		"drink":
			player.start_timed(tr("Bebendo água..."), 1.5, _consume_fx.bind({"sede": -25}, "drink"))
		"wash":
			player.start_timed(tr("Lavando o rosto..."), 2.0, _consume_fx.bind({"higiene": -12}, ""))
		"shower":
			player.start_timed(tr("Tomando banho..."), 5.0, _shower)
		"sit":
			player.sit_on(self)
		"read":
			var note: Dictionary = Data.map().get("notes", {}).get(def.get("note", ""), {})
			Events.request_message.emit(tr(note.get("title", "Bilhete")), tr(note.get("body", "")))
		"shop":
			Events.request_shop.emit(def.get("shop", ""))
		"start":
			player.start_timed(tr("Preparando..."), _job_time(def.station + "_start"), _station_start, "sizzle")
		"collect":
			player.start_timed(tr("Retirando..."), _job_time(def.station + "_collect"), _station_collect)
		"soda":
			player.start_timed(tr("Servindo refrigerante..."), _job_time("refri"), _soda)
		"deliver":
			player.start_timed(tr("Entregando..."), _job_time("deliver"), world.deliver_drive_order)
		"clock_in":
			var r := Game.job.clock_in(Clock.hour_f(), Game.player.needs)
			if not r.ok:
				Events.notify.emit(r.reason, "bad")
			else:
				Audio.play("order")
		"clock_out":
			var sum := Game.clock_out_player()
			if not sum.is_empty():
				world.show_shift_summary(sum)
		"fuel":
			var v: Node = world.player_vehicle_near(global_position, 6.0)
			if v != null:
				player.start_timed(tr("Abastecendo..."), 3.0, _fuel.bind(v))
		"search":
			var hidden: String = def.get("hidden_item", "")
			if hidden != "" and not Game.world_state.searched.has(item_id):
				Game.world_state.searched[item_id] = Clock.day()
				Game.player.inventory.add(hidden, 1, 0.0, false)
				Events.notify.emit(tr("Você encontrou: %s") % Data.item_name(hidden), "good")
				Audio.play("pickup")
			else:
				Events.notify.emit(tr("Nada além de poeira."), "info")

func _cook(item: String) -> void:
	var inv: Inventory = Game.player.inventory
	var cook: Dictionary = Data.item(item).get("cook", {})
	if not inv.has(item) or cook.is_empty():
		return
	for need in cook.get("needs", {}):
		if not inv.has(need, int(cook.needs[need])):
			return
	inv.remove(item, 1)
	for need in cook.get("needs", {}):
		inv.remove(need, int(cook.needs[need]))
	inv.add(cook.result, 1, 0.0, false)
	Clock.advance(float(cook.get("minutes", 10)))
	Game.player.skills.add_xp("culinaria", 8)
	Events.notify.emit(tr("Pronto: %s") % Data.item_name(cook.result), "good")

func _consume_fx(effects: Dictionary, sound: String) -> void:
	Game.player.needs.consume(effects)
	Events.needs_changed.emit()
	if sound != "":
		Audio.play(sound)

func _shower() -> void:
	Game.player.needs.set_need("higiene", 0.0)
	Game.player.needs.consume({"fadiga": -10})
	Clock.advance(15.0)
	Events.notify.emit(tr("Banho tomado. Que alívio!"), "good")

func _station_start() -> void:
	if Game.job.station_start(def.station, Game.player.skills.level("culinaria")):
		Audio.play_at("sizzle", global_position, -6.0)

func _station_collect() -> void:
	var r := Game.job.station_collect(def.station, Game.player.inventory)
	if r == "burnt":
		Events.notify.emit(tr("Descartado. Comece de novo."), "bad")
	elif r != "":
		Game.player.skills.add_xp("culinaria", 6)
		Audio.play("pickup")

func _soda() -> void:
	if Game.job.on_shift:
		Game.player.inventory.add("job_refri", 1, 0.0, false)
		Audio.play("pickup")

func _fuel(v: Node) -> void:
	var price := 6.0
	var need: float = v.fuel_capacity() - v.fuel
	var liters: float = min(need, floor(float(Game.player.money) / price))
	if liters < 0.5:
		Events.notify.emit(tr("Dinheiro insuficiente."), "bad")
		return
	var cost := int(ceil(liters * price))
	if Game.player.spend(cost, "Combustível"):
		v.fuel += liters
		v.save_state()
		Events.notify.emit(tr("Abastecido: %.1f L") % liters, "good")
