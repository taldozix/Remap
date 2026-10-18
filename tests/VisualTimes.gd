extends Node
## Ferramenta de arte: captura o mesmo lugar em vários horários e climas para
## conferir que a iluminação faz sentido o dia todo (ver docs/DIRECAO_ARTE.md).
##   godot --path . res://tests/VisualTimes.tscn -- --screens=<pasta>

var world: Node
var out_dir := "user://screens"
const MOMENTS := [
	["06h45_amanhecer", 6, 45, "limpo"], ["09h00_manha", 9, 0, "limpo"], ["12h30_meio_dia", 12, 30, "limpo"],
	["15h00_tarde", 15, 0, "limpo"], ["17h50_entardecer", 17, 50, "limpo"], ["21h00_noite", 21, 0, "limpo"],
	["13h00_nublado", 13, 0, "nublado"], ["13h00_chuva", 13, 0, "chuva"],
]

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--screens="):
			out_dir = a.substr(10)
	DirAccess.make_dir_recursive_absolute(out_dir)
	Game.new_game()
	world = load("res://src/world/GameWorld.tscn").instantiate()
	add_child(world)
	while not world.ready_done:
		await get_tree().physics_frame
	world.ui.close_all()
	world.player.global_position = Vector3(-40, 0.05, 25)
	var day := 2
	for m in MOMENTS:
		Game.world_state.weather = {"state": m[3], "until": 1e9}
		Clock.set_time(day, m[1], m[2])
		day += 1
		world.camera_rig.snap()
		await get_tree().create_timer(4.0 if m[3] == "chuva" else 1.0).timeout
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s/t_%s.png" % [out_dir, m[0]])
		print("captura: ", m[0])
	get_tree().quit()
