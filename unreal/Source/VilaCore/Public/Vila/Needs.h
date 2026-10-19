// Necessidades e condição física do personagem (lógica pura, testável sem cena).
// Valores de 0 (ótimo) a 100 (crítico). Saúde vai de 0 (desmaio) a 100.
// Também simula temperatura corporal (°C), umidade da roupa (chuva) e dor.
#pragma once
#include "Vila/Core.h"

namespace vila {

class Needs {
public:
	static const std::vector<std::string> IDS;
	static const Json LABELS;
	// Variação por hora de jogo conforme a atividade.
	static const Json RATES;
	static constexpr double BODY_NORMAL = 36.6;

	Json v = Json{{"fome", 15.0}, {"sede", 15.0}, {"sono", 10.0}, {"fadiga", 0.0}, {"higiene", 5.0}};
	double saude = 100.0;
	double nausea_hours = 0.0;  // comida estragada
	double body_temp = BODY_NORMAL;
	double wetness = 0.0;       // 0 seco … 100 encharcado
	double dor = 0.0;           // 0 sem dor … 100 dor intensa
	bool fainted = false;       // sinaliza ao jogo que o personagem desmaiou
	// Modificadores de traços: {"fome": 1.15, "run_fatigue": 0.75, "no_nausea": true, ...}
	Json mods = Json::object();

	double get_need(const std::string& id) const;
	void set_need(const std::string& id, double value);
	// Avança a simulação. `fitness` (nível de condicionamento 0-10) reduz o desgaste da corrida.
	// `env`: {"ambient": °C, "outdoors": bool, "raining": bool, "warmth": float, "rain_protect": bool}
	void tick(double hours, const std::string& activity, int fitness = 0, const Json& env = Json::object());
	// Aplica efeitos de consumo: {"fome": -20, "saude": 10, "nausea": 4, "dor": -50}
	void consume(const Json& effects);
	void add_pain(double amount);
	// Multiplicador de velocidade de locomoção (cansaço, sono, sede, dor e frio pesam).
	double speed_multiplier() const;
	// Multiplicador de duração de tarefas (>1 = mais lento). Exaustão e dor atrapalham.
	double work_multiplier() const;
	bool can_run() const;
	// "Moodles": só aparecem quando relevantes. Array de {"id", "label", "level"}
	// (rótulos em português sem tr(), como no GDScript; a interface traduz).
	Json moodles() const;
	Json to_dict() const;
	void from_dict(const Json& d);

private:
	// Temperatura corporal tende a um alvo que depende do clima, da roupa, da chuva e do esforço.
	void tick_temperature(double hours, const std::string& activity, const Json& env);
	void add_moodle(Json& out, const std::string& id, const std::vector<std::pair<double, const char*>>& thresholds) const;
};

}  // namespace vila
