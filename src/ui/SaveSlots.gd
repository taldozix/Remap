class_name SaveSlots
extends RefCounted
## Lista de espaços de salvamento (usada na pausa e no menu principal).

static func build(saving: bool, on_pick: Callable) -> VBoxContainer:
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	for slot in Saves.SLOTS:
		if saving and slot == "auto":
			continue
		var meta := Saves.read_meta(slot)
		var label := TranslationServer.translate(Saves.SLOT_LABELS[slot])
		if meta.is_empty():
			label += " — " + TranslationServer.translate("vazio")
		else:
			label += " — %s, %s %d %s · R$ %d · %s" % [meta.get("weekday", ""), TranslationServer.translate("Dia"), int(meta.get("day", 1)), meta.get("time", ""), int(meta.get("money", 0)), str(meta.get("saved_at", "")).replace("T", " ")]
		var b := UITheme.button(label, on_pick.bind(slot))
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.disabled = not saving and meta.is_empty()
		v.add_child(b)
	return v
