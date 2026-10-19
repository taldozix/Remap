// Crimes e consequências. Sem testemunhas, o crime fica "sem autoria" (mas a vítima
// percebe o prejuízo). Com testemunhas: multa no dia seguinte, perda de reputação,
// memórias negativas e mudanças de comportamento dos NPCs.
#pragma once
#include "Vila/Core.h"

namespace vila {

class Relations;

class VILACORE_API LawSystem {
public:
	static const Json CRIMES;

	Json record = Json::array();          // [{"type", "day", "victim", "witnesses", "fine"}]
	int pending_fines = 0;
	Json pending_reasons = Json::array();
	Json unsolved = Json::array();        // crimes sem testemunha: [{"type", "victim", "day"}]

	// Registra um crime. `witnesses` = NPCs que viram (a vítima pode estar entre eles).
	Json report(const std::string& type, const std::string& victim, const Json& witnesses, int day, Relations& relations);
	// Remove a parte da multa de um incidente quando o jogador repara o erro (ex.: prestou socorro).
	void mitigate(const std::string& type, int amount);
	bool was_seen(const std::string& type, const std::string& victim) const;
	// Chamado na virada do dia: chega a notificação e a multa é cobrada (pode gerar dívida).
	Json collect_fines();
	Json to_dict() const;
	void from_dict(const Json& d);
};

}  // namespace vila
