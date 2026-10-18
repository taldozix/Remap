extends "res://tests/AcceptanceTest.gd"
## Aceitação da Fase 1 (profundidade da vida): comprar e usar propriedades, garagem
## com conserto pelo próprio jogador, roupas e temperatura na chuva, emprego de
## entregador, eventos dinâmicos (carteira, ferido, discussão), compra de carro e
## persistência de tudo isso. Rodar:
##   godot --headless --path . res://tests/AcceptanceFase1.tscn

func _run() -> void:
	Game.new_game()
	Game.player.apply_traits(["mao_na_graxa", "socorrista"])
	Game.pending_load = {"new": true}
	Game.world_state.weather = {"state": "limpo", "until": 1e9}
	await start_world()
	var p := Game.player
	# Configuração do teste: economias de algumas semanas de trabalho.
	p.money = 3000

	# ------------------------------------------------------------ A
	step("A. Comprar uma garagem, guardar o carro e consertá-lo")
	await skip_to(9, 0)
	check(p.skills.level("mecanica") == 2 and p.skills.level("primeiros_socorros") == 2, "traços escolhidos deram habilidades iniciais")
	var sign_g: Furniture = world.furniture["placa_garagem_3"]
	await teleport(sign_g.interact_point() + Vector3(-0.8, 0, 0))
	check(use_focus(sign_g, "buy_property"), "comprou a Garagem nº 3 pela placa")
	check(Game.property.owns("garagem_3") and p.money == 2550, "propriedade registrada e paga (R$ %d)" % p.money)
	check(world.ui.current_kind == "message", "mensagem de confirmação da compra")
	world.ui.close_all()
	var gdoor: Door = world.doors["portao_garagem_3"]
	await teleport(gdoor.global_position + Vector3(1.3, 0, 0.4))
	check(use_focus(gdoor, "toggle"), "abriu o portão com a própria chave")
	await wait(1.5)
	var car: Vehicle = world.vehicles["carro_jogador"]
	car.global_position = Vector3(53.5, 0, -30.0)
	car.rotation.y = deg_to_rad(90.0)
	await frames(3)
	await teleport(car.interact_point() + Vector3(0, 0, -0.6))
	check(use_focus(car, "enter"), "entrou no carro na frente da garagem")
	Input.action_press("move_up")
	for i in 240:
		await frames(1)
		if car.global_position.x < 46.5:
			break
	Input.action_release("move_up")
	Input.action_press("move_down")
	for i in 300:
		await frames(1)
		if absf(car.speed) < 0.5:
			break
	Input.action_release("move_down")
	await wait(0.3)
	check(world.building_at(car.global_position) == "garagem_3", "dirigiu para dentro da garagem (x=%.1f)" % car.global_position.x)
	player().exit_vehicle()
	await frames(3)
	var counter: Furniture = world.furniture["balcao_oficina"]
	await teleport(counter.interact_point())
	check(use_focus_action(["shop"]), "balcão da oficina aberto com o Rafael")
	await frames(3)
	world.ui.current_panel._buy("kit_reparo")
	world.ui.close_current()
	check(p.inventory.has("kit_reparo"), "comprou um kit de reparo")
	car.damage = 50.0
	await teleport(car.interact_point())
	var xp0 := float(p.skills.xp.get("mecanica", 0.0))
	check(use_focus(car, "repair"), "começou o conserto")
	await wait_timed()
	check(car.damage <= 50.0 - 24.0 + 0.1, "conserto na bancada da garagem própria rende mais (dano 50 → %d)" % int(car.damage))
	check(float(p.skills.xp.get("mecanica", 0.0)) > xp0, "ganhou experiência em mecânica")
	var shelf: Furniture = world.furniture["prateleira_garagem_3"]
	p.inventory.add("radio_velho", 1)
	await teleport(shelf.interact_point())
	check(use_focus(shelf, "open"), "abriu a prateleira da garagem")
	await frames(3)
	var crimes := Game.law.record.size()
	var idx := -1
	for i in p.inventory.items.size():
		if p.inventory.items[i].id == "radio_velho":
			idx = i
	world.ui.current_panel._move(true, idx, 1)
	world.ui.current_panel._move(false, 0, 1)
	world.ui.current_panel._move(true, p.inventory.items.size() - 1, 1)
	world.ui.close_current()
	check(Game.world_state.container("prateleira_garagem_3").has("radio_velho"), "guardou o rádio na garagem")
	check(Game.law.record.size() == crimes, "mexer nas próprias coisas não é furto")

	# ------------------------------------------------------------ B
	step("B. Comprar uma casa e usá-la")
	var sign_c: Furniture = world.furniture["placa_casa_5"]
	await teleport(sign_c.interact_point() + Vector3(0.8, 0, 0))
	check(use_focus(sign_c, "buy_property"), "comprou a Casa nº 5")
	check(Game.property.owned_homes().size() == 2, "agora tem duas casas")
	world.ui.close_all()
	var door5: Door = world.doors["porta_casa_5"]
	await teleport(door5.global_position + Vector3(-1.2, 0, 0))
	check(use_focus(door5, "unlock"), "a chave da casa nova abre a porta")
	await wait(0.4)
	var entered := await walk_to(door5.global_position + Vector3(2.0, 0, 0))
	if not entered:
		print("    jogador em ", player().global_position, " porta ", door5.global_position, " aberta=", door5.is_open)
	check(entered, "entrou na casa nova")
	await wait(0.3)
	check(world.player_building == "casa_5", "jogador dentro da casa 5")
	var bed5: Furniture = world.furniture["cama_casa5"]
	var acts: Array = bed5.get_actions(player())
	check(acts.size() > 0 and acts[0].id == "sleep" and acts[0].enabled, "a cama da casa nova pode ser usada")

	# ------------------------------------------------------------ C
	step("C. Roupas e temperatura na chuva")
	await skip_to(10, 0)
	Game.world_state.weather = {"state": "chuva", "until": Clock.total_minutes + 600}
	await teleport(Vector3(-30, 0, -6.5))
	p.needs.body_temp = 36.6
	p.needs.wetness = 0.0
	Engine.time_scale = 6.0
	await wait(60.0)
	Engine.time_scale = 1.0
	check(p.needs.wetness > 60.0, "andar na chuva sem proteção molha a roupa (%d%%)" % int(p.needs.wetness))
	check(p.needs.body_temp < 36.4, "a temperatura corporal caiu (%.1f °C)" % p.needs.body_temp)
	var wardrobe: Furniture = world.furniture["guarda_roupa_jogador"]
	await teleport(wardrobe.interact_point())
	check(use_focus(wardrobe, "open"), "abriu o guarda-roupa")
	await frames(3)
	var wr: Inventory = Game.world_state.container("guarda_roupa_jogador")
	for item in ["capa_chuva", "casaco"]:
		for i in wr.items.size():
			if wr.items[i].id == item:
				world.ui.current_panel._move(false, i, 1)
				break
	world.ui.close_current()
	check(p.equip("capa_chuva") and p.equip("casaco"), "vestiu capa de chuva e casaco")
	await frames(3)
	check(player().visual.appearance.get("jacket", false), "o personagem aparece de casaco")
	await teleport(Vector3(-30, 0, -6.5))
	var wet0 := p.needs.wetness
	Engine.time_scale = 6.0
	await wait(60.0)
	Engine.time_scale = 1.0
	check(p.needs.wetness < wet0 - 20.0, "com a capa, a roupa seca mesmo na chuva (%d%% → %d%%)" % [int(wet0), int(p.needs.wetness)])
	check(p.needs.body_temp > 36.0, "agasalhado(a), a temperatura se recupera (%.1f °C)" % p.needs.body_temp)
	Game.world_state.weather = {"state": "limpo", "until": 1e9}

	# ------------------------------------------------------------ D
	step("D. Trabalho de entregador")
	await skip_to(14, 0)
	fed()
	var antonio: NPC = world.npcs["antonio"]
	var bal0: Furniture = world.furniture["balcao_mercadinho"]
	await teleport(bal0.interact_point())
	await wait(0.3)
	check(use_focus_action(["talk_staff", "talk"]), "conversou com o Seu Antônio no balcão")
	await frames(3)
	var d1: Node = world.ui.current_panel
	choose(d1, "entregador")
	choose(d1, "Fechado")
	await finish_dialogue(d1)
	check(Game.delivery.hired, "virou entregador do mercadinho")
	var bal: Furniture = world.furniture["balcao_mercadinho"]
	await teleport(bal.interact_point())
	check(use_focus_action(["delivery"]), "pegou uma entrega no balcão")
	check(p.inventory.has("sacola_entrega") and not Game.delivery.active.is_empty(), "sacola de %s no inventário" % Game.delivery.active.get("label", "?"))
	check(not world.marker_positions().is_empty(), "marcador aponta o endereço")
	var drop: DeliveryDrop = world._drop
	await teleport(drop.global_position + Vector3(0.5, 0, 0.5))
	var m0 := p.money
	check(use_focus(drop, "deliver"), "entregou no endereço")
	check(p.money > m0 + 9 and Game.delivery.total == 1, "recebeu pela entrega no prazo (R$ %d)" % (p.money - m0))

	# ------------------------------------------------------------ E
	step("E. Eventos dinâmicos")
	check(Game.director.fire("carteira_perdida"), "evento: carteira perdida")
	var wallet_owner := ""
	for id in world.npcs:
		if Game.has_flag("carteira_dono_" + id):
			wallet_owner = id
	var gi: GroundItem = null
	for n in world.get_node("Items").get_children():
		if n is GroundItem and n.entry.id == "carteira_perdida":
			gi = n
	check(gi != null and wallet_owner != "", "carteira caída no chão; dono: %s" % wallet_owner)
	if gi:
		await teleport(gi.global_position + Vector3(0.4, 0, 0.4))
		check(use_focus(gi, "take"), "pegou a carteira")
		var owner_npc: NPC = world.npcs[wallet_owner]
		if owner_npc.state == "hidden":
			owner_npc._set_hidden(false)
		await teleport(owner_npc.global_position + Vector3(0, 0, 1.2))
		var rel0 := Game.relations.get_rel(wallet_owner)
		check(use_focus_action(["return_wallet"]), "devolveu a carteira ao dono")
		check(Game.relations.get_rel(wallet_owner) >= rel0 + 15 and not p.inventory.has("carteira_perdida"), "dono agradece: relação +15")
	check(Game.director.fire("pedestre_ferido"), "evento: pedestre ferido")
	var hurt: NPC = null
	for n in world.npcs.values():
		if n.state == "knocked":
			hurt = n
	check(hurt != null, "há um pedestre caído")
	if hurt:
		p.inventory.add("kit_curativo", 1)
		await teleport(hurt.global_position + Vector3(0, 0, 1.0))
		var fa0 := float(p.skills.xp.get("primeiros_socorros", 0.0))
		var rep0 := Game.relations.reputation
		check(use_focus(hurt, "first_aid"), "prestou primeiros socorros")
		await wait_timed()
		check(hurt.state != "knocked" and float(p.skills.xp.get("primeiros_socorros", 0.0)) > fa0, "o pedestre se levantou; experiência em primeiros socorros")
		check(Game.relations.reputation > rep0, "a reputação no bairro subiu")

	# ------------------------------------------------------------ F
	step("F. Comprar um carro usado")
	await skip_to(15, 30)
	fed()
	var bal_of: Furniture = world.furniture["balcao_oficina"]
	await teleport(bal_of.interact_point())
	check(use_focus_action(["talk_staff", "talk"]), "conversou com o Rafael no balcão da oficina")
	await frames(3)
	var d2: Node = world.ui.current_panel
	choose(d2, "sedã verde")
	var m1 := p.money
	choose(d2, "Fechado")
	await finish_dialogue(d2)
	check(Game.world_state.vehicles.carro_usado.owner == "player" and p.money == m1 - 700, "comprou o sedã usado (R$ 700)")
	var used: Vehicle = world.vehicles["carro_usado"]
	await teleport(used.interact_point())
	check(use_focus(used, "enter"), "a chave do carro novo funciona")
	player().exit_vehicle()

	# ------------------------------------------------------------ G
	step("G. Discussão na praça resolvida na conversa")
	await skip_to(17, 20)
	await wait(1.0)
	p.skills.add_xp("social", 3300)
	var fired := Game.director.fire("discussao_praca")
	check(fired, "Marcos e Tião começaram a discutir na praça")
	if fired:
		var marcos: NPC = world.npcs["marcos"]
		await teleport(marcos.global_position + Vector3(0.0, 0, 1.2))
		var rep1 := Game.relations.reputation
		var calmed := false
		for attempt in 4:
			if not Game.director.is_active("discussao_praca"):
				break
			use_focus(marcos, "calm")
			await frames(2)
		calmed = not Game.director.is_active("discussao_praca")
		check(calmed and Game.relations.reputation > rep1, "apaziguou a discussão (reputação %d → %d)" % [rep1, Game.relations.reputation])

	# ------------------------------------------------------------ H
	step("H. Salvar e carregar preservando imóveis, veículos e roupas")
	var snap := {
		"garage": Game.property.owns("garagem_3"), "house": Game.property.owns("casa_5"),
		"shelf": Game.world_state.container("prateleira_garagem_3").count("radio_velho"),
		"car_pos": car.global_position, "car_dmg": car.damage, "used": Game.world_state.vehicles.carro_usado.owner,
		"delivery": Game.delivery.total, "wear": p.is_equipped("casaco"), "traits": p.traits.duplicate(), "money": p.money,
	}
	world.ui.close_all()
	check(Saves.save_game("slot2"), "salvou no espaço 2")
	world.queue_free()
	await frames(5)
	Game.new_game()
	Game.from_dict(Saves.load_data("slot2"))
	Game.pending_load = {"loaded": true}
	await start_world()
	p = Game.player
	check(Game.property.owns("garagem_3") == snap.garage and Game.property.owns("casa_5") == snap.house, "imóveis comprados preservados")
	check(Game.world_state.container("prateleira_garagem_3").count("radio_velho") == snap.shelf, "objeto guardado na garagem continua lá")
	check(world.vehicles["carro_jogador"].global_position.distance_to(snap.car_pos) < 0.5 and world.building_at(world.vehicles["carro_jogador"].global_position) == "garagem_3", "carro continua dentro da garagem")
	check(absf(world.vehicles["carro_jogador"].damage - snap.car_dmg) < 0.1, "conserto preservado")
	check(Game.world_state.vehicles.carro_usado.owner == snap.used, "segundo carro continua seu")
	check(Game.delivery.total == snap.delivery and Game.delivery.hired, "histórico de entregas preservado")
	check(p.is_equipped("casaco") == snap.wear and p.traits == snap.traits, "roupas vestidas e traços preservados")
	check(p.money == snap.money, "dinheiro preservado (R$ %d)" % p.money)
	var acts2: Array = world.doors["porta_casa_5"].get_actions(player())
	check(acts2.any(func(a): return a.id in ["unlock", "open", "close", "lock"] and a.enabled), "a casa 5 continua abrindo com sua chave")
