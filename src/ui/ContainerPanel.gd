extends CenterContainer
## Transferência de itens entre o inventário e um contêiner (geladeira, armário,
## porta-malas, lixeira, baú). Pegar de contêiner alheio conta como furto.

var ui: Node
var world: Node
var container_id := ""
var title := ""
var left: VBoxContainer
var right: VBoxContainer
var left_w: Label
var right_w: Label
var _theft_reported := false

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

func open() -> void:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(920, 500)
	add_child(panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 10)
	panel.add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	head.add_child(UITheme.label(title, 22, UITheme.ACCENT))
	var meta := Game.world_state.meta(container_id)
	var owner: String = meta.get("owner", "")
	if owner != "" and owner != "player" and meta.get("private", true):
		head.add_child(UITheme.label("  " + tr("(não é seu — pegar algo é furto)"), 15, UITheme.BAD))
	if Game.world_state.container(container_id) and Game.world_state.container(container_id).refrigerated:
		head.add_child(UITheme.label("  " + tr("(refrigerado: conserva alimentos)"), 15, UITheme.QUEST))
	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(sp)
	head.add_child(UITheme.button(tr("Fechar"), func(): ui.close_current()))
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 16)
	v.add_child(h)
	var lc := _column(h, tr("Você"))
	left = lc[0]
	left_w = lc[1]
	var rc := _column(h, title)
	right = rc[0]
	right_w = rc[1]
	Audio.play("door_open", -10.0, 1.5)
	refresh()

func _column(parent: Control, label: String) -> Array:
	var col := VBoxContainer.new()
	col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(col)
	var hh := HBoxContainer.new()
	col.add_child(hh)
	hh.add_child(UITheme.label(label, 18))
	var w := UITheme.label("", 15, UITheme.MUTED)
	hh.add_child(w)
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(430, 400)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	col.add_child(scroll)
	var list := VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	return [list, w]

func refresh() -> void:
	var p := Game.player
	var c: Inventory = Game.world_state.container(container_id)
	if c == null:
		return
	left_w.text = "  %.1f / %.0f kg" % [p.inventory.weight(), p.capacity()]
	right_w.text = "  %.1f / %.0f kg" % [c.weight(), c.capacity]
	_fill(left, p.inventory, true)
	_fill(right, c, false)

func _fill(list: VBoxContainer, inv: Inventory, from_player: bool) -> void:
	for ch in list.get_children():
		ch.queue_free()
	if inv.items.is_empty():
		list.add_child(UITheme.label(tr("Vazio."), 15, UITheme.MUTED))
	for i in inv.items.size():
		var s: Dictionary = inv.items[i]
		var row := HBoxContainer.new()
		var spoiled := tr(" (estragado)") if inv.is_spoiled(s) else ""
		var l := UITheme.label("%s x%d%s" % [Data.item_name(s.id), int(s.qty), spoiled], 15, UITheme.BAD if spoiled != "" else UITheme.TEXT)
		l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		l.clip_text = true
		row.add_child(l)
		var verb := tr("Guardar") if from_player else tr("Pegar")
		row.add_child(UITheme.button(verb, _move.bind(from_player, i, 1)))
		if int(s.qty) > 1:
			row.add_child(UITheme.button(tr("Tudo"), _move.bind(from_player, i, int(s.qty))))
		list.add_child(row)

func _move(from_player: bool, index: int, qty: int) -> void:
	var p := Game.player
	var c: Inventory = Game.world_state.container(container_id)
	var moved := 0
	if from_player:
		var s: Dictionary = p.inventory.items[index]
		if Data.item(s.id).get("job_item", false):
			Events.notify.emit(tr("Itens do pedido ficam com você."), "bad")
			return
		if p.is_equipped(s.id) and p.inventory.count(s.id) <= qty:
			p.unequip(Data.item(s.id).get("slot", ""))
		moved = p.inventory.transfer_to(c, index, qty, true)
		if moved == 0:
			Events.notify.emit(tr("Não cabe aqui."), "bad")
	else:
		moved = c.transfer_to(p.inventory, index, qty, false)
		if moved > 0 and not _theft_reported:
			var meta := Game.world_state.meta(container_id)
			var owner: String = meta.get("owner", "")
			if owner != "" and owner != "player" and meta.get("private", true) and not Game.has_flag("roubado_" + container_id.trim_prefix("trunk_")):
				_theft_reported = true
				world.on_container_take(container_id)
	if moved > 0:
		Audio.play("pickup", -6.0)
		Events.inventory_changed.emit("player")
	refresh()
