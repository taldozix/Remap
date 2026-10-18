extends PanelContainer
## Opções: gráficos, áudio, jogabilidade/acessibilidade e controles (remapeáveis).
## Tudo é aplicado na hora e salvo em user://settings.cfg ao voltar.

var on_back: Callable
var tabs: TabContainer
var _waiting_action := ""
var _waiting_button: Button

func _ready() -> void:
	custom_minimum_size = Vector2(820, 560)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 10)
	add_child(v)
	v.add_child(UITheme.label(tr("Opções"), 24, UITheme.ACCENT))
	tabs = TabContainer.new()
	tabs.custom_minimum_size = Vector2(780, 440)
	v.add_child(tabs)
	_build()
	var foot := HBoxContainer.new()
	v.add_child(foot)
	foot.add_child(UITheme.button(tr("Restaurar padrões desta aba"), _reset_tab))
	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	foot.add_child(sp)
	foot.add_child(UITheme.button(tr("Voltar"), _back))

func _build() -> void:
	var cur := tabs.current_tab if tabs.get_tab_count() > 0 else 0
	for c in tabs.get_children():
		tabs.remove_child(c)
		c.queue_free()
	_tab(tr("Gráficos"), _graphics())
	_tab(tr("Áudio"), _audio())
	_tab(tr("Jogabilidade"), _game())
	_tab(tr("Controles"), _controls())
	tabs.current_tab = cur

func _tab(title: String, content: Control) -> void:
	var s := ScrollContainer.new()
	s.name = title
	s.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.add_child(content)
	tabs.add_child(s)

func _line(parent: Control, label: String, ctrl: Control) -> void:
	var h := HBoxContainer.new()
	var l := UITheme.label(label, 16)
	l.custom_minimum_size = Vector2(300, 0)
	h.add_child(l)
	ctrl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(ctrl)
	parent.add_child(h)

func _option(section: String, key: String, items: Array, values: Array = []) -> OptionButton:
	var ob := OptionButton.new()
	for it in items:
		ob.add_item(tr(str(it)))
	var cur = Settings.get_value(section, key)
	var idx := values.find(cur) if not values.is_empty() else int(cur)
	ob.select(max(0, idx))
	ob.item_selected.connect(func(i):
		Settings.set_value(section, key, values[i] if not values.is_empty() else i))
	return ob

func _slider(section: String, key: String, mn: float, mx: float, step: float, fmt: String = "%d%%", mult: float = 100.0) -> HBoxContainer:
	var h := HBoxContainer.new()
	var s := HSlider.new()
	s.min_value = mn
	s.max_value = mx
	s.step = step
	s.value = float(Settings.get_value(section, key))
	s.custom_minimum_size = Vector2(260, 24)
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var l := UITheme.label(fmt % (s.value * mult), 15, UITheme.MUTED)
	l.custom_minimum_size = Vector2(70, 0)
	s.value_changed.connect(func(val):
		l.text = fmt % (val * mult)
		Settings.set_value(section, key, val))
	h.add_child(s)
	h.add_child(l)
	return h

func _check(section: String, key: String) -> CheckButton:
	var c := CheckButton.new()
	c.button_pressed = bool(Settings.get_value(section, key))
	c.toggled.connect(func(on): Settings.set_value(section, key, on))
	return c

func _graphics() -> Control:
	var v := VBoxContainer.new()
	_line(v, tr("Resolução (janela)"), _option("graphics", "resolution", Settings.RESOLUTIONS, Settings.RESOLUTIONS))
	_line(v, tr("Modo de tela"), _option("graphics", "window_mode", ["Janela", "Tela cheia", "Tela cheia sem bordas"]))
	_line(v, tr("Escala de renderização"), _slider("graphics", "render_scale", 0.5, 1.0, 0.05))
	_line(v, tr("Qualidade das sombras"), _option("graphics", "shadows", ["Desligadas", "Baixa", "Média", "Alta"]))
	_line(v, tr("Qualidade dos efeitos"), _option("graphics", "effects", ["Baixa", "Média", "Alta (reflexos)"]))
	_line(v, tr("Densidade da grama"), _option("graphics", "grass_density", ["Sem grama", "Baixa", "Média", "Alta"]))
	_line(v, tr("Desfoque de miniatura (tilt-shift)"), _option("graphics", "tilt_shift", ["Desligado", "Sutil", "Forte"]))
	_line(v, tr("Gradação de cor e vinheta"), _check("graphics", "color_grading"))
	_line(v, tr("Distância de visão / simulação detalhada"), _option("graphics", "view_distance", ["Curta", "Média", "Longa"]))
	_line(v, tr("Densidade de pedestres (ao carregar)"), _slider("graphics", "npc_density", 0.0, 1.5, 0.25))
	_line(v, tr("Densidade do trânsito"), _slider("graphics", "traffic_density", 0.0, 1.5, 0.25))
	_line(v, tr("Sincronização vertical"), _check("graphics", "vsync"))
	_line(v, tr("Limite de FPS"), _option("graphics", "fps_limit", ["Sem limite", "30", "60", "120", "144"], [0, 30, 60, 120, 144]))
	_line(v, tr("Filtro de texturas"), _option("graphics", "texture_filter", ["Bilinear", "Anisotrópico 4x", "Anisotrópico 16x"]))
	_line(v, tr("Antisserrilhamento (MSAA)"), _option("graphics", "msaa", ["Desligado", "2x", "4x"]))
	return v

func _audio() -> Control:
	var v := VBoxContainer.new()
	_line(v, tr("Volume geral"), _slider("audio", "master", 0.0, 1.0, 0.05))
	_line(v, tr("Música"), _slider("audio", "music", 0.0, 1.0, 0.05))
	_line(v, tr("Efeitos sonoros"), _slider("audio", "sfx", 0.0, 1.0, 0.05))
	_line(v, tr("Vozes"), _slider("audio", "voice", 0.0, 1.0, 0.05))
	_line(v, tr("Sons ambientes"), _slider("audio", "ambient", 0.0, 1.0, 0.05))
	v.add_child(UITheme.label(tr("Os sons do protótipo são sintetizados pelo próprio jogo (originais). Arquivos em assets/audio substituem automaticamente."), 13, UITheme.MUTED))
	return v

func _game() -> Control:
	var v := VBoxContainer.new()
	var langs: Array = Settings.LANGUAGES.keys()
	var names: Array = []
	for k in langs:
		names.append(Settings.LANGUAGES[k])
	_line(v, tr("Idioma"), _option("game", "language", names, langs))
	_line(v, tr("Legendas (falas sobre os personagens)"), _check("game", "subtitles"))
	_line(v, tr("Tamanho do texto"), _slider("game", "text_scale", 0.8, 1.5, 0.05))
	_line(v, tr("Sensibilidade da câmera"), _slider("game", "camera_sensitivity", 0.5, 2.0, 0.1, "%.1fx", 1.0))
	_line(v, tr("Mostrar dicas"), _check("game", "show_hints"))
	v.add_child(UITheme.label(tr("A tradução para inglês cobre menus e interface; diálogos do protótipo estão em português."), 13, UITheme.MUTED))
	return v

func _controls() -> Control:
	var v := VBoxContainer.new()
	v.add_child(UITheme.label(tr("Clique em uma ação e pressione a nova tecla (Esc cancela)."), 14, UITheme.MUTED))
	for action in Settings.ACTIONS:
		var b := Button.new()
		b.text = Settings.binding_text(action)
		b.pressed.connect(_start_rebind.bind(action, b))
		_line(v, tr(Settings.ACTIONS[action].label), b)
	return v

func _start_rebind(action: String, b: Button) -> void:
	_waiting_action = action
	_waiting_button = b
	b.text = tr("Pressione uma tecla...")

func _input(event: InputEvent) -> void:
	if _waiting_action == "":
		return
	if event is InputEventKey and event.pressed and not event.echo:
		get_viewport().set_input_as_handled()
		if event.keycode == KEY_ESCAPE:
			_waiting_button.text = Settings.binding_text(_waiting_action)
		else:
			Settings.rebind(_waiting_action, event)
			_build()
			tabs.current_tab = 3
		_waiting_action = ""
	elif event is InputEventMouseButton and event.pressed and event.button_index not in [MOUSE_BUTTON_LEFT]:
		get_viewport().set_input_as_handled()
		Settings.rebind(_waiting_action, event)
		_waiting_action = ""
		_build()
		tabs.current_tab = 3

func _reset_tab() -> void:
	var section: String = ["graphics", "audio", "game", "input"][tabs.current_tab]
	Settings.reset_section(section)
	_build()

func _back() -> void:
	Settings.save_settings()
	if on_back.is_valid():
		on_back.call()
