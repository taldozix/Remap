extends Node
## Salvamento e carregamento em JSON (user://saves). Escrita atômica com backup,
## validação de arquivo corrompido e migração de versões antigas.

const DIR := "user://saves"
const SLOTS := ["auto", "rapido", "slot1", "slot2", "slot3"]
const SLOT_LABELS := {"auto": "Automático", "rapido": "Rápido", "slot1": "Espaço 1", "slot2": "Espaço 2", "slot3": "Espaço 3"}

var last_error: String = ""

func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(DIR)

func path_for(slot: String) -> String:
	return "%s/%s.json" % [DIR, slot]

## Motivo pelo qual não se pode salvar agora (vazio = pode).
func block_reason() -> String:
	if Game.world == null:
		return tr("Nenhuma partida em andamento.")
	if Game.job.on_shift:
		return tr("Não é possível salvar durante um turno de trabalho.")
	if Game.world.has_method("save_block_reason"):
		return Game.world.call("save_block_reason")
	return ""

func save_game(slot: String) -> bool:
	last_error = ""
	var data := Game.to_dict()
	var payload := {
		"meta": {
			"saved_at": Time.get_datetime_string_from_system(false, true),
			"unix": Time.get_unix_time_from_system(),
			"day": Clock.day(),
			"time": Clock.time_string(),
			"weekday": Clock.weekday_name(),
			"money": Game.player.money,
			"version": Game.SAVE_VERSION,
		},
		"data": data,
	}
	var path := path_for(slot)
	var tmp := path + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		last_error = tr("Não foi possível escrever o arquivo (%s).") % error_string(FileAccess.get_open_error())
		return false
	f.store_string(JSON.stringify(payload, "\t"))
	f.close()
	if FileAccess.file_exists(path):
		DirAccess.copy_absolute(path, path + ".bak")
		DirAccess.remove_absolute(path)
	var err := DirAccess.rename_absolute(tmp, path)
	if err != OK:
		last_error = tr("Falha ao finalizar o salvamento (%s).") % error_string(err)
		return false
	return true

func exists(slot: String) -> bool:
	return FileAccess.file_exists(path_for(slot))

func read_meta(slot: String) -> Dictionary:
	var r := _read(path_for(slot))
	return r.get("meta", {}) if r.get("ok", false) else {}

## Lê e valida um salvamento. Tenta o backup se o principal estiver corrompido.
func load_data(slot: String) -> Dictionary:
	last_error = ""
	var r := _read(path_for(slot))
	if not r.get("ok", false):
		var bak := _read(path_for(slot) + ".bak")
		if bak.get("ok", false):
			push_warning("Salvamento corrompido; usando backup.")
			r = bak
		else:
			last_error = r.get("error", tr("Arquivo inválido."))
			return {}
	return r.data

func _read(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {"ok": false, "error": tr("Salvamento não encontrado.")}
	var text := FileAccess.get_file_as_string(path)
	var parsed = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY or typeof(parsed.get("data")) != TYPE_DICTIONARY:
		return {"ok": false, "error": tr("Arquivo de salvamento corrompido.")}
	var data: Dictionary = parsed.data
	var version := int(data.get("version", 0))
	if version > Game.SAVE_VERSION:
		return {"ok": false, "error": tr("Salvamento de uma versão mais nova do jogo.")}
	data = migrate(data, version)
	if not data.has("player") or not data.has("clock"):
		return {"ok": false, "error": tr("Salvamento incompleto.")}
	return {"ok": true, "data": data, "meta": parsed.get("meta", {})}

## Migrações entre versões do formato. Versão 0 = arquivos de testes antigos sem campo version.
func migrate(data: Dictionary, from_version: int) -> Dictionary:
	var d := data
	if from_version < 1:
		if not d.has("property"):
			d["property"] = {}
		if not d.has("law"):
			d["law"] = {}
		d["version"] = 1
	if int(d.get("version", 1)) < 2:
		# v2: dono "owner" nas casas, roupas vestidas, traços, entregas e eventos.
		var pl: Dictionary = d.get("player", {})
		if not pl.has("traits"):
			pl["traits"] = []
		d["player"] = pl
		d["version"] = 2
	return d

## Slot mais recente (para "Continuar").
func latest_slot() -> String:
	var best := ""
	var best_t := -1.0
	for s in SLOTS:
		var m := read_meta(s)
		if not m.is_empty() and float(m.get("unix", 0)) > best_t:
			best_t = float(m.get("unix", 0))
			best = s
	return best

func delete_slot(slot: String) -> void:
	for p in [path_for(slot), path_for(slot) + ".bak"]:
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(p)

## Carrega e troca para a cena do mundo.
func load_and_play(slot: String) -> bool:
	var data := load_data(slot)
	if data.is_empty():
		return false
	Game.from_dict(data)
	Game.pending_load = {"loaded": true}
	get_tree().paused = false
	get_tree().change_scene_to_file("res://src/world/GameWorld.tscn")
	return true

func start_new_game(player_name: String = "", appearance: Dictionary = {}, traits: Array = []) -> void:
	Game.new_game()
	Game.player.apply_traits(traits)
	if player_name.strip_edges() != "":
		Game.player.name = player_name.strip_edges()
	if not appearance.is_empty():
		Game.player.appearance = appearance
	Game.pending_load = {"new": true}
	get_tree().paused = false
	get_tree().change_scene_to_file("res://src/world/GameWorld.tscn")
