#include "Vila/DialogueRunner.h"

#include "Vila/Data.h"
#include "Vila/Game.h"
#include "Vila/Rules.h"

namespace vila {

DialogueRunner::DialogueRunner(Rng* p_rng) { rng = p_rng ? p_rng : &game().rng; }

bool DialogueRunner::start(const std::string& p_npc, const std::string& p_dialogue) {
	npc_id = p_npc;
	dialogue_id = p_dialogue;
	dlg = data().dialogue(p_dialogue);
	if (dlg.empty()) {
		finished = true;
		return false;
	}
	finished = false;
	const Json entries = jarr(dlg, "entry");
	for (const auto& e : entries) {
		if (Rules::check(jobj(e, "if"), ctx())) {
			enter(jget(e, "node"));
			return !finished;
		}
	}
	finished = true;
	return false;
}

Json DialogueRunner::ctx() const {
	return Json{{"npc", npc_id}, {"reason", tr(jstr(data().npc(npc_id), "name", ""))}};
}

void DialogueRunner::enter(const Json& id_in) {
	const Json id = id_in;  // cópia: pode apontar para dentro de `node`
	if (id.is_null()) last_check = Json::object();
	if (id.is_null() || !has(jobj(dlg, "nodes"), to_str(id))) {
		node_id = nullptr;
		node = Json::object();
		finished = true;
		return;
	}
	node_id = to_str(id);
	node = jobj(dlg, "nodes")[node_id.get<std::string>()];
	if (has(node, "do")) {
		const Json effects = jget(node, "do");
		Rules::apply(effects, ctx());
	}
}

std::string DialogueRunner::speaker_name() const {
	if (jstr(node, "speaker", "self") == "player") return game().player.name;
	return tr(jstr(data().npc(npc_id), "name", npc_id));
}

std::string DialogueRunner::text() const { return substitute(tr(jstr(node, "text", ""))); }

std::string DialogueRunner::substitute(std::string t) {
	if (t.find('{') == std::string::npos) return t;
	const Json& v = jobj(game().world_state.vehicles, "carro_jogador");
	const std::vector<std::pair<std::string, std::string>> subs = {
		{"{player}", game().player.name},
		{"{money}", std::to_string(game().player.money)},
		{"{job_rank}", game().job.rank_title()},
		{"{job_perf}", std::to_string(static_cast<int>(game().job.performance))},
		{"{job_orders}", std::to_string(game().job.total_orders)},
		{"{job_warnings}", std::to_string(game().job.warnings)},
		{"{repair_cost}", std::to_string(static_cast<int>(std::ceil(jnum(v, "damage", 0.0) * 3)))},
	};
	for (const auto& [k, s] : subs) t = replace_all(t, k, s);
	return t;
}

Json DialogueRunner::choices() const {
	Json out = Json::array();
	const Json& list = jarr(node, "choices");
	for (size_t i = 0; i < list.size(); ++i) {
		const Json& c = list[i];
		const bool ok = Rules::check(jobj(c, "if"), ctx());
		if (!ok && !jbool(c, "show_disabled", false)) continue;
		std::string label = substitute(tr(to_str(jget(c, "text"))));
		double chance = -1.0;
		if (has(c, "check")) {
			const Json& chk = jget(c, "check");
			chance = Rules::chance(chk, ctx());
			label = fmt("[%s %d%%] %s", {tr(jstr(chk, "label", "Teste")), static_cast<int>(std::round(chance * 100)), label});
		}
		out.push_back(Json{{"index", static_cast<int>(i)}, {"text", label}, {"enabled", ok}, {"hint", tr(jstr(c, "disabled_hint", ""))}, {"chance", chance}});
	}
	return out;
}

bool DialogueRunner::has_choices() const { return !choices().empty(); }

void DialogueRunner::advance() {
	if (finished) return;
	if (has_choices()) return;
	enter(jget(node, "next"));
}

void DialogueRunner::choose(int index) {
	if (finished) return;
	const Json list = jarr(node, "choices");  // cópia: enter() troca `node`
	if (index < 0 || index >= static_cast<int>(list.size())) return;
	const Json& c = list[static_cast<size_t>(index)];
	if (!Rules::check(jobj(c, "if"), ctx())) return;
	if (has(c, "do")) Rules::apply(jget(c, "do"), ctx());
	if (has(c, "check")) {
		const Json& chk = jget(c, "check");
		const double chance = Rules::chance(chk, ctx());
		const bool success = rng->randf() < chance;
		last_check = Json{{"label", tr(jstr(chk, "label", "Teste"))}, {"success", success}, {"chance", chance}};
		enter(success ? jget(c, "success") : jget(c, "fail"));
		return;
	}
	last_check = Json::object();
	enter(jget(c, "next"));
}

}  // namespace vila
