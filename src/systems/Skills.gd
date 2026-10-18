class_name Skills
extends RefCounted
## Habilidades que evoluem com a prática. Cada nível tem efeito mecânico real
## (velocidade na cozinha, chance de persuasão, controle do carro, fôlego).

const DEFS := {
	"culinaria": {"name": "Culinária", "desc": "Prepara lanches mais rápido e com menos erros."},
	"social": {"name": "Social", "desc": "Aumenta a chance de persuadir e as gorjetas."},
	"direcao": {"name": "Direção", "desc": "Melhora o controle do veículo e reduz danos em batidas."},
	"condicionamento": {"name": "Condicionamento", "desc": "Cansa menos ao correr e corre mais rápido."},
}
const THRESHOLDS := [0, 50, 130, 250, 420, 650, 950, 1350, 1850, 2500, 3300]

var xp: Dictionary = {}

func level(id: String) -> int:
	var x := float(xp.get(id, 0.0))
	var lvl := 0
	for i in THRESHOLDS.size():
		if x >= THRESHOLDS[i]:
			lvl = i
	return lvl

func progress(id: String) -> float:
	var lvl := level(id)
	if lvl >= THRESHOLDS.size() - 1:
		return 1.0
	var x := float(xp.get(id, 0.0))
	return (x - THRESHOLDS[lvl]) / float(THRESHOLDS[lvl + 1] - THRESHOLDS[lvl])

func add_xp(id: String, amount: float) -> void:
	if not DEFS.has(id) or amount <= 0.0:
		return
	var before := level(id)
	xp[id] = float(xp.get(id, 0.0)) + amount
	var after := level(id)
	if after > before:
		Events.skill_level_up.emit(id, after)
		Events.notify.emit(tr("%s subiu para o nível %d!") % [tr(DEFS[id].name), after], "skill")

func to_dict() -> Dictionary:
	return {"xp": xp.duplicate()}

func from_dict(d: Dictionary) -> void:
	xp = {}
	var src: Dictionary = d.get("xp", {})
	for k in src:
		if DEFS.has(k):
			xp[k] = float(src[k])
