// Menu de pausa (port de src/ui/PauseMenu.gd): continuar, salvar, carregar, opções, voltar ao
// menu principal e sair. O AVilaHUD pausa o jogo enquanto ele está aberto.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"

class SBox;

class VILAAURORA_API SVilaPauseMenu : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaPauseMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void ShowMain();
	void ShowSave();
	void ShowLoad();
	void ShowOptions();
	TSharedRef<SVerticalBox> Clear();

	TSharedPtr<SBox> Content;
};
