#include "VilaProcMesh.h"

#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Vila/Core.h"
#include "VilaAurora.h"

namespace VilaProcMeshPriv
{
	// Godot (metros, Y para cima) -> UE (cm, Z para cima).
	FVector G2U(const FVector& G) { return FVector(G.X * 100.0, G.Z * 100.0, G.Y * 100.0); }
	FVector G2UDir(const FVector& G) { return FVector(G.X, G.Z, G.Y); }

	FVector3f AnyTangent(const FVector3f& N)
	{
		const FVector3f Ref = FMath::Abs(N.Z) < 0.95f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
		return FVector3f::CrossProduct(Ref, N).GetSafeNormal();
	}

	// Escreve um triângulo com a face da frente para `Facing` e normal de sombreamento `Shade`.
	// No Unreal a face da frente tem (B-A)x(C-A) apontando para trás da normal; corrige a ordem.
	void PushTri(FVilaProcBuilder& B, FVector P[3], FVector2f U[3], FLinearColor C[3], const FVector& Facing, const FVector& Shade)
	{
		const FVector Cross = FVector::CrossProduct(P[1] - P[0], P[2] - P[0]);
		if (FVector::DotProduct(Cross, Facing) > 0.0)
		{
			Swap(P[1], P[2]);
			Swap(U[1], U[2]);
			Swap(C[1], C[2]);
		}
		const FVector3f N = FVector3f(Shade.GetSafeNormal());
		for (int32 K = 0; K < 3; ++K)
		{
			B.Pos.Add(FVector3f(P[K]));
			B.Nrm.Add(N);
			B.UV.Add(U[K]);
			B.Col.Add(C[K]);
		}
	}
}

void FVilaProcBuilder::AddTri(const FVector& A, const FVector& B, const FVector& C, const FVector& N,
	const FVector2f& UA, const FVector2f& UB, const FVector2f& UC,
	const FLinearColor& CA, const FLinearColor& CB, const FLinearColor& CC)
{
	FVector P[3] = {A, B, C};
	FVector2f U[3] = {UA, UB, UC};
	FLinearColor Cl[3] = {CA, CB, CC};
	VilaProcMeshPriv::PushTri(*this, P, U, Cl, N, N);
}

void FVilaProcBuilder::AddTriTwoSided(const FVector& A, const FVector& B, const FVector& C, const FVector& N,
	const FVector2f& UA, const FVector2f& UB, const FVector2f& UC)
{
	AddTri(A, B, C, N, UA, UB, UC);
	AddTri(A, B, C, -N, UA, UB, UC);
}

void FVilaProcBuilder::AddQuad(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& N, const FLinearColor& C)
{
	AddTri(P0, P1, P2, N, FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), C, C, C);
	AddTri(P0, P2, P3, N, FVector2f(0, 0), FVector2f(1, 1), FVector2f(0, 1), C, C, C);
}

// ------------------------------------------------------------------ subsistema
UVilaProcMesh* UVilaProcMesh::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UVilaProcMesh>() : nullptr;
}

void UVilaProcMesh::Deinitialize()
{
	Cache.Empty();
	Super::Deinitialize();
}

UStaticMesh* UVilaProcMesh::GetOrBuild(const FString& Key, TFunctionRef<void(FVilaProcBuilder&)> Fill)
{
	if (TObjectPtr<UStaticMesh>* Found = Cache.Find(Key)) return Found->Get();
	FVilaProcBuilder B;
	Fill(B);
	return BuildMesh(Key, B);
}

UStaticMesh* UVilaProcMesh::BuildMesh(const FString& Key, const FVilaProcBuilder& B)
{
	if (B.Pos.Num() < 3)
	{
		Cache.Add(Key, nullptr);
		return nullptr;
	}
	const FName Slot(TEXT("Mat0"));
	FMeshDescription Desc;
	FStaticMeshAttributes Attr(Desc);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> Colors = Attr.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();

	const int32 N = B.Pos.Num() - B.Pos.Num() % 3;
	Desc.ReserveNewVertices(N);
	Desc.ReserveNewVertexInstances(N);
	Desc.ReserveNewTriangles(N / 3);
	Desc.ReserveNewPolygonGroups(1);
	const FPolygonGroupID Group = Desc.CreatePolygonGroup();
	SlotNames[Group] = Slot;

	TArray<FVertexInstanceID> Tri;
	Tri.SetNum(3);
	for (int32 T = 0; T < N; T += 3)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			const int32 I = T + K;
			const FVertexID V = Desc.CreateVertex();
			Positions[V] = B.Pos[I];
			const FVertexInstanceID VI = Desc.CreateVertexInstance(V);
			const FVector3f Nr = B.Nrm.IsValidIndex(I) ? B.Nrm[I] : FVector3f(0, 0, 1);
			Normals[VI] = Nr;
			Tangents[VI] = VilaProcMeshPriv::AnyTangent(Nr);
			Signs[VI] = 1.f;
			const FLinearColor C = B.Col.IsValidIndex(I) ? B.Col[I] : FLinearColor::White;
			Colors[VI] = FVector4f(C.R, C.G, C.B, C.A);
			UVs.Set(VI, 0, B.UV.IsValidIndex(I) ? B.UV[I] : FVector2f::ZeroVector);
			Tri[K] = VI;
		}
		Desc.CreateTriangle(Group, Tri);
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(this, MakeUniqueObjectName(this, UStaticMesh::StaticClass(), FName(TEXT("SM_VilaProc"))), RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, Slot, Slot));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;  // colisão vem das caixas do builder
	Params.bFastBuild = true;              // obrigatório fora do editor
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&Desc);
	if (!Mesh->BuildFromMeshDescriptions(Descs, Params))
	{
		UE_LOG(LogVila, Warning, TEXT("Falha ao gerar a malha procedural '%s'."), *Key);
		Cache.Add(Key, nullptr);
		return nullptr;
	}
	Cache.Add(Key, Mesh);
	return Mesh;
}

// ------------------------------------------------------------------ formas
UStaticMesh* UVilaProcMesh::Triangle()
{
	return GetOrBuild(TEXT("tri"), [](FVilaProcBuilder& B)
	{
		B.AddTriTwoSided(FVector(-50, 0, 0), FVector(50, 0, 0), FVector(0, 100, 0), FVector(0, 0, 1),
			FVector2f(0, 0), FVector2f(1, 0), FVector2f(0.5f, 1));
	});
}

UStaticMesh* UVilaProcMesh::GrassTuft()
{
	return GetOrBuild(TEXT("grass_tuft"), [](FVilaProcBuilder& B)
	{
		vila::Rng Rng(5);
		for (int32 I = 0; I < 11; ++I)
		{
			const double A = Rng.randf() * vila::TAU;
			const FVector Base = FVector(FMath::Cos(A), 0, FMath::Sin(A)) * Rng.randf_range(0.0, 0.2);
			const double Yaw = Rng.randf() * vila::TAU;
			const FVector Side(FMath::Cos(Yaw), 0, FMath::Sin(Yaw));
			const FVector Lean = FVector(-FMath::Sin(Yaw), 0, FMath::Cos(Yaw)) * Rng.randf_range(0.05, 0.16) + Base.GetSafeNormal() * 0.06;
			const double H = Rng.randf_range(0.26, 0.48);
			const double W = Rng.randf_range(0.03, 0.05);
			const FVector Mid = Base + Lean * 0.4 + FVector(0, H * 0.55, 0);
			const FVector Tip = Base + Lean + FVector(0, H, 0);
			const FVector Nrm = FVector::CrossProduct(Side, FVector(0, 1, 0)).GetSafeNormal();
			const FVector V[5] = {Base - Side * W, Base + Side * W, Mid + Side * W * 0.65, Mid - Side * W * 0.65, Tip};
			const FVector2f UV[5] = {FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 0.55f), FVector2f(0, 0.55f), FVector2f(0.5f, 1)};
			const int32 Tris[3][3] = {{0, 1, 2}, {0, 2, 3}, {3, 2, 4}};
			const FVector N = VilaProcMeshPriv::G2UDir(Nrm);
			for (const auto& T : Tris)
			{
				FVector P[3] = {VilaProcMeshPriv::G2U(V[T[0]]), VilaProcMeshPriv::G2U(V[T[1]]), VilaProcMeshPriv::G2U(V[T[2]])};
				FVector2f U[3] = {UV[T[0]], UV[T[1]], UV[T[2]]};
				FLinearColor C[3] = {FLinearColor(1, 1, 1, UV[T[0]].Y), FLinearColor(1, 1, 1, UV[T[1]].Y), FLinearColor(1, 1, 1, UV[T[2]].Y)};
				VilaProcMeshPriv::PushTri(B, P, U, C, N, N);
			}
		}
	});
}

UStaticMesh* UVilaProcMesh::LeafCluster(int32 Variant, int32 Cards, float CardRatio)
{
	const float Ratio = FMath::RoundToFloat(FMath::Clamp(CardRatio, 0.1f, 2.f) * 10.f) / 10.f;
	const FString Key = FString::Printf(TEXT("leaf|%d|%d|%.1f"), Variant, Cards, Ratio);
	return GetOrBuild(Key, [Variant, Cards, Ratio](FVilaProcBuilder& B)
	{
		vila::Rng Rng(static_cast<uint64_t>(1000 + Variant * 7919 + Cards));
		for (int32 I = 0; I < Cards; ++I)
		{
			// Mesma distribuição do FoliageBatcher.add_canopy, num maço de raio 1.
			const FVector Dir = FVector(Rng.randf_range(-1, 1), Rng.randf_range(-0.7, 1), Rng.randf_range(-1, 1)).GetSafeNormal();
			const FVector P = Dir * Rng.randf_range(0.35, 1.0);
			const double Size = Rng.randf_range(0.55, 0.9) * Ratio;
			FVector N = FVector(Rng.randf_range(-1, 1), Rng.randf_range(-1, 1), Rng.randf_range(-1, 1)).GetSafeNormal();
			if (N.IsNearlyZero()) N = FVector(0, 1, 0);
			const FVector T = FVector::CrossProduct(N, FMath::Abs(N.Y) < 0.9 ? FVector(0, 1, 0) : FVector(1, 0, 0)).GetSafeNormal();
			const FVector Bn = FVector::CrossProduct(N, T).GetSafeNormal();
			const int32 Cell = static_cast<int32>(Rng.randi() % 4);
			const FVector2f UV0((Cell % 2) * 0.5f, (Cell / 2) * 0.5f);
			const FVector Shade = (P + FVector(0, 0.6, 0)).GetSafeNormal();
			const float S = static_cast<float>(Rng.randf_range(0.82, 1.12) * FMath::Lerp(0.8, 1.1, FMath::Clamp(P.Y / 2.5 + 0.5, 0.0, 1.0)));
			const FLinearColor Col(S, S, S, FMath::Clamp(0.3f + 0.7f * static_cast<float>(P.Size()), 0.2f, 1.f));
			const FVector Corner[4] = {
				P - T * Size * 0.5 - Bn * Size * 0.5, P + T * Size * 0.5 - Bn * Size * 0.5,
				P + T * Size * 0.5 + Bn * Size * 0.5, P - T * Size * 0.5 + Bn * Size * 0.5};
			const FVector2f CUV[4] = {UV0 + FVector2f(0, 0.5f), UV0 + FVector2f(0.5f, 0.5f), UV0 + FVector2f(0.5f, 0), UV0};
			const int32 Tris[2][3] = {{0, 2, 1}, {0, 3, 2}};
			for (const auto& Tr : Tris)
			{
				FVector Pp[3] = {VilaProcMeshPriv::G2U(Corner[Tr[0]]), VilaProcMeshPriv::G2U(Corner[Tr[1]]), VilaProcMeshPriv::G2U(Corner[Tr[2]])};
				FVector2f U[3] = {CUV[Tr[0]], CUV[Tr[1]], CUV[Tr[2]]};
				FLinearColor C[3] = {Col, Col, Col};
				VilaProcMeshPriv::PushTri(B, Pp, U, C, VilaProcMeshPriv::G2UDir(N), VilaProcMeshPriv::G2UDir(Shade));
			}
		}
	});
}
