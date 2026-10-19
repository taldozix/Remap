// Veículo dirigível (e estacionado). Port de src/vehicles/Vehicle.gd: física arcade em
// modelo bicicleta (aceleração, frenagem, ré, inércia, derrapagem com freio de mão),
// colisões com dano, consumo de combustível, buzina, faróis e atropelamento. Cinemático:
// move com SetActorLocation + sweep e desliza nas paredes (sem Chaos Vehicle).
// Estado salvo em vila::game().world_state.vehicles[id].
//
// Origem do ator = centro da caixa de colisão (que começa 30 cm acima do chão, como no
// Godot). O visual (UVilaCarVisual) fica com a origem no chão. GetGroundLocation() dá a
// posição no chão.
#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Pawn.h"
#include "VilaInteractable.h"
#include "Vila/Core.h"

#include "VilaVehicle.generated.h"

class UBoxComponent;
class USpotLightComponent;
class UAudioComponent;
class UVilaCarVisual;
class AVilaWorld;
class AVilaPlayerCharacter;
struct FHitResult;

UCLASS()
class VILAAURORA_API AVilaVehicle : public APawn, public IVilaInteractable
{
	GENERATED_BODY()

public:
	AVilaVehicle();

	virtual void Tick(float DeltaSeconds) override;

	// Lê o estado em vila::game().world_state.vehicles[id] (tipo, cor, combustível, dano),
	// dimensiona a colisão, monta o visual e posiciona o ator em "pos"/"rot" do estado.
	void Setup(const FString& InVehicleId, AVilaWorld* InWorld);

	FString VehicleId;
	float Speed = 0.f;     // cm/s, com sinal (ré negativa)
	float Fuel = 0.f;      // litros
	float Damage = 0.f;    // 0..100
	float LatSpeed = 0.f;  // m/s (derrapagem lateral, + = direita)
	float SteerRad = 0.f;  // esterço como no Godot (rad, + = esquerda)
	vila::Json Def;        // data/vehicles.json do tipo

	UPROPERTY() TObjectPtr<AVilaPlayerCharacter> Driver;

	FString DisplayName() const;
	float FuelCapacity() const;
	FString OwnerId() const;
	bool IsPlayers() const;
	float MaxSpeed() const;  // m/s, já com a perda por dano
	void SetDriver(AVilaPlayerCharacter* D);  // nullptr = desce
	// Ponto livre no chão para o jogador descer (cm).
	FVector ExitPosition() const;
	FVector GetGroundLocation() const;
	TArray<FVilaAction> GetDriverActions(AVilaPlayerCharacter* Player);
	// Throttle: -1 (freio/ré) … +1 (acelerar). Steer: -1 esquerda … +1 direita.
	// O controller chama a cada quadro enquanto o jogador dirige.
	void SetDriveInput(float Throttle, float Steer, bool bHandbrake);
	void Horn();
	void SaveState();
	void RefreshFromState();
	// Conserto feito pelo próprio jogador (Mecânica aumenta; garagem própria rende 50% a mais).
	float RepairAmount() const;
	UVilaCarVisual* GetVisual() const;

	// IVilaInteractable
	virtual FVector GetInteractPoint() const override;
	virtual FString GetInteractTitle() const override;
	virtual float GetInteractRadius() const override { return 280.f; }  // Vehicle.radius = 2.8 m
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) override;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) override;
	virtual void SetFocused(bool bFocused) override;

protected:
	virtual void BeginPlay() override;

private:
	void MoveWithSlide(const FVector& Delta, TArray<FHitResult>& OutHits);
	void HitPedestrians(double& SpeedM, const FVector& Fwd, const FVector& Right);
	void OnCrash(double Impact, AActor* Other);
	void UpdateDamageFx();
	void UpdateHeadlights(bool bOn);
	void UpdateGround(float DeltaSeconds);
	void RepairWithKit();
	void BreakIn();
	vila::Json& State() const;

	UPROPERTY() TObjectPtr<UBoxComponent> Box;
	UPROPERTY() TObjectPtr<UVilaCarVisual> Visual;
	UPROPERTY() TObjectPtr<USpotLightComponent> HeadlightL;
	UPROPERTY() TObjectPtr<USpotLightComponent> HeadlightR;
	UPROPERTY() TObjectPtr<UAudioComponent> EngineSound;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;

	float WidthM = 1.8f;
	float HeightM = 1.4f;
	float LengthM = 4.2f;
	float BoxZ = 86.f;     // altura do centro da caixa acima do chão (cm)
	float VisualZ = 0.f;   // altura do chão sob o carro (cm), suavizada
	float HitCd = 0.f;
	double DistAcc = 0.0;  // metros
	bool bHeadlightsOn = false;

	float InThrottle = 0.f;
	float InSteer = 0.f;
	bool bInHandbrake = false;
	double InputTime = -100.0;

	FTimerHandle DoorTimer;
};
