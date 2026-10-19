// Folhagem e materiais da natureza (port de Nature.gd + FoliageBatcher.gd).
// As copas são maços de cartões de folhas (malha procedural do UVilaProcMesh, material
// M_Leaf com recorte alfa) instanciados em HISM com a tinta por instância. Sem o M_Leaf,
// cada maço vira uma esfera colorida ("bolha" de copa).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Vila/Core.h"

#include "VilaNature.generated.h"

class UMaterialInterface;
class USceneComponent;
class UVilaAssets;
class UVilaMeshBatch;

UCLASS()
class VILAAURORA_API UVilaNature : public UObject
{
	GENERATED_BODY()

public:
	void Init(UVilaAssets* InAssets);

	// FoliageBatcher.add_canopy: `TrunkTopG` e os maços (centro, raio) em metros no espaço do
	// Godot (x, y para cima, z). `Tint` é a cor das folhas (valores crus do Godot).
	void AddCanopy(const FVector& TrunkTopG, const TArray<TPair<FVector, FVector>>& ClustersG, const FLinearColor& Tint,
		vila::Rng& Rng, int32 CardsPerCluster = 46, float CardSize = 1.f);

	// Cria os componentes de folhagem presos a `Parent`.
	void Build(USceneComponent* Parent);

	int32 NumClusters() const { return Clusters.Num(); }

	// ------------------------------------------------ materiais (Nature.gd)
	static UMaterialInterface* LeafMaterial(UVilaAssets* Assets);                  // nullptr sem o M_Leaf
	static UMaterialInterface* GrassMaterial(UVilaAssets* Assets, bool& bOutTinted); // M_Grass ou cor
	static UMaterialInterface* DirtMaterial(UVilaAssets* Assets);                  // terra batida das trilhas
	static UMaterialInterface* GroundMaterial(UVilaAssets* Assets);                // gramado do chão (Mat.grass)

private:
	struct FCluster
	{
		FVector Center;  // UE, cm
		FVector Radii;   // UE, cm (X, Y, Z)
		FLinearColor Tint;
		int32 Cards = 46;
		float Ratio = 0.5f;
		int32 Variant = 0;
		float Yaw = 0.f;
	};
	TArray<FCluster> Clusters;
	UPROPERTY() TObjectPtr<UVilaAssets> Assets;
};
