// Trabalho temporário de entregador do Mercadinho (bico, sem turno fixo).
// Pega uma sacola no balcão, leva ao endereço antes do prazo e recebe na hora.
#pragma once
#include "Vila/Core.h"

namespace vila {

class Inventory;
class PlayerState;

class DeliveryJob {
public:
	bool hired = false;
	Json active = Json::object();
	int done_today = 0;
	int total = 0;
	int late_total = 0;

	const Json& cfg() const;
	void hire();
	Json can_take() const;
	Json take(double now, Rng& rng, Inventory& inv);
	Json complete(double now, PlayerState& player);
	// Entregas esquecidas por muito tempo são canceladas.
	void tick(double minutes, double now);
	void new_day();
	double time_left(double now) const;
	Json to_dict() const;
	void from_dict(const Json& d);

private:
	int last_dest = -1;
};

}  // namespace vila
