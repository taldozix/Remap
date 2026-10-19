// Trânsito (port de src/vehicles/TrafficSystem.gd): grafo de faixas gerado das ruas do mapa
// (mão direita, como no Brasil), semáforo nos cruzamentos de "signals", preferência por ordem
// de chegada nos demais, carros que seguem o da frente, param para pedestres e buzinam.
// Também envia clientes para a fila do drive-thru durante o turno do jogador.
// Unidades: cm e cm/s (o Godot usava metros; as constantes foram convertidas).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vila/Core.h"

#include "VilaTrafficSystem.generated.h"

class AVilaWorld;
class UVilaWorldBuilder;
class AVilaTrafficCar;
class UStaticMeshComponent;
class UStaticMesh;
class USceneComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// Cruzamento do grafo ("x,z" do mapa).
struct FVilaTrafficNode
{
	FVector Pos = FVector::ZeroVector;  // cm, Z = 0
	bool bSignal = false;
	TWeakObjectPtr<AVilaTrafficCar> Occupant;
	float OccT = 0.f;
};

// Faixa dirigida de A para B, já deslocada para a direita e recuada da faixa de retenção.
struct FVilaTrafficEdge
{
	FString A;
	FString B;
	FVector Dir = FVector::ForwardVector;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FString Axis;  // "ew" | "ns"
};

// Obstáculo que os motoristas respeitam.
struct FVilaTrafficObstacle
{
	TWeakObjectPtr<AActor> Actor;
	FVector Pos = FVector::ZeroVector;  // cm, Z = 0
	float Half = 0.f;                   // cm
	float Width = 0.f;                  // cm
	bool bPlayer = false;
	bool bTraffic = false;              // é um AVilaTrafficCar
};

UCLASS()
class VILAAURORA_API AVilaTrafficSystem : public AActor
{
	GENERATED_BODY()

public:
	static constexpr float LaneSpeed = 1100.f;  // LANE_SPEED 11 m/s (roads.speed_limit)
	static constexpr float TurnSpeed = 550.f;   // TURN_SPEED 5.5 m/s
	static constexpr float Green = 16.f;        // s
	static constexpr float Yellow = 3.f;        // s
	static constexpr float StopDist = 820.f;    // WorldBuilder.STOP_DIST 8.2 m

	AVilaTrafficSystem();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Setup(AVilaWorld* World, const vila::Json& Map);
	UPROPERTY() TArray<TObjectPtr<AVilaTrafficCar>> Cars;
	FString LightState(const FString& Axis) const;   // "green", "yellow" ou "red"
	bool NearNode(const FVector& P, float RadiusCm) const;
	AVilaTrafficCar* SpawnCustomer();                 // cliente do drive-thru
	void RemoveCar(AVilaTrafficCar* Car);
	AVilaTrafficCar* SpawnRandom(bool bInitial = false);

	// ------------------------------------------------ grafo (usado pelos carros)
	TMap<FString, FVilaTrafficNode> Nodes;
	TArray<FVilaTrafficEdge> Edges;
	TMap<FString, TArray<int32>> OutEdges;
	TArray<FVector> DrivePath;  // cm, Z = 0
	int32 WindowIndex = 9;
	int32 JoinEdge = -1;
	float LaneSpeedCm = LaneSpeed;

	int32 FindEdge(const FString& A, const FString& B) const;
	int32 ChooseNext(int32 EdgeIndex);
	// Curva entre o fim de uma faixa e o início da próxima (Bézier quadrática, 8 pontos).
	TArray<FVector> Connector(int32 E1, int32 E2) const;
	bool CanEnter(AVilaTrafficCar* Car, const FString& NodeKey, const FString& Axis, float DistCm);
	void Release(AVilaTrafficCar* Car, const FString& NodeKey);
	// Outros carros, veículos do jogador/NPCs, o jogador a pé e os NPCs visíveis (cache por quadro).
	const TArray<FVilaTrafficObstacle>& Obstacles();
	static FString Key(double X, double Z);

private:
	void AddEdgePair(const FString& A, const FString& B, float OffCm);
	void BuildSignal(const FVector& Center);
	void SetupLamps();
	void UpdateLamps();
	void UpdateDensity();
	bool IsClear(const FVector& Pos, float RadiusCm) const;
	AVilaTrafficCar* SpawnCar(int32 EdgeIndex, const FVector& Pos, bool bCustomer);
	bool PlayerPos(FVector& Out) const;
	UStaticMeshComponent* AddPart(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& T, UMaterialInterface* Mat);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;
	// 6 materiais das lâmpadas: índice = eixo (0 ew, 1 ns) * 3 + cor (0 vermelho, 1 amarelo, 2 verde)
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMIDs;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> SignalParts;  // semáforos próprios (sem os do builder)
	UPROPERTY() TObjectPtr<UVilaWorldBuilder> SignalBuilder;      // lâmpadas montadas pelo W1 (SetSignalStates)
	TArray<FVector> SignalCenters;
	FString LampState[2];
	bool bRealEmissive = true;

	float Phase = 0.f;
	int32 TargetCount = 7;
	float SpawnT = 0.f;
	int32 NextId = 1;
	vila::Rng Rng;
	int32 SettingsConn = 0;

	TArray<FVilaTrafficObstacle> ObstacleCache;
	uint64 ObstacleFrame = MAX_uint64;
};
