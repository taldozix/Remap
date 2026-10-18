extends Node
## Gerenciador de áudio. Cada som tem um id; se existir um arquivo em
## res://assets/audio/<id>.ogg|.wav ele é usado, senão o som é sintetizado
## proceduralmente (recursos 100% originais, sem licenças de terceiros).

const MIX_RATE := 22050
const BUSES := ["Music", "SFX", "Voice", "Ambient"]

var _cache: Dictionary = {}
var _pool2d: Array[AudioStreamPlayer] = []
var _pool3d: Array[AudioStreamPlayer3D] = []
var _music: AudioStreamPlayer
var _ambient: Dictionary = {}  # id -> AudioStreamPlayer
var _rng := RandomNumberGenerator.new()

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	for b in BUSES:
		if AudioServer.get_bus_index(b) < 0:
			AudioServer.add_bus()
			var idx := AudioServer.bus_count - 1
			AudioServer.set_bus_name(idx, b)
			AudioServer.set_bus_send(idx, "Master")
	Settings.apply_audio()
	for i in 10:
		var p := AudioStreamPlayer.new()
		p.bus = "SFX"
		add_child(p)
		_pool2d.append(p)
	_music = AudioStreamPlayer.new()
	_music.bus = "Music"
	add_child(_music)

# ------------------------------------------------------------------ API
func stream(id: String) -> AudioStream:
	if _cache.has(id):
		return _cache[id]
	var s: AudioStream = null
	for ext in ["ogg", "wav", "mp3"]:
		var path := "res://assets/audio/%s.%s" % [id, ext]
		if ResourceLoader.exists(path):
			s = load(path)
			break
	if s == null:
		s = _synth(id)
	_cache[id] = s
	return s

func play(id: String, volume_db: float = 0.0, pitch: float = 1.0, bus: String = "SFX") -> void:
	if DisplayServer.get_name() == "headless":
		return
	for p in _pool2d:
		if not p.playing:
			p.stream = stream(id)
			p.volume_db = volume_db
			p.pitch_scale = pitch
			p.bus = bus
			p.play()
			return

## Som posicional (passos, portas, buzinas). Usa um pool de players 3D na árvore da cena atual.
func play_at(id: String, pos: Vector3, volume_db: float = 0.0, pitch: float = 1.0) -> void:
	if DisplayServer.get_name() == "headless":
		return
	var scene := get_tree().current_scene
	if scene == null or not scene is Node3D:
		play(id, volume_db, pitch)
		return
	var player: AudioStreamPlayer3D = null
	for p in _pool3d:
		if is_instance_valid(p) and not p.playing and p.get_parent() == scene:
			player = p
			break
	if player == null:
		_pool3d = _pool3d.filter(func(p): return is_instance_valid(p))
		if _pool3d.size() >= 24:
			return
		player = AudioStreamPlayer3D.new()
		player.bus = "SFX"
		player.max_distance = 45.0
		player.unit_size = 6.0
		scene.add_child(player)
		_pool3d.append(player)
	player.global_position = pos
	player.stream = stream(id)
	player.volume_db = volume_db
	player.pitch_scale = pitch
	player.play()

func play_music(id: String) -> void:
	if DisplayServer.get_name() == "headless":
		return
	var s := stream(id)
	if _music.stream == s and _music.playing:
		return
	_music.stream = s
	_music.play()

func stop_music() -> void:
	_music.stop()

func set_ambient(id: String, on: bool, volume_db: float = -8.0) -> void:
	if DisplayServer.get_name() == "headless":
		return
	if on:
		if not _ambient.has(id):
			var p := AudioStreamPlayer.new()
			p.bus = "Ambient"
			p.stream = stream(id)
			add_child(p)
			_ambient[id] = p
		var a: AudioStreamPlayer = _ambient[id]
		a.volume_db = volume_db
		if not a.playing:
			a.play()
	elif _ambient.has(id):
		_ambient[id].stop()

func stop_all_ambient() -> void:
	for id in _ambient:
		_ambient[id].stop()

# ------------------------------------------------------------------ síntese
func _synth(id: String) -> AudioStream:
	_rng.seed = hash(id)
	match id:
		"step": return _make(_noise_burst(0.07, 0.35, 2200.0), false)
		"door_open": return _make(_creak(0.45), false)
		"door_close": return _make(_thump(0.25, 85.0), false)
		"door_locked": return _make(_rattle(0.25), false)
		"pickup": return _make(_sweep(0.12, 700.0, 1300.0, 0.35), false)
		"drop": return _make(_thump(0.15, 140.0), false)
		"cash": return _make(_mix([_tone(0.5, 1568.0, 0.25, 6.0), _delay(_tone(0.5, 2093.0, 0.22, 6.0), 0.09)]), false)
		"notify": return _make(_mix([_tone(0.45, 660.0, 0.2, 5.0), _delay(_tone(0.4, 990.0, 0.18, 6.0), 0.08)]), false)
		"bad": return _make(_square(0.28, 196.0, 0.18), false)
		"order": return _make(_mix([_tone(0.15, 1046.0, 0.3, 10.0), _delay(_tone(0.15, 1046.0, 0.3, 10.0), 0.2)]), false)
		"crash": return _make(_noise_burst(0.7, 0.9, 900.0), false)
		"horn": return _make(_mix([_square(0.5, 392.0, 0.16), _square(0.5, 494.0, 0.14)]), false)
		"ui_click": return _make(_noise_burst(0.025, 0.25, 4000.0), false)
		"eat": return _make(_crunch(0.5), false)
		"drink": return _make(_gulp(0.5), false)
		"engine": return _make(_engine(0.5), true)
		"sizzle": return _make(_loop_noise(1.5, 6000.0, 0.12, true), true)
		"city": return _make(_loop_noise(4.0, 350.0, 0.35, false), true)
		"rain": return _make(_loop_noise(2.0, 5000.0, 0.22, true), true)
		"music_menu": return _make(_pad_music(), true)
	return _make(_tone(0.1, 440.0, 0.2, 10.0), false)

func _make(samples: PackedFloat32Array, loop: bool) -> AudioStreamWAV:
	var bytes := PackedByteArray()
	bytes.resize(samples.size() * 2)
	for i in samples.size():
		bytes.encode_s16(i * 2, int(clampf(samples[i], -1.0, 1.0) * 32000.0))
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = MIX_RATE
	w.stereo = false
	w.data = bytes
	if loop:
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_begin = 0
		w.loop_end = samples.size()
	return w

func _n(seconds: float) -> int:
	return int(seconds * MIX_RATE)

func _tone(sec: float, freq: float, amp: float, decay: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	for i in out.size():
		var t := float(i) / MIX_RATE
		out[i] = sin(TAU * freq * t) * amp * exp(-decay * t) * min(1.0, t * 200.0)
	return out

func _square(sec: float, freq: float, amp: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	for i in out.size():
		var t := float(i) / MIX_RATE
		var env: float = min(1.0, t * 80.0) * min(1.0, (sec - t) * 30.0)
		out[i] = (1.0 if fmod(t * freq, 1.0) < 0.5 else -1.0) * amp * env
	return out

func _sweep(sec: float, f0: float, f1: float, amp: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	var phase := 0.0
	for i in out.size():
		var t := float(i) / MIX_RATE
		phase += TAU * lerpf(f0, f1, t / sec) / MIX_RATE
		out[i] = sin(phase) * amp * (1.0 - t / sec)
	return out

func _noise_burst(sec: float, amp: float, cutoff: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	var a := clampf(TAU * cutoff / MIX_RATE, 0.0, 1.0)
	var y := 0.0
	for i in out.size():
		var t := float(i) / MIX_RATE
		y += a * (_rng.randf_range(-1.0, 1.0) - y)
		out[i] = y * amp * pow(1.0 - t / sec, 2.0) * 2.0
	return out

func _thump(sec: float, freq: float) -> PackedFloat32Array:
	return _mix([_tone(sec, freq, 0.6, 14.0), _noise_burst(sec * 0.5, 0.3, 1500.0)])

func _creak(sec: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	var phase := 0.0
	for i in out.size():
		var t := float(i) / MIX_RATE
		var f := 260.0 + 140.0 * sin(t * 9.0) + 80.0 * t
		phase += TAU * f / MIX_RATE
		var saw := fmod(phase / TAU, 1.0) * 2.0 - 1.0
		out[i] = saw * 0.12 * sin(PI * t / sec)
	return out

func _rattle(sec: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	for i in out.size():
		var t := float(i) / MIX_RATE
		var pulse: float = 1.0 if fmod(t, 0.06) < 0.012 else 0.0
		out[i] = _rng.randf_range(-1.0, 1.0) * 0.35 * pulse
	return out

func _crunch(sec: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	for i in out.size():
		var t := float(i) / MIX_RATE
		var gate: float = 1.0 if fmod(t, 0.16) < 0.05 else 0.0
		out[i] = _rng.randf_range(-1.0, 1.0) * 0.25 * gate
	return out

func _gulp(sec: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	for i in out.size():
		var t := float(i) / MIX_RATE
		var local := fmod(t, 0.18)
		out[i] = sin(TAU * (180.0 + 400.0 * local) * t) * 0.3 * exp(-local * 25.0)
	return out

func _engine(sec: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(_n(sec))
	for i in out.size():
		var t := float(i) / MIX_RATE
		var s := 0.0
		for h in [1, 2, 3, 4, 6]:
			s += sin(TAU * 60.0 * h * t) / float(h)
		var putt := 0.6 + 0.4 * sin(TAU * 30.0 * t)
		out[i] = s * 0.18 * putt
	return out

func _loop_noise(sec: float, cutoff: float, amp: float, highpass: bool) -> PackedFloat32Array:
	var n := _n(sec)
	var fade := _n(0.1)
	var raw := PackedFloat32Array()
	raw.resize(n + fade)
	var a := clampf(TAU * cutoff / MIX_RATE, 0.0, 1.0)
	var y := 0.0
	for i in raw.size():
		var x := _rng.randf_range(-1.0, 1.0)
		y += a * (x - y)
		raw[i] = (x - y) if highpass else y * 3.0
	var out := PackedFloat32Array()
	out.resize(n)
	for i in n:
		out[i] = raw[i] * amp
	for i in fade:
		var k := float(i) / fade
		out[i] = (raw[i] * k + raw[n + i] * (1.0 - k)) * amp
	return out

func _pad_music() -> PackedFloat32Array:
	# Progressão original Lá menor – Fá – Dó – Sol, timbre suave (4 s por acorde).
	var chords := [[220.0, 261.63, 329.63], [174.61, 220.0, 261.63], [261.63, 329.63, 392.0], [196.0, 246.94, 293.66]]
	var per := 4.0
	var out := PackedFloat32Array()
	out.resize(_n(per * chords.size()))
	for i in out.size():
		var t := float(i) / MIX_RATE
		var ci := int(t / per)
		var lt := t - ci * per
		var env: float = min(1.0, lt / 1.2) * min(1.0, (per - lt) / 1.0)
		var s := 0.0
		for f in chords[ci]:
			s += sin(TAU * f * t) * 0.5 + sin(TAU * f * 2.0 * t) * 0.12
		var bass: float = sin(TAU * chords[ci][0] * 0.5 * t) * 0.35
		var arp_f: float = chords[ci][int(lt * 2.0) % 3] * 2.0
		var arp := sin(TAU * arp_f * t) * 0.18 * exp(-fmod(lt, 0.5) * 6.0)
		out[i] = (s * 0.09 + bass * 0.25 + arp) * env * 0.8
	return out

func _mix(parts: Array) -> PackedFloat32Array:
	var n := 0
	for p in parts:
		n = max(n, p.size())
	var out := PackedFloat32Array()
	out.resize(n)
	for p in parts:
		for i in p.size():
			out[i] += p[i]
	return out

func _delay(p: PackedFloat32Array, sec: float) -> PackedFloat32Array:
	var d := _n(sec)
	var out := PackedFloat32Array()
	out.resize(p.size() + d)
	for i in p.size():
		out[i + d] = p[i]
	return out
