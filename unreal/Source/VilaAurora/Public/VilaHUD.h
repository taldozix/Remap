// Interface da partida (port de src/ui/UIManager.gd). O AHUD só cria a raiz Slate
// (SVilaHUDOverlay) e a põe no viewport; todo o desenho é Slate. Guarda a janela modal
// atual (inventário, contêiner, loja, diálogo, diário, mapa, pausa, sono, mensagem), a fila
// de mensagens e a loja pendente depois do diálogo, como o UIManager.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Vila/Core.h"

#include "VilaHUD.generated.h"

class AVilaNPC;
class AVilaPlayerCharacter;
class AVilaPlayerController;
class AVilaWorld;
class SVilaHUDOverlay;
class SVilaPanel;
class SWidget;

UCLASS()
class VILAAURORA_API AVilaHUD : public AHUD
{
	GENERATED_BODY()

public:
	AVilaHUD();

	static AVilaHUD* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ------------------------------------------------ contrato (API.md)
	bool IsModalOpen() const;
	bool IsDialogueOpen() const;
	void OpenContainer(const FString& ContainerId, const FString& Title);
	void OpenShop(const FString& ShopId);
	void OpenDialogue(AVilaNPC* Npc);
	void OpenPause();
	void OpenSleep(const FString& BedId, float Quality);
	void OpenPanel(const FString& Kind);     // "inventory", "journal", "map"
	void ShowMessage(const FString& Title, const FString& Body);
	void Fade(bool bToBlack);                // fade de 0,4 s (desmaio, sono)
	void CloseCurrent();
	void CloseAll();
	void ShowMainMenu();                     // fluxo Menu

	// ------------------------------------------------ extras usados pelos painéis
	void OpenDialogueWith(AVilaNPC* Npc, const FString& DialogueId);  // "" = diálogo do NPC
	void OpenModal(const FString& Kind, const TSharedRef<SVilaPanel>& Panel);
	const FString& GetCurrentKind() const { return CurrentKind; }
	bool IsMenuShown() const;
	void QuickSave();
	void ToggleDebug();
	float GetFadeAlpha() const { return FadeAlpha; }
	bool IsFadeDone() const { return FMath::IsNearlyEqual(FadeAlpha, FadeTarget); }
	// Pausa do relógio do jogo feita pela interface (liberada também no EndPlay).
	void HoldClock();
	void ReleaseClock();
	// call_deferred: roda no próximo quadro da interface.
	void Defer(TFunction<void()> Fn);
	// Chamado pelo overlay a cada quadro (roda mesmo com o jogo pausado).
	void TickUI(float DeltaSeconds);

	AVilaWorld* GetVilaWorld() const;
	AVilaPlayerCharacter* GetPlayer() const;
	AVilaPlayerController* GetVilaController() const;

private:
	void ConnectEvents();
	void DisconnectEvents();
	void OnNotify(const FString& Text, const FString& Kind);
	void OnRequestShop(const FString& ShopId);
	void OnRequestDialogue(const FString& NpcId, const FString& DialogueId);
	void SetUIMode(bool bModal);
	void RestoreFocus();
	void MaintainFocus();

	TSharedPtr<SVilaHUDOverlay> Overlay;
	TSharedPtr<SWidget> ViewportWidget;
	TSharedPtr<SVilaPanel> CurrentPanel;
	FString CurrentKind;
	TArray<TPair<FString, FString>> MsgQueue;
	FString PendingShop;
	TArray<TFunction<void()>> Deferred;

	float FadeAlpha = 0.f;
	float FadeTarget = 0.f;
	int32 ClockHolds = 0;
	bool bPausedGame = false;
	bool bPauseHold = false;
	bool bPrevDebug = false;
	bool bPrevQuickSave = false;

	int32 ConnNotify = 0;
	int32 ConnContainer = 0;
	int32 ConnShop = 0;
	int32 ConnMessage = 0;
	int32 ConnSleep = 0;
	int32 ConnDialogue = 0;
	int32 ConnSkill = 0;
	int32 ConnMoney = 0;
	int32 ConnSettings = 0;
};
