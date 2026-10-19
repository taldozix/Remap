// Eventos dinâmicos controlados por regras (data/events.json): janela de horário,
// chance por hora, intervalo mínimo em dias, clima e condições da linguagem Rules.
// Eventos que precisam do mundo são criados pelo IWorld (start_event/end_event).
#pragma once
#include "Vila/Core.h"

namespace vila {

class EventDirector {
public:
	Json last_fired = Json::object();  // id -> dia
	Json active = Json::object();      // id -> {"until": minutos, ...}
	Rng rng;
	bool auto_enabled = true;          // testes desligam o sorteio automático

	void tick(double minutes);
	void evaluate_hour();
	bool can_fire(const std::string& id) const;
	// Dispara um evento (também usado por testes e ferramentas de depuração).
	bool fire(const std::string& id);
	bool is_active(const std::string& id) const;
	double shop_discount(const std::string& shop_id) const;
	double wage_multiplier() const;
	Json to_dict() const;
	void from_dict(const Json& d);

private:
	void end(const std::string& id);
	double acc = 0.0;
};

}  // namespace vila
