extends Node
## Teste de aceitação do protótipo: executa os 9 critérios usando o mundo real
## (física, navegação, trânsito, interface e sistemas). Rodar:
##   godot --headless --path . res://tests/AcceptanceTest.tscn
## Sai com código 0 se todos os passos passarem.

var world: Node
var results: Array = []
var failures := 0

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	await _run()
	print("")
	print("===== RESULTADO DO TESTE DE ACEITAÇÃO =====")
	for r in results:
		print(("  OK     " if r.ok else "  FALHA  ") + r.msg)
	print("===== %d verificações, %d falhas =====" % [results.size(), failures])
	Engine.time_scale = 1.0
	get_tree().quit(0 if failures == 0 else 1)

func check(cond: bool, msg: String) -> bool:
	results.append({"ok": cond, "msg": msg})
	if not cond:
		failures += 1
		print("  FALHA: " + msg)
	else:
		print("  ok: " + msg)
	return cond

func step(title: String) -> void:
	print("\n== PASSO: " + title)

func wait(t: float) -> void:
	await get_tree().create_timer(t, true, false, false).timeout

func frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame

## Avança o relógio até a hora pedida (sempre para a frente), como um salto de tempo.
func skip_to(h: int, m: int = 0) -> void:
	var target := (Clock.day() - 1) * 1440.0 + h * 60.0 + m
	if target <= Clock.total_minutes:
		target += 1440.0
	var jump := target - Clock.total_minutes
	Clock.advance(jump)
	if jump > 8 * 60:
		fed()
	await frames(3)

## O teste verifica sistemas, não a sobrevivência: após saltos longos, simula que o
## personagem comeu e bebeu.
func fed() -> void:
	var n := Game.player.needs
	n.v = {"fome": 20.0, "sede": 20.0, "sono": 20.0, "fadiga": 0.0, "higiene": 15.0}
	n.saude = 100.0

func start_world() -> void:
	world = load("res://src/world/GameWorld.tscn").instantiate()
	add_child(world)
	while not world.ready_done:
		await frames(1)
	await frames(30)
	world.ui.close_all()

func player() -> Player:
	return world.player

## Anda com o controle real (ações de input) até o alvo. Retorna true se chegou.
func walk_to(target: Vector3, timeout: float = 20.0, run: bool = false) -> bool:
	var t := 0.0
	if run:
		Input.action_press("run")
	while t < timeout:
		var to := target - player().global_position
		to.y = 0
		if to.length() < 0.6:
			break
		var local := Basis(Vector3.UP, -world.camera_yaw()) * to.normalized()
		_set_axis(local.x, local.z)
		await frames(1)
		t += get_physics_process_delta_time() * Engine.time_scale
	_set_axis(0, 0)
	Input.action_release("run")
	await frames(2)
	var d := (target - player().global_position)
	d.y = 0
	return d.length() < 1.0

func _set_axis(x: float, z: float) -> void:
	for a in ["move_left", "move_right", "move_up", "move_down"]:
		Input.action_release(a)
	if x > 0.05: Input.action_press("move_right", x)
	if x < -0.05: Input.action_press("move_left", -x)
	if z > 0.05: Input.action_press("move_down", z)
	if z < -0.05: Input.action_press("move_up", -z)

func teleport(p: Vector3) -> void:
	if player().state != "free":
		player().stand_up()
	player().global_position = p + Vector3(0, 0.05, 0)
	player().velocity = Vector3.ZERO
	await frames(3)

## Usa o sistema de foco do jogador (o que aparece no HUD) e executa a ação pedida.
func use_focus(expected: Node, action_id: String) -> bool:
	player()._scan_interactables()
	if player().focus != expected:
		check(false, "foco esperado em %s, mas está em %s (jogador em %s, alvo em %s)" % [expected.name, player().focus.name if player().focus else "nada", str(player().global_position), str(expected.interact_point())])
		return false
	for a in player().focus_actions:
		if a.id == action_id:
			if not a.enabled:
				check(false, "ação %s desabilitada: %s" % [action_id, a.get("hint", "")])
				return false
			expected.do_action(player(), action_id)
			return true
	check(false, "ação %s não encontrada em %s" % [action_id, str(player().focus_actions)])
	return false

func wait_timed() -> void:
	for i in 1200:
		if player().timed.is_empty():
			return
		await frames(1)

func choose(panel: Node, contains: String) -> bool:
	var guard := 0
	while guard < 30 and not panel.runner.finished and panel.runner.choices().is_empty():
		panel.runner.advance()
		guard += 1
	for c in panel.runner.choices():
		if str(c.text).contains(contains) and c.enabled:
			panel._choose(c.index)
			return true
	check(false, "opção de diálogo '%s' não encontrada" % contains)
	return false

func finish_dialogue(panel: Node) -> void:
	var guard := 0
	while is_instance_valid(panel) and not panel.runner.finished and guard < 20:
		if panel.runner.choices().is_empty():
			panel.runner.advance()
		else:
			break
		guard += 1
	if world.ui.is_dialogue_open():
		world.ui.close_current()
	await frames(2)

# ======================================================================
func _run() -> void:
	Game.new_game()
	Game.pending_load = {"new": true}
	Game.world_state.weather = {"state": "limpo", "until": 1e9}
	await start_world()
	var p := Game.player

	# ------------------------------------------------------------ 1
	step("1. Sair de casa e explorar o bairro")
	check(world.player_building == "casa_jogador", "o jogo começa dentro de casa")
	var door: Door = world.doors["porta_casa_jogador"]
	check(await walk_to(door.global_position + Vector3(0, 0, -1.2)), "andou até a porta usando o controle")
	check(use_focus(door, "open"), "porta aberta por dentro com a interação")
	await wait(0.4)
	check(door.is_open, "porta está aberta")
	check(await walk_to(Vector3(-44, 0, -6.0), 15.0, true), "saiu correndo até a calçada")
	await wait(0.3)
	check(world.player_building == "", "o jogador está na rua")
	check(p.needs.get_need("fadiga") > 0.0 or p.skills.xp.get("condicionamento", 0) >= 0, "correr gera fadiga")

	# ------------------------------------------------------------ 2
	step("2. Encontrar NPCs realizando atividades")
	await skip_to(8, 30)
	await wait(1.0)
	var lucia: NPC = world.npcs["lucia"]
	var antonio: NPC = world.npcs["antonio"]
	var marcos: NPC = world.npcs["marcos"]
	check(lucia.activity == "sit" and lucia.spot_id == "praca_banco_1", "Dona Lúcia está sentada no banco da praça às 8h30 (%s/%s)" % [lucia.activity, lucia.spot_id])
	check(antonio.activity == "work" and antonio.spot_id == "mercadinho_balcao", "Seu Antônio trabalha no balcão do mercadinho")
	check(marcos.activity == "work" and marcos.spot_id == "oficina_marcos", "Marcos trabalha na oficina")
	# Rotina em movimento: às 10h a Lúcia vai ao mercadinho a pé.
	Engine.time_scale = 6.0
	await skip_to(9, 28)
	var start_pos := lucia.global_position
	var moved := false
	var arrived := false
	for i in 2400:
		await frames(1)
		if i % 300 == 0:
			print("    Lúcia: ", lucia.global_position, " ", lucia.state, " ", lucia.spot_id, " ", lucia.path_i, "/", lucia.path.size())
		if lucia.state == "moving":
			moved = true
		if lucia.spot_id in ["mercadinho_cliente", "mercadinho_fila"] and lucia.state == "at_spot":
			arrived = true
			break
	check(moved, "Lúcia saiu andando pela malha de navegação")
	check(arrived, "Lúcia chegou ao mercadinho (%.0f m percorridos)" % start_pos.distance_to(lucia.global_position))
	check(Game.world.traffic.cars.size() > 0, "há carros circulando no trânsito (%d)" % Game.world.traffic.cars.size())
	Engine.time_scale = 1.0

	# ------------------------------------------------------------ 3
	step("3. Dirigir um veículo")
	var car: Vehicle = world.vehicles["carro_jogador"]
	await teleport(car.interact_point() + Vector3(-0.6, 0, 0))
	var fuel0 := car.fuel
	var pos0 := car.global_position
	check(use_focus(car, "enter"), "entrou no próprio carro")
	await frames(5)
	check(player().state == "driving", "jogador está dirigindo")
	# Acelera pouco para não deixar o carro parado na avenida (bloquearia o trânsito).
	Input.action_press("move_up")
	await wait(1.4)
	Input.action_release("move_up")
	Input.action_press("move_down")
	for i in 600:
		await frames(1)
		if absf(car.speed) < 0.8:
			break
	Input.action_release("move_down")
	await wait(0.3)
	check(car.global_position.distance_to(pos0) > 3.0 and car.global_position.z < -5.0, "o carro andou %.1f m com aceleração e frenagem" % car.global_position.distance_to(pos0))
	check(car.fuel < fuel0, "consumiu combustível (%.3f → %.3f L)" % [fuel0, car.fuel])
	player().exit_vehicle()
	await frames(3)
	check(player().state == "free" and p.vehicle_id == "", "saiu do carro")
	var saved_car: Dictionary = Game.world_state.vehicles["carro_jogador"]
	check(Vector3(saved_car.pos[0], 0, saved_car.pos[2]).distance_to(car.global_position) < 0.5, "posição do carro registrada no estado do mundo")

	# ------------------------------------------------------------ 4
	step("4. Entrar em um estabelecimento")
	await skip_to(10, 40)
	fed()
	var mdoor: Door = world.doors["porta_mercadinho"]
	await teleport(mdoor.global_position + Vector3(0, 0, 1.2))
	await wait(0.4)
	check(not mdoor.locked(), "mercadinho destrancado no horário comercial")
	check(use_focus(mdoor, "open"), "abriu a porta do mercadinho")
	check(await walk_to(mdoor.global_position + Vector3(0, 0, -2.0)), "entrou andando")
	await wait(0.3)
	check(world.player_building == "mercadinho", "jogador está dentro do Mercadinho Bom Preço")

	# ------------------------------------------------------------ 6
	step("6. Comprar alimentos e cuidar das necessidades")
	var counter: Furniture = world.furniture["balcao_mercadinho"]
	await teleport(counter.interact_point())
	var money_before := p.money
	check(use_focus(counter, "shop"), "balcão aberto com o Seu Antônio presente")
	await frames(3)
	check(world.ui.current_kind == "shop", "janela da loja aberta")
	var shop_panel: Node = world.ui.current_panel
	shop_panel._buy("sanduiche")
	shop_panel._buy("agua")
	check(p.inventory.has("sanduiche") and p.money == money_before - 13, "comprou sanduíche e água (R$ %d → R$ %d)" % [money_before, p.money])
	world.ui.close_current()
	p.needs.set_need("fome", 60.0)
	var inv_panel: Node = load("res://src/ui/InventoryPanel.gd").new()
	world.ui.open_panel("inventory", inv_panel)
	for i in p.inventory.items.size():
		if p.inventory.items[i].id == "sanduiche":
			inv_panel.selected = i
	inv_panel._use()
	world.ui.close_current()
	check(p.needs.get_need("fome") < 40.0 and not p.inventory.has("sanduiche"), "comeu o sanduíche: fome 60 → %d" % int(p.needs.get_need("fome")))

	# ------------------------------------------------------------ 5
	step("5. Trabalhar no drive-thru e receber pagamento")
	await skip_to(11, 10)
	await wait(1.0)
	var sandra: NPC = world.npcs["sandra"]
	check(sandra.spot_id == "brasa_escritorio" and sandra.state == "at_spot", "Sandra está no escritório do Brasa")
	await teleport(sandra.global_position + Vector3(0, 0, 1.2))
	p.needs.set_need("higiene", 20.0)
	check(use_focus(sandra, "talk"), "conversou com a gerente")
	await frames(3)
	var dlg: Node = world.ui.current_panel
	check(Clock.is_held(), "o relógio pausa durante o diálogo")
	choose(dlg, "contratando")
	choose(dlg, "aprendo rápido")
	choose(dlg, "Topo")
	await finish_dialogue(dlg)
	check(Game.job.employed, "foi contratado(a) pelo Brasa Burger")
	check(not Clock.is_held(), "relógio voltou a correr após o diálogo")
	var tc: Furniture = world.furniture["relogio_ponto"]
	await teleport(tc.interact_point())
	check(use_focus(tc, "clock_in"), "bateu o ponto de entrada")
	check(Game.job.on_shift, "turno iniciado")
	var money_shift := p.money
	Engine.time_scale = 4.0
	var order: Dictionary = {}
	for i in 60 * 60:
		await frames(1)
		order = Game.job.front_order()
		if not order.is_empty():
			break
	check(not order.is_empty(), "um carro do trânsito entrou na fila e fez o pedido na janela: %s" % (DriveThruJob.order_text(order.get("items", {})) if not order.is_empty() else "—"))
	if not order.is_empty():
		var need: Dictionary = order.items
		var grill: Furniture = world.furniture["estacao_chapa"]
		var fryer: Furniture = world.furniture["estacao_fritadeira"]
		var soda: Furniture = world.furniture["estacao_refri"]
		for k in range(int(need.get("job_x_brasa", 0))):
			await teleport(grill.interact_point())
			use_focus(grill, "start")
			await wait_timed()
			for w in 2400:
				if Game.job.stations.chapa.state == "ready":
					break
				await frames(1)
			use_focus(grill, "collect")
			await wait_timed()
		for k in range(int(need.get("job_batata", 0))):
			await teleport(fryer.interact_point())
			use_focus(fryer, "start")
			await wait_timed()
			for w in 2400:
				if Game.job.stations.fritadeira.state == "ready":
					break
				await frames(1)
			use_focus(fryer, "collect")
			await wait_timed()
		for k in range(int(need.get("job_refri", 0))):
			await teleport(soda.interact_point())
			use_focus(soda, "soda")
			await wait_timed()
		var window: Furniture = world.furniture["janela_drive"]
		await teleport(window.interact_point())
		check(use_focus(window, "deliver"), "entregou o pedido na janela")
		await wait_timed()
		check(int(Game.job.shift.ok) >= 1, "pedido completo registrado (gorjeta R$ %d)" % int(Game.job.shift.tips))
		var car_left := false
		for i in 600:
			await frames(1)
			if Game.job.finished.is_empty():
				car_left = true
				break
		check(car_left, "o cliente saiu da janela e voltou para a rua")
	Engine.time_scale = 1.0
	await skip_to(13, 0)
	await teleport(tc.interact_point())
	check(use_focus(tc, "clock_out"), "bateu o ponto de saída")
	await frames(3)
	check(not Game.job.on_shift and p.money > money_shift, "recebeu pagamento do turno: R$ %d → R$ %d" % [money_shift, p.money])
	check(p.skills.xp.get("culinaria", 0.0) > 0.0, "ganhou experiência em culinária")
	world.ui.close_all()

	# ------------------------------------------------------------ 7
	step("7. Voltar para casa, guardar objetos e descansar")
	var fridge: Furniture = world.furniture["geladeira_jogador"]
	await teleport(fridge.interact_point())
	await wait(0.3)
	check(world.player_building == "casa_jogador", "de volta em casa")
	check(use_focus(fridge, "open"), "abriu a geladeira")
	await frames(3)
	var cpanel: Node = world.ui.current_panel
	var fridge_inv: Inventory = Game.world_state.container("geladeira_jogador")
	var agua_before := fridge_inv.count("agua")
	var idx := -1
	for i in p.inventory.items.size():
		if p.inventory.items[i].id == "agua":
			idx = i
	cpanel._move(true, idx, 1)
	check(fridge_inv.count("agua") == agua_before + 1, "guardou uma garrafa de água na geladeira")
	world.ui.close_current()
	var wardrobe_inv: Inventory = Game.world_state.container("guarda_roupa_jogador")
	wardrobe_inv.add("radio_velho", 1)
	p.needs.set_need("sono", 70.0)
	var t0 := Clock.total_minutes
	var bed: Furniture = world.furniture["cama_jogador"]
	await teleport(bed.global_position + Vector3(1.0, 0, 0.6))
	player().rotation.y = atan2(1.0, 0.6)
	check(use_focus(bed, "sleep"), "deitou para dormir")
	await frames(3)
	check(world.ui.current_kind == "sleep", "janela de sono aberta")
	world.ui.close_current()
	await world.do_sleep(8, "cama_jogador")
	check(Clock.total_minutes - t0 >= 7.5 * 60, "o tempo avançou %.1f h" % ((Clock.total_minutes - t0) / 60.0))
	check(p.needs.get_need("sono") < 20.0, "descansou: sono 70 → %d" % int(p.needs.get_need("sono")))
	check(Saves.exists("auto"), "salvamento automático ao dormir")

	# ------------------------------------------------------------ 8
	step("8. Conversar com um NPC e iniciar uma missão")
	await skip_to(15, 30)
	fed()
	await wait(1.0)
	check(lucia.spot_id == "calcada_lucia", "Lúcia está na calçada da casa dela à tarde")
	await teleport(lucia.global_position + Vector3(0, 0, 1.3))
	check(use_focus(lucia, "talk"), "iniciou conversa com a Dona Lúcia")
	await frames(3)
	var d2: Node = world.ui.current_panel
	choose(d2, "Prazer")
	choose(d2, "Deixa comigo")
	await finish_dialogue(d2)
	check(Game.quests.state_of("encomenda") == "active", "missão \"A Encomenda Extraviada\" iniciada")
	check(not world.marker_positions().is_empty(), "marcador de missão aponta para o Marcos")
	# Uma das soluções: suborno. Marcos fica na praça no fim da tarde.
	await skip_to(17, 15)
	await wait(1.0)
	p.money = max(p.money, 60)
	await teleport(marcos.global_position + Vector3(0, 0, 1.3))
	check(use_focus(marcos, "talk"), "conversou com o Marcos")
	await frames(3)
	var d3: Node = world.ui.current_panel
	choose(d3, "R$ 40")
	await finish_dialogue(d3)
	check(p.inventory.has("encomenda_lucia") and Game.quests.stage_of("encomenda") == "entregar", "conseguiu a encomenda (etapa: entregar)")
	await skip_to(18, 0)
	await wait(1.0)
	await teleport(lucia.global_position + Vector3(0.6, 0, 0.9))
	if lucia.is_sleeping() or lucia.state == "hidden":
		check(false, "Lúcia indisponível às 18h")
	elif use_focus(lucia, "talk"):
		await frames(3)
		var d4: Node = world.ui.current_panel
		choose(d4, "pagar R$ 40")
		await finish_dialogue(d4)
	check(Game.quests.state_of("encomenda") == "completed" and Game.quests.outcome_of("encomenda") == "suborno", "missão concluída pelo caminho do suborno")
	check(Game.relations.get_rel("lucia") > 20, "relação com a Lúcia melhorou (%d)" % Game.relations.get_rel("lucia"))

	# ------------------------------------------------------------ 9
	step("9. Salvar, sair e carregar preservando o progresso")
	var snapshot := {
		"money": p.money, "fome": p.needs.get_need("fome"), "time": Clock.total_minutes,
		"fridge_agua": fridge_inv.count("agua"), "radio": wardrobe_inv.count("radio_velho"),
		"car": world.vehicles["carro_jogador"].global_position, "fuel": world.vehicles["carro_jogador"].fuel,
		"quest": Game.quests.outcome_of("encomenda"), "employed": Game.job.employed,
		"orders": Game.job.total_orders, "rel": Game.relations.get_rel("lucia"), "bolo": p.inventory.count("bolo_fuba"),
		"pos": player().global_position, "door": Game.world_state.doors.porta_casa_jogador.open,
	}
	check(Saves.block_reason() == "", "nada impede o salvamento")
	check(Saves.save_game("slot1"), "jogo salvo no espaço 1")
	world.queue_free()
	await frames(5)
	Game.new_game()
	check(Game.player.money != snapshot.money or Game.quests.state_of("encomenda") == "not_started", "estado zerado (simulando sair do jogo)")
	var data := Saves.load_data("slot1")
	check(not data.is_empty(), "arquivo de salvamento lido e validado")
	Game.from_dict(data)
	Game.pending_load = {"loaded": true}
	await start_world()
	p = Game.player
	check(p.money == snapshot.money, "dinheiro preservado (R$ %d)" % p.money)
	check(absf(p.needs.get_need("fome") - snapshot.fome) < 0.5, "necessidades preservadas")
	check(absf(Clock.total_minutes - snapshot.time) < 2.0, "data e hora preservadas (%s, dia %d)" % [Clock.time_string(), Clock.day()])
	check(Game.world_state.container("geladeira_jogador").count("agua") == snapshot.fridge_agua, "geladeira com os mesmos itens")
	check(Game.world_state.container("guarda_roupa_jogador").count("radio_velho") == snapshot.radio, "guarda-roupa preservado")
	check(world.vehicles["carro_jogador"].global_position.distance_to(snapshot.car) < 0.5, "carro estacionado no mesmo lugar")
	check(absf(world.vehicles["carro_jogador"].fuel - snapshot.fuel) < 0.01, "combustível preservado")
	check(Game.quests.outcome_of("encomenda") == snapshot.quest, "missão e desfecho preservados")
	check(Game.job.employed == snapshot.employed and Game.job.total_orders == snapshot.orders, "emprego e histórico preservados")
	check(Game.relations.get_rel("lucia") == snapshot.rel, "relacionamentos preservados")
	check(p.inventory.count("bolo_fuba") == snapshot.bolo, "inventário preservado (bolo da Lúcia)")
	check(player().global_position.distance_to(snapshot.pos) < 1.5, "posição do jogador preservada")
	check(Game.world_state.doors.porta_casa_jogador.open == snapshot.door, "estado da porta preservado")
	var d5 := DialogueRunner.new()
	d5.start("antonio", "antonio")
	check(d5.node_id == "soube", "consequência observável após carregar: Seu Antônio comenta o favor e oferece desconto")
