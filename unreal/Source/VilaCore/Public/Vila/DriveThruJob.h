// Emprego no drive-thru (lógica pura). O mundo fornece clientes (carros na fila)
// e as estações físicas (chapa, fritadeira, máquina de refrigerante, janela).
// Tempos em minutos de jogo.
#pragma once
#include "Vila/Core.h"

namespace vila {

class Inventory;
class Needs;

class DriveThruJob {
public:
	static constexpr const char* JOB_ID = "brasa";

	bool employed = false;
	bool fired = false;
	int rank = 0;
	double performance = 50.0;
	int warnings = 0;
	int total_orders = 0;
	int shifts_done = 0;

	bool on_shift = false;
	Json shift = Json::object();
	Json orders = Json::array();      // pedidos ativos
	Json finished = Json::object();   // customer_id -> "done" | "failed"
	Json stations = Json::object();
	int next_order_id = 1;
	double customer_cooldown = 0.0;
	int customers_requested = 0;      // clientes a caminho (ainda sem pedido)

	DriveThruJob();

	const Json& cfg() const;
	std::string rank_title(int r = -1) const;
	int wage() const;
	bool is_open(double hour_f) const;
	void reset_stations();
	void hire();
	Json clock_in(double hour_f, Needs& needs);
	// Encerra o turno e calcula o pagamento. Abandono paga metade e gera advertência.
	Json clock_out(const std::string& reason = "normal");
	void add_warning(const std::string& why);
	void fire(const std::string& why);
	// Avança o turno; retorna o resumo se ele terminou automaticamente.
	Json tick(double minutes, double hour_f, bool player_in_area);
	bool wants_customer() const;
	void customer_dispatched(Rng& rng);
	Json create_order(const std::string& customer_id, Rng& rng);
	static std::string order_text(const Json& items);
	Json order_for(const std::string& customer_id) const;
	Json front_order() const;
	std::string status_of(const std::string& customer_id) const;
	// estações
	double station_cook_time(const std::string& s, int cooking_lvl) const;
	double station_window(const std::string& s, int cooking_lvl) const;
	bool station_start(const std::string& s, int cooking_lvl);
	std::string station_collect(const std::string& s, Inventory& inv);
	double station_progress(const std::string& s) const;
	// entrega
	Json deliver(Inventory& inv, int social_lvl);
	void customer_left(const std::string& customer_id);
	Json to_dict() const;
	void from_dict(const Json& d);

private:
	bool check_promotion();
	void fail_order(const Json& o);
};

}  // namespace vila
