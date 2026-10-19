// Estado persistente do personagem: necessidades, inventário, dinheiro, habilidades e posição.
#pragma once
#include "Vila/Core.h"
#include "Vila/Inventory.h"
#include "Vila/Needs.h"
#include "Vila/Skills.h"

namespace vila {

class VILACORE_API PlayerState {
public:
	static constexpr double BASE_CAPACITY = 12.0;

	std::string name = "Davi";
	Json appearance = Json{{"skin", "#c68e6a"}, {"hair", "#2b1d12"}, {"hair_style", "short"}, {"shirt", "#3f7f9f"},
	                       {"pants", "#2f3a4a"}, {"height", 1.0}, {"build", 1.0}};
	Needs needs;
	Inventory inventory{BASE_CAPACITY, "player"};
	Skills skills;
	int money = 0;
	Vec3 position;
	double rotation_y = 0.0;
	std::string vehicle_id;
	Json equipped = Json::object();  // slot -> item id (ex.: "costas": "mochila", "camisa": "camiseta_azul")
	Json traits = Json::array();     // ids de traits.json escolhidos na criação
	std::string activity = "idle";   // atividade atual, usada pela simulação de necessidades
	Json stats = Json{{"km_dirigidos", 0.0}, {"pedidos_entregues", 0}, {"dinheiro_ganho", 0}};

	double capacity() const;
	void refresh_capacity();
	// Soma dos modificadores dos traços (multiplicadores multiplicam; booleanos valem se algum tiver).
	Json trait_mods() const;
	void apply_traits(const Json& ids);
	// Isolamento térmico somado das roupas vestidas.
	double warmth() const;
	bool rain_protected() const;
	// Aparência final: cores das roupas vestidas sobrepõem a base escolhida na criação.
	Json visual_appearance() const;
	bool is_overloaded() const;
	void add_money(int amount, const std::string& reason = "");
	bool can_afford(int amount) const;
	bool spend(int amount, const std::string& reason = "");
	bool equip(const std::string& item_id);
	void unequip(const std::string& slot);
	bool is_equipped(const std::string& item_id) const;
	Json to_dict() const;
	void from_dict(const Json& d);
};

}  // namespace vila
