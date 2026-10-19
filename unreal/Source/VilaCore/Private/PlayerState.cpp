#include "Vila/PlayerState.h"

#include "Vila/Data.h"
#include "Vila/Events.h"

namespace vila {

// d.get(k, def) do GDScript para valores brutos.
static Json get_or(const Json& d, const std::string& k, const Json& def) {
	return has(d, k) ? d[k] : def;
}

double PlayerState::capacity() const {
	double cap = BASE_CAPACITY;
	for (const auto& [slot, id] : equipped.items()) cap += jnum(data().item(to_str(id)), "capacity_bonus", 0.0);
	return cap;
}

void PlayerState::refresh_capacity() {
	inventory.capacity = capacity();
	needs.mods = trait_mods();
}

Json PlayerState::trait_mods() const {
	Json out = Json::object();
	for (const auto& t : traits) {
		const Json& m = jobj(data().trait_def(to_str(t)), "mods");
		for (const auto& [k, val] : m.items()) {
			if (val.is_boolean())
				out[k] = jbool(out, k, false) || val.get<bool>();
			else
				out[k] = jnum(out, k, 1.0) * to_num(val);
		}
	}
	return out;
}

void PlayerState::apply_traits(const Json& ids) {
	traits = Json::array();
	if (ids.is_array()) {
		for (const auto& t : ids) {
			const Json& def = data().trait_def(to_str(t));
			if (!def.empty() && !json_array_has(traits, t)) {
				traits.push_back(t);
				for (const auto& [sk, lvl] : jobj(def, "skills").items()) {
					const int i = clampi(to_int(lvl), 0, static_cast<int>(Skills::THRESHOLDS.size()) - 1);
					skills.xp[sk] = std::max(jnum(skills.xp, sk, 0.0), static_cast<double>(Skills::THRESHOLDS[i]));
				}
			}
		}
	}
	refresh_capacity();
}

double PlayerState::warmth() const {
	double w = 0.0;
	for (const auto& [slot, id] : equipped.items()) w += jnum(data().item(to_str(id)), "warmth", 0.0);
	return w;
}

bool PlayerState::rain_protected() const {
	for (const auto& [slot, id] : equipped.items())
		if (jbool(data().item(to_str(id)), "rain_protect", false)) return true;
	return false;
}

Json PlayerState::visual_appearance() const {
	Json a = appearance;
	auto item_of = [this](const char* slot) -> const Json& { return data().item(jstr(equipped, slot)); };
	if (has(equipped, "camisa")) a["shirt"] = get_or(item_of("camisa"), "color", get_or(a, "shirt", "#4477aa"));
	if (has(equipped, "agasalho")) {
		a["shirt"] = get_or(item_of("agasalho"), "color", jget(a, "shirt"));
		a["jacket"] = true;
	}
	if (has(equipped, "chuva")) {
		a["shirt"] = get_or(item_of("chuva"), "color", jget(a, "shirt"));
		a["jacket"] = true;
	}
	if (has(equipped, "cabeca")) a["cap"] = get_or(item_of("cabeca"), "color", "#333333");
	if (has(equipped, "costas")) a["backpack"] = get_or(item_of("costas"), "color", "#5a4a3a");
	return a;
}

bool PlayerState::is_overloaded() const { return inventory.weight() > capacity(); }

void PlayerState::add_money(int amount, const std::string& reason) {
	if (amount == 0) return;
	money += amount;
	if (amount > 0) stats["dinheiro_ganho"] = jint(stats, "dinheiro_ganho", 0) + amount;
	events().money_changed.emit(money, amount);
	if (!reason.empty())
		events().notify.emit(fmt("%sR$ %d — %s", {amount > 0 ? "+" : "-", std::abs(amount), tr(reason)}), "money");
}

bool PlayerState::can_afford(int amount) const { return money >= amount; }

bool PlayerState::spend(int amount, const std::string& reason) {
	if (amount < 0 || money < amount) return false;
	add_money(-amount, reason);
	return true;
}

bool PlayerState::equip(const std::string& item_id) {
	const std::string slot = jstr(data().item(item_id), "slot", "");
	if (slot.empty() || !inventory.has(item_id)) return false;
	equipped[slot] = item_id;
	refresh_capacity();
	events().appearance_changed.emit();
	return true;
}

void PlayerState::unequip(const std::string& slot) {
	if (equipped.is_object()) equipped.erase(slot);
	refresh_capacity();
	events().appearance_changed.emit();
}

bool PlayerState::is_equipped(const std::string& item_id) const {
	for (const auto& [slot, id] : equipped.items())
		if (id == item_id) return true;
	return false;
}

Json PlayerState::to_dict() const {
	return Json{
		{"name", name},
		{"appearance", appearance},
		{"needs", needs.to_dict()},
		{"inventory", inventory.to_dict()},
		{"skills", skills.to_dict()},
		{"money", money},
		{"position", Json::array({position.x, position.y, position.z})},
		{"rotation_y", rotation_y},
		{"vehicle_id", vehicle_id},
		{"equipped", equipped},
		{"traits", traits},
		{"stats", stats},
	};
}

void PlayerState::from_dict(const Json& d) {
	name = jstr(d, "name", name);
	if (jget(d, "appearance").is_object()) appearance = d["appearance"];
	needs.from_dict(jobj(d, "needs"));
	inventory.from_dict(jobj(d, "inventory"));
	skills.from_dict(jobj(d, "skills"));
	money = jint(d, "money", 0);
	const Json& p = jget(d, "position");
	if (p.is_array() && p.size() >= 3)
		position = Vec3(to_num(p[0]), to_num(p[1]), to_num(p[2]));
	else
		position = Vec3();
	rotation_y = jnum(d, "rotation_y", 0.0);
	vehicle_id = jstr(d, "vehicle_id", "");
	equipped = jobj(d, "equipped");
	traits = jarr(d, "traits");
	std::vector<std::string> slots;
	for (const auto& [slot, id] : equipped.items()) slots.push_back(slot);
	for (const auto& slot : slots)
		if (!inventory.has(to_str(equipped[slot]))) equipped.erase(slot);
	if (jget(d, "stats").is_object()) stats = d["stats"];
	refresh_capacity();
}

}  // namespace vila
