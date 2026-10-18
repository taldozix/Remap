extends CanvasLayer
## Interface da partida: HUD contextual e janelas (inventário, contêiner, loja,
## diálogo, diário, mapa, pausa, opções, sono, mensagens). Janelas modais bloqueiam
## o controle do personagem; a pausa congela o jogo.

var world: Node
var root: Control
var hud: Control
var modal_layer: Control
var current: Control = null           # janela modal aberta (contêiner)
var current_panel: Control = null
var current_kind := ""
var _theme: Theme
var _msg_queue: Array = []
var _pending_shop := ""

# HUD
var clock_label: Label
var money_label: Label
var weather_label: Label
var toast_box: VBoxContainer
var prompt_panel: PanelContainer
var prompt_title: Label
var prompt_list: VBoxContainer
var progress_panel: PanelContainer
var progress_bar: ProgressBar
var progress_label: Label
var moodle_box: VBoxContainer
var quest_panel: PanelContainer
var quest_label: Label
var job_panel: PanelContainer
var job_label: RichTextLabel
var vehicle_panel: PanelContainer
var vehicle_label: Label
var fuel_bar: ProgressBar
var markers: Control
var fade_rect: ColorRect
var debug_label: Label
var _hud_t := 0.0
var _last_prompt_key := ""

func setup(p_world: Node) -> void:
	world = p_world
	layer = 10
	process_mode = Node.PROCESS_MODE_ALWAYS
	root = Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	_apply_theme()
	_build_hud()
	modal_layer = Control.new()
	modal_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	modal_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(modal_layer)
	fade_rect = ColorRect.new()
	fade_rect.color = Color(0, 0, 0, 0)
	fade_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	fade_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(fade_rect)
	Events.notify.connect(_on_notify)
	Events.request_container.connect(open_container)
	Events.request_shop.connect(_on_request_shop)
	Events.request_message.connect(show_message)
	Events.request_sleep.connect(open_sleep)
	Events.skill_level_up.connect(func(_s, _l): Audio.play("notify"))
	Settings.changed.connect(func(_s): _apply_theme())

func _apply_theme() -> void:
	_theme = UITheme.make()
	root.theme = _theme

func is_modal_open() -> bool:
	return current != null

func is_dialogue_open() -> bool:
	return current_kind == "dialogue"

# ------------------------------------------------------------------ HUD
func _build_hud() -> void:
	hud = Control.new()
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	hud.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(hud)
	markers = load("res://src/ui/MarkerLayer.gd").new()
	markers.world = world
	markers.set_anchors_preset(Control.PRESET_FULL_RECT)
	markers.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(markers)
	# Relógio e dinheiro (canto superior direito)
	var tr_panel := PanelContainer.new()
	tr_panel.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	tr_panel.position = Vector2(-300, 14)
	tr_panel.custom_minimum_size = Vector2(286, 0)
	tr_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(tr_panel)
	var v := VBoxContainer.new()
	tr_panel.add_child(v)
	clock_label = UITheme.label("", 20)
	money_label = UITheme.label("", 20, UITheme.GOOD)
	weather_label = UITheme.label("", 14, UITheme.MUTED)
	v.add_child(clock_label)
	v.add_child(money_label)
	v.add_child(weather_label)
	# Notificações (topo central)
	toast_box = VBoxContainer.new()
	toast_box.set_anchors_preset(Control.PRESET_CENTER_TOP)
	toast_box.position = Vector2(-260, 14)
	toast_box.custom_minimum_size = Vector2(520, 0)
	toast_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(toast_box)
	# Missão ativa (canto superior esquerdo)
	quest_panel = PanelContainer.new()
	quest_panel.position = Vector2(14, 14)
	quest_panel.custom_minimum_size = Vector2(330, 0)
	quest_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(quest_panel)
	quest_label = UITheme.label("", 15)
	quest_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	quest_label.custom_minimum_size = Vector2(300, 0)
	quest_panel.add_child(quest_label)
	# Turno no drive-thru
	job_panel = PanelContainer.new()
	job_panel.position = Vector2(14, 130)
	job_panel.custom_minimum_size = Vector2(330, 0)
	job_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(job_panel)
	job_label = RichTextLabel.new()
	job_label.bbcode_enabled = true
	job_label.fit_content = true
	job_label.custom_minimum_size = Vector2(320, 0)
	job_label.scroll_active = false
	job_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	job_panel.add_child(job_label)
	# Moodles (canto inferior esquerdo)
	moodle_box = VBoxContainer.new()
	moodle_box.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	moodle_box.position = Vector2(14, -20)
	moodle_box.grow_vertical = Control.GROW_DIRECTION_BEGIN
	moodle_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(moodle_box)
	# Prompt de interação (inferior central)
	prompt_panel = PanelContainer.new()
	prompt_panel.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	prompt_panel.grow_horizontal = Control.GROW_DIRECTION_BOTH
	prompt_panel.grow_vertical = Control.GROW_DIRECTION_BEGIN
	prompt_panel.position = Vector2(-220, -30)
	prompt_panel.custom_minimum_size = Vector2(440, 0)
	prompt_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(prompt_panel)
	var pv := VBoxContainer.new()
	prompt_panel.add_child(pv)
	prompt_title = UITheme.label("", 16, UITheme.ACCENT)
	pv.add_child(prompt_title)
	prompt_list = VBoxContainer.new()
	pv.add_child(prompt_list)
	progress_panel = PanelContainer.new()
	progress_panel.set_anchors_preset(Control.PRESET_CENTER)
	progress_panel.position = Vector2(-160, 80)
	progress_panel.custom_minimum_size = Vector2(320, 0)
	progress_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(progress_panel)
	var prv := VBoxContainer.new()
	progress_panel.add_child(prv)
	progress_label = UITheme.label("", 15)
	progress_bar = UITheme.bar(0, UITheme.ACCENT, 290)
	prv.add_child(progress_label)
	prv.add_child(progress_bar)
	# Veículo (inferior direito)
	vehicle_panel = PanelContainer.new()
	vehicle_panel.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	vehicle_panel.position = Vector2(-270, -110)
	vehicle_panel.custom_minimum_size = Vector2(250, 0)
	vehicle_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud.add_child(vehicle_panel)
	var vv := VBoxContainer.new()
	vehicle_panel.add_child(vv)
	vehicle_label = UITheme.label("", 17)
	fuel_bar = UITheme.bar(50, Color("e0b050"), 220)
	vv.add_child(vehicle_label)
	vv.add_child(fuel_bar)
	debug_label = UITheme.label("", 13, Color(0.7, 1, 0.7))
	debug_label.position = Vector2(14, 420)
	debug_label.visible = false
	hud.add_child(debug_label)

func _process(delta: float) -> void:
	if world == null or world.player == null:
		return
	_hud_t -= delta
	if _hud_t <= 0.0:
		_hud_t = 0.15
		_refresh_hud()
	_refresh_prompt()

func _refresh_hud() -> void:
	clock_label.text = "%s, %s · %s %d" % [Clock.weekday_name(), Clock.time_string(), tr("Dia"), Clock.day()]
	money_label.text = "R$ %d" % Game.player.money
	money_label.add_theme_color_override("font_color", UITheme.GOOD if Game.player.money >= 0 else UITheme.BAD)
	var w: String = Game.world_state.weather.get("state", "limpo")
	var wtxt: String = {"limpo": tr("Céu limpo"), "nublado": tr("Nublado"), "chuva": tr("Chovendo")}.get(w, w)
	var bname: String = world.buildings[world.player_building].def.name if world.player_building != "" else tr("Vila Aurora")
	weather_label.text = "%s · %s" % [wtxt, tr(bname)]
	# Moodles
	for c in moodle_box.get_children():
		c.queue_free()
	var p := Game.player
	for m in p.needs.moodles():
		var col: Color = [UITheme.WARN, Color("f08a3a"), UITheme.BAD][clampi(int(m.level) - 1, 0, 2)]
		moodle_box.add_child(_chip(tr(m.label), col))
	if p.is_overloaded():
		moodle_box.add_child(_chip(tr("Sobrecarregado"), Color("f08a3a")))
	if Game.property.total_debt() > 0:
		moodle_box.add_child(_chip(tr("Aluguel atrasado: R$ %d") % Game.property.total_debt(), UITheme.BAD))
	# Missão
	var qtxt := ""
	for qid in Game.quests.states:
		if Game.quests.state_of(qid) == "active":
			qtxt = "%s\n%s" % [tr(Data.quest(qid).title).to_upper(), Game.quests.objective(qid)]
			break
	quest_panel.visible = qtxt != ""
	quest_label.text = qtxt
	quest_label.add_theme_color_override("font_color", UITheme.QUEST)
	# Turno
	job_panel.visible = Game.job.on_shift
	if Game.job.on_shift:
		job_panel.position.y = 14 + (quest_panel.size.y + 10 if quest_panel.visible else 0)
		var t := "[color=#e8913a][b]%s[/b][/color] — %s\n" % [tr("TURNO NO BRASA"), Game.job.rank_title()]
		t += tr("Trabalhado: %d min · Pedidos: %d · Gorjetas: R$ %d") % [int(Game.job.shift.minutes), int(Game.job.shift.ok), int(Game.job.shift.tips)] + "\n"
		if Game.job.orders.is_empty():
			t += "[color=#9a9a9a]%s[/color]\n" % tr("Aguardando clientes...")
		for o in Game.job.orders:
			var frac := float(o.patience) / float(o.max_patience)
			var col := "#7fd17f" if frac > 0.5 else ("#f2c14e" if frac > 0.25 else "#ef6b5b")
			t += "[color=%s]#%d[/color] %s  [color=%s](%d%%)[/color]\n" % [col, o.id, DriveThruJob.order_text(o.remaining), col, int(frac * 100)]
		for s in Game.job.stations:
			var st: Dictionary = Game.job.stations[s]
			var lbl: String = tr(Data.job("brasa").stations[s].label)
			match st.state:
				"cooking": t += "%s: %d%%  " % [lbl, int(Game.job.station_progress(s) * 100)]
				"ready": t += "[color=#7fd17f]%s: %s[/color]  " % [lbl, tr("PRONTO")]
				"burnt": t += "[color=#ef6b5b]%s: %s[/color]  " % [lbl, tr("QUEIMOU")]
		var hands: Array[String] = []
		for s2 in p.inventory.items:
			if Data.item(s2.id).get("job_item", false):
				hands.append("%dx %s" % [int(s2.qty), Data.item_name(s2.id)])
		t += "\n" + tr("Nas mãos: %s") % (", ".join(hands) if not hands.is_empty() else "—")
		job_label.text = t
	# Veículo
	var pl: Player = world.player
	vehicle_panel.visible = pl.state == "driving" and pl.vehicle != null
	if vehicle_panel.visible:
		var veh: Vehicle = pl.vehicle
		vehicle_label.text = "%s\n%d km/h · %s %d%%" % [veh.display_name(), int(absf(veh.speed) * 3.6), tr("Dano"), int(veh.damage)]
		fuel_bar.value = veh.fuel / veh.fuel_capacity() * 100.0
	if debug_label.visible:
		var far := 0
		var hidden := 0
		for n in world.npcs.values():
			if n.lod_far: far += 1
			if n.state == "hidden": hidden += 1
		debug_label.text = "FPS %d | NPCs %d (longe %d, ocultos %d) | carros %d | pos %s | atividade %s" % [Engine.get_frames_per_second(), world.npcs.size(), far, hidden, world.traffic.cars.size() if world.traffic else 0, str(pl.global_position.snapped(Vector3.ONE * 0.1)), Game.player.activity]

func _chip(text: String, col: Color) -> PanelContainer:
	var pc := PanelContainer.new()
	var sb := UITheme.panel_style(Color(col.r * 0.25, col.g * 0.25, col.b * 0.25, 0.9), 8, 8)
	sb.border_color = col
	sb.set_border_width_all(2)
	pc.add_theme_stylebox_override("panel", sb)
	pc.add_child(UITheme.label(text, 15, col.lightened(0.2)))
	pc.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return pc

func _refresh_prompt() -> void:
	var pl: Player = world.player
	progress_panel.visible = not pl.timed.is_empty()
	if progress_panel.visible:
		progress_label.text = pl.timed.label
		progress_bar.value = pl.timed_progress() * 100.0
	var show := pl.focus != null and not pl.focus_actions.is_empty() and not is_modal_open() and pl.timed.is_empty()
	prompt_panel.visible = show
	if not show:
		_last_prompt_key = ""
		return
	var title: String = pl.focus.interact_title() if pl.focus.has_method("interact_title") else ""
	var key := title + str(pl.focus_actions)
	if key == _last_prompt_key:
		return
	_last_prompt_key = key
	prompt_title.text = title
	for c in prompt_list.get_children():
		c.queue_free()
	var first_done := false
	for i in pl.focus_actions.size():
		var a: Dictionary = pl.focus_actions[i]
		var keytxt := ""
		if a.enabled and not first_done:
			keytxt = "[%s] " % Settings.primary_key_text("interact")
			first_done = true
		elif i < 4:
			keytxt = "[%s] " % Settings.primary_key_text("option_%d" % (i + 1))
		var txt: String = keytxt + str(a.label)
		if not a.enabled and a.get("hint", "") != "":
			txt += " — " + str(a.hint)
		prompt_list.add_child(UITheme.label(txt, 16, UITheme.TEXT if a.enabled else UITheme.MUTED))

func _on_notify(text: String, kind: String) -> void:
	var pc := PanelContainer.new()
	pc.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var l := UITheme.label(text, 16, UITheme.kind_color(kind))
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.custom_minimum_size = Vector2(480, 0)
	pc.add_child(l)
	toast_box.add_child(pc)
	while toast_box.get_child_count() > 5:
		toast_box.get_child(0).free()
	if kind in ["quest", "skill"]:
		Audio.play("notify")
	elif kind == "bad":
		Audio.play("bad", -8.0)
	var tw := pc.create_tween()
	tw.tween_interval(4.5)
	tw.tween_property(pc, "modulate:a", 0.0, 0.6)
	tw.tween_callback(pc.queue_free)

# ------------------------------------------------------------------ entrada
func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("pause"):
		get_viewport().set_input_as_handled()
		if current:
			if current_kind == "dialogue":
				current_panel.try_close()
			else:
				close_current()
		else:
			open_pause()
		return
	if event.is_action_pressed("debug"):
		debug_label.visible = not debug_label.visible
		return
	if current and current_kind in ["inventory", "journal", "map"]:
		if (event.is_action_pressed("inventory") and current_kind == "inventory") or (event.is_action_pressed("journal") and current_kind == "journal") or (event.is_action_pressed("map") and current_kind == "map"):
			close_current()
			get_viewport().set_input_as_handled()
		return
	if current:
		return
	if event.is_action_pressed("inventory"):
		open_panel("inventory", load("res://src/ui/InventoryPanel.gd").new())
	elif event.is_action_pressed("journal"):
		open_panel("journal", load("res://src/ui/JournalPanel.gd").new())
	elif event.is_action_pressed("map"):
		open_panel("map", load("res://src/ui/MapPanel.gd").new())
	elif event.is_action_pressed("quick_save"):
		var why := Saves.block_reason()
		if why != "":
			Events.notify.emit(why, "bad")
		elif Saves.save_game("rapido"):
			Events.notify.emit(tr("Jogo salvo (rápido)."), "good")

# ------------------------------------------------------------------ janelas
## Abre uma janela modal. O painel recebe `ui` e `world` e chama ui.close_current().
func open_panel(kind: String, panel: Control) -> void:
	close_current()
	var holder := Control.new()
	holder.set_anchors_preset(Control.PRESET_FULL_RECT)
	holder.mouse_filter = Control.MOUSE_FILTER_IGNORE if kind == "dialogue" else Control.MOUSE_FILTER_STOP
	var dim := ColorRect.new()
	dim.color = Color(0, 0, 0, 0.35 if kind != "dialogue" else 0.0)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	dim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	holder.add_child(dim)
	modal_layer.add_child(holder)
	panel.set("ui", self)
	panel.set("world", world)
	holder.add_child(panel)
	current = holder
	current_panel = panel
	current_kind = kind
	if panel.has_method("open"):
		panel.open()

func close_current() -> void:
	if current == null:
		return
	var kind := current_kind
	var panel: Control = current_panel
	if panel and panel.has_method("on_close"):
		panel.on_close()
	current.queue_free()
	current = null
	current_panel = null
	current_kind = ""
	if kind == "pause":
		get_tree().paused = false
	if kind == "dialogue" and _pending_shop != "":
		var s := _pending_shop
		_pending_shop = ""
		call_deferred("open_shop", s)
		return
	if not _msg_queue.is_empty():
		var m: Array = _msg_queue.pop_front()
		call_deferred("show_message", m[0], m[1])

func close_all() -> void:
	_msg_queue.clear()
	_pending_shop = ""
	close_current()

func open_container(container_id: String, title: String) -> void:
	var p: Control = load("res://src/ui/ContainerPanel.gd").new()
	p.container_id = container_id
	p.title = title
	open_panel("container", p)

func _on_request_shop(shop_id: String) -> void:
	if current_kind == "dialogue":
		_pending_shop = shop_id
	else:
		open_shop(shop_id)

func open_shop(shop_id: String) -> void:
	var p: Control = load("res://src/ui/ShopPanel.gd").new()
	p.shop_id = shop_id
	open_panel("shop", p)

func open_dialogue(npc: Node) -> void:
	var p: Control = load("res://src/ui/DialoguePanel.gd").new()
	p.npc = npc
	open_panel("dialogue", p)

func open_pause() -> void:
	open_panel("pause", load("res://src/ui/PauseMenu.gd").new())
	get_tree().paused = true

func open_sleep(bed_id: String, _quality: float) -> void:
	var box := PanelContainer.new()
	box.custom_minimum_size = Vector2(420, 0)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 10)
	box.add_child(v)
	v.add_child(UITheme.label(tr("Dormir"), 22, UITheme.ACCENT))
	var info := UITheme.label("", 16)
	v.add_child(info)
	var slider := HSlider.new()
	slider.min_value = 1
	slider.max_value = 12
	slider.value = 8
	slider.custom_minimum_size = Vector2(360, 24)
	v.add_child(slider)
	var upd := func(val: float) -> void:
		var wake := fmod(Clock.hour_f() + val, 24.0)
		info.text = tr("Dormir %d h — acordar por volta das %02d:%02d.\nSono atual: %d%%") % [int(val), int(wake), int(fmod(wake, 1.0) * 60), int(Game.player.needs.get_need("sono"))]
	slider.value_changed.connect(upd)
	upd.call(8.0)
	var h := HBoxContainer.new()
	v.add_child(h)
	h.add_child(UITheme.button(tr("Dormir"), func():
		var hours := int(slider.value)
		close_current()
		world.do_sleep(hours, bed_id)))
	h.add_child(UITheme.button(tr("Cancelar"), close_current))
	_open_simple("sleep", box)

func show_message(title: String, body: String) -> void:
	if current != null:
		_msg_queue.append([title, body])
		return
	var box := PanelContainer.new()
	box.custom_minimum_size = Vector2(560, 0)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 12)
	box.add_child(v)
	v.add_child(UITheme.label(title, 22, UITheme.ACCENT))
	var l := UITheme.label(body, 17)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(520, 0)
	v.add_child(l)
	var ok := UITheme.button(tr("OK"), close_current)
	v.add_child(ok)
	_open_simple("message", box)
	ok.grab_focus()

func _open_simple(kind: String, box: Control) -> void:
	var c := CenterContainer.new()
	c.set_anchors_preset(Control.PRESET_FULL_RECT)
	c.add_child(box)
	open_panel(kind, c)

func fade(to_black: bool) -> void:
	var tw := create_tween()
	tw.tween_property(fade_rect, "color:a", 1.0 if to_black else 0.0, 0.6)
	await tw.finished
