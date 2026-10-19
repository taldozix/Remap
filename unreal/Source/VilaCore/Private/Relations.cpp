#include "Vila/Relations.h"

#include "Vila/Data.h"
#include "Vila/Events.h"

namespace vila {

int Relations::get_rel(const std::string& npc_id) const {
	return jint(rel, npc_id, jint(data().npc(npc_id), "start_relation", 0));
}

void Relations::change(const std::string& npc_id, int delta) {
	if (delta == 0) return;
	const int value = clampi(get_rel(npc_id) + delta, -100, 100);
	rel[npc_id] = value;
	events().relation_changed.emit(npc_id, value);
}

void Relations::change_reputation(int delta) {
	if (delta == 0) return;
	reputation = clampi(reputation + delta, -100, 100);
	events().reputation_changed.emit(reputation);
}

void Relations::add_memory(const std::string& npc_id, const std::string& tag, int day) {
	if (!has(memories, npc_id) || !memories[npc_id].is_array()) memories[npc_id] = Json::array();
	for (auto& m : memories[npc_id]) {
		if (jstr(m, "tag") == tag) {
			m["day"] = day;
			return;
		}
	}
	memories[npc_id].push_back(Json{{"tag", tag}, {"day", day}});
}

bool Relations::has_memory(const std::string& npc_id, const std::string& tag) const {
	for (const auto& m : jarr(memories, npc_id))
		if (jstr(m, "tag") == tag) return true;
	return false;
}

std::string Relations::attitude(const std::string& npc_id) const {
	const int r = get_rel(npc_id);
	if (r <= -40) return "hostil";
	if (r <= -10) return "desconfiado";
	if (r < 20) return "neutro";
	if (r < 50) return "amigável";
	return "amigo";
}

std::string Relations::reputation_label(int value) {
	if (value <= -40) return "Mal-visto";
	if (value <= -10) return "Suspeito";
	if (value < 15) return "Desconhecido";
	if (value < 40) return "Bem-visto";
	return "Querido pelo bairro";
}

Json Relations::to_dict() const {
	return Json{{"rel", rel}, {"memories", memories}, {"reputation", reputation}, {"greeted", greeted_day}};
}

void Relations::from_dict(const Json& d) {
	rel = Json::object();
	for (const auto& [k, val] : jobj(d, "rel").items()) rel[k] = to_int(val);
	memories = jobj(d, "memories");
	reputation = jint(d, "reputation", 0);
	greeted_day = Json::object();
	for (const auto& [k, val] : jobj(d, "greeted").items()) greeted_day[k] = to_int(val);
}

}  // namespace vila
