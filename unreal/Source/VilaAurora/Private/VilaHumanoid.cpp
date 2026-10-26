#include "VilaHumanoid.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "VilaAnimInstance.h"
#include "VilaAssets.h"
#include "VilaAurora.h"
#include "VilaConv.h"

// 1: crossfade pelo UVilaAnimInstance (proxy nativo). 0: PlayAnimation por pose, sem blend.
#ifndef VILA_ANIM_PROXY
#define VILA_ANIM_PROXY 1
#endif

using namespace VilaConv;

namespace
{
	// Velocidade (m/s) para a qual cada ciclo foi animado.
	float HumCycleSpeed(const FString& Pose)
	{
		if (Pose == TEXT("walk")) return 1.7f;
		if (Pose == TEXT("run")) return 4.4f;
		return 0.f;
	}

	// Bits de região do corpo escondidos por cada peça (ver Anatomy.region no gerador).
	int32 HumHideBits(const FString& Piece)
	{
		if (Piece == TEXT("Top_tee")) return (1 << 1) | (1 << 3);
		if (Piece == TEXT("Top_long") || Piece == TEXT("Top_jacket")) return (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5);
		if (Piece == TEXT("Bottom_pants")) return (1 << 2) | (1 << 7) | (1 << 8) | (1 << 9);
		if (Piece == TEXT("Bottom_shorts")) return (1 << 2) | (1 << 7);
		if (Piece == TEXT("Bottom_skirt")) return (1 << 2);
		if (Piece == TEXT("Shoes")) return (1 << 11);
		return 0;
	}

	// Color(r, g, b) do Godot (sRGB) -> linear.
	FLinearColor HumSrgb(float R, float G, float B)
	{
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(R, 0.f, 1.f) * 255.f)),
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(G, 0.f, 1.f) * 255.f)),
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(B, 0.f, 1.f) * 255.f))));
	}

	FLinearColor HumAppColor(const vila::Json& App, const char* Key, const char* Default)
	{
		const std::string V = vila::jstr(App, Key, Default);
		return Hex(V.empty() ? std::string(Default) : V);
	}

	// String.hash() do Godot (djb2), usado pelo hash() do GDScript para variar o rosto.
	uint32 HumGodotHash(const FString& Str)
	{
		uint32 H = 5381;
		for (const TCHAR C : Str) H = ((H << 5) + H) + static_cast<uint32>(C);
		return H;
	}

	// Nome do slot de material normalizado ("M_Skin" / "MI_skin" / "skin" -> "skin").
	FString HumSlotKey(FName Slot)
	{
		FString Key = Slot.ToString().ToLower();
		if (Key.StartsWith(TEXT("mi_"))) Key.RightChopInline(3);
		else if (Key.StartsWith(TEXT("m_"))) Key.RightChopInline(2);
		return Key;
	}

	struct FHumPieceOn
	{
		FString Name;
		bool bHasTint = false;
		FLinearColor Tint = FLinearColor::White;
	};
}

UVilaHumanoid::UVilaHumanoid()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UVilaHumanoid::InferSex(const vila::Json& App)
{
	if (vila::has(App, "sex")) return vila::to_str(vila::jget(App, "sex")) == "f" ? TEXT("f") : TEXT("m");
	const std::string Style = vila::jstr(App, "hair_style", "short");
	const bool bFem = Style == "long" || Style == "bob" || Style == "bun" || Style == "ponytail";
	return bFem && !vila::truthy(vila::jget(App, "beard")) ? TEXT("f") : TEXT("m");
}

void UVilaHumanoid::Clear()
{
	for (USkeletalMeshComponent* P : Pieces)
		if (P && P != Body) P->DestroyComponent();
	Pieces.Reset();
	if (Body) Body->DestroyComponent();
	Body = nullptr;
	Anim = nullptr;
	for (USceneComponent* C : FallbackParts)
		if (C) C->DestroyComponent();
	FallbackParts.Reset();
	FbRoot = FbPelvis = FbTorso = FbHead = nullptr;
	FbArmL = FbArmR = FbForeL = FbForeR = FbThighL = FbThighR = FbShinL = FbShinR = nullptr;
	if (ModelRoot) ModelRoot->DestroyComponent();
	ModelRoot = nullptr;
	bBuilt = false;
}

void UVilaHumanoid::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Com o ator inteiro sendo destruído, os componentes filhos vão junto.
	if (!bDestroyingHierarchy) Clear();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UVilaHumanoid::Build(const vila::Json& App)
{
	Clear();
	Appearance = App.is_object() ? App : vila::Json::object();
	Sex = InferSex(Appearance);
	HeightScale = static_cast<float>(vila::jnum(Appearance, "height", 1.0));
	bElderly = vila::truthy(vila::jget(Appearance, "elderly"));

	AActor* Owner = GetOwner();
	if (!Owner) return;
	ModelRoot = NewObject<USceneComponent>(Owner, NAME_None, RF_Transient);
	ModelRoot->SetupAttachment(this);
	ModelRoot->SetRelativeScale3D(FVector(HeightScale));
	ModelRoot->RegisterComponent();

	bFallback = !BuildSkeletal(Appearance);
	if (bFallback) BuildFallback(Appearance);
	bBuilt = true;
	bSnapPose = true;
	PlayCurrent(0.f, FMath::FRand() * 2.f);
}

// ------------------------------------------------------------------ peças importadas
bool UVilaHumanoid::BuildSkeletal(const vila::Json& App)
{
	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (!Assets || !Assets->CharacterPiece(Sex, TEXT("Body"))) return false;

	// _dress do Godot: escolhe as peças e as cores.
	const FLinearColor Skin = HumAppColor(App, "skin", "#d9a98a");
	const FLinearColor Hair = HumAppColor(App, "hair", "#2a1d14");
	const FLinearColor Shirt = HumAppColor(App, "shirt", "#4477aa");
	const FLinearColor Pants = HumAppColor(App, "pants", "#333344");
	const FLinearColor ShoesC = HumAppColor(App, "shoes", "#2a2420");
	const FString Top = vila::truthy(vila::jget(App, "jacket")) ? TEXT("Top_jacket") : (vila::jstr(App, "top") == "long" ? TEXT("Top_long") : TEXT("Top_tee"));
	const std::string BottomKey = vila::to_str(vila::jget(App, "bottom"));
	const FString Bottom = BottomKey == "shorts" ? TEXT("Bottom_shorts") : BottomKey == "skirt" ? TEXT("Bottom_skirt") : TEXT("Bottom_pants");
	const std::string Style = vila::jstr(App, "hair_style", "short");
	FString HairMesh = TEXT("Hair_short");
	if (Style == "bald") HairMesh = FString();
	else if (Style == "slick") HairMesh = TEXT("Hair_slick");
	else if (Style == "messy") HairMesh = TEXT("Hair_messy");
	else if (Style == "curly") HairMesh = TEXT("Hair_curly");
	else if (Style == "long") HairMesh = TEXT("Hair_long");
	else if (Style == "bob") HairMesh = TEXT("Hair_bob");
	else if (Style == "bun") HairMesh = TEXT("Hair_bun");
	else if (Style == "ponytail") HairMesh = TEXT("Hair_ponytail");

	TArray<FHumPieceOn> On;
	auto Add = [&On](const FString& Name, const FLinearColor* Tint)
	{
		FHumPieceOn P;
		P.Name = Name;
		if (Tint)
		{
			P.bHasTint = true;
			P.Tint = *Tint;
		}
		On.Add(P);
	};
	Add(TEXT("Body"), &Skin);
	Add(TEXT("Eyes"), nullptr);
	Add(TEXT("Lashes"), nullptr);
	Add(Top, &Shirt);
	Add(Bottom, &Pants);
	Add(TEXT("Shoes"), &ShoesC);
	if (vila::has(App, "cap"))
	{
		const FLinearColor C = HumAppColor(App, "cap", "#ffffff");
		Add(TEXT("Cap"), &C);
		if (HairMesh == TEXT("Hair_messy") || HairMesh == TEXT("Hair_curly")) HairMesh = TEXT("Hair_short");
	}
	if (vila::has(App, "beanie"))
	{
		const FLinearColor C = HumAppColor(App, "beanie", "#ffffff");
		Add(TEXT("Beanie"), &C);
		if (HairMesh == TEXT("Hair_short") || HairMesh == TEXT("Hair_slick") || HairMesh == TEXT("Hair_messy") || HairMesh == TEXT("Hair_curly")) HairMesh = FString();
	}
	if (!HairMesh.IsEmpty()) Add(HairMesh, &Hair);
	if (vila::truthy(vila::jget(App, "beard"))) Add(TEXT("Beard"), &Hair);
	if (vila::truthy(vila::jget(App, "mustache")))
	{
		const FLinearColor Gray = Hex("#6a6a6a");
		Add(TEXT("Mustache"), bElderly ? &Gray : &Hair);
	}
	if (vila::has(App, "backpack"))
	{
		const FLinearColor C = HumAppColor(App, "backpack", "#ffffff");
		Add(TEXT("Backpack"), &C);
	}
	if (vila::has(App, "apron"))
	{
		const FLinearColor C = HumAppColor(App, "apron", "#ffffff");
		Add(TEXT("Apron"), &C);
	}
	if (vila::truthy(vila::jget(App, "overalls"))) Add(TEXT("Overalls"), &Pants);

	int32 Hide = 0;
	for (const FHumPieceOn& P : On) Hide |= HumHideBits(P.Name);

	// Morphs (_apply_shape): peso, idade e variação estável do rosto.
	const double BuildAmt = vila::jnum(App, "build", 1.0);
	const FColor SkinSrgb = FColor::FromHex(S(vila::jstr(App, "skin", "#d9a98a")));
	const float SkinV = FMath::Max3(SkinSrgb.R, SkinSrgb.G, SkinSrgb.B) / 255.f;
	const uint32 H = HumGodotHash(S(vila::jstr(App, "skin")) + S(vila::jstr(App, "hair")) + S(vila::jstr(App, "shirt")));
	TArray<TPair<FName, float>> Morphs;
	Morphs.Emplace(FName(TEXT("heavy")), static_cast<float>(FMath::Clamp((BuildAmt - 1.0) / 0.2, 0.0, 1.0)));
	Morphs.Emplace(FName(TEXT("thin")), static_cast<float>(FMath::Clamp((1.0 - BuildAmt) / 0.15, 0.0, 1.0)));
	Morphs.Emplace(FName(TEXT("old")), bElderly ? 0.85f : 0.f);
	Morphs.Emplace(FName(TEXT("face_a")), FMath::Clamp((0.75f - SkinV) * 1.6f, 0.f, 0.8f) + static_cast<float>(H % 7) * 0.03f);
	Morphs.Emplace(FName(TEXT("face_b")), static_cast<float>(static_cast<int64>(static_cast<double>(H) / 7.0) % 5) * 0.12f);

	AActor* Owner = GetOwner();
	for (const FHumPieceOn& P : On)
	{
		USkeletalMesh* Mesh = Assets->CharacterPiece(Sex, P.Name);
		if (!Mesh) continue;
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient);
		Comp->SetupAttachment(ModelRoot);
		Comp->SetRelativeRotation(FRotator(0.f, MeshYawOffset, 0.f));
		Comp->SetSkeletalMeshAsset(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(true);
		Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Comp->RegisterComponent();

		// Cores por slot de material (_material do Godot).
		const TArray<FSkeletalMaterial>& Mats = Mesh->GetMaterials();
		for (int32 i = 0; i < Mats.Num(); ++i)
		{
			const FString Slot = HumSlotKey(Mats[i].MaterialSlotName);
			UMaterialInstanceDynamic* MID = Comp->CreateDynamicMaterialInstance(i);
			if (!MID) continue;
			FLinearColor C = P.bHasTint ? P.Tint : FLinearColor::White;
			if (Slot == TEXT("skin"))
			{
				C = Skin;
				MID->SetVectorParameterValue(TEXT("SkinTone"), Skin);
				MID->SetVectorParameterValue(TEXT("HairColor"), Hair);
				MID->SetScalarParameterValue(TEXT("HideBits"), static_cast<float>(Hide));
			}
			else if (Slot == TEXT("eye")) C = FLinearColor::White;
			else if (Slot == TEXT("lash")) C = HumSrgb(0.03f, 0.025f, 0.02f);
			else if (Slot == TEXT("metal")) C = HumSrgb(0.75f, 0.75f, 0.78f);
			else if (Slot == TEXT("sole")) C = HumSrgb(0.86f, 0.84f, 0.8f);
			else if (Slot == TEXT("strap")) C = HumSrgb(0.13f, 0.13f, 0.14f);
			MID->SetVectorParameterValue(TEXT("Color"), C);
		}
		for (const TPair<FName, float>& Morph : Morphs) Comp->SetMorphTarget(Morph.Key, Morph.Value);

		if (P.Name == TEXT("Body")) Body = Comp;
		else Pieces.Add(Comp);
	}
	if (!Body) return false;

	for (USkeletalMeshComponent* Piece : Pieces) Piece->SetLeaderPoseComponent(Body);
#if VILA_ANIM_PROXY
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UVilaAnimInstance::StaticClass());
	Anim = Cast<UVilaAnimInstance>(Body->GetAnimInstance());
	if (!Anim) UE_LOG(LogVila, Warning, TEXT("UVilaAnimInstance não foi criado para %s"), *GetNameSafe(Owner));
#else
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
#endif
	return true;
}

// ------------------------------------------------------------------ animação
float UVilaHumanoid::AnimRate() const
{
	float R = bElderly ? 0.8f : 1.f;
	const float Cycle = HumCycleSpeed(Pose);
	if (Cycle > 0.f) R = FMath::Clamp(FMath::Max(SpeedMs, 0.6f) / Cycle, 0.5f, 1.9f);
	return R;
}

void UVilaHumanoid::PlayCurrent(float Blend, float StartTime)
{
	if (bFallback || !Body) return;
	UVilaAssets* Assets = UVilaAssets::Get(this);
	UAnimSequence* Seq = Assets ? Assets->CharacterAnim(Sex, Pose) : nullptr;
	if (!Seq) return;  // has_animation falso: mantém a animação atual
	const float Rate = bLodFar ? 0.f : AnimRate();
#if VILA_ANIM_PROXY
	if (Anim)
	{
		Anim->PlayPose(Seq, Blend, StartTime);
		Anim->SetRate(Rate);
	}
#else
	Body->PlayAnimation(Seq, true);
	Body->SetPosition(StartTime, false);
	Body->SetPlayRate(Rate);
#endif
}

void UVilaHumanoid::SetPose(const FString& InPose)
{
	if (InPose == Pose) return;
	Pose = InPose;
	float Blend = (Pose == TEXT("walk") || Pose == TEXT("run")) ? 0.12f : 0.3f;
	const bool bInstant = Pose == TEXT("lie") || Pose == TEXT("knocked") || Pose == TEXT("sit");
	if (bInstant) Blend = 0.f;
	bSnapPose = bInstant;
	PlayCurrent(Blend, 0.f);
}

void UVilaHumanoid::SetMoveSpeed(float CmPerSec)
{
	SpeedMs = CmPerSec / 100.f;
	// Só troca entre poses de locomoção; poses de atividade ficam como estão.
	if (Pose == TEXT("idle") || Pose == TEXT("walk") || Pose == TEXT("run"))
		SetPose(SpeedMs < 0.2f ? TEXT("idle") : SpeedMs < 3.0f ? TEXT("walk") : TEXT("run"));
}

void UVilaHumanoid::SetLodFar(bool bFar)
{
	bLodFar = bFar;
	const float Rate = bFar ? 0.f : AnimRate();
#if VILA_ANIM_PROXY
	if (Anim) Anim->SetRate(Rate);
#else
	if (Body) Body->SetPlayRate(Rate);
#endif
}

void UVilaHumanoid::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBuilt || bLodFar || !IsVisible()) return;
	const AActor* Owner = GetOwner();
	if (Owner && Owner->IsHidden()) return;
	if (bFallback)
	{
		TickFallback(DeltaTime);
		return;
	}
	if (HumCycleSpeed(Pose) > 0.f)
	{
#if VILA_ANIM_PROXY
		if (Anim) Anim->SetRate(AnimRate());
#else
		if (Body) Body->SetPlayRate(AnimRate());
#endif
	}
}

// ------------------------------------------------------------------ boneco procedural
USceneComponent* UVilaHumanoid::AddPivot(USceneComponent* Parent, const FVector& Loc)
{
	USceneComponent* C = NewObject<USceneComponent>(GetOwner(), NAME_None, RF_Transient);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(Loc);
	C->RegisterComponent();
	FallbackParts.Add(C);
	return C;
}

UStaticMeshComponent* UVilaHumanoid::AddPart(USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Color, float Roughness)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(Mesh);
	C->SetRelativeLocation(Loc);
	C->SetRelativeScale3D(SizeCm / 100.f);  // formas básicas do motor: 100 cm, pivô no centro
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCastShadow(true);
	if (UVilaAssets* Assets = UVilaAssets::Get(this)) C->SetMaterial(0, Assets->Color(Color, Roughness));
	C->RegisterComponent();
	FallbackParts.Add(C);
	return C;
}

void UVilaHumanoid::BuildFallback(const vila::Json& App)
{
	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (!Assets || !ModelRoot) return;
	UStaticMesh* Cube = Assets->Cube();
	UStaticMesh* Cyl = Assets->Cylinder();
	UStaticMesh* Sph = Assets->Sphere();

	const FLinearColor Skin = HumAppColor(App, "skin", "#d9a98a");
	const FLinearColor Hair = HumAppColor(App, "hair", "#2a1d14");
	const FLinearColor Shirt = HumAppColor(App, "shirt", "#4477aa");
	const FLinearColor Pants = HumAppColor(App, "pants", "#333344");
	const FLinearColor ShoesC = HumAppColor(App, "shoes", "#2a2420");
	const bool bLongSleeve = vila::truthy(vila::jget(App, "jacket")) || vila::jstr(App, "top") == "long";
	const std::string Bottom = vila::to_str(vila::jget(App, "bottom"));
	const bool bBareShins = Bottom == "shorts" || Bottom == "skirt";
	const float Wide = FMath::Clamp(static_cast<float>(vila::jnum(App, "build", 1.0)), 0.8f, 1.3f);
	const bool bFem = Sex == TEXT("f");
	std::string Style = vila::jstr(App, "hair_style", "short");

	// Medidas em cm (escala 1,0 = 1,77 m), frente em +X, direita em +Y.
	FbRoot = AddPivot(ModelRoot, FVector::ZeroVector);
	FbPelvis = AddPivot(FbRoot, FVector(0, 0, 92));
	AddPart(FbPelvis, Cube, FVector(0, 0, 0), FVector(20, 32 * Wide, 16), Pants);
	if (Bottom == "skirt") AddPart(FbPelvis, Cyl, FVector(0, 0, -14), FVector(30 * Wide, 36 * Wide, 26), Pants);

	FbTorso = AddPivot(FbPelvis, FVector(0, 0, 4));
	AddPart(FbTorso, Cube, FVector(0, 0, 26), FVector(22 * Wide, (bFem ? 33.f : 36.f) * Wide, 46), Shirt);
	if (vila::truthy(vila::jget(App, "overalls"))) AddPart(FbTorso, Cube, FVector(11.5f * Wide, 0, 12), FVector(1.2f, 24, 26), Pants);
	if (vila::has(App, "apron")) AddPart(FbTorso, Cube, FVector(11.8f * Wide, 0, 6), FVector(1.5f, 30, 48), HumAppColor(App, "apron", "#ffffff"));
	if (vila::has(App, "backpack")) AddPart(FbTorso, Cube, FVector(-17.f * Wide, 0, 28), FVector(12, 28, 36), HumAppColor(App, "backpack", "#ffffff"));
	AddPart(FbTorso, Cyl, FVector(0, 0, 54), FVector(10, 10, 10), Skin, 0.6f);

	FbHead = AddPivot(FbTorso, FVector(0, 0, 58));
	AddPart(FbHead, Sph, FVector(1, 0, 11), FVector(21, 19, 24), Skin, 0.6f);
	AddPart(FbHead, Sph, FVector(10, -4, 13), FVector(2.5f, 2.5f, 2.5f), FLinearColor(0.02f, 0.02f, 0.02f), 0.3f);
	AddPart(FbHead, Sph, FVector(10, 4, 13), FVector(2.5f, 2.5f, 2.5f), FLinearColor(0.02f, 0.02f, 0.02f), 0.3f);
	const bool bCap = vila::has(App, "cap");
	const bool bBeanie = vila::has(App, "beanie");
	if (bBeanie && (Style == "short" || Style == "slick" || Style == "messy" || Style == "curly")) Style = "bald";
	if (Style != "bald")
	{
		const float Puff = Style == "curly" ? 1.12f : Style == "messy" ? 1.06f : 1.f;
		AddPart(FbHead, Sph, FVector(-1.5f, 0, 15), FVector(23, 21, 18) * Puff, Hair, 0.55f);
		if (Style == "long") AddPart(FbHead, Cube, FVector(-8, 0, 2), FVector(6, 20, 28), Hair, 0.55f);
		else if (Style == "bob") AddPart(FbHead, Cube, FVector(-5, 0, 7), FVector(10, 22, 14), Hair, 0.55f);
		else if (Style == "ponytail") AddPart(FbHead, Cyl, FVector(-13, 0, 6), FVector(5, 5, 18), Hair, 0.55f);
		else if (Style == "bun") AddPart(FbHead, Sph, FVector(-11, 0, 17), FVector(9, 9, 9), Hair, 0.55f);
	}
	if (bCap)
	{
		const FLinearColor C = HumAppColor(App, "cap", "#ffffff");
		AddPart(FbHead, Cyl, FVector(-1, 0, 21), FVector(22, 22, 7), C);
		AddPart(FbHead, Cube, FVector(11, 0, 18.5f), FVector(12, 18, 1.5f), C);
	}
	if (bBeanie) AddPart(FbHead, Sph, FVector(-1, 0, 18), FVector(23, 22, 17), HumAppColor(App, "beanie", "#ffffff"));
	if (vila::truthy(vila::jget(App, "beard"))) AddPart(FbHead, Cube, FVector(8, 0, 2), FVector(6, 15, 8), Hair, 0.7f);
	if (vila::truthy(vila::jget(App, "mustache"))) AddPart(FbHead, Cube, FVector(10.5f, 0, 7.5f), FVector(2, 7, 1.5f), bElderly ? Hex("#6a6a6a") : Hair, 0.7f);

	const float ShoulderY = (bFem ? 19.f : 21.f) * Wide;
	FbArmL = AddPivot(FbTorso, FVector(0, -ShoulderY, 46));
	FbArmR = AddPivot(FbTorso, FVector(0, ShoulderY, 46));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		USceneComponent* Arm = Side == 0 ? FbArmL : FbArmR;
		AddPart(Arm, Cyl, FVector(0, 0, -14), FVector(9, 9, 28), Shirt);
		USceneComponent* Fore = AddPivot(Arm, FVector(0, 0, -28));
		AddPart(Fore, Cyl, FVector(0, 0, -12), FVector(8, 8, 24), bLongSleeve ? Shirt : Skin, bLongSleeve ? 0.85f : 0.6f);
		AddPart(Fore, Sph, FVector(0, 0, -27), FVector(9, 8, 10), Skin, 0.6f);
		(Side == 0 ? FbForeL : FbForeR) = Fore;
	}

	FbThighL = AddPivot(FbPelvis, FVector(0, -9 * Wide, -6));
	FbThighR = AddPivot(FbPelvis, FVector(0, 9 * Wide, -6));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		USceneComponent* Thigh = Side == 0 ? FbThighL : FbThighR;
		AddPart(Thigh, Cyl, FVector(0, 0, -21), FVector(14 * Wide, 14 * Wide, 42), Bottom == "skirt" ? Skin : Pants, Bottom == "skirt" ? 0.6f : 0.85f);
		USceneComponent* Shin = AddPivot(Thigh, FVector(0, 0, -42));
		AddPart(Shin, Cyl, FVector(0, 0, -20), FVector(12, 12, 40), bBareShins ? Skin : Pants, bBareShins ? 0.6f : 0.85f);
		AddPart(Shin, Cube, FVector(4, 0, -42), FVector(24, 11, 8), ShoesC, 0.7f);
		(Side == 0 ? FbShinL : FbShinR) = Shin;
	}
}

void UVilaHumanoid::TickFallback(float Dt)
{
	if (!FbRoot || !FbPelvis || !FbTorso || !FbHead || !FbArmL || !FbArmR || !FbForeL || !FbForeR || !FbThighL || !FbThighR || !FbShinL || !FbShinR) return;
	const float Rate = bElderly ? 0.8f : 1.f;
	const float Two = 2.f * UE_PI;
	if (Pose == TEXT("walk")) Phase += Dt * Two * FMath::Max(SpeedMs, 0.6f) / 1.4f;
	else if (Pose == TEXT("run")) Phase += Dt * Two * FMath::Max(SpeedMs, 1.5f) / 2.4f;
	else Phase += Dt * Two * 0.3f * Rate;
	Phase = FMath::Fmod(Phase, 2000.f * UE_PI);
	const float P = Phase;
	const float Sn = FMath::Sin(P);
	const float Cs = FMath::Cos(P);

	FJoints Tg;
	if (Pose == TEXT("walk"))
	{
		Tg.ThighL = 25 * Sn; Tg.ThighR = -25 * Sn;
		Tg.ShinL = -40 * FMath::Max(0.f, Cs); Tg.ShinR = -40 * FMath::Max(0.f, -Cs);
		Tg.ArmL = -20 * Sn; Tg.ArmR = 20 * Sn;
		Tg.ForeL = Tg.ForeR = 15;
		Tg.PelvisZ = 92 + 1.5f * (FMath::Abs(Cs) - 0.5f);
	}
	else if (Pose == TEXT("run"))
	{
		Tg.ThighL = 45 * Sn; Tg.ThighR = -45 * Sn;
		Tg.ShinL = -80 * FMath::Max(0.f, Cs); Tg.ShinR = -80 * FMath::Max(0.f, -Cs);
		Tg.ArmL = -40 * Sn; Tg.ArmR = 40 * Sn;
		Tg.ForeL = Tg.ForeR = 80;
		Tg.Torso = -10;
		Tg.PelvisZ = 89 + 3 * FMath::Abs(Cs);
	}
	else if (Pose == TEXT("talk"))
	{
		Tg.ArmR = 20 + 8 * FMath::Sin(P * 7); Tg.ForeR = 55 + 15 * FMath::Sin(P * 5);
		Tg.ArmL = 4; Tg.ForeL = 12;
		Tg.Head = 4 * FMath::Sin(P * 4);
	}
	else if (Pose == TEXT("work"))
	{
		Tg.ArmL = Tg.ArmR = 45;
		Tg.ForeL = 40 + 15 * FMath::Sin(P * 9); Tg.ForeR = 40 + 15 * FMath::Sin(P * 9 + UE_PI);
		Tg.Torso = -8;
	}
	else if (Pose == TEXT("eat"))
	{
		Tg.ArmR = 30; Tg.ForeR = 115 + 8 * FMath::Sin(P * 4);
		Tg.ArmL = 8; Tg.ForeL = 35;
	}
	else if (Pose == TEXT("search"))
	{
		Tg.Torso = -35; Tg.Head = -10;
		Tg.ArmL = Tg.ArmR = 50;
		Tg.ForeL = 20 + 10 * FMath::Sin(P * 6); Tg.ForeR = 20 + 10 * FMath::Sin(P * 6 + 2);
	}
	else if (Pose == TEXT("sit"))
	{
		// Coxas a 0,47 m (mesma referência do Godot: visual.y = altura do assento - 0,47).
		Tg.PelvisZ = 58;
		Tg.ThighL = Tg.ThighR = 90; Tg.ShinL = Tg.ShinR = -90;
		Tg.ArmL = Tg.ArmR = 20; Tg.ForeL = Tg.ForeR = 45;
	}
	else if (Pose == TEXT("lie"))
	{
		// Deitado de costas, centrado no ator.
		Tg.RootPitch = 90; Tg.RootX = 88; Tg.RootZ = 11;
		Tg.ArmRoll = 10; Tg.ForeL = Tg.ForeR = 5;
	}
	else if (Pose == TEXT("knocked"))
	{
		// Caído de bruços.
		Tg.RootPitch = -90; Tg.RootX = -88; Tg.RootZ = 11;
		Tg.ArmRoll = 35; Tg.ArmL = Tg.ArmR = -10;
		Tg.ThighL = 15; Tg.ShinL = -35;
	}
	else  // idle e poses desconhecidas
	{
		Tg.ArmL = Tg.ArmR = 2 * Sn; Tg.ForeL = Tg.ForeR = 8;
		Tg.Torso = 1.5f * Sn;
	}
	if (bElderly && Tg.RootPitch == 0.f && Pose != TEXT("sit")) Tg.Torso -= 6;

	const float A = bSnapPose ? 1.f : 1.f - FMath::Exp(-Dt * 14.f);
	bSnapPose = false;
	auto L = [A](float& Cur, float Tgt) { Cur += (Tgt - Cur) * A; };
	L(Joints.ThighL, Tg.ThighL); L(Joints.ThighR, Tg.ThighR); L(Joints.ShinL, Tg.ShinL); L(Joints.ShinR, Tg.ShinR);
	L(Joints.ArmL, Tg.ArmL); L(Joints.ArmR, Tg.ArmR); L(Joints.ForeL, Tg.ForeL); L(Joints.ForeR, Tg.ForeR); L(Joints.ArmRoll, Tg.ArmRoll);
	L(Joints.Torso, Tg.Torso); L(Joints.Head, Tg.Head); L(Joints.PelvisZ, Tg.PelvisZ);
	L(Joints.RootPitch, Tg.RootPitch); L(Joints.RootX, Tg.RootX); L(Joints.RootZ, Tg.RootZ);

	// Pitch positivo num membro que aponta para baixo = balanço para a frente (+X).
	FbRoot->SetRelativeLocationAndRotation(FVector(Joints.RootX, 0, Joints.RootZ), FRotator(Joints.RootPitch, 0, 0));
	FbPelvis->SetRelativeLocation(FVector(0, 0, Joints.PelvisZ));
	FbTorso->SetRelativeRotation(FRotator(Joints.Torso, 0, 0));
	FbHead->SetRelativeRotation(FRotator(Joints.Head, 0, 0));
	FbArmL->SetRelativeRotation(FRotator(Joints.ArmL, 0, -Joints.ArmRoll));
	FbArmR->SetRelativeRotation(FRotator(Joints.ArmR, 0, Joints.ArmRoll));
	FbForeL->SetRelativeRotation(FRotator(Joints.ForeL, 0, 0));
	FbForeR->SetRelativeRotation(FRotator(Joints.ForeR, 0, 0));
	FbThighL->SetRelativeRotation(FRotator(Joints.ThighL, 0, 0));
	FbThighR->SetRelativeRotation(FRotator(Joints.ThighR, 0, 0));
	FbShinL->SetRelativeRotation(FRotator(Joints.ShinL, 0, 0));
	FbShinR->SetRelativeRotation(FRotator(Joints.ShinR, 0, 0));
}
