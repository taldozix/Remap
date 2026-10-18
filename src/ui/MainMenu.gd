extends Control
## Menu principal: novo jogo (com criação de personagem), continuar, carregar,
## opções, créditos e sair. Fundo com um personagem 3D girando.

var center: CenterContainer
var preview: Humanoid
var _app: Dictionary = {}
var _name_edit: LineEdit
var _pivot: Node3D
var _traits: Array = []

const SKINS := ["#f0c8a8", "#e0b494", "#c68e6a", "#a8765a", "#8a5a3c", "#5e3b28"]
const HAIRS := ["#1a1a1a", "#2b1d12", "#6b3d1e", "#a0522d", "#d8c080", "#9a9a9a"]
const SHIRTS := ["#3f7f9f", "#b8432f", "#2d7d46", "#e0b521", "#7a5ea8", "#eeeeea"]
const STYLES := ["short", "long", "curly", "bob", "ponytail", "bald", "bun"]
const STYLE_NAMES := ["Curto", "Longo", "Cacheado", "Chanel", "Rabo de cavalo", "Careca", "Coque"]

func _ready() -> void:
	Game.world = null
	Clock.running = false
	get_tree().paused = false
	theme = UITheme.make()
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := TextureRect.new()
	var grad := GradientTexture2D.new()
	var g := Gradient.new()
	g.set_color(0, Color(0.11, 0.09, 0.14))
	g.set_color(1, Color(0.36, 0.2, 0.14))
	grad.gradient = g
	grad.fill_from = Vector2(0, 0)
	grad.fill_to = Vector2(1, 1)
	bg.texture = grad
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.stretch_mode = TextureRect.STRETCH_SCALE
	add_child(bg)
	_build_preview()
	var left := VBoxContainer.new()
	left.position = Vector2(70, 80)
	left.add_theme_constant_override("separation", 6)
	add_child(left)
	left.add_child(UITheme.label("VILA AURORA", 64, UITheme.ACCENT))
	left.add_child(UITheme.label(tr("Open World Life RPG — protótipo jogável"), 20, UITheme.TEXT))
	left.add_child(UITheme.label(tr("Porto Serra · um bairro pequeno, porém vivo"), 15, UITheme.MUTED))
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0, 30)
	left.add_child(spacer)
	center = CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.offset_left = 420
	center.offset_right = -40
	center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(center)
	_main_buttons(left)
	Audio.play_music("music_menu")

func _build_preview() -> void:
	var svc := SubViewportContainer.new()
	svc.stretch = true
	svc.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	svc.offset_left = -520
	svc.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(svc)
	var sv := SubViewport.new()
	sv.transparent_bg = true
	sv.own_world_3d = true
	svc.add_child(sv)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35, 30, 0)
	light.light_energy = 1.3
	sv.add_child(light)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_CLEAR_COLOR
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.6, 0.55, 0.6)
	env.environment.ambient_light_energy = 0.8
	sv.add_child(env)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 1.1, 3.6)
	cam.fov = 40
	sv.add_child(cam)
	cam.look_at(Vector3(0, 0.95, 0))
	_pivot = Node3D.new()
	sv.add_child(_pivot)
	var floor_mi := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.9
	cyl.bottom_radius = 0.9
	cyl.height = 0.06
	floor_mi.mesh = cyl
	floor_mi.material_override = Mat.color(Color(0.25, 0.2, 0.2))
	floor_mi.position.y = -0.03
	_pivot.add_child(floor_mi)
	_app = Game.player.appearance.duplicate()
	_rebuild_preview()

func _rebuild_preview() -> void:
	if preview:
		preview.queue_free()
	preview = Humanoid.new()
	_pivot.add_child(preview)
	preview.build(_app)
	preview.set_pose("idle")

func _process(delta: float) -> void:
	if _pivot:
		_pivot.rotation.y += delta * 0.4

func _main_buttons(parent: Control) -> void:
	var latest := Saves.latest_slot()
	var b1 := UITheme.button(tr("Novo jogo"), _new_game_panel)
	b1.custom_minimum_size = Vector2(320, 44)
	parent.add_child(b1)
	var b2 := UITheme.button(tr("Continuar"), func():
		if not Saves.load_and_play(latest):
			_message(tr("Erro"), Saves.last_error))
	b2.disabled = latest == ""
	b2.custom_minimum_size = Vector2(320, 44)
	parent.add_child(b2)
	for pair in [[tr("Carregar jogo"), _load_panel], [tr("Opções"), _options_panel], [tr("Créditos"), _credits], [tr("Sair"), func(): get_tree().quit()]]:
		var b := UITheme.button(pair[0], pair[1])
		b.custom_minimum_size = Vector2(320, 44)
		parent.add_child(b)
	b1.grab_focus()

func _clear_center() -> void:
	for c in center.get_children():
		c.queue_free()

func _panel(min_size: Vector2) -> VBoxContainer:
	_clear_center()
	var p := PanelContainer.new()
	p.custom_minimum_size = min_size
	center.add_child(p)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 10)
	p.add_child(v)
	return v

func _new_game_panel() -> void:
	var v := _panel(Vector2(520, 0))
	v.add_child(UITheme.label(tr("Novo personagem"), 24, UITheme.ACCENT))
	v.add_child(UITheme.label(tr("Nome"), 15, UITheme.MUTED))
	_name_edit = LineEdit.new()
	_name_edit.text = Game.player.name
	_name_edit.max_length = 20
	v.add_child(_name_edit)
	_swatches(v, tr("Tom de pele"), SKINS, "skin")
	_swatches(v, tr("Cor do cabelo"), HAIRS, "hair")
	_swatches(v, tr("Camisa"), SHIRTS, "shirt")
	v.add_child(UITheme.label(tr("Cabelo"), 15, UITheme.MUTED))
	var ob := OptionButton.new()
	for i in STYLES.size():
		ob.add_item(tr(STYLE_NAMES[i]))
	ob.select(max(0, STYLES.find(_app.get("hair_style", "short"))))
	ob.item_selected.connect(func(i):
		_app.hair_style = STYLES[i]
		_rebuild_preview())
	v.add_child(ob)
	v.add_child(UITheme.label(tr("Traços (escolha até 2 — cada um tem vantagem e desvantagem)"), 15, UITheme.MUTED))
	var tf := HFlowContainer.new()
	for tid in Data.traits():
		var td: Dictionary = Data.trait_def(tid)
		var cb := CheckBox.new()
		cb.text = tr(td.name)
		cb.tooltip_text = "+ %s\n− %s" % [tr(td.pro), tr(td.con)]
		cb.button_pressed = _traits.has(tid)
		cb.toggled.connect(func(on):
			if on and not _traits.has(tid):
				if _traits.size() >= 2:
					cb.set_pressed_no_signal(false)
					return
				_traits.append(tid)
			elif not on:
				_traits.erase(tid)
			_trait_info.text = _traits_text())
		tf.add_child(cb)
	v.add_child(tf)
	_trait_info = UITheme.label(_traits_text(), 13, UITheme.MUTED)
	v.add_child(_trait_info)
	v.add_child(UITheme.label(tr("Você acaba de se mudar para a casa 8 da Vila Aurora. Sem emprego, com R$ 60 no bolso e o aluguel vencendo no domingo."), 14, UITheme.MUTED))
	var h := HBoxContainer.new()
	v.add_child(h)
	h.add_child(UITheme.button(tr("Começar"), func(): Saves.start_new_game(_name_edit.text, _app, _traits)))
	h.add_child(UITheme.button(tr("Voltar"), _clear_center))
	for l in v.get_children():
		if l is Label:
			l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			l.custom_minimum_size.x = 480

var _trait_info: Label

func _traits_text() -> String:
	if _traits.is_empty():
		return tr("Nenhum traço escolhido.")
	var parts: Array[String] = []
	for t in _traits:
		var td: Dictionary = Data.trait_def(t)
		parts.append("%s: + %s − %s" % [tr(td.name), tr(td.pro), tr(td.con)])
	return "\n".join(parts)

func _swatches(parent: Control, label: String, colors: Array, key: String) -> void:
	parent.add_child(UITheme.label(label, 15, UITheme.MUTED))
	var h := HBoxContainer.new()
	for c in colors:
		var b := Button.new()
		b.custom_minimum_size = Vector2(40, 32)
		var sb := StyleBoxFlat.new()
		sb.bg_color = Color(c)
		sb.set_corner_radius_all(6)
		if _app.get(key, "") == c:
			sb.border_color = Color.WHITE
			sb.set_border_width_all(3)
		for st in ["normal", "hover", "pressed", "focus"]:
			b.add_theme_stylebox_override(st, sb)
		b.pressed.connect(func():
			_app[key] = c
			_rebuild_preview()
			_new_game_panel())
		h.add_child(b)
	parent.add_child(h)

func _load_panel() -> void:
	var v := _panel(Vector2(620, 0))
	v.add_child(UITheme.label(tr("Carregar jogo"), 24, UITheme.ACCENT))
	v.add_child(SaveSlots.build(false, func(slot):
		if not Saves.load_and_play(slot):
			_message(tr("Erro ao carregar"), Saves.last_error)))
	v.add_child(UITheme.button(tr("Voltar"), _clear_center))

func _options_panel() -> void:
	_clear_center()
	var o: PanelContainer = load("res://src/ui/OptionsMenu.gd").new()
	o.on_back = func():
		_clear_center()
		theme = UITheme.make()
	center.add_child(o)

func _credits() -> void:
	var v := _panel(Vector2(620, 0))
	v.add_child(UITheme.label(tr("Créditos"), 24, UITheme.ACCENT))
	var l := UITheme.label(tr("Vila Aurora — protótipo do Open World Life RPG.\n\nDesign, código, mapa, personagens, diálogos e sons: originais, criados para este projeto (sons e modelos gerados proceduralmente).\nMotor: Godot Engine 4 (licença MIT).\n\nInspirações declaradas: a profundidade sistêmica de Project Zomboid, a liberdade urbana de GTA e a imersão de Red Dead Redemption 2 — sem uso de qualquer ativo, nome ou personagem desses jogos."), 15)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(580, 0)
	v.add_child(l)
	v.add_child(UITheme.button(tr("Voltar"), _clear_center))

func _message(title: String, body: String) -> void:
	var v := _panel(Vector2(480, 0))
	v.add_child(UITheme.label(title, 22, UITheme.BAD))
	v.add_child(UITheme.label(body, 16))
	v.add_child(UITheme.button(tr("OK"), _clear_center))
