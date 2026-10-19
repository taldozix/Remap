#include "Vila/Game.h"

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"

namespace vila {

Game& game() {
	static Game g;
	return g;
}

void Game::init() {
	if (initialized_) return;
	initialized_ = true;
	clock().time_advanced.connect([this](double m, bool s) { on_time_advanced(m, s); });
	clock().day_changed.connect([this](int d) { on_day_changed(d); });
	clock().hour_changed.connect([this](int h) { on_hour_changed(h); });
	events().inventory_changed.connect([this](const std::string& owner_id) {
		if (owner_id == "player") quests.check_advances();
	});
	new_game();
}

void Game::new_game() {
	player = PlayerState();
	world_state = WorldState();
	quests = QuestSystem();
	relations = Relations();
	law = LawSystem();
	job = DriveThruJob();
	shops = ShopSystem();
	property = PropertySystem();
	delivery = DeliveryJob();
	director = EventDirector();
	flags = Json::object();
	rng.randomize();
	const Json& start = jobj(data().map(), "start");
	world_state.init_from_map(data().map());
	shops.init_from_data();
	property.init_default();
	player.money = jint(start, "money", 60);
	const Json& sp = jget(start, "player_pos");
	player.position = sp.is_array() && sp.size() >= 2 ? Vec3(to_num(sp[0]), 0.0, to_num(sp[1])) : Vec3(-44.5, 0.0, -18.0);
	player.needs.v = Json{{"fome", 25.0}, {"sede", 20.0}, {"sono", 8.0}, {"fadiga", 0.0}, {"higiene", 10.0}};
	for (const auto& [item_id, qty] : jobj(start, "player_items").items()) player.inventory.add(item_id, to_int(qty), 0.0, false);
	for (const auto& item_id : jarr(start, "player_equipped")) player.equip(to_str(item_id));
	clock().set_time(1, jint(start, "hour", 7), jint(start, "minute", 30));
	last_day_processed_ = clock().day();
	shops.last_restock_day = clock().day();
	pending_load = Json::object();
}

// "owner" no mapa = quem for dono/inquilino do prédio segundo o sistema de propriedades.
bool Game::is_mine(const std::string& owner_id, const std::string& building_id) {
	if (owner_id == "player") return true;
	return owner_id == "owner" && property.owns_or_rents(building_id);
}

// Temperatura do ar (°C): mais fresco de madrugada, quente à tarde; chuva e nuvens esfriam.
double Game::ambient_temperature() const {
	const double h = clock().hour_f();
	double t = 23.0 + 5.5 * std::sin((h - 9.0) / 24.0 * TAU);
	const std::string st = jstr(world_state.weather, "state", "limpo");
	if (st == "chuva")
		t -= 5.0;
	else if (st == "nublado")
		t -= 2.0;
	return t;
}

Json Game::environment() const {
	const bool outdoors = world ? world->player_outdoors() : true;
	return Json{
		{"ambient", outdoors ? ambient_temperature() : 24.5},
		{"outdoors", outdoors},
		{"raining", jstr(world_state.weather, "state") == "chuva"},
		{"warmth", player.warmth()},
		{"rain_protect", player.rain_protected()},
	};
}

void Game::set_flag(const std::string& f) {
	if (!flags.contains(f)) {
		flags[f] = clock().day();
		events().flags_changed.emit();
	}
}

void Game::clear_flag(const std::string& f) {
	if (flags.erase(f) > 0) events().flags_changed.emit();
}

void Game::after_rules_applied() { quests.check_advances(); }

// ---------------------------------------------------------------- simulação
void Game::on_time_advanced(double minutes, bool skipped) {
	if (world == nullptr) return;
	const double hours = minutes / 60.0;
	player.needs.tick(hours, player.activity, player.skills.level("condicionamento"), environment());
	player.inventory.tick_age(hours);
	world_state.tick_age(hours);
	delivery.tick(minutes, clock().total_minutes);
	director.tick(minutes);
	if (job.on_shift) {
		const bool in_area = world->is_player_in_job_area();
		const Json summary = job.tick(minutes, clock().hour_f(), in_area);
		if (!summary.empty()) on_shift_ended(summary);
	}
	events().needs_changed.emit();
	// Desmaio só durante o jogo em tempo real (não em saltos de tempo, como dormir).
	if (player.needs.fainted) {
		player.needs.fainted = false;
		if (!skipped && world) world->on_player_fainted();
	}
}

void Game::on_shift_ended(const Json& summary) {
	player.inventory.remove_where([](const Json& s) { return jbool(data().item(jstr(s, "id")), "job_item", false); });
	if (jint(summary, "total", 0) > 0) player.add_money(jint(summary, "total"), "Turno no Brasa Burger");
	std::string why;
	const std::string reason = jstr(summary, "reason");
	if (reason == "fechamento")
		why = tr("O drive-thru fechou. Turno encerrado.");
	else if (reason == "abandono")
		why = tr("Você saiu do trabalho no meio do turno.");
	if (!why.empty()) events().notify.emit(why, "info");
	if (world) world->on_shift_ended(summary);
}

Json Game::clock_out_player() {
	Json summary = job.clock_out("normal");
	if (!summary.empty()) on_shift_ended(summary);
	return summary;
}

void Game::on_hour_changed(int hour) {
	if (world == nullptr) return;
	if (hour == 8) {
		const Json fines = law.collect_fines();
		if (!fines.empty()) {
			player.add_money(-jint(fines, "amount"), "Multa");
			std::vector<std::string> reasons;
			for (const auto& r : jarr(fines, "reasons")) reasons.push_back(to_str(r));
			events().request_message.emit(tr("Notificação da Guarda Municipal"),
				fmt(tr("Você recebeu uma multa de R$ %d por: %s.\nTestemunhas registraram a ocorrência."), {jint(fines, "amount"), join(reasons, ", ")}));
		}
	}
}

void Game::on_day_changed(int day) {
	if (world == nullptr || day <= last_day_processed_) return;
	last_day_processed_ = day;
	shops.restock(day);
	delivery.new_day();
	world_state.daily_refill(rng);
	if (clock().weekday() == 6)
		for (const auto& msg : property.charge_rent(player)) events().notify.emit(to_str(msg), "money");
}

// ---------------------------------------------------------------- crimes
// Registra um crime com testemunhas reais (NPCs próximos com linha de visão).
Json Game::report_crime(const std::string& type, const std::string& victim, bool victim_only, const Vec3* pos) {
	Json witnesses = Json::array();
	if (victim_only) {
		if (!victim.empty()) witnesses.push_back(victim);
	} else if (world) {
		const Vec3 p = pos ? *pos : player.position;
		for (const auto& w : world->find_witnesses(p, 14.0)) witnesses.push_back(w);
	}
	Json entry = law.report(type, victim, witnesses, clock().day(), relations);
	if (witnesses.empty()) {
		events().notify.emit(tr("Ninguém viu."), "info");
	} else {
		std::vector<std::string> names;
		for (const auto& w : witnesses) names.push_back(tr(jstr(data().npc(to_str(w)), "name", to_str(w))));
		events().notify.emit(fmt(tr("%s viu o que você fez!"), {join(names, ", ")}), "bad");
		const std::string employer = jstr(data().job("brasa"), "employer");
		if (job.employed && json_array_has(witnesses, Json(employer))) job.fire(tr("Sandra viu você cometendo um crime."));
	}
	return entry;
}

// ---------------------------------------------------------------- veículos
bool Game::is_vehicle_near_spot(const std::string& vehicle_id, const std::string& spot, double radius) const {
	return world ? world->is_vehicle_near_spot(vehicle_id, spot, radius) : false;
}

bool Game::buy_vehicle(const std::string& vehicle_id) {
	if (!world_state.vehicles.contains(vehicle_id)) return false;
	Json& v = world_state.vehicles[vehicle_id];
	if (!has(v, "for_sale")) return false;
	const int price = jint(v["for_sale"], "price");
	if (!player.spend(price, "Compra de veículo")) {
		events().notify.emit(fmt(tr("Dinheiro insuficiente (R$ %d)."), {price}), "bad");
		return false;
	}
	v["owner"] = "player";
	v["locked"] = false;
	v.erase("for_sale");
	if (world) world->refresh_vehicle(vehicle_id);
	events().notify.emit(fmt(tr("Veículo comprado: %s. A chave é sua."), {tr(jstr(data().vehicle(jstr(v, "type")), "name", vehicle_id))}), "good");
	return true;
}

int Game::vehicle_sale_value(const std::string& vehicle_id) const {
	const Json& v = jobj(world_state.vehicles, vehicle_id);
	return static_cast<int>(jnum(data().vehicle(jstr(v, "type")), "price", 800) * 0.5 * (1.0 - jnum(v, "damage", 0.0) / 200.0));
}

int Game::sell_vehicle(const std::string& vehicle_id) {
	if (!world_state.vehicles.contains(vehicle_id)) return 0;
	Json& v = world_state.vehicles[vehicle_id];
	if (jstr(v, "owner") != "player") return 0;
	const int value = vehicle_sale_value(vehicle_id);
	v["owner"] = "rafael";
	v["locked"] = true;
	v["for_sale"] = Json{{"price", static_cast<int>(jnum(data().vehicle(jstr(v, "type")), "price", 800) * 0.8)}, {"seller", "rafael"}};
	player.add_money(value, "Venda de veículo");
	if (world) world->refresh_vehicle(vehicle_id);
	return value;
}

void Game::repair_vehicle(const std::string& vehicle_id, int cost_per_point) {
	if (!world_state.vehicles.contains(vehicle_id)) return;
	Json& v = world_state.vehicles[vehicle_id];
	const int cost = static_cast<int>(std::ceil(jnum(v, "damage", 0.0) * cost_per_point));
	if (cost <= 0) {
		events().notify.emit(tr("O carro não precisa de conserto."), "info");
		return;
	}
	if (!player.spend(cost, "Conserto na oficina")) {
		events().notify.emit(fmt(tr("Dinheiro insuficiente para o conserto (R$ %d)."), {cost}), "bad");
		return;
	}
	v["damage"] = 0.0;
	clock().advance(60.0);
	if (world) world->refresh_vehicle(vehicle_id);
	events().notify.emit(tr("Carro consertado."), "good");
}

// ---------------------------------------------------------------- salvamento
Json Game::to_dict() {
	if (world) world->sync_to_state();
	return Json{
		{"version", SAVE_VERSION},
		{"clock", clock().to_dict()},
		{"player", player.to_dict()},
		{"world", world_state.to_dict()},
		{"quests", quests.to_dict()},
		{"relations", relations.to_dict()},
		{"law", law.to_dict()},
		{"job", job.to_dict()},
		{"shops", shops.to_dict()},
		{"property", property.to_dict()},
		{"delivery", delivery.to_dict()},
		{"director", director.to_dict()},
		{"flags", flags},
	};
}

void Game::from_dict(const Json& d) {
	new_game();
	clock().from_dict(jobj(d, "clock"));
	player.from_dict(jobj(d, "player"));
	world_state.from_dict(jobj(d, "world"));
	quests.from_dict(jobj(d, "quests"));
	relations.from_dict(jobj(d, "relations"));
	law.from_dict(jobj(d, "law"));
	job.from_dict(jobj(d, "job"));
	shops.from_dict(jobj(d, "shops"));
	property.from_dict(jobj(d, "property"));
	delivery.from_dict(jobj(d, "delivery"));
	director.from_dict(jobj(d, "director"));
	flags = jobj(d, "flags");
	last_day_processed_ = clock().day();
}

}  // namespace vila
