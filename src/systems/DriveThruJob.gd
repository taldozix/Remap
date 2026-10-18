class_name DriveThruJob
extends RefCounted
## Emprego no drive-thru (lógica pura). O mundo fornece clientes (carros na fila)
## e as estações físicas (chapa, fritadeira, máquina de refrigerante, janela).
## Tempos em minutos de jogo (com a escala padrão, 1 minuto de jogo = 1 segundo real).

const JOB_ID := "brasa"

var employed := false
var fired := false
var rank := 0
var performance := 50.0
var warnings := 0
var total_orders := 0
var shifts_done := 0

var on_shift := false
var shift: Dictionary = {}
var orders: Array = []      # pedidos ativos
var finished: Dictionary = {} # customer_id -> "done" | "failed" (o carro lê e vai embora)
var stations: Dictionary = {}
var next_order_id := 1
var customer_cooldown := 0.0
var customers_requested := 0  # clientes a caminho (ainda sem pedido)

func _init() -> void:
	reset_stations()

func cfg() -> Dictionary:
	return Data.job(JOB_ID)

func rank_title(r: int = -1) -> String:
	var ranks: Array = cfg().get("ranks", [])
	var i := rank if r < 0 else r
	return tr(ranks[clampi(i, 0, ranks.size() - 1)].title) if not ranks.is_empty() else ""

func wage() -> int:
	var ranks: Array = cfg().get("ranks", [])
	return int(ranks[clampi(rank, 0, ranks.size() - 1)].wage) if not ranks.is_empty() else 0

func is_open(hour_f: float) -> bool:
	var h: Array = cfg().get("hours", [10, 22])
	return Clock.is_hour_in(hour_f, float(h[0]), float(h[1]))

func reset_stations() -> void:
	stations = {}
	for s in cfg().get("stations", {}):
		stations[s] = {"state": "idle", "t": 0.0}

func hire() -> void:
	if fired:
		return
	employed = true
	Events.notify.emit(tr("Você foi contratado(a) como %s! Salário: R$ %d/hora.") % [rank_title(), wage()], "good")
	Events.job_updated.emit()

func clock_in(hour_f: float, needs: Needs) -> Dictionary:
	if not employed:
		return {"ok": false, "reason": tr("Você não trabalha aqui.")}
	if on_shift:
		return {"ok": false, "reason": tr("Você já está em turno.")}
	if not is_open(hour_f):
		return {"ok": false, "reason": tr("O drive-thru está fechado agora (10h às 22h).")}
	if needs.get_need("higiene") >= 75.0:
		return {"ok": false, "reason": tr("Sandra: \"Assim não dá. Vai tomar um banho e volta.\"")}
	on_shift = true
	shift = {"minutes": 0.0, "ok": 0, "failed": 0, "partial": 0, "tips": 0, "bonus": 0, "away": 0.0}
	orders.clear()
	finished.clear()
	customers_requested = 0
	customer_cooldown = 6.0
	reset_stations()
	Events.notify.emit(tr("Ponto batido. Bom trabalho!"), "info")
	Events.job_updated.emit()
	return {"ok": true}

## Encerra o turno e calcula o pagamento. Abandono paga metade e gera advertência.
func clock_out(reason: String = "normal") -> Dictionary:
	if not on_shift:
		return {}
	on_shift = false
	var hours := float(shift.minutes) / 60.0
	var wage_pay := int(round((hours + float(shift.get("extra", 0.0)) / 60.0) * wage()))
	if reason == "abandono":
		wage_pay = int(wage_pay * 0.5)
		add_warning(tr("abandonou o turno"))
	var total := wage_pay + int(shift.bonus) + int(shift.tips)
	if hours >= 1.0:
		shifts_done += 1
	var summary := {
		"reason": reason, "hours": hours, "wage": wage_pay, "bonus": int(shift.bonus),
		"tips": int(shift.tips), "total": total, "ok": int(shift.ok), "failed": int(shift.failed),
		"promoted": false,
	}
	orders.clear()
	customers_requested = 0
	reset_stations()
	if _check_promotion():
		summary.promoted = true
	Events.job_updated.emit()
	return summary

func add_warning(why: String) -> void:
	warnings += 1
	Events.notify.emit(tr("Advertência no trabalho (%d/%d): %s") % [warnings, int(cfg().get("max_warnings", 3)), why], "bad")
	if warnings >= int(cfg().get("max_warnings", 3)):
		fire(tr("Advertências demais."))

func fire(why: String) -> void:
	if not employed:
		return
	if on_shift:
		on_shift = false
		orders.clear()
	employed = false
	fired = true
	Events.notify.emit(tr("Você foi demitido(a) do Brasa Burger. %s") % why, "bad")
	Events.job_updated.emit()

func _check_promotion() -> bool:
	var ranks: Array = cfg().get("ranks", [])
	if rank + 1 >= ranks.size():
		return false
	var nxt: Dictionary = ranks[rank + 1]
	if performance >= float(nxt.get("min_perf", 999)) and total_orders >= int(nxt.get("min_orders", 999)):
		rank += 1
		Events.notify.emit(tr("Promoção! Agora você é %s (R$ %d/hora).") % [rank_title(), wage()], "good")
		return true
	return false

## Avança o turno: cozimento, paciência dos clientes, abandono e fechamento.
## Retorna o resumo do turno se ele terminou automaticamente.
func tick(minutes: float, hour_f: float, player_in_area: bool) -> Dictionary:
	if not on_shift:
		return {}
	if not is_open(hour_f):
		return clock_out("fechamento")
	shift.minutes = float(shift.minutes) + minutes
	if Game.director and Game.director.wage_multiplier() > 1.0:
		shift.extra = float(shift.get("extra", 0.0)) + minutes * (Game.director.wage_multiplier() - 1.0)
	for s in stations:
		var st: Dictionary = stations[s]
		if st.state == "cooking" or st.state == "ready":
			st.t = float(st.t) + minutes
			var c: Dictionary = cfg().stations[s]
			if st.state == "cooking" and st.t >= float(st.cook):
				st.state = "ready"
			if st.state == "ready" and st.t >= float(st.cook) + float(st.window):
				st.state = "burnt"
				Events.notify.emit(tr("%s queimou!") % tr(c.get("label", s)), "bad")
	for o in orders.duplicate():
		o.patience = float(o.patience) - minutes
		if o.patience <= 0.0:
			_fail_order(o)
	customer_cooldown -= minutes
	if player_in_area:
		shift.away = 0.0
	else:
		shift.away = float(shift.away) + minutes
		if shift.away >= float(cfg().get("abandon_minutes", 15)):
			return clock_out("abandono")
	return {}

## O mundo pergunta se deve mandar mais um carro para a fila.
func wants_customer() -> bool:
	return on_shift and customer_cooldown <= 0.0 and orders.size() + customers_requested < int(cfg().get("max_queue", 3))

func customer_dispatched(rng: RandomNumberGenerator) -> void:
	customers_requested += 1
	var iv: Array = cfg().get("customer_interval", [30, 55])
	customer_cooldown = rng.randf_range(float(iv[0]), float(iv[1]))

## Cliente chegou à janela: cria o pedido.
func create_order(customer_id: String, rng: RandomNumberGenerator) -> Dictionary:
	customers_requested = max(0, customers_requested - 1)
	var menu: Array = cfg().get("menu", ["job_x_brasa", "job_batata", "job_refri"])
	var items := {}
	var n := rng.randi_range(1, 3)
	for i in n:
		var it: String = menu[rng.randi() % menu.size()]
		items[it] = int(items.get(it, 0)) + 1
	var patience := float(cfg().get("patience", 80))
	var o := {"id": next_order_id, "customer": customer_id, "items": items, "remaining": items.duplicate(), "patience": patience, "max_patience": patience}
	next_order_id += 1
	orders.append(o)
	Events.notify.emit(tr("Novo pedido #%d na janela: %s") % [o.id, order_text(items)], "quest")
	Events.job_updated.emit()
	return o

static func order_text(items: Dictionary) -> String:
	var parts: Array[String] = []
	for k in items:
		parts.append("%dx %s" % [int(items[k]), Data.item_name(k)])
	return ", ".join(parts)

func order_for(customer_id: String) -> Dictionary:
	for o in orders:
		if o.customer == customer_id:
			return o
	return {}

func front_order() -> Dictionary:
	return orders[0] if not orders.is_empty() else {}

func status_of(customer_id: String) -> String:
	if finished.has(customer_id):
		return finished[customer_id]
	return "waiting" if not order_for(customer_id).is_empty() else "none"

# --------------------------------------------------------------- estações
func station_cook_time(s: String, cooking_lvl: int) -> float:
	var c: Dictionary = cfg().stations[s]
	return max(2.0, float(c.cook) + float(c.get("cook_per_level", 0.0)) * cooking_lvl)

func station_window(s: String, cooking_lvl: int) -> float:
	var c: Dictionary = cfg().stations[s]
	return float(c.window) + float(c.get("window_per_level", 0.0)) * cooking_lvl

func station_start(s: String, cooking_lvl: int) -> bool:
	if not on_shift or not stations.has(s) or stations[s].state != "idle":
		return false
	stations[s] = {"state": "cooking", "t": 0.0, "cook": station_cook_time(s, cooking_lvl), "window": station_window(s, cooking_lvl)}
	return true

## Retira o produto pronto (vai para o inventário do jogador). Queimado é descartado.
func station_collect(s: String, inv: Inventory) -> String:
	if not stations.has(s):
		return ""
	var st: Dictionary = stations[s]
	if st.state == "ready":
		var product: String = cfg().stations[s].product
		inv.add(product, 1, 0.0, false)
		stations[s] = {"state": "idle", "t": 0.0}
		return product
	if st.state == "burnt":
		stations[s] = {"state": "idle", "t": 0.0}
		performance = max(0.0, performance - 1.0)
		return "burnt"
	return ""

func station_progress(s: String) -> float:
	var st: Dictionary = stations.get(s, {})
	if st.get("state", "idle") == "cooking":
		return clampf(float(st.t) / float(st.cook), 0.0, 1.0)
	return 1.0 if st.get("state", "") in ["ready", "burnt"] else 0.0

# --------------------------------------------------------------- entrega
## Entrega à janela o que o jogador tiver do pedido da frente.
func deliver(inv: Inventory, social_lvl: int) -> Dictionary:
	var o := front_order()
	if o.is_empty():
		return {"status": "none"}
	var given := {}
	for k in o.remaining.keys():
		var need := int(o.remaining[k])
		var got := inv.remove(k, need)
		if got > 0:
			given[k] = got
			o.remaining[k] = need - got
			if int(o.remaining[k]) <= 0:
				o.remaining.erase(k)
	if given.is_empty():
		return {"status": "nothing", "order": o}
	if not o.remaining.is_empty():
		shift.partial = int(shift.partial) + 1
		performance = max(0.0, performance - 1.0)
		return {"status": "partial", "order": o, "given": given}
	# Pedido completo: bônus, gorjeta conforme rapidez e habilidade social.
	var speed := float(o.patience) / float(o.max_patience)
	var tip := 0
	if speed >= 0.6:
		tip = 4 + social_lvl
	elif speed >= 0.3:
		tip = 2 + int(social_lvl / 2)
	shift.ok = int(shift.ok) + 1
	shift.bonus = int(shift.bonus) + int(cfg().get("order_bonus", 2))
	shift.tips = int(shift.tips) + tip
	total_orders += 1
	performance = clampf(performance + (3.0 if speed >= 0.3 else 1.0), 0.0, 100.0)
	orders.erase(o)
	finished[o.customer] = "done"
	Events.job_updated.emit()
	return {"status": "complete", "order": o, "tip": tip}

func _fail_order(o: Dictionary) -> void:
	orders.erase(o)
	finished[o.customer] = "failed"
	shift.failed = int(shift.failed) + 1
	performance = max(0.0, performance - 6.0)
	Events.notify.emit(tr("O cliente do pedido #%d desistiu e foi embora.") % o.id, "bad")
	if performance < 15.0:
		fire(tr("Desempenho muito baixo."))
	Events.job_updated.emit()

func customer_left(customer_id: String) -> void:
	finished.erase(customer_id)

func to_dict() -> Dictionary:
	return {
		"employed": employed, "fired": fired, "rank": rank, "performance": performance,
		"warnings": warnings, "total_orders": total_orders, "shifts_done": shifts_done,
	}

## Turnos não são salvos no meio: ao carregar, o jogador está fora de turno.
func from_dict(d: Dictionary) -> void:
	employed = bool(d.get("employed", false))
	fired = bool(d.get("fired", false))
	rank = int(d.get("rank", 0))
	performance = float(d.get("performance", 50.0))
	warnings = int(d.get("warnings", 0))
	total_orders = int(d.get("total_orders", 0))
	shifts_done = int(d.get("shifts_done", 0))
	on_shift = false
	orders.clear()
	finished.clear()
	reset_stations()
