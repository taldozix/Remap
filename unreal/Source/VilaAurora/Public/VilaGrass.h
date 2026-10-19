// Grama densa em tufos (port de GrassField.gd): HISM em blocos de 20 m com distância de
// corte. Uma grade de ocupação (0,5 m) marca ruas, calçadas, prédios, trilhas e praças,
// onde não nasce grama. A densidade vem de graphics.grass_density (0 desligada … 3 alta).
//
// O UVilaWorldBuilder cria este ator no fim do Build (UVilaWorldBuilder::Grass) e ele
// mesmo se reconstrói quando a opção muda (Settings::changed("graphics")), como o
// GameWorld._on_settings_changed do Godot.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "VilaGrass.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UVilaWorldBuilder;

UCLASS()
class VILAAURORA_API AVilaGrass : public AActor
{
	GENERATED_BODY()

public:
	AVilaGrass();

	static constexpr float CELL = 0.5f;    // m
	static constexpr float CHUNK = 20.f;   // m

	// Monta a grama para o nível (0..3). Guarda o builder para os Rebuild seguintes.
	void Build(UVilaWorldBuilder* InBuilder, int32 InLevel);
	void Rebuild(int32 InLevel);

	int32 GetLevel() const { return Level; }
	int32 Instances = 0;

	// Célula ocupada (sem grama) nas coordenadas do Godot (metros, x/z).
	bool Blocked(float X, float Z) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Rasterize();
	void MarkRect(double X0, double Z0, double X1, double Z1, double Margin = 0.0);
	void MarkSegment(const FVector2D& P0, const FVector2D& P1, float HalfWidth);
	void Clear();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UVilaWorldBuilder> Builder;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Chunks;

	TArray<uint8> Grid;
	FVector2D Origin = FVector2D::ZeroVector;
	int32 GW = 0;
	int32 GH = 0;
	int32 Level = -1;
	int32 SettingsConn = 0;
};
