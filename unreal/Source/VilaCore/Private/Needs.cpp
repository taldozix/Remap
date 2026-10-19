#include "Vila/Needs.h"

#include "Vila/Events.h"

namespace vila {

const std::vector<std::string> Needs::IDS = {"fome", "sede", "sono", "fadiga", "higiene"};
const Json Needs::LABELS = Json{{"fome", "Fome"}, {"sede", "Sede"}, {"sono", "Sono"}, {"fadiga", "Fadiga"}, {"higiene", "Higiene"}};
const Json Needs::RATES = Json{
	{"fome", {{"default", 4.5}, {"sleep", 2.0}, {"run", 6.0}, {"work", 5.5}}},
	{"sede", {{"default", 6.5}, {"sleep", 2.5}, {"run", 12.0}, {"work", 8.0}}},
	{"sono", {{"default", 4.2}, {"sleep", -12.5}, {"rest", 3.0}, {"work", 5.0}}},
	{"fadiga", {{"default", -15.0}, {"walk", -6.0}, {"run", 60.0}, {"work", 12.0}, {"rest", -45.0}, {"sleep", -60.0}, {"drive", -8.0}}},
	{"higiene", {{"default", 1.5}, {"run", 6.0}, {"work", 6.0}, {"sleep", 1.0}}},
};

double Needs::get_need(const std::string& id) const { return jnum(v, id, 0.0); }

void Needs::set_need(const std::string& id, double value) { v[id] = clampf(value, 0.0, 100.0); }

void Needs::tick(double hours, const std::string& activity, int fitness, const Json& env) {
	if (hours <= 0.0) return;
	for (const auto& id : IDS) {
		const Json& table = RATES[id];
		double rate = jnum(table, activity, to_num(table["default"]));
		if (rate > 0.0) rate *= jnum(mods, id, 1.0);
		if (id == "fadiga" && activity == "run") rate *= (1.0 - 0.05 * fitness) * jnum(mods, "run_fatigue", 1.0);
		if (id == "sede" && body_temp > 37.5) rate *= 1.5;
		if (id == "fadiga" && body_temp < 35.6 && rate > -1.0) rate += 6.0;
		set_need(id, get_need(id) + rate * hours);
	}
	tick_temperature(hours, activity, env);
	dor = std::max(0.0, dor - (activity == "sleep" ? 8.0 : 4.0) * hours);
	// Saúde: penalidades graduais por negligência; recuperação lenta quando bem cuidado.
	double dmg = 0.0;
	if (get_need("fome") >= 100.0) dmg += 6.0;
	if (get_need("sede") >= 100.0) dmg += 9.0;
	if (nausea_hours > 0.0) {
		dmg += 3.0;
		nausea_hours = std::max(0.0, nausea_hours - hours);
	}
	if (body_temp < 35.0 || body_temp > 38.3) dmg += 3.0;
	if (dmg > 0.0)
		saude -= dmg * hours;
	else if (get_need("fome") < 70.0 && get_need("sede") < 70.0)
		saude += (activity == "sleep" ? 6.0 : 2.0) * hours;
	saude = clampf(saude, 0.0, 100.0);
	if (saude <= 0.0 || get_need("sono") >= 100.0) fainted = true;
}

// Roupa molhada esfria; correr no calor esquenta.
void Needs::tick_temperature(double hours, const std::string& activity, const Json& env) {
	if (env.is_null() || env.empty()) return;
	const bool outdoors = jbool(env, "outdoors", false);
	const bool raining = jbool(env, "raining", false);
	if (outdoors && raining && !jbool(env, "rain_protect", false))
		wetness = std::min(100.0, wetness + 140.0 * hours);
	else
		wetness = std::max(0.0, wetness - (!outdoors ? 70.0 : 35.0) * hours);
	const double ambient = jnum(env, "ambient", 24.0);
	const double warmth = jnum(env, "warmth", 0.0);
	double heat = 0.0;
	if (activity == "run")
		heat = 1.2;
	else if (activity == "work")
		heat = 0.4;
	else if (activity == "sleep")
		heat = warmth > 0.0 ? 0.3 : 0.0;
	const double balance = (ambient + warmth - 21.0) / 6.0 - wetness / 60.0 + heat;
	const double target = BODY_NORMAL + clampf(balance, -3.2, 2.2) * 0.5;
	body_temp = move_toward(body_temp, target, 1.0 * hours);
}

void Needs::consume(const Json& effects) {
	if (!effects.is_object()) return;
	for (const auto& [k, val] : effects.items()) {
		if (k == "saude") {
			saude = clampf(saude + to_num(val), 0.0, 100.0);
		} else if (k == "nausea") {
			if (!jbool(mods, "no_nausea", false)) nausea_hours += to_num(val);
		} else if (k == "dor") {
			add_pain(to_num(val));
		} else if (k == "temperatura") {
			body_temp = clampf(body_temp + to_num(val), 33.0, 40.0);
		} else if (vila::has(v, k)) {
			set_need(k, get_need(k) + to_num(val));
		}
	}
}

void Needs::add_pain(double amount) {
	if (amount > 0.0) amount *= jnum(mods, "dor", 1.0);
	dor = clampf(dor + amount, 0.0, 100.0);
}

double Needs::speed_multiplier() const {
	double m = 1.0;
	m -= std::max(0.0, get_need("fadiga") - 60.0) / 100.0 * 0.6;
	m -= std::max(0.0, get_need("sono") - 75.0) / 100.0 * 0.6;
	m -= std::max(0.0, get_need("sede") - 80.0) / 100.0 * 0.5;
	m -= std::max(0.0, dor - 40.0) / 100.0 * 0.5;
	if (body_temp < 35.3) m -= 0.1;
	if (saude < 30.0) m -= 0.2;
	return clampf(m, 0.45, 1.0);
}

double Needs::work_multiplier() const {
	double m = 1.0;
	m += std::max(0.0, get_need("fadiga") - 50.0) / 100.0;
	m += std::max(0.0, get_need("sono") - 60.0) / 80.0;
	m += std::max(0.0, get_need("fome") - 75.0) / 100.0;
	m += std::max(0.0, dor - 30.0) / 140.0;
	if (body_temp < 35.6) m += 0.15;
	return m;
}

bool Needs::can_run() const { return get_need("fadiga") < 85.0 && saude > 15.0 && dor < 75.0; }

Json Needs::moodles() const {
	Json out = Json::array();
	add_moodle(out, "fome", {{50, "Com fome"}, {75, "Faminto"}, {95, "Morrendo de fome"}});
	add_moodle(out, "sede", {{50, "Com sede"}, {75, "Muita sede"}, {95, "Desidratado"}});
	add_moodle(out, "sono", {{60, "Sonolento"}, {80, "Muito sono"}, {95, "Exausto de sono"}});
	add_moodle(out, "fadiga", {{50, "Cansado"}, {75, "Esgotado"}, {90, "No limite"}});
	add_moodle(out, "higiene", {{50, "Suado"}, {70, "Mal cheiroso"}, {90, "Fedendo"}});
	if (saude < 80.0) {
		const int lvl = saude >= 50.0 ? 1 : (saude >= 25.0 ? 2 : 3);
		static const char* labels[] = {"Machucado", "Ferido", "Em estado grave"};
		out.push_back(Json{{"id", "saude"}, {"label", labels[lvl - 1]}, {"level", lvl}});
	}
	if (dor >= 20.0)
		out.push_back(Json{{"id", "dor"}, {"label", dor < 50.0 ? "Dor" : "Dor forte"}, {"level", dor < 50.0 ? 1 : 2}});
	if (body_temp < 35.9)
		out.push_back(Json{{"id", "frio"}, {"label", body_temp >= 35.3 ? "Com frio" : "Hipotermia"}, {"level", body_temp >= 35.3 ? 1 : 3}});
	else if (body_temp > 37.5)
		out.push_back(Json{{"id", "calor"}, {"label", body_temp <= 38.0 ? "Com calor" : "Superaquecido"}, {"level", body_temp <= 38.0 ? 1 : 3}});
	if (wetness >= 40.0) out.push_back(Json{{"id", "molhado"}, {"label", wetness < 80.0 ? "Molhado" : "Encharcado"}, {"level", 1}});
	if (nausea_hours > 0.0) out.push_back(Json{{"id", "nausea"}, {"label", "Enjoado"}, {"level", 2}});
	return out;
}

void Needs::add_moodle(Json& out, const std::string& id, const std::vector<std::pair<double, const char*>>& thresholds) const {
	const double value = get_need(id);
	int lvl = 0;
	std::string label;
	for (size_t i = 0; i < thresholds.size(); ++i) {
		if (value >= thresholds[i].first) {
			lvl = static_cast<int>(i) + 1;
			label = thresholds[i].second;
		}
	}
	if (lvl > 0) out.push_back(Json{{"id", id}, {"label", label}, {"level", lvl}});
}

Json Needs::to_dict() const {
	return Json{{"v", v}, {"saude", saude}, {"nausea", nausea_hours}, {"body_temp", body_temp}, {"wetness", wetness}, {"dor", dor}};
}

void Needs::from_dict(const Json& d) {
	const Json& src = jobj(d, "v");
	for (const auto& id : IDS) v[id] = clampf(jnum(src, id, get_need(id)), 0.0, 100.0);
	saude = clampf(jnum(d, "saude", 100.0), 0.0, 100.0);
	nausea_hours = jnum(d, "nausea", 0.0);
	body_temp = clampf(jnum(d, "body_temp", BODY_NORMAL), 33.0, 40.0);
	wetness = clampf(jnum(d, "wetness", 0.0), 0.0, 100.0);
	dor = clampf(jnum(d, "dor", 0.0), 0.0, 100.0);
	fainted = false;
}

}  // namespace vila
