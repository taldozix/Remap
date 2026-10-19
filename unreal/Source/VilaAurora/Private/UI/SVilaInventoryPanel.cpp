#include "UI/SVilaInventoryPanel.h"

#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "VilaWorld.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Inventory.h"
#include "Vila/Needs.h"

using namespace VilaConv;
using VilaUI::T;

void SVilaInventoryPanel::Construct(const FArguments& InArgs)
{
	TSharedRef<SHorizontalBox> Extra = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			SAssignNew(WeightLabel, STextBlock).Font(VilaUI::Font(16)).ColorAndOpacity(FSlateColor(VilaUI::TextColor()))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			VilaUI::Bar(TAttribute<TOptional<float>>::CreateLambda([this]() { return TOptional<float>(WeightFrac); }), FSlateColor(VilaUI::Accent()), 180.f)
		];
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.MinDesiredWidth(860.f)
		.MinDesiredHeight(520.f)
		[
			VilaUI::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
				[
					MakeHeader(T("Inventário"), 24, Extra)
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 14.f, 0.f))
					[
						SNew(SBox).WidthOverride(470.f).HeightOverride(420.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(List, SVerticalBox)
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						VilaUI::Panel(SAssignNew(Details, SVerticalBox), VilaUI::BgLight(), 8.f, 12.f)
					]
				])
		]
	];
	Refresh();
}

SVilaInventoryPanel::~SVilaInventoryPanel() { Disconnect(); }

void SVilaInventoryPanel::OnOpen()
{
	Conn = vila::events().inventory_changed.connect([this](const std::string& Owner)
	{
		if (Owner == "player") Refresh();
	});
}

void SVilaInventoryPanel::OnClose() { Disconnect(); }

void SVilaInventoryPanel::Disconnect()
{
	if (Conn != 0) vila::events().inventory_changed.disconnect(Conn);
	Conn = 0;
}

void SVilaInventoryPanel::Refresh()
{
	vila::PlayerState& P = vila::game().player;
	const double Cap = P.capacity();
	WeightLabel->SetText(FText::FromString(VilaUI::F("Peso: %.1f / %.0f kg", {P.inventory.weight(), Cap})));
	WeightFrac = Cap > 0.0 ? static_cast<float>(FMath::Min(1.0, P.inventory.weight() / Cap)) : 0.f;
	List->ClearChildren();
	if (P.inventory.items.empty())
		List->AddSlot().AutoHeight()[VilaUI::Label(T("Você não está carregando nada."), 16, VilaUI::Muted())];
	for (size_t i = 0; i < P.inventory.items.size(); ++i)
	{
		const vila::Json& St = P.inventory.items[i];
		const std::string Id = vila::jstr(St, "id");
		const int32 Qty = vila::jint(St, "qty");
		FString Status;
		if (vila::Inventory::is_perishable(Id))
			Status = P.inventory.is_spoiled(St) ? T(" · estragado") : (P.inventory.freshness(St) > 0.5 ? T(" · fresco") : T(" · passando"));
		const FString Eq = P.is_equipped(Id) ? T(" (vestido)") : FString();
		const FString Text = S(vila::fmt("%s x%d%s%s   —   %.2f kg", {vila::data().item_name(Id), Qty, U(Eq), U(Status), vila::Inventory::item_weight(Id) * Qty}));
		const int32 Index = static_cast<int32>(i);
		TSharedRef<STextBlock> L = SNew(STextBlock).Text(FText::FromString(Text)).Font(VilaUI::Font(16));
		if (Index == Selected) L->SetColorAndOpacity(FSlateColor(VilaUI::Accent()));
		TSharedRef<SButton> B = SNew(SButton)
			.ButtonStyle(&VilaUI::ButtonStyle())
			.ContentPadding(FMargin(0.f))
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Index]()
			{
				VilaUI::PlaySound(TEXT("ui_click"), -10.f);
				Select(Index);
				return FReply::Handled();
			})
			[
				L
			];
		List->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))[B];
	}
	ShowDetails();
}

void SVilaInventoryPanel::Select(int32 Index)
{
	Selected = Index;
	Refresh();
}

void SVilaInventoryPanel::ShowDetails()
{
	Details->ClearChildren();
	const auto Add = [this](const TSharedRef<SWidget>& W) { Details->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[W]; };
	vila::PlayerState& P = vila::game().player;
	if (Selected < 0 || Selected >= static_cast<int32>(P.inventory.items.size()))
	{
		Add(VilaUI::Label(T("Selecione um item."), 16, VilaUI::Muted()));
		NeedsSummary();
		return;
	}
	const vila::Json St = P.inventory.items[static_cast<size_t>(Selected)];
	const std::string Id = vila::jstr(St, "id");
	const vila::Json& It = vila::data().item(Id);
	Add(VilaUI::Label(S(vila::data().item_name(Id)), 20, VilaUI::Accent()));
	Add(VilaUI::Label(Tr(vila::jstr(It, "desc")), 15, VilaUI::TextColor(), 300.f));
	Add(VilaUI::Label(VilaUI::F("Valor de referência: R$ %d", {vila::jint(It, "value")}), 14, VilaUI::Muted()));
	if (vila::has(It, "use"))
	{
		TArray<FString> Parts;
		for (const auto& U2 : vila::jobj(It, "use").items())
		{
			const double Val = vila::to_num(U2.value());
			FString Name = vila::has(vila::Needs::LABELS, U2.key()) ? Tr(vila::jstr(vila::Needs::LABELS, U2.key())) : Tr(vila::capitalize_first(U2.key()));
			if (U2.key() == "saude") Name = T("Saúde");
			Parts.Add(FString::Printf(TEXT("%s %s%d"), *Name, Val > 0.0 ? TEXT("+") : TEXT(""), static_cast<int32>(Val)));
		}
		Add(VilaUI::Label(FString::Join(Parts, TEXT(", ")), 14, VilaUI::Muted(), 300.f));
	}
	if (vila::Inventory::is_perishable(Id))
	{
		Add(VilaUI::Label(VilaUI::F("Frescor: %d%%", {static_cast<int>(P.inventory.freshness(St) * 100.0)}), 14,
			P.inventory.is_spoiled(St) ? VilaUI::Bad() : VilaUI::Good()));
	}
	TSharedRef<SWrapBox> Actions = SNew(SWrapBox).UseAllottedSize(true);
	const auto AddAction = [&Actions](const TSharedRef<SWidget>& W) { Actions->AddSlot().Padding(FMargin(0.f, 0.f, 6.f, 6.f))[W]; };
	if (vila::has(It, "use")) AddAction(VilaUI::Button(Tr(vila::jstr(It, "use_label", "Usar")), [this]() { Use(); }));
	if (vila::has(It, "slot"))
	{
		const std::string Slot = vila::jstr(It, "slot");
		if (P.is_equipped(Id))
			AddAction(VilaUI::Button(T("Tirar"), [this, Slot]()
			{
				vila::game().player.unequip(Slot);
				Refresh();
			}));
		else
			AddAction(VilaUI::Button(T("Vestir"), [this, Id]()
			{
				vila::PlayerState& PS = vila::game().player;
				PS.equip(Id);
				vila::events().notify.emit(vila::fmt(vila::tr("Capacidade de carga: %.0f kg"), {PS.capacity()}), std::string("info"));
				Refresh();
			}));
	}
	if (!vila::jbool(It, "job_item"))
	{
		AddAction(VilaUI::Button(T("Largar 1"), [this]() { Drop(1); }));
		const int32 Qty = vila::jint(St, "qty");
		if (Qty > 1) AddAction(VilaUI::Button(T("Largar tudo"), [this, Qty]() { Drop(Qty); }));
	}
	Add(Actions);
}

void SVilaInventoryPanel::NeedsSummary()
{
	const vila::Needs& N = vila::game().player.needs;
	const auto Row = [this](const FString& Label, float Value01, const FLinearColor& Col)
	{
		Details->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(90.f)[VilaUI::Label(Label, 15)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				VilaUI::Bar(TOptional<float>(Value01), FSlateColor(Col), 200.f)
			]
		];
	};
	Details->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[VilaUI::Label(T("Condição física"), 18, VilaUI::Accent())];
	for (const std::string& Id : vila::Needs::IDS)
	{
		const double Val = N.get_need(Id);
		Row(Tr(vila::jstr(vila::Needs::LABELS, Id)), static_cast<float>(Val / 100.0), VilaUI::Lerp(VilaUI::Good(), VilaUI::Bad(), static_cast<float>(Val / 100.0)));
	}
	Row(T("Saúde"), static_cast<float>(N.saude / 100.0), VilaUI::Lerp(VilaUI::Bad(), VilaUI::Good(), static_cast<float>(N.saude / 100.0)));
	Details->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
	[
		VilaUI::Label(T("(0 = ótimo; barras cheias indicam necessidade urgente)"), 13, VilaUI::Muted(), 300.f)
	];
}

void SVilaInventoryPanel::Use()
{
	vila::Game& G = vila::game();
	vila::PlayerState& P = G.player;
	if (Selected < 0 || Selected >= static_cast<int32>(P.inventory.items.size())) return;
	const vila::Json St = P.inventory.items[static_cast<size_t>(Selected)];
	const std::string ItemId = vila::jstr(St, "id");
	const vila::Json It = vila::data().item(ItemId);
	if (vila::jstr(It, "category") == "trabalho") return;
	const bool bSpoiled = P.inventory.is_spoiled(St);
	P.inventory.take_at(Selected, 1);
	P.needs.consume(vila::jobj(It, "use"));
	if (vila::has(It, "use_money"))
	{
		P.add_money(vila::jint(It, "use_money"), "Dinheiro da carteira");
		for (const auto& Npc : vila::data().npcs().items()) G.clear_flag("carteira_dono_" + Npc.key());
		G.set_flag("ficou_com_carteira");
	}
	if (bSpoiled)
	{
		P.needs.consume(vila::Json{{"nausea", 4.0}, {"saude", -8}});
		vila::events().notify.emit(vila::tr("Estava estragado... Você está enjoado(a)."), std::string("bad"));
	}
	VilaUI::PlaySound(S(vila::jstr(It, "sound", "pickup")));
	vila::events().needs_changed.emit();
	if (Selected >= static_cast<int32>(P.inventory.items.size())) Selected = -1;
	if (P.is_equipped(ItemId) && !P.inventory.has(ItemId)) P.unequip(vila::jstr(It, "slot"));
	Refresh();
}

void SVilaInventoryPanel::Drop(int32 Qty)
{
	if (AVilaWorld* World = VilaWorld()) World->DropItem(Selected, Qty);
	Selected = -1;
	Refresh();
}
