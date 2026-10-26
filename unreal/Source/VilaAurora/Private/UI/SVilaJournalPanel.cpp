#include "UI/SVilaJournalPanel.h"

#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Game.h"
#include "Vila/LawSystem.h"
#include "Vila/Needs.h"
#include "Vila/Relations.h"
#include "Vila/Skills.h"

using namespace VilaConv;
using VilaUI::F;
using VilaUI::T;

namespace
{
	// Coluna de conteúdo de uma aba.
	struct FCol
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		void Add(const TSharedRef<SWidget>& W, float Gap = 4.f) { V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, Gap))[W]; }
		void Wrap(const FString& Text, int32 Size = 15, const FLinearColor& Color = VilaUI::TextColor()) { Add(VilaUI::Label(Text, Size, Color, 820.f)); }
		void Head(const FString& Text) { Add(VilaUI::Label(Text, 18, VilaUI::Accent())); }
		void Row(const FString& Label, float Value01, const FLinearColor& Color, const FString& Text = FString())
		{
			TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(170.f)[VilaUI::Label(Label, 15)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					VilaUI::Bar(TOptional<float>(FMath::Clamp(Value01, 0.f, 1.f)), FSlateColor(Color), 300.f)
				];
			if (!Text.IsEmpty()) H->AddSlot().AutoWidth().VAlign(VAlign_Center)[VilaUI::Label(TEXT("  ") + Text, 14, VilaUI::Muted())];
			Add(H);
		}
	};
}

void SVilaJournalPanel::Construct(const FArguments& InArgs)
{
	TSharedRef<SVilaTabs> Tabs = SNew(SVilaTabs).ContentHeight(440.f);
	Tabs->AddTab(T("Missões"), Quests());
	Tabs->AddTab(T("Personagem"), Character());
	Tabs->AddTab(T("Habilidades"), Skills());
	Tabs->AddTab(T("Relações"), Relations());
	Tabs->AddTab(T("Trabalho e casa"), Work());
	Tabs->AddTab(T("Ocorrências"), Law());
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.MinDesiredWidth(920.f)
		.MinDesiredHeight(560.f)
		[
			VilaUI::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
				[
					MakeHeader(F("Diário de %s", {vila::game().player.name}), 24)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).WidthOverride(880.f)[Tabs]
				])
		]
	];
}

TSharedRef<SWidget> SVilaJournalPanel::Quests()
{
	FCol C;
	vila::Game& G = vila::game();
	bool bAny = false;
	for (const auto& It : vila::data().quests().items())
	{
		const std::string Qid = It.key();
		const std::string St = G.quests.state_of(Qid);
		if (St == "not_started") continue;
		bAny = true;
		const vila::Json& Q = vila::data().quest(Qid);
		const FString Status = St == "active" ? T("Em andamento") : St == "completed" ? T("Concluída") : St == "failed" ? T("Falhou") : S(St);
		C.Add(VilaUI::Label(FString::Printf(TEXT("%s — %s"), *Tr(vila::jstr(Q, "title")), *Status), 20, St == "active" ? VilaUI::Quest() : VilaUI::Muted()));
		C.Wrap(Tr(vila::jstr(Q, "summary")));
		if (St == "active")
		{
			C.Wrap(TEXT("▶ ") + S(G.quests.objective(Qid)), 16, VilaUI::Accent());
			const vila::Json& Stage = vila::jobj(vila::jobj(Q, "stages"), G.quests.stage_of(Qid));
			for (const vila::Json& H : vila::jarr(Stage, "hints")) C.Wrap(TEXT("   • ") + Tr(vila::to_str(H)), 14, VilaUI::Muted());
		}
		for (const vila::Json& E : vila::jarr(vila::jobj(G.quests.states, Qid), "log"))
		{
			C.Wrap(FString::Printf(TEXT("   %s %d, %s — %s"), *T("Dia"), vila::jint(E, "day"), *S(vila::jstr(E, "time")), *S(vila::jstr(E, "text"))), 14, VilaUI::Muted());
		}
	}
	if (!bAny) C.Wrap(T("Nenhuma missão ainda. Converse com as pessoas do bairro — algumas precisam de ajuda."), 16, VilaUI::Muted());
	C.Add(VilaUI::Separator());
	C.Wrap(T("A história principal (\"Os Livros da Aurora\") não faz parte deste protótipo. Você é livre para trabalhar, explorar e viver como quiser."), 14, VilaUI::Muted());
	return C.V;
}

TSharedRef<SWidget> SVilaJournalPanel::Character()
{
	FCol C;
	vila::Game& G = vila::game();
	vila::PlayerState& P = G.player;
	const vila::Needs& N = P.needs;
	C.Head(T("Condição física"));
	for (const std::string& Id : vila::Needs::IDS)
	{
		const double Val = N.get_need(Id);
		C.Row(Tr(vila::jstr(vila::Needs::LABELS, Id)), static_cast<float>(Val / 100.0), VilaUI::Lerp(VilaUI::Good(), VilaUI::Bad(), static_cast<float>(Val / 100.0)),
			FString::Printf(TEXT("%d/100"), static_cast<int32>(Val)));
	}
	C.Row(T("Saúde"), static_cast<float>(N.saude / 100.0), VilaUI::Lerp(VilaUI::Bad(), VilaUI::Good(), static_cast<float>(N.saude / 100.0)), FString::Printf(TEXT("%d/100"), static_cast<int32>(N.saude)));
	C.Row(T("Dor"), static_cast<float>(N.dor / 100.0), VilaUI::Lerp(VilaUI::Warn(), VilaUI::Bad(), static_cast<float>(N.dor / 100.0)), FString::Printf(TEXT("%d/100"), static_cast<int32>(N.dor)));
	C.Wrap(F("Temperatura corporal: %.1f °C · Roupa molhada: %d%% · Temperatura do ar: %.0f °C · Agasalho: %+.0f",
		{N.body_temp, static_cast<int>(N.wetness), G.ambient_temperature(), P.warmth()}));
	C.Wrap(F("Efeitos atuais: velocidade x%.2f · tarefas x%.2f mais lentas", {N.speed_multiplier(), N.work_multiplier()}), 14, VilaUI::Muted());
	TArray<FString> Worn;
	for (const auto& It : P.equipped.items()) Worn.Add(S(vila::data().item_name(vila::to_str(It.value()))));
	C.Wrap(F("Vestindo: %s", {U(Worn.Num() > 0 ? FString::Join(Worn, TEXT(", ")) : T("nada de especial"))}));
	if (!P.traits.empty())
	{
		C.Add(VilaUI::Separator());
		C.Head(T("Traços"));
		for (const vila::Json& Tid : P.traits)
		{
			const vila::Json& Td = vila::data().trait_def(vila::to_str(Tid));
			C.Wrap(FString::Printf(TEXT("%s — + %s  − %s"), *Tr(vila::jstr(Td, "name")), *Tr(vila::jstr(Td, "pro")), *Tr(vila::jstr(Td, "con"))), 14);
		}
	}
	C.Add(VilaUI::Separator());
	C.Head(T("Estatísticas"));
	C.Wrap(F("Dinheiro: R$ %d · Total ganho: R$ %d", {P.money, vila::jint(P.stats, "dinheiro_ganho")}));
	C.Wrap(F("Distância dirigida: %.1f km · Pedidos entregues: %d", {vila::jnum(P.stats, "km_dirigidos"), vila::jint(P.stats, "pedidos_entregues")}));
	C.Wrap(F("Reputação na Vila Aurora: %d (%s)", {G.relations.reputation, vila::tr(vila::Relations::reputation_label(G.relations.reputation))}));
	C.Wrap(F("Capacidade de carga: %.0f kg", {P.capacity()}));
	return C.V;
}

TSharedRef<SWidget> SVilaJournalPanel::Skills()
{
	FCol C;
	const vila::Skills& Sk = vila::game().player.skills;
	for (const auto& It : vila::Skills::DEFS.items())
	{
		const std::string Id = It.key();
		C.Row(F("%s (nível %d)", {vila::tr(vila::jstr(It.value(), "name")), Sk.level(Id)}), static_cast<float>(Sk.progress(Id)), VilaUI::Accent(),
			FString::Printf(TEXT("%d XP"), static_cast<int32>(vila::jnum(Sk.xp, Id))));
		C.Wrap(TEXT("   ") + Tr(vila::jstr(It.value(), "desc")), 14, VilaUI::Muted());
	}
	C.Wrap(T("Habilidades sobem com a prática: cozinhar no drive-thru, conversar e persuadir, dirigir e correr."), 14, VilaUI::Muted());
	return C.V;
}

TSharedRef<SWidget> SVilaJournalPanel::Relations()
{
	FCol C;
	vila::Relations& R = vila::game().relations;
	for (const auto& It : vila::data().npcs().items())
	{
		const std::string Id = It.key();
		const int32 Rel = R.get_rel(Id);
		const float T01 = (Rel + 100) / 200.f;
		C.Row(Tr(vila::jstr(vila::data().npc(Id), "name")), T01, VilaUI::Lerp(VilaUI::Bad(), VilaUI::Good(), T01),
			FString::Printf(TEXT("%d · %s"), Rel, *Tr(R.attitude(Id))));
		const vila::Json& Mems = vila::jarr(R.memories, Id);
		if (!Mems.empty())
		{
			TArray<FString> Tags;
			for (const vila::Json& M : Mems) Tags.Add(MemoryText(vila::to_str(vila::jget(M, "tag"))));
			C.Wrap(TEXT("   ") + T("Lembra: ") + FString::Join(Tags, TEXT(", ")), 13, VilaUI::Muted());
		}
	}
	return C.V;
}

FString SVilaJournalPanel::MemoryText(const std::string& Tag)
{
	static const vila::Json Texts = {
		{"ajudou", "você ajudou"}, {"ameacado", "foi ameaçado(a) por você"}, {"atropelado", "foi atropelado(a) por você"},
		{"prestou_socorro", "você prestou socorro"}, {"confessou", "você confessou"}, {"viu_furto", "viu você furtar"},
		{"vitima_furto", "foi furtado(a) por você"}, {"viu_invasao", "viu você invadir uma casa"}, {"vitima_invasao", "teve a casa invadida por você"},
		{"viu_atropelamento", "viu você atropelar alguém"}, {"vitima_atropelamento", "foi atropelado(a) por você"},
		{"vitima_ameaca", "foi ameaçado(a) por você"}, {"viu_furto_veiculo", "viu você furtar um carro"},
		{"devolveu_carteira", "você devolveu a carteira dele(a)"},
	};
	return Tr(vila::jstr(Texts, Tag, Tag));
}

TSharedRef<SWidget> SVilaJournalPanel::Work()
{
	FCol C;
	vila::Game& G = vila::game();
	const vila::DriveThruJob& J = G.job;
	C.Head(T("Brasa Burger Drive-Thru"));
	if (J.fired)
		C.Wrap(T("Você foi demitido(a)."), 15, VilaUI::Bad());
	else if (!J.employed)
		C.Wrap(T("Você não trabalha lá. A gerente Sandra está contratando."));
	else
	{
		C.Wrap(F("Cargo: %s · Salário: R$ %d/hora + R$ 2 por pedido + gorjetas", {J.rank_title(), J.wage()}));
		C.Row(T("Desempenho"), static_cast<float>(J.performance / 100.0), VilaUI::Accent(), FString::Printf(TEXT("%d/100"), static_cast<int32>(J.performance)));
		C.Wrap(F("Pedidos entregues: %d · Turnos completos: %d · Advertências: %d/3", {J.total_orders, J.shifts_done, J.warnings}));
		const vila::Json& Ranks = vila::jarr(vila::data().job("brasa"), "ranks");
		if (J.rank + 1 < static_cast<int>(Ranks.size()))
		{
			const vila::Json& Nx = Ranks[static_cast<size_t>(J.rank + 1)];
			C.Wrap(F("Próxima promoção (%s): desempenho %d+ e %d pedidos.", {vila::tr(vila::jstr(Nx, "title")), vila::jint(Nx, "min_perf"), vila::jint(Nx, "min_orders")}), 14, VilaUI::Muted());
		}
	}
	C.Add(VilaUI::Separator());
	C.Head(T("Entregas do Mercadinho"));
	const vila::DeliveryJob& D = G.delivery;
	if (!D.hired)
		C.Wrap(T("O Seu Antônio procura entregador. Pergunte a ele."));
	else
	{
		C.Wrap(F("Entregas feitas: %d (hoje: %d) · Atrasadas: %d", {D.total, D.done_today, D.late_total}));
		if (!D.active.empty())
			C.Wrap(F("Em andamento: %s — %d min restantes", {vila::jstr(D.active, "label"), static_cast<int>(D.time_left(vila::clock().total_minutes))}), 15, VilaUI::Quest());
	}
	C.Add(VilaUI::Separator());
	C.Head(T("Imóveis e veículos"));
	for (const auto& It : G.property.props.items())
	{
		const std::string Id = It.key();
		const vila::Json& Pr = It.value();
		const std::string Status = vila::jstr(Pr, "status");
		const std::string Cost = Status == "alugada"
			? vila::fmt(vila::tr("Aluguel R$ %d/semana"), {vila::jint(Pr, "rent")})
			: vila::fmt(vila::tr("IPTU R$ %d/semana · revenda R$ %d"), {vila::jint(Pr, "tax"), G.property.sale_value(Id)});
		C.Wrap(F("%s — %s · %s · Dívida: R$ %d", {vila::tr(vila::jstr(vila::data().property(Id), "name", Id)), vila::tr(Status), Cost, vila::jint(Pr, "debt")}));
	}
	for (const auto& It : vila::data().properties().items())
	{
		if (G.property.is_for_sale(It.key()))
			C.Wrap(F("À venda: %s — R$ %d (placa na frente do imóvel)", {vila::tr(vila::jstr(vila::data().property(It.key()), "name")), G.property.price(It.key())}), 14, VilaUI::Muted());
	}
	for (const auto& It : G.world_state.vehicles.items())
	{
		const vila::Json& Vs = It.value();
		const std::string VName = vila::tr(vila::jstr(vila::data().vehicle(vila::jstr(Vs, "type")), "name"));
		if (vila::jstr(Vs, "owner") == "player")
			C.Wrap(F("Veículo: %s — combustível %.0f L · dano %d%%", {VName, vila::jnum(Vs, "fuel"), vila::jint(Vs, "damage")}));
		else if (vila::has(Vs, "for_sale"))
			C.Wrap(F("À venda na oficina: %s — R$ %d", {VName, vila::jint(vila::jobj(Vs, "for_sale"), "price")}), 14, VilaUI::Muted());
	}
	C.Wrap(T("Guarde comida na geladeira (estraga 4x mais devagar) e objetos no guarda-roupa: tudo fica salvo."), 14, VilaUI::Muted());
	return C.V;
}

TSharedRef<SWidget> SVilaJournalPanel::Law()
{
	FCol C;
	const vila::LawSystem& L = vila::game().law;
	if (L.record.empty()) C.Wrap(T("Ficha limpa."), 16, VilaUI::Good());
	for (const vila::Json& E : L.record)
	{
		const std::string Type = vila::jstr(E, "type");
		const std::string Label = vila::jstr(vila::jobj(vila::LawSystem::CRIMES, Type), "label", Type);
		const bool bSolved = vila::jbool(E, "solved");
		const FString Who = bSolved ? F("testemunhado — multa R$ %d", {vila::jint(E, "fine")}) : T("sem testemunhas");
		C.Wrap(FString::Printf(TEXT("%s %d — %s (%s)"), *T("Dia"), vila::jint(E, "day"), *Tr(Label), *Who), 15, bSolved ? VilaUI::Bad() : VilaUI::Muted());
	}
	if (L.pending_fines > 0) C.Wrap(F("Multas a chegar amanhã às 8h: R$ %d", {L.pending_fines}), 15, VilaUI::Warn());
	return C.V;
}
