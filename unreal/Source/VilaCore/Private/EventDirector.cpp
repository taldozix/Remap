#include "Vila/EventDirector.h"

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Rules.h"
#include "Vila/World.h"
#include "Vila/WorldState.h"

namespace vila {

void EventDirector::tick(double minutes) {
	std::vector<std::string> ids;
	for (const auto& [id, _] : active.items()) ids.push_back(id);
	for (const auto& id : ids)
		if (has(active, id) && clock().total_minutes >= jnum(active[id], "until", 0.0)) end(id);
	acc += minutes;
	while (acc >= 60.0) {
		acc -= 60.0;
		evaluate_hour();
	}
}

void EventDirector::evaluate_hour() {
	if (!auto_enabled) return;
	for (const auto& [id, e] : data().events().items())
		if (can_fire(id) && rng.randf() < jnum(e, "chance", 0.1)) fire(id);
}

bool EventDirector::can_fire(const std::string& id) const {
	const Json& e = jobj(data().events(), id);
	if (e.empty() || has(active, id)) return false;
	const Json h = has(e, "hours") ? e["hours"] : Json::array({0, 24});
	if (!clock().hour_in(to_num(h[0]), to_num(h[1]))) return false;
	if (has(last_fired, id) && clock().day() - to_int(last_fired[id]) < jint(e, "cooldown_days", 1)) return false;
	if (jbool(e, "no_rain", false) && jstr(game().world_state.weather, "state", "") == "chuva") return false;
	return Rules::evaluate(jobj(e, "if"), Json::object());
}

bool EventDirector::fire(const std::string& id) {
	const Json& e = jobj(data().events(), id);
	if (e.empty()) return false;
	Json info = {{"until", clock().total_minutes + jnum(e, "duration", 0.0)}};
	if (id == "promocao_mercadinho") {
		info["discount"] = jnum(e, "discount", 0.25);
		events().notify.emit(tr(jstr(e, "notify")), "money");
	} else if (id == "turno_extra") {
		const double until = (clock().day() - 1) * 1440.0 + jnum(e, "until_hour", 18) * 60.0;
		info["until"] = until;
		info["multiplier"] = jnum(e, "multiplier", 1.5);
		events().request_message.emit(tr(jstr(e, "title")), tr(jstr(e, "notify")));
	} else {
		if (game().world == nullptr) return false;
		const Json r = game().world->start_event(id, e);
		if (!r.is_object() || r.empty()) return false;
		// merge sem sobrescrever (como Dictionary.merge do Godot)
		for (const auto& [k, v] : r.items())
			if (!info.contains(k)) info[k] = v;
	}
	last_fired[id] = clock().day();
	if (jnum(info, "until") > clock().total_minutes) active[id] = info;
	events().situation_started.emit(id);
	return true;
}

void EventDirector::end(const std::string& id) {
	const Json info = jobj(active, id);
	active.erase(id);
	if (game().world) game().world->end_event(id, info);
}

bool EventDirector::is_active(const std::string& id) const { return has(active, id); }

double EventDirector::shop_discount(const std::string& shop_id) const {
	if (shop_id == "mercadinho" && has(active, "promocao_mercadinho"))
		return jnum(active["promocao_mercadinho"], "discount", 0.0);
	return 0.0;
}

double EventDirector::wage_multiplier() const { return jnum(jobj(active, "turno_extra"), "multiplier", 1.0); }

Json EventDirector::to_dict() const {
	Json keep = Json::object();
	for (const auto& [id, v] : active.items())
		if (id == "promocao_mercadinho" || id == "turno_extra") keep[id] = v;
	return {{"last_fired", last_fired}, {"active", keep}};
}

// Situações espaciais não são salvas no meio; só o histórico de disparos.
void EventDirector::from_dict(const Json& d) {
	last_fired = jobj(d, "last_fired");
	active = jobj(d, "active");
}

}  // namespace vila
