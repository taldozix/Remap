class_name QuestSystem
extends RefCounted
## Missões orientadas a dados (data/quests.json). Cada etapa tem objetivo, marcadores
## e regras de avanço avaliadas quando o estado do jogo muda.

var states: Dictionary = {}  # id -> {"state", "stage", "outcome", "log": []}

func state_of(id: String) -> String:
	return str(states.get(id, {}).get("state", "not_started"))

func stage_of(id: String) -> String:
	return str(states.get(id, {}).get("stage", ""))

func outcome_of(id: String) -> String:
	return str(states.get(id, {}).get("outcome", ""))

func start(id: String) -> bool:
	var q := Data.quest(id)
	if q.is_empty() or state_of(id) != "not_started":
		return false
	states[id] = {"state": "active", "stage": str(q.get("start_stage", "")), "outcome": "", "log": []}
	_log(id, tr("Missão iniciada."))
	Events.notify.emit(tr("Nova missão: %s") % tr(q.get("title", id)), "quest")
	Events.quest_updated.emit(id)
	check_advances()
	return true

func set_stage(id: String, stage: String) -> void:
	if state_of(id) != "active" or stage_of(id) == stage:
		return
	states[id].stage = stage
	var obj: String = Data.quest(id).get("stages", {}).get(stage, {}).get("objective", "")
	_log(id, tr(obj))
	Events.notify.emit(tr("Objetivo atualizado: %s") % tr(obj), "quest")
	Events.quest_updated.emit(id)

func complete(id: String, outcome: String) -> void:
	if state_of(id) != "active":
		return
	states[id].state = "completed"
	states[id].outcome = outcome
	var text: String = Data.quest(id).get("outcomes", {}).get(outcome, "")
	if text != "":
		_log(id, tr(text))
	Events.notify.emit(tr("Missão concluída: %s") % tr(Data.quest(id).get("title", id)), "quest")
	Events.quest_updated.emit(id)

func fail(id: String, reason: String) -> void:
	if state_of(id) != "active":
		return
	states[id].state = "failed"
	_log(id, tr(reason))
	Events.quest_updated.emit(id)

func objective(id: String) -> String:
	if state_of(id) != "active":
		return ""
	return tr(Data.quest(id).get("stages", {}).get(stage_of(id), {}).get("objective", ""))

## Marcadores ativos ("npc:marcos", "pos:x,z") de todas as missões em andamento.
func active_markers() -> Array:
	var out: Array = []
	for id in states:
		if state_of(id) == "active":
			for m in Data.quest(id).get("stages", {}).get(stage_of(id), {}).get("markers", []):
				out.append(m)
	return out

func check_advances() -> void:
	for id in states.keys():
		if state_of(id) != "active":
			continue
		var stage: Dictionary = Data.quest(id).get("stages", {}).get(stage_of(id), {})
		for rule in stage.get("advance", []):
			if Rules.check(rule.get("if", {}), {}):
				if rule.has("to"):
					set_stage(id, str(rule.to))
				if rule.has("do"):
					Rules.apply(rule.do, {})
				break

func _log(id: String, text: String) -> void:
	if text == "":
		return
	states[id].log.append({"day": Clock.day(), "time": Clock.time_string(), "text": text})

func to_dict() -> Dictionary:
	return {"states": states.duplicate(true)}

func from_dict(d: Dictionary) -> void:
	states = {}
	var src: Dictionary = d.get("states", {})
	for id in src:
		if not Data.quest(id).is_empty():
			states[id] = src[id].duplicate(true)
