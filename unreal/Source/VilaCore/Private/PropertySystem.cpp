#include "Vila/PropertySystem.h"

#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/PlayerState.h"

namespace vila {

void PropertySystem::init_default() {
	props = {{"casa_jogador", {{"status", "alugada"}, {"rent", jint(data().property("casa_jogador"), "rent", 120)},
		{"tax", 0}, {"debt", 0}, {"paid", 0}}}};
}

bool PropertySystem::owns_or_rents(const std::string& building_id) const { return has(props, building_id); }

bool PropertySystem::owns(const std::string& building_id) const {
	return has(props, building_id) && jstr(props[building_id], "status") == "propria";
}

bool PropertySystem::is_for_sale(const std::string& building_id) const {
	return has(data().property(building_id), "price") && !has(props, building_id);
}

int PropertySystem::price(const std::string& building_id) const { return jint(data().property(building_id), "price", 0); }

int PropertySystem::sale_value(const std::string& building_id) const {
	const Json& p = jobj(props, building_id);
	return static_cast<int>(jnum(p, "paid", price(building_id)) * jnum(data().property(building_id), "sale_rate", 0.8));
}

Json PropertySystem::owned_homes() const {
	Json out = Json::array();
	for (const auto& [id, _] : props.items())
		if (jstr(data().property(id), "type", "") == "casa") out.push_back(id);
	return out;
}

Json PropertySystem::buy(const std::string& building_id, PlayerState& player) {
	if (!is_for_sale(building_id)) return {{"ok", false}, {"reason", tr("Este imóvel não está à venda.")}};
	const int p = price(building_id);
	if (total_debt() > 0) return {{"ok", false}, {"reason", tr("Quite suas dívidas antes de comprar um imóvel.")}};
	if (!player.spend(p, "Compra de imóvel")) return {{"ok", false}, {"reason", fmt(tr("Dinheiro insuficiente (R$ %d)."), {p})}};
	props[building_id] = {{"status", "propria"}, {"rent", 0}, {"tax", jint(data().property(building_id), "tax", 0)},
		{"debt", 0}, {"paid", p}};
	events().property_changed.emit(building_id);
	events().notify.emit(fmt(tr("Você comprou: %s!"), {tr(jstr(data().property(building_id), "name"))}), "good");
	return {{"ok", true}};
}

Json PropertySystem::sell(const std::string& building_id, PlayerState& player) {
	if (!owns(building_id)) return {{"ok", false}, {"reason", tr("Você não é dono deste imóvel.")}};
	if (owned_homes().size() <= 1 && jstr(data().property(building_id), "type", "") == "casa")
		return {{"ok", false}, {"reason", tr("Você precisa de outro lugar para morar antes de vender.")}};
	const int value = sale_value(building_id);
	props.erase(building_id);
	player.add_money(value, "Venda de imóvel");
	events().property_changed.emit(building_id);
	return {{"ok", true}, {"value", value}};
}

Json PropertySystem::end_lease(const std::string& building_id) {
	if (!has(props, building_id) || jstr(props[building_id], "status") != "alugada")
		return {{"ok", false}, {"reason", tr("Não há contrato de aluguel aqui.")}};
	if (jint(props[building_id], "debt") > 0) return {{"ok", false}, {"reason", tr("Pague o aluguel atrasado antes de sair.")}};
	if (owned_homes().size() <= 1) return {{"ok", false}, {"reason", tr("Você precisa de outra casa antes de entregar esta.")}};
	props.erase(building_id);
	events().property_changed.emit(building_id);
	return {{"ok", true}};
}

Json PropertySystem::charge_rent(PlayerState& player) {
	Json msgs = Json::array();
	for (auto& [id, p] : props.items()) {
		const int due = jint(p, "rent", 0) + jint(p, "tax", 0) + jint(p, "debt", 0);
		if (due <= 0) continue;
		const std::string what = jstr(p, "status") == "alugada" ? tr("Aluguel") : tr("IPTU");
		if (player.money >= due) {
			player.add_money(-due, what);
			p["debt"] = 0;
			msgs.push_back(fmt(tr("%s pago (%s): R$ %d."), {what, tr(jstr(data().property(id), "name", id)), due}));
		} else {
			const int paid = std::max(0, player.money);
			if (paid > 0) player.add_money(-paid, what + tr(" (parcial)"));
			p["debt"] = due - paid;
			msgs.push_back(fmt(tr("%s atrasado! Dívida: R$ %d."), {what, due - paid}));
		}
	}
	return msgs;
}

int PropertySystem::total_debt() const {
	int d = 0;
	for (const auto& [id, p] : props.items()) d += jint(p, "debt", 0);
	return d;
}

Json PropertySystem::to_dict() const { return {{"props", props}}; }

void PropertySystem::from_dict(const Json& d) {
	const Json& src = jobj(d, "props");
	if (src.empty()) {
		init_default();
		return;
	}
	props = Json::object();
	for (const auto& [id, v] : src.items()) {
		props[id] = v.is_object() ? v : Json::object();
		for (const char* k : {"tax", "debt", "paid", "rent"})
			if (!props[id].contains(k)) props[id][k] = 0;
	}
}

}  // namespace vila
