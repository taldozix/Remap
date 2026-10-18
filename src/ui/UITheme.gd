class_name UITheme
extends RefCounted
## Tema visual da interface (gerado em código; respeita o tamanho de texto das opções).

const ACCENT := Color("e8913a")
const BG := Color(0.07, 0.08, 0.1, 0.93)
const BG_LIGHT := Color(0.13, 0.15, 0.18, 0.95)
const TEXT := Color(0.94, 0.93, 0.9)
const MUTED := Color(0.62, 0.64, 0.66)
const GOOD := Color("7fd17f")
const BAD := Color("ef6b5b")
const WARN := Color("f2c14e")
const QUEST := Color("8fc7ff")

static func font_size(base: int = 17) -> int:
	return int(round(base * Settings.text_scale()))

static func panel_style(bg: Color = BG, radius: int = 10, margin: int = 14) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.set_corner_radius_all(radius)
	s.border_color = Color(1, 1, 1, 0.07)
	s.set_border_width_all(1)
	s.content_margin_left = margin
	s.content_margin_right = margin
	s.content_margin_top = margin * 0.75
	s.content_margin_bottom = margin * 0.75
	s.shadow_color = Color(0, 0, 0, 0.35)
	s.shadow_size = 6
	return s

static func make() -> Theme:
	var t := Theme.new()
	t.default_font_size = font_size()
	t.set_stylebox("panel", "PanelContainer", panel_style())
	t.set_stylebox("panel", "Panel", panel_style())
	t.set_color("font_color", "Label", TEXT)
	for state in ["normal", "hover", "pressed", "disabled", "focus"]:
		var s := StyleBoxFlat.new()
		s.set_corner_radius_all(7)
		s.content_margin_left = 12
		s.content_margin_right = 12
		s.content_margin_top = 6
		s.content_margin_bottom = 6
		match state:
			"normal": s.bg_color = Color(0.18, 0.2, 0.24)
			"hover": s.bg_color = Color(0.27, 0.3, 0.36)
			"pressed": s.bg_color = ACCENT.darkened(0.15)
			"disabled": s.bg_color = Color(0.12, 0.12, 0.14)
			"focus":
				s.bg_color = Color(0, 0, 0, 0)
				s.border_color = ACCENT
				s.set_border_width_all(2)
		t.set_stylebox(state, "Button", s)
		t.set_stylebox(state, "OptionButton", s)
	t.set_color("font_color", "Button", TEXT)
	t.set_color("font_hover_color", "Button", Color.WHITE)
	t.set_color("font_disabled_color", "Button", Color(0.45, 0.45, 0.47))
	t.set_color("font_pressed_color", "Button", Color.WHITE)
	var bg := StyleBoxFlat.new()
	bg.bg_color = Color(0.15, 0.16, 0.18)
	bg.set_corner_radius_all(5)
	var fill := StyleBoxFlat.new()
	fill.bg_color = ACCENT
	fill.set_corner_radius_all(5)
	t.set_stylebox("background", "ProgressBar", bg)
	t.set_stylebox("fill", "ProgressBar", fill)
	t.set_color("font_color", "ProgressBar", TEXT)
	var tab_sel := panel_style(BG_LIGHT, 6, 10)
	var tab_un := panel_style(Color(0.1, 0.11, 0.13, 0.9), 6, 10)
	t.set_stylebox("tab_selected", "TabContainer", tab_sel)
	t.set_stylebox("tab_unselected", "TabContainer", tab_un)
	t.set_stylebox("tab_hovered", "TabContainer", tab_sel)
	t.set_stylebox("panel", "TabContainer", panel_style(BG_LIGHT, 8, 12))
	var le := StyleBoxFlat.new()
	le.bg_color = Color(0.12, 0.13, 0.15)
	le.set_corner_radius_all(6)
	le.content_margin_left = 8
	le.content_margin_right = 8
	le.content_margin_top = 4
	le.content_margin_bottom = 4
	t.set_stylebox("normal", "LineEdit", le)
	t.set_color("default_color", "RichTextLabel", TEXT)
	t.set_font_size("normal_font_size", "RichTextLabel", font_size())
	t.set_font_size("bold_font_size", "RichTextLabel", font_size())
	return t

static func label(text: String, size: int = 17, color: Color = TEXT) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", font_size(size))
	l.add_theme_color_override("font_color", color)
	return l

static func button(text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.pressed.connect(cb)
	b.pressed.connect(func(): Audio.play("ui_click", -10.0))
	return b

static func bar(value: float, color: Color, width: float = 160.0) -> ProgressBar:
	var p := ProgressBar.new()
	p.min_value = 0
	p.max_value = 100
	p.value = value
	p.show_percentage = false
	p.custom_minimum_size = Vector2(width, 12)
	var fill := StyleBoxFlat.new()
	fill.bg_color = color
	fill.set_corner_radius_all(5)
	p.add_theme_stylebox_override("fill", fill)
	return p

static func kind_color(kind: String) -> Color:
	match kind:
		"good", "money": return GOOD
		"bad": return BAD
		"quest": return QUEST
		"skill": return WARN
	return TEXT

static func center_panel(parent: Control, size: Vector2) -> PanelContainer:
	var p := PanelContainer.new()
	p.custom_minimum_size = size
	p.set_anchors_preset(Control.PRESET_CENTER)
	p.position = -size * 0.5
	p.grow_horizontal = Control.GROW_DIRECTION_BOTH
	p.grow_vertical = Control.GROW_DIRECTION_BOTH
	parent.add_child(p)
	return p
