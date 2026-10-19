// Imóveis do jogador: aluguel, compra e venda (data/properties.json), IPTU semanal,
// dívida e acesso.
#pragma once
#include "Vila/Core.h"

namespace vila {

class PlayerState;

class VILACORE_API PropertySystem {
public:
	// building_id -> {"status": "alugada"/"propria", "rent", "tax", "debt", "paid"}
	Json props = Json::object();

	void init_default();
	bool owns_or_rents(const std::string& building_id) const;
	bool owns(const std::string& building_id) const;
	bool is_for_sale(const std::string& building_id) const;
	int price(const std::string& building_id) const;
	int sale_value(const std::string& building_id) const;
	Json owned_homes() const;  // array de ids
	Json buy(const std::string& building_id, PlayerState& player);
	Json sell(const std::string& building_id, PlayerState& player);
	// Encerrar o aluguel só é permitido com outra casa e sem dívida.
	Json end_lease(const std::string& building_id);
	// Cobrança semanal: aluguel e IPTU. Sem dinheiro, vira dívida. Array de mensagens.
	Json charge_rent(PlayerState& player);
	int total_debt() const;
	Json to_dict() const;
	void from_dict(const Json& d);
};

}  // namespace vila
