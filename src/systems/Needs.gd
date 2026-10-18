class_name Needs
extends RefCounted
## Necessidades e condição física do personagem (lógica pura, testável sem cena).
## Valores de 0 (ótimo) a 100 (crítico). Saúde vai de 0 (desmaio) a 100.
## Também simula temperatura corporal (°C), umidade da roupa (chuva) e dor.

const IDS := ["fome", "sede", "sono", "fadiga", "higiene"]
const LABELS := {"fome": "Fome", "sede": "Sede", "sono": "Sono", "fadiga": "Fadiga", "higiene": "Higiene"}

## Variação por hora de jogo conforme a atividade.
const RATES := {
	"fome":    {"default": 4.5, "sleep": 2.0, "run": 6.0, "work": 5.5},
	"sede":    {"default": 6.5, "sleep": 2.5, "run": 12.0, "work": 8.0},
	"sono":    {"default": 4.2, "sleep": -12.5, "rest": 3.0, "work": 5.0},
	"fadiga":  {"default": -15.0, "walk": -6.0, "run": 60.0, "work": 12.0, "rest": -45.0, "sleep": -60.0, "drive": -8.0},
	"higiene": {"default": 1.5, "run": 6.0, "work": 6.0, "sleep": 1.0},
}

const BODY_NORMAL := 36.6

var v: Dictionary = {"fome": 15.0, "sede": 15.0, "sono": 10.0, "fadiga": 0.0, "higiene": 5.0}
var saude: float = 100.0
var nausea_hours: float = 0.0   # comida estragada
var body_temp: float = BODY_NORMAL
var wetness: float = 0.0        # 0 seco … 100 encharcado
var dor: float = 0.0            # 0 sem dor … 100 dor intensa
var fainted: bool = false        # sinaliza ao jogo que o personagem desmaiou
## Modificadores de traços: {"fome": 1.15, "run_fatigue": 0.75, "no_nausea": true, ...}
var mods: Dictionary = {}

func get_need(id: String) -> float:
	return float(v.get(id, 0.0))

func set_need(id: String, value: float) -> void:
	v[id] = clampf(value, 0.0, 100.0)

## Avança a simulação. `fitness` (nível de condicionamento 0-10) reduz o desgaste da corrida.
## `env`: {"ambient": °C, "outdoors": bool, "raining": bool, "warmth": float, "rain_protect": bool}
func tick(hours: float, activity: String, fitness: int = 0, env: Dictionary = {}) -> void:
	if hours <= 0.0:
		return
	for id in IDS:
		var table: Dictionary = RATES[id]
		var rate: float = float(table.get(activity, table["default"]))
		if rate > 0.0:
			rate *= float(mods.get(id, 1.0))
		if id == "fadiga" and activity == "run":
			rate *= (1.0 - 0.05 * fitness) * float(mods.get("run_fatigue", 1.0))
		if id == "sede" and body_temp > 37.5:
			rate *= 1.5
		if id == "fadiga" and body_temp < 35.6 and rate > -1.0:
			rate += 6.0
		set_need(id, get_need(id) + rate * hours)
	_tick_temperature(hours, activity, env)
	dor = max(0.0, dor - (8.0 if activity == "sleep" else 4.0) * hours)
	# Saúde: penalidades graduais por negligência; recuperação lenta quando bem cuidado.
	var dmg := 0.0
	if get_need("fome") >= 100.0:
		dmg += 6.0
	if get_need("sede") >= 100.0:
		dmg += 9.0
	if nausea_hours > 0.0:
		dmg += 3.0
		nausea_hours = max(0.0, nausea_hours - hours)
	if body_temp < 35.0 or body_temp > 38.3:
		dmg += 3.0
	if dmg > 0.0:
		saude -= dmg * hours
	elif get_need("fome") < 70.0 and get_need("sede") < 70.0:
		saude += (6.0 if activity == "sleep" else 2.0) * hours
	saude = clampf(saude, 0.0, 100.0)
	if saude <= 0.0 or get_need("sono") >= 100.0:
		fainted = true

## Temperatura corporal tende a um alvo que depende do clima, da roupa, da chuva
## e do esforço. Roupa molhada esfria; correr no calor esquenta.
func _tick_temperature(hours: float, activity: String, env: Dictionary) -> void:
	if env.is_empty():
		return
	var outdoors: bool = env.get("outdoors", false)
	var raining: bool = env.get("raining", false)
	if outdoors and raining and not env.get("rain_protect", false):
		wetness = min(100.0, wetness + 140.0 * hours)
	else:
		wetness = max(0.0, wetness - (70.0 if not outdoors else 35.0) * hours)
	var ambient := float(env.get("ambient", 24.0))
	var warmth := float(env.get("warmth", 0.0))
	var heat := 0.0
	match activity:
		"run": heat = 1.2
		"work": heat = 0.4
		"sleep": heat = 0.3 if warmth > 0.0 else 0.0
	var balance := (ambient + warmth - 21.0) / 6.0 - wetness / 60.0 + heat
	var target := BODY_NORMAL + clampf(balance, -3.2, 2.2) * 0.5
	body_temp = move_toward(body_temp, target, 1.0 * hours)

## Aplica efeitos de consumo: {"fome": -20, "saude": 10, "nausea": 4, "dor": -50}
func consume(effects: Dictionary) -> void:
	for k in effects:
		if k == "saude":
			saude = clampf(saude + float(effects[k]), 0.0, 100.0)
		elif k == "nausea":
			if not mods.get("no_nausea", false):
				nausea_hours += float(effects[k])
		elif k == "dor":
			add_pain(float(effects[k]))
		elif k == "temperatura":
			body_temp = clampf(body_temp + float(effects[k]), 33.0, 40.0)
		elif v.has(k):
			set_need(k, get_need(k) + float(effects[k]))

func add_pain(amount: float) -> void:
	if amount > 0.0:
		amount *= float(mods.get("dor", 1.0))
	dor = clampf(dor + amount, 0.0, 100.0)

## Multiplicador de velocidade de locomoção (cansaço, sono, sede, dor e frio pesam).
func speed_multiplier() -> float:
	var m := 1.0
	m -= max(0.0, get_need("fadiga") - 60.0) / 100.0 * 0.6
	m -= max(0.0, get_need("sono") - 75.0) / 100.0 * 0.6
	m -= max(0.0, get_need("sede") - 80.0) / 100.0 * 0.5
	m -= max(0.0, dor - 40.0) / 100.0 * 0.5
	if body_temp < 35.3:
		m -= 0.1
	if saude < 30.0:
		m -= 0.2
	return clampf(m, 0.45, 1.0)

## Multiplicador de duração de tarefas (>1 = mais lento). Exaustão e dor atrapalham.
func work_multiplier() -> float:
	var m := 1.0
	m += max(0.0, get_need("fadiga") - 50.0) / 100.0
	m += max(0.0, get_need("sono") - 60.0) / 80.0
	m += max(0.0, get_need("fome") - 75.0) / 100.0
	m += max(0.0, dor - 30.0) / 140.0
	if body_temp < 35.6:
		m += 0.15
	return m

func can_run() -> bool:
	return get_need("fadiga") < 85.0 and saude > 15.0 and dor < 75.0

## "Moodles": só aparecem quando relevantes, para não poluir o HUD.
func moodles() -> Array:
	var out: Array = []
	_add_moodle(out, "fome", [[50, "Com fome"], [75, "Faminto"], [95, "Morrendo de fome"]])
	_add_moodle(out, "sede", [[50, "Com sede"], [75, "Muita sede"], [95, "Desidratado"]])
	_add_moodle(out, "sono", [[60, "Sonolento"], [80, "Muito sono"], [95, "Exausto de sono"]])
	_add_moodle(out, "fadiga", [[50, "Cansado"], [75, "Esgotado"], [90, "No limite"]])
	_add_moodle(out, "higiene", [[50, "Suado"], [70, "Mal cheiroso"], [90, "Fedendo"]])
	if saude < 80.0:
		var lvl := 1 if saude >= 50.0 else (2 if saude >= 25.0 else 3)
		out.append({"id": "saude", "label": ["Machucado", "Ferido", "Em estado grave"][lvl - 1], "level": lvl})
	if dor >= 20.0:
		out.append({"id": "dor", "label": "Dor" if dor < 50.0 else "Dor forte", "level": 1 if dor < 50.0 else 2})
	if body_temp < 35.9:
		out.append({"id": "frio", "label": "Com frio" if body_temp >= 35.3 else "Hipotermia", "level": 1 if body_temp >= 35.3 else 3})
	elif body_temp > 37.5:
		out.append({"id": "calor", "label": "Com calor" if body_temp <= 38.0 else "Superaquecido", "level": 1 if body_temp <= 38.0 else 3})
	if wetness >= 40.0:
		out.append({"id": "molhado", "label": "Molhado" if wetness < 80.0 else "Encharcado", "level": 1})
	if nausea_hours > 0.0:
		out.append({"id": "nausea", "label": "Enjoado", "level": 2})
	return out

func _add_moodle(out: Array, id: String, thresholds: Array) -> void:
	var value := get_need(id)
	var lvl := 0
	var label := ""
	for i in thresholds.size():
		if value >= float(thresholds[i][0]):
			lvl = i + 1
			label = thresholds[i][1]
	if lvl > 0:
		out.append({"id": id, "label": label, "level": lvl})

func to_dict() -> Dictionary:
	return {"v": v.duplicate(), "saude": saude, "nausea": nausea_hours, "body_temp": body_temp, "wetness": wetness, "dor": dor}

func from_dict(d: Dictionary) -> void:
	var src: Dictionary = d.get("v", {})
	for id in IDS:
		v[id] = clampf(float(src.get(id, v[id])), 0.0, 100.0)
	saude = clampf(float(d.get("saude", 100.0)), 0.0, 100.0)
	nausea_hours = float(d.get("nausea", 0.0))
	body_temp = clampf(float(d.get("body_temp", BODY_NORMAL)), 33.0, 40.0)
	wetness = clampf(float(d.get("wetness", 0.0)), 0.0, 100.0)
	dor = clampf(float(d.get("dor", 0.0)), 0.0, 100.0)
	fainted = false
