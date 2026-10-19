#include "Vila/DriveThruJob.h"

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Inventory.h"
#include "Vila/Needs.h"

namespace vila {

DriveThruJob::DriveThruJob() { reset_stations(); }

const Json& DriveThruJob::cfg() const { return data().job(JOB_ID); }

std::string DriveThruJob::rank_title(int r) const {
	const Json& ranks = jarr(cfg(), "ranks");
	if (ranks.empty()) return "";
	const int i = r < 0 ? rank : r;
	return tr(jstr(ranks[static_cast<size_t>(clampi(i, 0, static_cast<int>(ranks.size()) - 1))], "title"));
}

int DriveThruJob::wage() const {
	const Json& ranks = jarr(cfg(), "ranks");
	if (ranks.empty()) return 0;
	return jint(ranks[static_cast<size_t>(clampi(rank, 0, static_cast<int>(ranks.size()) - 1))], "wage");
}

bool DriveThruJob::is_open(double hour_f) const {
	const Json h = has(cfg(), "hours") ? cfg()["hours"] : Json::array({10, 22});
	return Clock::is_hour_in(hour_f, to_num(h[0]), to_num(h[1]));
}

void DriveThruJob::reset_stations() {
	stations = Json::object();
	for (const auto& [s, _] : jobj(cfg(), "stations").items()) stations[s] = {{"state", "idle"}, {"t", 0.0}};
}

void DriveThruJob::hire() {
	if (fired) return;
	employed = true;
	events().notify.emit(fmt(tr("Você foi contratado(a) como %s! Salário: R$ %d/hora."), {rank_title(), wage()}), "good");
	events().job_updated.emit();
}

Json DriveThruJob::clock_in(double hour_f, Needs& needs) {
	if (!employed) return {{"ok", false}, {"reason", tr("Você não trabalha aqui.")}};
	if (on_shift) return {{"ok", false}, {"reason", tr("Você já está em turno.")}};
	if (!is_open(hour_f)) return {{"ok", false}, {"reason", tr("O drive-thru está fechado agora (10h às 22h).")}};
	if (needs.get_need("higiene") >= 75.0)
		return {{"ok", false}, {"reason", tr("Sandra: \"Assim não dá. Vai tomar um banho e volta.\"")}};
	on_shift = true;
	shift = {{"minutes", 0.0}, {"ok", 0}, {"failed", 0}, {"partial", 0}, {"tips", 0}, {"bonus", 0}, {"away", 0.0}};
	orders = Json::array();
	finished = Json::object();
	customers_requested = 0;
	customer_cooldown = 6.0;
	reset_stations();
	events().notify.emit(tr("Ponto batido. Bom trabalho!"), "info");
	events().job_updated.emit();
	return {{"ok", true}};
}

Json DriveThruJob::clock_out(const std::string& reason) {
	if (!on_shift) return Json::object();
	on_shift = false;
	const double hours = jnum(shift, "minutes") / 60.0;
	int wage_pay = static_cast<int>(std::round((hours + jnum(shift, "extra", 0.0) / 60.0) * wage()));
	if (reason == "abandono") {
		wage_pay = static_cast<int>(wage_pay * 0.5);
		add_warning(tr("abandonou o turno"));
	}
	const int total = wage_pay + jint(shift, "bonus") + jint(shift, "tips");
	if (hours >= 1.0) shifts_done += 1;
	Json summary = {
		{"reason", reason}, {"hours", hours}, {"wage", wage_pay}, {"bonus", jint(shift, "bonus")},
		{"tips", jint(shift, "tips")}, {"total", total}, {"ok", jint(shift, "ok")}, {"failed", jint(shift, "failed")},
		{"promoted", false},
	};
	orders = Json::array();
	customers_requested = 0;
	reset_stations();
	if (check_promotion()) summary["promoted"] = true;
	events().job_updated.emit();
	return summary;
}

void DriveThruJob::add_warning(const std::string& why) {
	warnings += 1;
	events().notify.emit(fmt(tr("Advertência no trabalho (%d/%d): %s"), {warnings, jint(cfg(), "max_warnings", 3), why}), "bad");
	if (warnings >= jint(cfg(), "max_warnings", 3)) fire(tr("Advertências demais."));
}

void DriveThruJob::fire(const std::string& why) {
	if (!employed) return;
	if (on_shift) {
		on_shift = false;
		orders = Json::array();
	}
	employed = false;
	fired = true;
	events().notify.emit(fmt(tr("Você foi demitido(a) do Brasa Burger. %s"), {why}), "bad");
	events().job_updated.emit();
}

bool DriveThruJob::check_promotion() {
	const Json& ranks = jarr(cfg(), "ranks");
	if (rank + 1 >= static_cast<int>(ranks.size())) return false;
	const Json& nxt = ranks[static_cast<size_t>(rank + 1)];
	if (performance >= jnum(nxt, "min_perf", 999) && total_orders >= jint(nxt, "min_orders", 999)) {
		rank += 1;
		events().notify.emit(fmt(tr("Promoção! Agora você é %s (R$ %d/hora)."), {rank_title(), wage()}), "good");
		return true;
	}
	return false;
}

Json DriveThruJob::tick(double minutes, double hour_f, bool player_in_area) {
	if (!on_shift) return Json::object();
	if (!is_open(hour_f)) return clock_out("fechamento");
	shift["minutes"] = jnum(shift, "minutes") + minutes;
	if (game().director.wage_multiplier() > 1.0)
		shift["extra"] = jnum(shift, "extra", 0.0) + minutes * (game().director.wage_multiplier() - 1.0);
	for (auto& [s, st] : stations.items()) {
		const std::string state = jstr(st, "state");
		if (state == "cooking" || state == "ready") {
			st["t"] = jnum(st, "t") + minutes;
			const Json& c = jobj(jobj(cfg(), "stations"), s);
			if (jstr(st, "state") == "cooking" && jnum(st, "t") >= jnum(st, "cook")) st["state"] = "ready";
			if (jstr(st, "state") == "ready" && jnum(st, "t") >= jnum(st, "cook") + jnum(st, "window")) {
				st["state"] = "burnt";
				events().notify.emit(fmt(tr("%s queimou!"), {tr(jstr(c, "label", s))}), "bad");
			}
		}
	}
	// Cópia rasa como no GDScript: os pedidos são os mesmos (atualiza também o original).
	Json snapshot = orders;
	for (auto& o : snapshot) {
		o["patience"] = jnum(o, "patience") - minutes;
		for (auto& real : orders)
			if (real["id"] == o["id"]) real["patience"] = o["patience"];
		if (jnum(o, "patience") <= 0.0) fail_order(o);
	}
	customer_cooldown -= minutes;
	if (player_in_area) {
		shift["away"] = 0.0;
	} else {
		shift["away"] = jnum(shift, "away") + minutes;
		if (jnum(shift, "away") >= jnum(cfg(), "abandon_minutes", 15)) return clock_out("abandono");
	}
	return Json::object();
}

bool DriveThruJob::wants_customer() const {
	return on_shift && customer_cooldown <= 0.0 &&
		static_cast<int>(orders.size()) + customers_requested < jint(cfg(), "max_queue", 3);
}

void DriveThruJob::customer_dispatched(Rng& rng) {
	customers_requested += 1;
	const Json iv = has(cfg(), "customer_interval") ? cfg()["customer_interval"] : Json::array({30, 55});
	customer_cooldown = rng.randf_range(to_num(iv[0]), to_num(iv[1]));
}

Json DriveThruJob::create_order(const std::string& customer_id, Rng& rng) {
	customers_requested = std::max(0, customers_requested - 1);
	const Json menu = has(cfg(), "menu") ? cfg()["menu"] : Json::array({"job_x_brasa", "job_batata", "job_refri"});
	Json items = Json::object();
	const int n = rng.randi_range(1, 3);
	for (int i = 0; i < n; ++i) {
		const std::string it = to_str(menu[rng.randi() % menu.size()]);
		items[it] = jint(items, it, 0) + 1;
	}
	const double patience = jnum(cfg(), "patience", 80);
	Json o = {{"id", next_order_id}, {"customer", customer_id}, {"items", items}, {"remaining", items},
		{"patience", patience}, {"max_patience", patience}};
	next_order_id += 1;
	orders.push_back(o);
	events().notify.emit(fmt(tr("Novo pedido #%d na janela: %s"), {o["id"], order_text(items)}), "quest");
	events().job_updated.emit();
	return o;
}

std::string DriveThruJob::order_text(const Json& items) {
	std::vector<std::string> parts;
	for (const auto& [k, v] : items.items()) parts.push_back(fmt("%dx %s", {to_int(v), data().item_name(k)}));
	return join(parts, ", ");
}

Json DriveThruJob::order_for(const std::string& customer_id) const {
	for (const auto& o : orders)
		if (jstr(o, "customer") == customer_id) return o;
	return Json::object();
}

Json DriveThruJob::front_order() const { return orders.empty() ? Json::object() : orders[0]; }

std::string DriveThruJob::status_of(const std::string& customer_id) const {
	if (has(finished, customer_id)) return to_str(finished[customer_id]);
	return !order_for(customer_id).empty() ? "waiting" : "none";
}

// --------------------------------------------------------------- estações
double DriveThruJob::station_cook_time(const std::string& s, int cooking_lvl) const {
	const Json& c = jobj(jobj(cfg(), "stations"), s);
	return std::max(2.0, jnum(c, "cook") + jnum(c, "cook_per_level", 0.0) * cooking_lvl);
}

double DriveThruJob::station_window(const std::string& s, int cooking_lvl) const {
	const Json& c = jobj(jobj(cfg(), "stations"), s);
	return jnum(c, "window") + jnum(c, "window_per_level", 0.0) * cooking_lvl;
}

bool DriveThruJob::station_start(const std::string& s, int cooking_lvl) {
	if (!on_shift || !has(stations, s) || jstr(stations[s], "state") != "idle") return false;
	stations[s] = {{"state", "cooking"}, {"t", 0.0}, {"cook", station_cook_time(s, cooking_lvl)},
		{"window", station_window(s, cooking_lvl)}};
	return true;
}

std::string DriveThruJob::station_collect(const std::string& s, Inventory& inv) {
	if (!has(stations, s)) return "";
	const std::string state = jstr(stations[s], "state");
	if (state == "ready") {
		const std::string product = jstr(jobj(jobj(cfg(), "stations"), s), "product");
		inv.add(product, 1, 0.0, false);
		stations[s] = {{"state", "idle"}, {"t", 0.0}};
		return product;
	}
	if (state == "burnt") {
		stations[s] = {{"state", "idle"}, {"t", 0.0}};
		performance = std::max(0.0, performance - 1.0);
		return "burnt";
	}
	return "";
}

double DriveThruJob::station_progress(const std::string& s) const {
	const Json& st = jobj(stations, s);
	if (jstr(st, "state", "idle") == "cooking") return clampf(jnum(st, "t") / jnum(st, "cook"), 0.0, 1.0);
	const std::string state = jstr(st, "state", "");
	return (state == "ready" || state == "burnt") ? 1.0 : 0.0;
}

// --------------------------------------------------------------- entrega
Json DriveThruJob::deliver(Inventory& inv, int social_lvl) {
	if (orders.empty()) return {{"status", "none"}};
	Json& o = orders[0];
	Json given = Json::object();
	std::vector<std::string> keys;
	for (const auto& [k, _] : o["remaining"].items()) keys.push_back(k);
	for (const auto& k : keys) {
		const int need = to_int(o["remaining"][k]);
		const int got = inv.remove(k, need);
		if (got > 0) {
			given[k] = got;
			o["remaining"][k] = need - got;
			if (to_int(o["remaining"][k]) <= 0) o["remaining"].erase(k);
		}
	}
	if (given.empty()) return {{"status", "nothing"}, {"order", o}};
	if (!o["remaining"].empty()) {
		shift["partial"] = jint(shift, "partial") + 1;
		performance = std::max(0.0, performance - 1.0);
		return {{"status", "partial"}, {"order", o}, {"given", given}};
	}
	// Pedido completo: bônus, gorjeta conforme rapidez e habilidade social.
	const double speed = jnum(o, "patience") / jnum(o, "max_patience");
	int tip = 0;
	if (speed >= 0.6)
		tip = 4 + social_lvl;
	else if (speed >= 0.3)
		tip = 2 + social_lvl / 2;
	shift["ok"] = jint(shift, "ok") + 1;
	shift["bonus"] = jint(shift, "bonus") + jint(cfg(), "order_bonus", 2);
	shift["tips"] = jint(shift, "tips") + tip;
	total_orders += 1;
	performance = clampf(performance + (speed >= 0.3 ? 3.0 : 1.0), 0.0, 100.0);
	const Json done = o;  // cópia antes de remover da lista
	orders.erase(orders.begin());
	finished[jstr(done, "customer")] = "done";
	events().job_updated.emit();
	return {{"status", "complete"}, {"order", done}, {"tip", tip}};
}

void DriveThruJob::fail_order(const Json& o) {
	for (auto it = orders.begin(); it != orders.end(); ++it) {
		if ((*it)["id"] == o["id"]) {
			orders.erase(it);
			break;
		}
	}
	finished[jstr(o, "customer")] = "failed";
	shift["failed"] = jint(shift, "failed") + 1;
	performance = std::max(0.0, performance - 6.0);
	events().notify.emit(fmt(tr("O cliente do pedido #%d desistiu e foi embora."), {o["id"]}), "bad");
	if (performance < 15.0) fire(tr("Desempenho muito baixo."));
	events().job_updated.emit();
}

void DriveThruJob::customer_left(const std::string& customer_id) { finished.erase(customer_id); }

Json DriveThruJob::to_dict() const {
	return {
		{"employed", employed}, {"fired", fired}, {"rank", rank}, {"performance", performance},
		{"warnings", warnings}, {"total_orders", total_orders}, {"shifts_done", shifts_done},
	};
}

// Turnos não são salvos no meio: ao carregar, o jogador está fora de turno.
void DriveThruJob::from_dict(const Json& d) {
	employed = jbool(d, "employed", false);
	fired = jbool(d, "fired", false);
	rank = jint(d, "rank", 0);
	performance = jnum(d, "performance", 50.0);
	warnings = jint(d, "warnings", 0);
	total_orders = jint(d, "total_orders", 0);
	shifts_done = jint(d, "shifts_done", 0);
	on_shift = false;
	orders = Json::array();
	finished = Json::object();
	reset_stations();
}

}  // namespace vila
