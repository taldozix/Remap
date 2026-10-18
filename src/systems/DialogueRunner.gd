class_name DialogueRunner
extends RefCounted
## Executa um diálogo de data/dialogues.json: escolhe o nó de entrada pelo contexto,
## aplica efeitos, filtra opções por condição e resolve testes (persuasão etc.).

var npc_id: String = ""
var dialogue_id: String = ""
var dlg: Dictionary = {}
var node_id = null
var node: Dictionary = {}
var finished := false
var last_check: Dictionary = {}   # resultado do último teste (para a UI)
var rng: RandomNumberGenerator

func _init(p_rng: RandomNumberGenerator = null) -> void:
	rng = p_rng if p_rng else Game.rng

func start(p_npc: String, p_dialogue: String) -> bool:
	npc_id = p_npc
	dialogue_id = p_dialogue
	dlg = Data.dialogue(p_dialogue)
	if dlg.is_empty():
		finished = true
		return false
	finished = false
	for e in dlg.get("entry", []):
		if Rules.check(e.get("if", {}), ctx()):
			_enter(e.node)
			return not finished
	finished = true
	return false

func ctx() -> Dictionary:
	return {"npc": npc_id, "reason": tr(Data.npc(npc_id).get("name", ""))}

func _enter(id) -> void:
	last_check = last_check if id != null else {}
	if id == null or not dlg.get("nodes", {}).has(str(id)):
		node_id = null
		node = {}
		finished = true
		return
	node_id = str(id)
	node = dlg.nodes[node_id]
	if node.has("do"):
		Rules.apply(node.do, ctx())

func speaker_name() -> String:
	if node.get("speaker", "self") == "player":
		return Game.player.name
	return tr(Data.npc(npc_id).get("name", npc_id))

func text() -> String:
	return substitute(tr(str(node.get("text", ""))))

static func substitute(t: String) -> String:
	if not "{" in t:
		return t
	var v: Dictionary = Game.world_state.vehicles.get("carro_jogador", {})
	var subs := {
		"{player}": Game.player.name,
		"{money}": str(Game.player.money),
		"{job_rank}": Game.job.rank_title(),
		"{job_perf}": str(int(Game.job.performance)),
		"{job_orders}": str(Game.job.total_orders),
		"{job_warnings}": str(Game.job.warnings),
		"{repair_cost}": str(int(ceil(float(v.get("damage", 0.0)) * 3))),
	}
	for k in subs:
		t = t.replace(k, subs[k])
	return t

## Opções visíveis. Opções com condição falsa somem, a menos que tenham "show_disabled".
func choices() -> Array:
	var out: Array = []
	var list: Array = node.get("choices", [])
	for i in list.size():
		var c: Dictionary = list[i]
		var ok := Rules.check(c.get("if", {}), ctx())
		if not ok and not c.get("show_disabled", false):
			continue
		var label := substitute(tr(str(c.text)))
		var chance := -1.0
		if c.has("check"):
			chance = Rules.chance(c.check, ctx())
			label = "[%s %d%%] %s" % [tr(c.check.get("label", "Teste")), int(round(chance * 100)), label]
		out.append({"index": i, "text": label, "enabled": ok, "hint": tr(c.get("disabled_hint", "")), "chance": chance})
	return out

func has_choices() -> bool:
	return not choices().is_empty()

## Avança um nó sem escolhas.
func advance() -> void:
	if finished:
		return
	if has_choices():
		return
	_enter(node.get("next", null))

func choose(index: int) -> void:
	if finished:
		return
	var list: Array = node.get("choices", [])
	if index < 0 or index >= list.size():
		return
	var c: Dictionary = list[index]
	if not Rules.check(c.get("if", {}), ctx()):
		return
	if c.has("do"):
		Rules.apply(c.do, ctx())
	if c.has("check"):
		var chance := Rules.chance(c.check, ctx())
		var success := rng.randf() < chance
		last_check = {"label": tr(c.check.get("label", "Teste")), "success": success, "chance": chance}
		_enter(c.success if success else c.fail)
		return
	last_check = {}
	_enter(c.get("next", null))
