// Móveis e equipamentos com função real (port de src/world/Furniture.gd): camas,
// contêineres, fogão, pia, chuveiro, assentos, balcões de loja, estações do drive-thru,
// relógio de ponto, bomba de combustível, placas de venda, caixa de correio e o tapete do
// Marcos. O visual é montado pelo WorldBuilder (W1) e pendurado em GetVisualRoot().
// Origem no pé do móvel; o yaw vem do "rot" do JSON.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vila/Core.h"
#include "VilaInteractable.h"

#include "VilaFurniture.generated.h"

class AVilaVehicle;
class AVilaWorld;
class UTextRenderComponent;

UCLASS()
class VILAAURORA_API AVilaFurniture : public AActor, public IVilaInteractable
{
	GENERATED_BODY()

public:
	AVilaFurniture();

	void Setup(const vila::Json& Def, AVilaWorld* World, const FString& BuildingId);

	FString Kind, ItemId, OwnerId, ContainerId;
	vila::Json Def = vila::Json::object();
	float SeatHeightCm = 45.f;
	float Facing = 0.f;  // graus do Godot (o "rot" do JSON)
	FString Title;

	FVector Forward() const;
	FTransform SeatTransform() const;
	USceneComponent* GetVisualRoot() const { return VisualRoot; }
	// Item que o jogador pode cozinhar agora ("" se nenhum).
	FString PlayerHasCookable() const;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// IVilaInteractable
	virtual FVector GetInteractPoint() const override;
	virtual FString GetInteractTitle() const override { return Title; }
	virtual float GetInteractRadius() const override { return RadiusCm; }
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) override;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) override;
	virtual FString GetBuildingId() const override { return BuildingId; }
	virtual void SetFocused(bool bFocused) override;

private:
	bool IsMine() const;
	bool JobActionsAllowed() const;
	TArray<FVilaAction> SaleActions() const;
	TArray<FVilaAction> StationActions() const;
	float JobTime(const std::string& Key) const;
	double VehicleFuel(AVilaVehicle* V) const;  // combustível atual (via estado do núcleo)

	void Cook(const FString& Item);
	void ConsumeFx(const vila::Json& Effects, const FString& Sound);
	void Shower();
	void StationStart();
	void StationCollect();
	void Soda();
	void Fuel(AVilaVehicle* V);
	void UpdateStatusLabel();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY() TObjectPtr<UTextRenderComponent> StatusLabel;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;

	FString BuildingId;
	float RadiusCm = 160.f;
};
