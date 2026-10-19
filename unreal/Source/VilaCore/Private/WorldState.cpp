#include "Vila/WorldState.h"

#include "Vila/Data.h"

namespace vila {

void WorldState::init_from_map(const Json& map) {
	containers.clear();
	container_meta = Json::object();
	doors = Json::object();
	ground_items = Json::array();
	vehicles = Json::object();
	npcs = Json::object();
	searched = Json::object();
	for (const auto& b : jarr(map, "buildings")) {
		for (const auto& d : jarr(b, "doors"))
			doors[jstr(d, "id")] = Json{{"open", false}, {"locked", jbool(d, "locked", false)}};
		for (const auto& f : jarr(b, "furniture"))
			if (has(f, "container")) register_container(f["container"], jstr(b, "id", ""));
	}
	for (const auto& p : jarr(map, "props"))
		if (has(p, "container")) register_container(p["container"], "");
	for (const auto& v : jarr(map, "vehicles")) {
		const std::string vid = jstr(v, "id");
		const Json& vd = data().vehicle(jstr(v, "type"));
		const Json& pos = jarr(v, "pos");
		Json entry = Json{
			{"type", jget(v, "type")},
			{"pos", Json::array({pos.size() > 0 ? to_num(pos[0]) : 0.0, 0.0, pos.size() > 1 ? to_num(pos[1]) : 0.0})},
			{"rot", jnum(v, "rot", 0.0)},
			{"fuel", jnum(v, "fuel", jnum(vd, "fuel_capacity", 40.0))},
			{"damage", jnum(v, "damage", 0.0)},
			{"owner", jstr(v, "owner", "")},
			{"locked", jbool(v, "locked", true)},
			{"color", jstr(v, "color", "#aaaaaa")},
		};
		if (has(v, "for_sale")) entry["for_sale"] = v["for_sale"];
		vehicles[vid] = entry;
		const std::string tid = "trunk_" + vid;
		containers[tid] = Inventory(jnum(vd, "trunk_capacity", 30.0), tid);
		Inventory& trunk = containers[tid];
		container_meta[tid] = Json{{"owner", jstr(v, "owner", "")}, {"name", tr("Porta-malas")}, {"private", true}, {"building", ""}};
		for (const auto& [item_id, q] : jobj(v, "trunk").items()) trunk.add(item_id, to_int(q), 0.0, false);
	}
}

void WorldState::register_container(const Json& c, const std::string& building) {
	const std::string cid = jstr(c, "id");
	Inventory inv(jnum(c, "capacity", 20.0), cid);
	inv.refrigerated = jbool(c, "refrigerated", false);
	for (const auto& [item_id, q] : jobj(c, "contents").items()) inv.add(item_id, to_int(q), 0.0, false);
	containers[cid] = inv;
	container_meta[cid] = Json{
		{"owner", jstr(c, "owner", "")},
		{"name", jstr(c, "name", "Contêiner")},
		{"private", jbool(c, "private", jstr(c, "owner", "") != "")},
		{"building", building},
		{"refill", has(c, "refill") ? c["refill"] : Json::object()},
	};
}

std::vector<std::string> WorldState::container_order() const {
	std::vector<std::string> out;
	for (const auto& [id, m] : container_meta.items())
		if (containers.count(id)) out.push_back(id);
	for (const auto& [id, inv] : containers)
		if (!has(container_meta, id)) out.push_back(id);
	return out;
}

Inventory* WorldState::container(const std::string& id) {
	auto it = containers.find(id);
	return it == containers.end() ? nullptr : &it->second;
}

const Inventory* WorldState::container(const std::string& id) const {
	auto it = containers.find(id);
	return it == containers.end() ? nullptr : &it->second;
}

Json WorldState::meta(const std::string& id) const { return jobj(container_meta, id); }

bool WorldState::is_door_locked(const std::string& id) const { return jbool(jobj(doors, id), "locked", false); }

void WorldState::set_door(const std::string& id, bool open, bool locked) { doors[id] = Json{{"open", open}, {"locked", locked}}; }

void WorldState::drop_item(const std::string& id, int qty, double age, const Vec3& pos) {
	ground_items.push_back(Json{{"id", id}, {"qty", qty}, {"age", age}, {"pos", Json::array({pos.x, pos.y, pos.z})}});
}

void WorldState::tick_age(double hours) {
	for (auto& [id, inv] : containers) inv.tick_age(hours);
	for (auto& g : ground_items)
		if (Inventory::is_perishable(jstr(g, "id"))) g["age"] = jnum(g, "age", 0.0) + hours;
}

void WorldState::daily_refill(Rng& rng) {
	for (const auto& [id, m] : container_meta.items()) {
		Inventory* inv = container(id);
		if (inv == nullptr) continue;
		for (const auto& [item_id, q] : jobj(m, "refill").items()) {
			const int max_qty = to_int(q);
			if (inv->count(item_id) < max_qty) inv->add(item_id, rng.randi_range(0, max_qty - inv->count(item_id)), 0.0, true);
		}
	}
}

Json WorldState::to_dict() const {
	Json c = Json::object();
	for (const auto& id : container_order()) c[id] = containers.at(id).to_dict();
	return Json{
		{"containers", c}, {"doors", doors}, {"ground_items", ground_items}, {"vehicles", vehicles},
		{"npcs", npcs}, {"searched", searched}, {"weather", weather},
	};
}

void WorldState::from_dict(const Json& d) {
	init_from_map(data().map());
	for (const auto& [id, cd] : jobj(d, "containers").items()) {
		Inventory* inv = container(id);
		if (inv != nullptr) inv->from_dict(cd);
	}
	for (const auto& [id, dd] : jobj(d, "doors").items())
		if (has(doors, id)) doors[id] = Json{{"open", jbool(dd, "open", false)}, {"locked", jbool(dd, "locked", false)}};
	ground_items = jarr(d, "ground_items");
	for (const auto& [id, vs] : jobj(d, "vehicles").items()) {
		if (!has(vehicles, id) || !vs.is_object()) continue;
		if (!has(vs, "for_sale")) vehicles[id].erase("for_sale");
		for (const auto& [k, val] : vs.items()) vehicles[id][k] = val;
	}
	npcs = jobj(d, "npcs");
	searched = jobj(d, "searched");
	if (jget(d, "weather").is_object()) weather = d["weather"];
}

}  // namespace vila
