// Testes unitários do núcleo VilaCore — port 1:1 de tests/TestRunner.gd.
// Rodar:
//   cmake -S unreal/Tests/VilaCore -B build/vilacore && cmake --build build/vilacore -j4
//   build/vilacore/vila_tests unreal/Content/Data
// Sai com código 0 se tudo passar, 1 caso contrário.
#include <cstdio>
#include <filesystem>
#include <iostream>

#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/DialogueRunner.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Saves.h"

using namespace vila;

namespace {

std::vector<std::string> failures;
int passed = 0;
std::string current;

void check(bool cond, const std::string& msg) {
	if (cond)
		++passed;
	else
		failures.push_back(current + ": " + msg);
}

bool approx(double a, double b, double eps = 0.01) { return std::abs(a - b) <= eps; }

// O nó TestRunner do Godot fazia papel de "mundo" sem nenhum método espacial: aqui, os padrões de IWorld.
class FakeWorld : public IWorld {};
FakeWorld fake_world;

Json env(double ambient, bool raining, double warmth, bool rain_protect) {
	return Json{{"ambient", ambient}, {"outdoors", true}, {"raining", raining}, {"warmth", warmth}, {"rain_protect", rain_protect}};
}

Json fresh_needs() { return Json{{"fome", 0.0}, {"sede", 0.0}, {"sono", 0.0}, {"fadiga", 0.0}, {"higiene", 0.0}}; }

std::vector<std::string> moodle_ids(const Needs& n) {
	std::vector<std::string> ids;
	for (const auto& m : n.moodles()) ids.push_back(jstr(m, "id"));
	return ids;
}

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

Json bananas(const Inventory& inv) {
	Json out = Json::array();
	for (const auto& s : inv.items)
		if (jstr(s, "id") == "banana") out.push_back(s);
	return out;
}

int stock_total(const std::string& shop) {
	int t = 0;
	for (const auto& [k, v] : jobj(game().shops.stock, shop).items()) t += to_int(v);
	return t;
}

// ------------------------------------------------------------------
void test_needs() {
	Needs n;
	n.v = fresh_needs();
	n.tick(2.0, "idle");
	check(approx(n.get_need("fome"), 9.0), "fome sobe 4.5/h");
	check(approx(n.get_need("sede"), 13.0), "sede sobe 6.5/h");
	n.set_need("sono", 90.0);
	n.tick(4.0, "sleep");
	check(approx(n.get_need("sono"), 40.0), "dormir reduz sono");
	n.set_need("fadiga", 0.0);
	n.tick(0.5, "run", 0);
	check(approx(n.get_need("fadiga"), 30.0), "correr cansa");
	n.consume(Json{{"fome", -100}, {"saude", -30}});
	check(n.get_need("fome") == 0.0 && approx(n.saude, 70.0), "consumo aplica efeitos e limita");
	n.set_need("fadiga", 95.0);
	check(!n.can_run(), "exausto não corre");
	check(n.speed_multiplier() < 1.0, "fadiga reduz velocidade");
	check(n.work_multiplier() > 1.2, "fadiga torna o trabalho mais lento");
	n.set_need("sede", 100.0);
	const double before = n.saude;
	n.tick(1.0, "idle");
	check(n.saude < before, "desidratação fere");
	const auto labels = moodle_ids(n);
	check(contains(labels, "sede") && contains(labels, "fadiga"), "moodles aparecem quando relevantes");
}

void test_inventory() {
	Inventory inv(1.0);
	check(inv.add("agua", 3) == 2, "capacidade estrita (0.5kg cada, cabe 2)");
	check(approx(inv.weight(), 1.0), "peso total");
	check(inv.add("agua", 1, 0.0, false) == 1, "não estrito ignora capacidade");
	Inventory box(10.0);
	check(inv.transfer_to(box, 0, 3) == 3, "transferência entre inventários");
	check(box.count("agua") == 3 && inv.count("agua") == 0, "contagem após transferir");
	Inventory fridge(10.0);
	fridge.refrigerated = true;
	fridge.add("banana", 1);
	box.add("banana", 1);
	fridge.tick_age(80.0);
	box.tick_age(80.0);
	const Json fb = fridge.items[0];
	const Json bb = bananas(box)[0];
	check(!fridge.is_spoiled(fb), "geladeira conserva");
	check(box.is_spoiled(bb), "fora da geladeira estraga");
	box.add("banana", 1, 0.0);
	check(bananas(box).size() == 2, "perecíveis de idades diferentes não empilham");
	check(box.remove("banana", 1) == 1 && !box.is_spoiled(bananas(box)[0]), "remove o mais velho primeiro");
	PlayerState p;
	p.inventory.add("mochila", 1);
	check(approx(p.capacity(), 12.0), "capacidade base");
	p.equip("mochila");
	check(approx(p.capacity(), 20.0), "mochila aumenta capacidade");
}

void test_skills() {
	Skills s;
	check(s.level("culinaria") == 0, "começa no nível 0");
	s.add_xp("culinaria", 140);
	check(s.level("culinaria") == 2, "XP vira nível");
	check(s.progress("culinaria") > 0.0 && s.progress("culinaria") < 1.0, "progresso parcial");
}

void test_shop() {
	Game& g = game();
	PlayerState& p = g.player;
	p.money = 50;
	const int stock_before = to_int(g.shops.stock["mercadinho"]["agua"]);
	Json r = g.shops.buy("mercadinho", "agua", 2, p, g.relations);
	check(jbool(r, "ok") && p.money == 44, "compra debita (2 x R$3)");
	check(to_int(g.shops.stock["mercadinho"]["agua"]) == stock_before - 2, "estoque diminui");
	check(!jbool(g.shops.buy("mercadinho", "mochila", 2, p, g.relations), "ok"), "sem estoque suficiente");
	p.money = 1;
	check(!jbool(g.shops.buy("mercadinho", "kit_curativo", 1, p, g.relations), "ok"), "sem dinheiro não compra");
	g.relations.change("antonio", 40);
	check(g.shops.price_buy("mercadinho", "kit_curativo", g.relations) < 25, "desconto de freguês");
	p.inventory.add("relogio_antigo", 1);
	int idx = -1;
	for (size_t i = 0; i < p.inventory.items.size(); ++i)
		if (jstr(p.inventory.items[i], "id") == "relogio_antigo") idx = static_cast<int>(i);
	const int money_before = p.money;
	Json s = g.shops.sell("penhores", idx, 1, p);
	check(jbool(s, "ok") && p.money == money_before + static_cast<int>(std::floor(45 * 0.55)), "venda no penhor");
	check(!g.shops.buys_item("mercadinho", "relogio_antigo"), "mercadinho não compra relógio");
	g.relations.change("antonio", -100);
	check(!g.shops.refuses_service("mercadinho", g.relations).empty(), "dono hostil recusa atendimento");
	Rng rng(7);
	const int total_before = stock_total("mercadinho");
	check(!g.shops.npc_purchase("mercadinho", rng).empty(), "NPC compra algo");
	check(stock_total("mercadinho") == total_before - 1, "compra de NPC consome estoque");
}

void test_job_flow() {
	Game& g = game();
	DriveThruJob& j = g.job;
	PlayerState& p = g.player;
	Rng rng(42);
	check(!jbool(j.clock_in(12.0, p.needs), "ok"), "não contratado não bate ponto");
	j.hire();
	check(j.employed, "contratação");
	check(!jbool(j.clock_in(8.0, p.needs), "ok"), "fechado às 8h");
	p.needs.set_need("higiene", 80.0);
	check(!jbool(j.clock_in(12.0, p.needs), "ok"), "sujo não trabalha");
	p.needs.set_need("higiene", 10.0);
	check(jbool(j.clock_in(12.0, p.needs), "ok"), "bate ponto");
	j.customer_cooldown = 0.0;
	check(j.wants_customer(), "pede cliente");
	j.customer_dispatched(rng);
	j.create_order("carro_1", rng);
	// No GDScript o pedido devolvido é uma referência; em C++ alteramos o que ficou na fila.
	for (auto& o : j.orders) {
		if (jstr(o, "customer") != "carro_1" && jstr(o, "customer_id") != "carro_1") continue;
		o["items"] = Json{{"job_x_brasa", 1}, {"job_batata", 1}, {"job_refri", 1}};
		o["remaining"] = o["items"];
	}
	check(j.station_start("chapa", 0), "chapa inicia");
	check(!j.station_start("chapa", 0), "chapa ocupada");
	j.station_start("fritadeira", 0);
	j.tick(6.5, 12.2, true);
	check(jstr(j.stations["fritadeira"], "state") == "ready", "batata pronta em 6 min");
	check(jstr(j.stations["chapa"], "state") == "cooking", "hambúrguer ainda cozinhando");
	check(j.station_collect("fritadeira", p.inventory) == "job_batata", "retira batata");
	j.tick(3.0, 12.3, true);
	check(j.station_collect("chapa", p.inventory) == "job_x_brasa", "retira hambúrguer");
	Json r1 = j.deliver(p.inventory, 0);
	check(jstr(r1, "status") == "partial", "entrega parcial sem refri");
	p.inventory.add("job_refri", 1);
	Json r2 = j.deliver(p.inventory, 0);
	check(jstr(r2, "status") == "complete", "pedido completo");
	check(j.status_of("carro_1") == "done", "carro sabe que pode ir");
	j.tick(50.0, 13.2, true);
	const int money_before = p.money;
	Json summary = g.clock_out_player();
	check(jint(summary, "total") > 0 && p.money == money_before + jint(summary, "total"), "pagamento ao bater ponto de saída");
	check(jint(summary, "ok") == 1, "1 pedido no resumo");
	check(p.inventory.count("job_refri") == 0, "itens do trabalho são recolhidos");
}

void test_job_abandon_and_burn() {
	Game& g = game();
	DriveThruJob& j = g.job;
	PlayerState& p = g.player;
	j.hire();
	j.clock_in(15.0, p.needs);
	j.station_start("chapa", 0);
	j.tick(9.0 + 7.5, 15.3, true);
	check(jstr(j.stations["chapa"], "state") == "burnt", "hambúrguer queima se esquecido");
	check(j.station_collect("chapa", p.inventory) == "burnt", "descarta queimado");
	Json s = j.tick(16.0, 15.6, false);
	check(jstr(s, "reason") == "abandono", "abandono encerra turno");
	check(j.warnings == 1, "abandono gera advertência");
	j.add_warning("teste");
	j.add_warning("teste");
	check(j.fired && !j.employed, "3 advertências = demissão");
}

DialogueRunner run_dialogue(const std::string& npc, const std::vector<std::string>& picks) {
	DialogueRunner d;
	d.start(npc, jstr(data().npc(npc), "dialogue"));
	for (const auto& pick : picks) {
		int guard = 0;
		while (!d.finished && !d.has_choices() && guard < 10) {
			d.advance();
			++guard;
		}
		if (d.finished) break;
		bool found = false;
		for (const auto& c : d.choices()) {
			if (contains(jstr(c, "text"), pick) && jbool(c, "enabled")) {
				d.choose(jint(c, "index"));
				found = true;
				break;
			}
		}
		check(found, fmt("opção '%s' disponível em %s/%s", {pick, npc, to_str(d.node_id)}));
		if (!found) break;
	}
	while (!d.finished && !d.has_choices()) d.advance();
	return d;
}

void test_quest_bribe_path() {
	Game& g = game();
	PlayerState& p = g.player;
	p.money = 100;
	run_dialogue("lucia", {"Prazer", "Deixa comigo"});
	check(g.quests.state_of("encomenda") == "active", "missão iniciada pela Lúcia");
	check(g.quests.stage_of("encomenda") == "investigar", "etapa inicial");
	run_dialogue("marcos", {"R$ 40"});
	check(p.money == 60, "suborno cobra R$ 40");
	check(p.inventory.has("encomenda_lucia"), "recebe encomenda");
	check(!g.world_state.container("bau_marcos")->has("encomenda_lucia"), "a caixa saiu do baú do Marcos");
	check(g.quests.stage_of("encomenda") == "entregar", "etapa avança ao pegar o item");
	run_dialogue("lucia", {"pagar R$ 40"});
	check(g.quests.state_of("encomenda") == "completed", "missão concluída");
	check(g.quests.outcome_of("encomenda") == "suborno", "desfecho suborno");
	check(p.money == 100, "Lúcia reembolsa");
	check(p.inventory.has("bolo_fuba"), "ganha bolo");
	check(!p.inventory.has("encomenda_lucia"), "entregou a caixa");
	DialogueRunner d;
	d.start("antonio", "antonio");
	check(to_str(d.node_id) == "soube", "consequência: seu Antônio soube do favor");
	check(g.shops.discount("mercadinho", g.relations) >= 0.1, "consequência: desconto no mercadinho");
}

void test_quest_persuasion_and_buy() {
	Game& g = game();
	PlayerState& p = g.player;
	g.quests.start("encomenda");
	// Força sucesso: habilidade social alta + semente.
	p.skills.add_xp("social", 3300);
	g.relations.change("marcos", 60);
	g.rng.seed(1);
	run_dialogue("marcos", {"remédio de pressão"});
	check(g.has_flag("obtida_persuasao") && p.inventory.has("encomenda_lucia"), "persuasão bem-sucedida entrega a caixa");
	run_dialogue("lucia", {"Conversei com o Marcos"});
	check(g.quests.outcome_of("encomenda") == "diplomacia", "desfecho diplomacia");
	check(g.relations.reputation >= 10, "reputação sobe");
	// Caminho alternativo em outra partida: comprar o remédio.
	g.new_game();
	p.money = 100;
	g.quests.start("encomenda");
	g.shops.buy("mercadinho", "remedio_pressao", 1, p, g.relations);
	check(g.quests.stage_of("encomenda") == "entregar", "comprar remédio também avança");
	run_dialogue("lucia", {"presente"});
	check(g.quests.outcome_of("encomenda") == "compra_presente", "desfecho presente");
	check(g.world_state.container("bau_marcos")->has("encomenda_lucia"), "Marcos ficou com a caixa");
}

void test_dialogue_conditions() {
	Game& g = game();
	DialogueRunner d;
	d.start("sandra", "sandra");
	check(to_str(d.node_id) == "inicio", "Sandra começa no início");
	g.player.needs.set_need("higiene", 90.0);
	run_dialogue("sandra", {"contratando", "aprendo", "Topo"});
	check(!g.job.employed, "sujo(a) não é contratado(a)");
	g.player.needs.set_need("higiene", 0.0);
	run_dialogue("sandra", {"contratando", "aprendo", "Topo"});
	check(g.job.employed, "contratado(a) pela Sandra");
	DialogueRunner d2;
	d2.start("marcos", "marcos");
	check(to_str(d2.node_id) == "padrao", "Marcos padrão sem missão");
	g.player.money = 10;
	g.quests.start("encomenda");
	DialogueRunner d3;
	d3.start("marcos", "marcos");
	Json bribe = Json::array(), persu = Json::array();
	for (const auto& c : d3.choices()) {
		if (contains(jstr(c, "text"), "R$ 40")) bribe.push_back(c);
		if (jnum(c, "chance", -1.0) >= 0.0) persu.push_back(c);
	}
	check(bribe.size() == 1 && !jbool(bribe[0], "enabled"), "suborno aparece desabilitado sem dinheiro");
	check(persu.size() >= 1 && jstr(persu[0], "text").rfind("[", 0) == 0, "testes mostram a chance");
	// Tião: dica só com relação suficiente
	DialogueRunner d4;
	d4.start("tiao", "tiao");
	int marcos_opts = 0;
	for (const auto& c : d4.choices())
		if (contains(jstr(c, "text"), "Marcos") && jbool(c, "enabled")) ++marcos_opts;
	check(marcos_opts == 1, "pergunta sobre Marcos disponível");
	run_dialogue("tiao", {"Marcos"});
	check(!g.has_flag("dica_tapete"), "sem relação, Tião não conta");
	g.player.money = 20;
	run_dialogue("tiao", {"R$ 5"});
	run_dialogue("tiao", {"Marcos"});
	check(g.has_flag("dica_tapete"), "com relação, Tião dá a dica");
}

void test_law() {
	Game& g = game();
	Relations& r = g.relations;
	g.law.report("furto", "marcos", Json::array(), 1, r);
	check(g.law.pending_fines == 0 && g.law.unsolved.size() == 1, "furto sem testemunha fica sem autoria");
	g.law.report("furto", "marcos", Json::array({"lucia"}), 1, r);
	check(g.law.pending_fines == 90, "multa pendente");
	check(r.reputation < 0, "reputação cai");
	check(r.has_memory("lucia", "viu_furto"), "testemunha lembra");
	check(r.get_rel("marcos") < -30, "vítima fica hostil");
	Json f = g.law.collect_fines();
	check(jint(f, "amount") == 90 && g.law.pending_fines == 0, "multa cobrada");
}

void test_rent() {
	Game& g = game();
	PlayerState& p = g.player;
	p.money = 200;
	g.property.charge_rent(p);
	check(p.money == 80, "aluguel cobrado");
	p.money = 50;
	g.property.charge_rent(p);
	check(p.money == 0 && g.property.total_debt() == 70, "aluguel atrasado vira dívida");
}

void test_save_roundtrip() {
	Game& g = game();
	PlayerState& p = g.player;
	p.money = 321;
	p.inventory.add("cafe_lata", 2);
	p.skills.add_xp("direcao", 77);
	p.needs.set_need("fome", 42.0);
	g.world_state.container("geladeira_jogador")->add("sanduiche", 2);
	g.world_state.set_door("porta_casa_jogador", true, false);
	g.world_state.vehicles["carro_jogador"]["fuel"] = 3.5;
	g.quests.start("encomenda");
	g.relations.change("lucia", 12);
	g.job.hire();
	g.set_flag("teste_flag");
	vila::clock().set_time(3, 14, 20);
	const std::string json = g.to_dict().dump();
	g.new_game();
	check(g.player.money != 321, "estado resetado antes de carregar");
	g.from_dict(Json::parse(json));
	check(p.money == 321, "dinheiro preservado");
	check(p.inventory.count("cafe_lata") == 2, "inventário preservado");
	check(approx(p.needs.get_need("fome"), 42.0), "necessidades preservadas");
	check(jint(p.skills.xp, "direcao", 0) == 77, "habilidades preservadas");
	check(g.world_state.container("geladeira_jogador")->count("sanduiche") == 2, "geladeira preservada");
	check(jbool(jobj(g.world_state.doors, "porta_casa_jogador"), "open"), "porta preservada");
	check(approx(jnum(jobj(g.world_state.vehicles, "carro_jogador"), "fuel"), 3.5), "combustível preservado");
	check(g.quests.state_of("encomenda") == "active", "missão preservada");
	check(g.job.employed, "emprego preservado");
	check(g.has_flag("teste_flag"), "flags preservadas");
	check(vila::clock().day() == 3 && vila::clock().hour() == 14 && vila::clock().minute() == 20, "horário preservado");
}

void test_save_migration_and_corruption() {
	Json old = Json{{"clock", {{"total_minutes", 600.0}}}, {"player", {{"money", 5}}}};
	Json m = Saves::migrate(old, 0);
	check(jint(m, "version") == 2 && has(m, "property"), "migração v0 → v2");
	const std::string path = saves().path_for("slot3");
	write_text_file(path, "{isso não é json");
	if (file_exists(path + ".bak")) remove_file(path + ".bak");
	check(saves().load_data("slot3").empty() && !saves().last_error.empty(), "arquivo corrompido é rejeitado com mensagem");
	saves().delete_slot("slot3");
}

void test_data_integrity() {
	check(data().errors.empty(), "dados sem referências quebradas: " + join(data().errors, "; "));
	for (const auto& [npc_id, npc] : data().npcs().items())
		for (const auto& e : jarr(npc, "schedule")) check(has(e, "place") && has(e, "activity"), "agenda válida de " + npc_id);
	for (const auto& [did, dlg] : jobj(data().db, "dialogues").items()) {
		const Json& nodes = jobj(dlg, "nodes");
		for (const auto& [nid, n] : nodes.items()) {
			std::vector<std::string> targets;
			if (!jget(n, "next").is_null()) targets.push_back(to_str(n["next"]));
			for (const auto& c : jarr(n, "choices"))
				for (const char* k : {"next", "success", "fail"})
					if (!jget(c, k).is_null()) targets.push_back(to_str(c[k]));
			for (const auto& t : targets) check(nodes.contains(t), fmt("nó %s/%s aponta para %s existente", {did, nid, t}));
		}
	}
}

// ------------------------------------------------------------------ fase 1
void test_temperature_and_clothes() {
	Game& g = game();
	PlayerState& p = g.player;
	check(p.is_equipped("camiseta_azul"), "começa vestindo camiseta");
	Needs& n = p.needs;
	n.body_temp = 36.6;
	n.tick(2.0, "idle", 0, env(16.0, true, 0.0, false));
	check(n.wetness >= 99.0, "chuva sem proteção encharca a roupa");
	check(n.body_temp < 35.9, fmt("molhado no frio, a temperatura cai (%.1f)", {n.body_temp}));
	check(contains(moodle_ids(n), "frio"), "moodle de frio aparece");
	check(n.work_multiplier() > 1.0, "frio deixa o trabalho mais lento");
	// Com capa e casaco, a mesma noite chuvosa é suportável.
	p.inventory.add("capa_chuva", 1);
	p.inventory.add("casaco", 1);
	p.equip("capa_chuva");
	p.equip("casaco");
	check(p.rain_protected() && p.warmth() >= 6.0, "capa protege e casaco aquece");
	check(jbool(p.visual_appearance(), "jacket"), "casaco aparece no visual");
	Needs n2;
	n2.tick(3.0, "idle", 0, env(16.0, true, p.warmth(), true));
	check(n2.wetness == 0.0 && n2.body_temp > 35.9, fmt("agasalhado(a) e seco(a), sem frio (%.1f)", {n2.body_temp}));
	Needs n3, n4;
	for (int i = 0; i < 20; ++i) {
		n3.tick(0.1, "run", 0, env(31.0, false, 6.0, false));
		n4.tick(0.1, "run", 0, env(22.0, false, 0.0, false));
	}
	check(n3.body_temp > 37.5, "correr de casaco no calor superaquece");
	check(n3.get_need("sede") > n4.get_need("sede"), "calor aumenta a sede");
	p.unequip("agasalho");
	check(p.warmth() < 6.0, "tirar o casaco reduz o agasalho");
}

void test_pain_and_traits() {
	Game& g = game();
	Needs n;
	n.add_pain(60.0);
	check(n.speed_multiplier() < 1.0 && n.work_multiplier() > 1.0, "dor reduz velocidade e desempenho");
	n.consume(Json{{"dor", -50}});
	check(n.dor <= 10.0, "analgésico reduz a dor");
	n.add_pain(30.0);
	n.tick(5.0, "idle");
	check(n.dor < 30.0, "dor passa com o tempo");
	PlayerState& p = g.player;
	p.apply_traits(Json::array({"atletico", "estomago_forte"}));
	check(p.skills.level("condicionamento") == 2, "traço dá nível inicial de habilidade");
	Needs base;
	base.v = fresh_needs();
	base.tick(1.0, "run", 0);
	p.needs.v = fresh_needs();
	p.needs.tick(1.0, "run", 0);
	check(p.needs.get_need("fadiga") < base.get_need("fadiga"), "atlético cansa menos correndo");
	check(p.needs.get_need("fome") > base.get_need("fome"), "atlético sente mais fome");
	p.needs.consume(Json{{"nausea", 4.0}});
	check(p.needs.nausea_hours == 0.0, "estômago forte ignora comida estragada");
	g.from_dict(Json::parse(g.to_dict().dump()));
	check(json_array_has(g.player.traits, "atletico") && jnum(g.player.needs.mods, "run_fatigue", 1.0) < 1.0,
		"traços e modificadores sobrevivem ao salvamento");
}

void test_property_market() {
	Game& g = game();
	PlayerState& p = g.player;
	PropertySystem& pr = g.property;
	check(pr.owns_or_rents("casa_jogador") && !pr.owns("casa_jogador"), "começa alugando a casa 8");
	check(g.is_mine("owner", "casa_jogador") && !g.is_mine("owner", "casa_5"), "dono resolvido pela propriedade");
	p.money = 500;
	check(!jbool(pr.buy("casa_5", p), "ok"), "sem dinheiro não compra a casa");
	check(jbool(pr.buy("garagem_3", p), "ok") && p.money == 50, "comprou a garagem (R$ 450)");
	check(pr.owns("garagem_3") && g.is_mine("owner", "garagem_3"), "garagem registrada como própria");
	check(!jbool(pr.end_lease("casa_jogador"), "ok"), "não pode entregar a única casa");
	p.money = 1300;
	check(jbool(pr.buy("casa_5", p), "ok"), "comprou a casa 5");
	check(pr.owned_homes().size() == 2, "duas casas registradas");
	p.money = 300;
	Json msgs = pr.charge_rent(p);
	check(p.money == 300 - 120 - 15 - 6 && msgs.size() == 3, "domingo: aluguel + IPTU da casa e da garagem");
	check(jbool(pr.end_lease("casa_jogador"), "ok") && !pr.owns_or_rents("casa_jogador"), "entregou a casa alugada");
	check(!jbool(pr.sell("casa_5", p), "ok"), "não vende a única casa");
	const int before = p.money;
	Json r = pr.sell("garagem_3", p);
	check(jbool(r, "ok") && p.money == before + 360, "vendeu a garagem por 80%");
	check(pr.is_for_sale("garagem_3"), "garagem volta ao mercado");
}

void test_delivery_job() {
	Game& g = game();
	PlayerState& p = g.player;
	DeliveryJob& dj = g.delivery;
	Rng rng(3);
	check(!jbool(dj.take(0.0, rng, p.inventory), "ok"), "sem contratação não há entrega");
	run_dialogue("antonio", {"entregador", "Fechado"});
	check(dj.hired, "contratado(a) como entregador pelo Seu Antônio");
	Json t = dj.take(100.0, rng, p.inventory);
	check(jbool(t, "ok") && p.inventory.has("sacola_entrega"), "pegou a sacola");
	check(!jbool(dj.take(100.0, rng, p.inventory), "ok"), "uma entrega por vez");
	int money = p.money;
	Json c = dj.complete(150.0, p);
	check(jbool(c, "ok") && jbool(c, "on_time") && p.money > money + 9, "entrega no prazo paga com gorjeta");
	dj.take(200.0, rng, p.inventory);
	money = p.money;
	Json c2 = dj.complete(200.0 + 200.0, p);
	check(jbool(c2, "ok") && !jbool(c2, "on_time") && p.money - money < 9, "atraso paga menos");
	dj.take(500.0, rng, p.inventory);
	const int rel = g.relations.get_rel("antonio");
	dj.tick(1.0, 500.0 + 300.0);
	check(dj.active.empty() && !p.inventory.has("sacola_entrega") && g.relations.get_rel("antonio") < rel,
		"entrega abandonada é cancelada e irrita o dono");
}

void test_event_director() {
	Game& g = game();
	EventDirector& d = g.director;
	vila::clock().set_time(1, 10, 0);
	check(d.can_fire("promocao_mercadinho"), "promoção pode acontecer às 10h");
	vila::clock().set_time(1, 22, 0);
	check(!d.can_fire("promocao_mercadinho"), "promoção respeita o horário");
	vila::clock().set_time(1, 10, 0);
	const int price = g.shops.price_buy("mercadinho", "kit_curativo", g.relations);
	check(d.fire("promocao_mercadinho"), "promoção disparada");
	check(g.shops.price_buy("mercadinho", "kit_curativo", g.relations) < price, "promoção reduz o preço de verdade");
	check(!d.can_fire("promocao_mercadinho"), "não repete enquanto ativa / no mesmo dia");
	vila::clock().advance(130.0);
	d.tick(0.0);
	check(!d.is_active("promocao_mercadinho"), "promoção acaba depois de 2 horas");
	check(!d.can_fire("turno_extra"), "turno extra exige estar empregado");
	g.job.hire();
	vila::clock().set_time(1, 11, 0);
	check(d.fire("turno_extra"), "turno extra disparado");
	g.job.clock_in(11.0, g.player.needs);
	g.job.tick(60.0, 11.5, true);
	Json s = g.job.clock_out();
	check(jint(s, "wage") == static_cast<int>(std::round(1.5 * g.job.wage())), fmt("turno extra paga 50%% a mais (R$ %d)", {jint(s, "wage")}));
	check(!d.fire("carteira_perdida"), "eventos espaciais precisam do mundo");
}

void test_vehicle_market() {
	Game& g = game();
	PlayerState& p = g.player;
	auto v = [&]() -> const Json& { return jobj(g.world_state.vehicles, "carro_usado"); };
	check(has(v(), "for_sale") && jstr(v(), "owner") == "rafael", "sedã usado à venda na oficina");
	p.money = 100;
	check(!g.buy_vehicle("carro_usado"), "sem dinheiro não compra carro");
	p.money = 800;
	check(g.buy_vehicle("carro_usado") && p.money == 100, "comprou o carro por R$ 700");
	check(jstr(v(), "owner") == "player" && !has(v(), "for_sale"), "carro agora é do jogador");
	const int value = g.sell_vehicle("carro_usado");
	check(value > 0 && has(v(), "for_sale") && jstr(v(), "owner") == "rafael", fmt("vendeu de volta ao Rafael (R$ %d)", {value}));
}

void test_save_v2_fields() {
	Game& g = game();
	PlayerState& p = g.player;
	p.money = 2000;
	g.property.buy("casa_5", p);
	g.delivery.hire();
	p.needs.body_temp = 35.4;
	p.needs.dor = 33.0;
	p.inventory.add("casaco", 1);
	p.equip("casaco");
	const Json saved = Json::parse(g.to_dict().dump());
	g.new_game();
	g.from_dict(saved);
	check(g.property.owns("casa_5"), "imóvel comprado preservado");
	check(g.delivery.hired, "contrato de entregador preservado");
	check(std::abs(p.needs.body_temp - 35.4) < 0.01 && std::abs(p.needs.dor - 33.0) < 0.01, "temperatura e dor preservadas");
	check(p.is_equipped("casaco"), "roupa vestida preservada");
	Json old = Json{{"version", 1}, {"clock", {{"total_minutes", 600.0}}}, {"player", {{"money", 5}}},
		{"property", {{"props", {{"casa_jogador", {{"status", "alugada"}, {"rent", 120}, {"debt", 0}}}}}}}};
	Json m = Saves::migrate(old, 1);
	check(jint(m, "version") == 2 && has(jobj(m, "player"), "traits"), "migração v1 → v2");
	g.from_dict(m);
	check(g.property.owns_or_rents("casa_jogador") && jint(jobj(g.property.props, "casa_jogador"), "tax", -1) == 0,
		"save antigo carrega com campos novos");
}

}  // namespace

int main(int argc, char** argv) {
	const std::string data_dir = argc > 1 ? argv[1] : "unreal/Content/Data";
	if (!data().load(data_dir)) {
		std::cerr << "Falha ao carregar dados de " << data_dir << std::endl;
		return 2;
	}
	const auto tmp = std::filesystem::temp_directory_path() / "vila_tests_saves";
	std::filesystem::create_directories(tmp);
	saves().dir = tmp.string();
	game().init();

	const std::vector<std::pair<const char*, void (*)()>> tests = {
		{"test_needs", test_needs},
		{"test_inventory", test_inventory},
		{"test_skills", test_skills},
		{"test_shop", test_shop},
		{"test_job_flow", test_job_flow},
		{"test_job_abandon_and_burn", test_job_abandon_and_burn},
		{"test_quest_bribe_path", test_quest_bribe_path},
		{"test_quest_persuasion_and_buy", test_quest_persuasion_and_buy},
		{"test_dialogue_conditions", test_dialogue_conditions},
		{"test_law", test_law},
		{"test_rent", test_rent},
		{"test_save_roundtrip", test_save_roundtrip},
		{"test_save_migration_and_corruption", test_save_migration_and_corruption},
		{"test_data_integrity", test_data_integrity},
		{"test_temperature_and_clothes", test_temperature_and_clothes},
		{"test_pain_and_traits", test_pain_and_traits},
		{"test_property_market", test_property_market},
		{"test_delivery_job", test_delivery_job},
		{"test_event_director", test_event_director},
		{"test_vehicle_market", test_vehicle_market},
		{"test_save_v2_fields", test_save_v2_fields},
	};
	for (const auto& [name, fn] : tests) {
		current = name;
		game().world = nullptr;
		game().new_game();
		game().world = &fake_world;  // habilita simulação de tempo sem o mundo 3D
		fn();
	}
	game().world = nullptr;
	std::cout << "\n==== TESTES: " << passed << " verificações OK, " << failures.size() << " falhas ====" << std::endl;
	for (const auto& f : failures) std::cout << "  FALHA: " << f << std::endl;
	return failures.empty() ? 0 : 1;
}
