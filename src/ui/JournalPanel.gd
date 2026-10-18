extends CenterContainer
## Diário: missões, habilidades, relações, personagem, trabalho e registro de ocorrências.

var ui: Node
var world: Node

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

func open() -> void:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(920, 560)
	add_child(panel)
	var v := VBoxContainer.new()
	panel.add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	head.add_child(UITheme.label(tr("Diário de %s") % Game.player.name, 24, UITheme.ACCENT))
	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(sp)
	head.add_child(UITheme.button(tr("Fechar"), func(): ui.close_current()))
	var tabs := TabContainer.new()
	tabs.custom_minimum_size = Vector2(880, 480)
	v.add_child(tabs)
	_tab(tabs, tr("Missões"), _quests())
	_tab(tabs, tr("Personagem"), _character())
	_tab(tabs, tr("Habilidades"), _skills())
	_tab(tabs, tr("Relações"), _relations())
	_tab(tabs, tr("Trabalho e casa"), _work())
	_tab(tabs, tr("Ocorrências"), _law())

func _tab(tabs: TabContainer, title: String, content: Control) -> void:
	var scroll := ScrollContainer.new()
	scroll.name = title
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(content)
	tabs.add_child(scroll)

func _wrap(text: String, size: int = 15, color: Color = UITheme.TEXT) -> Label:
	var l := UITheme.label(text, size, color)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(820, 0)
	return l

func _quests() -> Control:
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 8)
	var any := false
	for qid in Data.quests():
		var st := Game.quests.state_of(qid)
		if st == "not_started":
			continue
		any = true
		var q := Data.quest(qid)
		var status: String = {"active": tr("Em andamento"), "completed": tr("Concluída"), "failed": tr("Falhou")}.get(st, st)
		v.add_child(UITheme.label("%s — %s" % [tr(q.title), status], 20, UITheme.QUEST if st == "active" else UITheme.MUTED))
		v.add_child(_wrap(tr(q.get("summary", ""))))
		if st == "active":
			v.add_child(_wrap("▶ " + Game.quests.objective(qid), 16, UITheme.ACCENT))
			for hnt in q.get("stages", {}).get(Game.quests.stage_of(qid), {}).get("hints", []):
				v.add_child(_wrap("   • " + tr(hnt), 14, UITheme.MUTED))
		for e in Game.quests.states[qid].get("log", []):
			v.add_child(_wrap("   %s %d, %s — %s" % [tr("Dia"), int(e.day), e.time, e.text], 14, UITheme.MUTED))
	if not any:
		v.add_child(_wrap(tr("Nenhuma missão ainda. Converse com as pessoas do bairro — algumas precisam de ajuda."), 16, UITheme.MUTED))
	v.add_child(HSeparator.new())
	v.add_child(_wrap(tr("A história principal (\"Os Livros da Aurora\") não faz parte deste protótipo. Você é livre para trabalhar, explorar e viver como quiser."), 14, UITheme.MUTED))
	return v

func _row(parent: Control, label: String, value: float, color: Color, text: String = "") -> void:
	var h := HBoxContainer.new()
	var l := UITheme.label(label, 15)
	l.custom_minimum_size = Vector2(170, 0)
	h.add_child(l)
	h.add_child(UITheme.bar(value, color, 300))
	if text != "":
		h.add_child(UITheme.label("  " + text, 14, UITheme.MUTED))
	parent.add_child(h)

func _character() -> Control:
	var v := VBoxContainer.new()
	var p := Game.player
	var n := p.needs
	v.add_child(UITheme.label(tr("Condição física"), 18, UITheme.ACCENT))
	for id in Needs.IDS:
		var val := n.get_need(id)
		_row(v, tr(Needs.LABELS[id]), val, UITheme.GOOD.lerp(UITheme.BAD, val / 100.0), "%d/100" % int(val))
	_row(v, tr("Saúde"), n.saude, UITheme.BAD.lerp(UITheme.GOOD, n.saude / 100.0), "%d/100" % int(n.saude))
	_row(v, tr("Dor"), n.dor, UITheme.WARN.lerp(UITheme.BAD, n.dor / 100.0), "%d/100" % int(n.dor))
	v.add_child(_wrap(tr("Temperatura corporal: %.1f °C · Roupa molhada: %d%% · Temperatura do ar: %.0f °C · Agasalho: %+.0f") % [n.body_temp, int(n.wetness), Game.ambient_temperature(), p.warmth()], 15))
	v.add_child(_wrap(tr("Efeitos atuais: velocidade x%.2f · tarefas x%.2f mais lentas") % [n.speed_multiplier(), n.work_multiplier()], 14, UITheme.MUTED))
	var worn: Array[String] = []
	for slot in p.equipped:
		worn.append(Data.item_name(p.equipped[slot]))
	v.add_child(_wrap(tr("Vestindo: %s") % (", ".join(worn) if not worn.is_empty() else tr("nada de especial"))))
	if not p.traits.is_empty():
		v.add_child(HSeparator.new())
		v.add_child(UITheme.label(tr("Traços"), 18, UITheme.ACCENT))
		for t in p.traits:
			var td: Dictionary = Data.trait_def(t)
			v.add_child(_wrap("%s — + %s  − %s" % [tr(td.name), tr(td.pro), tr(td.con)], 14))
	v.add_child(HSeparator.new())
	v.add_child(UITheme.label(tr("Estatísticas"), 18, UITheme.ACCENT))
	v.add_child(_wrap(tr("Dinheiro: R$ %d · Total ganho: R$ %d") % [p.money, int(p.stats.get("dinheiro_ganho", 0))]))
	v.add_child(_wrap(tr("Distância dirigida: %.1f km · Pedidos entregues: %d") % [float(p.stats.get("km_dirigidos", 0.0)), int(p.stats.get("pedidos_entregues", 0))]))
	v.add_child(_wrap(tr("Reputação na Vila Aurora: %d (%s)") % [Game.relations.reputation, tr(Relations.reputation_label(Game.relations.reputation))]))
	v.add_child(_wrap(tr("Capacidade de carga: %.0f kg") % p.capacity()))
	return v

func _skills() -> Control:
	var v := VBoxContainer.new()
	var s := Game.player.skills
	for id in Skills.DEFS:
		var d: Dictionary = Skills.DEFS[id]
		_row(v, "%s (nível %d)" % [tr(d.name), s.level(id)], s.progress(id) * 100.0, UITheme.ACCENT, "%d XP" % int(s.xp.get(id, 0)))
		v.add_child(_wrap("   " + tr(d.desc), 14, UITheme.MUTED))
	v.add_child(_wrap(tr("Habilidades sobem com a prática: cozinhar no drive-thru, conversar e persuadir, dirigir e correr."), 14, UITheme.MUTED))
	return v

func _relations() -> Control:
	var v := VBoxContainer.new()
	for id in Data.npcs():
		var r := Game.relations.get_rel(id)
		var col := UITheme.BAD.lerp(UITheme.GOOD, (r + 100) / 200.0)
		_row(v, tr(Data.npc(id).name), (r + 100) / 2.0, col, "%d · %s" % [r, tr(Game.relations.attitude(id))])
		var mems: Array = Game.relations.memories.get(id, [])
		if not mems.is_empty():
			var tags: Array[String] = []
			for m in mems:
				tags.append(_memory_text(str(m.tag)))
			v.add_child(_wrap("   " + tr("Lembra: ") + ", ".join(tags), 13, UITheme.MUTED))
	return v

func _memory_text(tag: String) -> String:
	var t := {
		"ajudou": "você ajudou", "ameacado": "foi ameaçado(a) por você", "atropelado": "foi atropelado(a) por você",
		"prestou_socorro": "você prestou socorro", "confessou": "você confessou", "viu_furto": "viu você furtar",
		"vitima_furto": "foi furtado(a) por você", "viu_invasao": "viu você invadir uma casa", "vitima_invasao": "teve a casa invadida por você",
		"viu_atropelamento": "viu você atropelar alguém", "vitima_atropelamento": "foi atropelado(a) por você",
		"vitima_ameaca": "foi ameaçado(a) por você", "viu_furto_veiculo": "viu você furtar um carro",
		"devolveu_carteira": "você devolveu a carteira dele(a)",
	}
	return tr(t.get(tag, tag))

func _work() -> Control:
	var v := VBoxContainer.new()
	var j := Game.job
	v.add_child(UITheme.label(tr("Brasa Burger Drive-Thru"), 18, UITheme.ACCENT))
	if j.fired:
		v.add_child(_wrap(tr("Você foi demitido(a)."), 15, UITheme.BAD))
	elif not j.employed:
		v.add_child(_wrap(tr("Você não trabalha lá. A gerente Sandra está contratando.")))
	else:
		v.add_child(_wrap(tr("Cargo: %s · Salário: R$ %d/hora + R$ 2 por pedido + gorjetas") % [j.rank_title(), j.wage()]))
		_row(v, tr("Desempenho"), j.performance, UITheme.ACCENT, "%d/100" % int(j.performance))
		v.add_child(_wrap(tr("Pedidos entregues: %d · Turnos completos: %d · Advertências: %d/3") % [j.total_orders, j.shifts_done, j.warnings]))
		var ranks: Array = Data.job("brasa").ranks
		if j.rank + 1 < ranks.size():
			var nx: Dictionary = ranks[j.rank + 1]
			v.add_child(_wrap(tr("Próxima promoção (%s): desempenho %d+ e %d pedidos.") % [tr(nx.title), int(nx.min_perf), int(nx.min_orders)], 14, UITheme.MUTED))
	v.add_child(HSeparator.new())
	v.add_child(UITheme.label(tr("Entregas do Mercadinho"), 18, UITheme.ACCENT))
	var dj := Game.delivery
	if not dj.hired:
		v.add_child(_wrap(tr("O Seu Antônio procura entregador. Pergunte a ele.")))
	else:
		v.add_child(_wrap(tr("Entregas feitas: %d (hoje: %d) · Atrasadas: %d") % [dj.total, dj.done_today, dj.late_total]))
		if not dj.active.is_empty():
			v.add_child(_wrap(tr("Em andamento: %s — %d min restantes") % [dj.active.label, int(dj.time_left(Clock.total_minutes))], 15, UITheme.QUEST))
	v.add_child(HSeparator.new())
	v.add_child(UITheme.label(tr("Imóveis e veículos"), 18, UITheme.ACCENT))
	for id in Game.property.props:
		var pr: Dictionary = Game.property.props[id]
		var cost := tr("Aluguel R$ %d/semana") % int(pr.rent) if pr.status == "alugada" else tr("IPTU R$ %d/semana · revenda R$ %d") % [int(pr.tax), Game.property.sale_value(id)]
		v.add_child(_wrap(tr("%s — %s · %s · Dívida: R$ %d") % [tr(Data.property(id).get("name", id)), tr(pr.status), cost, int(pr.debt)]))
	for id in Data.properties():
		if Game.property.is_for_sale(id):
			v.add_child(_wrap(tr("À venda: %s — R$ %d (placa na frente do imóvel)") % [tr(Data.property(id).name), Game.property.price(id)], 14, UITheme.MUTED))
	for vid in Game.world_state.vehicles:
		var vs: Dictionary = Game.world_state.vehicles[vid]
		if vs.get("owner", "") == "player":
			v.add_child(_wrap(tr("Veículo: %s — combustível %.0f L · dano %d%%") % [tr(Data.vehicle(vs.type).name), float(vs.fuel), int(vs.damage)]))
		elif vs.has("for_sale"):
			v.add_child(_wrap(tr("À venda na oficina: %s — R$ %d") % [tr(Data.vehicle(vs.type).name), int(vs.for_sale.price)], 14, UITheme.MUTED))
	v.add_child(_wrap(tr("Guarde comida na geladeira (estraga 4x mais devagar) e objetos no guarda-roupa: tudo fica salvo."), 14, UITheme.MUTED))
	return v

func _law() -> Control:
	var v := VBoxContainer.new()
	if Game.law.record.is_empty():
		v.add_child(_wrap(tr("Ficha limpa."), 16, UITheme.GOOD))
	for e in Game.law.record:
		var label: String = LawSystem.CRIMES.get(e.type, {}).get("label", e.type)
		var who := tr("sem testemunhas") if not e.get("solved", false) else tr("testemunhado — multa R$ %d") % int(e.fine)
		v.add_child(_wrap("%s %d — %s (%s)" % [tr("Dia"), int(e.day), tr(label), who], 15, UITheme.BAD if e.get("solved", false) else UITheme.MUTED))
	if Game.law.pending_fines > 0:
		v.add_child(_wrap(tr("Multas a chegar amanhã às 8h: R$ %d") % Game.law.pending_fines, 15, UITheme.WARN))
	return v
