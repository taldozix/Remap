// Loja (port de src/ui/ShopPanel.gd): comprar do estoque (preço com desconto de freguês)
// e vender ao penhor. Usa vila::game().shops.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"

class SVilaShopPanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaShopPanel) {}
		SLATE_ARGUMENT(FString, ShopId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> Column(const FString& Label, TSharedPtr<SVerticalBox>& OutList);
	void Refresh();
	void Buy(const std::string& ItemId);
	void Sell(int32 Index);

	FString ShopId;
	TSharedPtr<SVerticalBox> BuyList;
	TSharedPtr<SVerticalBox> SellList;
	TSharedPtr<STextBlock> MoneyLabel;
	TSharedPtr<STextBlock> InfoLabel;
};
