extends Node
## Ferramenta de desenvolvimento da direção de arte: monta uma vitrine na rua
## (frota de carros, personagens, fachada e interior) e salva capturas em vários
## horários e climas. Rodar com janela (Xvfb): ... tests/ArtShowcase.tscn -- --screens=<dir>

var world: Node
var out_dir := "user://screens"
var only := ""

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--screens="):
			out_dir = a.substr(10)
		if a.begins_with("--only="):
			only = a.substr(7)
	DirAccess.make_dir_recursive_absolute(out_dir)
	Game.new_game()
	Game.pending_load = {"new": true}
	Game.director.auto_enabled = false
	world = load("res://src/world/GameWorld.tscn").instantiate()
	add_child(world)
	await get_tree().create_timer(2.0).timeout
	world.ui.close_all()
	world.ui.visible = false
	# Frota de vitrine estacionada junto à calçada (só visual).
	var kinds := ["hatch", "sedan", "pickup", "van"]
	var sizes := {"hatch": [1.75, 1.35, 3.9], "sedan": [1.8, 1.4, 4.4], "pickup": [1.9, 1.6, 4.9], "van": [1.95, 2.0, 4.8]}
	var colors := [Color("c94c4c"), Color("e8e4d8"), Color("3b5b8c"), Color("d9a441")]
	for i in kinds.size():
		var cv := CarVisual.new()
		world.add_child(cv)
		cv.build(kinds[i], sizes[kinds[i]], colors[i])
		cv.position = Vector3(8.0 + i * 6.2, 0.0, 3.0)
		cv.rotation_degrees.y = 90.0 if i % 2 == 0 else -90.0
	world.camera_rig._dist_goal = 11.0
	await _scene("frota_manha", Vector3(18, 0.05, 6.2), 1, 10, 0, "limpo")
	await _scene("frota_meio_dia", Vector3(18, 0.05, 6.2), 1, 12, 30, "limpo")
	await _scene("frota_tarde", Vector3(18, 0.05, 6.2), 1, 17, 30, "limpo")
	await _scene("frota_chuva", Vector3(18, 0.05, 6.2), 2, 14, 0, "chuva")
	await _scene("frota_noite", Vector3(18, 0.05, 6.2), 2, 21, 0, "limpo")
	world.camera_rig._dist_goal = 4.5
	await _scene("personagem_perto", Vector3(18, 0.05, 6.2), 3, 10, 0, "limpo")
	world.camera_rig._dist_goal = 6.0
	await _scene("carro_perto", Vector3(14, 0.05, 6.2), 3, 11, 0, "limpo")
	world.camera_rig._dist_goal = 16.0
	await _scene("rua_casas", Vector3(-42, 0.05, -8), 3, 10, 30, "limpo")
	await _scene("casa_interior", Vector3(-44.0, 0.05, -18.6), 3, 10, 30, "limpo")
	await _scene("mercadinho", Vector3(22, 0.05, -10), 3, 11, 30, "limpo")
	await _scene("praca", Vector3(-30, 0.05, 12), 3, 16, 30, "nublado")
	get_tree().quit()

func _scene(name: String, pos: Vector3, day: int, h: int, m: int, weather: String) -> void:
	if only != "" and not name.begins_with(only):
		return
	Game.world_state.weather = {"state": weather, "until": Clock.total_minutes + 600}
	Clock.set_time(day, h, m)
	Clock.advance(1)
	world.player.global_position = pos
	await _wait(1.5)
	world.camera_rig.snap()
	await _wait(0.8)
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [out_dir, name])
	print("captura: ", name)

func _wait(t: float) -> void:
	await get_tree().create_timer(t).timeout
