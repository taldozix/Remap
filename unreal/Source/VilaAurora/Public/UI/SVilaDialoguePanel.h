// Caixa de diálogo (port de src/ui/DialoguePanel.gd): nome em destaque, retrato, texto com
// efeito de digitação, opções à direita (com chance dos testes e opções desabilitadas com
// dica). O relógio do jogo pausa durante a conversa.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"
#include "UObject/WeakObjectPtr.h"
#include "Vila/DialogueRunner.h"

class AVilaNPC;

class SVilaDialoguePanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaDialoguePanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AVilaNPC>, Npc)
		SLATE_ARGUMENT(FString, DialogueId)  // "" = def.dialogue do NPC
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void OnOpen() override;
	virtual void OnClose() override;
	virtual void TryClose() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	static FString Initials(const FString& Name);
	void ShowNode();
	void ShowChoices();
	void SkipOrAdvance();
	void Choose(int32 Index);
	void Finish();
	void UpdateTyped();

	TWeakObjectPtr<AVilaNPC> Npc;
	FString DialogueId;
	vila::DialogueRunner Runner;
	bool bStarted = false;
	bool bHeld = false;
	bool bChoicesVisible = false;

	TSharedPtr<STextBlock> NameLabel;
	TSharedPtr<STextBlock> PrefixLabel;
	TSharedPtr<STextBlock> TextLabel;
	TSharedPtr<STextBlock> HintLabel;
	TSharedPtr<SVerticalBox> ChoicesBox;

	FString Prefix;
	FLinearColor PrefixColor = FLinearColor::White;
	FString FullText;
	float Typing = 0.f;
	int32 FullLen = 0;
};
