// Controle do jogador (port de Player.gd): movimento relativo à câmera, correr, interação
// contextual (foco no interagível mais próximo à frente, [T] alterna), ações com duração
// (barra de progresso), sentar/deitar/dormir e entrar/sair de veículos.
// A entrada chega pelo AVilaPlayerController (Enhanced Input).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Templates/Function.h"
#include "VilaInteractable.h"

#include "VilaPlayerCharacter.generated.h"

class AVilaFurniture;
class AVilaVehicle;
class AVilaWorld;
class AVilaPlayerController;
class UVilaHumanoid;

UCLASS()
class VILAAURORA_API AVilaPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AVilaPlayerCharacter();

	// estado do jogador, como no Godot:
	//   "free", "driving", "sitting", "lying", "sleeping", "rest", "bed"
	FString State = TEXT("free");
	UPROPERTY() TObjectPtr<AVilaVehicle> Vehicle;      // veículo atual, se dirigindo
	UPROPERTY() TObjectPtr<UVilaHumanoid> Humanoid;
	UPROPERTY() TObjectPtr<AVilaFurniture> Seat;       // assento/cama atual (sentado, deitado, dormindo)
	bool bInputLocked = false;                         // input_locked do Godot

	void RebuildVisual();                              // aparência/roupas do PlayerState
	void CycleTarget();
	AActor* GetFocus() const;                          // alvo IVilaInteractable atual
	TArray<FVilaAction> GetFocusActions() const;       // ações do alvo (para o HUD)
	void Perform(AActor* Target, const FString& ActionId);
	// Ação com tempo (barra de progresso no HUD). Callback ao concluir; interrompida se o jogador se mover.
	void StartTimed(const FString& Label, float Seconds, TFunction<void()> OnDone, const FString& Sound = FString());
	bool IsTimedActive() const;
	float TimedProgress() const;                       // 0..1
	FString TimedLabel() const;
	void SitOn(AVilaFurniture* F);
	void RestOnBed(AVilaFurniture* F);
	void StandUp();
	void BeginSleep();
	void EndSleep();
	void EnterVehicle(AVilaVehicle* V);
	void ExitVehicle();
	void SyncToState();                                // posição/rotação/veículo -> vila::game().player

	// --- chamados pelo AVilaPlayerController
	// Executa a ação de índice `Index` do foco (-1 = primeira habilitada, a tecla de interagir).
	void DoFocusAction(int32 Index);
	// Tecla do veículo: sai dirigindo ou entra no veículo mais próximo (até 3,2 m).
	void OnVehicleKey();

	// Pés do personagem (a origem do Godot). A origem do ator UE é o centro da cápsula.
	FVector FeetLocation() const;
	// Coloca o jogador na posição/rotação salvas em vila::game().player (não entra no veículo).
	void PlaceFromState();

	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	static constexpr float WalkSpeed = 2.4f;  // m/s
	static constexpr float RunSpeed = 5.2f;   // m/s

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AVilaWorld* GetVilaWorld() const;
	AVilaPlayerController* GetVilaController() const;
	void StepMovement(float Dt);
	void ScanInteractables();
	TArray<FVilaAction> SeatedActions() const;
	void ProcessTimed(float Dt);
	void CancelTimed();
	void WaitHour();
	void SetBodyEnabled(bool bEnabled);
	void SetVisualOffset(float ZCm);
	void SetFeetLocation(const FVector& Feet);
	void SetFocusTarget(AActor* NewFocus, bool bOutline);

	mutable TWeakObjectPtr<AVilaWorld> CachedWorld;

	TWeakObjectPtr<AActor> Focus;
	TWeakObjectPtr<AActor> Outlined;
	TArray<FVilaAction> FocusActions;
	TArray<TWeakObjectPtr<AActor>> FocusCandidates;
	int32 Cycle = 0;

	// ação com duração
	bool bTimed = false;
	FString TimedText;
	float TimedT = 0.f;
	float TimedDur = 1.f;
	TFunction<void()> TimedCallback;

	float ScanT = 0.f;
	float StepT = 0.f;
	float BumpT = 0.f;
	float RunDist = 0.f;
	float VisualOffset = 0.f;
};
