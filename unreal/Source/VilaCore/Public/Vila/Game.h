// Estado central da partida (autoload Game). Possui os sistemas de simulação e faz a ponte
// entre eles: o tempo passa → necessidades, comida, trabalho, aluguel, multas.
// O mundo 3D se registra em `world` para consultas espaciais.
#pragma once
#include "Vila/Core.h"
#include "Vila/DeliveryJob.h"
#include "Vila/DriveThruJob.h"
#include "Vila/EventDirector.h"
#include "Vila/LawSystem.h"
#include "Vila/PlayerState.h"
#include "Vila/PropertySystem.h"
#include "Vila/QuestSystem.h"
#include "Vila/Relations.h"
#include "Vila/ShopSystem.h"
#include "Vila/World.h"
#include "Vila/WorldState.h"

namespace vila {

class VILACORE_API Game {
public:
	static constexpr int SAVE_VERSION = 2;

	PlayerState player;
	WorldState world_state;
	QuestSystem quests;
	Relations relations;
	LawSystem law;
	DriveThruJob job;
	ShopSystem shops;
	PropertySystem property;
	DeliveryJob delivery;
	EventDirector director;
	Json flags = Json::object();
	Rng rng;
	IWorld* world = nullptr;            // mundo ativo (nullptr no menu)
	Json pending_load = Json::object();  // {"new": true} / {"loaded": true}

	// Liga os sinais do relógio e do inventário (o _ready do autoload). Idempotente.
	void init();
	void new_game();
	bool is_mine(const std::string& owner_id, const std::string& building_id);
	double ambient_temperature() const;
	Json environment() const;
	// flags
	bool has_flag(const std::string& f) const { return flags.contains(f); }
	void set_flag(const std::string& f);
	void clear_flag(const std::string& f);
	void after_rules_applied();
	// trabalho
	Json clock_out_player();
	// crimes (pos == nullptr: posição do jogador)
	Json report_crime(const std::string& type, const std::string& victim, bool victim_only = false, const Vec3* pos = nullptr);
	// veículos
	bool is_vehicle_near_spot(const std::string& vehicle_id, const std::string& spot, double radius) const;
	bool buy_vehicle(const std::string& vehicle_id);
	int vehicle_sale_value(const std::string& vehicle_id) const;
	int sell_vehicle(const std::string& vehicle_id);
	void repair_vehicle(const std::string& vehicle_id, int cost_per_point);
	// salvamento
	Json to_dict();
	void from_dict(const Json& d);

private:
	void on_time_advanced(double minutes, bool skipped);
	void on_shift_ended(const Json& summary);
	void on_hour_changed(int hour);
	void on_day_changed(int day);
	int last_day_processed_ = 0;
	bool initialized_ = false;
};

VILACORE_API Game& game();

}  // namespace vila
