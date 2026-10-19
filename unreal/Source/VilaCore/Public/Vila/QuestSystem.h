// Missões orientadas a dados (data/quests.json). Cada etapa tem objetivo, marcadores
// e regras de avanço avaliadas quando o estado do jogo muda.
#pragma once
#include "Vila/Core.h"

namespace vila {

class QuestSystem {
public:
	Json states = Json::object();  // id -> {"state", "stage", "outcome", "log": []}

	std::string state_of(const std::string& id) const;
	std::string stage_of(const std::string& id) const;
	std::string outcome_of(const std::string& id) const;
	bool start(const std::string& id);
	void set_stage(const std::string& id, const std::string& stage);
	void complete(const std::string& id, const std::string& outcome);
	void fail(const std::string& id, const std::string& reason);
	std::string objective(const std::string& id) const;
	// Marcadores ativos ("npc:marcos", "pos:x,z") de todas as missões em andamento.
	Json active_markers() const;
	void check_advances();
	Json to_dict() const;
	void from_dict(const Json& d);

private:
	void log(const std::string& id, const std::string& text);
};

}  // namespace vila
