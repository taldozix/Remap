// Barramento de eventos global (autoload Events do Godot). Sistemas emitem; interface
// e mundo escutam. Nós do Godot viram ids (string).
#pragma once
#include "Vila/Core.h"

namespace vila {

struct Events {
	Signal<std::string, std::string> notify;       // (texto, tipo: info/good/bad/quest/money/skill)
	Signal<int, int> money_changed;                 // (total, variação)
	Signal<std::string> inventory_changed;         // "player" ou id do contêiner
	Signal<> needs_changed;
	Signal<std::string, int> skill_level_up;
	Signal<std::string> quest_updated;
	Signal<> flags_changed;
	Signal<std::string, int> relation_changed;
	Signal<int> reputation_changed;
	Signal<> job_updated;
	Signal<Json> crime_recorded;
	Signal<std::string, std::string> bark;          // (npc, texto)
	Signal<> appearance_changed;
	Signal<std::string> property_changed;
	Signal<std::string> situation_started;
	// Pedidos de interface
	Signal<std::string, std::string> request_dialogue;   // (npc, diálogo)
	Signal<std::string, std::string> request_container;  // (id, título)
	Signal<std::string> request_shop;
	Signal<std::string, std::string> request_message;    // (título, corpo)
	Signal<std::string, double> request_sleep;           // (cama, qualidade)
	Signal<> dialogue_closed;
	// Mundo
	Signal<std::string> player_vehicle_changed;  // id do veículo ("" = a pé)
	Signal<std::string> player_building_changed;
	Signal<> world_ready;
	Signal<> game_loaded;

	// Desconecta todos (ao trocar de partida/mundo).
	void clear_all();
};

Events& events();

}  // namespace vila
