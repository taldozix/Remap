#include "Vila/LawSystem.h"

#include "Vila/Events.h"
#include "Vila/Relations.h"

namespace vila {

const Json LawSystem::CRIMES = {
	{"furto", {{"label", "Furto"}, {"fine", 90}, {"rep", -12}, {"rel_victim", -35}, {"rel_witness", -15}}},
	{"invasao", {{"label", "Invasão de domicílio"}, {"fine", 60}, {"rep", -8}, {"rel_victim", -25}, {"rel_witness", -8}}},
	{"atropelamento", {{"label", "Atropelamento"}, {"fine", 150}, {"rep", -15}, {"rel_victim", -40}, {"rel_witness", -15}}},
	{"omissao_socorro", {{"label", "Omissão de socorro"}, {"fine", 120}, {"rep", -10}, {"rel_victim", -20}, {"rel_witness", -10}}},
	{"ameaca", {{"label", "Ameaça"}, {"fine", 0}, {"rep", -6}, {"rel_victim", -30}, {"rel_witness", -10}}},
	{"furto_veiculo", {{"label", "Furto de veículo"}, {"fine", 250}, {"rep", -18}, {"rel_victim", -50}, {"rel_witness", -15}}},
	{"batida", {{"label", "Colisão de trânsito"}, {"fine", 40}, {"rep", -2}, {"rel_victim", -10}, {"rel_witness", 0}}},
};

Json LawSystem::report(const std::string& type, const std::string& victim, const Json& witnesses, int day, Relations& relations) {
	const Json& def = jobj(CRIMES, type);
	if (def.empty()) return Json::object();
	const Json wit = witnesses.is_array() ? witnesses : Json::array();
	Json entry = {{"type", type}, {"day", day}, {"victim", victim}, {"witnesses", wit}, {"fine", 0}};
	if (wit.empty()) {
		unsolved.push_back(Json{{"type", type}, {"victim", victim}, {"day", day}});
		entry["solved"] = false;
	} else {
		entry["solved"] = true;
		const int fine = jint(def, "fine");
		entry["fine"] = fine;
		if (fine > 0) {
			pending_fines += fine;
			pending_reasons.push_back(jget(def, "label"));
		}
		relations.change_reputation(jint(def, "rep"));
		for (const auto& wj : wit) {
			const std::string w = to_str(wj);
			relations.add_memory(w, "viu_" + type, day);
			if (w != victim) relations.change(w, jint(def, "rel_witness"));
		}
		if (!victim.empty()) {
			relations.change(victim, jint(def, "rel_victim"));
			relations.add_memory(victim, "vitima_" + type, day);
		}
	}
	record.push_back(entry);
	events().crime_recorded.emit(entry);
	return entry;
}

void LawSystem::mitigate(const std::string& type, int amount) {
	(void)type;
	pending_fines = std::max(0, pending_fines - amount);
	if (pending_fines == 0) pending_reasons = Json::array();
}

bool LawSystem::was_seen(const std::string& type, const std::string& victim) const {
	for (const auto& e : record) {
		if (jstr(e, "type") == type && jstr(e, "victim") == victim && jbool(e, "solved", false)) return true;
	}
	return false;
}

Json LawSystem::collect_fines() {
	if (pending_fines <= 0) return Json::object();
	Json out = {{"amount", pending_fines}, {"reasons", pending_reasons}};
	pending_fines = 0;
	pending_reasons = Json::array();
	return out;
}

Json LawSystem::to_dict() const {
	return Json{{"record", record}, {"pending", pending_fines}, {"reasons", pending_reasons}, {"unsolved", unsolved}};
}

void LawSystem::from_dict(const Json& d) {
	record = jarr(d, "record");
	pending_fines = jint(d, "pending", 0);
	pending_reasons = jarr(d, "reasons");
	unsolved = jarr(d, "unsolved");
}

}  // namespace vila
