// Câmera em três-quartos (port de CameraRig.gd): segue o jogador ou o veículo, gira em passos de
// 45° e tem zoom. SpringArm sem teste de colisão (as paredes da frente são cortadas pelo
// AVilaBuilding::SetCut, como no Godot) + câmera com profundidade de campo de miniatura
// (graphics.tilt_shift: 0 desligada, 1 sutil, 2 forte). No menu principal, orbita a praça.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "VilaCameraRig.generated.h"

class UCameraComponent;
class USpringArmComponent;

UCLASS()
class VILAAURORA_API AVilaCameraRig : public AActor
{
	GENERATED_BODY()

public:
	AVilaCameraRig();

	UPROPERTY() TObjectPtr<AActor> Target;
	void Snap();
	FVector ViewDir() const;      // direção câmera -> alvo, no plano XY, normalizada
	float MoveYaw() const;        // yaw (graus UE) usado para "cima" no input de movimento
	UCameraComponent* GetCamera() const;
	void SetMenuOrbit(const FVector& Center, float RadiusCm);  // modo menu principal

	// Entrada (o AVilaPlayerController chama): cam_left = +1, cam_right = -1.
	void RotateStep(int32 Dir);
	// zoom_in = +1 (aproxima), zoom_out = -1.
	void ZoomStep(int32 Dir);
	// Distância desejada (cm). O diálogo com NPC importante aproxima para 11 m e depois restaura.
	float GetZoomGoal() const { return DistGoal; }
	void SetZoomGoal(float Cm) { DistGoal = FMath::Clamp(Cm, MinDist, MaxDist); }
	float GetDistance() const { return Distance; }
	bool IsMenuOrbit() const { return bMenuOrbit; }

	static constexpr float MinDist = 900.f;
	static constexpr float MaxDist = 4200.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Alvo efetivo (o veículo quando o jogador dirige) e ponto de foco (cm).
	AActor* ResolveFocus(FVector& OutFocus) const;
	void UpdateView(float Dt);
	void ApplyLens();
	void ReadSettings();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USpringArmComponent> Arm;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	// Yaw da direção de visão (graus UE). Godot: yaw = -35° -> UE -90 - (-35) = -55.
	float Yaw = -55.f;
	float YawGoal = -55.f;
	float Pitch = -52.f;
	float Distance = 2100.f;
	float DistGoal = 2100.f;

	bool bMenuOrbit = false;
	FVector OrbitCenter = FVector::ZeroVector;
	float OrbitRadius = 3000.f;

	int32 TiltShift = 1;
	float Sensitivity = 1.f;
	int32 SettingsConn = 0;
};
