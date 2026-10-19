// Carro do trânsito (port de src/vehicles/TrafficCar.gd), cinemático. Segue faixas e curvas
// do grafo, respeita semáforo e preferência, mantém distância, para para pedestres, buzina
// para quem atrapalha. Clientes do drive-thru desviam para a fila, fazem o pedido na janela
// e voltam à rua. Unidades em cm e cm/s.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "VilaTrafficCar.generated.h"

class UBoxComponent;
class UTextRenderComponent;
class UVilaCarVisual;
class AVilaTrafficSystem;
class AVilaWorld;

UCLASS()
class VILAAURORA_API AVilaTrafficCar : public AActor
{
	GENERATED_BODY()

public:
	AVilaTrafficCar();

	virtual void Tick(float DeltaSeconds) override;

	// O sistema chama logo depois de criar (equivale às variáveis definidas antes do _ready).
	void Init(AVilaTrafficSystem* InSys, AVilaWorld* InWorld, const FString& InCarId, const FString& InType, const FLinearColor& InColor, bool bInCustomer);
	void StartOnEdge(int32 EdgeIndex, const FVector& Pos);
	void OnHitByPlayer(float Impact);
	FString CurrentBark() const;  // "" sem balão

	FString CustomerId;  // car_id do Godot ("trafego_N"); é o id do cliente no DriveThruJob
	FString VType = TEXT("sedan");
	bool bCustomer = false;
	FString Mode = TEXT("edge");  // edge, turn, custom
	int32 Edge = -1;
	int32 NextEdge = -1;
	FString CurAxis;
	float Speed = 0.f;         // cm/s
	FString OrderState;        // "", "ordered", "leaving"
	float Damage = 0.f;
	float LengthCm = 440.f;

private:
	float LaneSpeedFor(const FString& M) const;
	void OnPointsEnd();
	void AtWindow();
	void Say(const FString& Text);
	void Face(const FVector& Dir, float Weight);
	void UpdateIndicator();

	UPROPERTY() TObjectPtr<UBoxComponent> Box;
	UPROPERTY() TObjectPtr<UVilaCarVisual> Visual;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BarkText;
	UPROPERTY() TObjectPtr<AVilaTrafficSystem> Sys;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;

	TArray<FVector> Points;  // cm, Z = 0
	int32 Idx = 0;
	bool bGranted = false;
	float StoppedT = 0.f;
	float HornCd = 0.f;
	float BarkT = 0.f;
	FString Bark;
	float BoxZ = 86.f;
	float VisualZ = 0.f;
	FLinearColor Color = FLinearColor::White;
};
