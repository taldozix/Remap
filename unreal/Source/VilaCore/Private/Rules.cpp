#include "Vila/Rules.h"

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"

namespace vila {

namespace {
// arg[i] do GDScript, sem estourar se não for array.
const Json& at(const Json& a, size_t i) {
	if (!a.is_array() || i >= a.size()) return empty_json();
	return a[i];
}
// Iteração `for x in arg` sobre um array (vazio se não for array).
const Json& as_array(const Json& a) {
	static const Json e = Json::array();
	return a.is_array() ? a : e;
}
// Iteração `for k in arg` sobre um dicionário (vazio se não for objeto).
const Json& as_object(const Json& a) {
	static const Json e = Json::object();
	return a.is_object() ? a : e;
}
}  // namespace

std::string Rules::t(const std::string& text) { return tr(text); }

std::string Rules::npc(const Json& id, const Json& ctx) {
	return to_str(id) == "self" ? jstr(ctx, "npc", "") : to_str(id);
}

bool Rules::evaluate(const Json& cond, const Json& ctx) {
	for (const auto& [key, val] : as_object(cond).items()) {
		if (!check_one(key, val, ctx)) return false;
	}
	return true;
}

bool Rules::check_one(const std::string& key, const Json& arg, const Json& ctx) {
	PlayerState& p = game().player;
	if (key == "all") {
		for (const auto& c : as_array(arg))
			if (!evaluate(c, ctx)) return false;
		return true;
	}
	if (key == "any") {
		for (const auto& c : as_array(arg))
			if (evaluate(c, ctx)) return true;
		return false;
	}
	if (key == "not") return !evaluate(arg, ctx);
	if (key == "flag") return game().has_flag(to_str(arg));
	if (key == "not_flag") return !game().has_flag(to_str(arg));
	if (key == "quest_state") {
		for (const auto& [q, v] : as_object(arg).items())
			if (game().quests.state_of(q) != to_str(v)) return false;
		return true;
	}
	if (key == "quest_stage") {
		for (const auto& [q, v] : as_object(arg).items())
			if (game().quests.state_of(q) != "active" || game().quests.stage_of(q) != to_str(v)) return false;
		return true;
	}
	if (key == "quest_outcome") {
		for (const auto& [q, v] : as_object(arg).items())
			if (game().quests.outcome_of(q) != to_str(v)) return false;
		return true;
	}
	if (key == "has_item") {
		if (arg.is_object()) return p.inventory.has(to_str(jget(arg, "id")), jint(arg, "qty", 1));
		return p.inventory.has(to_str(arg));
	}
	if (key == "not_has_item") return !p.inventory.has(to_str(arg));
	if (key == "money_gte") return p.money >= to_int(arg);
	if (key == "relation_gte") {
		for (const auto& [n, v] : as_object(arg).items())
			if (game().relations.get_rel(npc(Json(n), ctx)) < to_int(v)) return false;
		return true;
	}
	if (key == "relation_lt") {
		for (const auto& [n, v] : as_object(arg).items())
			if (game().relations.get_rel(npc(Json(n), ctx)) >= to_int(v)) return false;
		return true;
	}
	if (key == "reputation_gte") return game().relations.reputation >= to_int(arg);
	if (key == "reputation_lt") return game().relations.reputation < to_int(arg);
	if (key == "skill_gte") {
		for (const auto& [s, v] : as_object(arg).items())
			if (p.skills.level(s) < to_int(v)) return false;
		return true;
	}
	if (key == "memory") return game().relations.has_memory(npc(jget(arg, "npc"), ctx), to_str(jget(arg, "tag")));
	if (key == "not_memory") return !game().relations.has_memory(npc(jget(arg, "npc"), ctx), to_str(jget(arg, "tag")));
	if (key == "need_gte") {
		for (const auto& [n, v] : as_object(arg).items())
			if (p.needs.get_need(n) < to_num(v)) return false;
		return true;
	}
	if (key == "need_lt") {
		for (const auto& [n, v] : as_object(arg).items())
			if (p.needs.get_need(n) >= to_num(v)) return false;
		return true;
	}
	if (key == "employed") return game().job.employed == truthy(arg);
	if (key == "fired") return game().job.fired == truthy(arg);
	if (key == "on_shift") return game().job.on_shift == truthy(arg);
	if (key == "job_rank_gte") return game().job.rank >= to_int(arg);
	if (key == "hour_between") return clock().hour_in(to_num(at(arg, 0)), to_num(at(arg, 1)));
	if (key == "container_has") {
		Inventory* inv = game().world_state.container(to_str(jget(arg, "container")));
		return inv != nullptr && inv->has(to_str(jget(arg, "id")));
	}
	if (key == "not_container_has") {
		Inventory* inv2 = game().world_state.container(to_str(jget(arg, "container")));
		return inv2 == nullptr || !inv2->has(to_str(jget(arg, "id")));
	}
	if (key == "vehicle_damage_gte") {
		const Json& v = jobj(game().world_state.vehicles, to_str(jget(arg, "vehicle")));
		return jnum(v, "damage", 0.0) >= to_num(jget(arg, "value"));
	}
	if (key == "vehicle_near") {
		// {"vehicle": id, "spot": spot_id, "radius": m}
		return game().is_vehicle_near_spot(to_str(jget(arg, "vehicle")), to_str(jget(arg, "spot")), jnum(arg, "radius", 12.0));
	}
	if (key == "crimes_gte") return static_cast<int>(game().law.record.size()) >= to_int(arg);
	if (key == "owns_property") return game().property.owns(to_str(arg));
	if (key == "delivery_hired") return game().delivery.hired == truthy(arg);
	if (key == "vehicle_for_sale") return has(jobj(game().world_state.vehicles, to_str(arg)), "for_sale");
	if (key == "today") return jint(game().flags, to_str(arg), -1) == clock().day();
	if (key == "not_today") return jint(game().flags, to_str(arg), -1) != clock().day();
	if (key == "has_category") {
		for (const auto& s : as_array(p.inventory.items)) {
			if (jstr(data().item(jstr(s, "id")), "category", "") == to_str(arg) && !p.inventory.is_spoiled(s)) return true;
		}
		return false;
	}
	push_warning("Condição desconhecida: " + key);
	return false;
}

double Rules::chance(const Json& spec, const Json& ctx) {
	double c = jnum(spec, "base", 0.5);
	if (has(spec, "skill")) c += game().player.skills.level(to_str(jget(spec, "skill"))) * jnum(spec, "per_level", 0.1);
	if (has(spec, "relation")) c += game().relations.get_rel(npc(jget(spec, "relation"), ctx)) * jnum(spec, "per_relation", 0.004);
	if (has(spec, "per_reputation")) c += game().relations.reputation * to_num(jget(spec, "per_reputation"));
	return clampf(c, 0.05, 0.95);
}

void Rules::apply(const Json& effects, const Json& ctx) {
	for (const auto& e : as_array(effects)) {
		for (const auto& [op, val] : as_object(e).items()) apply_one(op, val, ctx);
	}
	game().after_rules_applied();
}

void Rules::apply_one(const std::string& op, const Json& arg, const Json& ctx) {
	PlayerState& p = game().player;
	if (op == "set_flag") {
		game().set_flag(to_str(arg));
	} else if (op == "clear_flag") {
		game().clear_flag(to_str(arg));
	} else if (op == "give_item") {
		const int qty = jint(arg, "qty", 1);
		const std::string id = to_str(jget(arg, "id"));
		const int added = p.inventory.add(id, qty, 0.0, false);
		if (added > 0) events().notify.emit(fmt(t("Recebido: %s x%d"), {data().item_name(id), added}), "good");
	} else if (op == "take_item") {
		const std::string id = to_str(jget(arg, "id"));
		const int removed = p.inventory.remove(id, jint(arg, "qty", 1));
		if (removed > 0) events().notify.emit(fmt(t("Entregue: %s x%d"), {data().item_name(id), removed}), "info");
	} else if (op == "container_take") {
		Inventory* inv = game().world_state.container(to_str(jget(arg, "container")));
		if (inv) inv->remove(to_str(jget(arg, "id")), jint(arg, "qty", 1));
	} else if (op == "money") {
		p.add_money(to_int(arg), jstr(ctx, "reason", "Pagamento"));
	} else if (op == "set_today") {
		game().flags[to_str(arg)] = clock().day();
		events().flags_changed.emit();
	} else if (op == "take_category") {
		const Json items = p.inventory.items;  // cópia: remove() altera a lista
		for (const auto& s : as_array(items)) {
			if (jstr(data().item(jstr(s, "id")), "category", "") == to_str(arg) && !p.inventory.is_spoiled(s)) {
				const std::string item_id = jstr(s, "id");
				p.inventory.remove(item_id, 1);
				events().notify.emit(fmt(t("Entregue: %s"), {data().item_name(item_id)}), "info");
				break;
			}
		}
	} else if (op == "relation") {
		game().relations.change(npc(jget(arg, "npc"), ctx), to_int(jget(arg, "delta")));
	} else if (op == "reputation") {
		game().relations.change_reputation(to_int(arg));
	} else if (op == "memory") {
		game().relations.add_memory(npc(jget(arg, "npc"), ctx), to_str(jget(arg, "tag")), clock().day());
	} else if (op == "quest_start") {
		game().quests.start(to_str(arg));
	} else if (op == "quest_stage") {
		game().quests.set_stage(to_str(at(arg, 0)), to_str(at(arg, 1)));
	} else if (op == "quest_complete") {
		game().quests.complete(to_str(at(arg, 0)), to_str(at(arg, 1)));
	} else if (op == "skill_xp") {
		for (const auto& [s, v] : as_object(arg).items()) p.skills.add_xp(s, to_num(v));
	} else if (op == "job_hire") {
		game().job.hire();
	} else if (op == "delivery_hire") {
		game().delivery.hire();
	} else if (op == "buy_vehicle") {
		game().buy_vehicle(to_str(arg));
	} else if (op == "notify") {
		events().notify.emit(t(to_str(arg)), "info");
	} else if (op == "open_shop") {
		events().request_shop.emit(to_str(arg));
	} else if (op == "crime") {
		const Json victim = has(arg, "victim") ? jget(arg, "victim") : Json("self");
		game().report_crime(to_str(jget(arg, "type")), npc(victim, ctx), jbool(arg, "victim_only", true));
	} else if (op == "needs") {
		p.needs.consume(arg);
		events().needs_changed.emit();
	} else if (op == "repair_vehicle") {
		game().repair_vehicle(to_str(jget(arg, "vehicle")), jint(arg, "cost_per_point", 3));
	} else if (op == "advance_time") {
		clock().advance(to_num(arg));
	} else {
		push_warning("Efeito desconhecido: " + op);
	}
}

}  // namespace vila
