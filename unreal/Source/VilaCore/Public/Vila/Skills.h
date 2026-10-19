// Habilidades que evoluem com a prática. Cada nível tem efeito mecânico real
// (velocidade na cozinha, chance de persuasão, controle do carro, fôlego).
#pragma once
#include "Vila/Core.h"

namespace vila {

class Skills {
public:
	static const Json DEFS;                    // id -> {"name", "desc"}
	static const std::vector<int> THRESHOLDS;  // xp mínimo por nível (0..10)

	Json xp = Json::object();  // id -> float

	int level(const std::string& id) const;
	double progress(const std::string& id) const;
	void add_xp(const std::string& id, double amount);
	Json to_dict() const;
	void from_dict(const Json& d);
};

}  // namespace vila
