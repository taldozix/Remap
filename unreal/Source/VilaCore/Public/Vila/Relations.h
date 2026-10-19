// Relacionamentos individuais (-100..100), memórias de NPCs sobre o jogador
// e reputação no bairro. Diálogos, preços e reações consultam estes dados.
#pragma once
#include "Vila/Core.h"

namespace vila {

class Relations {
public:
	Json rel = Json::object();          // npc_id -> int
	Json memories = Json::object();     // npc_id -> [{"tag": String, "day": int}]
	int reputation = 0;                 // reputação na Vila Aurora
	Json greeted_day = Json::object();  // npc_id -> último dia em que cumprimentou

	int get_rel(const std::string& npc_id) const;
	void change(const std::string& npc_id, int delta);
	void change_reputation(int delta);
	void add_memory(const std::string& npc_id, const std::string& tag, int day);
	bool has_memory(const std::string& npc_id, const std::string& tag) const;
	// "hostil", "desconfiado", "neutro", "amigável", "amigo"
	std::string attitude(const std::string& npc_id) const;
	static std::string reputation_label(int value);  // sem tr(), como no GDScript
	Json to_dict() const;
	void from_dict(const Json& d);
};

}  // namespace vila
