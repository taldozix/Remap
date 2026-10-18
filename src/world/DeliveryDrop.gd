class_name DeliveryDrop
extends Interactable
## Ponto de entrega ativo (porta do cliente). Some quando a entrega termina.

func setup(p_world: Node, label: String) -> void:
	world = p_world
	title = tr("Entrega: %s") % label
	radius = 2.2
	var mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.6
	cm.bottom_radius = 0.6
	cm.height = 0.04
	mi.mesh = cm
	mi.position.y = 0.14
	mi.material_override = Mat.emissive(Color(0.5, 0.8, 1.0), 1.2)
	add_child(mi)

func get_actions(_player: Node) -> Array:
	var has := Game.player.inventory.has("sacola_entrega")
	return [action("deliver", tr("Deixar a entrega"), has, tr("Você está sem a sacola"))]

func do_action(_player: Node, _id: String) -> void:
	var r := Game.delivery.complete(Clock.total_minutes, Game.player)
	if not r.ok:
		Events.notify.emit(r.reason, "bad")
		return
	Audio.play("cash")
	Events.notify.emit(tr("Entrega concluída: R$ %d%s") % [r.pay, tr(" (no prazo!)") if r.on_time else tr(" (atrasada)")], "good" if r.on_time else "info")
	if r.recipient != "" and world.npcs.has(r.recipient) and world.npcs[r.recipient].visible:
		world.npcs[r.recipient].bark(tr("Obrigado(a)! Chegou certinho.") if r.on_time else tr("Demorou, hein..."))
	world.refresh_delivery()
