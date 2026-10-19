#include "Vila/Inventory.h"

#include "Vila/Data.h"
#include "Vila/Events.h"

namespace vila {

Inventory::Inventory(double p_capacity, std::string p_owner) : capacity(p_capacity), owner_id(std::move(p_owner)) {}

double Inventory::item_weight(const std::string& id) { return jnum(data().item(id), "weight", 0.1); }

bool Inventory::is_perishable(const std::string& id) { return vila::has(data().item(id), "spoil_hours"); }

double Inventory::weight() const {
	double w = 0.0;
	for (const auto& s : items) w += item_weight(jstr(s, "id")) * jint(s, "qty");
	return w;
}

double Inventory::free_weight() const { return capacity - weight(); }

int Inventory::count(const std::string& id) const {
	int n = 0;
	for (const auto& s : items)
		if (jstr(s, "id") == id) n += jint(s, "qty");
	return n;
}

bool Inventory::has(const std::string& id, int qty) const { return count(id) >= qty; }

bool Inventory::can_fit(const std::string& id, int qty, double limit) const {
	const double cap = limit < 0.0 ? capacity : limit;
	return weight() + item_weight(id) * qty <= cap + 0.0001;
}

int Inventory::add(const std::string& id, int qty, double age, bool strict) {
	if (qty <= 0 || !data().has_item(id)) return 0;
	int fits = qty;
	if (strict) {
		const double w = item_weight(id);
		fits = w <= 0.0 ? qty : std::min(qty, static_cast<int>(std::floor((free_weight() + 0.0001) / w)));
	}
	if (fits <= 0) return 0;
	bool merged = false;
	for (auto& s : items) {
		if (jstr(s, "id") == id && std::abs(jnum(s, "age", 0.0) - age) < 6.0) {
			s["qty"] = jint(s, "qty") + fits;
			merged = true;
			break;
		}
	}
	if (!merged) items.push_back(Json{{"id", id}, {"qty", fits}, {"age", age}});
	changed();
	return fits;
}

int Inventory::remove(const std::string& id, int qty) {
	int removed = 0;
	std::vector<size_t> stacks;
	for (size_t i = 0; i < items.size(); ++i)
		if (jstr(items[i], "id") == id) stacks.push_back(i);
	std::stable_sort(stacks.begin(), stacks.end(),
	                 [this](size_t a, size_t b) { return jnum(items[a], "age", 0.0) > jnum(items[b], "age", 0.0); });
	for (size_t i : stacks) {
		if (removed >= qty) break;
		Json& s = items[i];
		const int take = std::min(jint(s, "qty"), qty - removed);
		s["qty"] = jint(s, "qty") - take;
		removed += take;
	}
	Json kept = Json::array();
	for (const auto& s : items)
		if (jint(s, "qty") > 0) kept.push_back(s);
	items = std::move(kept);
	if (removed > 0) changed();
	return removed;
}

Json Inventory::take_at(int index, int qty) {
	if (index < 0 || index >= static_cast<int>(items.size())) return Json::object();
	Json& s = items[static_cast<size_t>(index)];
	const int take = std::min(qty, jint(s, "qty"));
	Json out = Json{{"id", jget(s, "id")}, {"qty", take}, {"age", jnum(s, "age", 0.0)}};
	s["qty"] = jint(s, "qty") - take;
	if (jint(s, "qty") <= 0) items.erase(static_cast<size_t>(index));
	changed();
	return out;
}

int Inventory::transfer_to(Inventory& other, int index, int qty, bool strict) {
	if (index < 0 || index >= static_cast<int>(items.size())) return 0;
	const Json& s = items[static_cast<size_t>(index)];
	int want = std::min(qty, jint(s, "qty"));
	const double w = item_weight(jstr(s, "id"));
	if (strict && w > 0.0) want = std::min(want, static_cast<int>(std::floor((other.free_weight() + 0.0001) / w)));
	if (want <= 0) return 0;
	const Json moved = take_at(index, want);
	other.add(jstr(moved, "id"), jint(moved, "qty"), jnum(moved, "age"), false);
	return jint(moved, "qty");
}

void Inventory::tick_age(double hours) {
	const double rate = refrigerated ? 0.25 : 1.0;
	for (auto& s : items)
		if (is_perishable(jstr(s, "id"))) s["age"] = jnum(s, "age", 0.0) + hours * rate;
}

bool Inventory::is_spoiled(const Json& stack) const {
	const double spoil = jnum(data().item(jstr(stack, "id")), "spoil_hours", -1.0);
	return spoil > 0.0 && jnum(stack, "age", 0.0) >= spoil;
}

double Inventory::freshness(const Json& stack) const {
	const double spoil = jnum(data().item(jstr(stack, "id")), "spoil_hours", -1.0);
	if (spoil <= 0.0) return 1.0;
	return clampf(1.0 - jnum(stack, "age", 0.0) / spoil, 0.0, 1.0);
}

void Inventory::clear() {
	items = Json::array();
	changed();
}

int Inventory::remove_where(const std::function<bool(const Json&)>& pred) {
	const int before = static_cast<int>(items.size());
	Json kept = Json::array();
	for (const auto& s : items)
		if (!pred(s)) kept.push_back(s);
	items = std::move(kept);
	if (static_cast<int>(items.size()) != before) changed();
	return before - static_cast<int>(items.size());
}

void Inventory::changed() {
	if (!owner_id.empty()) events().inventory_changed.emit(owner_id);
}

Json Inventory::to_dict() const { return Json{{"items", items}, {"capacity", capacity}, {"refrigerated", refrigerated}}; }

void Inventory::from_dict(const Json& d) {
	items = Json::array();
	for (const auto& s : jarr(d, "items")) {
		if (s.is_object() && data().has_item(jstr(s, "id", "")))
			items.push_back(Json{{"id", jstr(s, "id")}, {"qty", jint(s, "qty", 1)}, {"age", jnum(s, "age", 0.0)}});
	}
	capacity = jnum(d, "capacity", capacity);
	refrigerated = jbool(d, "refrigerated", refrigerated);
}

}  // namespace vila
