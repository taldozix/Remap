extends Node
## Barramento de eventos global. Sistemas emitem; interface e mundo escutam.
## Mantém os sistemas desacoplados (ex.: Needs não conhece o HUD).

signal notify(text: String, kind: String)          # kind: info, good, bad, quest, money, skill
signal money_changed(amount: int, delta: int)
signal inventory_changed(owner_id: String)       # "player" ou id de contêiner
signal needs_changed
signal skill_level_up(skill_id: String, level: int)
signal quest_updated(quest_id: String)
signal flags_changed
signal relation_changed(npc_id: String, value: int)
signal reputation_changed(value: int)
signal job_updated
signal crime_recorded(crime: Dictionary)
signal bark(npc_id: String, text: String)
signal appearance_changed
signal property_changed(building_id: String)
signal situation_started(id: String)

# Pedidos de interface (o mundo pede, a UI decide como mostrar).
signal request_dialogue(npc_id: String, dialogue_id: String)
signal request_container(container_id: String, title: String)
signal request_shop(shop_id: String)
signal request_message(title: String, body: String)
signal request_sleep(bed_id: String, quality: float)
signal dialogue_closed

# Mundo
signal player_vehicle_changed(vehicle: Node)
signal player_building_changed(building_id: String)
signal world_ready
signal game_loaded
