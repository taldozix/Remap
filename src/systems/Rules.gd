class_name Rules
extends RefCounted
## Linguagem de condições e efeitos compartilhada por diálogos, missões e eventos.
## Condições: dicionário (todas as chaves precisam ser verdadeiras).
## Efeitos: lista de dicionários {operação: argumentos}.
## `ctx.npc` permite usar "self" para o NPC da conversa.

static func _t(text: String) -> String:
	return TranslationServer.translate(text)

static func _npc(id, ctx: Dictionary) -> String:
	return str(ctx.get("npc", "")) if str(id) == "self" else str(id)

static func check(cond: Dictionary, ctx: Dictionary = {}) -> bool:
	for key in cond:
		if not _check_one(key, cond[key], ctx):
			return false
	return true

static func _check_one(key: String, arg, ctx: Dictionary) -> bool:
	var p: PlayerState = Game.player
	match key:
		"all":
			for c in arg:
				if not check(c, ctx): return false
			return true
		"any":
			for c in arg:
				if check(c, ctx): return true
			return false
		"not":
			return not check(arg, ctx)
		"flag":
			return Game.has_flag(str(arg))
		"not_flag":
			return not Game.has_flag(str(arg))
		"quest_state":
			for q in arg:
				if Game.quests.state_of(q) != str(arg[q]): return false
			return true
		"quest_stage":
			for q in arg:
				if Game.quests.state_of(q) != "active" or Game.quests.stage_of(q) != str(arg[q]): return false
			return true
		"quest_outcome":
			for q in arg:
				if Game.quests.outcome_of(q) != str(arg[q]): return false
			return true
		"has_item":
			if typeof(arg) == TYPE_DICTIONARY:
				return p.inventory.has(str(arg.id), int(arg.get("qty", 1)))
			return p.inventory.has(str(arg))
		"not_has_item":
			return not p.inventory.has(str(arg))
		"money_gte":
			return p.money >= int(arg)
		"relation_gte":
			for n in arg:
				if Game.relations.get_rel(_npc(n, ctx)) < int(arg[n]): return false
			return true
		"relation_lt":
			for n in arg:
				if Game.relations.get_rel(_npc(n, ctx)) >= int(arg[n]): return false
			return true
		"reputation_gte":
			return Game.relations.reputation >= int(arg)
		"reputation_lt":
			return Game.relations.reputation < int(arg)
		"skill_gte":
			for s in arg:
				if p.skills.level(s) < int(arg[s]): return false
			return true
		"memory":
			return Game.relations.has_memory(_npc(arg.npc, ctx), str(arg.tag))
		"not_memory":
			return not Game.relations.has_memory(_npc(arg.npc, ctx), str(arg.tag))
		"need_gte":
			for n in arg:
				if p.needs.get_need(n) < float(arg[n]): return false
			return true
		"need_lt":
			for n in arg:
				if p.needs.get_need(n) >= float(arg[n]): return false
			return true
		"employed":
			return Game.job.employed == bool(arg)
		"fired":
			return Game.job.fired == bool(arg)
		"on_shift":
			return Game.job.on_shift == bool(arg)
		"job_rank_gte":
			return Game.job.rank >= int(arg)
		"hour_between":
			return Clock.hour_in(float(arg[0]), float(arg[1]))
		"container_has":
			var inv: Inventory = Game.world_state.container(str(arg.container))
			return inv != null and inv.has(str(arg.id))
		"not_container_has":
			var inv2: Inventory = Game.world_state.container(str(arg.container))
			return inv2 == null or not inv2.has(str(arg.id))
		"vehicle_damage_gte":
			var v: Dictionary = Game.world_state.vehicles.get(str(arg.vehicle), {})
			return float(v.get("damage", 0.0)) >= float(arg.value)
		"vehicle_near":
			# {"vehicle": id, "spot": spot_id, "radius": m}
			return Game.is_vehicle_near_spot(str(arg.vehicle), str(arg.spot), float(arg.get("radius", 12.0)))
		"crimes_gte":
			return Game.law.record.size() >= int(arg)
		"today":
			return int(Game.flags.get(str(arg), -1)) == Clock.day()
		"not_today":
			return int(Game.flags.get(str(arg), -1)) != Clock.day()
		"has_category":
			for s in p.inventory.items:
				if Data.item(s.id).get("category", "") == str(arg) and not p.inventory.is_spoiled(s):
					return true
			return false
	push_warning("Condição desconhecida: " + key)
	return false

## Probabilidade de um teste (persuasão, intimidação...). Mostrada ao jogador antes de escolher.
static func chance(spec: Dictionary, ctx: Dictionary = {}) -> float:
	var c := float(spec.get("base", 0.5))
	if spec.has("skill"):
		c += Game.player.skills.level(str(spec.skill)) * float(spec.get("per_level", 0.1))
	if spec.has("relation"):
		c += Game.relations.get_rel(_npc(spec.relation, ctx)) * float(spec.get("per_relation", 0.004))
	if spec.has("per_reputation"):
		c += Game.relations.reputation * float(spec.per_reputation)
	return clampf(c, 0.05, 0.95)

static func apply(effects: Array, ctx: Dictionary = {}) -> void:
	for e in effects:
		for op in e:
			_apply_one(op, e[op], ctx)
	Game.after_rules_applied()

static func _apply_one(op: String, arg, ctx: Dictionary) -> void:
	var p: PlayerState = Game.player
	match op:
		"set_flag":
			Game.set_flag(str(arg))
		"clear_flag":
			Game.clear_flag(str(arg))
		"give_item":
			var qty := int(arg.get("qty", 1))
			var added := p.inventory.add(str(arg.id), qty, 0.0, false)
			if added > 0:
				Events.notify.emit(_t("Recebido: %s x%d") % [Data.item_name(str(arg.id)), added], "good")
		"take_item":
			var removed := p.inventory.remove(str(arg.id), int(arg.get("qty", 1)))
			if removed > 0:
				Events.notify.emit(_t("Entregue: %s x%d") % [Data.item_name(str(arg.id)), removed], "info")
		"container_take":
			var inv: Inventory = Game.world_state.container(str(arg.container))
			if inv:
				inv.remove(str(arg.id), int(arg.get("qty", 1)))
		"money":
			p.add_money(int(arg), str(ctx.get("reason", "Pagamento")))
		"set_today":
			Game.flags[str(arg)] = Clock.day()
			Events.flags_changed.emit()
		"take_category":
			for s in p.inventory.items:
				if Data.item(s.id).get("category", "") == str(arg) and not p.inventory.is_spoiled(s):
					var item_id: String = s.id
					p.inventory.remove(item_id, 1)
					Events.notify.emit(_t("Entregue: %s") % Data.item_name(item_id), "info")
					break
		"relation":
			Game.relations.change(_npc(arg.npc, ctx), int(arg.delta))
		"reputation":
			Game.relations.change_reputation(int(arg))
		"memory":
			Game.relations.add_memory(_npc(arg.npc, ctx), str(arg.tag), Clock.day())
		"quest_start":
			Game.quests.start(str(arg))
		"quest_stage":
			Game.quests.set_stage(str(arg[0]), str(arg[1]))
		"quest_complete":
			Game.quests.complete(str(arg[0]), str(arg[1]))
		"skill_xp":
			for s in arg:
				p.skills.add_xp(s, float(arg[s]))
		"job_hire":
			Game.job.hire()
		"notify":
			Events.notify.emit(_t(str(arg)), "info")
		"open_shop":
			Events.request_shop.emit(str(arg))
		"crime":
			Game.report_crime(str(arg.type), _npc(arg.get("victim", "self"), ctx), bool(arg.get("victim_only", true)))
		"needs":
			p.needs.consume(arg)
			Events.needs_changed.emit()
		"repair_vehicle":
			Game.repair_vehicle(str(arg.vehicle), int(arg.get("cost_per_point", 3)))
		"advance_time":
			Clock.advance(float(arg))
		_:
			push_warning("Efeito desconhecido: " + op)
