// Comércio: estoque por loja, preços afetados por relacionamento e reputação,
// horário de funcionamento e presença do atendente (verificada pelo mundo).
#pragma once
#include "Vila/Core.h"

namespace vila {

class PlayerState;
class Relations;

class ShopSystem {
public:
	Json stock = Json::object();  // shop_id -> {item_id: qty}
	int last_restock_day = 0;

	void init_from_data();
	void restock(int day);
	bool is_open(const std::string& shop_id, double hour_f) const;
	// Desconto de "freguês": relacionamento com o dono e reputação no bairro.
	double discount(const std::string& shop_id, Relations& relations) const;
	// Quem tem reputação muito baixa (ou foi visto roubando o dono) não é atendido.
	std::string refuses_service(const std::string& shop_id, Relations& relations) const;
	int price_buy(const std::string& shop_id, const std::string& item_id, Relations& relations) const;
	int price_sell(const std::string& shop_id, const std::string& item_id) const;
	bool buys_item(const std::string& shop_id, const std::string& item_id) const;
	Json buy(const std::string& shop_id, const std::string& item_id, int qty, PlayerState& player, Relations& relations);
	Json sell(const std::string& shop_id, int index, int qty, PlayerState& player);
	// Compras feitas por NPCs consomem o estoque de verdade.
	std::string npc_purchase(const std::string& shop_id, Rng& rng);
	Json to_dict() const;
	void from_dict(const Json& d);
};

}  // namespace vila
