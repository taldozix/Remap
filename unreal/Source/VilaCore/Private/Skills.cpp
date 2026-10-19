#include "Vila/Skills.h"

#include "Vila/Events.h"

namespace vila {

const Json Skills::DEFS = Json{
	{"culinaria", {{"name", "Culinária"}, {"desc", "Prepara lanches mais rápido e com menos erros."}}},
	{"social", {{"name", "Social"}, {"desc", "Aumenta a chance de persuadir e as gorjetas."}}},
	{"direcao", {{"name", "Direção"}, {"desc", "Melhora o controle do veículo e reduz danos em batidas."}}},
	{"condicionamento", {{"name", "Condicionamento"}, {"desc", "Cansa menos ao correr e corre mais rápido."}}},
	{"mecanica", {{"name", "Mecânica"}, {"desc", "Conserta mais dano do carro por kit de reparo e mais rápido."}}},
	{"primeiros_socorros", {{"name", "Primeiros socorros"}, {"desc", "Curativos tratam mais saúde e dor; permite socorrer feridos."}}},
};
const std::vector<int> Skills::THRESHOLDS = {0, 50, 130, 250, 420, 650, 950, 1350, 1850, 2500, 3300};

int Skills::level(const std::string& id) const {
	const double x = jnum(xp, id, 0.0);
	int lvl = 0;
	for (size_t i = 0; i < THRESHOLDS.size(); ++i)
		if (x >= THRESHOLDS[i]) lvl = static_cast<int>(i);
	return lvl;
}

double Skills::progress(const std::string& id) const {
	const int lvl = level(id);
	if (lvl >= static_cast<int>(THRESHOLDS.size()) - 1) return 1.0;
	const double x = jnum(xp, id, 0.0);
	return (x - THRESHOLDS[lvl]) / static_cast<double>(THRESHOLDS[lvl + 1] - THRESHOLDS[lvl]);
}

void Skills::add_xp(const std::string& id, double amount) {
	if (!has(DEFS, id) || amount <= 0.0) return;
	const int before = level(id);
	xp[id] = jnum(xp, id, 0.0) + amount;
	const int after = level(id);
	if (after > before) {
		events().skill_level_up.emit(id, after);
		events().notify.emit(fmt(tr("%s subiu para o nível %d!"), {tr(jstr(DEFS[id], "name")), after}), "skill");
	}
}

Json Skills::to_dict() const { return Json{{"xp", xp}}; }

void Skills::from_dict(const Json& d) {
	xp = Json::object();
	for (const auto& [k, val] : jobj(d, "xp").items())
		if (has(DEFS, k)) xp[k] = to_num(val);
}

}  // namespace vila
