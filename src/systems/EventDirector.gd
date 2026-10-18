class_name EventDirector
extends RefCounted
## Eventos dinâmicos controlados por regras (data/events.json): janela de horário,
## chance por hora, intervalo mínimo em dias, clima e condições da linguagem Rules.
## Eventos que precisam do mundo (carteira, ferido, discussão) são criados pelo
## GameWorld; os de economia (promoção, turno extra) funcionam só com dados.

var last_fired: Dictionary = {}   # id -> dia
var active: Dictionary = {}       # id -> {"until": minutos, ...}
var rng := RandomNumberGenerator.new()
var auto_enabled := true          # testes desligam o sorteio automático
var _acc := 0.0

func _init() -> void:
	rng.randomize()

func tick(minutes: float) -> void:
	for id in active.keys():
		if Clock.total_minutes >= float(active[id].get("until", 0.0)):
			_end(id)
	_acc += minutes
	while _acc >= 60.0:
		_acc -= 60.0
		evaluate_hour()

func evaluate_hour() -> void:
	if not auto_enabled:
		return
	for id in Data.events():
		if can_fire(id) and rng.randf() < float(Data.events()[id].get("chance", 0.1)):
			fire(id)

func can_fire(id: String) -> bool:
	var e: Dictionary = Data.events().get(id, {})
	if e.is_empty() or active.has(id):
		return false
	var h: Array = e.get("hours", [0, 24])
	if not Clock.hour_in(float(h[0]), float(h[1])):
		return false
	if last_fired.has(id) and Clock.day() - int(last_fired[id]) < int(e.get("cooldown_days", 1)):
		return false
	if e.get("no_rain", false) and Game.world_state.weather.get("state", "") == "chuva":
		return false
	return Rules.check(e.get("if", {}), {})

## Dispara um evento (também usado por testes e ferramentas de depuração).
func fire(id: String) -> bool:
	var e: Dictionary = Data.events().get(id, {})
	if e.is_empty():
		return false
	var info := {"until": Clock.total_minutes + float(e.get("duration", 0.0))}
	match id:
		"promocao_mercadinho":
			info.discount = float(e.get("discount", 0.25))
			Events.notify.emit(tr(e.notify), "money")
		"turno_extra":
			var until := (Clock.day() - 1) * 1440.0 + float(e.get("until_hour", 18)) * 60.0
			info.until = until
			info.multiplier = float(e.get("multiplier", 1.5))
			Events.request_message.emit(tr(e.title), tr(e.notify))
		_:
			if Game.world == null or not Game.world.has_method("start_event"):
				return false
			var r: Dictionary = Game.world.call("start_event", id, e)
			if r.is_empty():
				return false
			info.merge(r)
	last_fired[id] = Clock.day()
	if float(info.until) > Clock.total_minutes:
		active[id] = info
	Events.situation_started.emit(id)
	return true

func _end(id: String) -> void:
	var info: Dictionary = active.get(id, {})
	active.erase(id)
	if Game.world and Game.world.has_method("end_event"):
		Game.world.call("end_event", id, info)

func is_active(id: String) -> bool:
	return active.has(id)

func shop_discount(shop_id: String) -> float:
	if shop_id == "mercadinho" and active.has("promocao_mercadinho"):
		return float(active.promocao_mercadinho.get("discount", 0.0))
	return 0.0

func wage_multiplier() -> float:
	return float(active.get("turno_extra", {}).get("multiplier", 1.0))

func to_dict() -> Dictionary:
	var keep := {}
	for id in active:
		if id in ["promocao_mercadinho", "turno_extra"]:
			keep[id] = active[id]
	return {"last_fired": last_fired.duplicate(), "active": keep}

## Situações espaciais (carteira, ferido, discussão) não são salvas no meio; só o
## histórico de disparos — o mundo recomeça coerente ao carregar.
func from_dict(d: Dictionary) -> void:
	last_fired = d.get("last_fired", {}).duplicate()
	active = d.get("active", {}).duplicate(true)
