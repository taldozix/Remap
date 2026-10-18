extends Node
## Configurações persistentes (user://settings.cfg): gráficos, áudio, jogabilidade,
## idioma e mapeamento de teclas. Tudo aqui é aplicado de verdade e salvo.

signal changed(section: String)

const PATH := "user://settings.cfg"

const RESOLUTIONS := ["1280x720", "1366x768", "1600x900", "1920x1080", "2560x1440"]
const LANGUAGES := {"pt_BR": "Português (Brasil)", "en": "English"}

const DEFAULTS := {
	"graphics": {
		"resolution": "1280x720",
		"window_mode": 0,        # 0 janela, 1 tela cheia, 2 sem bordas
		"render_scale": 1.0,
		"shadows": 2,            # 0 desligado … 3 ultra
		"effects": 1,            # 0 baixo, 1 médio, 2 alto
		"view_distance": 1,      # 0 curta, 1 média, 2 longa
		"npc_density": 1.0,
		"traffic_density": 1.0,
		"vsync": true,
		"fps_limit": 60,
		"texture_filter": 1,     # 0 bilinear, 1 aniso 4x, 2 aniso 16x
		"msaa": 1,               # 0 off, 1 2x, 2 4x
		"grass_density": 2,      # 0 sem grama … 3 alta
		"tilt_shift": 1,         # desfoque de miniatura: 0 off, 1 sutil, 2 forte
		"model_detail": 2,       # detalhe dos modelos (LOD): 0 baixo … 2 alto
		"color_grading": true,   # gradação de cor e vinheta
	},
	"audio": {"master": 0.8, "music": 0.6, "sfx": 0.8, "voice": 0.8, "ambient": 0.7},
	"game": {
		"language": "pt_BR",
		"subtitles": true,
		"text_scale": 1.0,
		"camera_sensitivity": 1.0,
		"show_hints": true,
	},
}

## Ações e teclas padrão. O jogador pode remapear tudo em Opções > Controles.
const ACTIONS := {
	"move_up": {"label": "Mover para cima / acelerar", "keys": [KEY_W, KEY_UP]},
	"move_down": {"label": "Mover para baixo / frear e ré", "keys": [KEY_S, KEY_DOWN]},
	"move_left": {"label": "Mover para a esquerda / virar", "keys": [KEY_A, KEY_LEFT]},
	"move_right": {"label": "Mover para a direita / virar", "keys": [KEY_D, KEY_RIGHT]},
	"run": {"label": "Correr", "keys": [KEY_SHIFT]},
	"interact": {"label": "Interagir", "keys": [KEY_E]},
	"cycle_target": {"label": "Alternar alvo de interação", "keys": [KEY_T]},
	"vehicle": {"label": "Entrar/sair do veículo", "keys": [KEY_F]},
	"handbrake": {"label": "Freio de mão", "keys": [KEY_SPACE]},
	"horn": {"label": "Buzina", "keys": [KEY_H]},
	"inventory": {"label": "Inventário", "keys": [KEY_TAB]},
	"journal": {"label": "Diário (missões, habilidades, relações)", "keys": [KEY_J]},
	"map": {"label": "Mapa", "keys": [KEY_M]},
	"cam_left": {"label": "Girar câmera (esquerda)", "keys": [KEY_Z]},
	"cam_right": {"label": "Girar câmera (direita)", "keys": [KEY_X]},
	"zoom_in": {"label": "Aproximar câmera", "mouse": [MOUSE_BUTTON_WHEEL_UP], "keys": [KEY_EQUAL]},
	"zoom_out": {"label": "Afastar câmera", "mouse": [MOUSE_BUTTON_WHEEL_DOWN], "keys": [KEY_MINUS]},
	"option_1": {"label": "Opção de interação 1", "keys": [KEY_1]},
	"option_2": {"label": "Opção de interação 2", "keys": [KEY_2]},
	"option_3": {"label": "Opção de interação 3", "keys": [KEY_3]},
	"option_4": {"label": "Opção de interação 4", "keys": [KEY_4]},
	"pause": {"label": "Pausa / voltar", "keys": [KEY_ESCAPE]},
	"quick_save": {"label": "Salvamento rápido", "keys": [KEY_F5]},
	"debug": {"label": "Painel de diagnóstico", "keys": [KEY_F3]},
}

var data: Dictionary = {}
var bindings: Dictionary = {}  # action -> Array de dicionários de evento

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_load_translations()
	load_settings()
	apply_all()

func get_value(section: String, key: String):
	return data.get(section, {}).get(key, DEFAULTS[section][key])

func set_value(section: String, key: String, value) -> void:
	if not data.has(section):
		data[section] = {}
	data[section][key] = value
	match section:
		"graphics": apply_graphics()
		"audio": apply_audio()
		"game": apply_game()
	changed.emit(section)

func load_settings() -> void:
	data = DEFAULTS.duplicate(true)
	bindings = _default_bindings()
	var cfg := ConfigFile.new()
	if cfg.load(PATH) != OK:
		_apply_bindings()
		return
	for section in DEFAULTS:
		for key in DEFAULTS[section]:
			if cfg.has_section_key(section, key):
				var v = cfg.get_value(section, key)
				if typeof(v) == typeof(DEFAULTS[section][key]) or (typeof(v) in [TYPE_INT, TYPE_FLOAT] and typeof(DEFAULTS[section][key]) in [TYPE_INT, TYPE_FLOAT]):
					data[section][key] = v
	if cfg.has_section("input"):
		for action in ACTIONS:
			if cfg.has_section_key("input", action):
				var evs = cfg.get_value("input", action)
				if typeof(evs) == TYPE_ARRAY:
					bindings[action] = evs
	_apply_bindings()

func save_settings() -> void:
	var cfg := ConfigFile.new()
	for section in data:
		for key in data[section]:
			cfg.set_value(section, key, data[section][key])
	for action in bindings:
		cfg.set_value("input", action, bindings[action])
	var err := cfg.save(PATH)
	if err != OK:
		push_error("Falha ao salvar configurações: %s" % err)

func reset_section(section: String) -> void:
	if section == "input":
		bindings = _default_bindings()
		_apply_bindings()
	else:
		data[section] = DEFAULTS[section].duplicate(true)
	apply_all()
	changed.emit(section)

func apply_all() -> void:
	apply_graphics()
	apply_audio()
	apply_game()

# ---------------------------------------------------------------- gráficos
func apply_graphics() -> void:
	if DisplayServer.get_name() == "headless":
		return
	var mode: int = get_value("graphics", "window_mode")
	match mode:
		0:
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
			DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, false)
			var parts: PackedStringArray = str(get_value("graphics", "resolution")).split("x")
			if parts.size() == 2:
				var size := Vector2i(int(parts[0]), int(parts[1]))
				var screen := DisplayServer.screen_get_size()
				if screen.x > 0:
					size = Vector2i(min(size.x, screen.x), min(size.y, screen.y))
				DisplayServer.window_set_size(size)
		1:
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN)
		2:
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if get_value("graphics", "vsync") else DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = int(get_value("graphics", "fps_limit"))
	var vp := get_viewport()
	if vp:
		vp.scaling_3d_scale = clampf(float(get_value("graphics", "render_scale")), 0.5, 1.0)
		vp.msaa_3d = [Viewport.MSAA_DISABLED, Viewport.MSAA_2X, Viewport.MSAA_4X][int(get_value("graphics", "msaa"))]
		vp.anisotropic_filtering_level = [Viewport.ANISOTROPY_DISABLED, Viewport.ANISOTROPY_4X, Viewport.ANISOTROPY_16X][int(get_value("graphics", "texture_filter"))]
		# Limiar de LOD das malhas (carros e personagens têm LODs gerados na importação).
		vp.mesh_lod_threshold = [6.0, 2.5, 1.0][clampi(int(get_value("graphics", "model_detail")), 0, 2)]
	var shadow_q: int = get_value("graphics", "shadows")
	RenderingServer.directional_shadow_atlas_set_size([1024, 2048, 4096, 8192][shadow_q], true)
	RenderingServer.directional_soft_shadow_filter_set_quality([0, 1, 2, 4][shadow_q])

# ---------------------------------------------------------------- áudio
func apply_audio() -> void:
	for bus_name in ["Master", "Music", "SFX", "Voice", "Ambient"]:
		var idx := AudioServer.get_bus_index(bus_name)
		if idx < 0:
			continue
		var key: String = bus_name.to_lower()
		var v: float = clampf(float(get_value("audio", key)), 0.0, 1.0)
		AudioServer.set_bus_volume_db(idx, linear_to_db(max(v, 0.0001)))
		AudioServer.set_bus_mute(idx, v <= 0.001)

# ---------------------------------------------------------------- jogo
func apply_game() -> void:
	TranslationServer.set_locale(str(get_value("game", "language")))

func text_scale() -> float:
	return clampf(float(get_value("game", "text_scale")), 0.75, 1.6)

func _load_translations() -> void:
	# Português é o idioma-fonte; outros idiomas são tabelas fonte→tradução em data/i18n.
	for locale in LANGUAGES:
		var path := "res://data/i18n/%s.json" % locale
		if not FileAccess.file_exists(path):
			continue
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(parsed) != TYPE_DICTIONARY:
			continue
		var t := Translation.new()
		t.locale = locale
		for k in parsed:
			t.add_message(k, str(parsed[k]))
		TranslationServer.add_translation(t)

# ---------------------------------------------------------------- controles
func _default_bindings() -> Dictionary:
	var out := {}
	for action in ACTIONS:
		var list: Array = []
		for k in ACTIONS[action].get("keys", []):
			list.append({"type": "key", "code": k})
		for m in ACTIONS[action].get("mouse", []):
			list.append({"type": "mouse", "button": m})
		out[action] = list
	return out

func _apply_bindings() -> void:
	for action in ACTIONS:
		if not InputMap.has_action(action):
			InputMap.add_action(action, 0.2)
		InputMap.action_erase_events(action)
		for ev_dict in bindings.get(action, []):
			var ev := _dict_to_event(ev_dict)
			if ev:
				InputMap.action_add_event(action, ev)

## Substitui a tecla principal de uma ação (mantém as secundárias).
func rebind(action: String, event: InputEvent) -> void:
	var d := _event_to_dict(event)
	if d.is_empty():
		return
	var list: Array = bindings.get(action, []).duplicate()
	if list.is_empty():
		list.append(d)
	else:
		list[0] = d
	# Remove o mesmo atalho de outras ações para evitar conflito.
	for other in bindings:
		if other == action:
			continue
		var filtered: Array = []
		for e in bindings[other]:
			if e != d:
				filtered.append(e)
		bindings[other] = filtered
	bindings[action] = list
	_apply_bindings()
	changed.emit("input")

func binding_text(action: String) -> String:
	var list: Array = bindings.get(action, [])
	if list.is_empty():
		return "—"
	var parts: Array[String] = []
	for d in list:
		parts.append(_event_text(d))
	return " / ".join(parts)

func primary_key_text(action: String) -> String:
	var list: Array = bindings.get(action, [])
	return _event_text(list[0]) if not list.is_empty() else "?"

func _event_text(d: Dictionary) -> String:
	if d.get("type") == "key":
		return OS.get_keycode_string(int(d.get("code", 0)))
	match int(d.get("button", 0)):
		MOUSE_BUTTON_LEFT: return tr("Mouse esq.")
		MOUSE_BUTTON_RIGHT: return tr("Mouse dir.")
		MOUSE_BUTTON_MIDDLE: return tr("Mouse meio")
		MOUSE_BUTTON_WHEEL_UP: return tr("Roda ↑")
		MOUSE_BUTTON_WHEEL_DOWN: return tr("Roda ↓")
	return "Mouse %d" % int(d.get("button", 0))

func _event_to_dict(ev: InputEvent) -> Dictionary:
	if ev is InputEventKey:
		var code: int = ev.physical_keycode if ev.keycode == 0 else ev.keycode
		return {"type": "key", "code": code}
	if ev is InputEventMouseButton:
		return {"type": "mouse", "button": ev.button_index}
	return {}

func _dict_to_event(d: Dictionary) -> InputEvent:
	match d.get("type", ""):
		"key":
			var k := InputEventKey.new()
			k.keycode = int(d.get("code", 0))
			return k
		"mouse":
			var m := InputEventMouseButton.new()
			m.button_index = int(d.get("button", 1))
			return m
	return null
