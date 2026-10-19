// Interface de tudo com que o jogador interage (portas, móveis, itens no chão, NPCs,
// veículos, ponto de entrega). Equivale ao grupo "interactable" + get_actions/do_action do
// Godot (src/world/Interactable.gd).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "VilaInteractable.generated.h"

class AVilaPlayerCharacter;

USTRUCT(BlueprintType)
struct FVilaAction
{
	GENERATED_BODY()

	UPROPERTY() FString Id;
	UPROPERTY() FString Label;
	UPROPERTY() bool bEnabled = true;
	UPROPERTY() FString Hint;  // motivo quando desabilitada / detalhe

	FVilaAction() = default;
	FVilaAction(const FString& InId, const FString& InLabel, bool bInEnabled = true, const FString& InHint = FString())
		: Id(InId), Label(InLabel), bEnabled(bInEnabled), Hint(InHint) {}
};

UINTERFACE(MinimalAPI, NotBlueprintable)
class UVilaInteractable : public UInterface
{
	GENERATED_BODY()
};

class VILAAURORA_API IVilaInteractable
{
	GENERATED_BODY()

public:
	// Ponto de referência para distância/foco e para o marcador do prompt (espaço do mundo, cm).
	virtual FVector GetInteractPoint() const = 0;
	virtual FString GetInteractTitle() const = 0;
	// Raio (cm) dentro do qual o jogador pode interagir. Godot: Interactable.radius = 1.7 m.
	virtual float GetInteractRadius() const { return 170.f; }
	// Desempate do foco: maior = preferido (NPCs > veículos > móveis > itens).
	virtual float GetFocusPriority() const { return 0.f; }
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) = 0;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) = 0;
	// Prédio a que pertence ("" = rua). Usado para invasão/recortes.
	virtual FString GetBuildingId() const { return FString(); }
	// Liga/desliga o contorno de foco (Custom Depth stencil 1, lido pelo pós-processo de contorno).
	virtual void SetFocused(bool bFocused) {}
};
