#include "Vila/QuestSystem.h"

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Rules.h"

namespace vila {

std::string QuestSystem::state_of(const std::string& id) const { return jstr(jobj(states, id), "state", "not_started"); }

std::string QuestSystem::stage_of(const std::string& id) const { return jstr(jobj(states, id), "stage", ""); }

std::string QuestSystem::outcome_of(const std::string& id) const { return jstr(jobj(states, id), "outcome", ""); }

bool QuestSystem::start(const std::string& id) {
	const Json& q = data().quest(id);
	if (q.empty() || state_of(id) != "not_started") return false;
	states[id] = Json{{"state", "active"}, {"stage", jstr(q, "start_stage", "")}, {"outcome", ""}, {"log", Json::array()}};
	log(id, tr("Missão iniciada."));
	events().notify.emit(fmt(tr("Nova missão: %s"), {tr(jstr(q, "title", id))}), "quest");
	events().quest_updated.emit(id);
	check_advances();
	return true;
}

void QuestSystem::set_stage(const std::string& id, const std::string& stage) {
	if (state_of(id) != "active" || stage_of(id) == stage) return;
	states[id]["stage"] = stage;
	const std::string obj = jstr(jobj(jobj(data().quest(id), "stages"), stage), "objective", "");
	log(id, tr(obj));
	events().notify.emit(fmt(tr("Objetivo atualizado: %s"), {tr(obj)}), "quest");
	events().quest_updated.emit(id);
}

void QuestSystem::complete(const std::string& id, const std::string& outcome) {
	if (state_of(id) != "active") return;
	states[id]["state"] = "completed";
	states[id]["outcome"] = outcome;
	const std::string text = jstr(jobj(data().quest(id), "outcomes"), outcome, "");
	if (!text.empty()) log(id, tr(text));
	events().notify.emit(fmt(tr("Missão concluída: %s"), {tr(jstr(data().quest(id), "title", id))}), "quest");
	events().quest_updated.emit(id);
}

void QuestSystem::fail(const std::string& id, const std::string& reason) {
	if (state_of(id) != "active") return;
	states[id]["state"] = "failed";
	log(id, tr(reason));
	events().quest_updated.emit(id);
}

std::string QuestSystem::objective(const std::string& id) const {
	if (state_of(id) != "active") return "";
	return tr(jstr(jobj(jobj(data().quest(id), "stages"), stage_of(id)), "objective", ""));
}

Json QuestSystem::active_markers() const {
	Json out = Json::array();
	for (const auto& [id, st] : states.items()) {
		(void)st;
		if (state_of(id) == "active") {
			for (const auto& m : jarr(jobj(jobj(data().quest(id), "stages"), stage_of(id)), "markers")) out.push_back(m);
		}
	}
	return out;
}

void QuestSystem::check_advances() {
	std::vector<std::string> keys;  // states.keys(): cópia, regras podem iniciar missões
	for (const auto& [id, st] : states.items()) {
		(void)st;
		keys.push_back(id);
	}
	for (const auto& id : keys) {
		if (state_of(id) != "active") continue;
		const Json stage = jobj(jobj(data().quest(id), "stages"), stage_of(id));
		for (const auto& rule : jarr(stage, "advance")) {
			if (Rules::check(jobj(rule, "if"), Json::object())) {
				if (has(rule, "to")) set_stage(id, to_str(jget(rule, "to")));
				if (has(rule, "do")) Rules::apply(jget(rule, "do"), Json::object());
				break;
			}
		}
	}
}

void QuestSystem::log(const std::string& id, const std::string& text) {
	if (text.empty()) return;
	Json& lg = states[id]["log"];
	if (!lg.is_array()) lg = Json::array();
	lg.push_back(Json{{"day", clock().day()}, {"time", clock().time_string()}, {"text", text}});
}

Json QuestSystem::to_dict() const { return Json{{"states", states}}; }

void QuestSystem::from_dict(const Json& d) {
	states = Json::object();
	const Json& src = jobj(d, "states");
	for (const auto& [id, v] : src.items()) {
		if (!data().quest(id).empty()) states[id] = v;
	}
}

}  // namespace vila
