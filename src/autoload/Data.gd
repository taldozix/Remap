extends Node
## Banco de dados do jogo. Todo conteúdo (itens, NPCs, diálogos, missões, lojas,
## veículos, mapa) vem de arquivos JSON em res://data — fácil de expandir e traduzir.

const FILES := {
	"items": "res://data/items.json",
	"npcs": "res://data/npcs.json",
	"dialogues": "res://data/dialogues.json",
	"quests": "res://data/quests.json",
	"shops": "res://data/shops.json",
	"vehicles": "res://data/vehicles.json",
	"jobs": "res://data/jobs.json",
	"chatter": "res://data/chatter.json",
	"map": "res://data/map/vila_aurora.json",
}

var db: Dictionary = {}
var errors: Array[String] = []

func _ready() -> void:
	reload()

func reload() -> void:
	errors.clear()
	for key in FILES:
		db[key] = _load_json(FILES[key])
	_validate()
	for e in errors:
		push_warning("[Data] " + e)

func _load_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		errors.append("Arquivo ausente: " + path)
		return {}
	var text := FileAccess.get_file_as_string(path)
	var parsed = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		errors.append("JSON inválido: " + path)
		return {}
	return parsed

## Checagem de referências cruzadas: evita itens/diálogos inexistentes em tempo de execução.
func _validate() -> void:
	var items: Dictionary = db.get("items", {})
	for shop_id in db.get("shops", {}):
		for item_id in db.shops[shop_id].get("sells", {}):
			if not items.has(item_id):
				errors.append("Loja %s vende item inexistente %s" % [shop_id, item_id])
	for npc_id in db.get("npcs", {}):
		var dlg: String = db.npcs[npc_id].get("dialogue", "")
		if dlg != "" and not db.get("dialogues", {}).has(dlg):
			errors.append("NPC %s usa diálogo inexistente %s" % [npc_id, dlg])

func item(id: String) -> Dictionary:
	return db.get("items", {}).get(id, {})

func has_item(id: String) -> bool:
	return db.get("items", {}).has(id)

func item_name(id: String) -> String:
	return tr(item(id).get("name", id))

func npc(id: String) -> Dictionary:
	return db.get("npcs", {}).get(id, {})

func npcs() -> Dictionary:
	return db.get("npcs", {})

func dialogue(id: String) -> Dictionary:
	return db.get("dialogues", {}).get(id, {})

func quest(id: String) -> Dictionary:
	return db.get("quests", {}).get(id, {})

func quests() -> Dictionary:
	return db.get("quests", {})

func shop(id: String) -> Dictionary:
	return db.get("shops", {}).get(id, {})

func shops() -> Dictionary:
	return db.get("shops", {})

func vehicle(id: String) -> Dictionary:
	return db.get("vehicles", {}).get(id, {})

func job(id: String) -> Dictionary:
	return db.get("jobs", {}).get(id, {})

func chatter() -> Dictionary:
	return db.get("chatter", {})

func map() -> Dictionary:
	return db.get("map", {})
