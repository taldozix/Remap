#include "VilaDoor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "VilaAssets.h"
#include "VilaAudio.h"
#include "VilaConv.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaWorld.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"

using namespace VilaConv;

namespace
{
	constexpr float SwingDeg = 95.f;
	constexpr float RollupLiftCm = 320.f;

	// Espaço local do Godot (x ao longo da parede, y para cima, z espessura) -> espaço local
	// da porta no Unreal (X espessura/normal, Y ao longo da parede, Z para cima). Em cm.
	FVector G(float X, float Y, float Z) { return FVector(Z, X, Y); }
}

AVilaDoor::AVilaDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetMobility(EComponentMobility::Movable);
	Pivot->SetupAttachment(Root);
	// Corpo da folha: bloqueia só personagens (camada 2 do Godot), não a visão nem a câmera,
	// e não corta a navmesh (NPCs passam pela porta).
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Pivot);
	Collision->SetMobility(EComponentMobility::Movable);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetCanEverAffectNavigation(false);
}

UStaticMeshComponent* AVilaDoor::AddPart(UStaticMesh* Mesh, const FVector& CenterCm, const FVector& SizeCm, UMaterialInterface* Material)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetupAttachment(Pivot);
	C->SetRelativeLocation(CenterCm);
	C->SetRelativeScale3D(SizeCm / 100.f);
	if (Material) C->SetMaterial(0, Material);
	C->RegisterComponent();
	AddInstanceComponent(C);
	return C;
}

void AVilaDoor::Setup(const vila::Json& InDef, AVilaWorld* World, const FString& InBuildingId, float WallThicknessCm, const FLinearColor& Color)
{
	Def = InDef;
	DoorId = S(vila::jstr(InDef, "id"));
	VilaWorld = World;
	BuildingId = InBuildingId;
	WidthCm = static_cast<float>(vila::jnum(InDef, "width", 1.1) * M);
	bRollup = vila::truthy(vila::jget(InDef, "rollup"));
	Title = Tr(vila::jstr(InDef, "label", "Porta"));
	HeightCm = bRollup ? 340.f : 215.f;
	const float W = WidthCm, H = HeightCm, T = WallThicknessCm;

	Pivot->SetRelativeLocation(FVector(0.0, -W * 0.5, 0.0));
	Collision->SetRelativeLocation(G(W * 0.5f, H * 0.5f, 0.f));
	Collision->SetBoxExtent(G(W, H, T * 0.6f) * 0.5f);

	UVilaAssets* A = UVilaAssets::Get(this);
	if (!A) return;
	UStaticMesh* Cube = A->Cube();
	UMaterialInterface* LeafMat = nullptr;
	if (vila::truthy(vila::jget(InDef, "glass")))
	{
		LeafMat = A->Glass(VilaW::SRGB(0.55f, 0.68f, 0.76f), 0.45f);
		// caixilho de alumínio: montantes, travessas e puxador
		UMaterialInterface* Alu = A->SurfaceLocal(TEXT("metal_brushed"), VilaW::SRGB(0.75f, 0.76f, 0.78f), 1.f);
		const struct { FVector Pos; FVector Sz; } Frames[] = {
			{G(W * 0.5f, H - 4.f, 0.f), G(W, 8.f, T * 0.7f)},
			{G(W * 0.5f, 5.f, 0.f), G(W, 10.f, T * 0.7f)},
			{G(4.f, H * 0.5f, 0.f), G(8.f, H, T * 0.7f)},
			{G(W - 4.f, H * 0.5f, 0.f), G(8.f, H, T * 0.7f)},
			{G(W * 0.5f, 100.f, 0.f), G(W, 6.f, T * 0.7f)},
			{G(W - 16.f, 105.f, 0.f), G(3.f, 50.f, T * 1.2f)},
		};
		for (const auto& F : Frames) AddPart(Cube, F.Pos, F.Sz, Alu);
	}
	else if (bRollup)
	{
		LeafMat = A->SurfaceLocal(TEXT("corrugated"), VilaW::SRGB(0.78f, 0.79f, 0.8f), 1.f);
		AddPart(Cube, G(W * 0.5f, 5.f, 0.f), G(W, 8.f, T * 0.7f), A->SurfaceLocal(TEXT("metal_painted"), VilaW::SRGB(0.3f, 0.31f, 0.33f), 1.f));
	}
	else
	{
		// porta de madeira com almofadas (relevo) dos dois lados
		LeafMat = A->SurfaceLocal(TEXT("wood"), VilaW::LerpSRGB(Color, VilaW::SRGB(0.55f, 0.38f, 0.24f), 0.35f), 1.f);
		UMaterialInterface* PanelMat = A->SurfaceLocal(TEXT("wood"), VilaW::LerpSRGB(Color, VilaW::SRGB(0.4f, 0.27f, 0.17f), 0.45f), 1.f);
		for (const float Side : {-1.f, 1.f})
			for (const float PY : {55.f, 150.f})
				AddPart(Cube, G(W * 0.5f, PY, Side * T * 0.3f), G(W - 30.f, PY > 100.f ? 70.f : 75.f, 2.f), PanelMat);
		UMaterialInterface* Brass = A->Color(VilaW::SRGB(0.8f, 0.7f, 0.42f), 0.3f, 0.9f);
		AddPart(A->Sphere(), G(W - 12.f, 100.f, T * 0.38f), FVector(7.0), Brass);
		AddPart(A->Sphere(), G(W - 12.f, 100.f, -T * 0.38f), FVector(7.0), Brass);
	}
	AddPart(Cube, G(W * 0.5f, H * 0.5f, 0.f), G(W, H, T * 0.6f), LeafMat);

	bSetup = true;
	// _ready do Godot: estado salvo, sem animação.
	const vila::Json& St = vila::jobj(vila::game().world_state.doors, U(DoorId));
	SetOpen(vila::jbool(St, "open", false), true);
}

bool AVilaDoor::IsLocked() const
{
	return vila::game().world_state.is_door_locked(U(DoorId));
}

// Lojas: destrancadas no horário de funcionamento.
bool AVilaDoor::WithinHours() const
{
	const vila::Json& H = vila::jarr(Def, "hours");
	return H.size() == 2 && vila::clock().hour_in(vila::to_num(H[0]), vila::to_num(H[1]));
}

void AVilaDoor::ApplyPivot()
{
	if (bRollup)
		Pivot->SetRelativeLocation(FVector(0.0, -WidthCm * 0.5, Current));
	else
		Pivot->SetRelativeRotation(FRotator(0.0, Current, 0.0));
}

void AVilaDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSetup) return;
	if (bRollup)
		Current = FMath::FInterpConstantTo(Current, Target, DeltaSeconds, 300.f);
	else
		Current = FMath::Lerp(Current, Target, FMath::Min(1.f, DeltaSeconds * 9.f));
	ApplyPivot();

	CheckT -= DeltaSeconds;
	if (CheckT > 0.f) return;
	CheckT = 0.25f;
	// Horário: lojas abrem e fecham as portas sozinhas (trancam ao fechar).
	if (vila::has(Def, "hours"))
	{
		const bool bShouldUnlock = WithinHours();
		if (bShouldUnlock == IsLocked())
		{
			vila::game().world_state.set_door(U(DoorId), bIsOpen, !bShouldUnlock);
			if (bRollup) SetOpen(bShouldUnlock);
		}
	}
	if (bRollup) return;
	// NPCs com acesso abrem a porta ao chegar perto.
	bool bNpcNear = false;
	if (VilaWorld)
		for (AVilaNPC* N : VilaWorld->NPCsNear(GetActorLocation(), 180.f))
			if (N->HasDoorAccess(this))
			{
				bNpcNear = true;
				break;
			}
	if (bNpcNear)
	{
		NpcHold = 1.6f;
		if (!bIsOpen) SetOpen(true);
	}
	else if (NpcHold > 0.f)
	{
		NpcHold -= 0.25f;
		if (NpcHold <= 0.f && bIsOpen && !bPlayerOpened) SetOpen(false);
	}
}

void AVilaDoor::SetOpen(bool bOpen, bool bInstant)
{
	if (bOpen != bIsOpen && !bInstant)
		if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->PlayAt(bOpen ? TEXT("door_open") : TEXT("door_close"), GetActorLocation(), -4.f);
	bIsOpen = bOpen;
	Target = bRollup ? (bOpen ? RollupLiftCm : 0.f) : (bOpen ? SwingDeg * SwingSign : 0.f);
	if (bInstant)
	{
		Current = Target;
		ApplyPivot();
	}
	Collision->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	const bool bLocked = vila::jbool(vila::jobj(vila::game().world_state.doors, U(DoorId)), "locked", false);
	vila::game().world_state.set_door(U(DoorId), bOpen, bLocked);
}

// Quem tem a chave: dono(s) listados, funcionário (player_job) ou item-chave no inventário.
bool AVilaDoor::PlayerHasKey() const
{
	if (PlayerHasKeyByRight()) return true;
	const vila::Json Id(U(DoorId));
	for (const vila::Json& St : vila::game().player.inventory.items)
		if (vila::json_array_has(vila::jarr(vila::data().item(vila::jstr(St, "id")), "opens"), Id)) return true;
	return false;
}

bool AVilaDoor::PlayerHasKeyByRight() const
{
	const vila::Json& Keys = vila::jarr(Def, "keys");
	vila::Game& Gm = vila::game();
	return vila::json_array_has(Keys, vila::Json("player")) || (vila::json_array_has(Keys, vila::Json("owner")) && Gm.property.owns_or_rents(U(BuildingId))) ||
		(vila::json_array_has(Keys, vila::Json("player_job")) && Gm.job.employed);
}

bool AVilaDoor::NpcHasKey(const FString& NpcId) const
{
	const vila::Json& Keys = vila::jarr(Def, "keys");
	return vila::json_array_has(Keys, vila::Json(U(NpcId))) || (vila::json_array_has(Keys, vila::Json("pedestre")) && NpcId.StartsWith(TEXT("pedestre")));
}

TArray<FVilaAction> AVilaDoor::GetActions(AVilaPlayerCharacter* Player)
{
	TArray<FVilaAction> Out;
	const bool bInside = VilaWorld && Player ? VilaWorld->IsInsideBuilding(VilaW::Feet(Player), BuildingId) : false;
	if (bRollup)
	{
		if (PlayerHasKey()) Out.Add(FVilaAction(TEXT("toggle"), bIsOpen ? Tr("Fechar portão") : Tr("Abrir portão")));
		return Out;
	}
	if (bIsOpen)
	{
		Out.Add(FVilaAction(TEXT("close"), Tr("Fechar")));
	}
	else if (IsLocked() && !bInside)
	{
		Out.Add(FVilaAction(TEXT("knock"), Tr("Bater na porta")));
		if (PlayerHasKey())
			Out.Add(FVilaAction(TEXT("unlock"), Tr("Destrancar e abrir")));
		else
			Out.Add(FVilaAction(TEXT("locked"), Tr("Trancada"), false, Tr("Você não tem a chave")));
	}
	else
	{
		Out.Add(FVilaAction(TEXT("open"), Tr("Abrir")));
	}
	if (PlayerHasKey() && !vila::has(Def, "hours"))
	{
		if (IsLocked())
		{
			if (bIsOpen || bInside) Out.Add(FVilaAction(TEXT("unlock_only"), Tr("Destrancar")));
		}
		else
		{
			Out.Add(FVilaAction(TEXT("lock"), Tr("Trancar")));
		}
	}
	return Out;
}

void AVilaDoor::DoAction(AVilaPlayerCharacter* Player, const FString& Id)
{
	UVilaAudio* Audio = UVilaAudio::Get(this);
	const FVector PF = VilaW::Feet(Player);
	if (Id == TEXT("open") || Id == TEXT("toggle"))
	{
		if (bRollup)
		{
			SetOpen(!bIsOpen);
			return;
		}
		if (IsLocked() && !(VilaWorld && VilaWorld->IsInsideBuilding(PF, BuildingId)))
		{
			if (Audio) Audio->PlayAt(TEXT("door_locked"), GetActorLocation());
			return;
		}
		bPlayerOpened = true;
		SetSwingFrom(PF);
		SetOpen(true);
	}
	else if (Id == TEXT("close"))
	{
		bPlayerOpened = false;
		SetOpen(false);
	}
	else if (Id == TEXT("unlock"))
	{
		vila::game().world_state.set_door(U(DoorId), bIsOpen, false);
		bPlayerOpened = true;
		SetSwingFrom(PF);
		SetOpen(true);
		if (!PlayerHasKeyByRight() && !BuildingId.IsEmpty() && VilaWorld) VilaWorld->OnPlayerUsedKey(BuildingId);
	}
	else if (Id == TEXT("unlock_only"))
	{
		vila::game().world_state.set_door(U(DoorId), bIsOpen, false);
		vila::events().notify.emit(vila::tr("Porta destrancada."), "info");
	}
	else if (Id == TEXT("lock"))
	{
		if (bIsOpen) SetOpen(false);
		vila::game().world_state.set_door(U(DoorId), false, true);
		vila::events().notify.emit(vila::tr("Porta trancada."), "info");
	}
	else if (Id == TEXT("knock"))
	{
		if (Audio) Audio->PlayAt(TEXT("door_locked"), GetActorLocation(), 2.f, 0.6f);
		if (VilaWorld) VilaWorld->OnKnock(BuildingId);
	}
}

void AVilaDoor::SetSwingFrom(const FVector& P)
{
	// Yaw positivo leva a folha (+Y) para -X (para dentro): abre para longe de quem está em +X.
	const FVector Local = GetActorTransform().InverseTransformPosition(P);
	SwingSign = Local.X > 0.0 ? 1.f : -1.f;
}

void AVilaDoor::SetFocused(bool bFocused)
{
	VilaW::SetActorOutline(this, bFocused);
}
