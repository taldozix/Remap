#include "UI/SVilaContainerPanel.h"

#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "VilaWorld.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Inventory.h"

using namespace VilaConv;
using VilaUI::T;

void SVilaContainerPanel::Construct(const FArguments& InArgs)
{
	ContainerId = InArgs._ContainerId;
	Title = InArgs._Title;
	const std::string Cid = U(ContainerId);
	vila::Game& G = vila::game();

	// Avisos ao lado do título.
	TSharedRef<SHorizontalBox> Notes = SNew(SHorizontalBox);
	const vila::Json Meta = G.world_state.meta(Cid);
	const std::string Owner = vila::jstr(Meta, "owner");
	if (!Owner.empty() && Owner != "player" && vila::jbool(Meta, "private", true))
		Notes->AddSlot().AutoWidth().VAlign(VAlign_Center)[VilaUI::Label(TEXT("  ") + T("(não é seu — pegar algo é furto)"), 15, VilaUI::Bad())];
	if (const vila::Inventory* C = G.world_state.container(Cid))
		if (C->refrigerated)
			Notes->AddSlot().AutoWidth().VAlign(VAlign_Center)[VilaUI::Label(TEXT("  ") + T("(refrigerado: conserva alimentos)"), 15, VilaUI::Quest())];

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.MinDesiredWidth(920.f)
		.MinDesiredHeight(500.f)
		[
			VilaUI::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
				[
					MakeHeader(Title, 22, Notes)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 16.f, 0.f))
					[
						Column(T("Você"), Left, LeftW)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						Column(Title, Right, RightW)
					]
				])
		]
	];
	Refresh();
}

void SVilaContainerPanel::OnOpen()
{
	VilaUI::PlaySound(TEXT("door_open"), -10.f, 1.5f);
}

TSharedRef<SWidget> SVilaContainerPanel::Column(const FString& Label, TSharedPtr<SVerticalBox>& OutList, TSharedPtr<STextBlock>& OutWeight)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[VilaUI::Label(Label, 18)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(OutWeight, STextBlock).Font(VilaUI::Font(15)).ColorAndOpacity(FSlateColor(VilaUI::Muted()))
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MinDesiredWidth(430.f).HeightOverride(400.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OutList, SVerticalBox)
				]
			]
		];
}

void SVilaContainerPanel::Refresh()
{
	vila::PlayerState& P = vila::game().player;
	vila::Inventory* C = vila::game().world_state.container(U(ContainerId));
	if (!C) return;
	LeftW->SetText(FText::FromString(S(vila::fmt("  %.1f / %.0f kg", {P.inventory.weight(), P.capacity()}))));
	RightW->SetText(FText::FromString(S(vila::fmt("  %.1f / %.0f kg", {C->weight(), C->capacity}))));
	Fill(Left, P.inventory, true);
	Fill(Right, *C, false);
}

void SVilaContainerPanel::Fill(const TSharedPtr<SVerticalBox>& List, vila::Inventory& Inv, bool bFromPlayer)
{
	List->ClearChildren();
	if (Inv.items.empty())
		List->AddSlot().AutoHeight()[VilaUI::Label(T("Vazio."), 15, VilaUI::Muted())];
	for (size_t i = 0; i < Inv.items.size(); ++i)
	{
		const vila::Json& St = Inv.items[i];
		const std::string Id = vila::jstr(St, "id");
		const int32 Qty = vila::jint(St, "qty");
		const FString Spoiled = Inv.is_spoiled(St) ? T(" (estragado)") : FString();
		const int32 Index = static_cast<int32>(i);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s x%d%s"), *S(vila::data().item_name(Id)), Qty, *Spoiled)))
				.Font(VilaUI::Font(15))
				.ColorAndOpacity(FSlateColor(Spoiled.IsEmpty() ? VilaUI::TextColor() : VilaUI::Bad()))
				.Clipping(EWidgetClipping::ClipToBounds)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f, 0.f, 0.f))
			[
				VilaUI::Button(bFromPlayer ? T("Guardar") : T("Pegar"), [this, bFromPlayer, Index]() { Move(bFromPlayer, Index, 1); }, 15)
			];
		if (Qty > 1)
		{
			Row->AddSlot().AutoWidth().Padding(FMargin(4.f, 0.f, 0.f, 0.f))
			[
				VilaUI::Button(T("Tudo"), [this, bFromPlayer, Index, Qty]() { Move(bFromPlayer, Index, Qty); }, 15)
			];
		}
		List->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))[Row];
	}
}

void SVilaContainerPanel::Move(bool bFromPlayer, int32 Index, int32 Qty)
{
	vila::Game& G = vila::game();
	vila::PlayerState& P = G.player;
	const std::string Cid = U(ContainerId);
	vila::Inventory* C = G.world_state.container(Cid);
	if (!C) return;
	int32 Moved = 0;
	if (bFromPlayer)
	{
		if (Index < 0 || Index >= static_cast<int32>(P.inventory.items.size())) return;
		const std::string Id = vila::jstr(P.inventory.items[static_cast<size_t>(Index)], "id");
		if (vila::jbool(vila::data().item(Id), "job_item"))
		{
			vila::events().notify.emit(vila::tr("Itens do pedido ficam com você."), std::string("bad"));
			return;
		}
		if (P.is_equipped(Id) && P.inventory.count(Id) <= Qty) P.unequip(vila::jstr(vila::data().item(Id), "slot"));
		Moved = P.inventory.transfer_to(*C, Index, Qty, true);
		if (Moved == 0) vila::events().notify.emit(vila::tr("Não cabe aqui."), std::string("bad"));
	}
	else
	{
		Moved = C->transfer_to(P.inventory, Index, Qty, false);
		if (Moved > 0 && !bTheftReported)
		{
			const vila::Json Meta = G.world_state.meta(Cid);
			const std::string Owner = vila::jstr(Meta, "owner");
			std::string Flag = Cid;
			if (Flag.rfind("trunk_", 0) == 0) Flag = Flag.substr(6);
			if (!Owner.empty() && Owner != "player" && vila::jbool(Meta, "private", true) && !G.has_flag("roubado_" + Flag))
			{
				bTheftReported = true;
				if (AVilaWorld* World = VilaWorld()) World->OnContainerTake(ContainerId);
			}
		}
	}
	if (Moved > 0)
	{
		VilaUI::PlaySound(TEXT("pickup"), -6.f);
		vila::events().inventory_changed.emit(std::string("player"));
	}
	Refresh();
}
