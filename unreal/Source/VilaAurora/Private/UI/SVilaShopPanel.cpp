#include "UI/SVilaShopPanel.h"

#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
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

void SVilaShopPanel::Construct(const FArguments& InArgs)
{
	ShopId = InArgs._ShopId;
	const vila::Json& Shop = vila::data().shop(U(ShopId));
	TSharedRef<SHorizontalBox> Cols = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 16.f, 0.f))
		[
			Column(T("Comprar"), BuyList)
		];
	if (!vila::jarr(Shop, "buys").empty())
	{
		Cols->AddSlot().FillWidth(1.f)
		[
			Column(VilaUI::F("Vender (paga %d%% do valor)", {static_cast<int>(vila::jnum(Shop, "buy_rate", 0.5) * 100.0)}), SellList)
		];
	}
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.MinDesiredWidth(900.f)
		.MinDesiredHeight(520.f)
		[
			VilaUI::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
				[
					MakeHeader(Tr(vila::jstr(Shop, "name", U(ShopId))), 24,
						SAssignNew(MoneyLabel, STextBlock).Font(VilaUI::Font(20)).ColorAndOpacity(FSlateColor(VilaUI::Good())))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
				[
					SAssignNew(InfoLabel, STextBlock).Font(VilaUI::Font(15)).ColorAndOpacity(FSlateColor(VilaUI::Muted()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Cols
				])
		]
	];
	Refresh();
}

TSharedRef<SWidget> SVilaShopPanel::Column(const FString& Label, TSharedPtr<SVerticalBox>& OutList)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[VilaUI::Label(Label, 18)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MinDesiredWidth(420.f).HeightOverride(380.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OutList, SVerticalBox)
				]
			]
		];
}

void SVilaShopPanel::Refresh()
{
	vila::Game& G = vila::game();
	vila::PlayerState& P = G.player;
	const std::string Sid = U(ShopId);
	MoneyLabel->SetText(FText::FromString(FString::Printf(TEXT("R$ %d"), P.money)));
	const double Disc = G.shops.discount(Sid, G.relations);
	InfoLabel->SetText(FText::FromString(Disc > 0.0
		? VilaUI::F("Desconto de freguês: %d%%", {static_cast<int>(Disc * 100.0)})
		: VilaUI::F("Peso livre: %.1f kg", {P.capacity() - P.inventory.weight()})));

	BuyList->ClearChildren();
	for (const auto& It : vila::jobj(G.shops.stock, Sid).items())
	{
		const std::string ItemId = It.key();
		const int32 Qty = vila::to_int(It.value());
		const int32 Price = G.shops.price_buy(Sid, ItemId, G.relations);
		TSharedRef<SButton> B = VilaUI::Button(FString::Printf(TEXT("R$ %d"), Price), [this, ItemId]() { Buy(ItemId); }, 15);
		B->SetEnabled(!(Qty <= 0 || P.money < Price));
		BuyList->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(VilaUI::F("%s  (%d em estoque) — %.2f kg", {vila::data().item_name(ItemId), Qty, vila::Inventory::item_weight(ItemId)})))
				.Font(VilaUI::Font(15))
				.ColorAndOpacity(FSlateColor(Qty > 0 ? VilaUI::TextColor() : VilaUI::Muted()))
				.Clipping(EWidgetClipping::ClipToBounds)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f, 0.f, 0.f))[B]
		];
	}

	if (SellList.IsValid())
	{
		SellList->ClearChildren();
		bool bAny = false;
		for (size_t i = 0; i < P.inventory.items.size(); ++i)
		{
			const vila::Json& St = P.inventory.items[i];
			const std::string Id = vila::jstr(St, "id");
			if (!G.shops.buys_item(Sid, Id)) continue;
			bAny = true;
			const int32 Index = static_cast<int32>(i);
			SellList->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					VilaUI::Label(FString::Printf(TEXT("%s x%d"), *S(vila::data().item_name(Id)), vila::jint(St, "qty")), 15)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					VilaUI::Button(FString::Printf(TEXT("+R$ %d"), G.shops.price_sell(Sid, Id)), [this, Index]() { Sell(Index); }, 15)
				]
			];
		}
		if (!bAny) SellList->AddSlot().AutoHeight()[VilaUI::Label(T("Nada que interesse a esta loja."), 15, VilaUI::Muted())];
	}
}

void SVilaShopPanel::Buy(const std::string& ItemId)
{
	vila::Game& G = vila::game();
	const vila::Json R = G.shops.buy(U(ShopId), ItemId, 1, G.player, G.relations);
	if (vila::jbool(R, "ok"))
	{
		VilaUI::PlaySound(TEXT("cash"), -6.f);
		G.player.skills.add_xp("social", 1);
	}
	else
		vila::events().notify.emit(vila::jstr(R, "reason"), std::string("bad"));
	Refresh();
}

void SVilaShopPanel::Sell(int32 Index)
{
	vila::Game& G = vila::game();
	const vila::Json R = G.shops.sell(U(ShopId), Index, 1, G.player);
	if (vila::jbool(R, "ok"))
		VilaUI::PlaySound(TEXT("cash"), -6.f);
	else
		vila::events().notify.emit(vila::jstr(R, "reason"), std::string("bad"));
	Refresh();
}
