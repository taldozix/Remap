class_name DayNight
extends Node3D
## Ciclo de dia e noite + clima (céu limpo, nublado, chuva). Controla sol, céu,
## luz ambiente, neblina, postes, janelas acesas, partículas de chuva e piso molhado.

var env: Environment
var sun: DirectionalLight3D
var sky_mat: ProceduralSkyMaterial
var rain: CPUParticles3D
var world: Node
var streetlights: Array = []
var buildings: Array = []
var _wet := 0.0
var _rng := RandomNumberGenerator.new()
var _rain_amount := 0.0

func setup(p_world: Node) -> void:
	world = p_world
	_rng.randomize()
	var we := WorldEnvironment.new()
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky_mat = ProceduralSkyMaterial.new()
	sky.sky_material = sky_mat
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_sky_contribution = 0.35
	env.ambient_light_color = Color(0.86, 0.86, 0.82)
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_exposure = 0.92
	env.fog_enabled = true
	env.fog_light_color = Color(0.7, 0.75, 0.82)
	env.fog_density = 0.0025
	env.fog_sky_affect = 0.2
	env.adjustment_enabled = true
	env.adjustment_saturation = 0.98
	env.adjustment_contrast = 1.04
	we.environment = env
	add_child(we)
	sun = DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.directional_shadow_max_distance = 70.0
	sun.shadow_bias = 0.04
	sun.shadow_blur = 1.2
	add_child(sun)
	rain = CPUParticles3D.new()
	rain.amount = 1400
	rain.lifetime = 0.9
	rain.emitting = false
	rain.emission_shape = CPUParticles3D.EMISSION_SHAPE_BOX
	rain.emission_box_extents = Vector3(26, 1, 26)
	rain.direction = Vector3(0.15, -1, 0.05)
	rain.spread = 3.0
	rain.initial_velocity_min = 22.0
	rain.initial_velocity_max = 26.0
	rain.gravity = Vector3(0, -9.8, 0)
	var drop := BoxMesh.new()
	drop.size = Vector3(0.02, 0.5, 0.02)
	rain.mesh = drop
	var rm := StandardMaterial3D.new()
	rm.albedo_color = Color(0.75, 0.82, 0.95, 0.45)
	rm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	rm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	rain.material_override = rm
	add_child(rain)
	apply_settings()
	Settings.changed.connect(func(_s): apply_settings())
	Clock.hour_changed.connect(_on_hour)

func apply_settings() -> void:
	var fx: int = Settings.get_value("graphics", "effects")
	var shadows: int = Settings.get_value("graphics", "shadows")
	env.ssao_enabled = fx >= 1
	env.ssao_radius = 1.2
	env.ssao_intensity = 1.6
	env.glow_enabled = fx >= 1
	env.glow_intensity = 0.5
	env.glow_bloom = 0.05
	env.ssr_enabled = fx >= 2
	env.sdfgi_enabled = false
	sun.shadow_enabled = shadows > 0
	sun.directional_shadow_max_distance = [40.0, 55.0, 75.0, 110.0][shadows]
	rain.amount = [500, 1000, 1600][fx]
	var vd: int = Settings.get_value("graphics", "view_distance")
	env.fog_density = [0.006, 0.0035, 0.002][vd]

## Clima muda a cada poucas horas, com regras simples (mais chuva à tarde).
func _on_hour(hour: int) -> void:
	var w: Dictionary = Game.world_state.weather
	if Clock.total_minutes < float(w.get("until", 0.0)):
		return
	var r := _rng.randf()
	var chance_rain := 0.22 if hour >= 13 and hour <= 19 else 0.1
	var state := "limpo"
	if r < chance_rain:
		state = "chuva"
	elif r < chance_rain + 0.25:
		state = "nublado"
	if state != w.get("state", "limpo"):
		if state == "chuva":
			Events.notify.emit(tr("Começou a chover."), "info")
		elif w.get("state", "") == "chuva":
			Events.notify.emit(tr("A chuva parou."), "info")
	Game.world_state.weather = {"state": state, "until": Clock.total_minutes + _rng.randf_range(90.0, 240.0)}

func _process(delta: float) -> void:
	var h := Clock.hour_f()
	# Altura do sol: nasce às 6h, se põe às 18h30.
	var day_t := clampf((h - 6.0) / 12.5, 0.0, 1.0)
	var elev := sin(day_t * PI)
	var is_day := h >= 6.0 and h <= 18.5
	var weather: String = Game.world_state.weather.get("state", "limpo")
	var cloud := 0.0
	if weather == "nublado":
		cloud = 0.45
	elif weather == "chuva":
		cloud = 0.8
	_rain_amount = move_toward(_rain_amount, 1.0 if weather == "chuva" else 0.0, delta * 0.3)
	# Sol: alto e branco no meio do dia (sombras curtas), baixo e dourado só
	# perto do nascer (6h) e do pôr do sol (18h30) — como na referência de arte.
	var sun_angle := lerpf(80.0, 250.0, day_t)
	sun.rotation_degrees = Vector3(-max(7.0, elev * 66.0) if is_day else -40.0, sun_angle, 0)
	var golden := smoothstep(0.42, 0.06, elev) if is_day else 0.0
	golden *= (1.0 - cloud)
	var sun_col := Color(1.0, 0.98, 0.95).lerp(Color(1.0, 0.64, 0.34), golden)
	var night := 0.0
	if not is_day:
		night = 1.0
	elif elev < 0.2:
		night = 1.0 - elev / 0.2
	night = clampf(night, 0.0, 1.0)
	if is_day:
		sun.light_color = sun_col
		sun.light_energy = lerpf(0.2, 1.05, clampf(elev * 2.2, 0.0, 1.0)) * (1.0 - cloud * 0.72)
	else:
		sun.light_color = Color(0.55, 0.65, 0.95)
		sun.light_energy = 0.22
	# Céu encoberto: sombras difusas e quase sem contorno.
	sun.shadow_opacity = 1.0 - cloud * 0.8
	var top_day := Color(0.27, 0.5, 0.86).lerp(Color(0.48, 0.52, 0.58), cloud)
	var hor_day := Color(0.72, 0.82, 0.93).lerp(Color(0.66, 0.68, 0.71), cloud).lerp(Color(1.0, 0.68, 0.45), golden * 0.8)
	sky_mat.sky_top_color = top_day.lerp(Color(0.05, 0.07, 0.16), night)
	sky_mat.sky_horizon_color = hor_day.lerp(Color(0.16, 0.18, 0.28), night)
	sky_mat.ground_horizon_color = sky_mat.sky_horizon_color
	sky_mat.ground_bottom_color = Color(0.15, 0.17, 0.2).lerp(Color(0.02, 0.03, 0.05), night)
	env.ambient_light_energy = lerpf(0.5 + cloud * 0.25 - golden * 0.08, 0.3, night)
	env.ambient_light_color = Color(0.86, 0.86, 0.82).lerp(Color(0.5, 0.58, 0.85), night)
	env.fog_light_color = sky_mat.sky_horizon_color.lerp(Color(1.0, 0.78, 0.55), golden * 0.5 * (1.0 - night))
	env.fog_sun_scatter = 0.25 * golden
	# Gradação de cor da câmera acompanha a hora e o clima.
	if world.postfx:
		var hl := Color(1.0, 1.0, 1.0).lerp(Color(1.08, 1.0, 0.85), golden)
		var sh := Color(1.0, 1.0, 1.0).lerp(Color(0.86, 0.95, 1.04), golden)
		var sat := 0.93 + golden * 0.08
		hl = hl.lerp(Color(0.95, 0.98, 1.06), night * 0.8)
		sh = sh.lerp(Color(0.85, 0.92, 1.1), night * 0.8)
		sat = lerpf(sat, 0.85, night * 0.6)
		hl = hl.lerp(Color(0.97, 0.99, 1.02), cloud)
		sh = sh.lerp(Color(0.95, 0.98, 1.02), cloud)
		sat = lerpf(sat, 0.8, cloud)
		world.postfx.set_mood(hl, sh, sat)
	Mat.set_night(night)
	var lamps_on := night > 0.35 or (cloud > 0.7 and night > 0.1)
	for l in streetlights:
		l.visible = lamps_on
	for b in buildings:
		b.set_night(night)
	# Chuva
	rain.emitting = _rain_amount > 0.2
	if world.camera_rig:
		rain.global_position = world.camera_rig.global_position + Vector3(0, 14, 0)
	_wet = move_toward(_wet, 1.0 if weather == "chuva" else 0.0, delta * (0.08 if weather == "chuva" else 0.02))
	Mat.set_wetness(_wet)
	Nature.set_wetness(_wet)
	Audio.set_ambient("rain", _rain_amount > 0.2, -14.0 + _rain_amount * 6.0)
