extends Node
## Ferramenta de desenvolvimento: inicia uma partida e salva capturas de tela em
## user://screens (ou no diretório passado em --screens=<dir>). Rodar com janela (Xvfb).

var world: Node
var out_dir := "user://screens"

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--screens="):
			out_dir = a.substr(10)
	DirAccess.make_dir_recursive_absolute(out_dir)
	Game.new_game()
	Game.pending_load = {"new": true}
	world = load("res://src/world/GameWorld.tscn").instantiate()
	add_child(world)
	await get_tree().create_timer(2.0).timeout
	world.ui.close_all()
	await _shot("01_casa_interior")
	world.player.global_position = Vector3(-44, 0.05, -8)
	await _shot("02_rua_casas")
	Clock.set_time(1, 9, 0)
	Clock.advance(1)
	world.player.global_position = Vector3(-30, 0.05, 12)
	await _wait(3.0)
	await _shot("03_praca_manha")
	world.player.global_position = Vector3(22, 0.05, -10)
	await _shot("04_mercadinho")
	world.player.global_position = Vector3(22, 0.05, -18)
	await _shot("05_mercadinho_dentro")
	world.player.global_position = Vector3(30, 0.05, 18)
	Clock.set_time(1, 12, 0)
	Clock.advance(1)
	await _wait(2.0)
	await _shot("06_brasa")
	world.player.global_position = Vector3(28, 0.05, 25)
	await _shot("07_brasa_cozinha")
	world.player.global_position = Vector3(-2, 0.05, -10)
	await _shot("08_cruzamento")
	Clock.set_time(1, 21, 0)
	Clock.advance(1)
	world.player.global_position = Vector3(-14, 0.05, -4)
	await _wait(2.0)
	await _shot("09_noite")
	Game.world_state.weather = {"state": "chuva", "until": Clock.total_minutes + 300}
	Clock.set_time(2, 15, 0)
	Clock.advance(1)
	world.player.global_position = Vector3(-20, 0.05, -6)
	await _wait(4.0)
	await _shot("10_chuva")
	world.ui.open_panel("inventory", load("res://src/ui/InventoryPanel.gd").new())
	await _shot("11_inventario")
	world.ui.close_current()
	world.ui.open_dialogue(world.npcs["lucia"])
	await _wait(2.5)
	await _shot("12_dialogo")
	world.ui.close_current()
	get_tree().quit()

func _wait(t: float) -> void:
	await get_tree().create_timer(t).timeout

func _shot(name: String) -> void:
	world.camera_rig.snap()
	await _wait(0.6)
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	img.save_png("%s/%s.png" % [out_dir, name])
	print("captura: ", name)
