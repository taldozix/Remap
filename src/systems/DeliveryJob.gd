class_name DeliveryJob
extends RefCounted
## Trabalho temporário de entregador do Mercadinho (bico, sem turno fixo).
## Pega uma sacola no balcão, leva ao endereço antes do prazo e recebe na hora.
## Atrasos pagam menos; abandonar a entrega irrita o Seu Antônio.

var hired := false
var active: Dictionary = {}
var done_today := 0
var total := 0
var late_total := 0
var _last_dest := -1

func cfg() -> Dictionary:
	return Data.deliveries()

func hire() -> void:
	hired = true
	Events.notify.emit(tr("Agora você faz entregas para o Mercadinho. Pegue as sacolas no balcão."), "good")

func can_take() -> Dictionary:
	if not hired:
		return {"ok": false, "reason": tr("Fale com o Seu Antônio sobre entregas.")}
	if not active.is_empty():
		return {"ok": false, "reason": tr("Termine a entrega atual primeiro.")}
	if done_today >= int(cfg().get("max_per_day", 6)):
		return {"ok": false, "reason": tr("Sem mais entregas hoje.")}
	return {"ok": true}

func take(now: float, rng: RandomNumberGenerator, inv: Inventory) -> Dictionary:
	var c := can_take()
	if not c.ok:
		return c
	var dests: Array = cfg().get("destinations", [])
	var idx := rng.randi() % dests.size()
	if idx == _last_dest:
		idx = (idx + 1) % dests.size()
	_last_dest = idx
	var d: Dictionary = dests[idx]
	active = {"dest": idx, "spot": d.spot, "recipient": d.get("recipient", ""), "label": d.label,
		"deadline": now + float(cfg().get("deadline_minutes", 75)), "taken": now}
	inv.add("sacola_entrega", 1, 0.0, false)
	Events.notify.emit(tr("Entrega para %s. Prazo: %d minutos.") % [d.label, int(cfg().get("deadline_minutes", 75))], "quest")
	Events.quest_updated.emit("entrega")
	return {"ok": true, "delivery": active}

func complete(now: float, player: PlayerState) -> Dictionary:
	if active.is_empty():
		return {"ok": false, "reason": tr("Nenhuma entrega ativa.")}
	if not player.inventory.has("sacola_entrega"):
		return {"ok": false, "reason": tr("Você está sem a sacola da entrega.")}
	player.inventory.remove("sacola_entrega", 1)
	var on_time := now <= float(active.deadline)
	var pay := int(cfg().get("pay_base", 9))
	if on_time:
		pay += int(cfg().get("pay_on_time_tip", 5)) + player.skills.level("social")
	else:
		pay = int(pay * 0.6)
		late_total += 1
	player.add_money(pay, "Entrega do Mercadinho")
	player.skills.add_xp("social", 3)
	player.skills.add_xp("condicionamento", 2)
	var rec: String = active.get("recipient", "")
	if rec != "":
		Game.relations.change(rec, 2 if on_time else 0)
	done_today += 1
	total += 1
	var out := {"ok": true, "pay": pay, "on_time": on_time, "recipient": rec}
	active = {}
	Events.quest_updated.emit("entrega")
	return out

## Entregas esquecidas por muito tempo são canceladas (e a sacola volta ao mercadinho).
func tick(_minutes: float, now: float) -> void:
	if active.is_empty() or now <= float(active.deadline) + 120.0:
		return
	active = {}
	Game.player.inventory.remove("sacola_entrega", 1)
	Game.relations.change("antonio", -4)
	late_total += 1
	Events.notify.emit(tr("A entrega foi cancelada por atraso. Seu Antônio não gostou."), "bad")
	Events.quest_updated.emit("entrega")

func new_day() -> void:
	done_today = 0

func time_left(now: float) -> float:
	return float(active.get("deadline", now)) - now

func to_dict() -> Dictionary:
	return {"hired": hired, "active": active.duplicate(), "done_today": done_today, "total": total, "late": late_total}

func from_dict(d: Dictionary) -> void:
	hired = bool(d.get("hired", false))
	active = d.get("active", {}).duplicate()
	done_today = int(d.get("done_today", 0))
	total = int(d.get("total", 0))
	late_total = int(d.get("late", 0))
