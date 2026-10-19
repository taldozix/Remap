#include "VilaNature.h"

#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "VilaAssets.h"
#include "VilaMeshBatch.h"
#include "VilaProcMesh.h"

namespace
{
	float ToLin(float C) { return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f); }
	FLinearColor SRGB(float R, float G, float B) { return FLinearColor(ToLin(R), ToLin(G), ToLin(B), 1.f); }
	FVector G2U(const FVector& G) { return FVector(G.X * 100.0, G.Z * 100.0, G.Y * 100.0); }
}

void UVilaNature::Init(UVilaAssets* InAssets)
{
	Assets = InAssets;
	Clusters.Reset();
}

void UVilaNature::AddCanopy(const FVector& TrunkTopG, const TArray<TPair<FVector, FVector>>& ClustersG, const FLinearColor& Tint,
	vila::Rng& Rng, int32 CardsPerCluster, float CardSize)
{
	for (const TPair<FVector, FVector>& C : ClustersG)
	{
		const FVector R = C.Value;
		const double AvgR = FMath::Max((R.X + R.Y + R.Z) / 3.0, 0.05);
		FCluster Cl;
		Cl.Center = G2U(C.Key);
		Cl.Radii = FVector(R.X, R.Z, R.Y) * 100.0;
		Cl.Tint = Tint;
		Cl.Cards = CardsPerCluster;
		// tamanho médio do cartão (0,55..0,9 x card_size) relativo ao raio do maço
		Cl.Ratio = static_cast<float>(CardSize / AvgR);
		Cl.Variant = static_cast<int32>(Rng.randi() % 3);
		Cl.Yaw = static_cast<float>(Rng.randf() * 360.0);
		Clusters.Add(Cl);
	}
}

void UVilaNature::Build(USceneComponent* Parent)
{
	if (!Assets || !Parent || Clusters.Num() == 0) return;
	UVilaMeshBatch* Batch = NewObject<UVilaMeshBatch>(this);
	Batch->Init(Assets, 0.85f, true);
	Batch->Mobility = EComponentMobility::Static;
	UMaterialInterface* Leaf = LeafMaterial(Assets);
	UGameInstance* GI = Assets->GetGameInstance();
	UVilaProcMesh* Proc = GI ? GI->GetSubsystem<UVilaProcMesh>() : nullptr;
	for (const FCluster& C : Clusters)
	{
		const FQuat Rot = FRotator(0.f, C.Yaw, 0.f).Quaternion();
		UStaticMesh* Mesh = (Leaf && Proc) ? Proc->LeafCluster(C.Variant, C.Cards, C.Ratio) : nullptr;
		if (Mesh)
		{
			// A malha tem raio de 100 cm: escala = raio em metros.
			Batch->AddMesh(Mesh, FTransform(Rot, C.Center, C.Radii / 100.0), C.Tint, FString(), Leaf, true);
		}
		else
		{
			// Sem o material de folhas: bolha de copa (esfera um pouco menor que o maço).
			Batch->AddSphere(C.Center, Rot, C.Radii * 0.9, C.Tint, FString());
		}
	}
	Batch->Flush(Parent, TEXT("Foliage"));
	Clusters.Reset();
}

UMaterialInterface* UVilaNature::LeafMaterial(UVilaAssets* InAssets)
{
	return InAssets ? InAssets->Master(TEXT("M_Leaf")) : nullptr;
}

UMaterialInterface* UVilaNature::GrassMaterial(UVilaAssets* InAssets, bool& bOutTinted)
{
	bOutTinted = false;
	if (!InAssets) return nullptr;
	if (UMaterialInterface* G = InAssets->Master(TEXT("M_Grass")))
	{
		bOutTinted = true;  // o M_Grass pode ler a PerInstanceCustomData (tom/secura de cada tufo)
		return G;
	}
	// Meio-termo entre root_color e tip_color do shader de grama do Godot.
	const FLinearColor Mid = SRGB(0.17f, 0.225f, 0.09f);
	if (InAssets->HasContent())
	{
		bOutTinted = true;
		return InAssets->Color(Mid, 0.9f, 0.f, true);
	}
	return InAssets->Color(Mid, 0.9f);
}

UMaterialInterface* UVilaNature::DirtMaterial(UVilaAssets* InAssets)
{
	return InAssets ? InAssets->Surface(TEXT("dirt")) : nullptr;
}

UMaterialInterface* UVilaNature::GroundMaterial(UVilaAssets* InAssets)
{
	// Mat.grass(): verde escuro entre (0.12, 0.17, 0.07) e (0.2, 0.25, 0.11). Sobre a textura
	// "ground" (marrom), a tinta puxa para esse verde.
	return InAssets ? InAssets->Surface(TEXT("ground"), FLinearColor(0.33f, 0.71f, 0.42f)) : nullptr;
}
