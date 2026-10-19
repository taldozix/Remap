#include "Vila/Saves.h"

#include "Vila/Clock.h"
#include "Vila/Game.h"

namespace vila {

Saves& saves() {
	static Saves s;
	return s;
}

const std::vector<std::string>& Saves::slots() {
	static const std::vector<std::string> s = {"auto", "rapido", "slot1", "slot2", "slot3"};
	return s;
}

std::string Saves::slot_label(const std::string& slot) {
	static const Json labels = {{"auto", "Automático"}, {"rapido", "Rápido"}, {"slot1", "Espaço 1"}, {"slot2", "Espaço 2"}, {"slot3", "Espaço 3"}};
	return tr(jstr(labels, slot, slot));
}

// Motivo pelo qual não se pode salvar agora (vazio = pode).
std::string Saves::block_reason() const {
	if (game().world == nullptr) return tr("Nenhuma partida em andamento.");
	if (game().job.on_shift) return tr("Não é possível salvar durante um turno de trabalho.");
	return game().world->save_block_reason();
}

bool Saves::save_game(const std::string& slot) {
	last_error.clear();
	make_dirs(dir);
	const Json payload = {
		{"meta",
			{
				{"saved_at", datetime_string()},
				{"unix", unix_time()},
				{"day", clock().day()},
				{"time", clock().time_string()},
				{"weekday", clock().weekday_name()},
				{"money", game().player.money},
				{"version", Game::SAVE_VERSION},
			}},
		{"data", game().to_dict()},
	};
	const std::string path = path_for(slot);
	const std::string tmp = path + ".tmp";
	if (!write_text_file(tmp, payload.dump(1, '\t'))) {
		last_error = tr("Não foi possível escrever o arquivo.");
		return false;
	}
	if (file_exists(path)) {
		copy_file(path, path + ".bak");
		remove_file(path);
	}
	if (!rename_file(tmp, path)) {
		last_error = tr("Falha ao finalizar o salvamento.");
		return false;
	}
	return true;
}

Json Saves::read_meta(const std::string& slot) const {
	const Json r = read(path_for(slot));
	return jbool(r, "ok") ? jobj(r, "meta") : Json::object();
}

// Lê e valida um salvamento. Tenta o backup se o principal estiver corrompido.
Json Saves::load_data(const std::string& slot) {
	last_error.clear();
	Json r = read(path_for(slot));
	if (!jbool(r, "ok")) {
		Json bak = read(path_for(slot) + ".bak");
		if (jbool(bak, "ok")) {
			push_warning("Salvamento corrompido; usando backup.");
			r = bak;
		} else {
			last_error = jstr(r, "error", tr("Arquivo inválido."));
			return Json::object();
		}
	}
	return r["data"];
}

Json Saves::read(const std::string& path) const {
	std::string text;
	if (!file_exists(path) || !read_text_file(path, text)) return Json{{"ok", false}, {"error", tr("Salvamento não encontrado.")}};
	Json parsed = Json::parse(text, nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object() || !jget(parsed, "data").is_object())
		return Json{{"ok", false}, {"error", tr("Arquivo de salvamento corrompido.")}};
	Json d = parsed["data"];
	const int version = jint(d, "version", 0);
	if (version > Game::SAVE_VERSION) return Json{{"ok", false}, {"error", tr("Salvamento de uma versão mais nova do jogo.")}};
	d = migrate(d, version);
	if (!has(d, "player") || !has(d, "clock")) return Json{{"ok", false}, {"error", tr("Salvamento incompleto.")}};
	return Json{{"ok", true}, {"data", d}, {"meta", jobj(parsed, "meta")}};
}

// Migrações entre versões do formato. Versão 0 = arquivos antigos sem campo version.
Json Saves::migrate(Json d, int from_version) {
	if (from_version < 1) {
		if (!has(d, "property")) d["property"] = Json::object();
		if (!has(d, "law")) d["law"] = Json::object();
		d["version"] = 1;
	}
	if (jint(d, "version", 1) < 2) {
		// v2: dono "owner" nas casas, roupas vestidas, traços, entregas e eventos.
		Json pl = jobj(d, "player");
		if (!has(pl, "traits")) pl["traits"] = Json::array();
		d["player"] = pl;
		d["version"] = 2;
	}
	return d;
}

std::string Saves::latest_slot() const {
	std::string best;
	double best_t = -1.0;
	for (const auto& s : slots()) {
		const Json m = read_meta(s);
		if (!m.empty() && jnum(m, "unix", 0) > best_t) {
			best_t = jnum(m, "unix", 0);
			best = s;
		}
	}
	return best;
}

void Saves::delete_slot(const std::string& slot) {
	remove_file(path_for(slot));
	remove_file(path_for(slot) + ".bak");
}

bool Saves::prepare_load(const std::string& slot) {
	const Json d = load_data(slot);
	if (d.empty()) return false;
	game().from_dict(d);
	game().pending_load = Json{{"loaded", true}};
	return true;
}

void Saves::prepare_new_game(const std::string& player_name, const Json& appearance, const Json& traits) {
	game().new_game();
	game().player.apply_traits(traits.is_array() ? traits : Json::array());
	if (!strip(player_name).empty()) game().player.name = strip(player_name);
	if (appearance.is_object() && !appearance.empty()) game().player.appearance = appearance;
	game().pending_load = Json{{"new", true}};
}

}  // namespace vila
