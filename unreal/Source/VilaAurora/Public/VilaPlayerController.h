// Controle do jogador. Cria UInputAction/UInputMappingContext em runtime a partir de
// vila::settings().bindings (os nomes de tecla do JSON são nomes de FKey) e os recria em
// Settings::changed("input"). Roteia para o jogador, para o veículo, para a câmera ou para o HUD
// (port do _unhandled_input de Player.gd, CameraRig.gd e UIManager.gd e do input de Vehicle.gd).
//
// Teclas de eixo com sinal ("Gamepad_LeftY+", "Gamepad_LeftX-") não são FKey válidas: viram uma
// ação Axis1D interna mapeada no eixo (com Negate para "-"), lida como força 0..1.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "VilaPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AVilaPlayerCharacter;
class AVilaWorld;
struct FInputActionInstance;

// Metade de um eixo do gamepad ligada a uma ação ("Gamepad_LeftY+" -> move_up).
struct FVilaAxisBinding
{
	FString Action;
	TWeakObjectPtr<UInputAction> InputAction;
	bool bPrevDown = false;
};

UCLASS()
class VILAAURORA_API AVilaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVilaPlayerController();

	// Para a UI: suspende o controle do jogo quando há painel modal (mostra o cursor).
	void SetUIMode(bool bModal);
	bool IsActionDown(const FString& Action) const;   // "run", "handbrake"...
	FVector2D GetMoveAxis() const;                    // move_left/right/up/down combinados (-1..1); X = direita, Y = cima
	// Força 0..1 da ação (analógica nos eixos do gamepad), como Input.get_action_strength.
	float ActionStrength(const FString& Action) const;
	bool IsUIMode() const { return bUIModal; }
	// Refaz o mapeamento a partir de vila::settings().bindings.
	void RebuildInputMappings();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	void CreateActions();
	bool ApplyMappings();
	void OnActionStarted(const FInputActionInstance& Instance);
	void HandlePressed(const FString& Action);
	float RawStrength(const FString& Action) const;
	AVilaPlayerCharacter* GetVilaPlayer() const;
	AVilaWorld* GetVilaWorld() const;

	UPROPERTY() TMap<FString, TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> AxisActions;   // ações internas dos eixos com sinal
	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	TArray<FVilaAxisBinding> AxisBindings;

	bool bUIModal = false;
	bool bMappingDirty = true;
	bool bBound = false;
	FString LastPanel;  // painel aberto por tecla ("inventory", "journal", "map")
	int32 SettingsConn = 0;
};
