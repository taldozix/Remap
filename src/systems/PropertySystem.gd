class_name PropertySystem
extends RefCounted
## Imóveis do jogador. No protótipo: a casa alugada (aluguel semanal, dívida e acesso).
## A estrutura já comporta compra/venda de vários imóveis (status "propria").

var props: Dictionary = {}  # building_id -> {"status": "alugada"/"propria", "rent": int, "debt": int}

func init_default() -> void:
	props = {"casa_jogador": {"status": "alugada", "rent": 120, "debt": 0}}

func owns_or_rents(building_id: String) -> bool:
	return props.has(building_id)

## Cobrança semanal no domingo. Sem dinheiro, vira dívida (e o senhorio reclama).
func charge_rent(player: PlayerState) -> Array:
	var msgs: Array = []
	for id in props:
		var p: Dictionary = props[id]
		if p.status != "alugada":
			continue
		var due := int(p.rent) + int(p.debt)
		if player.money >= due:
			player.add_money(-due, "Aluguel")
			p.debt = 0
			msgs.append(tr("Aluguel pago: R$ %d.") % due)
		else:
			var paid: int = max(0, player.money)
			if paid > 0:
				player.add_money(-paid, "Aluguel (parcial)")
			p.debt = due - paid
			msgs.append(tr("Aluguel atrasado! Dívida com o senhorio: R$ %d.") % p.debt)
	return msgs

func total_debt() -> int:
	var d := 0
	for id in props:
		d += int(props[id].get("debt", 0))
	return d

func to_dict() -> Dictionary:
	return {"props": props.duplicate(true)}

func from_dict(d: Dictionary) -> void:
	init_default()
	var src: Dictionary = d.get("props", {})
	for id in src:
		props[id] = src[id].duplicate()
