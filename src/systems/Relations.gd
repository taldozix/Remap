class_name Relations
extends RefCounted
## Relacionamentos individuais (-100..100), memórias de NPCs sobre o jogador
## e reputação no bairro. Diálogos, preços e reações consultam estes dados.

var rel: Dictionary = {}          # npc_id -> int
var memories: Dictionary = {}     # npc_id -> [{"tag": String, "day": int}]
var reputation: int = 0           # reputação na Vila Aurora
var greeted_day: Dictionary = {}  # npc_id -> último dia em que cumprimentou

func get_rel(npc_id: String) -> int:
	return int(rel.get(npc_id, int(Data.npc(npc_id).get("start_relation", 0))))

func change(npc_id: String, delta: int) -> void:
	if delta == 0:
		return
	rel[npc_id] = clampi(get_rel(npc_id) + delta, -100, 100)
	Events.relation_changed.emit(npc_id, rel[npc_id])

func change_reputation(delta: int) -> void:
	if delta == 0:
		return
	reputation = clampi(reputation + delta, -100, 100)
	Events.reputation_changed.emit(reputation)

func add_memory(npc_id: String, tag: String, day: int) -> void:
	if not memories.has(npc_id):
		memories[npc_id] = []
	for m in memories[npc_id]:
		if m.tag == tag:
			m.day = day
			return
	memories[npc_id].append({"tag": tag, "day": day})

func has_memory(npc_id: String, tag: String) -> bool:
	for m in memories.get(npc_id, []):
		if m.tag == tag:
			return true
	return false

func attitude(npc_id: String) -> String:
	var r := get_rel(npc_id)
	if r <= -40: return "hostil"
	if r <= -10: return "desconfiado"
	if r < 20: return "neutro"
	if r < 50: return "amigável"
	return "amigo"

static func reputation_label(value: int) -> String:
	if value <= -40: return "Mal-visto"
	if value <= -10: return "Suspeito"
	if value < 15: return "Desconhecido"
	if value < 40: return "Bem-visto"
	return "Querido pelo bairro"

func to_dict() -> Dictionary:
	return {"rel": rel.duplicate(), "memories": memories.duplicate(true), "reputation": reputation, "greeted": greeted_day.duplicate()}

func from_dict(d: Dictionary) -> void:
	rel = {}
	for k in d.get("rel", {}):
		rel[k] = int(d.rel[k])
	memories = d.get("memories", {}).duplicate(true)
	reputation = int(d.get("reputation", 0))
	greeted_day = {}
	for k in d.get("greeted", {}):
		greeted_day[k] = int(d.greeted[k])
