extends Control
## Caixa de diálogo inspirada na clareza de Zelda: BotW — nome em destaque, retrato,
## texto com efeito de digitação, opções à direita. O relógio do jogo pausa durante a
## conversa; personagens importantes recebem enquadramento de câmera mais próximo.

var ui: Node
var world: Node
var npc: Node
var runner: DialogueRunner
var box: PanelContainer
var name_label: Label
var text_label: RichTextLabel
var portrait: PanelContainer
var choices_box: VBoxContainer
var hint_label: Label
var _typing := 0.0
var _full_len := 0
var _prev_dist := 0.0

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE

func open() -> void:
	runner = DialogueRunner.new()
	Clock.hold()
	npc.begin_talk()
	if npc.def.get("important", false):
		_prev_dist = world.camera_rig._dist_goal
		world.camera_rig._dist_goal = 11.0
	box = PanelContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	box.custom_minimum_size = Vector2(980, 200)
	box.position = Vector2(-490, -230)
	var st := UITheme.panel_style(Color(0.05, 0.06, 0.08, 0.9), 16, 20)
	st.border_color = Color(1, 1, 1, 0.12)
	box.add_theme_stylebox_override("panel", st)
	add_child(box)
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 18)
	box.add_child(h)
	portrait = PanelContainer.new()
	portrait.custom_minimum_size = Vector2(110, 110)
	var app: Dictionary = npc.def.get("appearance", {})
	var ps := UITheme.panel_style(Color(app.get("shirt", "#446688")).darkened(0.2), 55, 0)
	ps.border_color = Color(app.get("skin", "#d9a98a"))
	ps.set_border_width_all(5)
	portrait.add_theme_stylebox_override("panel", ps)
	var initials := UITheme.label(_initials(npc.display_name), 40)
	initials.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	initials.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	portrait.add_child(initials)
	var pv := VBoxContainer.new()
	pv.add_child(portrait)
	h.add_child(pv)
	var mid := VBoxContainer.new()
	mid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(mid)
	var tag := PanelContainer.new()
	var ts := UITheme.panel_style(UITheme.ACCENT, 12, 6)
	tag.add_theme_stylebox_override("panel", ts)
	tag.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	name_label = UITheme.label("", 18, Color(0.1, 0.08, 0.05))
	tag.add_child(name_label)
	mid.add_child(tag)
	text_label = RichTextLabel.new()
	text_label.bbcode_enabled = true
	text_label.fit_content = true
	text_label.custom_minimum_size = Vector2(480, 90)
	text_label.add_theme_font_size_override("normal_font_size", UITheme.font_size(19))
	text_label.add_theme_font_size_override("bold_font_size", UITheme.font_size(19))
	mid.add_child(text_label)
	hint_label = UITheme.label("", 14, UITheme.MUTED)
	mid.add_child(hint_label)
	choices_box = VBoxContainer.new()
	choices_box.custom_minimum_size = Vector2(320, 0)
	choices_box.add_theme_constant_override("separation", 6)
	h.add_child(choices_box)
	if not runner.start(npc.npc_id, npc.def.get("dialogue", "pedestre")):
		call_deferred("_finish")
		return
	_show_node()

func _initials(n: String) -> String:
	var parts := n.split(" ", false)
	var out := ""
	for p in parts:
		if p.length() > 0 and out.length() < 2 and p[0] == p[0].to_upper():
			out += p[0]
	return out if out != "" else n.substr(0, 1)

func _show_node() -> void:
	if runner.finished:
		_finish()
		return
	name_label.text = runner.speaker_name()
	var prefix := ""
	if not runner.last_check.is_empty():
		var ok: bool = runner.last_check.success
		prefix = "[color=%s][%s: %s][/color]\n" % ["#7fd17f" if ok else "#ef6b5b", runner.last_check.label, tr("sucesso") if ok else tr("falhou")]
	text_label.text = prefix + runner.text()
	_full_len = text_label.get_total_character_count()
	text_label.visible_characters = 0
	_typing = 0.0
	for c in choices_box.get_children():
		c.queue_free()
	choices_box.visible = false
	hint_label.text = ""

func _show_choices() -> void:
	choices_box.visible = true
	var cs := runner.choices()
	if cs.is_empty():
		hint_label.text = tr("[%s / clique] continuar") % Settings.primary_key_text("interact")
		return
	hint_label.text = tr("[Esc] encerrar conversa")
	var n := 1
	for c in cs:
		var b := UITheme.button("%d. %s" % [n, c.text], _choose.bind(c.index))
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		b.custom_minimum_size = Vector2(320, 0)
		if not c.enabled:
			b.disabled = true
			if c.hint != "":
				b.text += " (%s)" % c.hint
		choices_box.add_child(b)
		n += 1
	var first := choices_box.get_child(0) as Button
	if first:
		first.grab_focus()

func _process(delta: float) -> void:
	if text_label == null or choices_box.visible:
		return
	_typing += delta * 55.0
	text_label.visible_characters = int(_typing)
	if _typing >= _full_len:
		text_label.visible_characters = -1
		_show_choices()

func _skip_or_advance() -> void:
	if not choices_box.visible:
		_typing = _full_len
		return
	if runner.choices().is_empty():
		runner.advance()
		_show_node()

func _gui_input(_e: InputEvent) -> void:
	pass

func _unhandled_input(event: InputEvent) -> void:
	if runner == null:
		return
	if event.is_action_pressed("interact") or (event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT) or (event is InputEventKey and event.pressed and event.keycode == KEY_SPACE):
		get_viewport().set_input_as_handled()
		_skip_or_advance()
		return
	if choices_box.visible:
		for i in 4:
			if event.is_action_pressed("option_%d" % (i + 1)):
				var cs := runner.choices()
				if i < cs.size() and cs[i].enabled:
					get_viewport().set_input_as_handled()
					_choose(cs[i].index)
				return

func _choose(index: int) -> void:
	Game.player.skills.add_xp("social", 1)
	runner.choose(index)
	_show_node()

## Encerrar naturalmente (Esc): o NPC se despede.
func try_close() -> void:
	npc.bark(tr("Até mais."))
	_finish()

func _finish() -> void:
	ui.close_current()

func on_close() -> void:
	Clock.release()
	if is_instance_valid(npc):
		npc.end_talk()
	if _prev_dist > 0.0:
		world.camera_rig._dist_goal = _prev_dist
	Events.dialogue_closed.emit()
