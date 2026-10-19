// Raiz Slate da interface da partida (port da árvore de nós do UIManager.gd): camada do HUD
// (relógio, dinheiro, notificações, missão, turno, moodles, prompt, barra de ação, veículo,
// diagnóstico, marcadores e balões de fala), espaço do menu principal, espaço da janela
// modal (com o escurecimento) e a camada de fade por cima de tudo.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/WeakObjectPtr.h"

class AVilaHUD;
class AVilaWorld;
class SBox;
class SBorder;
class SVerticalBox;
class STextBlock;
class SVilaWorldLayer;

// Base das janelas modais (o "panel" do UIManager.open_panel). Ocupa a tela toda: bloqueia o
// mouse para o jogo, trata Esc (fechar) e as teclas de alternância do próprio painel.
class VILAAURORA_API SVilaPanel : public SCompoundWidget
{
public:
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	// UIManager: panel.open() logo depois de entrar na tela; panel.on_close() ao sair.
	virtual void OnOpen() {}
	virtual void OnClose() {}
	// Esc / pausa com o painel aberto. Padrão: fecha. O diálogo despede-se do NPC.
	virtual void TryClose();

	void Bind(AVilaHUD* InHUD, const FString& InKind);
	const FString& GetKind() const { return Kind; }

protected:
	AVilaHUD* GetHUD() const;
	AVilaWorld* VilaWorld() const;
	void Close();  // ui.close_current()
	// Cabeçalho padrão: título + espaço + [extra] + botão "Fechar".
	TSharedRef<SWidget> MakeHeader(const FString& Title, int32 TitleSize = 24, TSharedPtr<SWidget> Extra = nullptr);

	TWeakObjectPtr<AVilaHUD> HUD;
	FString Kind;
};

// Janela simples centralizada (mensagem, sono): UIManager._open_simple.
class VILAAURORA_API SVilaSimplePanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaSimplePanel) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(TFunction<void()>, OnAccept)  // Enter
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TFunction<void()> OnAccept;
};

class VILAAURORA_API SVilaHUDOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaHUDOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AVilaHUD>, HUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void SetModal(TSharedRef<SWidget> Panel, float Dim);
	void ClearModal();
	void SetMenu(TSharedPtr<SWidget> Menu);
	TSharedPtr<SWidget> GetMenu() const { return MenuWidget; }

	void AddToast(const FString& Text, const FString& Kind);
	void OnMoneyChanged(int32 Total, int32 Delta);
	void ToggleDebug();
	// Recria a camada do HUD (o tamanho do texto mudou).
	void RebuildHUD();

private:
	void RefreshHUD();
	void RefreshPrompt();
	void RefreshJob();
	TSharedRef<SWidget> Chip(const FString& Text, const FLinearColor& Col) const;

	TWeakObjectPtr<AVilaHUD> HUD;
	TSharedPtr<SBox> HUDSlot;
	TSharedPtr<SBox> MenuSlot;
	TSharedPtr<SBox> ModalSlot;
	TSharedPtr<SWidget> MenuWidget;

	// HUD
	TSharedPtr<SWidget> HUDLayer;
	TSharedPtr<STextBlock> ClockText;
	TSharedPtr<STextBlock> MoneyText;
	TSharedPtr<STextBlock> MoneyDeltaText;
	TSharedPtr<STextBlock> WeatherText;
	TSharedPtr<SVerticalBox> ToastBox;
	TSharedPtr<SBox> QuestBox;
	TSharedPtr<STextBlock> QuestText;
	TSharedPtr<SBox> JobBox;
	TSharedPtr<SVerticalBox> JobLines;
	TSharedPtr<SVerticalBox> MoodleBox;
	TSharedPtr<SBox> PromptBox;
	TSharedPtr<STextBlock> PromptTitle;
	TSharedPtr<SVerticalBox> PromptList;
	TSharedPtr<SBox> ProgressBox;
	TSharedPtr<STextBlock> ProgressText;
	TSharedPtr<SBox> VehicleBox;
	TSharedPtr<STextBlock> VehicleText;
	TSharedPtr<STextBlock> DebugText;
	TSharedPtr<SVilaWorldLayer> WorldLayer;

	struct FToast
	{
		TSharedPtr<SWidget> Widget;
		float Age = 0.f;
	};
	TArray<FToast> Toasts;

	float HudTimer = 0.f;
	float ProgressValue = 0.f;
	float FuelValue = 0.f;
	float MoneyDeltaAge = 99.f;
	float FpsSmoothed = 60.f;
	bool bDebug = false;
	FString LastPromptKey;
	FString LastMoodleKey;
	FString LastJobKey;
};
