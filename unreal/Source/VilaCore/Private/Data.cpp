#include "Vila/Data.h"

namespace vila {

Data& data() {
	static Data d;
	return d;
}

static const std::pair<const char*, const char*> FILES[] = {
	{"items", "items.json"}, {"npcs", "npcs.json"}, {"dialogues", "dialogues.json"},
	{"quests", "quests.json"}, {"shops", "shops.json"}, {"vehicles", "vehicles.json"},
	{"jobs", "jobs.json"}, {"chatter", "chatter.json"}, {"traits", "traits.json"},
	{"properties", "properties.json"}, {"events", "events.json"}, {"deliveries", "deliveries.json"},
	{"map", "map/vila_aurora.json"},
};

bool Data::load(const std::string& data_dir) {
	dir_ = data_dir;
	errors.clear();
	db = Json::object();
	for (const auto& [key, file] : FILES) {
		const std::string path = data_dir + "/" + file;
		std::string text;
		if (!read_text_file(path, text)) {
			errors.push_back("Arquivo ausente: " + path);
			db[key] = Json::object();
			continue;
		}
		Json parsed = Json::parse(text, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object()) {
			errors.push_back("JSON inválido: " + path);
			db[key] = Json::object();
			continue;
		}
		db[key] = std::move(parsed);
	}
	// traduções (data/i18n/<locale>.json)
	std::string en;
	if (read_text_file(data_dir + "/i18n/en.json", en)) {
		Json t = Json::parse(en, nullptr, false);
		if (!t.is_discarded()) load_translations("en", t);
	}
	validate();
	for (const auto& e : errors) push_warning("[Data] " + e);
	return errors.empty();
}

// Checagem de referências cruzadas: evita itens/diálogos inexistentes em tempo de execução.
void Data::validate() {
	const Json& items = jobj(db, "items");
	for (const auto& [shop_id, shop] : jobj(db, "shops").items())
		for (const auto& [item_id, _] : jobj(shop, "sells").items())
			if (!has(items, item_id)) errors.push_back(fmt("Loja %s vende item inexistente %s", {shop_id, item_id}));
	for (const auto& [npc_id, npc] : jobj(db, "npcs").items()) {
		const std::string dlg = jstr(npc, "dialogue");
		if (!dlg.empty() && !has(jobj(db, "dialogues"), dlg)) errors.push_back(fmt("NPC %s usa diálogo inexistente %s", {npc_id, dlg}));
	}
}

}  // namespace vila
