#include "VilaWorldBuilder.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/Crc.h"
#include "NavigationSystem.h"
#include "Vila/Settings.h"
#include "VilaAssets.h"
#include "VilaAurora.h"
#include "VilaBuilding.h"
#include "VilaConv.h"
#include "VilaDoor.h"
#include "VilaFurniture.h"
#include "VilaGrass.h"
#include "VilaMeshBatch.h"
#include "VilaNature.h"
#include "VilaWorld.h"

// ======================================================================== espaço do Godot
// O port mantém a geometria no espaço do Godot (metros, Y para cima, z para "baixo" do mapa)
// para ficar verificável linha a linha com WorldBuilder.gd; a conversão para o UE acontece
// só na entrada dos lotes (G2U / GQuat).
namespace VilaWB
{
	using vila::Json;

	// Base do Godot (colunas X, Y, Z).
	struct FGB
	{
		FVector X = FVector(1, 0, 0);
		FVector Y = FVector(0, 1, 0);
		FVector Z = FVector(0, 0, 1);

		FVector operator*(const FVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
		FGB operator*(const FGB& O) const
		{
			FGB R;
			R.X = (*this) * O.X;
			R.Y = (*this) * O.Y;
			R.Z = (*this) * O.Z;
			return R;
		}
		// Basis(axis, angle) do Godot (regra da mão direita).
		static FGB Axis(const FVector& K, double Rad)
		{
			const FVector N = K.GetSafeNormal();
			const double C = FMath::Cos(Rad), S = FMath::Sin(Rad);
			auto Rot = [&](const FVector& V) { return V * C + FVector::CrossProduct(N, V) * S + N * FVector::DotProduct(N, V) * (1.0 - C); };
			FGB R;
			R.X = Rot(FVector(1, 0, 0));
			R.Y = Rot(FVector(0, 1, 0));
			R.Z = Rot(FVector(0, 0, 1));
			return R;
		}
	};
	FGB YRot(double Deg) { return FGB::Axis(FVector(0, 1, 0), FMath::DegreesToRadians(Deg)); }

	// Transform3D do Godot.
	struct FGX
	{
		FGB B;
		FVector O = FVector::ZeroVector;
		FGX() = default;
		FGX(const FGB& InB, const FVector& InO) : B(InB), O(InO) {}
		FVector operator*(const FVector& P) const { return B * P + O; }
		FGX operator*(const FGX& T) const { return FGX(B * T.B, B * T.O + O); }
	};
	FGX At(const FVector& P) { return FGX(FGB(), P); }

	FVector G2U(const FVector& G) { return FVector(G.X * 100.0, G.Z * 100.0, G.Y * 100.0); }
	FVector GDir(const FVector& G) { return FVector(G.X, G.Z, G.Y); }
	FQuat GQuat(const FGB& B) { return FRotationMatrix::MakeFromXY(GDir(B.X), GDir(B.Z)).ToQuat(); }
	FQuat YawQ(double GodotDeg) { return FRotator(0.0, -GodotDeg, 0.0).Quaternion(); }
	FVector GSize(const FVector& S) { return FVector(S.X, S.Z, S.Y) * 100.0; }
	FVector V3(double X, double Y, double Z) { return FVector(X, Y, Z); }

	// ---------------------------------------------------------------- cores (Color do Godot)
	// Cores de vértice/instância do Godot são usadas cruas (o Godot as trata como lineares);
	// cores de material (albedo, tint "source_color") passam por Lin().
	FLinearColor GC(double R, double G, double B) { return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), 1.f); }
	FLinearColor HexRaw(const std::string& Hex)
	{
		if (Hex.empty()) return FLinearColor::White;
		FLinearColor C = FColor::FromHex(VilaConv::S(Hex)).ReinterpretAsLinear();
		C.A = 1.f;
		return C;
	}
	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, double T)
	{
		const float F = static_cast<float>(T);
		return FLinearColor(A.R + (B.R - A.R) * F, A.G + (B.G - A.G) * F, A.B + (B.B - A.B) * F, 1.f);
	}
	FLinearColor Darkened(const FLinearColor& C, double A) { const float K = 1.f - static_cast<float>(A); return FLinearColor(C.R * K, C.G * K, C.B * K, 1.f); }
	FLinearColor Lightened(const FLinearColor& C, double A)
	{
		const float K = static_cast<float>(A);
		return FLinearColor(C.R + (1.f - C.R) * K, C.G + (1.f - C.G) * K, C.B + (1.f - C.B) * K, 1.f);
	}
	FLinearColor Scale(const FLinearColor& C, double K) { return FLinearColor(C.R * K, C.G * K, C.B * K, 1.f); }
	float ToLin(float C) { return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f); }
	FLinearColor Lin(const FLinearColor& C) { return FLinearColor(ToLin(C.R), ToLin(C.G), ToLin(C.B), 1.f); }
	FLinearColor FromHSV(double H, double S, double V) { return FLinearColor(static_cast<float>(H * 360.0), static_cast<float>(S), static_cast<float>(V)).HSVToLinearRGB(); }
	const FLinearColor WHITE_C = FLinearColor(1, 1, 1, 1);

	// Color("rrggbb") cru (sem FString na inicialização estática).
	FLinearColor RGB8(int32 R, int32 G, int32 B) { return FLinearColor(R / 255.f, G / 255.f, B / 255.f, 1.f); }
	const FLinearColor WOOD = RGB8(0xe6, 0xcb, 0xa8);       // tintas sobre a textura de madeira
	const FLinearColor WOOD_DARK = RGB8(0xa9, 0x86, 0x63);
	const FLinearColor WHITE = RGB8(0xee, 0xee, 0xea);
	const FLinearColor STEEL = RGB8(0xb8, 0xbc, 0xc2);
	const FLinearColor DARK = RGB8(0x2a, 0x2a, 0x2e);

	// ---------------------------------------------------------------- json
	double Num(const Json& A, size_t I, double Def = 0.0) { return (A.is_array() && A.size() > I) ? vila::to_num(A[I], Def) : Def; }
	FVector GPos2(const Json& A) { return V3(Num(A, 0), 0.0, Num(A, 1)); }  // [x, z] -> Vector3(x, 0, z)
	FString FS(const std::string& S) { return VilaConv::S(S); }

	// ---------------------------------------------------------------- lote no espaço do Godot
	// Mesma interface do MeshBatcher/ChunkBatcher; `Origin` = posição do nó pai (prédio).
	struct FGBatch
	{
		UVilaMeshBatch* B = nullptr;
		FVector Origin = FVector::ZeroVector;

		void add_box(const FGX& Xf, const FVector& Size, const FLinearColor& C, const FString& M = FString(), UMaterialInterface* Explicit = nullptr) const
		{
			B->AddBox(G2U(Origin + Xf.O), GQuat(Xf.B), GSize(Size), C, M, Explicit);
		}
		void box(const FVector& Center, const FVector& Size, const FLinearColor& C, double RotDeg = 0.0, const FString& M = FString()) const
		{
			add_box(FGX(YRot(RotDeg), Center), Size, C, M);
		}
		void long_box(const FVector& Center, const FVector& Size, const FLinearColor& C, const FString& M = FString()) const
		{
			box(Center, Size, C, 0.0, M);
		}
		void add_cylinder(const FVector& Center, double Radius, double Height, const FLinearColor& C, int32 /*Segments*/ = 8, const FString& M = FString()) const
		{
			B->AddCylinder(G2U(Origin + Center), FQuat::Identity, static_cast<float>(Radius * 100.0), static_cast<float>(Height * 100.0), C, M);
		}
		void add_tube(const FVector& A, const FVector& E, double Radius, const FLinearColor& C, int32 /*Segments*/ = 6, const FString& M = FString()) const
		{
			B->AddTube(G2U(Origin + A), G2U(Origin + E), static_cast<float>(Radius * 100.0), C, M);
		}
		void add_sphere(const FVector& Center, const FVector& Radius, const FLinearColor& C, int32 /*Rings*/ = 5, int32 /*Segments*/ = 8, const FString& M = FString()) const
		{
			B->AddSphere(G2U(Origin + Center), FQuat::Identity, GSize(Radius), C, M);
		}
		void add_quad(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FLinearColor& C, const FString& M, const FVector& Facing) const
		{
			B->AddQuad(G2U(Origin + P0), G2U(Origin + P1), G2U(Origin + P2), G2U(Origin + P3), GDir(Facing), C, M);
		}
		void add_tri(const FVector& P0, const FVector& P1, const FVector& P2, const FLinearColor& C, const FString& M) const
		{
			B->AddTri(G2U(Origin + P0), G2U(Origin + P1), G2U(Origin + P2), C, M);
		}
	};

	struct FSideFrame
	{
		FVector Axis, Fixed, Out;
		double Len = 0.0, Rot = 0.0;
	};
	FSideFrame SideFrame(const std::string& Side, double W, double D)
	{
		// eixo ao longo da parede, posição fixa, comprimento e rotação
		const double T = UVilaWorldBuilder::WALL_T;
		if (Side == "n") return {V3(1, 0, 0), V3(0, 0, -D * 0.5), V3(0, 0, -1), W + T, 0.0};
		if (Side == "s") return {V3(1, 0, 0), V3(0, 0, D * 0.5), V3(0, 0, 1), W + T, 0.0};
		if (Side == "e") return {V3(0, 0, 1), V3(W * 0.5, 0, 0), V3(1, 0, 0), D - T, 90.0};
		return {V3(0, 0, 1), V3(-W * 0.5, 0, 0), V3(-1, 0, 0), D - T, 90.0};
	}
	FVector SideOut(const std::string& Side)
	{
		if (Side == "n") return V3(0, 0, -1);
		if (Side == "s") return V3(0, 0, 1);
		if (Side == "e") return V3(1, 0, 0);
		return V3(-1, 0, 0);
	}
	// Yaw UE cujo +X aponta para fora da parede.
	double OutYaw(const std::string& Side)
	{
		const FVector N = VilaConv::SideNormal(Side);
		return FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X));
	}

	const char* const INTERACTIVE[] = {"bed", "wardrobe", "fridge", "chest", "crate", "stove", "sink", "shower", "sofa", "chair",
		"note", "counter", "grill", "fryer", "soda_machine", "drive_window", "time_clock"};
	bool IsInteractiveType(const std::string& T)
	{
		for (const char* K : INTERACTIVE)
			if (T == K) return true;
		return false;
	}

	const FLinearColor LEAF_TINTS[3] = {FLinearColor(0.30f, 0.44f, 0.17f), FLinearColor(0.62f, 0.62f, 0.22f), FLinearColor(0.88f, 0.68f, 0.24f)};

// ======================================================================== construção
struct FVilaWorldBuild
{
	UVilaWorldBuilder& W;
	UWorld* UW = nullptr;
	UVilaAssets* Assets = nullptr;
	AVilaWorldStatic* Static = nullptr;
	const Json& Map;
	vila::Rng Rng{1337};
	FGBatch Deco, Markings, LampHeads, Sidewalks, AsphaltB, Curbs, Ground;
	UVilaMeshBatch* Colliders = nullptr;
	FVector CurrentBuildingOrigin = FVector::ZeroVector;

	FVilaWorldBuild(UVilaWorldBuilder& InW, const Json& InMap) : W(InW), Map(InMap) {}

	// ---------------------------------------------------------------- utilitários
	UVilaMeshBatch* NewBatch(float Rough, bool bShadows, EComponentMobility::Type Mob = EComponentMobility::Static)
	{
		UVilaMeshBatch* B = NewObject<UVilaMeshBatch>(&W);
		B->Init(Assets, Rough, bShadows);
		B->Mobility = Mob;
		W.Batches.Add(B);
		return B;
	}
	FGBatch MakeG(UVilaMeshBatch* B, const FVector& Origin = FVector::ZeroVector) const
	{
		FGBatch G;
		G.B = B;
		G.Origin = Origin;
		return G;
	}

	void add_collider(const FVector& Center, const FVector& Size, double RotDeg = 0.0)
	{
		Colliders->AddBox(G2U(Center), YawQ(RotDeg), GSize(Size), WHITE_C);
	}
	void add_cyl_collider(const FVector& Center, double Radius, double Height)
	{
		Colliders->AddCylinder(G2U(Center), FQuat::Identity, static_cast<float>(Radius * 100.0), static_cast<float>(Height * 100.0), WHITE_C);
	}

	FActorSpawnParameters SpawnParams() const
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return P;
	}

	// Componente de malha simples preso a `Parent` (transform do mundo UE).
	UStaticMeshComponent* MeshComp(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& WorldXf, UMaterialInterface* Mat,
		EComponentMobility::Type Mob = EComponentMobility::Static, bool bShadow = true)
	{
		AActor* Owner = Parent->GetOwner();
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), FName(TEXT("Mesh"))));
		C->SetMobility(Mob);
		C->SetStaticMesh(Mesh);
		if (Mat) C->SetMaterial(0, Mat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(bShadow);
		C->SetupAttachment(Parent);
		C->SetRelativeTransform(WorldXf.GetRelativeTransform(Parent->GetComponentTransform()));
		C->RegisterComponent();
		Owner->AddInstanceComponent(C);
		return C;
	}

	// Label3D do Godot: texto plano, legível de quem olha para a direção `Yaw` (UE).
	UTextRenderComponent* Label(USceneComponent* Parent, const FString& Text, const FVector& WorldPos, double Yaw, float WorldSize,
		const FColor& Color = FColor::White, EComponentMobility::Type Mob = EComponentMobility::Static)
	{
		AActor* Owner = Parent->GetOwner();
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(Owner, MakeUniqueObjectName(Owner, UTextRenderComponent::StaticClass(), FName(TEXT("Label"))));
		T->SetMobility(Mob);
		T->SetText(FText::FromString(Text));
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetWorldSize(WorldSize);
		T->SetTextRenderColor(Color);
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		T->SetCastShadow(false);
		T->SetupAttachment(Parent);
		const FTransform Xf(FRotator(0.0, Yaw, 0.0), WorldPos);
		T->SetRelativeTransform(Xf.GetRelativeTransform(Parent->GetComponentTransform()));
		T->RegisterComponent();
		Owner->AddInstanceComponent(T);
		return T;
	}

	AVilaFurniture* SpawnFurniture(const Json& Def, const FVector& PosG, double RotDeg, const FString& BuildingId)
	{
		AVilaFurniture* F = UW->SpawnActor<AVilaFurniture>(AVilaFurniture::StaticClass(), G2U(PosG),
			FRotator(0.0, VilaConv::YawFromGodot(RotDeg), 0.0), SpawnParams());
		if (F) F->Setup(Def, W.VilaWorld, BuildingId);
		return F;
	}
	USceneComponent* VisualRootOf(AVilaFurniture* F) const
	{
		if (!F) return nullptr;
		USceneComponent* R = F->GetVisualRoot();
		return R ? R : F->GetRootComponent();
	}
	// Lote do visual de um móvel (preso ao visual root no fim; peças móveis acompanham o ator).
	UVilaMeshBatch* FurnitureBatch(float Rough = 0.7f)
	{
		return NewBatch(Rough, true, EComponentMobility::Movable);
	}
	void FlushFurniture(UVilaMeshBatch* B, AVilaFurniture* F, USceneComponent* Fallback)
	{
		USceneComponent* Root = VisualRootOf(F);
		B->Flush(Root ? Root : Fallback, TEXT("Visual"));
	}

	void add_spot(const FString& Id, const FVector& PosG, double Rot, const FString& Pose, bool bHidden, const FString& Building, AVilaFurniture* Fn = nullptr)
	{
		FVilaSpot S;
		S.Id = Id;
		S.Location = G2U(V3(PosG.X, 0.0, PosG.Z));
		S.Yaw = static_cast<float>(VilaConv::YawFromGodot(Rot));
		S.Pose = Pose;
		S.bHidden = bHidden;
		S.BuildingId = Building;
		S.Furniture = Fn;
		W.Spots.Add(Id, S);
	}

	// ---------------------------------------------------------------- build
	void Run()
	{
		Rng.seed(1337);
		Static = UW->SpawnActor<AVilaWorldStatic>(AVilaWorldStatic::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams());
		if (!Static)
		{
			UE_LOG(LogVila, Error, TEXT("WorldBuilder: falha ao criar o AVilaWorldStatic."));
			return;
		}
		Static->Builder = &W;
		W.StaticActor = Static;
		USceneComponent* Root = Static->Root;

		Deco = MakeG(NewBatch(0.85f, true));
		Markings = MakeG(NewBatch(0.6f, false));
		LampHeads = MakeG(NewBatch(0.3f, false));
		Sidewalks = MakeG(NewBatch(0.8f, true));
		AsphaltB = MakeG(NewBatch(0.9f, false));
		Curbs = MakeG(NewBatch(0.85f, false));
		Ground = MakeG(NewBatch(0.9f, false));
		Colliders = NewBatch(0.85f, false);
		Colliders->bCollision = true;
		Colliders->bHidden = true;
		Colliders->bSizeCull = false;
		W.Foliage = NewObject<UVilaNature>(&W);
		W.Foliage->Init(Assets);
		MakeLampMaterial();
		LampHeads.B->MaterialOverride = W.LampMID;

		build_ground();
		build_roads();
		build_areas();
		for (const Json& B : vila::jarr(Map, "buildings")) build_building(B);
		for (const Json& P : vila::jarr(Map, "props")) build_prop(P);
		const Json& SpotsJ = vila::jobj(Map, "spots");
		for (auto It = SpotsJ.begin(); It != SpotsJ.end(); ++It)
		{
			const Json& S = It.value();
			add_spot(FS(It.key()), GPos2(vila::jget(S, "pos")), vila::jnum(S, "rot", 0.0), FS(vila::jstr(S, "pose", "stand")), vila::jbool(S, "hidden", false), FString());
		}
		// No Godot o mobiliário urbano era somado ao lote "deco" depois de ele já ter sido
		// montado (postes e hidrantes ficavam invisíveis, só com colisão). Aqui eles aparecem.
		build_street_furniture();
		build_signals();

		Deco.B->Flush(Root, TEXT("Deco"));
		W.Foliage->Build(Root);
		Sidewalks.B->Flush(Root, TEXT("Sidewalks"));
		AsphaltB.B->Flush(Root, TEXT("Asphalt"));
		Curbs.B->Flush(Root, TEXT("Curbs"));
		Markings.B->Flush(Root, TEXT("Markings"));
		LampHeads.B->Flush(Root, TEXT("LampHeads"));
		Ground.B->Flush(Root, TEXT("Ground"));
		Colliders->Flush(Root, TEXT("Collision"));
		W.Batches.Reset();
	}

	void MakeLampMaterial()
	{
		UMaterialInterface* Em = Assets->Master(TEXT("M_Emissive"));
		W.LampMode = Em ? 1 : 0;
		W.LampMID = Assets->MakeMID(Em, &W);
		W.LampMID->SetVectorParameterValue(TEXT("Color"), Lin(GC(0.95, 0.93, 0.85)));
		W.LampMID->SetScalarParameterValue(TEXT("Intensity"), 0.f);
	}

	// ---------------------------------------------------------------- chão e ruas
	void build_ground()
	{
		const Json& B = vila::jget(Map, "bounds");
		const double B0 = Num(B, 0, -85), B1 = Num(B, 1, -85), B2 = Num(B, 2, 85), B3 = Num(B, 3, 85);
		const FVector2D Size(B2 - B0 + 60.0, B3 - B1 + 60.0);
		const int32 Tiles = FMath::CeilToInt(Size.X / 40.0);
		const double Tile = Size.X / Tiles;
		UMaterialInterface* GMat = UVilaNature::GroundMaterial(Assets);
		for (int32 I = 0; I < Tiles; ++I)
			for (int32 J = 0; J < Tiles; ++J)
			{
				const FVector P = V3(-Size.X * 0.5 + Tile * (I + 0.5), 0, -Size.Y * 0.5 + Tile * (J + 0.5));
				Ground.B->AddMesh(Assets->Plane(), FTransform(FQuat::Identity, G2U(P), FVector(Tile, Tile, 1.0)), WHITE_C, FString(), GMat);
			}
		add_collider(V3(0, -0.5, 0), V3(Size.X, 1.0, Size.Y));
		// Limites do mundo: cercas e paredes invisíveis.
		const double Lim = B2 - 1.0;
		for (const double S : {-1.0, 1.0})
		{
			add_collider(V3(S * Lim, 2, 0), V3(1, 4, Lim * 2));
			add_collider(V3(0, 2, S * Lim), V3(Lim * 2, 4, 1));
			for (int32 I = -static_cast<int32>(Lim); I < static_cast<int32>(Lim); I += 3)
			{
				Deco.box(V3(S * (Lim - 0.5), 0.6, I), V3(0.08, 1.2, 0.08), WOOD_DARK, 0.0, TEXT("wood_out"));
				Deco.box(V3(I, 0.6, S * (Lim - 0.5)), V3(0.08, 1.2, 0.08), WOOD_DARK, 0.0, TEXT("wood_out"));
			}
			Deco.long_box(V3(S * (Lim - 0.5), 0.95, 0), V3(0.05, 0.08, Lim * 2), WOOD, TEXT("wood_out"));
			Deco.long_box(V3(0, 0.95, S * (Lim - 0.5)), V3(Lim * 2, 0.08, 0.05), WOOD, TEXT("wood_out"));
			Deco.long_box(V3(S * (Lim - 0.5), 0.45, 0), V3(0.05, 0.08, Lim * 2), WOOD, TEXT("wood_out"));
			Deco.long_box(V3(0, 0.45, S * (Lim - 0.5)), V3(Lim * 2, 0.08, 0.05), WOOD, TEXT("wood_out"));
		}
	}

	void build_roads()
	{
		const Json& R = vila::jobj(Map, "roads");
		const Json& XS = vila::jarr(R, "xs");
		const Json& ZS = vila::jarr(R, "zs");
		if (XS.size() < 1 || ZS.size() < 1) return;
		const double HW = vila::jnum(R, "width", 9.0) * 0.5;
		const double SW = vila::jnum(R, "sidewalk", 3.0);
		auto X = [&](size_t I) { return vila::to_num(XS[I]); };
		auto Z = [&](size_t I) { return vila::to_num(ZS[I]); };
		const double XMin = X(0) - HW, XMax = X(XS.size() - 1) + HW;
		const double ZMin = Z(0) - HW, ZMax = Z(ZS.size() - 1) + HW;
		const FString Asph = TEXT("asphalt");
		// Trechos entre cruzamentos + quadrados dos cruzamentos (sem sobreposição).
		for (size_t K = 0; K < ZS.size(); ++K)
			for (size_t I = 0; I + 1 < XS.size(); ++I)
			{
				const double A0 = X(I) + HW, A1 = X(I + 1) - HW;
				AsphaltB.long_box(V3((A0 + A1) * 0.5, 0.0, Z(K)), V3(A1 - A0, 0.02, HW * 2), WHITE_C, Asph);
			}
		for (size_t K = 0; K < XS.size(); ++K)
		{
			for (size_t I = 0; I + 1 < ZS.size(); ++I)
			{
				const double A = Z(I) + HW, BZ = Z(I + 1) - HW;
				AsphaltB.long_box(V3(X(K), 0.0, (A + BZ) * 0.5), V3(HW * 2, 0.02, BZ - A), WHITE_C, Asph);
			}
			for (size_t J = 0; J < ZS.size(); ++J)
				AsphaltB.box(V3(X(K), 0.0, Z(J)), V3(HW * 2, 0.02, HW * 2), WHITE_C, 0.0, Asph);
		}
		// Calçadas: anel em volta de cada quarteirão + anel externo.
		for (size_t I = 0; I + 1 < XS.size(); ++I)
			for (size_t J = 0; J + 1 < ZS.size(); ++J)
			{
				const double X0 = X(I) + HW, X1 = X(I + 1) - HW, Z0 = Z(J) + HW, Z1 = Z(J + 1) - HW;
				sidewalk(X0, Z0, X1 - X0, SW);
				sidewalk(X0, Z1 - SW, X1 - X0, SW);
				curb_rect(X0, Z0, X1, Z1);
				sidewalk(X0, Z0 + SW, SW, Z1 - Z0 - 2 * SW);
				sidewalk(X1 - SW, Z0 + SW, SW, Z1 - Z0 - 2 * SW);
			}
		sidewalk(XMin - SW, ZMin - SW, XMax - XMin + 2 * SW, SW);
		sidewalk(XMin - SW, ZMax, XMax - XMin + 2 * SW, SW);
		sidewalk(XMin - SW, ZMin, SW, ZMax - ZMin);
		sidewalk(XMax, ZMin, SW, ZMax - ZMin);
		curb_rect(XMin, ZMin, XMax, ZMax);
		// Marcações: linha central tracejada amarela, bordas brancas, retenção e faixas de pedestre.
		const FLinearColor Yellow = GC(0.93, 0.76, 0.2);
		const FLinearColor White = GC(0.92, 0.92, 0.9);
		for (size_t K = 0; K < ZS.size(); ++K)
			for (size_t I = 0; I + 1 < XS.size(); ++I)
				road_markings(V3(X(I), 0, Z(K)), V3(X(I + 1), 0, Z(K)), HW, Yellow, White);
		for (size_t K = 0; K < XS.size(); ++K)
			for (size_t I = 0; I + 1 < ZS.size(); ++I)
				road_markings(V3(X(K), 0, Z(I)), V3(X(K), 0, Z(I + 1)), HW, Yellow, White);
	}

	void sidewalk(double RX, double RZ, double SX, double SZ)
	{
		if (SX <= 0.01 || SZ <= 0.01) return;
		W.SidewalkRects.Add(FBox2D(FVector2D(RX * 100.0, RZ * 100.0), FVector2D((RX + SX) * 100.0, (RZ + SZ) * 100.0)));
		const double H = UVilaWorldBuilder::SIDEWALK_H;
		Sidewalks.long_box(V3(RX + SX * 0.5, H * 0.5, RZ + SZ * 0.5), V3(SX, H, SZ), WHITE_C, TEXT("sidewalk"));
	}

	// Meio-fio de concreto em volta de um retângulo (borda da calçada voltada para a rua).
	void curb_rect(double X0, double Z0, double X1, double Z1)
	{
		const FLinearColor C = GC(0.8, 0.79, 0.76);
		const double T = 0.14;
		const double H = UVilaWorldBuilder::SIDEWALK_H;
		const FString M = TEXT("curb");
		Curbs.long_box(V3((X0 + X1) * 0.5, H * 0.5 + 0.005, Z0 + T * 0.5), V3(X1 - X0, H + 0.01, T), C, M);
		Curbs.long_box(V3((X0 + X1) * 0.5, H * 0.5 + 0.005, Z1 - T * 0.5), V3(X1 - X0, H + 0.01, T), C, M);
		Curbs.long_box(V3(X0 + T * 0.5, H * 0.5 + 0.005, (Z0 + Z1) * 0.5), V3(T, H + 0.01, Z1 - Z0 - 2 * T), C, M);
		Curbs.long_box(V3(X1 - T * 0.5, H * 0.5 + 0.005, (Z0 + Z1) * 0.5), V3(T, H + 0.01, Z1 - Z0 - 2 * T), C, M);
	}

	void road_markings(const FVector& A, const FVector& B, double HW, const FLinearColor& Yellow, const FLinearColor& White)
	{
		const FVector D = (B - A).GetSafeNormal();
		const FVector Right = V3(-D.Z, 0, D.X);
		const double Length = FVector::Distance(A, B);
		const double Rot = FMath::RadiansToDegrees(FMath::Atan2(-D.X, -D.Z));
		const FVector Y = V3(0, 0.013, 0);
		const double SD = UVilaWorldBuilder::STOP_DIST;
		double T = SD + 1.0;
		while (T < Length - SD - 1.0)
		{
			Markings.box(A + D * (T + 1.0) + Y, V3(0.14, 0.004, 2.0), Yellow, Rot);
			T += 4.5;
		}
		const double Inner = Length - 2 * 4.5;
		for (const double S : {-1.0, 1.0})
			Markings.box((A + B) * 0.5 + Right * S * (HW - 0.35) + Y, V3(0.1, 0.004, Inner), White, Rot);
		// Faixas de pedestre e linhas de retenção nas duas pontas.
		const FVector Ends[2][2] = {{A, D}, {B, -D}};
		for (const auto& End : Ends)
		{
			const FVector C = End[0];
			const FVector Dir = End[1];
			const FVector R2 = V3(-Dir.Z, 0, Dir.X);
			for (int32 K = -4; K < 5; ++K)
				Markings.box(C + Dir * 6.1 + R2 * (K * 0.95) + Y, V3(0.5, 0.004, 2.4), White, Rot);
			Markings.box(C + Dir * (SD - 0.4) + R2 * (-HW * 0.5) + Y, V3(HW - 0.2, 0.004, 0.35), White, Rot + 90.0);
		}
	}

	void build_areas()
	{
		for (const Json& A : vila::jarr(Map, "areas"))
		{
			const std::string Type = vila::jstr(A, "type");
			if (Type == "asphalt")
			{
				const Json& R = vila::jget(A, "rect");
				AsphaltB.box(V3((Num(R, 0) + Num(R, 2)) * 0.5, 0.0, (Num(R, 1) + Num(R, 3)) * 0.5), V3(Num(R, 2) - Num(R, 0), 0.02, Num(R, 3) - Num(R, 1)), WHITE_C, 0.0, TEXT("asphalt"));
				parking_lines(R);
			}
			else if (Type == "plaza" || Type == "path")
			{
				const Json& R = vila::jget(A, "rect");
				Ground.box(V3((Num(R, 0) + Num(R, 2)) * 0.5, 0.0, (Num(R, 1) + Num(R, 3)) * 0.5), V3(Num(R, 2) - Num(R, 0), 0.03, Num(R, 3) - Num(R, 1)),
					WHITE_C, 0.0, Type == "plaza" ? TEXT("plaza") : TEXT("sidewalk"));
			}
			else if (Type == "trail")
			{
				const Json& TP = vila::jarr(A, "points");
				const double TW = vila::jnum(A, "width", 2.2);
				UMaterialInterface* Dirt = UVilaNature::DirtMaterial(Assets);
				for (size_t I = 0; I + 1 < TP.size(); ++I)
				{
					const FVector Q0 = GPos2(TP[I]), Q1 = GPos2(TP[I + 1]);
					const double Len = FVector::Distance(Q0, Q1) + TW * 0.6;
					FVector Mid = (Q0 + Q1) * 0.5;
					Mid.Y = 0.012 + I * 0.0005;
					const FVector Dir = GDir((Q1 - Q0).GetSafeNormal());
					const FQuat Rot = FRotationMatrix::MakeFromX(Dir).ToQuat();
					Ground.B->AddMesh(Assets->Plane(), FTransform(Rot, G2U(Mid), FVector(Len, TW, 1.0)), WHITE_C, FString(), Dirt);
				}
			}
			else if (Type == "drive_lane")
			{
				const Json& Pts = vila::jarr(A, "points");
				for (size_t I = 0; I + 1 < Pts.size(); ++I)
				{
					const FVector P0 = GPos2(Pts[I]), P1 = GPos2(Pts[I + 1]);
					const double Len = FVector::Distance(P0, P1);
					const FVector Dir = GDir((P1 - P0).GetSafeNormal());
					const FQuat Rot = FRotationMatrix::MakeFromX(Dir).ToQuat();
					AsphaltB.B->AddBox(G2U((P0 + P1) * 0.5), Rot, FVector(Len + 4.0, 4.0, 0.024) * 100.0, WHITE_C, TEXT("asphalt"));
				}
			}
		}
	}

	// Vagas pintadas nos estacionamentos (linhas ao longo do lado maior).
	void parking_lines(const Json& R)
	{
		const double X0 = Num(R, 0), Z0 = Num(R, 1), X1 = Num(R, 2), Z1 = Num(R, 3);
		const FLinearColor White = GC(0.88, 0.88, 0.85);
		if (X1 - X0 >= Z1 - Z0)
		{
			for (double X = X0 + 0.3; X <= X1 - 0.3; X += 2.6)
				Markings.box(V3(X, 0.013, Z0 + 2.6), V3(0.1, 0.004, 4.6), White);
		}
		else
		{
			for (double Z = Z0 + 0.3; Z <= Z1 - 0.3; Z += 2.6)
				Markings.box(V3(X0 + 2.6, 0.013, Z), V3(4.6, 0.004, 0.1), White);
		}
	}

	// Mobiliário urbano decorativo: postes de energia com fiação, hidrantes e bueiros.
	// Só objetos fora das faixas de circulação (junto ao meio-fio, longe de cruzamentos,
	// entradas de garagem e portas).
	void build_street_furniture()
	{
		const Json& R = vila::jobj(Map, "roads");
		const Json& XS = vila::jarr(R, "xs");
		const Json& ZS = vila::jarr(R, "zs");
		if (XS.size() < 1 || ZS.size() < 1) return;
		const double HW = vila::jnum(R, "width", 9.0) * 0.5;
		TArray<FVector> Blocked;
		for (const Json& A : vila::jarr(Map, "areas"))
		{
			const std::string Type = vila::jstr(A, "type");
			if (Type == "drive_lane")
			{
				for (const Json& Q : vila::jarr(A, "points")) Blocked.Add(GPos2(Q));
			}
			else if (Type == "asphalt")
			{
				const Json& RR = vila::jget(A, "rect");
				for (int32 T = 0; T < 5; ++T)
					Blocked.Add(V3(FMath::Lerp(Num(RR, 0), Num(RR, 2), T / 4.0), 0, FMath::Lerp(Num(RR, 1), Num(RR, 3), T / 4.0)));
			}
		}
		for (const Json& B : vila::jarr(Map, "buildings"))
		{
			const FVector C = GPos2(vila::jget(B, "center"));
			const Json& Sz = vila::jget(B, "size");
			for (const Json& DD : vila::jarr(B, "doors"))
			{
				const std::string Side = vila::jstr(DD, "side");
				const bool bNS = Side == "n" || Side == "s";
				const double Off = vila::jnum(DD, "offset", 0.0);
				Blocked.Add(C + SideOut(Side) * (Num(Sz, bNS ? 1 : 0) * 0.5) + (bNS ? V3(Off, 0, 0) : V3(0, 0, Off)));
			}
		}
		for (const Json& P : vila::jarr(Map, "props"))
			if (vila::has(P, "pos")) Blocked.Add(GPos2(P["pos"]));
		auto Free = [&](const FVector& Q, double Rad)
		{
			for (const FVector& BQ : Blocked)
				if (FVector2D(BQ.X - Q.X, BQ.Z - Q.Z).Size() < Rad) return false;
			for (const Json& X : XS)
				for (const Json& Z : ZS)
					if (FMath::Abs(Q.X - vila::to_num(X)) < HW + 4.0 && FMath::Abs(Q.Z - vila::to_num(Z)) < HW + 4.0) return false;
			return true;
		};
		const FLinearColor WoodPole = GC(0.62, 0.6, 0.56);
		const FLinearColor Wire = GC(0.08, 0.08, 0.09);
		const double XFirst = vila::to_num(XS[0]), XLast = vila::to_num(XS[XS.size() - 1]);
		// postes ao longo do lado norte de cada rua horizontal, a 35 cm do meio-fio
		for (const Json& ZJ : ZS)
		{
			const double PZ = vila::to_num(ZJ) - HW - 0.35;
			TArray<FVector> Tops;
			for (double X = XFirst - 20.0; X <= XLast + 20.0; X += 22.0)
			{
				const FVector Q = V3(X, 0, PZ);
				if (Free(Q, 3.5) && FMath::Abs(X) < 84.0)
				{
					Deco.add_cylinder(Q + V3(0, 3.75, 0), 0.12, 7.5, WoodPole, 10, TEXT("concrete"));
					Deco.box(Q + V3(0, 7.1, 0.0), V3(1.6, 0.1, 0.1), WOOD_DARK, 0.0, TEXT("wood_out"));
					for (const double K : {-0.7, 0.0, 0.7})
						Deco.add_cylinder(Q + V3(K, 7.22, 0), 0.04, 0.14, GC(0.85, 0.85, 0.8), 6, FString());
					Deco.box(Q + V3(0, 5.6, 0.18), V3(0.45, 0.6, 0.3), GC(0.45, 0.47, 0.48), 0.0, TEXT("metal"));
					add_cyl_collider(Q + V3(0, 1.5, 0), 0.14, 3.0);
					Tops.Add(Q + V3(0, 7.28, 0));
				}
			}
			for (int32 I = 0; I + 1 < Tops.Num(); ++I)
			{
				const FVector A0 = Tops[I], A1 = Tops[I + 1];
				if (FVector::Distance(A0, A1) > 50.0) continue;
				for (const double K : {-0.7, 0.0, 0.7})
					wire(A0 + V3(K, 0, 0), A1 + V3(K, 0, 0), 0.55, Wire);
			}
		}
		// hidrantes perto das esquinas e bueiros nas pistas
		const double SH = UVilaWorldBuilder::SIDEWALK_H;
		for (const Json& XJ : XS)
			for (const Json& ZJ : ZS)
			{
				const double X = vila::to_num(XJ), Z = vila::to_num(ZJ);
				const FVector HQ = V3(X + HW + 5.0, SH, Z + HW + 0.5);
				const FLinearColor Red = GC(0.72, 0.18, 0.14);
				Deco.add_cylinder(HQ + V3(0, 0.3, 0), 0.11, 0.6, Red, 12, TEXT("metal"));
				Deco.add_sphere(HQ + V3(0, 0.62, 0), V3(0.12, 0.09, 0.12), Red, 4, 10, TEXT("metal"));
				Deco.add_tube(HQ + V3(-0.18, 0.4, 0), HQ + V3(0.18, 0.4, 0), 0.05, Darkened(Red, 0.2), 8, TEXT("metal"));
				for (const FVector& D : {V3(1, 0, 0), V3(0, 0, 1)})
				{
					const FVector MQ = V3(X, 0.011, Z) + D * 16.0 + V3(D.Z, 0, D.X) * 1.8;
					Markings.add_cylinder(MQ, 0.36, 0.006, GC(0.22, 0.22, 0.23), 16, TEXT("steel"));
				}
			}
	}

	void wire(const FVector& A, const FVector& B, double Sag, const FLinearColor& Col)
	{
		const int32 N = 8;
		FVector Prev = A;
		for (int32 I = 1; I <= N; ++I)
		{
			const double T = static_cast<double>(I) / N;
			const FVector Q = FMath::Lerp(A, B, T) - V3(0, Sag * 4.0 * T * (1.0 - T), 0);
			Deco.add_tube(Prev, Q, 0.012, Col, 4, FString());
			Prev = Q;
		}
	}

	// Semáforos (TrafficSystem._build_signal do Godot): poste, caixa, viseiras e 3 lâmpadas.
	void build_signals()
	{
		const Json& Signals = vila::jarr(vila::jobj(Map, "roads"), "signals");
		const FLinearColor PoleCol = Lin(GC(0.3, 0.33, 0.33));
		const FLinearColor BoxCol = Lin(GC(0.14, 0.15, 0.16));
		const FLinearColor Cols[3] = {GC(1, 0.15, 0.1), GC(1, 0.75, 0.1), GC(0.2, 1, 0.35)};
		const TCHAR* ColNames[3] = {TEXT("red"), TEXT("yellow"), TEXT("green")};
		UMaterialInterface* Em = Assets->Master(TEXT("M_Emissive"));
		W.SignalMode = Em ? 1 : 0;
		for (const Json& S : Signals)
		{
			const FVector Center = V3(Num(S, 0), 0, Num(S, 1));
			const double HW = 4.5 + 1.2;
			struct FHead { FVector Pos; double Facing; const TCHAR* Axis; };
			const FHead Heads[4] = {
				{V3(HW, 0, HW), 180.0, TEXT("ns")}, {V3(-HW, 0, -HW), 0.0, TEXT("ns")},
				{V3(HW, 0, -HW), -90.0, TEXT("ew")}, {V3(-HW, 0, HW), 90.0, TEXT("ew")}};
			for (const FHead& H : Heads)
			{
				const FGX Root(YRot(H.Facing), Center + H.Pos);
				Deco.add_cylinder(Root * V3(0, 1.7, 0), 0.08, 3.4, PoleCol, 10, TEXT("metal"));
				Deco.add_box(Root * At(V3(0, 3.2, 0)), V3(0.35, 0.95, 0.25), BoxCol, TEXT("metal"));
				for (int32 I = 0; I < 3; ++I)
					Deco.add_box(Root * At(V3(0, 3.62 - I * 0.29, -0.2)), V3(0.24, 0.03, 0.14), BoxCol, TEXT("metal"));
				add_cyl_collider(Root * V3(0, 1.5, 0), 0.1, 3.0);
				for (int32 I = 0; I < 3; ++I)
				{
					UMaterialInstanceDynamic* MID = Assets->MakeMID(Em, &W);
					const FLinearColor Base = Lin(Cols[I]);
					MID->SetVectorParameterValue(TEXT("Color"), Lin(Scale(Cols[I], 0.3)));
					MID->SetScalarParameterValue(TEXT("Intensity"), 0.f);
					const FTransform Xf(FQuat::Identity, G2U(Root * V3(0, 3.5 - I * 0.29, -0.14)), FVector(0.18));
					UStaticMeshComponent* Lamp = MeshComp(Static->Root, Assets->Sphere(), Xf, MID, EComponentMobility::Movable, false);
					W.SignalLamps.Add(Lamp);
					W.SignalMIDs.Add(MID);
					W.SignalBaseColors.Add(Base);
					W.SignalAxisGodot.Add(H.Axis);
					W.SignalAxis.Add(FString(H.Axis) == TEXT("ew") ? TEXT("x") : TEXT("z"));
					W.SignalColor.Add(ColNames[I]);
				}
			}
		}
	}

	// ---------------------------------------------------------------- prédios
	// Acabamento externo do prédio: [material, tinta]. Pode ser fixado em style.facade.
	TPair<FString, FLinearColor> facade(const Json& B) const
	{
		const Json& Style = vila::jobj(B, "style");
		const FLinearColor WallCol = HexRaw(vila::jstr(Style, "wall", "#dddddd"));
		std::string F = vila::jstr(Style, "facade", "");
		if (F.empty())
		{
			const std::string T = vila::jstr(B, "type");
			F = T == "shop" || T == "apartment" ? "brick" : (T == "garage" ? "concrete" : "plaster");
		}
		if (F == "brick") return TPair<FString, FLinearColor>(TEXT("brick"), Mix(WallCol, WHITE_C, 0.72));
		if (F == "concrete") return TPair<FString, FLinearColor>(TEXT("concrete"), Mix(WallCol, WHITE_C, 0.35));
		if (F == "siding") return TPair<FString, FLinearColor>(TEXT("siding"), WallCol);
		return TPair<FString, FLinearColor>(TEXT("plaster"), WallCol);
	}

	struct FOpening
	{
		double Offset, Width;
		const Json* Door;
	};

	void build_building(const Json& B)
	{
		const FVector C = GPos2(vila::jget(B, "center"));
		const Json& Sz = vila::jget(B, "size");
		const double Wd = Num(Sz, 0, 1.0), D = Num(Sz, 1, 1.0);
		const double H = vila::jnum(B, "height", 3.0);
		const Json& Style = vila::jobj(B, "style");
		const FLinearColor WallCol = HexRaw(vila::jstr(Style, "wall", "#dddddd"));
		const FLinearColor TrimCol = HexRaw(vila::jstr(Style, "trim", "#ffffff"));
		const FLinearColor RoofCol = HexRaw(vila::jstr(Style, "roof", "#884444"));
		const FLinearColor Accent = HexRaw(vila::jstr(Style, "accent", "#446688"));
		const FString Id = FS(vila::jstr(B, "id"));
		const std::string Type = vila::jstr(B, "type");

		AVilaBuilding* Node = UW->SpawnActor<AVilaBuilding>(AVilaBuilding::StaticClass(), G2U(C), FRotator::ZeroRotator, SpawnParams());
		if (!Node) return;
		Node->Setup(B);
		W.Buildings.Add(Id, Node);
		Node->Interior = Node->MakeContainer(TEXT("Interior"));

		// Vidro das janelas (um MID por prédio; o SetNight acende)
		UMaterialInterface* WinParent = Assets->Master(TEXT("M_Windows"));
		Node->WindowMode = WinParent ? 1 : 0;
		if (!WinParent)
		{
			WinParent = Assets->Master(TEXT("M_Emissive"));
			Node->WindowMode = WinParent ? 2 : 0;
		}
		Node->WindowMID = Assets->MakeMID(WinParent, Node);
		if (Node->WindowMode != 1) Node->WindowMID->SetVectorParameterValue(TEXT("Color"), Lin(GC(0.42, 0.55, 0.66)));
		Node->WindowMID->SetScalarParameterValue(TEXT("Intensity"), 0.f);
		Node->WindowMID->SetScalarParameterValue(TEXT("Night"), 0.f);

		// Piso interno
		UVilaMeshBatch* InteriorB = NewBatch(0.7f, true);
		const FGBatch Interior = MakeG(InteriorB, C);
		const bool bPlanks = (Type == "house" || Type == "shop") && Id != TEXT("mercadinho");
		const FLinearColor FCol = HexRaw(vila::jstr(Style, "floor", "#a08060"));
		const FString FMat = bPlanks ? TEXT("wood_floor") : (Type == "garage" ? TEXT("concrete") : TEXT("porcelain"));
		UMaterialInterface* FloorMat = Assets->Surface(FMat, Lin(bPlanks ? Mix(FCol, WHITE_C, 0.45) : Mix(FCol, WHITE_C, 0.6)));
		Interior.add_box(At(V3(0, 0, 0)), V3(Wd - 0.05, 0.05, D - 0.05), WHITE_C, FString(), FloorMat);

		// Paredes
		TMap<FString, TArray<FOpening>> Openings;
		for (const TCHAR* S : {TEXT("n"), TEXT("s"), TEXT("e"), TEXT("w")}) Openings.Add(FString(S));
		for (const Json& Door : vila::jarr(B, "doors"))
		{
			const FString Side = FS(vila::jstr(Door, "side", "s"));
			if (TArray<FOpening>* Arr = Openings.Find(Side))
				Arr->Add({vila::jnum(Door, "offset", 0.0), vila::jnum(Door, "width", 1.1), &Door});
		}
		for (const char* S : {"n", "s", "e", "w"})
			build_wall_side(Node, B, S, Wd, D, H, Openings[FS(S)], WallCol, TrimCol, Accent);

		// Telhado
		Node->Roof = Node->MakeContainer(TEXT("Roof"));
		UVilaMeshBatch* RoofB = NewBatch(0.8f, true);
		const FGBatch RB = MakeG(RoofB, C);
		if (Type == "house")
			gable_roof(RB, B, Wd, D, H, RoofCol, WallCol, TrimCol);
		else
			flat_roof(RB, B, Wd, D, H, RoofCol, TrimCol);
		RoofB->Flush(Node->Roof, TEXT("RoofMesh"));

		// Luz interna (noite)
		if (!vila::jbool(B, "closed_interior", false))
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(Node, TEXT("Lamp"));
			L->SetMobility(EComponentMobility::Movable);
			L->IntensityUnits = ELightUnits::Lumens;
			L->Intensity = 1.1f * 1000.f;  // light_energy 1.1 do Godot
			L->AttenuationRadius = static_cast<float>(FMath::Max(Wd, D) * 0.85 * 100.0);
			L->SetLightColor(Lin(GC(1.0, 0.86, 0.66)));
			L->SetCastShadows(true);  // sem sombra a luz vazaria pelas paredes
			L->SetupAttachment(Node->Root);
			L->SetRelativeLocation(FVector(0, 0, (H - 0.5) * 100.0));
			L->SetVisibility(false);
			L->RegisterComponent();
			Node->AddInstanceComponent(L);
			Node->Lamp = L;
		}

		// Mobília e paredes internas
		CurrentBuildingOrigin = C;
		for (const Json& F : vila::jarr(B, "furniture"))
			build_furniture(Node, B, F, Interior, Accent);
		InteriorB->Flush(Node->Interior, TEXT("Interior"));

		// Placa do estabelecimento
		if (vila::has(Style, "sign"))
		{
			const Json& Doors = vila::jarr(B, "doors");
			const std::string FSide = Doors.size() > 0 ? vila::jstr(Doors[0], "side", "s") : "s";
			add_sign(Node, FSide, C, Wd, D, H, FS(vila::to_str(Style["sign"])), Accent);
		}
		const Json& BS = vila::jobj(B, "spots");
		for (auto It = BS.begin(); It != BS.end(); ++It)
		{
			const Json& S = It.value();
			add_spot(FS(It.key()), C + GPos2(vila::jget(S, "pos")), vila::jnum(S, "rot", 0.0), FS(vila::jstr(S, "pose", "stand")), vila::jbool(S, "hidden", false), Id);
		}
	}

	void build_wall_side(AVilaBuilding* Node, const Json& B, const std::string& Side, double Wd, double D, double H,
		const TArray<FOpening>& Openings, const FLinearColor& WallCol, const FLinearColor& TrimCol, const FLinearColor& Accent)
	{
		const double WALL_T = UVilaWorldBuilder::WALL_T;
		const FSideFrame F = SideFrame(Side, Wd, D);
		const FVector Origin = GPos2(vila::jget(B, "center"));
		UVilaMeshBatch* FullB = NewBatch(0.85f, true);
		UVilaMeshBatch* LowB = NewBatch(0.85f, true);
		UVilaMeshBatch* PanesB = NewBatch(0.1f, false);
		PanesB->MaterialOverride = Node->WindowMID;
		const FGBatch Full = MakeG(FullB, Origin), Low = MakeG(LowB, Origin), Panes = MakeG(PanesB, Origin);
		const double Half = F.Len * 0.5;
		const TPair<FString, FLinearColor> Fac = facade(B);
		const FString Ext = Fac.Key;
		const FLinearColor ExtCol = Fac.Value;
		const FLinearColor InCol = Mix(WallCol, WHITE_C, 0.55);
		const double Skin = 0.03;
		const FVector SkinOff = F.Out * (WALL_T * 0.5 + Skin * 0.5);
		const double Extra = (Side == "n" || Side == "s") ? 0.07 : 0.0;  // cantos cobertos pelo revestimento
		const FString PlasterIn = TEXT("plaster_in"), Concrete = TEXT("concrete"), Metal = TEXT("metal");
		const FString BId = FS(vila::jstr(B, "id"));
		const std::string Type = vila::jstr(B, "type");

		// Intervalos sólidos entre aberturas de porta
		struct FCut { double A, E; const Json* Door; };
		TArray<FCut> Cuts;
		for (const FOpening& O : Openings) Cuts.Add({O.Offset - O.Width * 0.5, O.Offset + O.Width * 0.5, O.Door});
		Cuts.Sort([](const FCut& A, const FCut& Bc) { return A.A < Bc.A; });
		TArray<TPair<double, double>> Segs;
		double Cur = -Half;
		for (const FCut& Cut : Cuts)
		{
			if (Cut.A > Cur) Segs.Emplace(Cur, Cut.A);
			Cur = Cut.E;
		}
		if (Cur < Half) Segs.Emplace(Cur, Half);
		for (int32 I = 0; I < Segs.Num(); ++I)
		{
			const double Mid = (Segs[I].Key + Segs[I].Value) * 0.5;
			const double Len = Segs[I].Value - Segs[I].Key;
			if (Len <= 0.01) continue;
			const double E0 = (I == 0 && FMath::IsNearlyEqual(Segs[I].Key, -Half, 1e-4)) ? Extra : 0.0;
			const double E1 = (I == Segs.Num() - 1 && FMath::IsNearlyEqual(Segs[I].Value, Half, 1e-4)) ? Extra : 0.0;
			const FVector Center = F.Fixed + F.Axis * Mid;
			const FVector SkCenter = Center + F.Axis * (E1 - E0) * 0.5;
			Full.box(Center + V3(0, H * 0.5, 0), V3(Len, H, WALL_T), InCol, F.Rot, PlasterIn);
			Full.box(SkCenter + SkinOff + V3(0, H * 0.5, 0), V3(Len + E0 + E1, H, Skin), ExtCol, F.Rot, Ext);
			// rodapé de concreto e cimalha
			Full.box(SkCenter + V3(0, 0.2, 0) + F.Out * (WALL_T * 0.5 + Skin + 0.015), V3(Len + E0 + E1, 0.4, 0.03), GC(0.62, 0.6, 0.57), F.Rot, Concrete);
			Full.box(SkCenter + V3(0, H - 0.09, 0) + F.Out * (WALL_T * 0.5 + Skin + 0.02), V3(Len + E0 + E1, 0.18, 0.05), TrimCol, F.Rot, Concrete);
			Low.box(Center + V3(0, 0.3, 0), V3(Len, 0.6, WALL_T), InCol, F.Rot, PlasterIn);
			Low.box(SkCenter + SkinOff + V3(0, 0.3, 0), V3(Len + E0 + E1, 0.6, Skin), ExtCol, F.Rot, Ext);
			add_collider(Origin + Center + V3(0, H * 0.5, 0), V3(Len, H, WALL_T), F.Rot);
		}
		for (const FCut& Cut : Cuts)
		{
			const Json& DD = *Cut.Door;
			const double DoorH = vila::jbool(DD, "rollup", false) ? 3.4 : UVilaWorldBuilder::DOOR_H;
			const double Mid2 = (Cut.A + Cut.E) * 0.5;
			const double Len2 = Cut.E - Cut.A;
			const FVector Center2 = F.Fixed + F.Axis * Mid2;
			if (H > DoorH)
			{
				Full.box(Center2 + V3(0, (H + DoorH) * 0.5, 0), V3(Len2, H - DoorH, WALL_T), InCol, F.Rot, PlasterIn);
				Full.box(Center2 + SkinOff + V3(0, (H + DoorH) * 0.5, 0), V3(Len2, H - DoorH, Skin), ExtCol, F.Rot, Ext);
				add_collider(Origin + Center2 + V3(0, (H + DoorH) * 0.5, 0), V3(Len2, H - DoorH, WALL_T), F.Rot);
			}
			// batente e soleira
			const FLinearColor Jamb = Mix(TrimCol, GC(0.85, 0.83, 0.78), 0.3);
			Full.box(Center2 + V3(0, DoorH + 0.06, 0), V3(Len2 + 0.16, 0.12, WALL_T + Skin * 2 + 0.06), Jamb, F.Rot, Concrete);
			for (const double Sgn : {-1.0, 1.0})
				Full.box(Center2 + F.Axis * Sgn * (Len2 * 0.5 + 0.035) + V3(0, DoorH * 0.5, 0), V3(0.07, DoorH, WALL_T + Skin * 2 + 0.04), Jamb, F.Rot, Concrete);
			Full.box(Center2 + V3(0, 0.015, 0) + F.Out * 0.12, V3(Len2 + 0.16, 0.03, 0.4), GC(0.7, 0.68, 0.64), F.Rot, Concrete);
			// Porta (W2): origem embaixo e no centro do vão, +X para fora do prédio.
			AVilaDoor* Door = UW->SpawnActor<AVilaDoor>(AVilaDoor::StaticClass(), G2U(Origin + Center2), FRotator(0.0, OutYaw(Side), 0.0), SpawnParams());
			if (Door)
			{
				Door->Setup(DD, W.VilaWorld, BId, static_cast<float>(WALL_T * 100.0), Lin(Darkened(Accent, 0.15)));
				W.Doors.Add(FS(vila::jstr(DD, "id")), Door);
			}
		}
		// Janelas: vidro (acende à noite), caixilho com travessas, peitoril e verga
		const int32 Floors = vila::jint(B, "floors", 1);
		for (const Json& Win : vila::jarr(B, "windows"))
		{
			if (vila::jstr(Win, "side") != Side) continue;
			const double Off = vila::jnum(Win, "offset", 0.0);
			const double WW = vila::jnum(Win, "width", 1.2);
			const bool bLow = vila::jbool(Win, "low", false);
			double WH = bLow ? 1.7 : 1.1;
			double WY = bLow ? 1.25 : 1.55;
			if (vila::jbool(Win, "drive", false))
			{
				WH = 1.0;
				WY = 1.35;
			}
			for (int32 Fl = 0; Fl < Floors; ++Fl)
			{
				const double OY = 3.1 * Fl;
				const FVector WC = F.Fixed + F.Axis * Off + V3(0, OY, 0);
				window(Full, Panes, WC, F, WW, WH, WY, TrimCol, bLow, Type, Accent, Fl == 0);
			}
		}
		// Toldo na fachada de lojas
		const Json& Doors = vila::jarr(B, "doors");
		if ((Type == "shop" || Type == "restaurant") && Doors.size() > 0 && vila::jstr(Doors[0], "side") == Side)
		{
			const FVector AW = F.Fixed + F.Out * 0.85 + V3(0, 2.78, 0);
			const double AwLen = FMath::Min(F.Len - 1.0, 9.0);
			const double Tilt = FMath::DegreesToRadians(-14.0) * ((Side == "s" || Side == "e") ? 1.0 : -1.0);
			Full.add_box(FGX(YRot(F.Rot) * FGB::Axis(V3(1, 0, 0), Tilt), AW), V3(AwLen, 0.05, 1.65), Accent, TEXT("canvas"));
			Full.box(AW + F.Out * 0.8 + V3(0, -0.28, 0), V3(AwLen, 0.22, 0.02), Darkened(Accent, 0.15), F.Rot, TEXT("canvas"));
			for (const double Sgn : {-1.0, 1.0})
				Full.add_tube(AW + F.Axis * Sgn * AwLen * 0.48 + F.Out * 0.78 + V3(0, -0.15, 0),
					F.Fixed + F.Axis * Sgn * AwLen * 0.48 + F.Out * (WALL_T * 0.5 + Skin) + V3(0, 3.1, 0), 0.015, GC(0.3, 0.3, 0.32), 5, TEXT("steel"));
		}
		const FString SideName = FS(Side);
		USceneComponent* Cont = Node->MakeContainer(TEXT("Wall_") + SideName);
		FullB->Flush(Cont, TEXT("Wall"));
		PanesB->Flush(Cont, TEXT("Panes"));
		USceneComponent* LowC = Node->MakeContainer(TEXT("WallLow_") + SideName);
		LowB->Flush(LowC, TEXT("WallLow"));
		LowC->SetVisibility(false, true);
		Node->SideFull.Add(SideName, Cont);
		Node->SideLow.Add(SideName, LowC);
	}

	void window(const FGBatch& Full, const FGBatch& Panes, const FVector& WC, const FSideFrame& F, double WW, double WH, double WY,
		const FLinearColor& TrimCol, bool bLow, const std::string& Type, const FLinearColor& Accent, bool bGround)
	{
		const double WALL_T = UVilaWorldBuilder::WALL_T;
		const double Depth = WALL_T + 0.08;
		const FLinearColor FrameCol = Mix(TrimCol, GC(0.92, 0.91, 0.88), 0.4);
		const FVector C = WC + V3(0, WY, 0);
		const FString Metal = TEXT("metal"), Concrete = TEXT("concrete"), WoodOut = TEXT("wood_out");
		Panes.box(C, V3(WW, WH, 0.03), WHITE_C, F.Rot);
		const double T = 0.06;
		Full.box(C + V3(0, WH * 0.5 + T * 0.5, 0), V3(WW + T * 2, T, Depth), FrameCol, F.Rot, Metal);
		Full.box(C - V3(0, WH * 0.5 + T * 0.5, 0), V3(WW + T * 2, T, Depth), FrameCol, F.Rot, Metal);
		for (const double Sgn : {-1.0, 1.0})
			Full.box(C + F.Axis * Sgn * (WW * 0.5 + T * 0.5), V3(T, WH, Depth), FrameCol, F.Rot, Metal);
		// travessas: vertical no meio e horizontal no terço superior (janela de correr)
		if (WW > 0.9) Full.box(C, V3(0.04, WH, 0.07), FrameCol, F.Rot, Metal);
		if (!bLow) Full.box(C + V3(0, WH * 0.18, 0), V3(WW, 0.035, 0.06), FrameCol, F.Rot, Metal);
		// peitoril saliente e verga
		Full.box(C - V3(0, WH * 0.5 + T + 0.03, 0) + F.Out * (WALL_T * 0.5 + 0.08), V3(WW + 0.3, 0.06, 0.22), GC(0.72, 0.7, 0.66), F.Rot, Concrete);
		Full.box(C + V3(0, WH * 0.5 + T + 0.06, 0) + F.Out * (WALL_T * 0.5 + 0.04), V3(WW + 0.24, 0.12, 0.08), GC(0.66, 0.64, 0.6), F.Rot, Concrete);
		if (!bLow && Type == "house" && bGround)
		{
			for (const double Sgn : {-1.0, 1.0})
			{
				// venezianas de madeira com ripas
				const FVector SC = C + F.Axis * Sgn * (WW * 0.5 + 0.26) + F.Out * (WALL_T * 0.5 + 0.06);
				Full.box(SC, V3(0.4, WH + 0.12, 0.04), Accent, F.Rot, WoodOut);
				for (int32 K = 0; K < 6; ++K)
					Full.box(SC + V3(0, -WH * 0.42 + K * WH * 0.17, 0) + F.Out * 0.025, V3(0.34, 0.03, 0.015), Darkened(Accent, 0.2), F.Rot, WoodOut);
			}
		}
		if (bLow && (Type == "shop" || Type == "restaurant"))
			Full.box(C - V3(0, WH * 0.5 + 0.25, 0) + F.Out * (WALL_T * 0.5 + 0.02), V3(WW + 0.12, 0.5, 0.05), GC(0.35, 0.36, 0.38), F.Rot, TEXT("steel"));
	}

	void gable_roof(const FGBatch& RB, const Json& B, double Wd, double D, double H, const FLinearColor& RoofCol, const FLinearColor& WallCol, const FLinearColor& TrimCol)
	{
		// cumeeira ao longo de X; águas para ±Z; beiral de 45 cm
		const double WALL_T = UVilaWorldBuilder::WALL_T;
		const double Ov = 0.45;
		const double X0 = -Wd * 0.5 - Ov, X1 = Wd * 0.5 + Ov;
		const double Z0 = -D * 0.5 - Ov, Z1 = D * 0.5 + Ov;
		const double Rise = 1.5;
		const double Eave = H - Ov * Rise / (D * 0.5 + Ov) + 0.05;
		const double Top = H + Rise * (D * 0.5) / (D * 0.5 + Ov) + 0.05;
		const FLinearColor TileCol = Mix(RoofCol, GC(0.62, 0.32, 0.22), 0.55);
		const FString Tiles = TEXT("roof_tiles"), WoodOut = TEXT("wood_out");
		RB.add_quad(V3(X0, Eave, Z0), V3(X1, Eave, Z0), V3(X1, Top, 0), V3(X0, Top, 0), TileCol, Tiles, V3(0, 1, -1));
		RB.add_quad(V3(X1, Eave, Z1), V3(X0, Eave, Z1), V3(X0, Top, 0), V3(X1, Top, 0), TileCol, Tiles, V3(0, 1, 1));
		// espessura das telhas na borda e forro do beiral (visto de baixo)
		RB.add_quad(V3(X0, Eave - 0.06, Z0), V3(X1, Eave - 0.06, Z0), V3(X1, Top - 0.06, 0), V3(X0, Top - 0.06, 0), TrimCol, WoodOut, V3(0, -1, 0));
		RB.add_quad(V3(X1, Eave - 0.06, Z1), V3(X0, Eave - 0.06, Z1), V3(X0, Top - 0.06, 0), V3(X1, Top - 0.06, 0), TrimCol, WoodOut, V3(0, -1, 0));
		for (const double ZZ : {Z0, Z1})
			RB.box(V3(0, Eave - 0.06, ZZ), V3(X1 - X0 + 0.04, 0.16, 0.04), TrimCol, 0.0, WoodOut);
		// cumeeira (peças de capa)
		RB.add_tube(V3(X0 - 0.02, Top + 0.02, 0), V3(X1 + 0.02, Top + 0.02, 0), 0.09, Darkened(TileCol, 0.12), 8, Tiles);
		// empenas (triângulos de parede nas laterais) e testeiras inclinadas
		const TPair<FString, FLinearColor> Fac = facade(B);
		for (const double SX : {-1.0, 1.0})
		{
			const double XX = SX * (Wd * 0.5 + WALL_T * 0.5 + 0.03);
			RB.add_tri(V3(XX, H - 0.02, -D * 0.5 - 0.1), V3(XX, H - 0.02, D * 0.5 + 0.1), V3(XX, Top - 0.05, 0), Fac.Value, Fac.Key);
			for (const double ZS : {-1.0, 1.0})
				RB.add_tube(V3(SX * (X1 - 0.01), Eave - 0.02, ZS * Z1), V3(SX * (X1 - 0.01), Top - 0.02, 0), 0.05, TrimCol, 4, WoodOut);
		}
		// chaminé de tijolo
		RB.box(V3(Wd * 0.25, Top - 0.3, -D * 0.18), V3(0.55, 1.5, 0.55), GC(0.95, 0.9, 0.86), 0.0, TEXT("brick"));
		RB.box(V3(Wd * 0.25, Top + 0.48, -D * 0.18), V3(0.65, 0.08, 0.65), GC(0.6, 0.58, 0.55), 0.0, TEXT("concrete"));
	}

	void flat_roof(const FGBatch& RB, const Json& B, double Wd, double D, double H, const FLinearColor& RoofCol, const FLinearColor& TrimCol)
	{
		const std::string Type = vila::jstr(B, "type");
		const bool bGarage = Type == "garage";
		const FString Concrete = TEXT("concrete"), Steel = TEXT("steel"), Metal = TEXT("metal");
		RB.box(V3(0, H + 0.1, 0), V3(Wd + 0.26, 0.2, D + 0.26), Mix(RoofCol, GC(0.6, 0.6, 0.6), 0.65), 0.0, bGarage ? TEXT("roof_fiber") : TEXT("roof_flat"));
		const FLinearColor ParCol = Mix(TrimCol, GC(0.75, 0.73, 0.7), 0.5);
		for (const double Sg : {-1.0, 1.0})
		{
			RB.box(V3(0, H + 0.42, Sg * (D * 0.5 + 0.06)), V3(Wd + 0.3, 0.5, 0.18), ParCol, 0.0, Concrete);
			RB.box(V3(Sg * (Wd * 0.5 + 0.06), H + 0.42, 0), V3(0.18, 0.5, D + 0.3), ParCol, 0.0, Concrete);
			RB.box(V3(0, H + 0.69, Sg * (D * 0.5 + 0.06)), V3(Wd + 0.38, 0.05, 0.26), Darkened(ParCol, 0.1), 0.0, Concrete);
			RB.box(V3(Sg * (Wd * 0.5 + 0.06), H + 0.69, 0), V3(0.26, 0.05, D + 0.38), Darkened(ParCol, 0.1), 0.0, Concrete);
		}
		// detalhes de laje: condensadoras, exaustores, alçapão, tubulações e caixa d'água
		vila::Rng R(FCrc::StrCrc32(*FS(vila::jstr(B, "id"))));
		const int32 Units = 1 + static_cast<int32>(Wd * D / 45.0);
		for (int32 I = 0; I < Units; ++I)
		{
			const FVector P = V3(R.randf_range(-Wd * 0.32, Wd * 0.32), H + 0.2, R.randf_range(-D * 0.3, D * 0.3));
			RB.box(P + V3(0, 0.4, 0), V3(1.0, 0.8, 0.75), GC(0.84, 0.84, 0.82), 0.0, Metal);
			RB.add_cylinder(P + V3(0, 0.81, 0), 0.3, 0.03, GC(0.2, 0.2, 0.22), 14, Steel);
			RB.box(P + V3(0, 0.12, 0), V3(1.1, 0.24, 0.85), GC(0.55, 0.55, 0.55), 0.0, Concrete);
			RB.add_tube(P + V3(0.5, 0.3, 0), P + V3(0.5 + R.randf_range(0.6, 1.4), 0.15, 0), 0.04, GC(0.3, 0.3, 0.32), 6, Steel);
		}
		for (int32 I = 0; I < 2 + static_cast<int32>(Wd / 6.0); ++I)
		{
			const FVector VP = V3(R.randf_range(-Wd * 0.4, Wd * 0.4), H + 0.2, R.randf_range(-D * 0.4, D * 0.4));
			RB.add_cylinder(VP + V3(0, 0.25, 0), 0.12, 0.5, GC(0.7, 0.7, 0.72), 10, Steel);
			RB.add_cylinder(VP + V3(0, 0.52, 0), 0.2, 0.05, GC(0.6, 0.6, 0.62), 10, Steel);
		}
		const FVector HP = V3(-Wd * 0.3, H + 0.2, D * 0.28);
		RB.box(HP + V3(0, 0.15, 0), V3(0.9, 0.3, 0.9), GC(0.5, 0.5, 0.48), 0.0, Concrete);
		RB.box(HP + V3(0, 0.32, 0), V3(0.8, 0.05, 0.8), GC(0.32, 0.34, 0.36), 0.0, Metal);
		if (!bGarage && Wd * D > 60.0)
		{
			const FVector TP = V3(Wd * 0.3, H + 0.2, -D * 0.25);
			RB.add_cylinder(TP + V3(0, 0.75, 0), 0.75, 1.5, GC(0.25, 0.42, 0.62), 16, Metal);
			RB.add_cylinder(TP + V3(0, 1.55, 0), 0.6, 0.12, GC(0.22, 0.38, 0.56), 16, Metal);
		}
		if (Type == "shop" || Type == "restaurant" || Type == "garage")
		{
			// exaustor de cozinha / caixa técnica
			RB.box(V3(Wd * 0.2, H + 0.6, D * 0.1), V3(1.2, 0.7, 0.9), GC(0.78, 0.79, 0.8), 0.0, Steel);
		}
	}

	void add_sign(AVilaBuilding* Node, const std::string& Side, const FVector& Origin, double Wd, double D, double H, const FString& Text, const FLinearColor& Accent)
	{
		const FSideFrame F = SideFrame(Side, Wd, D);
		USceneComponent* Cont = Node->SideFull.FindRef(FS(Side));
		if (!Cont) Cont = Node->Root;
		const FVector BoardSize = V3(FMath::Min(F.Len - 0.6, 7.0), 0.75, 0.12);
		const FVector Pos = F.Fixed + F.Out * 0.18 + V3(0, H + 0.05, 0);
		const FTransform BoardXf(YawQ(F.Rot), G2U(Origin + Pos), GSize(BoardSize) / 100.0);
		MeshComp(Cont, Assets->Cube(), BoardXf, Assets->Color(Lin(Accent), 0.6f));
		// Label3D: font 64 x pixel 0,0065 (~0,42 m); cor (1, 0.97, 0.88)
		Label(Cont, Text, G2U(Origin + Pos + F.Out * 0.08), OutYaw(Side), 36.f, FColor(255, 247, 224));
	}

	// ---------------------------------------------------------------- mobília
	void build_furniture(AVilaBuilding* Node, const Json& B, const Json& F, const FGBatch& Batch, const FLinearColor& Accent)
	{
		const FVector Origin = GPos2(vila::jget(B, "center"));
		const FString BId = FS(vila::jstr(B, "id"));
		const std::string Type = vila::jstr(F, "type");
		if (Type == "partition")
		{
			const FVector A = GPos2(vila::jget(F, "from"));
			const FVector E = GPos2(vila::jget(F, "to"));
			const double Len = FVector::Distance(A, E) + 0.12;
			const double Rot = FMath::Abs(A.Z - E.Z) < 0.01 ? 0.0 : 90.0;
			const double H = vila::jnum(B, "height", 3.0) - 0.05;
			Batch.box((A + E) * 0.5 + V3(0, H * 0.5, 0), V3(Len, H, 0.12), Mix(HexRaw(vila::jstr(vila::jobj(B, "style"), "wall", "#dddddd")), WHITE_C, 0.55), Rot, TEXT("plaster_in"));
			add_collider(Origin + (A + E) * 0.5 + V3(0, H * 0.5, 0), V3(Len, H, 0.12), Rot);
			return;
		}
		const FVector LP = GPos2(vila::jget(F, "pos"));
		const double RotDeg = vila::jnum(F, "rot", 0.0);
		const FGX Xf(YRot(RotDeg), LP);
		const bool bInteractive = IsInteractiveType(Type) && (Type != "crate" || vila::has(F, "container")) && (Type != "counter" || vila::has(F, "shop"));
		AVilaFurniture* Fn = nullptr;
		UVilaMeshBatch* OwnB = nullptr;
		if (bInteractive)
		{
			Fn = SpawnFurniture(F, Origin + LP, RotDeg, BId);
			OwnB = FurnitureBatch(0.7f);
		}
		const FGBatch Target = OwnB ? MakeG(OwnB, Origin) : Batch;
		const FVector ColSize = furniture_visual(Target, F, Xf, Accent);
		if (!ColSize.IsZero())
			add_collider(Origin + Xf * V3(0, ColSize.Y * 0.5, 0), ColSize, RotDeg);
		if (Fn)
		{
			FlushFurniture(OwnB, Fn, Node->Interior);
			if (vila::has(F, "id")) W.Furniture.Add(FS(vila::to_str(F["id"])), Fn);
			if (vila::has(F, "container")) W.Furniture.Add(FS(vila::jstr(F["container"], "id")), Fn);
		}
		else if (OwnB)
		{
			OwnB->Flush(Node->Interior, TEXT("Visual"));
		}
		if (vila::has(F, "spot"))
		{
			const FVector WP = Origin + LP;
			const FVector Fwd = YRot(RotDeg) * V3(0, 0, -1);
			const FString Spot = FS(vila::to_str(F["spot"]));
			if (Type == "bed") add_spot(Spot, WP, RotDeg, TEXT("lie"), false, BId, Fn);
			else if (Type == "sofa" || Type == "chair") add_spot(Spot, WP + Fwd * 0.12, RotDeg, TEXT("sit"), false, BId, Fn);
			else if (Type == "table") add_spot(Spot, WP - Fwd * 0.95, RotDeg, TEXT("eat"), false, BId, Fn);
			else add_spot(Spot, WP, RotDeg, TEXT("stand"), false, BId, Fn);
		}
	}

	// Desenha a mobília com caixas coloridas. Retorna o tamanho da colisão (zero = sem).
	FVector furniture_visual(const FGBatch& BT, const Json& F, const FGX& Xf, const FLinearColor& Accent)
	{
		const double L = vila::jnum(F, "len", 1.0);
		const FLinearColor Col = HexRaw(vila::jstr(F, "color", "#8b5e34"));
		const bool bHasColor = vila::has(F, "color");
		auto Bx = [&](const FVector& P, const FVector& S, const FLinearColor& C, const TCHAR* M) { BT.add_box(Xf * At(P), S, C, M); };
		const std::string T = vila::jstr(F, "type");
		const TCHAR* Wood = TEXT("wood");
		const TCHAR* Fabric = TEXT("fabric");
		const TCHAR* Metal = TEXT("metal");
		const TCHAR* Steel = TEXT("steel");
		const TCHAR* None = TEXT("");
		if (T == "bed")
		{
			for (const double LX : {-0.65, 0.65})
				for (const double LZ : {-0.9, 0.9})
					Bx(V3(LX, 0.1, LZ), V3(0.07, 0.2, 0.07), WOOD_DARK, Wood);
			Bx(V3(0, 0.25, 0.02), V3(1.4, 0.1, 1.96), WOOD, Wood);
			Bx(V3(0, 0.4, 0.05), V3(1.32, 0.2, 1.85), GC(0.93, 0.92, 0.9), Fabric);
			Bx(V3(0, 0.51, 0.04), V3(1.28, 0.03, 1.8), GC(0.96, 0.96, 0.95), Fabric);
			for (const double PX : {-0.32, 0.32})
				Bx(V3(PX, 0.57, -0.7), V3(0.5, 0.1, 0.32), GC(0.97, 0.96, 0.94), Fabric);
			Bx(V3(0, 0.53, 0.33), V3(1.36, 0.06, 1.2), Mix(Accent, WHITE_C, 0.25), Fabric);
			Bx(V3(-0.69, 0.42, 0.33), V3(0.03, 0.26, 1.2), Mix(Accent, WHITE_C, 0.2), Fabric);
			Bx(V3(0.69, 0.42, 0.33), V3(0.03, 0.26, 1.2), Mix(Accent, WHITE_C, 0.2), Fabric);
			Bx(V3(0, 0.62, -0.98), V3(1.48, 1.0, 0.06), WOOD_DARK, Wood);
			for (const double HX : {-0.36, 0.36})
				Bx(V3(HX, 0.75, -0.95), V3(0.6, 0.5, 0.02), WOOD, Wood);
			Bx(V3(0, 1.14, -0.98), V3(1.52, 0.05, 0.1), WOOD_DARK, Wood);
			return V3(1.4, 0.6, 2.0);
		}
		if (T == "wardrobe")
		{
			Bx(V3(0, 0.05, 0), V3(1.16, 0.1, 0.56), WOOD_DARK, Wood);
			Bx(V3(0, 1.02, 0), V3(1.2, 1.86, 0.6), WOOD, Wood);
			Bx(V3(0, 1.98, 0), V3(1.26, 0.06, 0.64), WOOD_DARK, Wood);
			for (const double DX : {-0.3, 0.3})
			{
				Bx(V3(DX, 1.35, -0.305), V3(0.48, 0.9, 0.015), Darkened(WOOD, 0.08), Wood);
				Bx(V3(DX, 0.55, -0.305), V3(0.48, 0.55, 0.015), Darkened(WOOD, 0.08), Wood);
			}
			Bx(V3(0, 1.02, -0.31), V3(0.015, 1.84, 0.015), WOOD_DARK, Wood);
			for (const double HX : {-0.06, 0.06})
				BT.add_tube(Xf * V3(HX, 0.95, -0.34), Xf * V3(HX, 1.2, -0.34), 0.012, STEEL, 6, Steel);
			return V3(1.2, 2.0, 0.6);
		}
		if (T == "nightstand")
		{
			Bx(V3(0, 0.25, 0), V3(0.45, 0.5, 0.45), WOOD, Wood);
			Bx(V3(0, 0.62, 0), V3(0.18, 0.25, 0.18), HexRaw("f0d890"), None);
			return FVector::ZeroVector;
		}
		if (T == "shower")
		{
			Bx(V3(0, 0.03, 0), V3(0.95, 0.06, 0.95), WHITE, None);
			Bx(V3(0, 1.0, 0.47), V3(0.95, 2.0, 0.03), GC(0.75, 0.85, 0.9), None);
			Bx(V3(0, 1.9, -0.3), V3(0.05, 0.4, 0.05), STEEL, Steel);
			Bx(V3(0, 2.05, -0.2), V3(0.2, 0.04, 0.2), STEEL, Steel);
			return FVector::ZeroVector;
		}
		if (T == "toilet")
		{
			Bx(V3(0, 0.2, 0.05), V3(0.4, 0.4, 0.55), WHITE, None);
			Bx(V3(0, 0.55, -0.22), V3(0.42, 0.5, 0.18), WHITE, None);
			return V3(0.45, 0.8, 0.65);
		}
		if (T == "fridge")
		{
			Bx(V3(0, 0.92, 0), V3(0.7, 1.8, 0.68), WHITE, Metal);
			Bx(V3(0, 0.03, 0), V3(0.66, 0.06, 0.6), GC(0.3, 0.3, 0.32), Metal);
			Bx(V3(0, 1.32, -0.345), V3(0.68, 0.94, 0.02), Darkened(WHITE, 0.03), Metal);
			Bx(V3(0, 0.45, -0.345), V3(0.68, 0.76, 0.02), Darkened(WHITE, 0.03), Metal);
			BT.add_tube(Xf * V3(0.27, 1.0, -0.38), Xf * V3(0.27, 1.4, -0.38), 0.014, STEEL, 6, Steel);
			BT.add_tube(Xf * V3(0.27, 0.7, -0.38), Xf * V3(0.27, 0.95, -0.38), 0.014, STEEL, 6, Steel);
			return V3(0.7, 1.8, 0.7);
		}
		if (T == "stove")
		{
			Bx(V3(0, 0.46, 0), V3(0.7, 0.88, 0.62), WHITE, Metal);
			Bx(V3(0, 0.91, 0), V3(0.68, 0.025, 0.6), GC(0.12, 0.12, 0.13), Steel);
			for (const FVector& Q : {V3(-0.16, 0.93, -0.13), V3(0.16, 0.93, -0.13), V3(-0.16, 0.93, 0.13), V3(0.16, 0.93, 0.13)})
			{
				BT.add_cylinder(Xf * Q, 0.085, 0.02, GC(0.25, 0.25, 0.26), 12, Steel);
				BT.add_cylinder(Xf * (Q + V3(0, 0.015, 0)), 0.04, 0.02, GC(0.15, 0.15, 0.16), 10, Steel);
			}
			Bx(V3(0, 1.02, 0.3), V3(0.7, 0.2, 0.03), WHITE, Metal);
			Bx(V3(0, 0.42, -0.315), V3(0.5, 0.36, 0.01), GC(0.1, 0.1, 0.11), TEXT("glass"));
			Bx(V3(0, 0.67, -0.33), V3(0.5, 0.025, 0.03), STEEL, Steel);
			for (int32 K = 0; K < 4; ++K)
				BT.add_cylinder(Xf * V3(-0.21 + K * 0.14, 0.8, -0.32), 0.022, 0.03, GC(0.15, 0.15, 0.16), 8, None);
			return V3(0.7, 0.95, 0.62);
		}
		if (T == "sink")
		{
			Bx(V3(0, 0.45, 0), V3(0.7, 0.9, 0.62), WOOD, Wood);
			Bx(V3(0, 0.91, 0), V3(0.72, 0.04, 0.64), HexRaw("dddddd"), TEXT("concrete"));
			Bx(V3(0, 0.9, 0.05), V3(0.45, 0.04, 0.35), Darkened(STEEL, 0.3), Steel);
			Bx(V3(0, 1.1, 0.27), V3(0.04, 0.3, 0.04), STEEL, Steel);
			return V3(0.7, 0.95, 0.62);
		}
		if (T == "table")
		{
			Bx(V3(0, 0.75, 0), V3(1.2, 0.04, 0.8), WOOD, Wood);
			Bx(V3(0, 0.69, 0), V3(1.08, 0.08, 0.68), WOOD_DARK, Wood);
			for (const FVector& Q : {V3(-0.52, 0.36, -0.32), V3(0.52, 0.36, -0.32), V3(-0.52, 0.36, 0.32), V3(0.52, 0.36, 0.32)})
				Bx(Q, V3(0.06, 0.72, 0.06), WOOD_DARK, Wood);
			return V3(1.2, 0.78, 0.8);
		}
		if (T == "chair")
		{
			Bx(V3(0, 0.45, 0), V3(0.44, 0.04, 0.44), WOOD, Wood);
			for (const FVector& Q : {V3(-0.19, 0.22, -0.19), V3(0.19, 0.22, -0.19), V3(-0.19, 0.22, 0.19), V3(0.19, 0.22, 0.19)})
				Bx(Q, V3(0.035, 0.45, 0.035), WOOD_DARK, Wood);
			for (const double SX : {-0.19, 0.19})
				Bx(V3(SX, 0.72, 0.2), V3(0.035, 0.52, 0.035), WOOD_DARK, Wood);
			for (int32 K = 0; K < 3; ++K)
				Bx(V3(0, 0.62 + K * 0.12, 0.2), V3(0.38, 0.05, 0.02), WOOD, Wood);
			return FVector::ZeroVector;
		}
		if (T == "sofa")
		{
			const FLinearColor Fab = Mix(Accent, GC(0.55, 0.55, 0.55), 0.3);
			for (const FVector& Q : {V3(-0.9, 0.04, -0.35), V3(0.9, 0.04, -0.35), V3(-0.9, 0.04, 0.4), V3(0.9, 0.04, 0.4)})
				Bx(Q, V3(0.06, 0.08, 0.06), WOOD_DARK, Wood);
			Bx(V3(0, 0.2, 0.05), V3(1.9, 0.26, 0.85), Darkened(Fab, 0.15), Fabric);
			Bx(V3(0, 0.6, 0.38), V3(1.9, 0.55, 0.2), Darkened(Fab, 0.1), Fabric);
			for (const double SX : {-1.0, 1.0})
			{
				Bx(V3(SX * 0.95, 0.42, 0.05), V3(0.2, 0.36, 0.86), Darkened(Fab, 0.18), Fabric);
				BT.add_tube(Xf * V3(SX * 0.95, 0.6, -0.37), Xf * V3(SX * 0.95, 0.6, 0.47), 0.1, Darkened(Fab, 0.18), 10, Fabric);
			}
			for (const double CX : {-0.43, 0.43})
			{
				Bx(V3(CX, 0.4, -0.02), V3(0.83, 0.14, 0.68), Fab, Fabric);
				Bx(V3(CX, 0.68, 0.24), V3(0.8, 0.42, 0.14), Lightened(Fab, 0.03), Fabric);
			}
			Bx(V3(-0.62, 0.62, 0.12), V3(0.36, 0.3, 0.1), GC(0.86, 0.8, 0.66), Fabric);
			return V3(2.0, 0.9, 0.9);
		}
		if (T == "tv")
		{
			Bx(V3(0, 0.24, 0), V3(1.2, 0.48, 0.42), WOOD, Wood);
			Bx(V3(-0.3, 0.24, -0.215), V3(0.55, 0.36, 0.01), WOOD_DARK, Wood);
			Bx(V3(0.3, 0.24, -0.215), V3(0.55, 0.36, 0.01), WOOD_DARK, Wood);
			Bx(V3(0, 0.52, 0.05), V3(0.3, 0.04, 0.18), GC(0.12, 0.12, 0.13), Metal);
			Bx(V3(0, 0.88, 0.05), V3(1.0, 0.6, 0.04), GC(0.1, 0.1, 0.11), Metal);
			Bx(V3(0, 0.88, 0.028), V3(0.94, 0.54, 0.005), GC(0.03, 0.04, 0.05), TEXT("glass"));
			return V3(1.2, 0.6, 0.4);
		}
		if (T == "rug")
		{
			Bx(V3(0, 0.035, 0), V3(2.0, 0.01, 1.4), Lightened(Accent, 0.3), Fabric);
			return FVector::ZeroVector;
		}
		if (T == "plant")
		{
			BT.add_cylinder(Xf * V3(0, 0.18, 0), 0.18, 0.36, GC(0.72, 0.42, 0.3), 14, TEXT("concrete"));
			BT.add_cylinder(Xf * V3(0, 0.35, 0), 0.16, 0.02, GC(0.3, 0.22, 0.15), 14, TEXT("dirt"));
			vila::Rng PR(static_cast<uint64_t>(static_cast<int64>(Xf.O.X * 13.0 + Xf.O.Z * 7.0)));
			const FVector WPos = CurrentBuildingOrigin + Xf * V3(0, 0.75, 0);
			TArray<TPair<FVector, FVector>> Cl;
			Cl.Emplace(WPos, V3(0.32, 0.4, 0.32));
			W.Foliage->AddCanopy(WPos - V3(0, 0.4, 0), Cl, GC(0.28, 0.42, 0.18), PR, 26, 0.45f);
			return FVector::ZeroVector;
		}
		if (T == "shelf")
		{
			Bx(V3(0, 0.9, 0), V3(L, 1.8, 0.5), bHasColor ? Col : HexRaw("d0d4d8"), Metal);
			for (const double Lvl : {0.35, 0.8, 1.25, 1.65})
			{
				double X = -L * 0.5 + 0.2;
				while (X < L * 0.5 - 0.2)
				{
					const FLinearColor PC = FromHSV(Rng.randf(), 0.55, 0.85);
					const double BH = Rng.randf_range(0.18, 0.3);
					for (const double Sd : {-1.0, 1.0})
						Bx(V3(X, Lvl + BH * 0.5, Sd * 0.27), V3(0.22, BH, 0.06), PC, Metal);
					X += Rng.randf_range(0.25, 0.32);
				}
			}
			return V3(L, 1.8, 0.5);
		}
		if (T == "fridge_display")
		{
			Bx(V3(0, 1.0, 0), V3(L, 2.0, 0.7), HexRaw("dfe6ea"), Metal);
			Bx(V3(0, 1.05, -0.36), V3(L - 0.1, 1.7, 0.02), GC(0.6, 0.85, 0.95), None);
			for (int32 I = 0; I < static_cast<int32>(L / 0.25); ++I)
			{
				Bx(V3(-L * 0.5 + 0.15 + I * 0.25, 1.4, -0.3), V3(0.12, 0.25, 0.12), FromHSV(Rng.randf(), 0.6, 0.9), None);
				Bx(V3(-L * 0.5 + 0.15 + I * 0.25, 0.8, -0.3), V3(0.12, 0.25, 0.12), FromHSV(Rng.randf(), 0.6, 0.9), None);
			}
			return V3(L, 2.0, 0.7);
		}
		if (T == "counter")
		{
			Bx(V3(0, 0.5, 0), V3(L, 1.0, 0.7), Darkened(Accent, 0.1), Metal);
			Bx(V3(0, 1.02, 0), V3(L + 0.05, 0.05, 0.75), HexRaw("e8e2d6"), TEXT("concrete"));
			if (vila::jbool(F, "register", false))
			{
				Bx(V3(L * 0.3, 1.15, 0), V3(0.4, 0.2, 0.35), DARK, Metal);
				Bx(V3(L * 0.3, 1.3, 0.05), V3(0.3, 0.18, 0.04), HexRaw("2b3a4a"), Metal);
			}
			return V3(L, 1.05, 0.7);
		}
		if (T == "crate")
		{
			Bx(V3(0, 0.3, 0), V3(0.7, 0.6, 0.7), HexRaw("a07a4a"), Wood);
			Bx(V3(0.1, 0.8, 0.05), V3(0.55, 0.4, 0.55), HexRaw("b48a55"), Wood);
			return V3(0.7, 1.0, 0.7);
		}
		if (T == "chest")
		{
			Bx(V3(0, 0.27, 0), V3(0.95, 0.54, 0.5), bHasColor ? Col : WOOD, Wood);
			Bx(V3(0, 0.45, 0), V3(0.97, 0.06, 0.52), WOOD_DARK, Wood);
			Bx(V3(0, 0.38, -0.26), V3(0.1, 0.12, 0.03), HexRaw("c8a040"), Wood);
			return V3(0.95, 0.56, 0.5);
		}
		if (T == "grill")
		{
			Bx(V3(0, 0.45, 0), V3(1.3, 0.9, 0.7), STEEL, Steel);
			Bx(V3(0, 0.92, 0), V3(1.2, 0.05, 0.6), DARK, Metal);
			Bx(V3(0, 1.6, 0.25), V3(1.3, 0.25, 0.3), Darkened(STEEL, 0.2), Steel);
			return V3(1.3, 0.95, 0.7);
		}
		if (T == "fryer")
		{
			Bx(V3(0, 0.45, 0), V3(0.9, 0.9, 0.7), STEEL, Steel);
			Bx(V3(-0.2, 0.9, 0), V3(0.35, 0.04, 0.5), HexRaw("c9a227"), None);
			Bx(V3(0.2, 0.9, 0), V3(0.35, 0.04, 0.5), HexRaw("c9a227"), None);
			Bx(V3(0, 1.05, -0.3), V3(0.6, 0.06, 0.04), DARK, Metal);
			return V3(0.9, 0.95, 0.7);
		}
		if (T == "soda_machine")
		{
			Bx(V3(0, 0.85, 0), V3(0.8, 1.7, 0.6), HexRaw("c0392b"), Metal);
			Bx(V3(0, 1.3, -0.31), V3(0.6, 0.45, 0.02), WHITE, Metal);
			for (int32 I = 0; I < 3; ++I)
				Bx(V3(-0.2 + I * 0.2, 0.95, -0.33), V3(0.08, 0.12, 0.08), STEEL, Steel);
			return V3(0.8, 1.7, 0.6);
		}
		if (T == "drive_window")
		{
			Bx(V3(0, 1.0, 0.0), V3(1.4, 0.06, 0.5), HexRaw("e8e2d6"), TEXT("concrete"));
			Bx(V3(0, 0.5, 0.05), V3(1.4, 1.0, 0.4), Darkened(Accent, 0.1), Metal);
			Bx(V3(0.4, 1.12, 0), V3(0.35, 0.18, 0.3), DARK, Metal);
			return V3(1.4, 1.0, 0.5);
		}
		if (T == "time_clock")
		{
			Bx(V3(0, 1.4, 0.05), V3(0.35, 0.45, 0.12), HexRaw("3a4a5a"), Metal);
			Bx(V3(0, 1.48, -0.02), V3(0.22, 0.12, 0.02), HexRaw("9fe0a0"), Metal);
			return FVector::ZeroVector;
		}
		if (T == "desk")
		{
			Bx(V3(0, 0.74, 0), V3(1.4, 0.05, 0.7), WOOD, Wood);
			Bx(V3(-0.6, 0.36, 0), V3(0.08, 0.72, 0.65), WOOD_DARK, Wood);
			Bx(V3(0.6, 0.36, 0), V3(0.08, 0.72, 0.65), WOOD_DARK, Wood);
			Bx(V3(0, 1.0, 0.15), V3(0.55, 0.38, 0.04), DARK, Metal);
			return V3(1.4, 0.78, 0.7);
		}
		if (T == "kitchen_counter")
		{
			Bx(V3(0, 0.45, 0), V3(L, 0.9, 0.6), STEEL, Steel);
			Bx(V3(0, 0.92, 0), V3(L, 0.04, 0.62), Lightened(STEEL, 0.2), Steel);
			for (int32 I = 0; I < 3; ++I)
				Bx(V3(-L * 0.3 + I * L * 0.3, 1.0, 0), V3(0.3, 0.12, 0.25), HexRaw("f3c26b"), Metal);
			return V3(L, 0.95, 0.6);
		}
		if (T == "lift")
		{
			Bx(V3(-1.3, 1.0, 0), V3(0.25, 2.0, 0.25), HexRaw("d04a2a"), Metal);
			Bx(V3(1.3, 1.0, 0), V3(0.25, 2.0, 0.25), HexRaw("d04a2a"), Metal);
			Bx(V3(0, 0.08, 0), V3(2.4, 0.08, 3.6), HexRaw("444444"), Metal);
			return FVector::ZeroVector;
		}
		if (T == "workbench")
		{
			Bx(V3(0, 0.45, 0), V3(L, 0.9, 0.7), HexRaw("6a6e74"), Metal);
			Bx(V3(0, 0.92, 0), V3(L, 0.05, 0.72), WOOD, Wood);
			Bx(V3(0, 1.5, 0.32), V3(L, 1.0, 0.05), HexRaw("8a8e94"), Metal);
			return V3(L, 0.95, 0.7);
		}
		if (T == "tire_stack")
		{
			for (int32 I = 0; I < 3; ++I)
				BT.add_cylinder(Xf * V3(0, 0.12 + I * 0.24, 0), 0.32, 0.22, HexRaw("2a2a2c"), 14, TEXT("rubber"));
			return V3(0.6, 0.7, 0.6);
		}
		if (T == "note")
		{
			Bx(V3(0, 0.775, 0), V3(0.22, 0.005, 0.3), HexRaw("fffbea"), None);
			return FVector::ZeroVector;
		}
		return FVector::ZeroVector;
	}

	// ---------------------------------------------------------------- objetos externos
	void build_prop(const Json& P)
	{
		const FVector Pos = vila::has(P, "pos") ? GPos2(P["pos"]) : FVector::ZeroVector;
		const double Rot = vila::jnum(P, "rot", 0.0);
		const FGX Xf(YRot(Rot), Pos);
		const FLinearColor Iron = GC(0.24, 0.26, 0.27);
		const std::string Type = vila::jstr(P, "type");
		USceneComponent* Root = Static->Root;
		// Objetos com função (Furniture do Godot) levam o visual no próprio ator.
		auto Bx = [&](const FGBatch& G, const FVector& LP, const FVector& S, const FLinearColor& C, const TCHAR* M) { G.add_box(Xf * At(LP), S, C, M); };
		auto Tb = [&](const FGBatch& G, const FVector& A, const FVector& E, double R, const FLinearColor& C, const TCHAR* M) { G.add_tube(Xf * A, Xf * E, R, C, 8, M); };

		if (Type == "tree")
		{
			build_tree(Pos, vila::jint(P, "variant", 0));
		}
		else if (Type == "bench")
		{
			Json Def = Json::object();
			Def["type"] = "bench";
			Def["rot"] = Rot;
			Def["id"] = vila::has(P, "id") ? P["id"] : Json("");
			AVilaFurniture* Fn = SpawnFurniture(Def, Pos, Rot, FString());
			UVilaMeshBatch* FB = FurnitureBatch(0.85f);
			const FGBatch G = MakeG(FB);
			for (int32 K = 0; K < 3; ++K)
				Bx(G, V3(0, 0.45, -0.18 + K * 0.17), V3(1.8, 0.04, 0.13), WOOD, TEXT("wood_out"));
			for (int32 K = 0; K < 2; ++K)
				Bx(G, V3(0, 0.62 + K * 0.17, 0.25), V3(1.8, 0.13, 0.035), WOOD, TEXT("wood_out"));
			for (const double X : {-0.75, 0.75})
			{
				Bx(G, V3(X, 0.22, -0.05), V3(0.06, 0.44, 0.06), Iron, TEXT("metal"));
				Bx(G, V3(X, 0.22, 0.2), V3(0.06, 0.44, 0.06), Iron, TEXT("metal"));
				Bx(G, V3(X, 0.42, 0.0), V3(0.05, 0.05, 0.5), Iron, TEXT("metal"));
				Tb(G, V3(X, 0.42, 0.24), V3(X, 0.9, 0.27), 0.025, Iron, TEXT("metal"));
			}
			FlushFurniture(FB, Fn, Root);
			add_collider(Xf * V3(0, 0.4, 0.05), V3(1.8, 0.8, 0.55), Rot);
			if (Fn && vila::has(P, "id")) W.Furniture.Add(FS(vila::to_str(P["id"])), Fn);
			if (vila::has(P, "spot"))
			{
				const FVector Fwd = YRot(Rot) * V3(0, 0, -1);
				add_spot(FS(vila::to_str(P["spot"])), Pos + Fwd * 0.12, Rot, TEXT("sit"), false, FString(), Fn);
			}
		}
		else if (Type == "streetlight")
		{
			const FLinearColor Pole = GC(0.3, 0.33, 0.33);
			Bx(Deco, V3(0, 0.2, 0), V3(0.36, 0.4, 0.36), GC(0.6, 0.58, 0.55), TEXT("concrete"));
			Deco.add_cylinder(Xf * V3(0, 2.0, 0), 0.085, 3.6, Pole, 10, TEXT("metal"));
			Deco.add_cylinder(Xf * V3(0, 4.6, 0), 0.06, 1.8, Pole, 10, TEXT("metal"));
			Tb(Deco, V3(0, 5.4, 0), V3(0, 5.8, -0.4), 0.045, Pole, TEXT("metal"));
			Tb(Deco, V3(0, 5.8, -0.4), V3(0, 5.75, -1.25), 0.04, Pole, TEXT("metal"));
			Bx(Deco, V3(0, 5.7, -1.35), V3(0.5, 0.12, 0.32), Darkened(Pole, 0.2), TEXT("metal"));
			LampHeads.add_box(Xf * At(V3(0, 5.62, -1.35)), V3(0.42, 0.05, 0.26), WHITE_C);
			add_cyl_collider(Pos + V3(0, 1.5, 0), 0.12, 3.0);
			UPointLightComponent* L = NewObject<UPointLightComponent>(Static, MakeUniqueObjectName(Static, UPointLightComponent::StaticClass(), FName(TEXT("Streetlight"))));
			L->SetMobility(EComponentMobility::Movable);
			L->IntensityUnits = ELightUnits::Lumens;
			L->Intensity = 1.4f * 2500.f;  // light_energy 1.4 do Godot
			L->AttenuationRadius = 1300.f;  // omni_range 13 m
			L->SetLightColor(Lin(GC(1.0, 0.82, 0.55)));
			L->SetCastShadows(false);       // o AVilaWorldStatic liga a sombra só nos postes perto da câmera
			L->SetupAttachment(Root);
			L->SetRelativeLocation(G2U(Xf * V3(0, 5.3, -1.35)));
			L->SetVisibility(false);
			L->RegisterComponent();
			Static->AddInstanceComponent(L);
			W.Streetlights.Add(L);
			W.StreetShadow.Add(false);
		}
		else if (Type == "trash")
		{
			const Json& Cont = vila::jobj(P, "container");
			Json Def = Json::object();
			Def["type"] = "trash";
			Def["rot"] = Rot;
			Def["container"] = Cont;
			Def["id"] = vila::jget(Cont, "id");
			AVilaFurniture* Fn = SpawnFurniture(Def, Pos, Rot, FString());
			UVilaMeshBatch* FB = FurnitureBatch(0.85f);
			const FGBatch G = MakeG(FB);
			const FLinearColor Green = GC(0.42, 0.55, 0.45);
			G.add_cylinder(Pos + V3(0, 0.45, 0), 0.3, 0.9, Green, 16, TEXT("metal"));
			G.add_cylinder(Pos + V3(0, 0.88, 0), 0.32, 0.06, Darkened(Green, 0.2), 16, TEXT("metal"));
			G.add_cylinder(Pos + V3(0, 0.93, 0), 0.24, 0.06, Darkened(Green, 0.3), 16, TEXT("metal"));
			G.add_cylinder(Pos + V3(0, 0.03, 0), 0.31, 0.06, Iron, 16, TEXT("metal"));
			FlushFurniture(FB, Fn, Root);
			add_cyl_collider(Pos + V3(0, 0.45, 0), 0.3, 0.9);
			if (Fn) W.Furniture.Add(FS(vila::jstr(Cont, "id")), Fn);
		}
		else if (Type == "bus_stop")
		{
			const FLinearColor Fr = GC(0.38, 0.42, 0.45);
			Bx(Deco, V3(0, 2.45, 0.3), V3(3.3, 0.08, 1.5), GC(0.75, 0.76, 0.77), TEXT("steel"));
			for (const double X : {-1.5, 1.5})
				for (const double Z : {-0.3, 0.9})
					Bx(Deco, V3(X, 1.22, Z), V3(0.07, 2.44, 0.07), Fr, TEXT("metal"));
			Bx(Deco, V3(0, 1.35, 0.92), V3(3.0, 1.9, 0.02), WHITE_C, TEXT("glass"));
			Bx(Deco, V3(0, 0.28, 0.92), V3(3.0, 0.06, 0.05), Fr, TEXT("metal"));
			Bx(Deco, V3(0, 2.3, 0.92), V3(3.0, 0.06, 0.05), Fr, TEXT("metal"));
			Bx(Deco, V3(0, 0.46, 0.66), V3(2.4, 0.04, 0.36), WOOD, TEXT("wood_out"));
			for (const double X : {-1.0, 0.0, 1.0})
				Bx(Deco, V3(X, 0.23, 0.66), V3(0.05, 0.46, 0.3), Fr, TEXT("metal"));
			Bx(Deco, V3(1.9, 1.5, 0.3), V3(0.06, 3.0, 0.06), Fr, TEXT("metal"));
			Bx(Deco, V3(1.9, 2.8, 0.3), V3(0.55, 0.55, 0.04), GC(0.2, 0.42, 0.62), TEXT("metal"));
		}
		else if (Type == "doormat")
		{
			Json Def = Json::object();
			Def["type"] = "doormat";
			Def["rot"] = Rot;
			Def["id"] = vila::jget(P, "id");
			Def["hidden_item"] = vila::jstr(P, "hidden_item", "");
			AVilaFurniture* Fn = SpawnFurniture(Def, Pos, Rot, FString());
			UVilaMeshBatch* FB = FurnitureBatch(0.85f);
			Bx(MakeG(FB), V3(0, 0.015, 0), V3(1.0, 0.025, 0.6), GC(0.55, 0.4, 0.3), TEXT("fabric"));
			FlushFurniture(FB, Fn, Root);
			if (Fn) W.Furniture.Add(FS(vila::to_str(vila::jget(P, "id"))), Fn);
		}
		else if (Type == "pump")
		{
			Json Def = Json::object();
			Def["type"] = "pump";
			Def["rot"] = Rot;
			Def["id"] = vila::jget(P, "id");
			AVilaFurniture* Fn = SpawnFurniture(Def, Pos, Rot, FString());
			UVilaMeshBatch* FB = FurnitureBatch(0.85f);
			const FGBatch G = MakeG(FB);
			Bx(G, V3(0, 0.15, 0), V3(1.4, 0.3, 0.9), GC(0.7, 0.7, 0.68), TEXT("concrete"));
			Bx(G, V3(0, 1.0, 0), V3(0.7, 1.6, 0.5), GC(0.8, 0.3, 0.22), TEXT("metal"));
			Bx(G, V3(0, 1.82, 0), V3(0.74, 0.08, 0.54), GC(0.9, 0.9, 0.88), TEXT("metal"));
			Bx(G, V3(0, 1.35, -0.26), V3(0.45, 0.35, 0.02), GC(0.1, 0.16, 0.22), TEXT(""));
			Tb(G, V3(0.38, 1.25, 0), V3(0.45, 0.6, -0.1), 0.025, GC(0.1, 0.1, 0.1), TEXT("rubber"));
			Bx(G, V3(0.38, 1.0, 0), V3(0.08, 0.3, 0.1), Iron, TEXT("metal"));
			FlushFurniture(FB, Fn, Root);
			add_collider(Xf * V3(0, 0.9, 0), V3(0.8, 1.8, 0.6), Rot);
			if (Fn) W.Furniture.Add(FS(vila::to_str(vila::jget(P, "id"))), Fn);
		}
		else if (Type == "canopy")
		{
			const Json& S = vila::jget(P, "size");
			const double SX = Num(S, 0, 12.0), SZ = Num(S, 1, 8.0);
			Bx(Deco, V3(0, 4.6, 0), V3(SX, 0.4, SZ), GC(0.92, 0.92, 0.9), TEXT("metal"));
			Bx(Deco, V3(0, 4.6, 0), V3(SX + 0.1, 0.25, SZ + 0.1), GC(0.8, 0.3, 0.22), TEXT("metal"));
			for (const double X : {-1.0, 1.0})
				for (const double Z : {-1.0, 1.0})
				{
					Bx(Deco, V3(X * (SX * 0.5 - 0.6), 2.2, Z * (SZ * 0.5 - 0.6)), V3(0.3, 4.4, 0.3), GC(0.85, 0.85, 0.83), TEXT("metal"));
					add_collider(Pos + V3(X * (SX * 0.5 - 0.6), 2.2, Z * (SZ * 0.5 - 0.6)), V3(0.3, 4.4, 0.3));
				}
			LampHeads.add_box(At(Pos + V3(0, 4.38, 0)), V3(SX * 0.6, 0.06, 0.4), WHITE_C);
		}
		else if (Type == "fountain")
		{
			const FLinearColor Stone = GC(0.82, 0.8, 0.76);
			Deco.add_cylinder(Pos + V3(0, 0.3, 0), 2.6, 0.6, Stone, 32, TEXT("concrete"));
			Deco.add_cylinder(Pos + V3(0, 0.62, 0), 2.62, 0.06, Darkened(Stone, 0.1), 32, TEXT("concrete"));
			Deco.add_cylinder(Pos + V3(0, 0.56, 0), 2.35, 0.04, WHITE_C, 32, TEXT("water"));
			Deco.add_cylinder(Pos + V3(0, 1.1, 0), 0.35, 1.6, Stone, 16, TEXT("concrete"));
			Deco.add_cylinder(Pos + V3(0, 1.9, 0), 0.9, 0.15, Stone, 20, TEXT("concrete"));
			Deco.add_cylinder(Pos + V3(0, 1.97, 0), 0.75, 0.02, WHITE_C, 20, TEXT("water"));
			add_cyl_collider(Pos + V3(0, 0.4, 0), 2.6, 0.8);
		}
		else if (Type == "kiosk")
		{
			Bx(Deco, V3(0, 1.2, 0), V3(2.2, 2.4, 1.8), GC(0.45, 0.62, 0.52), TEXT("corrugated"));
			Bx(Deco, V3(0, 2.5, 0), V3(2.6, 0.15, 2.2), GC(0.9, 0.75, 0.38), TEXT("metal"));
			Bx(Deco, V3(0, 1.3, -0.91), V3(1.6, 0.9, 0.02), GC(0.96, 0.94, 0.88), TEXT(""));
			Bx(Deco, V3(0, 0.85, -1.0), V3(1.8, 0.05, 0.25), WOOD, TEXT("wood_out"));
			add_collider(Xf * V3(0, 1.2, 0), V3(2.2, 2.4, 1.8), Rot);
		}
		else if (Type == "mailbox")
		{
			AVilaFurniture* Fn = vila::has(P, "id") ? prop_furniture(P, Pos, Rot) : nullptr;
			UVilaMeshBatch* FB = Fn ? FurnitureBatch(0.85f) : nullptr;
			const FGBatch G = FB ? MakeG(FB) : Deco;
			Bx(G, V3(0, 0.55, 0), V3(0.07, 1.1, 0.07), Iron, TEXT("metal"));
			Bx(G, V3(0, 1.15, 0), V3(0.45, 0.3, 0.3), GC(0.72, 0.28, 0.22), TEXT("metal"));
			Bx(G, V3(0, 1.32, 0), V3(0.47, 0.04, 0.32), GC(0.6, 0.22, 0.18), TEXT("metal"));
			if (FB) FlushFurniture(FB, Fn, Root);
		}
		else if (Type == "sale_sign")
		{
			AVilaFurniture* Fn = prop_furniture(P, Pos, Rot);
			UVilaMeshBatch* FB = FurnitureBatch(0.85f);
			const FGBatch G = MakeG(FB);
			Bx(G, V3(-0.5, 0.7, 0), V3(0.07, 1.4, 0.07), WOOD, TEXT("wood_out"));
			Bx(G, V3(0.5, 0.7, 0), V3(0.07, 1.4, 0.07), WOOD, TEXT("wood_out"));
			Bx(G, V3(0, 1.25, 0), V3(1.3, 0.6, 0.05), GC(0.75, 0.25, 0.2), TEXT("metal"));
			FlushFurniture(FB, Fn, Root);
			// Label3D na frente da placa (font 48 x pixel 0,006 ~ 0,29 m)
			USceneComponent* LabelParent = Fn ? VisualRootOf(Fn) : Root;
			Label(LabelParent ? LabelParent : Root, VilaConv::Tr("VENDE-SE"), G2U(Xf * V3(0, 1.25, -0.04)), VilaConv::YawFromGodot(Rot), 26.f,
				FColor::White, EComponentMobility::Movable);
		}
		else if (Type == "sign")
		{
			Bx(Deco, V3(0, 1.0, 0), V3(0.08, 2.0, 0.08), Iron, TEXT("metal"));
			Bx(Deco, V3(0, 2.1, 0), V3(2.6, 0.6, 0.06), GC(0.2, 0.36, 0.28), TEXT("metal"));
			// o Label3D do Godot fica no lado +Z local da placa
			Label(Root, FS(vila::jstr(P, "text", "")), G2U(Xf * V3(0, 2.1, 0.04)), VilaConv::YawFromGodot(Rot) + 180.0, 26.f);
		}
		else if (Type == "hedge")
		{
			const FVector A = GPos2(vila::jget(P, "from"));
			const FVector E = GPos2(vila::jget(P, "to"));
			const double R2 = FMath::Abs(A.Z - E.Z) < 0.01 ? 0.0 : 90.0;
			const double Len = FVector::Distance(A, E);
			// canteiro de concreto com arbustos de folhas
			Deco.box((A + E) * 0.5 + V3(0, 0.15, 0), V3(Len + 0.2, 0.3, 0.9), GC(0.7, 0.68, 0.64), R2, TEXT("concrete"));
			Deco.box((A + E) * 0.5 + V3(0, 0.31, 0), V3(Len, 0.02, 0.7), GC(0.35, 0.26, 0.18), R2, TEXT("dirt"));
			vila::Rng HR(static_cast<uint64_t>(static_cast<int64>(A.X * 31.0 + A.Z * 17.0)));
			const int32 N = static_cast<int32>(Len / 0.9);
			for (int32 I = 0; I < N + 1; ++I)
			{
				const FVector C = FMath::Lerp(A, E, (I + 0.5) / static_cast<double>(N + 1)) + V3(0, 0.75, 0);
				TArray<TPair<FVector, FVector>> Cl;
				Cl.Emplace(C, V3(0.55, 0.5, 0.45) * HR.randf_range(0.9, 1.15));
				W.Foliage->AddCanopy(C - V3(0, 0.5, 0), Cl, Mix(GC(0.26, 0.36, 0.17), GC(0.33, 0.42, 0.2), HR.randf()), HR, 26, 0.55f);
			}
			add_collider((A + E) * 0.5 + V3(0, 0.55, 0), V3(Len, 1.1, 0.8), R2);
		}
	}

	// Árvore: tronco afunilado, galhos e copa de cartões de folhas (paleta de outono).
	void build_tree(const FVector& Pos, int32 Variant)
	{
		vila::Rng TR(static_cast<uint64_t>(static_cast<int64>(Pos.X * 73.0 + Pos.Z * 191.0)));
		const double Sc = TR.randf_range(1.2, 1.75);
		const double H = 2.6 * Sc;
		const FLinearColor Bark = Mix(GC(0.52, 0.45, 0.38), GC(0.85, 0.82, 0.76), Variant == 2 ? 0.6 : 0.0);
		const FString WoodOut = TEXT("wood_out");
		// tronco levemente inclinado, raízes e galhos até cada maço da copa
		const FVector Lean = V3(TR.randf_range(-0.25, 0.25), 0, TR.randf_range(-0.25, 0.25));
		const FVector Top = Pos + V3(0, H, 0) + Lean;
		Deco.add_tube(Pos + V3(0, -0.1, 0), Top, 0.17 * Sc, Bark, 9, WoodOut);
		for (int32 I = 0; I < 4; ++I)
		{
			const double RA = vila::TAU * I / 4.0 + TR.randf() * 0.6;
			Deco.add_tube(Pos + V3(0, 0.25, 0), Pos + V3(FMath::Cos(RA), -0.05, FMath::Sin(RA)) * 0.55 * Sc, 0.07 * Sc, Bark, 6, WoodOut);
		}
		TArray<TPair<FVector, FVector>> Clusters;
		Clusters.Emplace(Top + V3(0, 1.4, 0) * Sc, V3(1.55, 1.2, 1.55) * Sc);
		const int32 N = 5 + static_cast<int32>(TR.randi() % 3);
		for (int32 I = 0; I < N; ++I)
		{
			const double A = TR.randf() * 0.5 + I * vila::TAU / N;
			const FVector Off = V3(FMath::Cos(A), 0, FMath::Sin(A)) * TR.randf_range(1.0, 1.7) * Sc;
			const FVector CPos = Top + Off + V3(0, TR.randf_range(0.2, 1.6) * Sc, 0);
			Clusters.Emplace(CPos, V3(1.0, 0.8, 1.0) * Sc * TR.randf_range(0.8, 1.15));
			const FVector Mid = FMath::Lerp(Top, CPos, 0.55) + V3(0, 0.2, 0);
			Deco.add_tube(Top - V3(0, 0.3, 0), Mid, 0.08 * Sc, Bark, 6, WoodOut);
			Deco.add_tube(Mid, CPos, 0.045 * Sc, Bark, 5, WoodOut);
		}
		Deco.add_tube(Top - V3(0, 0.2, 0), Top + V3(0, 1.3, 0) * Sc, 0.09 * Sc, Bark, 6, WoodOut);
		// Maioria verde; ipês amarelos como destaque
		static const int32 Picks[7] = {0, 0, 0, 0, 0, 1, 2};
		const int32 Pick = Picks[TR.randi() % 7];
		const FLinearColor Tint = Mix(LEAF_TINTS[Pick], LEAF_TINTS[FMath::Min(Pick + 1, 2)], TR.randf() * 0.15);
		W.Foliage->AddCanopy(Top, Clusters, Tint, TR, 54);
		add_cyl_collider(Pos + V3(0, 1.0, 0), 0.25, 2.0);
	}

	AVilaFurniture* prop_furniture(const Json& P, const FVector& Pos, double Rot)
	{
		Json D = P;
		D["rot"] = Rot;
		AVilaFurniture* Fn = SpawnFurniture(D, Pos, Rot, FString());
		if (Fn) W.Furniture.Add(FS(vila::to_str(vila::jget(P, "id"))), Fn);
		return Fn;
	}
};

}  // namespace VilaWB

// ======================================================================== UVilaWorldBuilder
void UVilaWorldBuilder::Build(AVilaWorld* World, const vila::Json& InMap)
{
	VilaWorld = World;
	Map = InMap;
	UWorld* UW = World ? World->GetWorld() : nullptr;
	Assets = UVilaAssets::Get(World);
	if (!UW || !Assets)
	{
		UE_LOG(LogVila, Error, TEXT("WorldBuilder: mundo ou UVilaAssets ausente; nada foi construído."));
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	VilaWB::FVilaWorldBuild B(*this, Map);
	B.UW = UW;
	B.Assets = Assets;
	B.Run();

	// Grama (GrassField.gd) com a densidade das opções.
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Grass = UW->SpawnActor<AVilaGrass>(AVilaGrass::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (Grass) Grass->Build(this, vila::to_int(vila::settings().get_value("graphics", "grass_density"), 2));

	// Navegação: a geração dinâmica está no DefaultEngine.ini; pede a reconstrução agora.
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(UW))
		Nav->Build();

	UE_LOG(LogVila, Log, TEXT("Vila construída em %.0f ms: %d prédios, %d portas, %d móveis, %d pontos, %d postes, grama %d tufos."),
		(FPlatformTime::Seconds() - T0) * 1000.0, Buildings.Num(), Doors.Num(), Furniture.Num(), Spots.Num(), Streetlights.Num(), Grass ? Grass->Instances : 0);
}

float UVilaWorldBuilder::GroundHeight(float X, float Y) const
{
	for (const FBox2D& R : SidewalkRects)
		if (X >= R.Min.X && X <= R.Max.X && Y >= R.Min.Y && Y <= R.Max.Y) return static_cast<float>(SIDEWALK_H * 100.0);
	return 0.f;
}

void UVilaWorldBuilder::SetSignalLamp(int32 Index, bool bOn)
{
	if (!SignalMIDs.IsValidIndex(Index) || !SignalMIDs[Index] || !SignalBaseColors.IsValidIndex(Index)) return;
	UMaterialInstanceDynamic* MID = SignalMIDs[Index];
	const FLinearColor Base = SignalBaseColors[Index];
	// Godot: albedo = cor x 0,3 e emissão 3,0 quando acesa.
	MID->SetVectorParameterValue(TEXT("Color"), bOn ? Base : Base * 0.09f);
	MID->SetScalarParameterValue(TEXT("Intensity"), bOn ? 3.f : 0.f);
}

void UVilaWorldBuilder::SetSignalStates(const FString& StateEW, const FString& StateNS)
{
	for (int32 I = 0; I < SignalLamps.Num(); ++I)
	{
		const bool bEW = SignalAxis.IsValidIndex(I) && SignalAxis[I] == TEXT("x");
		const FString& State = bEW ? StateEW : StateNS;
		SetSignalLamp(I, SignalColor.IsValidIndex(I) && SignalColor[I] == State);
	}
}

void UVilaWorldBuilder::UpdateNight(float Night)
{
	if (!LampMID || FMath::Abs(Night - LastNight) < 0.01f) return;
	LastNight = Night;
	// Mat.lamps(): emissão (1.0, 0.85, 0.6) x noite x 4.
	const FLinearColor Day(0.89f, 0.85f, 0.69f);  // (0.95, 0.93, 0.85) sRGB
	const FLinearColor Warm(1.f, 0.69f, 0.31f);   // (1.0, 0.85, 0.6) sRGB
	if (LampMode == 1)
	{
		LampMID->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Day, Warm, FMath::Clamp(Night, 0.f, 1.f)));
		LampMID->SetScalarParameterValue(TEXT("Intensity"), Night * 4.f);
	}
	else
	{
		LampMID->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Day, Warm * 2.f, FMath::Clamp(Night, 0.f, 1.f)));
	}
}

void UVilaWorldBuilder::UpdateStreetlightShadows(const FVector& ViewPoint, int32 Count)
{
	if (StreetShadow.Num() != Streetlights.Num()) StreetShadow.Init(false, Streetlights.Num());
	TArray<TPair<double, int32>> Order;
	for (int32 I = 0; I < Streetlights.Num(); ++I)
		if (Streetlights[I] && Streetlights[I]->IsVisible())
			Order.Emplace(FVector::DistSquared2D(Streetlights[I]->GetComponentLocation(), ViewPoint), I);
	Order.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	TArray<bool> Want;
	Want.Init(false, Streetlights.Num());
	for (int32 K = 0; K < FMath::Min(Count, Order.Num()); ++K) Want[Order[K].Value] = true;
	for (int32 I = 0; I < Streetlights.Num(); ++I)
		if (Streetlights[I] && Want[I] != StreetShadow[I])
		{
			Streetlights[I]->SetCastShadows(Want[I]);
			StreetShadow[I] = Want[I];
		}
}

// ======================================================================== AVilaWorldStatic
AVilaWorldStatic::AVilaWorldStatic()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

void AVilaWorldStatic::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UVilaWorldBuilder* B = Builder.Get();
	if (!B) return;
	// Noite: o ambiente publica no MPC (UVilaAssets::SetGlobal "Night") e/ou chama SetNight dos prédios.
	float Night = 0.f;
	if (const UVilaAssets* A = UVilaAssets::Get(this)) Night = A->GetGlobal(TEXT("Night"));
	for (const TPair<FString, TObjectPtr<AVilaBuilding>>& Pair : B->Buildings)
		if (Pair.Value) Night = FMath::Max(Night, Pair.Value->NightAmount());
	B->UpdateNight(Night);
	Accum += DeltaSeconds;
	if (Accum >= 0.5f)
	{
		Accum = 0.f;
		if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
			B->UpdateStreetlightShadows(Cam->GetCameraLocation());
	}
}
