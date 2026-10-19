// Linguagem de condições e efeitos compartilhada por diálogos, missões e eventos.
// Condições: dicionário (todas as chaves precisam ser verdadeiras).
// Efeitos: lista de dicionários {operação: argumentos}.
// `ctx.npc` permite usar "self" para o NPC da conversa.
#pragma once
#include "Vila/Core.h"

namespace vila {

class VILACORE_API Rules {
public:
	static bool evaluate(const Json& cond, const Json& ctx = Json::object());
	// Probabilidade de um teste (persuasão, intimidação...). Mostrada ao jogador antes de escolher.
	static double chance(const Json& spec, const Json& ctx = Json::object());
	static void apply(const Json& effects, const Json& ctx = Json::object());

private:
	static std::string t(const std::string& text);
	static std::string npc(const Json& id, const Json& ctx);
	static bool check_one(const std::string& key, const Json& arg, const Json& ctx);
	static void apply_one(const std::string& op, const Json& arg, const Json& ctx);
};

}  // namespace vila
