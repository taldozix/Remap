// Estado persistente do mundo: conteúdo de contêineres, portas, itens no chão,
// veículos, NPCs (ferimentos) e clima. Os atores da cena leem e escrevem aqui.
#pragma once
#include "Vila/Core.h"
#include "Vila/Inventory.h"

namespace vila {

class VILACORE_API WorldState {
public:
	std::map<std::string, Inventory> containers;  // id -> Inventory (ordem de registro: chaves de container_meta)
	Json container_meta = Json::object();  // id -> {"owner", "name", "private", "building", "refill"}
	Json doors = Json::object();           // id -> {"open": bool, "locked": bool}
	Json ground_items = Json::array();     // [{"id","qty","age","pos":[x,y,z]}]
	Json vehicles = Json::object();        // id -> {"type","pos","rot","fuel","damage","owner","locked","color"}
	Json npcs = Json::object();            // id -> {"injured_until": minutos, ...}
	Json searched = Json::object();        // id de objeto vasculhado -> dia
	Json weather = Json{{"state", "limpo"}, {"until", 0.0}};

	// Popula o estado inicial a partir do mapa (novo jogo).
	void init_from_map(const Json& map);
	Inventory* container(const std::string& id);  // nullptr se não existir
	const Inventory* container(const std::string& id) const;
	Json meta(const std::string& id) const;  // {} se não existir
	bool is_door_locked(const std::string& id) const;
	void set_door(const std::string& id, bool open, bool locked);
	void drop_item(const std::string& id, int qty, double age, const Vec3& pos);
	// Envelhece comida em todos os contêineres e no chão.
	void tick_age(double hours);
	// Lixeiras e afins se renovam diariamente (latinhas recicláveis etc.).
	void daily_refill(Rng& rng);
	Json to_dict() const;
	// Carrega por cima do estado inicial do mapa: novos contêineres/portas adicionados
	// ao mapa depois do salvamento continuam existindo (compatibilidade de versões).
	void from_dict(const Json& d);

private:
	void register_container(const Json& c, const std::string& building);
	std::vector<std::string> container_order() const;  // ordem de inserção (como o Dictionary)
};

}  // namespace vila
