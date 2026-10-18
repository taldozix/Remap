extends CenterContainer
## Mapa do bairro desenhado a partir dos dados do mapa (ruas, prédios, você, seu carro
## e marcadores de missão).

var ui: Node
var world: Node
var canvas: Control

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

func open() -> void:
	var panel := PanelContainer.new()
	add_child(panel)
	var v := VBoxContainer.new()
	panel.add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	head.add_child(UITheme.label(tr("Vila Aurora — Porto Serra"), 22, UITheme.ACCENT))
	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(sp)
	head.add_child(UITheme.button(tr("Fechar"), func(): ui.close_current()))
	canvas = Control.new()
	canvas.custom_minimum_size = Vector2(600, 600)
	canvas.draw.connect(_draw_map)
	v.add_child(canvas)
	v.add_child(UITheme.label(tr("▲ você   ■ seu carro   ◆ missão"), 14, UITheme.MUTED))

func _process(_d: float) -> void:
	if canvas:
		canvas.queue_redraw()

func _w2m(p: Vector3, size: Vector2) -> Vector2:
	var b: Array = Data.map().get("bounds", [-85, -85, 85, 85])
	return Vector2((p.x - float(b[0])) / (float(b[2]) - float(b[0])) * size.x, (p.z - float(b[1])) / (float(b[3]) - float(b[1])) * size.y)

func _rect(r: Rect2, size: Vector2) -> Rect2:
	var a := _w2m(Vector3(r.position.x, 0, r.position.y), size)
	var e := _w2m(Vector3(r.end.x, 0, r.end.y), size)
	return Rect2(a, e - a)

func _draw_map() -> void:
	var size := canvas.size
	var font := canvas.get_theme_default_font()
	canvas.draw_rect(Rect2(Vector2.ZERO, size), Color(0.22, 0.32, 0.18))
	var m: Dictionary = Data.map()
	for a in m.get("areas", []):
		if a.has("rect"):
			var r: Array = a.rect
			var col := Color(0.3, 0.42, 0.24) if a.type == "park" else (Color(0.25, 0.25, 0.27) if a.type == "asphalt" else Color(0.6, 0.58, 0.54))
			canvas.draw_rect(_rect(Rect2(float(r[0]), float(r[1]), float(r[2]) - float(r[0]), float(r[3]) - float(r[1])), size), col)
	for r in world.builder.sidewalk_rects:
		canvas.draw_rect(_rect(r, size), Color(0.62, 0.6, 0.56))
	var roads: Dictionary = m.roads
	var hw := float(roads.width) * 0.5
	for z in roads.zs:
		canvas.draw_rect(_rect(Rect2(float(roads.xs[0]) - hw, float(z) - hw, float(roads.xs[-1]) - float(roads.xs[0]) + hw * 2, hw * 2), size), Color(0.17, 0.17, 0.19))
	for x in roads.xs:
		canvas.draw_rect(_rect(Rect2(float(x) - hw, float(roads.zs[0]) - hw, hw * 2, float(roads.zs[-1]) - float(roads.zs[0]) + hw * 2), size), Color(0.17, 0.17, 0.19))
	for id in world.buildings:
		var b: Building = world.buildings[id]
		var rr := _rect(b.rect, size)
		var col := Color(b.def.style.get("roof", "#888888"))
		canvas.draw_rect(rr, col)
		canvas.draw_rect(rr, Color(0, 0, 0, 0.5), false, 1.5)
		var name: String = tr(b.def.name)
		canvas.draw_string(font, rr.position + Vector2(2, rr.size.y + 12), name, HORIZONTAL_ALIGNMENT_LEFT, max(rr.size.x + 60, 90), 11, Color(1, 1, 1, 0.9))
	var car: Node = world.vehicles.get("carro_jogador")
	if car:
		var cp := _w2m(car.global_position, size)
		canvas.draw_rect(Rect2(cp - Vector2(5, 5), Vector2(10, 10)), Color("4f8fe0"))
	for mk in world.marker_positions():
		var mp := _w2m(mk.pos, size)
		canvas.draw_colored_polygon(PackedVector2Array([mp + Vector2(0, -8), mp + Vector2(6, 0), mp + Vector2(0, 8), mp + Vector2(-6, 0)]), UITheme.QUEST)
	var pp := _w2m(world.player.global_position, size)
	var ang: float = -world.player.rotation.y
	var pts := PackedVector2Array([pp + Vector2(0, -9).rotated(ang), pp + Vector2(6, 6).rotated(ang), pp + Vector2(-6, 6).rotated(ang)])
	canvas.draw_colored_polygon(pts, UITheme.ACCENT)
