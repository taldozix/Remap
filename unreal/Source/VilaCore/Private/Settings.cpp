#include "Vila/Settings.h"

namespace vila {

Settings& settings() {
	static Settings s;
	return s;
}

// Opções gráficas do Unreal (aplicadas pelo módulo do jogo: escalabilidade, Lumen, TSR...).
const Json& Settings::defaults() {
	static const Json d = {
		{"graphics",
			{
				{"resolution", "1920x1080"},
				{"window_mode", 0},        // 0 janela, 1 tela cheia, 2 sem bordas
				{"render_scale", 1.0},     // porcentagem de tela (0.5–1.0)
				{"quality", 2},            // escalabilidade geral: 0 baixa … 3 épica
				{"shadows", 2},
				{"effects", 2},
				{"view_distance", 2},
				{"gi", 1},                 // iluminação global: 0 desligada, 1 Lumen, 2 Lumen + ray tracing por hardware
				{"reflections", 1},        // 0 SSR, 1 Lumen
				{"anti_aliasing", 2},      // 0 nenhum, 1 FXAA, 2 TSR
				{"npc_density", 1.0},
				{"traffic_density", 1.0},
				{"vsync", true},
				{"fps_limit", 60},
				{"grass_density", 2},      // 0 sem grama … 3 alta
				{"tilt_shift", 1},         // profundidade de campo de miniatura: 0 off, 1 sutil, 2 forte
				{"model_detail", 2},       // LOD: 0 baixo … 2 alto
				{"color_grading", true},
				{"motion_blur", false},
				{"volumetric_clouds", true},
			}},
		{"audio", {{"master", 0.8}, {"music", 0.6}, {"sfx", 0.8}, {"voice", 0.8}, {"ambient", 0.7}}},
		{"game",
			{
				{"language", "pt_BR"},
				{"subtitles", true},
				{"text_scale", 1.0},
				{"camera_sensitivity", 1.0},
				{"show_hints", true},
			}},
	};
	return d;
}

// Ações e teclas padrão (nomes de FKey do Unreal). Tudo é remapeável em Opções > Controles.
const Json& Settings::actions() {
	static const Json a = {
		{"move_up", {{"label", "Mover para cima / acelerar"}, {"keys", {"W", "Up", "Gamepad_LeftY+"}}}},
		{"move_down", {{"label", "Mover para baixo / frear e ré"}, {"keys", {"S", "Down"}}}},
		{"move_left", {{"label", "Mover para a esquerda / virar"}, {"keys", {"A", "Left"}}}},
		{"move_right", {{"label", "Mover para a direita / virar"}, {"keys", {"D", "Right"}}}},
		{"run", {{"label", "Correr"}, {"keys", {"LeftShift"}}}},
		{"interact", {{"label", "Interagir"}, {"keys", {"E", "Gamepad_FaceButton_Bottom"}}}},
		{"cycle_target", {{"label", "Alternar alvo de interação"}, {"keys", {"T"}}}},
		{"vehicle", {{"label", "Entrar/sair do veículo"}, {"keys", {"F", "Gamepad_FaceButton_Top"}}}},
		{"handbrake", {{"label", "Freio de mão"}, {"keys", {"SpaceBar"}}}},
		{"horn", {{"label", "Buzina"}, {"keys", {"H"}}}},
		{"inventory", {{"label", "Inventário"}, {"keys", {"Tab"}}}},
		{"journal", {{"label", "Diário (missões, habilidades, relações)"}, {"keys", {"J"}}}},
		{"map", {{"label", "Mapa"}, {"keys", {"M"}}}},
		{"cam_left", {{"label", "Girar câmera (esquerda)"}, {"keys", {"Z"}}}},
		{"cam_right", {{"label", "Girar câmera (direita)"}, {"keys", {"X"}}}},
		{"zoom_in", {{"label", "Aproximar câmera"}, {"keys", {"MouseScrollUp", "Equals"}}}},
		{"zoom_out", {{"label", "Afastar câmera"}, {"keys", {"MouseScrollDown", "Hyphen"}}}},
		{"option_1", {{"label", "Opção de interação 1"}, {"keys", {"One"}}}},
		{"option_2", {{"label", "Opção de interação 2"}, {"keys", {"Two"}}}},
		{"option_3", {{"label", "Opção de interação 3"}, {"keys", {"Three"}}}},
		{"option_4", {{"label", "Opção de interação 4"}, {"keys", {"Four"}}}},
		{"pause", {{"label", "Pausa / voltar"}, {"keys", {"Escape", "Gamepad_Special_Right"}}}},
		{"quick_save", {{"label", "Salvamento rápido"}, {"keys", {"F5"}}}},
		{"debug", {{"label", "Painel de diagnóstico"}, {"keys", {"F3"}}}},
	};
	return a;
}

const std::vector<std::string>& Settings::resolutions() {
	static const std::vector<std::string> r = {"1280x720", "1366x768", "1600x900", "1920x1080", "2560x1440", "3840x2160"};
	return r;
}

const Json& Settings::languages() {
	static const Json l = {{"pt_BR", "Português (Brasil)"}, {"en", "English"}};
	return l;
}

Json Settings::get_value(const std::string& section, const std::string& key) const {
	const Json& v = jget(jobj(data, section), key);
	if (!v.is_null()) return v;
	return jget(jobj(defaults(), section), key);
}

void Settings::set_value(const std::string& section, const std::string& key, const Json& value) {
	if (!data.contains(section)) data[section] = Json::object();
	data[section][key] = value;
	if (section == "game" && key == "language") set_locale(to_str(value));
	changed.emit(section);
}

Json Settings::default_bindings() const {
	Json out = Json::object();
	for (const auto& [action, def] : actions().items()) out[action] = jarr(def, "keys");
	return out;
}

void Settings::load() {
	data = defaults();
	bindings = default_bindings();
	std::string text;
	if (read_text_file(path, text)) {
		const Json saved = Json::parse(text, nullptr, false);
		if (!saved.is_discarded() && saved.is_object()) {
			// só aceita chaves conhecidas com tipo compatível
			for (const auto& [section, keys] : defaults().items())
				for (const auto& [key, def] : keys.items()) {
					const Json& v = jget(jobj(saved, section), key);
					if (v.is_null()) continue;
					if ((v.is_number() && def.is_number()) || v.type() == def.type()) data[section][key] = v;
				}
			for (const auto& [action, _] : actions().items()) {
				const Json& keys = jget(jobj(saved, "input"), action);
				if (keys.is_array()) bindings[action] = keys;
			}
		}
	}
	set_locale(to_str(get_value("game", "language")));
}

bool Settings::save() const {
	Json out = data;
	out["input"] = bindings;
	return write_text_file(path, out.dump(1, '\t'));
}

void Settings::reset_section(const std::string& section) {
	if (section == "input")
		bindings = default_bindings();
	else
		data[section] = jobj(defaults(), section);
	if (section == "game") set_locale(to_str(get_value("game", "language")));
	changed.emit(section);
}

// Substitui a tecla principal de uma ação (mantém as secundárias) e tira o mesmo atalho
// das outras ações para evitar conflito.
void Settings::rebind(const std::string& action, const std::string& key) {
	if (key.empty()) return;
	Json list = jarr(bindings, action);
	if (list.empty())
		list.push_back(key);
	else
		list[0] = key;
	for (auto& [other, keys] : bindings.items()) {
		if (other == action) continue;
		Json filtered = Json::array();
		for (const auto& k : keys)
			if (to_str(k) != key) filtered.push_back(k);
		keys = filtered;
	}
	bindings[action] = list;
	changed.emit("input");
}

static std::string key_text(const std::string& k) {
	static const Json names = {{"MouseScrollUp", "Roda ↑"}, {"MouseScrollDown", "Roda ↓"}, {"LeftMouseButton", "Mouse esq."},
		{"RightMouseButton", "Mouse dir."}, {"MiddleMouseButton", "Mouse meio"}, {"SpaceBar", "Espaço"}, {"LeftShift", "Shift"},
		{"Escape", "Esc"}, {"One", "1"}, {"Two", "2"}, {"Three", "3"}, {"Four", "4"}, {"Equals", "="}, {"Hyphen", "-"},
		{"Up", "↑"}, {"Down", "↓"}, {"Left", "←"}, {"Right", "→"}};
	return has(names, k) ? tr(jstr(names, k)) : k;
}

std::string Settings::binding_text(const std::string& action) const {
	const Json& list = jarr(bindings, action);
	if (list.empty()) return "—";
	std::vector<std::string> parts;
	for (const auto& k : list)
		if (to_str(k).rfind("Gamepad", 0) != 0) parts.push_back(key_text(to_str(k)));
	return join(parts, " / ");
}

std::string Settings::primary_key_text(const std::string& action) const {
	const Json& list = jarr(bindings, action);
	return list.empty() ? "?" : key_text(to_str(list[0]));
}

double Settings::text_scale() const { return clampf(to_num(get_value("game", "text_scale"), 1.0), 0.75, 1.6); }

}  // namespace vila
