// Constrói o bairro a partir de Content/Data/map/*.json (port de WorldBuilder.gd): chão,
// ruas, calçadas, marcações, áreas, prédios (paredes com corte, telhado, piso, portas,
// janelas, placas), mobília funcional, árvores, postes, semáforos e mobiliário urbano.
// No fim, cria a grama (AVilaGrass) e pede a reconstrução da navmesh.
//
// A geometria vira HISM (UVilaMeshBatch) presos a:
//   - AVilaWorldStatic (rua, chão, mobiliário urbano, folhagem, colisores invisíveis, luzes);
//   - AVilaBuilding (um por prédio: paredes por lado, telhado, interior, luz interna);
//   - AVilaFurniture::GetVisualRoot() (visual de cada móvel/objeto com função).
// As colisões seguem as do Godot (caixas e cilindros invisíveis, BlockAll exceto câmera).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "Vila/Core.h"

#include "VilaWorldBuilder.generated.h"

class AVilaBuilding;
class AVilaDoor;
class AVilaFurniture;
class AVilaGrass;
class AVilaWorld;
class ULocalLightComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UVilaAssets;
class UVilaMeshBatch;
class UVilaNature;
class UVilaWorldBuilder;

USTRUCT()
struct VILAAURORA_API FVilaSpot
{
	GENERATED_BODY()

	UPROPERTY() FString Id;
	UPROPERTY() FVector Location = FVector::ZeroVector;  // cm, no chão
	UPROPERTY() float Yaw = 0.f;                          // graus UE (já convertido)
	UPROPERTY() FString Pose;                             // "stand", "sit", "lie", "eat"... (como no Godot)
	UPROPERTY() bool bHidden = false;
	UPROPERTY() FString BuildingId;
	UPROPERTY() TWeakObjectPtr<AVilaFurniture> Furniture; // móvel associado, se houver
};

// Dono da geometria estática fora dos prédios. Também anima o que o Godot fazia pelo
// material global: cabeças de poste acesas à noite e sombras só nos postes perto da câmera.
UCLASS()
class VILAAURORA_API AVilaWorldStatic : public AActor
{
	GENERATED_BODY()

public:
	AVilaWorldStatic();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	TWeakObjectPtr<UVilaWorldBuilder> Builder;

private:
	float Accum = 0.f;
};

UCLASS()
class VILAAURORA_API UVilaWorldBuilder : public UObject
{
	GENERATED_BODY()

public:
	static constexpr double WALL_T = 0.2;      // m
	static constexpr double DOOR_H = 2.15;     // m
	static constexpr double SIDEWALK_H = 0.12; // m
	static constexpr double STOP_DIST = 8.2;   // distância do centro do cruzamento até a faixa de retenção

	// Monta toda a geometria da vila, gera colisão e pede a navmesh no fim.
	void Build(AVilaWorld* World, const vila::Json& Map);
	// Altura do piso (calçadas são 12 cm mais altas), em cm, no ponto (X, Y) do UE. Usada pelo
	// visual de pessoas e carros, como no Godot (as calçadas não têm colisão).
	float GroundHeight(float X, float Y) const;

	const vila::Json& GetMap() const { return Map; }
	AVilaWorld* GetVilaWorld() const { return VilaWorld; }

	UPROPERTY() TMap<FString, FVilaSpot> Spots;
	UPROPERTY() TMap<FString, TObjectPtr<AVilaBuilding>> Buildings;
	UPROPERTY() TMap<FString, TObjectPtr<AVilaDoor>> Doors;
	UPROPERTY() TMap<FString, TObjectPtr<AVilaFurniture>> Furniture;  // id do móvel e id do contêiner
	UPROPERTY() TArray<TObjectPtr<ULocalLightComponent>> Streetlights;  // DayNight liga/desliga (visibilidade)
	TArray<FBox2D> SidewalkRects;                 // cm (UE XY), usados por NPCs e grama

	// ------------------------------------------------ semáforos (o trânsito anima)
	// Uma entrada por lâmpada (3 por grupo focal). SignalAxis: "x" = via leste-oeste (Godot "ew"),
	// "z" = via norte-sul (Godot "ns"); SignalAxisGodot guarda o nome original. SignalColor:
	// "red" / "yellow" / "green". Use SetSignalLamp para acender/apagar (cuida do material).
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SignalLamps;
	TArray<FString> SignalAxis;
	TArray<FString> SignalAxisGodot;
	TArray<FString> SignalColor;
	void SetSignalLamp(int32 Index, bool bOn);
	// Atalho: acende cada lâmpada conforme o estado do eixo ("ew"/"x" e "ns"/"z").
	void SetSignalStates(const FString& StateEW, const FString& StateNS);

	// ------------------------------------------------ outros
	UPROPERTY() TObjectPtr<AVilaGrass> Grass;
	UPROPERTY() TObjectPtr<AVilaWorldStatic> StaticActor;

	// Cabeças de poste (Mat.lamps): emissão = noite x 4. Chamado pelo AVilaWorldStatic.
	void UpdateNight(float Night);
	// Sombras só nos `Count` postes acesos mais perto de `ViewPoint` (economia com Lumen/VSM).
	void UpdateStreetlightShadows(const FVector& ViewPoint, int32 Count = 4);

	// Estado interno da montagem (usado por VilaWB::FVilaWorldBuild em VilaWorldBuilder.cpp).
	// Público em vez de `friend` para o cabeçalho refletido não ter namespaces (UHT).

	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;
	UPROPERTY() TObjectPtr<UVilaAssets> Assets;
	UPROPERTY() TArray<TObjectPtr<UVilaMeshBatch>> Batches;  // vivos só durante o Build
	UPROPERTY() TObjectPtr<UVilaNature> Foliage;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LampMID;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SignalMIDs;
	TArray<FLinearColor> SignalBaseColors;  // linear
	int32 LampMode = 0;    // 0 cor pura, 1 M_Emissive
	int32 SignalMode = 0;
	float LastNight = -1.f;
	TArray<bool> StreetShadow;
	vila::Json Map;
};
