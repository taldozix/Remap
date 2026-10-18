class_name CameraRig
extends Node3D
## Câmera em três-quartos (isométrica em perspectiva). Segue o jogador ou o veículo,
## gira em passos de 45° e tem zoom. A direção de visão alimenta o corte de paredes.

var target: Node3D = null
var camera: Camera3D
var yaw := deg_to_rad(-35.0)
var _yaw_goal := yaw
var pitch := deg_to_rad(-52.0)
var distance := 21.0
var _dist_goal := 21.0
const MIN_DIST := 9.0
const MAX_DIST := 42.0

func _ready() -> void:
	camera = Camera3D.new()
	camera.fov = 34.0
	camera.far = 260.0
	camera.near = 0.3
	add_child(camera)
	camera.current = true

func _unhandled_input(event: InputEvent) -> void:
	var sens := float(Settings.get_value("game", "camera_sensitivity"))
	if event.is_action_pressed("cam_left"):
		_yaw_goal += deg_to_rad(45.0)
	elif event.is_action_pressed("cam_right"):
		_yaw_goal -= deg_to_rad(45.0)
	elif event.is_action_pressed("zoom_in"):
		_dist_goal = clampf(_dist_goal - 2.0 * sens, MIN_DIST, MAX_DIST)
	elif event.is_action_pressed("zoom_out"):
		_dist_goal = clampf(_dist_goal + 2.0 * sens, MIN_DIST, MAX_DIST)

func _process(delta: float) -> void:
	if target == null or not is_instance_valid(target):
		return
	var sens := float(Settings.get_value("game", "camera_sensitivity"))
	yaw = lerp_angle(yaw, _yaw_goal, min(1.0, delta * 6.0 * sens))
	distance = lerpf(distance, _dist_goal, min(1.0, delta * 6.0))
	var focus := target.global_position + Vector3(0, 1.0, 0)
	if target is Vehicle:
		focus += -target.global_transform.basis.z * clampf(target.speed * 0.35, -3.0, 6.0)
	global_position = global_position.lerp(focus, min(1.0, delta * 7.0)) if global_position.distance_to(focus) < 30.0 else focus
	var offset := Basis(Vector3.UP, yaw) * (Basis(Vector3.RIGHT, pitch) * Vector3(0, 0, distance))
	camera.global_position = global_position + offset
	camera.look_at(global_position, Vector3.UP)
	Nature.set_focus(camera.global_position, global_position + Vector3(0, 0.2, 0))

func snap() -> void:
	if target:
		global_position = target.global_position + Vector3(0, 1.0, 0)
		yaw = _yaw_goal
		distance = _dist_goal
		_process(0.0)

## Direção de visão horizontal (da câmera para a cena).
func view_dir() -> Vector3:
	var d := -camera.global_transform.basis.z
	d.y = 0
	return d.normalized()

## Yaw usado para movimento relativo à câmera (W = "para cima" na tela).
func move_yaw() -> float:
	return yaw
