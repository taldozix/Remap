extends CenterContainer
## Inventário: lista com peso, frescor e ações (comer/beber/usar, vestir, largar).

var ui: Node
var world: Node
var list: VBoxContainer
var details: VBoxContainer
var weight_bar: ProgressBar
var weight_label: Label
var selected := -1

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

func open() -> void:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(860, 520)
	add_child(panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 10)
	panel.add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	head.add_child(UITheme.label(tr("Inventário"), 24, UITheme.ACCENT))
	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(sp)
	weight_label = UITheme.label("", 16)
	head.add_child(weight_label)
	weight_bar = UITheme.bar(0, UITheme.ACCENT, 180)
	head.add_child(weight_bar)
	head.add_child(UITheme.button(tr("Fechar"), func(): ui.close_current()))
	var h := HBoxContainer.new()
	h.size_flags_vertical = Control.SIZE_EXPAND_FILL
	h.add_theme_constant_override("separation", 14)
	v.add_child(h)
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(470, 420)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	h.add_child(scroll)
	list = VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	var dp := PanelContainer.new()
	dp.add_theme_stylebox_override("panel", UITheme.panel_style(UITheme.BG_LIGHT, 8, 12))
	dp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(dp)
	details = VBoxContainer.new()
	details.add_theme_constant_override("separation", 8)
	dp.add_child(details)
	Events.inventory_changed.connect(_on_changed)
	refresh()

func on_close() -> void:
	if Events.inventory_changed.is_connected(_on_changed):
		Events.inventory_changed.disconnect(_on_changed)

func _on_changed(owner_id: String) -> void:
	if owner_id == "player":
		refresh()

func refresh() -> void:
	var p := Game.player
	weight_label.text = tr("Peso: %.1f / %.0f kg") % [p.inventory.weight(), p.capacity()]
	weight_bar.value = min(100.0, p.inventory.weight() / p.capacity() * 100.0)
	for c in list.get_children():
		c.queue_free()
	if p.inventory.items.is_empty():
		list.add_child(UITheme.label(tr("Você não está carregando nada."), 16, UITheme.MUTED))
	for i in p.inventory.items.size():
		var s: Dictionary = p.inventory.items[i]
		var b := Button.new()
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		var status := ""
		if Inventory.is_perishable(s.id):
			status = tr(" · estragado") if p.inventory.is_spoiled(s) else (tr(" · fresco") if p.inventory.freshness(s) > 0.5 else tr(" · passando"))
		var eq := tr(" (vestido)") if p.is_equipped(s.id) else ""
		b.text = "%s x%d%s%s   —   %.2f kg" % [Data.item_name(s.id), int(s.qty), eq, status, Inventory.item_weight(s.id) * int(s.qty)]
		if i == selected:
			b.add_theme_color_override("font_color", UITheme.ACCENT)
		b.pressed.connect(_select.bind(i))
		list.add_child(b)
	_show_details()

func _select(i: int) -> void:
	selected = i
	refresh()

func _show_details() -> void:
	for c in details.get_children():
		c.queue_free()
	var p := Game.player
	if selected < 0 or selected >= p.inventory.items.size():
		details.add_child(UITheme.label(tr("Selecione um item."), 16, UITheme.MUTED))
		_needs_summary()
		return
	var s: Dictionary = p.inventory.items[selected]
	var it := Data.item(s.id)
	details.add_child(UITheme.label(Data.item_name(s.id), 20, UITheme.ACCENT))
	var d := UITheme.label(tr(it.get("desc", "")), 15)
	d.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	d.custom_minimum_size = Vector2(300, 0)
	details.add_child(d)
	details.add_child(UITheme.label(tr("Valor de referência: R$ %d") % int(it.get("value", 0)), 14, UITheme.MUTED))
	if it.has("use"):
		var parts: Array[String] = []
		for k in it.use:
			var val := float(it.use[k])
			var name: String = tr(Needs.LABELS.get(k, k.capitalize()))
			if k == "saude":
				name = tr("Saúde")
			parts.append("%s %s%d" % [name, "+" if val > 0 else "", int(val)])
		details.add_child(UITheme.label(", ".join(parts), 14, UITheme.MUTED))
	if Inventory.is_perishable(s.id):
		details.add_child(UITheme.label(tr("Frescor: %d%%") % int(p.inventory.freshness(s) * 100), 14, UITheme.BAD if p.inventory.is_spoiled(s) else UITheme.GOOD))
	var actions := HFlowContainer.new()
	details.add_child(actions)
	if it.has("use"):
		actions.add_child(UITheme.button(tr(it.get("use_label", "Usar")), _use))
	if it.has("slot"):
		if p.is_equipped(s.id):
			actions.add_child(UITheme.button(tr("Tirar"), func():
				p.unequip(it.slot)
				refresh()))
		else:
			actions.add_child(UITheme.button(tr("Vestir"), func():
				p.equip(s.id)
				Events.notify.emit(tr("Capacidade de carga: %.0f kg") % p.capacity(), "info")
				refresh()))
	if not it.get("job_item", false):
		actions.add_child(UITheme.button(tr("Largar 1"), func(): _drop(1)))
		if int(s.qty) > 1:
			actions.add_child(UITheme.button(tr("Largar tudo"), func(): _drop(int(s.qty))))

func _needs_summary() -> void:
	var n := Game.player.needs
	details.add_child(UITheme.label(tr("Condição física"), 18, UITheme.ACCENT))
	for id in Needs.IDS:
		var row := HBoxContainer.new()
		var l := UITheme.label(tr(Needs.LABELS[id]), 15)
		l.custom_minimum_size = Vector2(90, 0)
		row.add_child(l)
		var val := n.get_need(id)
		row.add_child(UITheme.bar(val, UITheme.GOOD.lerp(UITheme.BAD, val / 100.0), 200))
		details.add_child(row)
	var row2 := HBoxContainer.new()
	var l2 := UITheme.label(tr("Saúde"), 15)
	l2.custom_minimum_size = Vector2(90, 0)
	row2.add_child(l2)
	row2.add_child(UITheme.bar(n.saude, UITheme.BAD.lerp(UITheme.GOOD, n.saude / 100.0), 200))
	details.add_child(row2)
	details.add_child(UITheme.label(tr("(0 = ótimo; barras cheias indicam necessidade urgente)"), 13, UITheme.MUTED))

func _use() -> void:
	var p := Game.player
	if selected < 0 or selected >= p.inventory.items.size():
		return
	var s: Dictionary = p.inventory.items[selected]
	var it := Data.item(s.id)
	if it.get("category", "") == "trabalho":
		return
	var spoiled := p.inventory.is_spoiled(s)
	var item_id: String = s.id
	p.inventory.take_at(selected, 1)
	p.needs.consume(it.use)
	if it.has("use_money"):
		p.add_money(int(it.use_money), "Dinheiro da carteira")
		for npc_id in Data.npcs():
			Game.clear_flag("carteira_dono_" + npc_id)
		Game.set_flag("ficou_com_carteira")
	if spoiled:
		p.needs.consume({"nausea": 4.0, "saude": -8})
		Events.notify.emit(tr("Estava estragado... Você está enjoado(a)."), "bad")
	Audio.play(it.get("sound", "pickup"))
	Events.needs_changed.emit()
	if selected >= p.inventory.items.size():
		selected = -1
	if p.is_equipped(item_id) and not p.inventory.has(item_id):
		p.unequip(it.get("slot", ""))
	refresh()

func _drop(qty: int) -> void:
	world.drop_item(selected, qty)
	selected = -1
	refresh()
