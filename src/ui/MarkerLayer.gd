extends Control
## Desenha marcadores de missão sobre o mundo; fora da tela, vira uma seta na borda.

var world: Node

func _process(_d: float) -> void:
	queue_redraw()

func _draw() -> void:
	if world == null or world.camera_rig == null or world.ui.is_modal_open():
		return
	var cam: Camera3D = world.camera_rig.camera
	var vp := get_viewport_rect().size
	var font := get_theme_default_font()
	for m in world.marker_positions():
		var pos: Vector3 = m.pos
		var behind := cam.is_position_behind(pos)
		var sp := cam.unproject_position(pos)
		var margin := 40.0
		var on_screen := not behind and sp.x > margin and sp.x < vp.x - margin and sp.y > margin and sp.y < vp.y - margin
		var col := UITheme.QUEST
		var dist: float = world.player.global_position.distance_to(pos)
		if on_screen:
			var bob := sin(Time.get_ticks_msec() / 300.0) * 4.0
			var c := sp + Vector2(0, bob)
			draw_colored_polygon(PackedVector2Array([c + Vector2(0, -12), c + Vector2(9, 0), c + Vector2(0, 12), c + Vector2(-9, 0)]), col)
			draw_string(font, c + Vector2(14, 6), "%s · %dm" % [m.label, int(dist)], HORIZONTAL_ALIGNMENT_LEFT, -1, 15, col)
		else:
			var center := vp * 0.5
			var dir := (sp - center).normalized()
			if behind:
				dir = -dir
			var edge: Vector2 = center + dir * min(vp.x, vp.y) * 0.45
			var ang := dir.angle()
			var pts := PackedVector2Array([edge + Vector2(16, 0).rotated(ang), edge + Vector2(-8, 9).rotated(ang), edge + Vector2(-8, -9).rotated(ang)])
			draw_colored_polygon(pts, col)
			draw_string(font, edge + Vector2(-30, 26), "%s %dm" % [m.label, int(dist)], HORIZONTAL_ALIGNMENT_LEFT, -1, 14, col)
