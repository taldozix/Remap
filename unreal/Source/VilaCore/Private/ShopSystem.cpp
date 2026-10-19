#include "Vila/ShopSystem.h"

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Game.h"
#include "Vila/Inventory.h"
#include "Vila/PlayerState.h"
#include "Vila/Relations.h"

namespace vila {

void ShopSystem::init_from_data() {
	stock = Json::object();
	for (const auto& [shop_id, _] : data().shops().items()) {
		Json s = Json::object();
		for (const auto& [k, v] : jobj(data().shop(shop_id), "sells").items()) s[k] = to_int(v);
		stock[shop_id] = s;
	}
}

void ShopSystem::restock(int day) {
	if (day == last_restock_day) return;
	last_restock_day = day;
	for (const auto& [shop_id, _] : data().shops().items()) {
		const Json& base = jobj(data().shop(shop_id), "sells");
		Json& s = stock[shop_id];
		for (const auto& [k, v] : base.items()) s[k] = std::max(jint(s, k, 0), to_int(v));
	}
}

bool ShopSystem::is_open(const std::string& shop_id, double hour_f) const {
	const Json& shop = data().shop(shop_id);
	const Json h = has(shop, "hours") ? shop["hours"] : Json::array({0, 24});
	return Clock::is_hour_in(hour_f, to_num(h[0]), to_num(h[1]));
}

double ShopSystem::discount(const std::string& shop_id, Relations& relations) const {
	const std::string owner = jstr(data().shop(shop_id), "owner", "");
	double d = 0.0;
	if (owner != "" && relations.get_rel(owner) >= 30) d += 0.10;
	if (relations.reputation >= 25) d += 0.05;
	d += game().director.shop_discount(shop_id);
	return std::min(d, 0.5);
}

std::string ShopSystem::refuses_service(const std::string& shop_id, Relations& relations) const {
	const std::string owner = jstr(data().shop(shop_id), "owner", "");
	if (owner != "" && relations.get_rel(owner) <= -40)
		return fmt(tr("%s se recusa a atender você."), {tr(jstr(data().npc(owner), "name", owner))});
	if (relations.reputation <= -30) return tr("\"Aqui não atendemos gente como você.\"");
	return "";
}

int ShopSystem::price_buy(const std::string& shop_id, const std::string& item_id, Relations& relations) const {
	const double base = jnum(data().item(item_id), "value", 1);
	const double markup = jnum(data().shop(shop_id), "markup", 1.0);
	return std::max(1, static_cast<int>(std::round(base * markup * (1.0 - discount(shop_id, relations)))));
}

int ShopSystem::price_sell(const std::string& shop_id, const std::string& item_id) const {
	const double rate = jnum(data().shop(shop_id), "buy_rate", 0.4);
	return std::max(0, static_cast<int>(std::floor(jnum(data().item(item_id), "value", 0) * rate)));
}

bool ShopSystem::buys_item(const std::string& shop_id, const std::string& item_id) const {
	const Json& it = data().item(item_id);
	if (jbool(it, "no_sell", false)) return false;
	const Json& cat = has(it, "category") ? it["category"] : Json("");
	return json_array_has(jarr(data().shop(shop_id), "buys"), cat);
}

Json ShopSystem::buy(const std::string& shop_id, const std::string& item_id, int qty, PlayerState& player, Relations& relations) {
	const int available = jint(jobj(stock, shop_id), item_id, 0);
	if (available < qty) return {{"ok", false}, {"reason", tr("Sem estoque.")}};
	const int total = price_buy(shop_id, item_id, relations) * qty;
	if (!player.can_afford(total)) return {{"ok", false}, {"reason", tr("Dinheiro insuficiente.")}};
	if (!player.inventory.can_fit(item_id, qty, player.capacity() * 1.5))
		return {{"ok", false}, {"reason", tr("Você não consegue carregar mais peso.")}};
	player.spend(total, jstr(data().shop(shop_id), "name", shop_id));
	player.inventory.add(item_id, qty, 0.0, false);
	stock[shop_id][item_id] = available - qty;
	return {{"ok", true}, {"total", total}};
}

Json ShopSystem::sell(const std::string& shop_id, int index, int qty, PlayerState& player) {
	if (index < 0 || index >= static_cast<int>(player.inventory.items.size())) return {{"ok", false}, {"reason", "?"}};
	const Json stack = player.inventory.items[static_cast<size_t>(index)];
	const std::string item_id = jstr(stack, "id");
	if (!buys_item(shop_id, item_id)) return {{"ok", false}, {"reason", tr("Esta loja não compra isso.")}};
	if (player.is_equipped(item_id) && player.inventory.count(item_id) <= qty)
		return {{"ok", false}, {"reason", tr("Desequipe o item antes de vender.")}};
	if (player.inventory.is_spoiled(stack)) return {{"ok", false}, {"reason", tr("Ninguém compra comida estragada.")}};
	const Json moved = player.inventory.take_at(index, qty);
	const int moved_qty = jint(moved, "qty", 0);
	const int total = price_sell(shop_id, item_id) * moved_qty;
	player.add_money(total, jstr(data().shop(shop_id), "name", shop_id));
	if (!has(stock, shop_id)) stock[shop_id] = Json::object();
	stock[shop_id][item_id] = jint(stock[shop_id], item_id, 0) + moved_qty;
	return {{"ok", true}, {"total", total}};
}

std::string ShopSystem::npc_purchase(const std::string& shop_id, Rng& rng) {
	if (!has(stock, shop_id) || !stock[shop_id].is_object()) return "";
	Json& s = stock[shop_id];
	std::vector<std::string> options;
	for (const auto& [k, v] : s.items()) {
		const std::string cat = jstr(data().item(k), "category", "");
		if (to_int(v) > 0 && (cat == "comida" || cat == "bebida")) options.push_back(k);
	}
	if (options.empty()) return "";
	const std::string pick = options[rng.randi() % options.size()];
	s[pick] = to_int(s[pick]) - 1;
	return pick;
}

Json ShopSystem::to_dict() const { return {{"stock", stock}, {"last_restock_day", last_restock_day}}; }

void ShopSystem::from_dict(const Json& d) {
	init_from_data();
	const Json& src = jobj(d, "stock");
	for (const auto& [shop_id, items] : src.items()) {
		if (!has(stock, shop_id) || !items.is_object()) continue;
		for (const auto& [k, v] : items.items())
			if (data().has_item(k)) stock[shop_id][k] = to_int(v);
	}
	last_restock_day = jint(d, "last_restock_day", 0);
}

}  // namespace vila
