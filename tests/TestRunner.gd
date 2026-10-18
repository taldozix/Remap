extends Node
## Testes unitários dos sistemas (sem cena 3D). Rodar:
##   godot --headless --path . res://tests/TestRunner.tscn
## Sai com código 0 se tudo passar, 1 caso contrário.

var failures: Array[String] = []
var passed := 0
var current := ""

func _ready() -> void:
	var tests := [
		"test_needs", "test_inventory", "test_skills", "test_shop", "test_job_flow",
		"test_job_abandon_and_burn", "test_quest_bribe_path", "test_quest_persuasion_and_buy",
		"test_dialogue_conditions", "test_law", "test_rent", "test_save_roundtrip", "test_save_migration_and_corruption",
		"test_data_integrity",
	]
	for t in tests:
		current = t
		Game.new_game()
		Game.world = self  # habilita simulação de tempo sem o mundo 3D
		call(t)
	Game.world = null
	print("")
	print("==== TESTES: %d verificações OK, %d falhas ====" % [passed, failures.size()])
	for f in failures:
		print("  FALHA: " + f)
	get_tree().quit(0 if failures.is_empty() else 1)

func check(cond: bool, msg: String) -> void:
	if cond:
		passed += 1
	else:
		failures.append("%s: %s" % [current, msg])

func approx(a: float, b: float, eps: float = 0.01) -> bool:
	return absf(a - b) <= eps

# ------------------------------------------------------------------
func test_needs() -> void:
	var n := Needs.new()
	n.v = {"fome": 0.0, "sede": 0.0, "sono": 0.0, "fadiga": 0.0, "higiene": 0.0}
	n.tick(2.0, "idle")
	check(approx(n.get_need("fome"), 9.0), "fome sobe 4.5/h")
	check(approx(n.get_need("sede"), 13.0), "sede sobe 6.5/h")
	n.set_need("sono", 90.0)
	n.tick(4.0, "sleep")
	check(approx(n.get_need("sono"), 40.0), "dormir reduz sono")
	n.set_need("fadiga", 0.0)
	n.tick(0.5, "run", 0)
	check(approx(n.get_need("fadiga"), 30.0), "correr cansa")
	n.consume({"fome": -100, "saude": -30})
	check(n.get_need("fome") == 0.0 and approx(n.saude, 70.0), "consumo aplica efeitos e limita")
	n.set_need("fadiga", 95.0)
	check(not n.can_run(), "exausto não corre")
	check(n.speed_multiplier() < 1.0, "fadiga reduz velocidade")
	check(n.work_multiplier() > 1.2, "fadiga torna o trabalho mais lento")
	n.set_need("sede", 100.0)
	var before := n.saude
	n.tick(1.0, "idle")
	check(n.saude < before, "desidratação fere")
	var labels := n.moodles().map(func(m): return m.id)
	check(labels.has("sede") and labels.has("fadiga"), "moodles aparecem quando relevantes")

func test_inventory() -> void:
	var inv := Inventory.new(1.0)
	check(inv.add("agua", 3) == 2, "capacidade estrita (0.5kg cada, cabe 2)")
	check(approx(inv.weight(), 1.0), "peso total")
	check(inv.add("agua", 1, 0.0, false) == 1, "não estrito ignora capacidade")
	var box := Inventory.new(10.0)
	check(inv.transfer_to(box, 0, 3) == 3, "transferência entre inventários")
	check(box.count("agua") == 3 and inv.count("agua") == 0, "contagem após transferir")
	var fridge := Inventory.new(10.0)
	fridge.refrigerated = true
	fridge.add("banana", 1)
	box.add("banana", 1)
	fridge.tick_age(80.0)
	box.tick_age(80.0)
	var fb: Dictionary = fridge.items[0]
	var bb: Dictionary = box.items.filter(func(s): return s.id == "banana")[0]
	check(not fridge.is_spoiled(fb), "geladeira conserva")
	check(box.is_spoiled(bb), "fora da geladeira estraga")
	box.add("banana", 1, 0.0)
	check(box.items.filter(func(s): return s.id == "banana").size() == 2, "perecíveis de idades diferentes não empilham")
	check(box.remove("banana", 1) == 1 and not box.is_spoiled(box.items.filter(func(s): return s.id == "banana")[0]), "remove o mais velho primeiro")
	var p := PlayerState.new()
	p.inventory.add("mochila", 1)
	check(approx(p.capacity(), 12.0), "capacidade base")
	p.equip("mochila")
	check(approx(p.capacity(), 20.0), "mochila aumenta capacidade")

func test_skills() -> void:
	var s := Skills.new()
	check(s.level("culinaria") == 0, "começa no nível 0")
	s.add_xp("culinaria", 140)
	check(s.level("culinaria") == 2, "XP vira nível")
	check(s.progress("culinaria") > 0.0 and s.progress("culinaria") < 1.0, "progresso parcial")

func test_shop() -> void:
	var p := Game.player
	p.money = 50
	var stock_before := int(Game.shops.stock.mercadinho.agua)
	var r := Game.shops.buy("mercadinho", "agua", 2, p, Game.relations)
	check(r.ok and p.money == 44, "compra debita (2 x R$3)")
	check(int(Game.shops.stock.mercadinho.agua) == stock_before - 2, "estoque diminui")
	check(not Game.shops.buy("mercadinho", "mochila", 2, p, Game.relations).ok, "sem estoque suficiente")
	p.money = 1
	check(not Game.shops.buy("mercadinho", "kit_curativo", 1, p, Game.relations).ok, "sem dinheiro não compra")
	Game.relations.change("antonio", 40)
	check(Game.shops.price_buy("mercadinho", "kit_curativo", Game.relations) < 25, "desconto de freguês")
	p.inventory.add("relogio_antigo", 1)
	var idx := -1
	for i in p.inventory.items.size():
		if p.inventory.items[i].id == "relogio_antigo":
			idx = i
	var money_before := p.money
	var s := Game.shops.sell("penhores", idx, 1, p)
	check(s.ok and p.money == money_before + int(floor(45 * 0.55)), "venda no penhor")
	check(not Game.shops.buys_item("mercadinho", "relogio_antigo"), "mercadinho não compra relógio")
	Game.relations.change("antonio", -100)
	check(Game.shops.refuses_service("mercadinho", Game.relations) != "", "dono hostil recusa atendimento")
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var total_before := 0
	for k in Game.shops.stock.mercadinho:
		total_before += int(Game.shops.stock.mercadinho[k])
	check(Game.shops.npc_purchase("mercadinho", rng) != "", "NPC compra algo")
	var total_after := 0
	for k in Game.shops.stock.mercadinho:
		total_after += int(Game.shops.stock.mercadinho[k])
	check(total_after == total_before - 1, "compra de NPC consome estoque")

func test_job_flow() -> void:
	var j := Game.job
	var p := Game.player
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	check(not j.clock_in(12.0, p.needs).ok, "não contratado não bate ponto")
	j.hire()
	check(j.employed, "contratação")
	check(not j.clock_in(8.0, p.needs).ok, "fechado às 8h")
	p.needs.set_need("higiene", 80.0)
	check(not j.clock_in(12.0, p.needs).ok, "sujo não trabalha")
	p.needs.set_need("higiene", 10.0)
	check(j.clock_in(12.0, p.needs).ok, "bate ponto")
	j.customer_cooldown = 0.0
	check(j.wants_customer(), "pede cliente")
	j.customer_dispatched(rng)
	var o := j.create_order("carro_1", rng)
	o.items = {"job_x_brasa": 1, "job_batata": 1, "job_refri": 1}
	o.remaining = o.items.duplicate()
	check(j.station_start("chapa", 0), "chapa inicia")
	check(not j.station_start("chapa", 0), "chapa ocupada")
	j.station_start("fritadeira", 0)
	j.tick(6.5, 12.2, true)
	check(j.stations.fritadeira.state == "ready", "batata pronta em 6 min")
	check(j.stations.chapa.state == "cooking", "hambúrguer ainda cozinhando")
	check(j.station_collect("fritadeira", p.inventory) == "job_batata", "retira batata")
	j.tick(3.0, 12.3, true)
	check(j.station_collect("chapa", p.inventory) == "job_x_brasa", "retira hambúrguer")
	var r1 := j.deliver(p.inventory, 0)
	check(r1.status == "partial", "entrega parcial sem refri")
	p.inventory.add("job_refri", 1)
	var r2 := j.deliver(p.inventory, 0)
	check(r2.status == "complete", "pedido completo")
	check(j.status_of("carro_1") == "done", "carro sabe que pode ir")
	j.tick(50.0, 13.2, true)
	var money_before := p.money
	var summary := Game.clock_out_player()
	check(summary.total > 0 and p.money == money_before + summary.total, "pagamento ao bater ponto de saída")
	check(summary.ok == 1, "1 pedido no resumo")
	check(p.inventory.count("job_refri") == 0, "itens do trabalho são recolhidos")

func test_job_abandon_and_burn() -> void:
	var j := Game.job
	var p := Game.player
	j.hire()
	j.clock_in(15.0, p.needs)
	j.station_start("chapa", 0)
	j.tick(9.0 + 7.5, 15.3, true)
	check(j.stations.chapa.state == "burnt", "hambúrguer queima se esquecido")
	check(j.station_collect("chapa", p.inventory) == "burnt", "descarta queimado")
	var s := j.tick(16.0, 15.6, false)
	check(s.get("reason", "") == "abandono", "abandono encerra turno")
	check(j.warnings == 1, "abandono gera advertência")
	j.add_warning("teste")
	j.add_warning("teste")
	check(j.fired and not j.employed, "3 advertências = demissão")

func _run_dialogue(npc: String, picks: Array) -> DialogueRunner:
	var d := DialogueRunner.new()
	d.start(npc, Data.npc(npc).dialogue)
	for pick in picks:
		var guard := 0
		while not d.finished and not d.has_choices() and guard < 10:
			d.advance()
			guard += 1
		if d.finished:
			break
		var found := false
		for c in d.choices():
			if str(c.text).contains(pick) and c.enabled:
				d.choose(c.index)
				found = true
				break
		check(found, "opção '%s' disponível em %s/%s" % [pick, npc, d.node_id])
		if not found:
			break
	while not d.finished and not d.has_choices():
		d.advance()
	return d

func test_quest_bribe_path() -> void:
	var p := Game.player
	p.money = 100
	_run_dialogue("lucia", ["Prazer", "Deixa comigo"])
	check(Game.quests.state_of("encomenda") == "active", "missão iniciada pela Lúcia")
	check(Game.quests.stage_of("encomenda") == "investigar", "etapa inicial")
	_run_dialogue("marcos", ["R$ 40"])
	check(p.money == 60, "suborno cobra R$ 40")
	check(p.inventory.has("encomenda_lucia"), "recebe encomenda")
	check(not Game.world_state.container("bau_marcos").has("encomenda_lucia"), "a caixa saiu do baú do Marcos")
	check(Game.quests.stage_of("encomenda") == "entregar", "etapa avança ao pegar o item")
	_run_dialogue("lucia", ["pagar R$ 40"])
	check(Game.quests.state_of("encomenda") == "completed", "missão concluída")
	check(Game.quests.outcome_of("encomenda") == "suborno", "desfecho suborno")
	check(p.money == 100, "Lúcia reembolsa")
	check(p.inventory.has("bolo_fuba"), "ganha bolo")
	check(not p.inventory.has("encomenda_lucia"), "entregou a caixa")
	var d := DialogueRunner.new()
	d.start("antonio", "antonio")
	check(d.node_id == "soube", "consequência: seu Antônio soube do favor")
	check(Game.shops.discount("mercadinho", Game.relations) >= 0.1, "consequência: desconto no mercadinho")

func test_quest_persuasion_and_buy() -> void:
	var p := Game.player
	Game.quests.start("encomenda")
	# Força sucesso: habilidade social alta + semente.
	p.skills.add_xp("social", 3300)
	Game.relations.change("marcos", 60)
	Game.rng.seed = 1
	_run_dialogue("marcos", ["remédio de pressão"])
	check(Game.has_flag("obtida_persuasao") and p.inventory.has("encomenda_lucia"), "persuasão bem-sucedida entrega a caixa")
	_run_dialogue("lucia", ["Conversei com o Marcos"])
	check(Game.quests.outcome_of("encomenda") == "diplomacia", "desfecho diplomacia")
	check(Game.relations.reputation >= 10, "reputação sobe")
	# Caminho alternativo em outra partida: comprar o remédio.
	Game.new_game()
	p = Game.player
	p.money = 100
	Game.quests.start("encomenda")
	Game.shops.buy("mercadinho", "remedio_pressao", 1, p, Game.relations)
	check(Game.quests.stage_of("encomenda") == "entregar", "comprar remédio também avança")
	_run_dialogue("lucia", ["presente"])
	check(Game.quests.outcome_of("encomenda") == "compra_presente", "desfecho presente")
	check(Game.world_state.container("bau_marcos").has("encomenda_lucia"), "Marcos ficou com a caixa")

func test_dialogue_conditions() -> void:
	var d := DialogueRunner.new()
	d.start("sandra", "sandra")
	check(d.node_id == "inicio", "Sandra começa no início")
	Game.player.needs.set_need("higiene", 90.0)
	_run_dialogue("sandra", ["contratando", "aprendo", "Topo"])
	check(not Game.job.employed, "sujo(a) não é contratado(a)")
	Game.player.needs.set_need("higiene", 0.0)
	_run_dialogue("sandra", ["contratando", "aprendo", "Topo"])
	check(Game.job.employed, "contratado(a) pela Sandra")
	var d2 := DialogueRunner.new()
	d2.start("marcos", "marcos")
	check(d2.node_id == "padrao", "Marcos padrão sem missão")
	Game.player.money = 10
	Game.quests.start("encomenda")
	var d3 := DialogueRunner.new()
	d3.start("marcos", "marcos")
	var bribe = d3.choices().filter(func(c): return str(c.text).contains("R$ 40"))
	check(bribe.size() == 1 and not bribe[0].enabled, "suborno aparece desabilitado sem dinheiro")
	var persu = d3.choices().filter(func(c): return c.chance >= 0.0)
	check(persu.size() >= 1 and str(persu[0].text).begins_with("["), "testes mostram a chance")
	# Tião: dica só com relação suficiente
	var d4 := DialogueRunner.new()
	d4.start("tiao", "tiao")
	check(d4.choices().filter(func(c): return str(c.text).contains("Marcos") and c.enabled).size() == 1, "pergunta sobre Marcos disponível")
	_run_dialogue("tiao", ["Marcos"])
	check(not Game.has_flag("dica_tapete"), "sem relação, Tião não conta")
	Game.player.money = 20
	_run_dialogue("tiao", ["R$ 5", ])
	_run_dialogue("tiao", ["Marcos"])
	check(Game.has_flag("dica_tapete"), "com relação, Tião dá a dica")

func test_law() -> void:
	var r := Game.relations
	Game.law.report("furto", "marcos", [], 1, r)
	check(Game.law.pending_fines == 0 and Game.law.unsolved.size() == 1, "furto sem testemunha fica sem autoria")
	Game.law.report("furto", "marcos", ["lucia"], 1, r)
	check(Game.law.pending_fines == 90, "multa pendente")
	check(r.reputation < 0, "reputação cai")
	check(r.has_memory("lucia", "viu_furto"), "testemunha lembra")
	check(r.get_rel("marcos") < -30, "vítima fica hostil")
	var f := Game.law.collect_fines()
	check(int(f.amount) == 90 and Game.law.pending_fines == 0, "multa cobrada")

func test_rent() -> void:
	var p := Game.player
	p.money = 200
	Game.property.charge_rent(p)
	check(p.money == 80, "aluguel cobrado")
	p.money = 50
	Game.property.charge_rent(p)
	check(p.money == 0 and Game.property.total_debt() == 70, "aluguel atrasado vira dívida")

func test_save_roundtrip() -> void:
	var p := Game.player
	p.money = 321
	p.inventory.add("cafe_lata", 2)
	p.skills.add_xp("direcao", 77)
	p.needs.set_need("fome", 42.0)
	Game.world_state.container("geladeira_jogador").add("sanduiche", 2)
	Game.world_state.set_door("porta_casa_jogador", true, false)
	Game.world_state.vehicles.carro_jogador.fuel = 3.5
	Game.quests.start("encomenda")
	Game.relations.change("lucia", 12)
	Game.job.hire()
	Game.set_flag("teste_flag")
	Clock.set_time(3, 14, 20)
	var json := JSON.stringify(Game.to_dict())
	Game.new_game()
	check(Game.player.money != 321, "estado resetado antes de carregar")
	Game.from_dict(JSON.parse_string(json))
	p = Game.player
	check(p.money == 321, "dinheiro preservado")
	check(p.inventory.count("cafe_lata") == 2, "inventário preservado")
	check(approx(p.needs.get_need("fome"), 42.0), "necessidades preservadas")
	check(int(p.skills.xp.get("direcao", 0)) == 77, "habilidades preservadas")
	check(Game.world_state.container("geladeira_jogador").count("sanduiche") == 2, "geladeira preservada")
	check(Game.world_state.doors.porta_casa_jogador.open, "porta preservada")
	check(approx(float(Game.world_state.vehicles.carro_jogador.fuel), 3.5), "combustível preservado")
	check(Game.quests.state_of("encomenda") == "active", "missão preservada")
	check(Game.job.employed, "emprego preservado")
	check(Game.has_flag("teste_flag"), "flags preservadas")
	check(Clock.day() == 3 and Clock.hour() == 14 and Clock.minute() == 20, "horário preservado")

func test_save_migration_and_corruption() -> void:
	var old := {"clock": {"total_minutes": 600.0}, "player": {"money": 5}}
	var m := Saves.migrate(old, 0)
	check(int(m.version) == 1 and m.has("property"), "migração v0 → v1")
	var path := Saves.path_for("slot3")
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string("{isso não é json")
	f.close()
	if FileAccess.file_exists(path + ".bak"):
		DirAccess.remove_absolute(path + ".bak")
	check(Saves.load_data("slot3").is_empty() and Saves.last_error != "", "arquivo corrompido é rejeitado com mensagem")
	Saves.delete_slot("slot3")

func test_data_integrity() -> void:
	check(Data.errors.is_empty(), "dados sem referências quebradas: %s" % str(Data.errors))
	for npc_id in Data.npcs():
		for e in Data.npc(npc_id).get("schedule", []):
			check(e.has("place") and e.has("activity"), "agenda válida de %s" % npc_id)
	for did in Data.db.dialogues:
		var nodes: Dictionary = Data.dialogue(did).nodes
		for nid in nodes:
			var n: Dictionary = nodes[nid]
			var targets: Array = []
			if n.get("next") != null:
				targets.append(n.next)
			for c in n.get("choices", []):
				for k in ["next", "success", "fail"]:
					if c.get(k) != null:
						targets.append(c[k])
			for t in targets:
				check(nodes.has(str(t)), "nó %s/%s aponta para %s existente" % [did, nid, t])
