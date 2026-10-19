// Porta com dobradiça, trancas, chaves e horário de funcionamento (lojas), port de
// src/world/Door.gd. NPCs com acesso abrem sozinhos ao se aproximar; o estado é salvo em
// WorldState. O builder (W1) cria o ator no vão: origem embaixo, no centro da abertura,
// +X para fora do prédio. A folha gira em torno de uma dobradiça em Y = -largura/2.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vila/Core.h"
#include "VilaInteractable.h"

#include "VilaDoor.generated.h"

class AVilaWorld;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class VILAAURORA_API AVilaDoor : public AActor, public IVilaInteractable
{
	GENERATED_BODY()

public:
	AVilaDoor();

	void Setup(const vila::Json& Def, AVilaWorld* World, const FString& BuildingId, float WallThicknessCm, const FLinearColor& Color);

	FString DoorId;
	vila::Json Def = vila::Json::object();
	float WidthCm = 110.f;
	float HeightCm = 215.f;
	bool bRollup = false;

	bool IsLocked() const;
	bool IsOpen() const { return bIsOpen; }
	void SetOpen(bool bOpen, bool bInstant = false);
	bool PlayerHasKey() const;
	// Chave "legítima" (dono/inquilino/funcionário), em oposição a uma chave achada.
	bool PlayerHasKeyByRight() const;
	bool NpcHasKey(const FString& NpcId) const;

	virtual void Tick(float DeltaSeconds) override;

	// IVilaInteractable
	virtual FVector GetInteractPoint() const override { return GetActorLocation(); }
	virtual FString GetInteractTitle() const override { return Title; }
	virtual float GetInteractRadius() const override { return 200.f; }
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) override;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) override;
	virtual FString GetBuildingId() const override { return BuildingId; }
	virtual void SetFocused(bool bFocused) override;

private:
	bool WithinHours() const;
	// Abre para o lado oposto de quem empurra (evita bater no jogador).
	void SetSwingFrom(const FVector& P);
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FVector& CenterCm, const FVector& SizeCm, UMaterialInterface* Material);
	void ApplyPivot();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> Pivot;
	UPROPERTY() TObjectPtr<UBoxComponent> Collision;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;

	FString BuildingId;
	FString Title;
	bool bIsOpen = false;
	float Current = 0.f;  // graus (dobradiça) ou cm (portão de enrolar)
	float Target = 0.f;
	float NpcHold = 0.f;
	bool bPlayerOpened = false;
	float CheckT = 0.f;
	float SwingSign = 1.f;
	bool bSetup = false;
};
