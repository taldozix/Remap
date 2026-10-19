// Executa um diálogo de data/dialogues.json: escolhe o nó de entrada pelo contexto,
// aplica efeitos, filtra opções por condição e resolve testes (persuasão etc.).
#pragma once
#include "Vila/Core.h"

namespace vila {

class VILACORE_API DialogueRunner {
public:
	std::string npc_id;
	std::string dialogue_id;
	Json dlg = Json::object();
	Json node_id;  // null ou string
	Json node = Json::object();
	bool finished = false;
	Json last_check = Json::object();  // resultado do último teste (para a UI)
	Rng* rng = nullptr;

	// p_rng == nullptr: usa game().rng.
	explicit DialogueRunner(Rng* p_rng = nullptr);

	bool start(const std::string& p_npc, const std::string& p_dialogue);
	Json ctx() const;
	std::string speaker_name() const;
	std::string text() const;
	static std::string substitute(std::string t);
	// Opções visíveis. Opções com condição falsa somem, a menos que tenham "show_disabled".
	Json choices() const;
	bool has_choices() const;
	// Avança um nó sem escolhas.
	void advance();
	void choose(int index);

private:
	void enter(const Json& id);
};

}  // namespace vila
