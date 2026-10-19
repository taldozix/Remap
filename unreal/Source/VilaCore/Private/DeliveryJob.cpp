#include "Vila/DeliveryJob.h"

#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Inventory.h"
#include "Vila/PlayerState.h"
#include "Vila/Relations.h"
#include "Vila/Skills.h"

namespace vila {

const Json& DeliveryJob::cfg() const { return data().deliveries(); }

void DeliveryJob::hire() {
	hired = true;
	events().notify.emit(tr("Agora você faz entregas para o Mercadinho. Pegue as sacolas no balcão."), "good");
}

Json DeliveryJob::can_take() const {
	if (!hired) return {{"ok", false}, {"reason", tr("Fale com o Seu Antônio sobre entregas.")}};
	if (!active.empty()) return {{"ok", false}, {"reason", tr("Termine a entrega atual primeiro.")}};
	if (done_today >= jint(cfg(), "max_per_day", 6)) return {{"ok", false}, {"reason", tr("Sem mais entregas hoje.")}};
	return {{"ok", true}};
}

Json DeliveryJob::take(double now, Rng& rng, Inventory& inv) {
	Json c = can_take();
	if (!jbool(c, "ok")) return c;
	const Json& dests = jarr(cfg(), "destinations");
	if (dests.empty()) return {{"ok", false}, {"reason", "?"}};  // evita divisão por zero
	int idx = static_cast<int>(rng.randi() % dests.size());
	if (idx == last_dest) idx = (idx + 1) % static_cast<int>(dests.size());
	last_dest = idx;
	const Json& d = dests[static_cast<size_t>(idx)];
	active = {{"dest", idx}, {"spot", jget(d, "spot")}, {"recipient", jstr(d, "recipient", "")}, {"label", jget(d, "label")},
		{"deadline", now + jnum(cfg(), "deadline_minutes", 75)}, {"taken", now}};
	inv.add("sacola_entrega", 1, 0.0, false);
	events().notify.emit(fmt(tr("Entrega para %s. Prazo: %d minutos."), {jget(d, "label"), jint(cfg(), "deadline_minutes", 75)}), "quest");
	events().quest_updated.emit("entrega");
	return {{"ok", true}, {"delivery", active}};
}

Json DeliveryJob::complete(double now, PlayerState& player) {
	if (active.empty()) return {{"ok", false}, {"reason", tr("Nenhuma entrega ativa.")}};
	if (!player.inventory.has("sacola_entrega")) return {{"ok", false}, {"reason", tr("Você está sem a sacola da entrega.")}};
	player.inventory.remove("sacola_entrega", 1);
	const bool on_time = now <= jnum(active, "deadline");
	int pay = jint(cfg(), "pay_base", 9);
	if (on_time) {
		pay += jint(cfg(), "pay_on_time_tip", 5) + player.skills.level("social");
	} else {
		pay = static_cast<int>(pay * 0.6);
		late_total += 1;
	}
	player.add_money(pay, "Entrega do Mercadinho");
	player.skills.add_xp("social", 3);
	player.skills.add_xp("condicionamento", 2);
	const std::string rec = jstr(active, "recipient", "");
	if (rec != "") game().relations.change(rec, on_time ? 2 : 0);
	done_today += 1;
	total += 1;
	Json out = {{"ok", true}, {"pay", pay}, {"on_time", on_time}, {"recipient", rec}};
	active = Json::object();
	events().quest_updated.emit("entrega");
	return out;
}

// Entregas esquecidas por muito tempo são canceladas (e a sacola volta ao mercadinho).
void DeliveryJob::tick(double /*minutes*/, double now) {
	if (active.empty() || now <= jnum(active, "deadline") + 120.0) return;
	active = Json::object();
	game().player.inventory.remove("sacola_entrega", 1);
	game().relations.change("antonio", -4);
	late_total += 1;
	events().notify.emit(tr("A entrega foi cancelada por atraso. Seu Antônio não gostou."), "bad");
	events().quest_updated.emit("entrega");
}

void DeliveryJob::new_day() { done_today = 0; }

double DeliveryJob::time_left(double now) const { return jnum(active, "deadline", now) - now; }

Json DeliveryJob::to_dict() const {
	return {{"hired", hired}, {"active", active}, {"done_today", done_today}, {"total", total}, {"late", late_total}};
}

void DeliveryJob::from_dict(const Json& d) {
	hired = jbool(d, "hired", false);
	active = jobj(d, "active");
	done_today = jint(d, "done_today", 0);
	total = jint(d, "total", 0);
	late_total = jint(d, "late", 0);
}

}  // namespace vila
