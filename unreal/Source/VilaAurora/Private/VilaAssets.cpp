#include "VilaAssets.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "VilaAurora.h"
#include "VilaConv.h"

namespace
{
	const TCHAR* MatDir = TEXT("/Game/Vila/Materials/");

	struct FSurfaceDef
	{
		const TCHAR* Set;
		float Tile;  // <= 0: usa o tile_m do conjunto
		float Macro;
		bool bDetile;
		bool bSwapTop;
		float Outdoor;
		float Rough;
		FLinearColor Tint;
	};

	// Mat.SURFACES do Godot.
	const TMap<FString, FSurfaceDef>& SurfaceDefs()
	{
		static const TMap<FString, FSurfaceDef> Defs = {
			{TEXT("asphalt"), {TEXT("asphalt"), -1, 0.18f, true, false, 1, 0, FLinearColor::White}},
			{TEXT("sidewalk"), {TEXT("sidewalk"), -1, 0.12f, true, false, 1, 0, FLinearColor(0.8f, 0.79f, 0.77f)}},
			{TEXT("plaza"), {TEXT("pedra_portuguesa"), -1, 0.08f, false, false, 1, 0, FLinearColor(0.88f, 0.87f, 0.85f)}},
			{TEXT("concrete"), {TEXT("concrete"), -1, 0.15f, true, false, 1, 0, FLinearColor::White}},
			{TEXT("curb"), {TEXT("concrete"), 1.0f, 0.1f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("plaster"), {TEXT("plaster"), -1, 0.1f, true, false, 1, 0, FLinearColor::White}},
			{TEXT("plaster_in"), {TEXT("plaster"), -1, 0.05f, false, false, 0, 0, FLinearColor::White}},
			{TEXT("brick"), {TEXT("brick"), -1, 0.12f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("siding"), {TEXT("wood_siding"), -1, 0.08f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("roof_tiles"), {TEXT("roof_tiles"), -1, 0.15f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("roof_tiles_z"), {TEXT("roof_tiles"), -1, 0.15f, false, true, 1, 0, FLinearColor::White}},
			{TEXT("roof_flat"), {TEXT("roof_flat"), -1, 0.2f, true, false, 1, 0, FLinearColor::White}},
			{TEXT("roof_fiber"), {TEXT("roof_fiber"), -1, 0.12f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("roof_fiber_z"), {TEXT("roof_fiber"), -1, 0.12f, false, true, 1, 0, FLinearColor::White}},
			{TEXT("wood_floor"), {TEXT("wood_floor"), -1, 0.1f, false, false, 0, 0, FLinearColor::White}},
			{TEXT("porcelain"), {TEXT("porcelain"), -1, 0.1f, false, false, 0, 0, FLinearColor::White}},
			{TEXT("wall_tiles"), {TEXT("wall_tiles"), -1, 0.1f, false, false, 0, 0, FLinearColor::White}},
			{TEXT("wood"), {TEXT("wood"), 0.8f, 0.1f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("wood_out"), {TEXT("wood"), 0.8f, 0.1f, false, false, 1, 0.15f, FLinearColor::White}},
			{TEXT("fabric"), {TEXT("fabric"), 0.35f, 0.1f, false, false, 0, 0, FLinearColor::White}},
			{TEXT("metal"), {TEXT("metal_painted"), 0.8f, 0.1f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("steel"), {TEXT("metal_brushed"), 0.6f, 0.1f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("corrugated"), {TEXT("corrugated"), 1.0f, 0.1f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("canvas"), {TEXT("cloth_canvas"), 0.4f, 0.1f, false, false, 1, 0, FLinearColor::White}},
			{TEXT("dirt"), {TEXT("dirt"), -1, 0.1f, true, false, 1, 0, FLinearColor::White}},
			{TEXT("rubber"), {TEXT("rubber"), 0.3f, 0.1f, false, false, 1, 0, FLinearColor::White}},
		};
		return Defs;
	}

	// Cor média de cada albedo (sRGB) para o modo sem conteúdo importado.
	FLinearColor AverageAlbedo(const FString& Set)
	{
		static const TMap<FString, FColor> Avg = {
			{"asphalt", FColor(50, 50, 52)}, {"brick", FColor(142, 91, 71)}, {"cloth_canvas", FColor(223, 221, 217)},
			{"cloth_denim", FColor(192, 194, 198)}, {"cloth_knit", FColor(226, 226, 226)}, {"concrete", FColor(149, 147, 143)},
			{"corrugated", FColor(151, 149, 152)}, {"dirt", FColor(107, 77, 53)}, {"fabric", FColor(226, 226, 221)},
			{"ground", FColor(73, 64, 39)}, {"hair", FColor(176, 176, 176)}, {"leather", FColor(210, 203, 197)},
			{"metal_brushed", FColor(174, 174, 177)}, {"metal_painted", FColor(225, 225, 225)},
			{"pedra_portuguesa", FColor(115, 111, 104)}, {"plaster", FColor(213, 211, 208)}, {"porcelain", FColor(215, 206, 191)},
			{"roof_fiber", FColor(139, 138, 132)}, {"roof_flat", FColor(87, 85, 84)}, {"roof_tiles", FColor(142, 73, 41)},
			{"rubber", FColor(227, 227, 227)}, {"sidewalk", FColor(154, 151, 145)}, {"wall_tiles", FColor(222, 224, 222)},
			{"wood", FColor(165, 120, 77)}, {"wood_floor", FColor(147, 101, 62)}, {"wood_siding", FColor(219, 216, 207)},
		};
		const FColor* C = Avg.Find(Set);
		return C ? FLinearColor::FromSRGBColor(*C) : FLinearColor(0.5f, 0.5f, 0.5f);
	}

	FString ColorKey(const FLinearColor& C) { return FString::Printf(TEXT("%.3f,%.3f,%.3f,%.3f"), C.R, C.G, C.B, C.A); }
}

UVilaAssets* UVilaAssets::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UVilaAssets>() : nullptr;
}

void UVilaAssets::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	BasicMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Vila/Materials/MPC_Vila.MPC_Vila"));
	bHasContent = Master(TEXT("M_Surface")) != nullptr;
	if (!bHasContent)
		UE_LOG(LogVila, Warning, TEXT("Conteúdo da Vila não encontrado em /Game/Vila. Rode Scripts/build_content.py no editor; usando cores sólidas."));
}

template <typename T>
T* UVilaAssets::LoadCached(const FString& Path)
{
	if (TObjectPtr<UObject>* Found = Loaded.Find(Path)) return Cast<T>(Found->Get());
	// O pacote pode não existir (conteúdo não gerado): checa antes para não poluir o log.
	FString PackageName = Path;
	int32 Dot;
	if (PackageName.FindChar(TEXT('.'), Dot)) PackageName.LeftInline(Dot);
	T* Obj = FPackageName::DoesPackageExist(PackageName) ? LoadObject<T>(nullptr, *Path) : nullptr;
	Loaded.Add(Path, Obj);
	return Obj;
}

UMaterialInterface* UVilaAssets::Master(const FString& Name)
{
	return LoadCached<UMaterialInterface>(FString(MatDir) + Name + TEXT(".") + Name);
}

UMaterialInterface* UVilaAssets::Fallback(const FLinearColor& C)
{
	const FString Key = TEXT("fallback|") + ColorKey(C);
	if (TObjectPtr<UMaterialInterface>* M = Materials.Find(Key)) return *M;
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BasicMaterial, this);
	MID->SetVectorParameterValue(TEXT("Color"), C);
	Materials.Add(Key, MID);
	return MID;
}

UMaterialInterface* UVilaAssets::Surface(const FString& Key, const FLinearColor& Tint, bool bInstanceTint)
{
	const FString CacheKey = FString::Printf(TEXT("surf|%s|%s|%d"), *Key, *ColorKey(Tint), bInstanceTint ? 1 : 0);
	if (TObjectPtr<UMaterialInterface>* M = Materials.Find(CacheKey)) return *M;

	const FSurfaceDef* Def = SurfaceDefs().Find(Key);
	const FSurfaceDef Direct = {*Key, -1, 0.1f, false, false, 1, 0, FLinearColor::White};
	if (!Def) Def = &Direct;
	const FString Set = Def->Set;
	const FLinearColor FinalTint = Def->Tint * Tint;

	UMaterialInterface* Parent = Master(TEXT("M_Surface"));
	UTexture2D* Albedo = Texture(FString::Printf(TEXT("/Game/Vila/Textures/T_%s_A"), *Set));
	if (!Parent || !Albedo)
	{
		UMaterialInterface* F = Fallback(AverageAlbedo(Set) * FinalTint);
		Materials.Add(CacheKey, F);
		return F;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetTextureParameterValue(TEXT("Albedo"), Albedo);
	if (UTexture2D* N = Texture(FString::Printf(TEXT("/Game/Vila/Textures/T_%s_N"), *Set))) MID->SetTextureParameterValue(TEXT("Normal"), N);
	if (UTexture2D* O = Texture(FString::Printf(TEXT("/Game/Vila/Textures/T_%s_ORM"), *Set))) MID->SetTextureParameterValue(TEXT("ORM"), O);
	if (Def->Tile > 0.f) MID->SetScalarParameterValue(TEXT("TileMeters"), Def->Tile);  // senão o MI_<set> já traz o tile_m
	MID->SetVectorParameterValue(TEXT("Tint"), FinalTint);
	MID->SetScalarParameterValue(TEXT("Macro"), Def->Macro);
	MID->SetScalarParameterValue(TEXT("Detile"), Def->bDetile ? 1.f : 0.f);
	MID->SetScalarParameterValue(TEXT("SwapTop"), Def->bSwapTop ? 1.f : 0.f);
	MID->SetScalarParameterValue(TEXT("Outdoor"), Def->Outdoor);
	MID->SetScalarParameterValue(TEXT("RoughBias"), Def->Rough);
	MID->SetScalarParameterValue(TEXT("UseCustomTint"), bInstanceTint ? 1.f : 0.f);
	// O tile_m de cada conjunto vem do MI_<set> criado pelo build_content.py; aqui só
	// sobrescrevemos quando a definição pede um tile próprio.
	if (Def->Tile <= 0.f)
		if (UMaterialInterface* SetMI = LoadCached<UMaterialInterface>(FString::Printf(TEXT("/Game/Vila/Materials/Surfaces/MI_%s.MI_%s"), *Set, *Set)))
		{
			float Tile = 1.f;
			if (SetMI->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("TileMeters")), Tile)) MID->SetScalarParameterValue(TEXT("TileMeters"), Tile);
		}
	Materials.Add(CacheKey, MID);
	return MID;
}

UMaterialInterface* UVilaAssets::SurfaceLocal(const FString& Set, const FLinearColor& Tint, float Scale)
{
	const FString CacheKey = FString::Printf(TEXT("local|%s|%s|%.2f"), *Set, *ColorKey(Tint), Scale);
	if (TObjectPtr<UMaterialInterface>* M = Materials.Find(CacheKey)) return *M;
	UMaterialInterface* Parent = Master(TEXT("M_SurfaceLocal"));
	UTexture2D* Albedo = Texture(FString::Printf(TEXT("/Game/Vila/Textures/T_%s_A"), *Set));
	if (!Parent || !Albedo)
	{
		UMaterialInterface* F = Fallback(AverageAlbedo(Set) * Tint);
		Materials.Add(CacheKey, F);
		return F;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetTextureParameterValue(TEXT("Albedo"), Albedo);
	if (UTexture2D* N = Texture(FString::Printf(TEXT("/Game/Vila/Textures/T_%s_N"), *Set))) MID->SetTextureParameterValue(TEXT("Normal"), N);
	if (UTexture2D* O = Texture(FString::Printf(TEXT("/Game/Vila/Textures/T_%s_ORM"), *Set))) MID->SetTextureParameterValue(TEXT("ORM"), O);
	MID->SetVectorParameterValue(TEXT("Tint"), Tint);
	MID->SetScalarParameterValue(TEXT("Scale"), Scale);
	Materials.Add(CacheKey, MID);
	return MID;
}

UMaterialInterface* UVilaAssets::Color(const FLinearColor& C, float Roughness, float Metallic, bool bInstanceTint)
{
	const FString CacheKey = FString::Printf(TEXT("color|%s|%.2f|%.2f|%d"), *ColorKey(C), Roughness, Metallic, bInstanceTint ? 1 : 0);
	if (TObjectPtr<UMaterialInterface>* M = Materials.Find(CacheKey)) return *M;
	UMaterialInterface* Parent = Master(TEXT("M_Color"));
	if (!Parent) return Fallback(C);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetVectorParameterValue(TEXT("Color"), C);
	MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	MID->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	MID->SetScalarParameterValue(TEXT("UseCustomTint"), bInstanceTint ? 1.f : 0.f);
	Materials.Add(CacheKey, MID);
	return MID;
}

UMaterialInterface* UVilaAssets::Emissive(const FLinearColor& C, float Intensity)
{
	const FString CacheKey = FString::Printf(TEXT("emis|%s|%.2f"), *ColorKey(C), Intensity);
	if (TObjectPtr<UMaterialInterface>* M = Materials.Find(CacheKey)) return *M;
	UMaterialInterface* Parent = Master(TEXT("M_Emissive"));
	if (!Parent) return Fallback(C);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetVectorParameterValue(TEXT("Color"), C);
	MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	Materials.Add(CacheKey, MID);
	return MID;
}

UMaterialInterface* UVilaAssets::Glass(const FLinearColor& Tint, float Opacity)
{
	const FString CacheKey = FString::Printf(TEXT("glass|%s|%.2f"), *ColorKey(Tint), Opacity);
	if (TObjectPtr<UMaterialInterface>* M = Materials.Find(CacheKey)) return *M;
	UMaterialInterface* Parent = Master(TEXT("M_Glass"));
	if (!Parent) return Fallback(Tint * 0.4f);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetVectorParameterValue(TEXT("Tint"), Tint);
	MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	Materials.Add(CacheKey, MID);
	return MID;
}

UMaterialInstanceDynamic* UVilaAssets::MakeMID(UMaterialInterface* Parent, UObject* Outer) const
{
	return UMaterialInstanceDynamic::Create(Parent ? Parent : BasicMaterial.Get(), Outer);
}

UMaterialInstanceDynamic* UVilaAssets::MakeColorMID(const FString& MasterName, const FLinearColor& C, UObject* Outer)
{
	UMaterialInterface* Parent = Master(MasterName);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent ? Parent : BasicMaterial.Get(), Outer);
	MID->SetVectorParameterValue(TEXT("Color"), C);
	return MID;
}

void UVilaAssets::SetGlobal(const UObject* WorldContext, FName Param, float Value)
{
	Globals.Add(Param, Value);
	if (Collection && WorldContext) UKismetMaterialLibrary::SetScalarParameterValue(WorldContext, Collection, Param, Value);
}

float UVilaAssets::GetGlobal(FName Param) const
{
	const float* V = Globals.Find(Param);
	return V ? *V : 0.f;
}

UStaticMesh* UVilaAssets::VehiclePart(const FString& Model, const FString& Part)
{
	return LoadCached<UStaticMesh>(FString::Printf(TEXT("/Game/Vila/Vehicles/%s/SM_%s.SM_%s"), *Model, *Part, *Part));
}

const vila::Json& UVilaAssets::VehicleLayout()
{
	if (!bLayoutLoaded)
	{
		bLayoutLoaded = true;
		std::string Text;
		Layout = vila::Json::object();
		if (vila::read_text_file(VilaConv::U(DataDir() + TEXT("art/vehicles_layout.json")), Text))
		{
			try
			{
				Layout = vila::Json::parse(Text);
			}
			catch (const std::exception& E)
			{
				UE_LOG(LogVila, Warning, TEXT("vehicles_layout.json inválido: %s"), UTF8_TO_TCHAR(E.what()));
			}
		}
	}
	return Layout;
}

USkeletalMesh* UVilaAssets::CharacterPiece(const FString& Sex, const FString& Piece)
{
	return LoadCached<USkeletalMesh>(FString::Printf(TEXT("/Game/Vila/Characters/%s/SK_%s.SK_%s"), *Sex, *Piece, *Piece));
}

UAnimSequence* UVilaAssets::CharacterAnim(const FString& Sex, const FString& Anim)
{
	return LoadCached<UAnimSequence>(FString::Printf(TEXT("/Game/Vila/Characters/%s/Anims/A_%s.A_%s"), *Sex, *Anim, *Anim));
}

USkeleton* UVilaAssets::CharacterSkeleton(const FString& Sex)
{
	return LoadCached<USkeleton>(FString::Printf(TEXT("/Game/Vila/Characters/%s/SKEL_Human.SKEL_Human"), *Sex));
}

UTexture2D* UVilaAssets::Texture(const FString& Path)
{
	FString Full = Path;
	if (!Full.Contains(TEXT(".")))
	{
		FString Name;
		Path.Split(TEXT("/"), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		Full = Path + TEXT(".") + Name;
	}
	return LoadCached<UTexture2D>(Full);
}

FString UVilaAssets::DataDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data")) + TEXT("/");
}
