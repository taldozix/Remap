// Inventário (port de src/ui/InventoryPanel.gd): lista com peso, frescor e ações
// (comer/beber/usar, vestir, largar), e o resumo da condição física.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"

class SVilaInventoryPanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaInventoryPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVilaInventoryPanel() override;
	virtual void OnOpen() override;
	virtual void OnClose() override;

private:
	void Refresh();
	void ShowDetails();
	void NeedsSummary();
	void Select(int32 Index);
	void Use();
	void Drop(int32 Qty);
	void Disconnect();

	TSharedPtr<SVerticalBox> List;
	TSharedPtr<SVerticalBox> Details;
	TSharedPtr<STextBlock> WeightLabel;
	float WeightFrac = 0.f;
	int32 Selected = -1;
	int32 Conn = 0;
};
