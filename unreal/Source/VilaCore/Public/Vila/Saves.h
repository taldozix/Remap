// Salvamento e carregamento em JSON. Escrita atômica com backup, validação de arquivo
// corrompido e migração de versões antigas. A pasta é definida pelo host
// (Unreal: FPaths::ProjectSavedDir()/SaveGames).
#pragma once
#include "Vila/Core.h"

namespace vila {

class Saves {
public:
	static const std::vector<std::string>& slots();
	static std::string slot_label(const std::string& slot);

	std::string dir = "saves";
	std::string last_error;

	std::string path_for(const std::string& slot) const { return dir + "/" + slot + ".json"; }
	std::string block_reason() const;  // vazio = pode salvar
	bool save_game(const std::string& slot);
	bool exists(const std::string& slot) const { return file_exists(path_for(slot)); }
	Json read_meta(const std::string& slot) const;
	Json load_data(const std::string& slot);  // objeto vazio em caso de erro (ver last_error)
	static Json migrate(Json data, int from_version);
	std::string latest_slot() const;
	void delete_slot(const std::string& slot);
	// Prepara o estado; o host troca para o mapa do jogo depois.
	bool prepare_load(const std::string& slot);
	void prepare_new_game(const std::string& player_name, const Json& appearance, const Json& traits);

private:
	Json read(const std::string& path) const;
};

Saves& saves();

}  // namespace vila
