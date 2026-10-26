// Menu principal (port de src/ui/MainMenu.gd): novo jogo (criação de personagem), continuar,
// carregar, opções, créditos e sair. O fundo é a própria vila (AVilaWorld no fluxo Menu, com a
// câmera orbitando a praça). Mostra os erros de dados e o aviso de conteúdo 3D ausente.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AVilaHUD;
class SBox;
class SVerticalBox;

class VILAAURORA_API SVilaMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AVilaHUD>, HUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	void Rebuild();  // recria tudo (o tamanho do texto pode ter mudado nas opções)
	TSharedRef<SWidget> MainButtons();
	TSharedRef<SWidget> Diagnostics();
	void ClearCenter();
	TSharedRef<SVerticalBox> ShowPanel(float MinWidth);
	void NewGame();
	void Continue();
	void LoadPanel();
	void OptionsPanel();
	void Credits();
	void Message(const FString& Title, const FString& Body);

	TWeakObjectPtr<AVilaHUD> HUD;
	TSharedPtr<SBox> Center;
};
