class_name Needs
extends RefCounted
## Necessidades e condição física do personagem (lógica pura, testável sem cena).
## Valores de 0 (ótimo) a 100 (crítico). Saúde vai de 0 (desmaio) a 100.

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

var v: Dictionary = {"fome": 15.0, "sede": 15.0, "sono": 10.0, "fadiga": 0.0, "higiene": 5.0}
var saude: float = 100.0
var nausea_hours: float = 0.0   # comida estragada
var fainted: bool = false        # sinaliza ao jogo que o personagem desmaiou

func get_need(id: String) -> float:
	return float(v.get(id, 0.0))

func set_need(id: String, value: float) -> void:
	v[id] = clampf(value, 0.0, 100.0)

## Avança a simulação. `fitness` (nível de condicionamento 0-10) reduz o desgaste da corrida.
func tick(hours: float, activity: String, fitness: int = 0) -> void:
	if hours <= 0.0:
		return
	for id in IDS:
		var table: Dictionary = RATES[id]
		var rate: float = float(table.get(activity, table["default"]))
		if id == "fadiga" and activity == "run":
			rate *= (1.0 - 0.05 * fitness)
		set_need(id, get_need(id) + rate * hours)
	# Saúde: penalidades graduais por negligência; recuperação lenta quando bem cuidado.
	var dmg := 0.0
	if get_need("fome") >= 100.0:
		dmg += 6.0
	if get_need("sede") >= 100.0:
		dmg += 9.0
	if nausea_hours > 0.0:
		dmg += 3.0
		nausea_hours = max(0.0, nausea_hours - hours)
	if dmg > 0.0:
		saude -= dmg * hours
	elif get_need("fome") < 70.0 and get_need("sede") < 70.0:
		saude += (6.0 if activity == "sleep" else 2.0) * hours
	saude = clampf(saude, 0.0, 100.0)
	if saude <= 0.0 or get_need("sono") >= 100.0:
		fainted = true

## Aplica efeitos de consumo: {"fome": -20, "saude": 10, "nausea": 4}
func consume(effects: Dictionary) -> void:
	for k in effects:
		if k == "saude":
			saude = clampf(saude + float(effects[k]), 0.0, 100.0)
		elif k == "nausea":
			nausea_hours += float(effects[k])
		elif v.has(k):
			set_need(k, get_need(k) + float(effects[k]))

## Multiplicador de velocidade de locomoção (cansaço, sono e sede pesam).
func speed_multiplier() -> float:
	var m := 1.0
	m -= max(0.0, get_need("fadiga") - 60.0) / 100.0 * 0.6
	m -= max(0.0, get_need("sono") - 75.0) / 100.0 * 0.6
	m -= max(0.0, get_need("sede") - 80.0) / 100.0 * 0.5
	if saude < 30.0:
		m -= 0.2
	return clampf(m, 0.45, 1.0)

## Multiplicador de duração de tarefas (>1 = mais lento). Estar exausto atrapalha o trabalho.
func work_multiplier() -> float:
	var m := 1.0
	m += max(0.0, get_need("fadiga") - 50.0) / 100.0
	m += max(0.0, get_need("sono") - 60.0) / 80.0
	m += max(0.0, get_need("fome") - 75.0) / 100.0
	return m

func can_run() -> bool:
	return get_need("fadiga") < 85.0 and saude > 15.0

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
	return {"v": v.duplicate(), "saude": saude, "nausea": nausea_hours}

func from_dict(d: Dictionary) -> void:
	var src: Dictionary = d.get("v", {})
	for id in IDS:
		v[id] = clampf(float(src.get(id, v[id])), 0.0, 100.0)
	saude = clampf(float(d.get("saude", 100.0)), 0.0, 100.0)
	nausea_hours = float(d.get("nausea", 0.0))
	fainted = false
