extends CenterContainer
## Menu de pausa: continuar, salvar, carregar, opções, voltar ao menu, sair.

var ui: Node
var world: Node
var panel: PanelContainer
var content: VBoxContainer

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	process_mode = Node.PROCESS_MODE_ALWAYS

func open() -> void:
	panel = PanelContainer.new()
	panel.custom_minimum_size = Vector2(560, 0)
	add_child(panel)
	_main()

func _clear() -> void:
	for c in panel.get_children():
		panel.remove_child(c)
		c.queue_free()
	content = VBoxContainer.new()
	content.add_theme_constant_override("separation", 8)
	panel.add_child(content)

func _main() -> void:
	_clear()
	content.add_child(UITheme.label(tr("Pausado"), 26, UITheme.ACCENT))
	content.add_child(UITheme.label("%s, %s · %s %d" % [Clock.weekday_name(), Clock.time_string(), tr("Dia"), Clock.day()], 15, UITheme.MUTED))
	var cont := UITheme.button(tr("Continuar"), func(): ui.close_current())
	content.add_child(cont)
	content.add_child(UITheme.button(tr("Salvar jogo"), _save_menu))
	content.add_child(UITheme.button(tr("Carregar jogo"), _load_menu))
	content.add_child(UITheme.button(tr("Opções"), _options))
	content.add_child(UITheme.button(tr("Sair para o menu principal"), func():
		get_tree().paused = false
		get_tree().change_scene_to_file("res://src/ui/MainMenu.tscn")))
	content.add_child(UITheme.button(tr("Sair do jogo"), func(): get_tree().quit()))
	cont.grab_focus()

func _save_menu() -> void:
	_clear()
	content.add_child(UITheme.label(tr("Salvar jogo"), 24, UITheme.ACCENT))
	var why := Saves.block_reason()
	if why != "":
		content.add_child(UITheme.label(why, 16, UITheme.BAD))
	else:
		content.add_child(SaveSlots.build(true, func(slot):
			if Saves.save_game(slot):
				Events.notify.emit(tr("Jogo salvo."), "good")
				_main()
			else:
				Events.notify.emit(Saves.last_error, "bad")))
	content.add_child(UITheme.button(tr("Voltar"), _main))

func _load_menu() -> void:
	_clear()
	content.add_child(UITheme.label(tr("Carregar jogo"), 24, UITheme.ACCENT))
	content.add_child(UITheme.label(tr("O progresso não salvo será perdido."), 14, UITheme.WARN))
	content.add_child(SaveSlots.build(false, func(slot):
		if not Saves.load_and_play(slot):
			Events.notify.emit(Saves.last_error, "bad")))
	content.add_child(UITheme.button(tr("Voltar"), _main))

func _options() -> void:
	for c in panel.get_children():
		panel.remove_child(c)
		c.queue_free()
	var o: PanelContainer = load("res://src/ui/OptionsMenu.gd").new()
	o.on_back = _main
	o.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	panel.add_child(o)
