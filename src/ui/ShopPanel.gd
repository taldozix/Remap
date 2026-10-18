extends CenterContainer
## Loja: comprar do estoque (preço com desconto de freguês) e vender ao penhor.

var ui: Node
var world: Node
var shop_id := ""
var buy_list: VBoxContainer
var sell_list: VBoxContainer
var money_label: Label
var info_label: Label

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

func open() -> void:
	var s := Data.shop(shop_id)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(900, 520)
	add_child(panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 10)
	panel.add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	head.add_child(UITheme.label(tr(s.get("name", shop_id)), 24, UITheme.ACCENT))
	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(sp)
	money_label = UITheme.label("", 20, UITheme.GOOD)
	head.add_child(money_label)
	head.add_child(UITheme.button(tr("Fechar"), func(): ui.close_current()))
	info_label = UITheme.label("", 15, UITheme.MUTED)
	v.add_child(info_label)
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 16)
	v.add_child(h)
	buy_list = _column(h, tr("Comprar"))
	if not s.get("buys", []).is_empty():
		sell_list = _column(h, tr("Vender (paga %d%% do valor)") % int(float(s.get("buy_rate", 0.5)) * 100))
	refresh()

func _column(parent: Control, label: String) -> VBoxContainer:
	var col := VBoxContainer.new()
	col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(col)
	col.add_child(UITheme.label(label, 18))
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(420, 380)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	col.add_child(scroll)
	var list := VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	return list

func refresh() -> void:
	var p := Game.player
	money_label.text = "R$ %d" % p.money
	var disc := Game.shops.discount(shop_id, Game.relations)
	info_label.text = tr("Desconto de freguês: %d%%") % int(disc * 100) if disc > 0.0 else tr("Peso livre: %.1f kg") % (p.capacity() - p.inventory.weight())
	for c in buy_list.get_children():
		c.queue_free()
	var stock: Dictionary = Game.shops.stock.get(shop_id, {})
	for item_id in stock:
		var qty := int(stock[item_id])
		var price := Game.shops.price_buy(shop_id, item_id, Game.relations)
		var row := HBoxContainer.new()
		var l := UITheme.label("%s  (%d em estoque) — %.2f kg" % [Data.item_name(item_id), qty, Inventory.item_weight(item_id)], 15, UITheme.TEXT if qty > 0 else UITheme.MUTED)
		l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		l.clip_text = true
		row.add_child(l)
		var b := UITheme.button("R$ %d" % price, _buy.bind(item_id))
		b.disabled = qty <= 0 or p.money < price
		row.add_child(b)
		buy_list.add_child(row)
	if sell_list:
		for c in sell_list.get_children():
			c.queue_free()
		var any := false
		for i in p.inventory.items.size():
			var s: Dictionary = p.inventory.items[i]
			if not Game.shops.buys_item(shop_id, s.id):
				continue
			any = true
			var row2 := HBoxContainer.new()
			var l2 := UITheme.label("%s x%d" % [Data.item_name(s.id), int(s.qty)], 15)
			l2.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row2.add_child(l2)
			row2.add_child(UITheme.button("+R$ %d" % Game.shops.price_sell(shop_id, s.id), _sell.bind(i)))
			sell_list.add_child(row2)
		if not any:
			sell_list.add_child(UITheme.label(tr("Nada que interesse a esta loja."), 15, UITheme.MUTED))

func _buy(item_id: String) -> void:
	var r := Game.shops.buy(shop_id, item_id, 1, Game.player, Game.relations)
	if r.ok:
		Audio.play("cash", -6.0)
		Game.player.skills.add_xp("social", 1)
	else:
		Events.notify.emit(r.reason, "bad")
	refresh()

func _sell(index: int) -> void:
	var r := Game.shops.sell(shop_id, index, 1, Game.player)
	if r.ok:
		Audio.play("cash", -6.0)
	else:
		Events.notify.emit(r.reason, "bad")
	refresh()
