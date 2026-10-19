// Item largado no chão (port de src/world/GroundItem.gd), persistente em
// WorldState.ground_items. Sacola de papel pardo com faixa colorida da categoria.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vila/Core.h"
#include "VilaInteractable.h"

#include "VilaGroundItem.generated.h"

class AVilaWorld;

UCLASS()
class VILAAURORA_API AVilaGroundItem : public AActor, public IVilaInteractable
{
	GENERATED_BODY()

public:
	AVilaGroundItem();

	void Setup(const vila::Json& Entry, AVilaWorld* World);
	vila::Json Entry = vila::Json::object();

	// IVilaInteractable
	virtual FVector GetInteractPoint() const override { return GetActorLocation(); }
	virtual FString GetInteractTitle() const override { return Title; }
	virtual float GetInteractRadius() const override { return 140.f; }
	// Itens no chão são pequenos: ganham do móvel ou da pessoa ao lado no foco.
	virtual float GetFocusPriority() const override { return 0.6f; }
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) override;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) override;
	virtual void SetFocused(bool bFocused) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;
	FString Title;
};
