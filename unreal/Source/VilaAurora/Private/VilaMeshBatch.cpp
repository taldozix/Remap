#include "VilaMeshBatch.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Vila/Settings.h"
#include "VilaAssets.h"
#include "VilaProcMesh.h"

namespace VilaMeshBatchPriv
{
	// Cor sRGB (como o albedo do Godot) -> linear.
	float ToLin(float C) { return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f); }
	FLinearColor SRGB(float R, float G, float B) { return FLinearColor(ToLin(R), ToLin(G), ToLin(B), 1.f); }
}

void UVilaMeshBatch::Init(UVilaAssets* InAssets, float InRoughness, bool bInShadows)
{
	Assets = InAssets;
	Roughness = InRoughness;
	bShadows = bInShadows;
	CullScale = ViewDistanceScale();
}

float UVilaMeshBatch::ViewDistanceScale()
{
	const int32 V = vila::to_int(vila::settings().get_value("graphics", "view_distance"), 2);
	static const float Scales[3] = {0.6f, 1.f, 1.6f};
	return Scales[FMath::Clamp(V, 0, 2)];
}

float UVilaMeshBatch::CullForSize(float MaxDimCm, float Scale)
{
	if (MaxDimCm >= 400.f) return 0.f;          // peças grandes: sempre visíveis
	if (MaxDimCm < 60.f) return 5000.f * Scale;  // miudezas: 50 m
	return 15000.f * Scale;                      // médias: 150 m
}

UMaterialInterface* UVilaMeshBatch::ResolveMaterial(UVilaAssets* InAssets, const FString& MatKey, const FLinearColor& Tint, float InRoughness, bool bAllowInstanceTint, bool& bOutTinted)
{
	bOutTinted = false;
	if (!InAssets) return nullptr;
	const bool bTint = bAllowInstanceTint && InAssets->HasContent();
	if (MatKey == TEXT("glass")) return InAssets->Glass(VilaMeshBatchPriv::SRGB(0.55f, 0.68f, 0.76f), 0.4f);
	if (MatKey == TEXT("water"))
	{
		if (UMaterialInterface* W = InAssets->Master(TEXT("M_Water"))) return W;
		return InAssets->Glass(VilaMeshBatchPriv::SRGB(0.16f, 0.26f, 0.3f), 0.85f);
	}
	if (MatKey.IsEmpty())
	{
		if (bTint)
		{
			bOutTinted = true;
			return InAssets->Color(FLinearColor::White, InRoughness, 0.f, true);
		}
		return InAssets->Color(Tint, InRoughness);
	}
	if (bTint)
	{
		bOutTinted = true;
		return InAssets->Surface(MatKey, FLinearColor::White, true);
	}
	return InAssets->Surface(MatKey, Tint);
}

void UVilaMeshBatch::AddMesh(UStaticMesh* Mesh, const FTransform& Xf, const FLinearColor& Tint, const FString& MatKey, UMaterialInterface* Explicit, bool bExplicitTint)
{
	if (!Mesh) return;
	UMaterialInterface* Mat = nullptr;
	bool bTinted = false;
	FString Key = FString::Printf(TEXT("%p|"), Mesh);
	if (bHidden)
	{
		Key += TEXT("hidden");
	}
	else if (MaterialOverride)
	{
		Mat = MaterialOverride;
		Key += TEXT("override");
	}
	else if (Explicit)
	{
		Mat = Explicit;
		bTinted = bExplicitTint;
		Key += FString::Printf(TEXT("explicit:%p|%d"), Explicit, bExplicitTint ? 1 : 0);
	}
	else
	{
		Mat = ResolveMaterial(Assets, MatKey, Tint, Roughness, true, bTinted);
		Key += MatKey;
		if (!bTinted) Key += TEXT("|") + FString::Printf(TEXT("%p"), Mat);
	}
	float Cull = 0.f;
	if (bSizeCull && !bHidden)
	{
		const FVector Ext = Mesh->GetBounds().BoxExtent * Xf.GetScale3D().GetAbs() * 2.0;
		Cull = CullForSize(static_cast<float>(Ext.GetMax()), CullScale);
		Key += FString::Printf(TEXT("|c%d"), FMath::RoundToInt(Cull));
	}
	FVilaMeshGroup& G = Groups.FindOrAdd(Key);
	if (!G.Mesh)
	{
		G.Mesh = Mesh;
		G.Material = Mat;
		G.bTinted = bTinted;
		G.CullDistance = Cull;
	}
	G.Transforms.Add(Xf);
	G.Tints.Add(Tint);
	++Count;
}

void UVilaMeshBatch::AddBox(const FVector& Center, const FQuat& Rot, const FVector& SizeCm, const FLinearColor& Tint, const FString& MatKey, UMaterialInterface* Explicit)
{
	if (!Assets) return;
	AddMesh(Assets->Cube(), FTransform(Rot, Center, SizeCm / 100.0), Tint, MatKey, Explicit);
}

void UVilaMeshBatch::AddCylinder(const FVector& Center, const FQuat& Rot, float RadiusCm, float HeightCm, const FLinearColor& Tint, const FString& MatKey)
{
	if (!Assets) return;
	AddMesh(Assets->Cylinder(), FTransform(Rot, Center, FVector(RadiusCm * 2.0 / 100.0, RadiusCm * 2.0 / 100.0, HeightCm / 100.0)), Tint, MatKey);
}

void UVilaMeshBatch::AddTube(const FVector& A, const FVector& B, float RadiusCm, const FLinearColor& Tint, const FString& MatKey)
{
	const FVector D = B - A;
	const double Len = D.Size();
	if (Len < 0.01) return;
	const FQuat Rot = FRotationMatrix::MakeFromZ(D / Len).ToQuat();
	AddCylinder((A + B) * 0.5, Rot, RadiusCm, static_cast<float>(Len), Tint, MatKey);
}

void UVilaMeshBatch::AddSphere(const FVector& Center, const FQuat& Rot, const FVector& RadiiCm, const FLinearColor& Tint, const FString& MatKey)
{
	if (!Assets) return;
	AddMesh(Assets->Sphere(), FTransform(Rot, Center, RadiiCm * 2.0 / 100.0), Tint, MatKey);
}

void UVilaMeshBatch::AddQuad(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& Facing, const FLinearColor& Tint, const FString& MatKey)
{
	if (!Assets) return;
	const FVector U = P1 - P0;
	const FVector V = P3 - P0;
	if (U.IsNearlyZero() || V.IsNearlyZero()) return;
	// Normal geométrica do retângulo, virada para o lado de `Facing`.
	FVector N = FVector::CrossProduct(U, V).GetSafeNormal();
	if (!Facing.IsNearlyZero() && FVector::DotProduct(N, Facing) < 0.0) N = -N;
	const FQuat Rot = FRotationMatrix::MakeFromXZ(U.GetSafeNormal(), N).ToQuat();
	const FVector Center = (P0 + P1 + P2 + P3) * 0.25;
	// A malha Plane do motor: 100x100 no plano XY, normal +Z.
	const FVector Y = Rot.GetAxisY();
	const double SV = FMath::Abs(FVector::DotProduct(V, Y));
	AddMesh(Assets->Plane(), FTransform(Rot, Center, FVector(U.Size() / 100.0, SV / 100.0, 1.0)), Tint, MatKey);
}

void UVilaMeshBatch::AddTri(const FVector& A, const FVector& B, const FVector& C, const FLinearColor& Tint, const FString& MatKey)
{
	if (!Assets) return;
	UGameInstance* GI = Assets->GetGameInstance();
	UVilaProcMesh* Proc = GI ? GI->GetSubsystem<UVilaProcMesh>() : nullptr;
	UStaticMesh* Mesh = Proc ? Proc->Triangle() : nullptr;
	if (!Mesh) return;
	const FVector M = (A + B) * 0.5;
	const FVector X = B - A;
	const FVector Yv = C - M;
	if (X.IsNearlyZero() || Yv.IsNearlyZero()) return;
	const FQuat Rot = FRotationMatrix::MakeFromXY(X.GetSafeNormal(), Yv.GetSafeNormal()).ToQuat();
	AddMesh(Mesh, FTransform(Rot, M, FVector(X.Size() / 100.0, Yv.Size() / 100.0, 1.0)), Tint, MatKey);
}

TArray<UHierarchicalInstancedStaticMeshComponent*> UVilaMeshBatch::Flush(USceneComponent* Parent, const FString& BaseName)
{
	TArray<UHierarchicalInstancedStaticMeshComponent*> Out;
	AActor* Owner = Parent ? Parent->GetOwner() : nullptr;
	if (!Owner)
	{
		Groups.Empty();
		Count = 0;
		return Out;
	}
	const FTransform ParentXf = Parent->GetComponentTransform();
	for (TPair<FString, FVilaMeshGroup>& Pair : Groups)
	{
		FVilaMeshGroup& G = Pair.Value;
		if (!G.Mesh || G.Transforms.Num() == 0) continue;
		const FName Name = MakeUniqueObjectName(Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(), FName(*BaseName));
		UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, Name);
		C->SetMobility(Mobility);
		C->SetStaticMesh(G.Mesh);
		if (G.Material) C->SetMaterial(0, G.Material);
		C->SetCastShadow(bShadows && !bHidden);
		C->bAffectDistanceFieldLighting = !bHidden;
		if (bCollision)
		{
			C->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			C->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			C->SetCanEverAffectNavigation(true);
		}
		else
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCanEverAffectNavigation(false);
		}
		if (bHidden)
		{
			C->SetVisibility(false);
			C->SetHiddenInGame(true);
		}
		if (G.CullDistance > 0.f)
		{
			C->InstanceStartCullDistance = FMath::RoundToInt(G.CullDistance * 0.85f);
			C->InstanceEndCullDistance = FMath::RoundToInt(G.CullDistance);
		}
		if (G.bTinted) C->SetNumCustomDataFloats(3);
		C->SetupAttachment(Parent);
		C->RegisterComponent();
		Owner->AddInstanceComponent(C);

		TArray<FTransform> Rel;
		Rel.Reserve(G.Transforms.Num());
		for (const FTransform& W : G.Transforms) Rel.Add(W.GetRelativeTransform(ParentXf));
		C->AddInstances(Rel, false);
		if (G.bTinted)
		{
			TArray<float> Data;
			Data.SetNum(3);
			for (int32 I = 0; I < G.Tints.Num(); ++I)
			{
				Data[0] = G.Tints[I].R;
				Data[1] = G.Tints[I].G;
				Data[2] = G.Tints[I].B;
				C->SetCustomData(I, Data, false);
			}
			C->MarkRenderStateDirty();
		}
		Out.Add(C);
	}
	Groups.Empty();
	Count = 0;
	return Out;
}
