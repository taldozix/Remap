// Banco de dados do jogo: todo o conteúdo vem dos arquivos JSON (Content/Data no Unreal).
#pragma once
#include "Vila/Core.h"

namespace vila {

class VILACORE_API Data {
public:
	Json db = Json::object();
	std::vector<std::string> errors;

	// Carrega todos os arquivos a partir da pasta de dados (contém items.json, map/, i18n/...).
	bool load(const std::string& data_dir);
	void reload() { load(dir_); }
	const std::string& dir() const { return dir_; }

	const Json& item(const std::string& id) const { return section_item("items", id); }
	bool has_item(const std::string& id) const { return has(jobj(db, "items"), id); }
	std::string item_name(const std::string& id) const { return tr(jstr(item(id), "name", id)); }
	const Json& npc(const std::string& id) const { return section_item("npcs", id); }
	const Json& npcs() const { return jobj(db, "npcs"); }
	const Json& dialogue(const std::string& id) const { return section_item("dialogues", id); }
	const Json& quest(const std::string& id) const { return section_item("quests", id); }
	const Json& quests() const { return jobj(db, "quests"); }
	const Json& shop(const std::string& id) const { return section_item("shops", id); }
	const Json& shops() const { return jobj(db, "shops"); }
	const Json& vehicle(const std::string& id) const { return section_item("vehicles", id); }
	const Json& job(const std::string& id) const { return section_item("jobs", id); }
	const Json& chatter() const { return jobj(db, "chatter"); }
	const Json& trait_def(const std::string& id) const { return section_item("traits", id); }
	const Json& traits() const { return jobj(db, "traits"); }
	const Json& property(const std::string& id) const { return section_item("properties", id); }
	const Json& properties() const { return jobj(db, "properties"); }
	const Json& events() const { return jobj(db, "events"); }
	const Json& deliveries() const { return jobj(db, "deliveries"); }
	const Json& map() const { return jobj(db, "map"); }

private:
	const Json& section_item(const char* section, const std::string& id) const { return jobj(jobj(db, section), id); }
	void validate();
	std::string dir_;
};

VILACORE_API Data& data();

}  // namespace vila
