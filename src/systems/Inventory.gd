class_name Inventory
extends RefCounted
## Inventário limitado por peso. Usado pelo jogador, contêineres e porta-malas.
## Itens perecíveis guardam "age" (horas) e não se empilham com idades muito diferentes.

var items: Array = []        # [{"id": String, "qty": int, "age": float}]
var capacity: float = 12.0   # kg
var refrigerated: bool = false
var owner_id: String = ""

func _init(p_capacity: float = 12.0, p_owner: String = "") -> void:
	capacity = p_capacity
	owner_id = p_owner

static func item_weight(id: String) -> float:
	return float(Data.item(id).get("weight", 0.1))

static func is_perishable(id: String) -> bool:
	return Data.item(id).has("spoil_hours")

func weight() -> float:
	var w := 0.0
	for s in items:
		w += item_weight(s.id) * int(s.qty)
	return w

func free_weight() -> float:
	return capacity - weight()

func count(id: String) -> int:
	var n := 0
	for s in items:
		if s.id == id:
			n += int(s.qty)
	return n

func has(id: String, qty: int = 1) -> bool:
	return count(id) >= qty

func can_fit(id: String, qty: int = 1, limit: float = -1.0) -> bool:
	var cap := capacity if limit < 0.0 else limit
	return weight() + item_weight(id) * qty <= cap + 0.0001

## Adiciona itens. Com `strict`, respeita a capacidade e retorna quanto coube.
func add(id: String, qty: int = 1, age: float = 0.0, strict: bool = true) -> int:
	if qty <= 0 or not Data.has_item(id):
		return 0
	var fits := qty
	if strict:
		var w := item_weight(id)
		fits = qty if w <= 0.0 else min(qty, int(floor((free_weight() + 0.0001) / w)))
	if fits <= 0:
		return 0
	var merged := false
	for s in items:
		if s.id == id and absf(float(s.get("age", 0.0)) - age) < 6.0:
			s.qty = int(s.qty) + fits
			merged = true
			break
	if not merged:
		items.append({"id": id, "qty": fits, "age": age})
	_changed()
	return fits

## Remove `qty` unidades, começando pelos mais velhos. Retorna a quantidade removida.
func remove(id: String, qty: int = 1) -> int:
	var removed := 0
	var stacks := []
	for s in items:
		if s.id == id:
			stacks.append(s)
	stacks.sort_custom(func(a, b): return float(a.get("age", 0.0)) > float(b.get("age", 0.0)))
	for s in stacks:
		if removed >= qty:
			break
		var take: int = min(int(s.qty), qty - removed)
		s.qty = int(s.qty) - take
		removed += take
	items = items.filter(func(s): return int(s.qty) > 0)
	if removed > 0:
		_changed()
	return removed

## Remove de uma pilha específica e devolve uma cópia do que saiu (com idade).
func take_at(index: int, qty: int = 1) -> Dictionary:
	if index < 0 or index >= items.size():
		return {}
	var s: Dictionary = items[index]
	var take: int = min(qty, int(s.qty))
	var out := {"id": s.id, "qty": take, "age": float(s.get("age", 0.0))}
	s.qty = int(s.qty) - take
	if int(s.qty) <= 0:
		items.remove_at(index)
	_changed()
	return out

## Move uma pilha (ou parte) para outro inventário. Retorna a quantidade movida.
func transfer_to(other: Inventory, index: int, qty: int = 1, strict: bool = true) -> int:
	if index < 0 or index >= items.size():
		return 0
	var s: Dictionary = items[index]
	var want: int = min(qty, int(s.qty))
	var w := item_weight(s.id)
	if strict and w > 0.0:
		want = min(want, int(floor((other.free_weight() + 0.0001) / w)))
	if want <= 0:
		return 0
	var moved := take_at(index, want)
	other.add(moved.id, moved.qty, moved.age, false)
	return int(moved.qty)

## Envelhece perecíveis. Geladeira desacelera 4x.
func tick_age(hours: float) -> void:
	var rate := 0.25 if refrigerated else 1.0
	for s in items:
		if is_perishable(s.id):
			s.age = float(s.get("age", 0.0)) + hours * rate

func is_spoiled(stack: Dictionary) -> bool:
	var spoil := float(Data.item(stack.id).get("spoil_hours", -1.0))
	return spoil > 0.0 and float(stack.get("age", 0.0)) >= spoil

func freshness(stack: Dictionary) -> float:
	var spoil := float(Data.item(stack.id).get("spoil_hours", -1.0))
	if spoil <= 0.0:
		return 1.0
	return clampf(1.0 - float(stack.get("age", 0.0)) / spoil, 0.0, 1.0)

func clear() -> void:
	items.clear()
	_changed()

func remove_where(pred: Callable) -> int:
	var before := items.size()
	items = items.filter(func(s): return not pred.call(s))
	if items.size() != before:
		_changed()
	return before - items.size()

func _changed() -> void:
	if owner_id != "":
		Events.inventory_changed.emit(owner_id)

func to_dict() -> Dictionary:
	return {"items": items.duplicate(true), "capacity": capacity, "refrigerated": refrigerated}

func from_dict(d: Dictionary) -> void:
	items.clear()
	for s in d.get("items", []):
		if typeof(s) == TYPE_DICTIONARY and Data.has_item(str(s.get("id", ""))):
			items.append({"id": str(s.id), "qty": int(s.get("qty", 1)), "age": float(s.get("age", 0.0))})
	capacity = float(d.get("capacity", capacity))
	refrigerated = bool(d.get("refrigerated", refrigerated))
