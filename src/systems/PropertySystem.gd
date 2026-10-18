class_name PropertySystem
extends RefCounted
## Imóveis do jogador: aluguel (casa nº 8), compra e venda (data/properties.json),
## IPTU semanal, dívida e acesso. Ter um imóvel dá chave das portas, uso de cama,
## chuveiro, fogão e armazenamento sem contar como furto.

var props: Dictionary = {}  # building_id -> {"status": "alugada"/"propria", "rent": int, "tax": int, "debt": int, "paid": int}

func init_default() -> void:
	props = {"casa_jogador": {"status": "alugada", "rent": int(Data.property("casa_jogador").get("rent", 120)), "tax": 0, "debt": 0, "paid": 0}}

func owns_or_rents(building_id: String) -> bool:
	return props.has(building_id)

func owns(building_id: String) -> bool:
	return props.has(building_id) and props[building_id].status == "propria"

func is_for_sale(building_id: String) -> bool:
	return Data.property(building_id).has("price") and not props.has(building_id)

func price(building_id: String) -> int:
	return int(Data.property(building_id).get("price", 0))

func sale_value(building_id: String) -> int:
	var p: Dictionary = props.get(building_id, {})
	return int(float(p.get("paid", price(building_id))) * float(Data.property(building_id).get("sale_rate", 0.8)))

func owned_homes() -> Array:
	var out: Array = []
	for id in props:
		if Data.property(id).get("type", "") == "casa":
			out.append(id)
	return out

func buy(building_id: String, player: PlayerState) -> Dictionary:
	if not is_for_sale(building_id):
		return {"ok": false, "reason": tr("Este imóvel não está à venda.")}
	var p := price(building_id)
	if total_debt() > 0:
		return {"ok": false, "reason": tr("Quite suas dívidas antes de comprar um imóvel.")}
	if not player.spend(p, "Compra de imóvel"):
		return {"ok": false, "reason": tr("Dinheiro insuficiente (R$ %d).") % p}
	props[building_id] = {"status": "propria", "rent": 0, "tax": int(Data.property(building_id).get("tax", 0)), "debt": 0, "paid": p}
	Events.property_changed.emit(building_id)
	Events.notify.emit(tr("Você comprou: %s!") % tr(Data.property(building_id).name), "good")
	return {"ok": true}

func sell(building_id: String, player: PlayerState) -> Dictionary:
	if not owns(building_id):
		return {"ok": false, "reason": tr("Você não é dono deste imóvel.")}
	if owned_homes().size() <= 1 and Data.property(building_id).get("type", "") == "casa":
		return {"ok": false, "reason": tr("Você precisa de outro lugar para morar antes de vender.")}
	var value := sale_value(building_id)
	props.erase(building_id)
	player.add_money(value, "Venda de imóvel")
	Events.property_changed.emit(building_id)
	return {"ok": true, "value": value}

## Encerrar o aluguel só é permitido com outra casa e sem dívida.
func end_lease(building_id: String) -> Dictionary:
	if not props.has(building_id) or props[building_id].status != "alugada":
		return {"ok": false, "reason": tr("Não há contrato de aluguel aqui.")}
	if int(props[building_id].debt) > 0:
		return {"ok": false, "reason": tr("Pague o aluguel atrasado antes de sair.")}
	if owned_homes().size() <= 1:
		return {"ok": false, "reason": tr("Você precisa de outra casa antes de entregar esta.")}
	props.erase(building_id)
	Events.property_changed.emit(building_id)
	return {"ok": true}

## Cobrança semanal no domingo: aluguel e IPTU. Sem dinheiro, vira dívida.
func charge_rent(player: PlayerState) -> Array:
	var msgs: Array = []
	for id in props:
		var p: Dictionary = props[id]
		var due := int(p.get("rent", 0)) + int(p.get("tax", 0)) + int(p.get("debt", 0))
		if due <= 0:
			continue
		var what := tr("Aluguel") if p.status == "alugada" else tr("IPTU")
		if player.money >= due:
			player.add_money(-due, what)
			p.debt = 0
			msgs.append(tr("%s pago (%s): R$ %d.") % [what, tr(Data.property(id).get("name", id)), due])
		else:
			var paid: int = max(0, player.money)
			if paid > 0:
				player.add_money(-paid, what + tr(" (parcial)"))
			p.debt = due - paid
			msgs.append(tr("%s atrasado! Dívida: R$ %d.") % [what, p.debt])
	return msgs

func total_debt() -> int:
	var d := 0
	for id in props:
		d += int(props[id].get("debt", 0))
	return d

func to_dict() -> Dictionary:
	return {"props": props.duplicate(true)}

func from_dict(d: Dictionary) -> void:
	var src: Dictionary = d.get("props", {})
	if src.is_empty():
		init_default()
		return
	props = {}
	for id in src:
		props[id] = src[id].duplicate()
		for k in ["tax", "debt", "paid", "rent"]:
			if not props[id].has(k):
				props[id][k] = 0
