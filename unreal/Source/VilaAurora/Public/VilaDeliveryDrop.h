// Ponto de entrega ativo (porta do cliente), port de src/world/DeliveryDrop.gd.
// Some quando a entrega termina (AVilaWorld::RefreshDelivery).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VilaInteractable.h"

#include "VilaDeliveryDrop.generated.h"

class AVilaWorld;

UCLASS()
class VILAAURORA_API AVilaDeliveryDrop : public AActor, public IVilaInteractable
{
	GENERATED_BODY()

public:
	AVilaDeliveryDrop();

	void Setup(AVilaWorld* World, const FString& Label);

	// IVilaInteractable
	virtual FVector GetInteractPoint() const override { return GetActorLocation(); }
	virtual FString GetInteractTitle() const override { return Title; }
	virtual float GetInteractRadius() const override { return 220.f; }
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) override;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) override;
	virtual void SetFocused(bool bFocused) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;
	FString Title;
};
