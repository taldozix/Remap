class_name GameWorld
extends Node3D
## Cena principal da partida. Constrói o bairro, cria jogador, NPCs, veículos e
## trânsito, e faz a ponte espacial entre os sistemas de simulação (testemunhas,
## invasão de domicílio, atropelamentos, cortes de parede, marcadores de missão).

var builder: WorldBuilder
var spots: Dictionary = {}
var buildings: Dictionary = {}
var doors: Dictionary = {}
var furniture: Dictionary = {}
var npcs: Dictionary = {}
var vehicles: Dictionary = {}
var player: Player
var camera_rig: CameraRig
var traffic: TrafficSystem
var daynight: DayNight
var ui: Node
var nav_region: NavigationRegion3D
var grass: GrassField
var postfx: PostFX
var _grass_level := -1
var ready_done := false
var player_building := ""
var _incidents: Array = []
var _trespass_t := 0.0
var _trespass_reported: Dictionary = {}
var _staff_bark_t := 0.0
var _cut_t := 0.0

const AMBIENT_NAMES := ["Carla", "Rogério", "Fernanda", "Seu Zé", "Bianca", "Diego"]

func _ready() -> void:
	Game.world = self
	Clock.running = false
	Clock.reset_holds()
	for n in ["Buildings", "Props", "Lights", "Actors", "Vehicles", "Items"]:
		var node := Node3D.new()
		node.name = n
		add_child(node)
	daynight = DayNight.new()
	add_child(daynight)
	daynight.setup(self)
	nav_region = NavigationRegion3D.new()
	nav_region.name = "Navigation"
	add_child(nav_region)
	builder = WorldBuilder.new()
	builder.build(self, Data.map(), nav_region)
	spots = builder.spots
	buildings = builder.buildings
	doors = builder.doors
	furniture = builder.furniture
	grass = GrassField.new()
	grass.name = "Grass"
	add_child(grass)
	var t0 := Time.get_ticks_msec()
	_grass_level = int(Settings.get_value("graphics", "grass_density"))
	grass.build(builder, _grass_level)
	print_verbose("Grama: %d tufos em %d ms" % [grass.instances, Time.get_ticks_msec() - t0])
	Settings.changed.connect(_on_settings_changed)
	postfx = PostFX.new()
	add_child(postfx)
	daynight.streetlights = builder.streetlights
	daynight.buildings = buildings.values()
	for g in Game.world_state.ground_items:
		_spawn_ground_item(g)
	_spawn_vehicles()
	_spawn_player()
	camera_rig = CameraRig.new()
	add_child(camera_rig)
	camera_rig.target = player if player.state != "driving" else player.vehicle
	camera_rig.snap()
	ui = load("res://src/ui/UIManager.gd").new()
	add_child(ui)
	ui.setup(self)
	# A malha de navegação só fica disponível após a sincronização do servidor.
	await get_tree().physics_frame
	await get_tree().physics_frame
	_spawn_npcs()
	traffic = TrafficSystem.new()
	traffic.name = "Traffic"
	add_child(traffic)
	traffic.setup(self, Data.map())
	refresh_delivery()
	Events.appearance_changed.connect(func(): if player: player.rebuild_visual())
	Clock.running = true
	ready_done = true
	Audio.stop_music()
	Audio.set_ambient("city", true, -16.0)
	Events.world_ready.emit()
	if Game.pending_load.has("new"):
		var intro := tr("Segunda-feira, 7h30. Primeira manhã na casa nova.\n\nTem um bilhete do senhorio na mesa da cozinha. A geladeira tem pouca coisa, a carteira tem R$ 60 e o aluguel vence no domingo.")
		if Settings.get_value("game", "show_hints"):
			intro += "\n\n" + tr("Dica: [%s] interage, [%s] entra/sai do carro, [%s] abre o inventário, [%s] o diário, [%s] o mapa, [%s]/[%s] giram a câmera e [%s] pausa (salvar/carregar).") % [Settings.primary_key_text("interact"), Settings.primary_key_text("vehicle"), Settings.primary_key_text("inventory"), Settings.primary_key_text("journal"), Settings.primary_key_text("map"), Settings.primary_key_text("cam_left"), Settings.primary_key_text("cam_right"), Settings.primary_key_text("pause")]
		Events.request_message.emit(tr("Vila Aurora, Porto Serra"), intro)
	elif Game.pending_load.has("loaded"):
		Events.notify.emit(tr("Jogo carregado."), "good")
		Events.game_loaded.emit()
	Game.pending_load = {}

func _on_settings_changed(section: String) -> void:
	if section != "graphics":
		return
	var lvl := int(Settings.get_value("graphics", "grass_density"))
	if lvl != _grass_level and grass:
		_grass_level = lvl
		grass.build(builder, lvl)

func _exit_tree() -> void:
	if Game.world == self:
		Game.world = null
	Clock.running = false
	Audio.stop_all_ambient()

# ------------------------------------------------------------------ criação
func _spawn_player() -> void:
	player = Player.new()
	player.name = "Player"
	player.world = self
	get_node("Actors").add_child(player)
	player.global_position = Game.player.position + Vector3(0, 0.05, 0)
	player.rotation.y = Game.player.rotation_y
	var vid := Game.player.vehicle_id
	if vid != "" and vehicles.has(vid):
		player.enter_vehicle(vehicles[vid])

func _spawn_vehicles() -> void:
	for id in Game.world_state.vehicles:
		var st: Dictionary = Game.world_state.vehicles[id]
		var v := Vehicle.new()
		v.name = id
		v.setup(id, st, self)
		get_node("Vehicles").add_child(v)
		v.global_position = Vector3(float(st.pos[0]), 0, float(st.pos[2]))
		v.rotation.y = deg_to_rad(float(st.rot))
		vehicles[id] = v

func _spawn_npcs() -> void:
	var density := float(Settings.get_value("graphics", "npc_density"))
	for id in Data.npcs():
		_spawn_npc(id, Data.npc(id))
	var ambient := int(round(6 * density))
	for i in ambient:
		var app := {
			"skin": ["#f0c8a8", "#c68e6a", "#8a5a3c", "#5e3b28", "#e0b494", "#a8765a"][i % 6],
			"hair": ["#1a1a1a", "#6b3d1e", "#d8c080", "#2b1d12", "#9a9a9a", "#a0522d"][(i * 5) % 6],
			"hair_style": ["short", "long", "curly", "bob", "bald", "ponytail"][i % 6],
			"shirt": ["#d35d5d", "#5d8fd3", "#e0c050", "#60a070", "#9a6ad0", "#e08a40"][(i * 7) % 6],
			"pants": ["#2f3a4a", "#3a3a3a", "#4a5a7a", "#5a4a3a"][i % 4],
			"height": 0.95 + (i % 3) * 0.04, "build": 0.9 + (i % 4) * 0.06,
		}
		var start := 6.5 + (i % 4) * 0.5
		var end := 19.5 + (i % 3) * 0.7
		var def := {
			"name": AMBIENT_NAMES[i % AMBIENT_NAMES.size()], "role": "Morador(a)", "dialogue": "pedestre",
			"appearance": app, "walk_speed": 1.25 + (i % 3) * 0.1,
			"schedule": [
				{"from": end, "to": start, "place": "predio_interior", "activity": "sleep"},
				{"from": start, "to": end, "place": "", "activity": "wander"},
			],
		}
		_spawn_npc("pedestre_%d" % i, def)

func _spawn_npc(id: String, def: Dictionary) -> void:
	var n := NPC.new()
	n.name = id
	n.setup(id, def, self)
	get_node("Actors").add_child(n)
	n.global_position = Vector3(0, 0, 0)
	npcs[id] = n
	n._decide()

func _spawn_ground_item(g: Dictionary) -> void:
	var gi := GroundItem.new()
	gi.setup(g, self)
	get_node("Items").add_child(gi)
	gi.global_position = Vector3(float(g.pos[0]), 0.0, float(g.pos[2]))

func drop_item(index: int, qty: int) -> void:
	var inv := Game.player.inventory
	if index < 0 or index >= inv.items.size():
		return
	var s: Dictionary = inv.items[index]
	if Game.player.is_equipped(s.id) and inv.count(s.id) <= qty:
		Game.player.unequip(Data.item(s.id).get("slot", ""))
	var moved := inv.take_at(index, qty)
	var pos := player.global_position + (-player.global_transform.basis.z) * 0.6
	var e := {"id": moved.id, "qty": int(moved.qty), "age": float(moved.age), "pos": [pos.x, 0.0, pos.z]}
	Game.world_state.ground_items.append(e)
	_spawn_ground_item(e)
	Audio.play("drop")

func pick_ground_item(gi: GroundItem) -> void:
	var e: Dictionary = gi.entry
	var inv := Game.player.inventory
	if not inv.can_fit(e.id, int(e.qty), Game.player.capacity() * 1.5):
		Events.notify.emit(tr("Pesado demais para carregar."), "bad")
		return
	inv.add(e.id, int(e.qty), float(e.get("age", 0.0)), false)
	Game.world_state.ground_items.erase(e)
	gi.queue_free()
	Audio.play("pickup")

# ------------------------------------------------------------------ consultas
func ui_blocking() -> bool:
	return ui != null and ui.is_modal_open()

func camera_yaw() -> float:
	return camera_rig.move_yaw() if camera_rig else 0.0

func ground_height(x: float, z: float) -> float:
	return builder.ground_height(x, z) if builder else 0.0

func is_inside_building(pos: Vector3, bid: String) -> bool:
	return buildings.has(bid) and buildings[bid].contains(pos)

func building_at(pos: Vector3) -> String:
	for id in buildings:
		if buildings[id].contains(pos):
			return id
	return ""

func npcs_near(pos: Vector3, r: float) -> Array:
	var out: Array = []
	for n in npcs.values():
		if n.visible and n.global_position.distance_to(pos) <= r:
			out.append(n)
	return out

func find_path(from: Vector3, to: Vector3) -> PackedVector3Array:
	var map_rid := nav_region.get_navigation_map()
	var p := NavigationServer3D.map_get_path(map_rid, from, to, true)
	if p.is_empty():
		p = PackedVector3Array([to])
	return p

func npc_lod_distance() -> float:
	return 36.0 + float(Settings.get_value("graphics", "view_distance")) * 14.0

func chat_partner(npc: NPC) -> NPC:
	for n in npcs.values():
		if n != npc and n.state == "at_spot" and n.activity == "chat" and n.global_position.distance_to(npc.global_position) < 3.5:
			return n
	return null

## Conversas espontâneas refletem o que o jogador fez (fofoca do bairro).
func conversation_lines() -> Array:
	var c: Dictionary = Data.chatter()
	var rep := Game.relations.reputation
	var r := randf()
	if r < 0.35:
		var hit := Game.law.record.any(func(e): return e.type == "atropelamento")
		if hit:
			return [tr(c.gossip_hit.pick_random()), tr("Que perigo. Tem que tomar cuidado.")]
		if rep >= 15:
			return [tr(c.gossip_good.pick_random()), tr("É, parece gente boa mesmo.")]
		if rep <= -10:
			return [tr(c.gossip_bad.pick_random()), tr("Vou trancar tudo hoje.")]
	if Game.world_state.weather.get("state", "") == "chuva":
		return [tr(c.rain.pick_random()), tr("Pois é...")]
	var pair: Array = c.chat_pairs.pick_random()
	return [tr(pair[0]), tr(pair[1])]

## Loja funciona se está no horário, se o atendente está no balcão e se aceita o jogador.
func shop_unavailable_reason(shop_id: String, _counter: Node) -> String:
	var s := Data.shop(shop_id)
	if s.is_empty():
		return tr("Loja inválida")
	if not Game.shops.is_open(shop_id, Clock.hour_f()):
		return tr("Fechado (abre às %dh)") % int(s.hours[0])
	var staff_present := false
	for b in buildings.values():
		for f in b.def.get("furniture", []):
			if f.get("shop", "") == shop_id:
				var staff_spot: String = f.get("staff_spot", "")
				for n in npcs.values():
					if n.spot_id == staff_spot and n.state == "at_spot":
						staff_present = true
	if not staff_present:
		return tr("Ninguém no balcão agora")
	return Game.shops.refuses_service(shop_id, Game.relations)

func counter_for_staff(spot_id: String) -> Furniture:
	if spot_id == "":
		return null
	for f in furniture.values():
		if f.kind == "counter" and f.def.get("staff_spot", "") == spot_id:
			return f
	return null

func staff_at(spot_id: String) -> NPC:
	if spot_id == "":
		return null
	for n in npcs.values():
		if n.spot_id == spot_id and n.state == "at_spot" and not n.is_sleeping():
			return n
	return null

func player_vehicle_near(pos: Vector3, r: float) -> Vehicle:
	for v in vehicles.values():
		if v.is_players() and v.global_position.distance_to(pos) <= r:
			return v
	return null

func nearest_vehicle(pos: Vector3, r: float) -> Vehicle:
	var best: Vehicle = null
	var bd := r
	for v in vehicles.values():
		var d: float = v.interact_point().distance_to(pos)
		if d <= bd:
			bd = d
			best = v
	return best

func is_vehicle_near_spot(vid: String, spot: String, radius: float) -> bool:
	if not vehicles.has(vid) or not spots.has(spot):
		return false
	return vehicles[vid].global_position.distance_to(spots[spot].pos) <= radius

func refresh_vehicle(vid: String) -> void:
	if vehicles.has(vid):
		vehicles[vid].refresh_from_state()

func player_outdoors() -> bool:
	return player != null and player_building == "" and player.state != "driving"

func is_player_in_job_area() -> bool:
	return player != null and buildings.has("brasa") and buildings.brasa.contains(player.global_position, 1.0)

## Testemunhas: NPCs acordados, visíveis, no raio, com linha de visão livre.
func find_witnesses(pos: Vector3, radius: float) -> Array:
	var out: Array = []
	var space := get_world_3d().direct_space_state
	for n in npcs.values():
		if not n.can_witness():
			continue
		var d: float = n.global_position.distance_to(pos)
		if d > radius:
			continue
		var q := PhysicsRayQueryParameters3D.create(n.global_position + Vector3(0, 1.6, 0), pos + Vector3(0, 1.2, 0), 1)
		if space.intersect_ray(q).is_empty():
			out.append(n.npc_id)
	return out

# ------------------------------------------------------------------ eventos do mundo
func start_dialogue(npc: NPC) -> void:
	if npc.is_sleeping():
		return
	ui.open_dialogue(npc)

func deliver_drive_order() -> void:
	var r := Game.job.deliver(Game.player.inventory, Game.player.skills.level("social"))
	match r.status:
		"complete":
			Game.player.stats["pedidos_entregues"] = int(Game.player.stats.get("pedidos_entregues", 0)) + 1
			Game.player.skills.add_xp("social", 6)
			Game.player.skills.add_xp("culinaria", 4)
			Audio.play("cash")
			Events.notify.emit(tr("Pedido #%d entregue! Gorjeta: R$ %d") % [r.order.id, r.tip], "good")
		"partial":
			Events.notify.emit(tr("Cliente: \"Tá faltando coisa: %s!\"") % DriveThruJob.order_text(r.order.remaining), "bad")
		"nothing":
			Events.notify.emit(tr("Você não tem nada deste pedido nas mãos."), "bad")
		"none":
			Events.notify.emit(tr("Não há cliente na janela."), "info")

func show_shift_summary(s: Dictionary) -> void:
	var body := tr("Horas trabalhadas: %.1f\nSalário: R$ %d\nBônus por pedidos: R$ %d\nGorjetas: R$ %d\nPedidos entregues: %d · Clientes perdidos: %d\n\nTotal recebido: R$ %d") % [s.hours, s.wage, s.bonus, s.tips, s.ok, s.failed, s.total]
	if s.get("promoted", false):
		body += "\n\n" + tr("Você foi promovido(a) a %s!") % Game.job.rank_title()
	Events.request_message.emit(tr("Fim do turno — Brasa Burger"), body)
	Audio.play("cash")

func on_shift_ended(summary: Dictionary) -> void:
	if summary.get("reason", "") != "normal":
		show_shift_summary(summary)

var _fainting := false

func on_player_fainted() -> void:
	if player.state == "sleeping" or _fainting:
		return
	_fainting = true
	print_verbose("Desmaio: ", Game.player.needs.to_dict())
	push_warning("Jogador desmaiou: %s" % str(Game.player.needs.to_dict()))
	if player.state == "driving":
		player.vehicle.speed = 0.0
		player.exit_vehicle()
	ui.close_all()
	var cause := tr("exaustão") if Game.player.needs.get_need("sono") >= 100.0 else tr("problemas de saúde")
	Clock.advance(8 * 60.0)
	var n := Game.player.needs
	n.set_need("sono", 30.0)
	n.saude = max(n.saude, 35.0)
	n.set_need("fome", min(n.get_need("fome"), 70.0))
	n.set_need("sede", min(n.get_need("sede"), 60.0))
	Game.player.add_money(-80, "Atendimento no posto de saúde")
	var bed: Furniture = null
	for home in Game.property.owned_homes():
		for f in furniture.values():
			if f.kind == "bed" and f.building_id == home:
				bed = f
	if bed:
		player.stand_up()
		player.global_position = bed.global_position + Vector3(1.2, 0.05, 0.6)
	Game.player.needs.fainted = false
	_fainting = false
	Events.request_message.emit(tr("Você desmaiou"), tr("Por %s, você apagou e foi levado(a) ao posto de saúde. Acordou horas depois em casa, com uma conta de R$ 80.\n\nCuide da alimentação, da hidratação e do sono.") % cause)

## Dormir: o tempo avança em fatias; acorda cedo se a fome ou a sede ficarem críticas.
func do_sleep(hours: int, bed_id: String) -> void:
	var bed: Furniture = furniture.get(bed_id)
	if bed == null:
		return
	if player.state != "lying" or player.seat != bed:
		player.rest_on_bed(bed)
	player.begin_sleep()
	await ui.fade(true)
	var slept := 0.0
	var woke_reason := ""
	Game.player.activity = "sleep"
	while slept < hours * 60.0:
		Clock.advance(30.0)
		Game.player.activity = "sleep"
		slept += 30.0
		var n := Game.player.needs
		if n.get_need("fome") >= 95.0 or n.get_need("sede") >= 95.0:
			woke_reason = tr("Você acordou com fome e sede.")
			break
		if n.get_need("sono") <= 0.0 and slept >= 60.0 and hours >= 12:
			break
	player.end_sleep()
	player.stand_up()
	Game.player.activity = "idle"
	await ui.fade(false)
	Events.notify.emit(tr("Você dormiu %.1f h.") % (slept / 60.0), "info")
	if woke_reason != "":
		Events.notify.emit(woke_reason, "bad")
	if Saves.save_game("auto"):
		Events.notify.emit(tr("Jogo salvo automaticamente."), "good")

func on_npc_hit(npc: NPC, severity: float) -> void:
	var witnesses := find_witnesses(npc.global_position, 22.0)
	if not witnesses.has(npc.npc_id):
		witnesses.append(npc.npc_id)
	for w in witnesses:
		if w != npc.npc_id and npcs.has(w):
			npcs[w].bark(Data.chatter().witness_hit.pick_random())
	_incidents.append({"npc": npc.npc_id, "t": 30.0, "witnesses": witnesses, "helped": false, "severity": severity})
	Events.notify.emit(tr("Você atropelou %s! Pare e preste socorro.") % npc.display_name, "bad")

func on_npc_helped(npc: NPC) -> void:
	for inc in _incidents:
		if inc.npc == npc.npc_id:
			inc.helped = true
			inc.t = 0.0
			return
	Game.relations.change_reputation(1)

func _resolve_incident(inc: Dictionary) -> void:
	var ws: Array = inc.witnesses
	Game.law.report("atropelamento", inc.npc, ws, Clock.day(), Game.relations)
	if inc.helped:
		Game.law.mitigate("atropelamento", 100)
		Game.relations.change(inc.npc, 15)
		Game.relations.add_memory(inc.npc, "prestou_socorro", Clock.day())
		Events.notify.emit(tr("Você prestou socorro. A multa será menor."), "info")
	else:
		Game.law.report("omissao_socorro", inc.npc, ws, Clock.day(), Game.relations)
		Events.notify.emit(tr("Fugir sem prestar socorro foi registrado pelas testemunhas."), "bad")

func on_horn(pos: Vector3) -> void:
	for n in npcs_near(pos, 7.0):
		if n.state != "knocked" and not n.is_sleeping():
			n.bark(tr("Que susto!"))

func on_knock(building_id: String) -> void:
	var owner: String = buildings[building_id].def.get("owner", "")
	if owner != "" and npcs.has(owner) and buildings[building_id].contains(npcs[owner].global_position) and not npcs[owner].is_sleeping():
		npcs[owner].bark(tr("Quem é? Agora não posso, volta mais tarde!"))
	else:
		Events.notify.emit(tr("Ninguém atende."), "info")

func on_player_used_key(building_id: String) -> void:
	var owner: String = buildings[building_id].def.get("owner", "")
	if owner != "" and owner != "player":
		Game.set_flag("entrou_" + building_id)
		var w := find_witnesses(player.global_position, 14.0)
		if not w.is_empty():
			Game.report_crime("invasao", owner, false, player.global_position)

## Pegar coisas de contêineres alheios é furto (testemunhado ou não).
func on_container_take(container_id: String) -> void:
	var meta := Game.world_state.meta(container_id)
	var owner: String = meta.get("owner", "")
	if owner in ["", "player", "owner"] or not meta.get("private", true):
		return
	if container_id.begins_with("trunk_") and Game.has_flag("roubado_" + container_id.substr(6)):
		pass
	var entry := Game.report_crime("furto", owner, false, player.global_position)
	if not entry.get("solved", true):
		Game.set_flag("furto_nao_resolvido_" + owner)

# ------------------------------------------------------------------ quadro a quadro
func _process(delta: float) -> void:
	if not ready_done:
		return
	_cut_t -= delta
	if _cut_t <= 0.0:
		_cut_t = 0.1
		_update_cutaway()
	for inc in _incidents.duplicate():
		inc.t = float(inc.t) - delta
		if inc.t <= 0.0:
			_incidents.erase(inc)
			_resolve_incident(inc)
	_check_trespass(delta)
	_process_events(delta)

func _update_cutaway() -> void:
	var pp: Vector3 = player.vehicle.global_position if player.state == "driving" and player.vehicle else player.global_position
	var vd := camera_rig.view_dir()
	var inside_id := ""
	for id in buildings:
		var b: Building = buildings[id]
		var inside := b.contains(pp)
		if inside:
			inside_id = id
		var behind := false
		if not inside:
			for k in [1.2, 2.6, 4.0]:
				if b.contains(pp - vd * k):
					behind = true
					break
		b.set_cut(inside or behind, vd)
	if inside_id != player_building:
		player_building = inside_id
		Events.player_building_changed.emit(inside_id)

## Invasão: dono presente vê o jogador dentro de casa sem permissão.
func _check_trespass(delta: float) -> void:
	_staff_bark_t -= delta
	var bid := player_building
	if bid == "":
		_trespass_t = 0.0
		return
	var b: Building = buildings[bid]
	# Cozinha do Brasa: só funcionários.
	if b.def.has("staff_zone") and not Game.job.employed:
		var z: Array = b.def.staff_zone
		var lp := player.global_position - b.position
		if lp.x >= float(z[0]) and lp.x <= float(z[2]) and lp.z >= float(z[1]) and lp.z <= float(z[3]) and _staff_bark_t <= 0.0:
			_staff_bark_t = 8.0
			for n in npcs_near(player.global_position, 9.0):
				if n.npc_id in ["kleber", "sandra"]:
					n.bark(Data.chatter().staff_only.pick_random())
					Game.relations.change(n.npc_id, -1)
					break
	var owner: String = b.def.get("owner", "")
	if not b.def.get("private", false) or owner in ["", "player", "owner"] or Game.property.owns_or_rents(bid):
		return
	if not npcs.has(owner):
		return
	var o: NPC = npcs[owner]
	if b.contains(o.global_position) and o.can_witness():
		_trespass_t += delta
		if _trespass_t > 0.5 and _trespass_t - delta <= 0.5:
			o.bark(Data.chatter().trespass.pick_random())
		if _trespass_t > 5.0 and not _trespass_reported.has(bid + str(Clock.day())):
			_trespass_reported[bid + str(Clock.day())] = true
			Game.report_crime("invasao", owner, false, player.global_position)
	else:
		_trespass_t = 0.0

## Posições dos marcadores de missão (para o HUD).
func marker_positions() -> Array:
	var out: Array = []
	if not Game.delivery.active.is_empty() and spots.has(Game.delivery.active.spot):
		out.append({"pos": spots[Game.delivery.active.spot].pos + Vector3(0, 1.5, 0), "label": tr("Entrega")})
	for m in Game.quests.active_markers():
		var parts: PackedStringArray = str(m).split(":")
		if parts.size() != 2:
			continue
		if parts[0] == "npc" and npcs.has(parts[1]):
			var n: NPC = npcs[parts[1]]
			var p: Vector3 = n.global_position if n.visible else n.spot.get("pos", n.global_position)
			out.append({"pos": p + Vector3(0, 2.4, 0), "label": n.display_name})
	return out

# ------------------------------------------------------------------ entregas
var _drop: DeliveryDrop = null

func refresh_delivery() -> void:
	if _drop and is_instance_valid(_drop):
		_drop.queue_free()
	_drop = null
	var a: Dictionary = Game.delivery.active
	if a.is_empty() or not spots.has(a.spot):
		return
	_drop = DeliveryDrop.new()
	_drop.setup(self, a.label)
	get_node("Props").add_child(_drop)
	_drop.global_position = spots[a.spot].pos

# ------------------------------------------------------------------ eventos dinâmicos
var _argue_t := 0.0
var _argue_i := 0

## Cria a situação no mundo. Retorna {} se as condições espaciais não existem agora.
func start_event(id: String, def: Dictionary) -> Dictionary:
	match id:
		"carteira_perdida":
			var owners: Array = npcs.values().filter(func(n): return n.visible and not n.is_sleeping() and n.state != "knocked" and n.activity != "leave")
			if owners.is_empty():
				return {}
			var owner: NPC = owners[Game.director.rng.randi() % owners.size()]
			var ws: Array = Data.map().get("wander_spots", [])
			var sp: String = ws[Game.director.rng.randi() % ws.size()]
			var pos: Vector3 = spots[sp].pos + Vector3(Game.director.rng.randf_range(-1.5, 1.5), 0, Game.director.rng.randf_range(-1.5, 1.5))
			var e := {"id": "carteira_perdida", "qty": 1, "age": 0.0, "pos": [pos.x, 0.0, pos.z]}
			Game.world_state.ground_items.append(e)
			_spawn_ground_item(e)
			Game.set_flag("carteira_dono_" + owner.npc_id)
			_notify_near(def, pos)
			return {"owner": owner.npc_id, "pos": [pos.x, pos.z]}
		"pedestre_ferido":
			var cands: Array = npcs.values().filter(func(n): return n.is_ambient and n.visible and n.state in ["moving", "at_spot"] and building_at(n.global_position) == "")
			if cands.is_empty():
				return {}
			var n2: NPC = cands[Game.director.rng.randi() % cands.size()]
			n2.knock_down(Vector3.ZERO, 0.5, false, 240.0)
			for w in npcs_near(n2.global_position, 15.0):
				if w != n2:
					w.bark(tr("Alguém ajuda aqui!"))
					break
			_notify_near(def, n2.global_position)
			return {"npc": n2.npc_id}
		"discussao_praca":
			var parts: Array = def.get("participants", [])
			for pid in parts:
				if not npcs.has(pid) or npcs[pid].state != "at_spot" or npcs[pid].building_at_spot() != "":
					return {}
			if npcs[parts[0]].global_position.distance_to(npcs[parts[1]].global_position) > 14.0:
				return {}
			_argue_t = 0.0
			_argue_i = 0
			_notify_near(def, npcs[parts[0]].global_position)
			return {"participants": parts}
	return {}

func end_event(id: String, _info: Dictionary) -> void:
	if id == "discussao_praca":
		for pid in Data.events().get(id, {}).get("participants", []):
			if npcs.has(pid):
				npcs[pid].bark(tr("Hunf."))

func _notify_near(def: Dictionary, pos: Vector3) -> void:
	if player and player.global_position.distance_to(pos) <= float(def.get("notify_radius", 40.0)):
		Events.notify.emit(tr(def.get("notify", "")), "info")

func _process_events(delta: float) -> void:
	if not Game.director.is_active("discussao_praca"):
		return
	_argue_t -= delta
	if _argue_t > 0.0:
		return
	_argue_t = 5.0
	var def: Dictionary = Data.events().discussao_praca
	var lines: Array = def.lines[_argue_i % def.lines.size()]
	var parts: Array = def.participants
	npcs[parts[_argue_i % 2]].bark(tr(lines[0]))
	npcs[parts[(_argue_i + 1) % 2]].call_deferred("bark_later", tr(lines[1]), 2.4)
	_argue_i += 1

## Ações extras que eventos e itens acrescentam a um NPC.
func extra_npc_actions(npc: NPC) -> Array:
	var out: Array = []
	if Game.player.inventory.has("carteira_perdida") and Game.has_flag("carteira_dono_" + npc.npc_id):
		out.append(Interactable.action("return_wallet", tr("Devolver a carteira")))
	if Game.director.is_active("discussao_praca") and Data.events().discussao_praca.participants.has(npc.npc_id):
		var ch := clampf(0.35 + Game.player.skills.level("social") * 0.1 + Game.relations.reputation * 0.003, 0.05, 0.95)
		out.append(Interactable.action("calm", tr("[Social %d%%] Apaziguar a discussão") % int(ch * 100)))
	return out

func do_extra_npc_action(npc: NPC, id: String) -> bool:
	match id:
		"return_wallet":
			Game.player.inventory.remove("carteira_perdida", 1)
			Game.clear_flag("carteira_dono_" + npc.npc_id)
			Game.relations.change(npc.npc_id, 15)
			Game.relations.change_reputation(3)
			Game.relations.add_memory(npc.npc_id, "devolveu_carteira", Clock.day())
			Game.player.add_money(10, "Recompensa")
			npc.bark(tr("Minha carteira! Muito obrigado(a), tem gente honesta no mundo!"))
			return true
		"calm":
			var ch := clampf(0.35 + Game.player.skills.level("social") * 0.1 + Game.relations.reputation * 0.003, 0.05, 0.95)
			var parts: Array = Data.events().discussao_praca.participants
			if Game.director.rng.randf() < ch:
				for pid in parts:
					Game.relations.change(pid, 5)
					npcs[pid].bark(tr("...Tá bom, tá bom. Desculpa."))
				Game.relations.change_reputation(3)
				Game.player.skills.add_xp("social", 25)
				Game.director.active.erase("discussao_praca")
				Events.notify.emit(tr("Você acalmou os ânimos na praça."), "good")
			else:
				Game.relations.change("marcos", -3)
				Game.player.skills.add_xp("social", 8)
				npcs["marcos"].bark(tr("Não se mete, não!"))
			return true
	return false

# ------------------------------------------------------------------ salvamento
func sync_to_state() -> void:
	if player:
		player.sync_to_state()
	for v in vehicles.values():
		v.save_state()

func save_block_reason() -> String:
	if player and player.state == "driving" and absf(player.vehicle.speed) > 1.0:
		return tr("Pare o veículo para salvar.")
	if not _incidents.is_empty():
		return tr("Não é possível salvar agora.")
	if ui and ui.is_dialogue_open():
		return tr("Termine a conversa antes de salvar.")
	return ""
