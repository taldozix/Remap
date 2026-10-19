// Configurações persistentes (gráficos, áudio, jogo, idioma, controles), salvas em JSON.
// O núcleo guarda e valida; o host (Unreal) aplica (escalabilidade, volumes, teclas).
#pragma once
#include "Vila/Core.h"

namespace vila {

class VILACORE_API Settings {
public:
	Signal<std::string> changed;  // seção alterada ("graphics", "audio", "game", "input")

	static const Json& defaults();
	// ação -> {"label": ..., "keys": [nomes de tecla do Unreal, ex.: "W", "Up", "MouseScrollUp"]}
	static const Json& actions();
	static const std::vector<std::string>& resolutions();
	static const Json& languages();

	std::string path = "settings.json";
	Json data = Json::object();
	Json bindings = Json::object();  // ação -> [nomes de tecla]

	Json get_value(const std::string& section, const std::string& key) const;
	void set_value(const std::string& section, const std::string& key, const Json& value);
	void load();
	bool save() const;
	void reset_section(const std::string& section);
	void rebind(const std::string& action, const std::string& key);  // troca a tecla principal
	std::string binding_text(const std::string& action) const;
	std::string primary_key_text(const std::string& action) const;
	double text_scale() const;
	Json default_bindings() const;
};

VILACORE_API Settings& settings();

}  // namespace vila
