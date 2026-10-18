class_name LawSystem
extends RefCounted
## Crimes e consequências. Sem testemunhas, o crime fica "sem autoria" (mas a vítima
## percebe o prejuízo). Com testemunhas: multa no dia seguinte, perda de reputação,
## memórias negativas e mudanças de comportamento dos NPCs.

const CRIMES := {
	"furto": {"label": "Furto", "fine": 90, "rep": -12, "rel_victim": -35, "rel_witness": -15},
	"invasao": {"label": "Invasão de domicílio", "fine": 60, "rep": -8, "rel_victim": -25, "rel_witness": -8},
	"atropelamento": {"label": "Atropelamento", "fine": 150, "rep": -15, "rel_victim": -40, "rel_witness": -15},
	"omissao_socorro": {"label": "Omissão de socorro", "fine": 120, "rep": -10, "rel_victim": -20, "rel_witness": -10},
	"ameaca": {"label": "Ameaça", "fine": 0, "rep": -6, "rel_victim": -30, "rel_witness": -10},
	"furto_veiculo": {"label": "Furto de veículo", "fine": 250, "rep": -18, "rel_victim": -50, "rel_witness": -15},
	"batida": {"label": "Colisão de trânsito", "fine": 40, "rep": -2, "rel_victim": -10, "rel_witness": 0},
}

var record: Array = []        # [{"type", "day", "victim", "witnesses", "fine"}]
var pending_fines: int = 0
var pending_reasons: Array = []
var unsolved: Array = []      # crimes sem testemunha: [{"type", "victim", "day"}]

## Registra um crime. `witnesses` = NPCs que viram (a vítima pode estar entre eles).
func report(type: String, victim: String, witnesses: Array, day: int, relations: Relations) -> Dictionary:
	var def: Dictionary = CRIMES.get(type, {})
	if def.is_empty():
		return {}
	var entry := {"type": type, "day": day, "victim": victim, "witnesses": witnesses.duplicate(), "fine": 0}
	if witnesses.is_empty():
		unsolved.append({"type": type, "victim": victim, "day": day})
		entry["solved"] = false
	else:
		entry["solved"] = true
		var fine := int(def.fine)
		entry.fine = fine
		if fine > 0:
			pending_fines += fine
			pending_reasons.append(def.label)
		relations.change_reputation(int(def.rep))
		for w in witnesses:
			relations.add_memory(w, "viu_" + type, day)
			if w != victim:
				relations.change(w, int(def.rel_witness))
		if victim != "":
			relations.change(victim, int(def.rel_victim))
			relations.add_memory(victim, "vitima_" + type, day)
	record.append(entry)
	Events.crime_recorded.emit(entry)
	return entry

## Remove a parte da multa de um incidente quando o jogador repara o erro (ex.: prestou socorro).
func mitigate(type: String, amount: int) -> void:
	pending_fines = max(0, pending_fines - amount)
	if pending_fines == 0:
		pending_reasons.clear()

func was_seen(type: String, victim: String) -> bool:
	for e in record:
		if e.type == type and e.victim == victim and e.get("solved", false):
			return true
	return false

## Chamado na virada do dia: chega a notificação e a multa é cobrada (pode gerar dívida).
func collect_fines() -> Dictionary:
	if pending_fines <= 0:
		return {}
	var out := {"amount": pending_fines, "reasons": pending_reasons.duplicate()}
	pending_fines = 0
	pending_reasons.clear()
	return out

func to_dict() -> Dictionary:
	return {"record": record.duplicate(true), "pending": pending_fines, "reasons": pending_reasons.duplicate(), "unsolved": unsolved.duplicate(true)}

func from_dict(d: Dictionary) -> void:
	record = d.get("record", []).duplicate(true)
	pending_fines = int(d.get("pending", 0))
	pending_reasons = d.get("reasons", []).duplicate()
	unsolved = d.get("unsolved", []).duplicate(true)
