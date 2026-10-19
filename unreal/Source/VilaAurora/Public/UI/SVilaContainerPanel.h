// Transferência entre o inventário e um contêiner (port de src/ui/ContainerPanel.gd):
// geladeira, armário, porta-malas, lixeira, baú. Pegar de contêiner alheio conta como furto.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"

namespace vila { class Inventory; }

class SVilaContainerPanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaContainerPanel) {}
		SLATE_ARGUMENT(FString, ContainerId)
		SLATE_ARGUMENT(FString, Title)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void OnOpen() override;

private:
	TSharedRef<SWidget> Column(const FString& Label, TSharedPtr<SVerticalBox>& OutList, TSharedPtr<STextBlock>& OutWeight);
	void Refresh();
	void Fill(const TSharedPtr<SVerticalBox>& List, vila::Inventory& Inv, bool bFromPlayer);
	void Move(bool bFromPlayer, int32 Index, int32 Qty);

	FString ContainerId;
	FString Title;
	TSharedPtr<SVerticalBox> Left;
	TSharedPtr<SVerticalBox> Right;
	TSharedPtr<STextBlock> LeftW;
	TSharedPtr<STextBlock> RightW;
	bool bTheftReported = false;
};
