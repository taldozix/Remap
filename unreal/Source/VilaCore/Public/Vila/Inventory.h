// Inventário limitado por peso. Usado pelo jogador, contêineres e porta-malas.
// Itens perecíveis guardam "age" (horas) e não se empilham com idades muito diferentes.
#pragma once
#include "Vila/Core.h"

namespace vila {

class Inventory {
public:
	Json items = Json::array();  // [{"id": String, "qty": int, "age": float}]
	double capacity = 12.0;      // kg
	bool refrigerated = false;
	std::string owner_id;

	Inventory(double p_capacity = 12.0, std::string p_owner = "");

	static double item_weight(const std::string& id);
	static bool is_perishable(const std::string& id);

	double weight() const;
	double free_weight() const;
	int count(const std::string& id) const;
	bool has(const std::string& id, int qty = 1) const;
	bool can_fit(const std::string& id, int qty = 1, double limit = -1.0) const;
	// Adiciona itens. Com `strict`, respeita a capacidade e retorna quanto coube.
	int add(const std::string& id, int qty = 1, double age = 0.0, bool strict = true);
	// Remove `qty` unidades, começando pelos mais velhos. Retorna a quantidade removida.
	int remove(const std::string& id, int qty = 1);
	// Remove de uma pilha específica e devolve uma cópia do que saiu (com idade). {} se índice inválido.
	Json take_at(int index, int qty = 1);
	// Move uma pilha (ou parte) para outro inventário. Retorna a quantidade movida.
	int transfer_to(Inventory& other, int index, int qty = 1, bool strict = true);
	// Envelhece perecíveis. Geladeira desacelera 4x.
	void tick_age(double hours);
	bool is_spoiled(const Json& stack) const;
	double freshness(const Json& stack) const;
	void clear();
	int remove_where(const std::function<bool(const Json&)>& pred);
	Json to_dict() const;
	void from_dict(const Json& d);

private:
	void changed();
};

}  // namespace vila
