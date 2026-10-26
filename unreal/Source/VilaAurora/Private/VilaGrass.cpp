#include "VilaGrass.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Math/UnrealMathUtility.h"
#include "Vila/Settings.h"
#include "VilaAssets.h"
#include "VilaAurora.h"
#include "VilaBuilding.h"
#include "VilaMeshBatch.h"
#include "VilaNature.h"
#include "VilaProcMesh.h"
#include "VilaWorldBuilder.h"

namespace VilaGrassPriv
{
	const float DENSITY[4] = {0.f, 2.5f, 5.f, 8.f};  // tufos por m² (desligado, baixa, média, alta)

	double Num(const vila::Json& A, size_t I, double Def = 0.0)
	{
		return (A.is_array() && A.size() > I) ? vila::to_num(A[I], Def) : Def;
	}
}

AVilaGrass::AVilaGrass()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

void AVilaGrass::EndPlay(const EEndPlayReason::Type Reason)
{
	if (SettingsConn)
	{
		vila::settings().changed.disconnect(SettingsConn);
		SettingsConn = 0;
	}
	Super::EndPlay(Reason);
}

void AVilaGrass::Build(UVilaWorldBuilder* InBuilder, int32 InLevel)
{
	Builder = InBuilder;
	if (!SettingsConn)
	{
		TWeakObjectPtr<AVilaGrass> Weak(this);
		SettingsConn = vila::settings().changed.connect([Weak](const std::string& Section)
		{
			if (Section != "graphics") return;
			AVilaGrass* G = Weak.Get();
			if (!G) return;
			const int32 L = vila::to_int(vila::settings().get_value("graphics", "grass_density"), 2);
			if (L != G->GetLevel()) G->Rebuild(L);
		});
	}
	Rebuild(InLevel);
}

void AVilaGrass::Clear()
{
	for (UHierarchicalInstancedStaticMeshComponent* C : Chunks)
		if (C) C->DestroyComponent();
	Chunks.Reset();
	Instances = 0;
}

void AVilaGrass::Rebuild(int32 InLevel)
{
	Level = FMath::Clamp(InLevel, 0, 3);
	Clear();
	if (Level <= 0 || !Builder) return;
	if (Grid.Num() == 0) Rasterize();

	UVilaAssets* Assets = UVilaAssets::Get(this);
	UVilaProcMesh* Proc = UVilaProcMesh::Get(this);
	UStaticMesh* Tuft = Proc ? Proc->GrassTuft() : nullptr;
	if (!Assets || !Tuft) return;
	bool bTinted = false;
	UMaterialInterface* Mat = UVilaNature::GrassMaterial(Assets, bTinted);

	const float Density = VilaGrassPriv::DENSITY[Level];
	const float Spacing = 1.f / FMath::Sqrt(Density);
	vila::Rng Rng(4242);
	const float Cull = 7000.f * UVilaMeshBatch::ViewDistanceScale();
	const vila::Json& Map = Builder->GetMap();
	const vila::Json& B = vila::jget(Map, "bounds");
	const float X0 = static_cast<float>(VilaGrassPriv::Num(B, 0, -85)) + 1.f;
	const float Z0 = static_cast<float>(VilaGrassPriv::Num(B, 1, -85)) + 1.f;
	const float X1 = static_cast<float>(VilaGrassPriv::Num(B, 2, 85)) - 1.f;
	const float Z1 = static_cast<float>(VilaGrassPriv::Num(B, 3, 85)) - 1.f;

	TArray<FTransform> Xf;
	TArray<FLinearColor> Cols;
	for (float CX = X0; CX < X1; CX += CHUNK)
	{
		for (float CZ = Z0; CZ < Z1; CZ += CHUNK)
		{
			const float RX1 = CX + FMath::Min(CHUNK, X1 - CX);
			const float RZ1 = CZ + FMath::Min(CHUNK, Z1 - CZ);
			Xf.Reset();
			Cols.Reset();
			for (float Z = CZ; Z < RZ1; Z += Spacing)
			{
				for (float X = CX; X < RX1; X += Spacing)
				{
					const float PX = X + static_cast<float>(Rng.randf_range(0.0, Spacing));
					const float PZ = Z + static_cast<float>(Rng.randf_range(0.0, Spacing));
					if (Blocked(PX, PZ)) continue;
					// Ruído suave (FastNoiseLite freq. 0,045 no Godot) em 0..1.
					const float N = FMath::Clamp(FMath::PerlinNoise2D(FVector2D(PX * 0.045f + 9.37f, PZ * 0.045f + 4.11f)) * 0.5f + 0.5f, 0.f, 1.f);
					// Prados mais altos e amarelados em manchas; perto de bordas, mais baixa.
					const float Tall = FMath::Lerp(0.8f, 1.45f, N) * static_cast<float>(Rng.randf_range(0.85, 1.15));
					const float Yaw = static_cast<float>(Rng.randf() * 360.0);
					Xf.Add(FTransform(FRotator(0.f, Yaw, 0.f), FVector(PX * 100.f, PZ * 100.f, 0.f), FVector(1.f, 1.f, Tall)));
					const float Dry = FMath::Clamp(N * 1.3f - 0.55f + static_cast<float>(Rng.randf_range(-0.1, 0.1)), 0.f, 1.f);
					const float Tone = static_cast<float>(Rng.randf_range(0.85, 1.12));
					Cols.Add(FMath::Lerp(FLinearColor(Tone, Tone, Tone), FLinearColor(1.35f, 1.12f, 0.6f), Dry * 0.55f));
				}
			}
			if (Xf.Num() == 0) continue;
			UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this,
				MakeUniqueObjectName(this, UHierarchicalInstancedStaticMeshComponent::StaticClass(), FName(TEXT("Grass"))));
			C->SetMobility(EComponentMobility::Static);
			C->SetStaticMesh(Tuft);
			if (Mat) C->SetMaterial(0, Mat);
			C->SetCastShadow(false);
			C->bAffectDistanceFieldLighting = false;
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCanEverAffectNavigation(false);
			C->InstanceStartCullDistance = FMath::RoundToInt(Cull - 800.f);
			C->InstanceEndCullDistance = FMath::RoundToInt(Cull);
			if (bTinted) C->SetNumCustomDataFloats(3);
			C->SetupAttachment(Root);
			C->RegisterComponent();
			AddInstanceComponent(C);
			// A raiz do ator fica na origem: transformações do mundo = relativas.
			const FTransform RootXf = Root->GetComponentTransform();
			TArray<FTransform> Rel;
			Rel.Reserve(Xf.Num());
			for (const FTransform& W : Xf) Rel.Add(W.GetRelativeTransform(RootXf));
			C->AddInstances(Rel, false);
			if (bTinted)
			{
				TArray<float> Data;
				Data.SetNum(3);
				for (int32 I = 0; I < Cols.Num(); ++I)
				{
					Data[0] = Cols[I].R;
					Data[1] = Cols[I].G;
					Data[2] = Cols[I].B;
					C->SetCustomData(I, Data, false);
				}
				C->MarkRenderStateDirty();
			}
			Chunks.Add(C);
			Instances += Xf.Num();
		}
	}
	UE_LOG(LogVila, Verbose, TEXT("Grama: %d tufos"), Instances);
}

// ------------------------------------------------------------------ ocupação
bool AVilaGrass::Blocked(float X, float Z) const
{
	const int32 IX = FMath::FloorToInt((X - Origin.X) / CELL);
	const int32 IZ = FMath::FloorToInt((Z - Origin.Y) / CELL);
	if (IX < 0 || IZ < 0 || IX >= GW || IZ >= GH) return true;
	return Grid[IZ * GW + IX] != 0;
}

void AVilaGrass::MarkRect(double RX0, double RZ0, double RX1, double RZ1, double Margin)
{
	const int32 AX = static_cast<int32>((RX0 - Margin - Origin.X) / CELL);
	const int32 AZ = static_cast<int32>((RZ0 - Margin - Origin.Y) / CELL);
	const int32 EX = FMath::CeilToInt((RX1 + Margin - Origin.X) / CELL);
	const int32 EZ = FMath::CeilToInt((RZ1 + Margin - Origin.Y) / CELL);
	for (int32 IZ = FMath::Max(0, AZ); IZ < FMath::Min(GH, EZ); ++IZ)
		for (int32 IX = FMath::Max(0, AX); IX < FMath::Min(GW, EX); ++IX)
			Grid[IZ * GW + IX] = 1;
}

void AVilaGrass::MarkSegment(const FVector2D& P0, const FVector2D& P1, float HalfWidth)
{
	const FVector2D MinP = FVector2D(FMath::Min(P0.X, P1.X), FMath::Min(P0.Y, P1.Y)) - FVector2D(HalfWidth, HalfWidth);
	const FVector2D MaxP = FVector2D(FMath::Max(P0.X, P1.X), FMath::Max(P0.Y, P1.Y)) + FVector2D(HalfWidth, HalfWidth);
	const FVector2D D = P1 - P0;
	const double L2 = D.SizeSquared();
	for (int32 IZ = FMath::Max(0, static_cast<int32>((MinP.Y - Origin.Y) / CELL)); IZ < FMath::Min(GH, static_cast<int32>((MaxP.Y - Origin.Y) / CELL) + 1); ++IZ)
	{
		for (int32 IX = FMath::Max(0, static_cast<int32>((MinP.X - Origin.X) / CELL)); IX < FMath::Min(GW, static_cast<int32>((MaxP.X - Origin.X) / CELL) + 1); ++IX)
		{
			const FVector2D C(Origin.X + (IX + 0.5) * CELL, Origin.Y + (IZ + 0.5) * CELL);
			const double T = L2 > 0.0 ? FMath::Clamp(FVector2D::DotProduct(C - P0, D) / L2, 0.0, 1.0) : 0.0;
			if (FVector2D::Distance(P0 + D * T, C) <= HalfWidth) Grid[IZ * GW + IX] = 1;
		}
	}
}

void AVilaGrass::Rasterize()
{
	const vila::Json& Map = Builder->GetMap();
	const vila::Json& B = vila::jget(Map, "bounds");
	Origin = FVector2D(VilaGrassPriv::Num(B, 0, -85), VilaGrassPriv::Num(B, 1, -85));
	GW = static_cast<int32>((VilaGrassPriv::Num(B, 2, 85) - VilaGrassPriv::Num(B, 0, -85)) / CELL) + 1;
	GH = static_cast<int32>((VilaGrassPriv::Num(B, 3, 85) - VilaGrassPriv::Num(B, 1, -85)) / CELL) + 1;
	Grid.Init(0, GW * GH);

	const vila::Json& Roads = vila::jobj(Map, "roads");
	const vila::Json& XS = vila::jarr(Roads, "xs");
	const vila::Json& ZS = vila::jarr(Roads, "zs");
	const float HW = static_cast<float>(vila::jnum(Roads, "width", 9.0) * 0.5);
	if (XS.size() > 0 && ZS.size() > 0)
	{
		const float XA = static_cast<float>(vila::to_num(XS[0])), XB = static_cast<float>(vila::to_num(XS[XS.size() - 1]));
		const float ZA = static_cast<float>(vila::to_num(ZS[0])), ZB = static_cast<float>(vila::to_num(ZS[ZS.size() - 1]));
		for (const vila::Json& Z : ZS)
			MarkRect(XA - HW, static_cast<float>(vila::to_num(Z)) - HW, XB + HW, static_cast<float>(vila::to_num(Z)) + HW);
		for (const vila::Json& X : XS)
			MarkRect(static_cast<float>(vila::to_num(X)) - HW, ZA - HW, static_cast<float>(vila::to_num(X)) + HW, ZB + HW);
	}
	for (const FBox2D& R : Builder->SidewalkRects)
		MarkRect(R.Min.X / 100.f, R.Min.Y / 100.f, R.Max.X / 100.f, R.Max.Y / 100.f, 0.15f);
	for (const TPair<FString, TObjectPtr<AVilaBuilding>>& Pair : Builder->Buildings)
	{
		if (!Pair.Value) continue;
		const FBox2D& R = Pair.Value->Rect;
		MarkRect(R.Min.X / 100.f, R.Min.Y / 100.f, R.Max.X / 100.f, R.Max.Y / 100.f, 0.5f);
	}
	for (const vila::Json& A : vila::jarr(Map, "areas"))
	{
		const std::string Type = vila::jstr(A, "type");
		if (vila::has(A, "rect") && Type != "park")
		{
			const vila::Json& RR = A["rect"];
			MarkRect(VilaGrassPriv::Num(RR, 0), VilaGrassPriv::Num(RR, 1), VilaGrassPriv::Num(RR, 2), VilaGrassPriv::Num(RR, 3), 0.2f);
		}
		if (vila::has(A, "points"))
		{
			const float HWid = Type == "drive_lane" ? 2.3f : static_cast<float>(vila::jnum(A, "width", 2.0) * 0.5 - 0.15);
			const vila::Json& Pts = A["points"];
			for (size_t I = 0; I + 1 < Pts.size(); ++I)
				MarkSegment(FVector2D(VilaGrassPriv::Num(Pts[I], 0), VilaGrassPriv::Num(Pts[I], 1)), FVector2D(VilaGrassPriv::Num(Pts[I + 1], 0), VilaGrassPriv::Num(Pts[I + 1], 1)), HWid);
		}
	}
	for (const vila::Json& P : vila::jarr(Map, "props"))
	{
		const std::string Type = vila::jstr(P, "type");
		if (!vila::has(P, "pos")) continue;
		const FVector2D Pos(VilaGrassPriv::Num(P["pos"], 0), VilaGrassPriv::Num(P["pos"], 1));
		if (Type == "fountain")
			MarkSegment(Pos, Pos, 2.9f);
		else if (Type == "canopy" || Type == "bus_stop" || Type == "kiosk" || Type == "pump")
			MarkSegment(Pos, Pos, Type != "canopy" ? 3.f : 7.f);
	}
}
