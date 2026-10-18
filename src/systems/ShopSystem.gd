class_name ShopSystem
extends RefCounted
## Comércio: estoque por loja, preços afetados por relacionamento e reputação,
## horário de funcionamento e presença do atendente (verificada pelo mundo).

var stock: Dictionary = {}   # shop_id -> {item_id: qty}
var last_restock_day: int = 0

func init_from_data() -> void:
	stock = {}
	for shop_id in Data.shops():
		stock[shop_id] = Data.shop(shop_id).get("sells", {}).duplicate()
		for k in stock[shop_id]:
			stock[shop_id][k] = int(stock[shop_id][k])

func restock(day: int) -> void:
	if day == last_restock_day:
		return
	last_restock_day = day
	for shop_id in Data.shops():
		var base: Dictionary = Data.shop(shop_id).get("sells", {})
		for k in base:
			stock[shop_id][k] = max(int(stock[shop_id].get(k, 0)), int(base[k]))

func is_open(shop_id: String, hour_f: float) -> bool:
	var h: Array = Data.shop(shop_id).get("hours", [0, 24])
	return Clock.is_hour_in(hour_f, float(h[0]), float(h[1]))

## Desconto de "freguês": relacionamento com o dono e reputação no bairro.
func discount(shop_id: String, relations: Relations) -> float:
	var owner: String = Data.shop(shop_id).get("owner", "")
	var d := 0.0
	if owner != "" and relations.get_rel(owner) >= 30:
		d += 0.10
	if relations.reputation >= 25:
		d += 0.05
	return d

## Quem tem reputação muito baixa (ou foi visto roubando o dono) não é atendido.
func refuses_service(shop_id: String, relations: Relations) -> String:
	var owner: String = Data.shop(shop_id).get("owner", "")
	if owner != "" and relations.get_rel(owner) <= -40:
		return tr("%s se recusa a atender você.") % tr(Data.npc(owner).get("name", owner))
	if relations.reputation <= -30:
		return tr("\"Aqui não atendemos gente como você.\"")
	return ""

func price_buy(shop_id: String, item_id: String, relations: Relations) -> int:
	var base := float(Data.item(item_id).get("value", 1))
	var markup := float(Data.shop(shop_id).get("markup", 1.0))
	return max(1, int(round(base * markup * (1.0 - discount(shop_id, relations)))))

func price_sell(shop_id: String, item_id: String) -> int:
	var rate := float(Data.shop(shop_id).get("buy_rate", 0.4))
	return max(0, int(floor(float(Data.item(item_id).get("value", 0)) * rate)))

func buys_item(shop_id: String, item_id: String) -> bool:
	var it := Data.item(item_id)
	if it.get("no_sell", false):
		return false
	return Data.shop(shop_id).get("buys", []).has(it.get("category", ""))

func buy(shop_id: String, item_id: String, qty: int, player: PlayerState, relations: Relations) -> Dictionary:
	var available := int(stock.get(shop_id, {}).get(item_id, 0))
	if available < qty:
		return {"ok": false, "reason": tr("Sem estoque.")}
	var total := price_buy(shop_id, item_id, relations) * qty
	if not player.can_afford(total):
		return {"ok": false, "reason": tr("Dinheiro insuficiente.")}
	if not player.inventory.can_fit(item_id, qty, player.capacity() * 1.5):
		return {"ok": false, "reason": tr("Você não consegue carregar mais peso.")}
	player.spend(total, Data.shop(shop_id).get("name", shop_id))
	player.inventory.add(item_id, qty, 0.0, false)
	stock[shop_id][item_id] = available - qty
	return {"ok": true, "total": total}

func sell(shop_id: String, index: int, qty: int, player: PlayerState) -> Dictionary:
	if index < 0 or index >= player.inventory.items.size():
		return {"ok": false, "reason": "?"}
	var stack: Dictionary = player.inventory.items[index]
	if not buys_item(shop_id, stack.id):
		return {"ok": false, "reason": tr("Esta loja não compra isso.")}
	if player.is_equipped(stack.id) and player.inventory.count(stack.id) <= qty:
		return {"ok": false, "reason": tr("Desequipe o item antes de vender.")}
	if player.inventory.is_spoiled(stack):
		return {"ok": false, "reason": tr("Ninguém compra comida estragada.")}
	var item_id: String = stack.id
	var moved := player.inventory.take_at(index, qty)
	var total := price_sell(shop_id, item_id) * int(moved.qty)
	player.add_money(total, Data.shop(shop_id).get("name", shop_id))
	if not stock.has(shop_id):
		stock[shop_id] = {}
	stock[shop_id][item_id] = int(stock[shop_id].get(item_id, 0)) + int(moved.qty)
	return {"ok": true, "total": total}

## Compras feitas por NPCs consomem o estoque de verdade.
func npc_purchase(shop_id: String, rng: RandomNumberGenerator) -> String:
	var s: Dictionary = stock.get(shop_id, {})
	var options: Array = []
	for k in s:
		if int(s[k]) > 0 and Data.item(k).get("category", "") in ["comida", "bebida"]:
			options.append(k)
	if options.is_empty():
		return ""
	var pick: String = options[rng.randi() % options.size()]
	s[pick] = int(s[pick]) - 1
	return pick

func to_dict() -> Dictionary:
	return {"stock": stock.duplicate(true), "last_restock_day": last_restock_day}

func from_dict(d: Dictionary) -> void:
	init_from_data()
	var src: Dictionary = d.get("stock", {})
	for shop_id in src:
		if stock.has(shop_id):
			for k in src[shop_id]:
				if Data.has_item(k):
					stock[shop_id][k] = int(src[shop_id][k])
	last_restock_day = int(d.get("last_restock_day", 0))
