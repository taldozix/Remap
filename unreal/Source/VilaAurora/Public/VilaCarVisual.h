// Visual dos carros (port de src/vehicles/CarVisual.gd). Usado pelo AVilaVehicle e pelo
// AVilaTrafficCar. Monta o modelo importado (/Game/Vila/Vehicles/<modelo>/SM_<Parte>,
// posições/pivôs em Content/Data/art/vehicles_layout.json) ou, sem conteúdo, um carro
// procedural com as formas básicas do motor. Origem no chão, centro do comprimento,
// frente = +X. Expõe rodas (giram/esterçam), porta do motorista, luzes, setas e fumaça.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "VilaCarVisual.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UVilaAssets;

UCLASS(ClassGroup = (Vila))
class VILAAURORA_API UVilaCarVisual : public USceneComponent
{
	GENERATED_BODY()

public:
	UVilaCarVisual();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Kind: hatch | sedan | pickup | van (vehicles.json "kind"). Tamanhos em metros (vehicles.json "size").
	void Build(const FString& InKind, float WidthM, float HeightM, float LengthM, const FLinearColor& InPaint);
	// SpeedCm: cm/s com sinal. SteerRad: ângulo da direção como no Godot (positivo = esquerda).
	void Animate(float SpeedCm, float SteerRad, float DeltaTime);
	void SetLights(bool bHead, bool bBraking);
	// Abre/fecha a porta do motorista (0,25 s, como o tween do Godot).
	void SetDoor(bool bOpen);
	// Seta piscando: -1 esquerda, 0 desligada, 1 direita. No modelo importado o material
	// "indicator" é um só, então pisca dos dois lados.
	void SetIndicator(int32 Dir);
	void SetSmoke(bool bOn);
	bool IsSmoking() const { return bSmoke; }
	// Contorno de foco (Custom Depth, stencil 1).
	void SetCustomDepth(bool bOn);

	bool HasImportedModel() const { return bImported; }
	float GetWheelRadius() const { return WheelRadius; }  // cm
	const FString& GetKind() const { return Kind; }

private:
	void Clear();
	bool BuildImported(UVilaAssets* Assets);
	void BuildProcedural(UVilaAssets* Assets);
	void BuildSmoke(UVilaAssets* Assets);
	USceneComponent* AddPivot(USceneComponent* Parent, const FTransform& T);
	UStaticMeshComponent* AddMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& T, UMaterialInterface* Mat = nullptr);
	UStaticMeshComponent* AddBox(USceneComponent* Parent, const FVector& Center, const FVector& SizeCm, UMaterialInterface* Mat);
	void AssignSlotMaterials(UStaticMeshComponent* C);
	void SetLightEnergy(UMaterialInstanceDynamic* MID, const FLinearColor& Base, float Energy, float& Cache);
	void ApplyIndicators(bool bLit);

	UPROPERTY() TObjectPtr<USceneComponent> Model;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Created;      // tudo que foi criado (para Clear)
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Meshes;  // malhas visíveis (contorno)
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> WheelSpin;    // pivô que gira
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> FrontSteer;   // pivô que esterça
	UPROPERTY() TObjectPtr<USceneComponent> DoorPivot;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PaintMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HeadMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TailMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> IndicatorLMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> IndicatorRMID;  // == IndicatorLMID no modelo importado
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Puffs;

	FString Kind = TEXT("sedan");
	FLinearColor Paint = FLinearColor::Gray;
	float LengthCm = 440.f;
	float WidthCm = 180.f;
	float HeightCm = 140.f;
	float WheelRadius = 32.f;
	bool bImported = false;
	bool bRealEmissive = true;  // false: material de cor sólida (sem parâmetro Intensity)

	float SpinRad = 0.f;
	float DoorAngle = 0.f;
	float DoorTarget = 0.f;
	int32 IndicatorDir = 0;
	float BlinkT = 0.f;
	bool bIndicatorLit = false;
	float HeadCache = -1.f;
	float TailCache = -1.f;
	float IndLCache = -1.f;
	float IndRCache = -1.f;

	bool bSmoke = false;
	float PuffSpawnT = 0.f;
	int32 NextPuff = 0;
	FVector SmokeOrigin = FVector::ZeroVector;
	TArray<float> PuffAge;
	TArray<FVector> PuffVel;
	TArray<float> PuffSize;
};
