#include "VilaNPC.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaConv.h"
#include "VilaDoor.h"
#include "VilaFurniture.h"
#include "VilaHumanoid.h"
#include "VilaPlayerCharacter.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Settings.h"

using namespace VilaConv;

namespace
{
	constexpr float NpcRadius = 28.f;       // 0,28 m
	constexpr float NpcHalfHeight = 85.f;   // altura 1,7 m

	// e.get("k", "") do GDScript para valores que podem ser números ou textos.
	std::string Str(const vila::Json& J, const char* Key)
	{
		const vila::Json& V = vila::jget(J, Key);
		return V.is_null() ? std::string() : vila::to_str(V);
	}

	float LerpYaw(float From, float To, float Alpha)
	{
		return FRotator::NormalizeAxis(From + FMath::FindDeltaAngleDegrees(From, To) * Alpha);
	}

	bool IsRaining()
	{
		return vila::jstr(vila::game().world_state.weather, "state") == "chuva";
	}

	// tr("... %s ...") % [x]
	FString Trf(const char* Key, const std::string& Arg)
	{
		return S(vila::fmt(vila::tr(Key), {Arg}));
	}
}

AVilaNPC::AVilaNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	AutoPossessAI = EAutoPossessAI::Spawned;
	AIControllerClass = AAIController::StaticClass();

	GetCapsuleComponent()->InitCapsuleSize(NpcRadius, NpcHalfHeight);

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->bOrientRotationToMovement = false;
	MoveComp->bUseControllerDesiredRotation = false;
	MoveComp->bRunPhysicsWithNoController = true;
	// velocity = dir * spd do Godot: troca de velocidade instantânea.
	MoveComp->MaxAcceleration = 20000.f;
	MoveComp->BrakingDecelerationWalking = 20000.f;
	MoveComp->GroundFriction = 8.f;
	MoveComp->MaxWalkSpeed = 130.f;
	MoveComp->MaxStepHeight = 35.f;
	// Desvio local entre pedestres (o Godot deixava uns atravessarem os outros).
	MoveComp->bUseRVOAvoidance = true;
	MoveComp->AvoidanceConsiderationRadius = 150.f;

	Humanoid = CreateDefaultSubobject<UVilaHumanoid>(TEXT("Humanoid"));
	Humanoid->SetupAttachment(GetCapsuleComponent());
	Humanoid->SetRelativeLocation(FVector(0.f, 0.f, -NpcHalfHeight));
}

void AVilaNPC::Setup(const FString& InNpcId, const vila::Json& InDef, AVilaWorld* InWorld)
{
	NpcId = InNpcId;
	Def = InDef;
	VilaWorld = InWorld;
	WalkSpeed = static_cast<float>(vila::jnum(Def, "walk_speed", 1.3));
	bIsAmbient = NpcId.StartsWith(TEXT("pedestre"));
	Humanoid->Build(vila::jobj(Def, "appearance"));
	Humanoid->SetPose(TEXT("idle"));
	bSetupDone = true;
	// GameWorld._spawn_npc chamava n._decide() logo após criar: posiciona pela agenda.
	if (HasActorBegunPlay()) Decide();
}

void AVilaNPC::BeginPlay()
{
	Super::BeginPlay();
	Tags.AddUnique(FName(TEXT("npc")));
	TWeakObjectPtr<AVilaNPC> Weak(this);
	ClockConn = vila::clock().time_advanced.connect([Weak](double, bool bSkipped)
	{
		if (bSkipped)
			if (AVilaNPC* Self = Weak.Get()) Self->bSnapPending = true;
	});
	DecideT = FMath::FRand() * 0.5f;
	if (bSetupDone) Decide();
}

void AVilaNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	vila::clock().time_advanced.disconnect(ClockConn);
	if (UWorld* W = GetWorld()) W->GetTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

FString AVilaNPC::DisplayName() const
{
	return Tr(vila::jstr(Def, "name", U(NpcId)));
}

FVector AVilaNPC::FeetLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

void AVilaNPC::SetFeetLocation(const FVector& Feet)
{
	SetActorLocation(Feet + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);
}

void AVilaNPC::SetVisualOffset(float ZCm)
{
	Humanoid->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + ZCm));
}

// _shape.disabled do Godot. Sem colisão, o movimento também para (senão a gravidade derruba).
void AVilaNPC::SetBodyEnabled(bool bEnabled)
{
	GetCapsuleComponent()->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->StopMovementImmediately();
	if (bEnabled) MoveComp->SetMovementMode(MOVE_Walking);
	else MoveComp->DisableMovement();
}

void AVilaNPC::FaceYaw(float Yaw, float Alpha)
{
	SetActorRotation(FRotator(0.f, LerpYaw(GetActorRotation().Yaw, Yaw, Alpha), 0.f));
}

// ------------------------------------------------------------------ agenda
vila::Json AVilaNPC::ScheduleEntry() const
{
	const double H = vila::clock().hour_f();
	for (const vila::Json& E : vila::jarr(Def, "schedule"))
		if (vila::Clock::is_hour_in(H, vila::jnum(E, "from"), vila::jnum(E, "to"))) return E;
	return Entry;
}

FString AVilaNPC::HomeSpot() const
{
	for (const vila::Json& E : vila::jarr(Def, "schedule"))
		if (Str(E, "activity").rfind("sleep", 0) == 0) return S(Str(E, "place"));
	return FString();
}

vila::Json AVilaNPC::EffectiveEntry() const
{
	vila::Json E = ScheduleEntry();
	if (IsRaining() && NpcId != TEXT("tiao"))
	{
		const std::string Act = Str(E, "activity");
		const bool bOutdoorAct = Act == "sit" || Act == "stand" || Act == "chat" || Act == "jog" || Act == "wander";
		const AVilaWorld* W = GetVilaWorld();
		const FVilaSpot* Sp = W ? W->Spot(S(Str(E, "place"))) : nullptr;
		const FString Home = HomeSpot();
		if (bOutdoorAct && Sp && Sp->BuildingId.IsEmpty() && !Home.IsEmpty())
		{
			vila::Json Out = vila::Json::object();
			Out["place"] = U(Home);
			Out["activity"] = "shelter";
			Out["from"] = vila::has(E, "from") ? E["from"] : vila::Json(0);
			Out["to"] = vila::has(E, "to") ? E["to"] : vila::Json(0);
			return Out;
		}
	}
	return E;
}

void AVilaNPC::Decide()
{
	if (State == TEXT("talking") || State == TEXT("knocked")) return;
	const vila::Json E = EffectiveEntry();
	if (!E.is_object() || E.empty()) return;
	const bool bChanged = Str(E, "place") != Str(Entry, "place") || Str(E, "activity") != Str(Entry, "activity");
	if (bSnapPending)
	{
		bSnapPending = false;
		Entry = E;
		Activity = S(Str(E, "activity"));
		SnapTo(PlaceFor(E));
		return;
	}
	if (bChanged)
	{
		if (Str(Entry, "activity") == "shelter" && !IsRaining())
		{
		}
		else if (Str(E, "activity") == "shelter")
		{
			Bark(Pick("rain"));
		}
		Entry = E;
		Activity = S(Str(E, "activity"));
		Sub.Reset();
		ActivityT = 0.f;
		GoTo(PlaceFor(E));
	}
}

FString AVilaNPC::PlaceFor(const vila::Json& E) const
{
	const std::string Act = Str(E, "activity");
	if (Act == "jog") return TEXT("__jog");
	if (Act == "wander")
	{
		const vila::Json& Ws = vila::jarr(vila::data().map(), "wander_spots");
		return Ws.empty() ? FString() : S(vila::to_str(Ws[static_cast<size_t>(FMath::RandRange(0, static_cast<int32>(Ws.size()) - 1))]));
	}
	return S(Str(E, "place"));
}

bool AVilaNPC::SpotData(const FString& Id, FVilaSpot& Out) const
{
	if (Id == TEXT("__jog"))
	{
		const vila::Json& R = vila::jarr(vila::data().map(), "jog_route");
		Out = FVilaSpot();
		Out.Id = Id;
		Out.Pose = TEXT("stand");
		Out.Yaw = static_cast<float>(YawFromGodot(0.0));
		if (!R.empty())
		{
			const vila::Json& P = R[static_cast<size_t>(JogI % static_cast<int32>(R.size()))];
			if (P.is_array() && P.size() >= 2) Out.Location = Map2D(vila::to_num(P[0]), vila::to_num(P[1]));
		}
		return true;
	}
	const AVilaWorld* W = GetVilaWorld();
	const FVilaSpot* Sp = W ? W->Spot(Id) : nullptr;
	if (!Sp) return false;
	Out = *Sp;
	// Pedestres passeando se espalham em volta do ponto em vez de empilhar no mesmo lugar.
	if (Activity == TEXT("wander") && Out.BuildingId.IsEmpty() && !Out.bHidden)
	{
		const float A = FMath::FRand() * 2.f * PI;
		Out.Location += FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * FMath::FRandRange(120.f, 260.f);
		Out.Yaw = FMath::FRandRange(-180.f, 180.f);
	}
	return true;
}

void AVilaNPC::SnapTo(const FString& Id)
{
	SpotId = Id;
	bHasSpot = SpotData(Id, Spot);
	if (!bHasSpot) return;
	if (State == TEXT("hidden")) SetNpcHidden(false);
	SetFeetLocation(Spot.Location);
	Path.Reset();
	Arrive();
}

void AVilaNPC::GoTo(const FString& InSpotId)
{
	SpotId = InSpotId;
	bHasSpot = SpotData(InSpotId, Spot);
	if (!bHasSpot) return;
	if (State == TEXT("hidden")) SetNpcHidden(false);
	SetVisualOffset(0.f);
	SetBodyEnabled(true);
	const AVilaWorld* W = GetVilaWorld();
	if (W) Path = W->FindPath(FeetLocation(), Spot.Location);
	else Path = {Spot.Location};
	PathI = 0;
	State = TEXT("moving");
	StuckT = 0.f;
	StuckPos = FeetLocation();
}

void AVilaNPC::Arrive()
{
	State = TEXT("at_spot");
	SetActorRotation(FRotator(0.f, Spot.Yaw, 0.f));
	SetVisualOffset(0.f);
	SetBodyEnabled(true);
	if (SpotId == TEXT("__jog"))
	{
		++JogI;
		GoTo(TEXT("__jog"));
		return;
	}
	const bool bHideAct = Activity == TEXT("sleep") || Activity == TEXT("work") || Activity == TEXT("leave") || Activity == TEXT("shelter") || Activity == TEXT("stand") || Activity == TEXT("wander");
	if (Spot.bHidden && bHideAct)
	{
		if (Activity == TEXT("leave"))
		{
			Sub = TEXT("waiting_bus");
			ActivityT = 0.f;
		}
		else
		{
			SetNpcHidden(true);
			return;
		}
	}
	ApplyPose();
}

void AVilaNPC::ApplyPose()
{
	const FString Pose = Spot.Pose.IsEmpty() ? FString(TEXT("stand")) : Spot.Pose;
	if (Activity == TEXT("sleep") || Activity == TEXT("sleep_bench"))
	{
		if (Pose == TEXT("lie") || Activity == TEXT("sleep_bench"))
		{
			LieAtSpot();
			return;
		}
		if (Spot.bHidden)
		{
			SetNpcHidden(true);
			return;
		}
		Humanoid->SetPose(TEXT("idle"));
	}
	else if (Activity == TEXT("sit") || Activity == TEXT("shelter"))
	{
		if (Pose == TEXT("sit")) SitAtSpot();
		else Humanoid->SetPose(TEXT("idle"));
	}
	else if (Activity == TEXT("work"))
	{
		Humanoid->SetPose(Pose == TEXT("work") ? TEXT("work") : TEXT("idle"));
	}
	else if (Activity == TEXT("search"))
	{
		Humanoid->SetPose(TEXT("search"));
	}
	else if (Activity == TEXT("eat"))
	{
		Humanoid->SetPose(TEXT("eat"));
	}
	else
	{
		if (Pose == TEXT("sit")) SitAtSpot();
		else if (Pose == TEXT("lie")) LieAtSpot();
		else Humanoid->SetPose(TEXT("idle"));
	}
}

void AVilaNPC::SitAtSpot()
{
	SetBodyEnabled(false);
	Humanoid->SetPose(TEXT("sit"));
	const AVilaFurniture* Fn = Spot.Furniture.Get();
	SetVisualOffset((Fn ? Fn->SeatHeightCm : 45.f) - 47.f);
}

void AVilaNPC::LieAtSpot()
{
	SetBodyEnabled(false);
	Humanoid->SetPose(TEXT("lie"));
	const AVilaFurniture* Fn = Spot.Furniture.Get();
	if (Fn && Fn->Kind == TEXT("bed"))
	{
		const FVector Fwd = Fn->Forward().GetSafeNormal2D();
		SetFeetLocation(Fn->GetActorLocation() - Fwd * 85.f);
		SetActorRotation(FRotator(0.f, Fwd.Rotation().Yaw, 0.f));
		SetVisualOffset(62.f);
	}
	else
	{
		// Banco da praça: deita ao longo do assento (Godot: rot + 90°, anti-horário = yaw UE - 90).
		const float Yaw = FRotator::NormalizeAxis(Spot.Yaw - 90.f);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		SetFeetLocation(Spot.Location - FRotator(0.f, Yaw, 0.f).Vector() * 85.f);
		SetVisualOffset(60.f);
	}
}

void AVilaNPC::SetNpcHidden(bool bHide)
{
	SetActorHiddenInGame(bHide);
	SetBodyEnabled(!bHide);
	if (bHide) State = TEXT("hidden");
	else if (State == TEXT("hidden")) State = TEXT("idle");
}

bool AVilaNPC::IsSleeping() const
{
	return State == TEXT("hidden") || (State == TEXT("at_spot") && Activity.StartsWith(TEXT("sleep")));
}

// ------------------------------------------------------------------ processamento
void AVilaNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSetupDone) return;
	BarkCd -= DeltaSeconds;
	ReactionCd -= DeltaSeconds;
	if (BarkT > 0.f)
	{
		BarkT -= DeltaSeconds;
		if (BarkT <= 0.f) BarkText.Reset();
	}
	DecideT -= DeltaSeconds;
	if (DecideT <= 0.f)
	{
		DecideT = 0.5f;
		Decide();
		UpdateLod();
		if (State == TEXT("at_spot")) ProcessActivity(0.5f);
		if ((State == TEXT("moving") || State == TEXT("at_spot")) && !bLodFar) ReactToWorld();
	}
	if (State == TEXT("moving"))
	{
		StepMove(DeltaSeconds);
	}
	else if (State == TEXT("knocked"))
	{
		KnockT -= DeltaSeconds;
		if (KnockT <= 0.f) GetUp();
	}
	else if (State == TEXT("talking"))
	{
		const AVilaWorld* W = GetVilaWorld();
		if (W && W->Player)
		{
			const FVector To = W->Player->FeetLocation() - FeetLocation();
			if (!To.IsNearlyZero()) FaceYaw(To.Rotation().Yaw, FMath::Min(1.f, DeltaSeconds * 6.f));
		}
	}
}

void AVilaNPC::UpdateLod()
{
	const AVilaWorld* W = GetVilaWorld();
	if (!W || !W->Player) return;
	const bool bFar = FVector::Dist(FeetLocation(), W->Player->FeetLocation()) > W->NPCLodDistance();
	if (bFar == bLodFar) return;
	bLodFar = bFar;
	Humanoid->SetLodFar(bFar);
	// Longe: tick reduzido e sem física (deslocamento direto em StepMove).
	SetActorTickInterval(bFar ? 0.1f : 0.f);
	GetCharacterMovement()->SetComponentTickEnabled(!bFar);
}

void AVilaNPC::StepMove(float Dt)
{
	if (PathI >= Path.Num())
	{
		Arrive();
		return;
	}
	const FVector Target = Path[PathI];
	const FVector Feet = FeetLocation();
	FVector To = Target - Feet;
	To.Z = 0.0;
	if (To.Size() < 35.0)
	{
		++PathI;
		if (PathI >= Path.Num()) Arrive();
		return;
	}
	float Spd = WalkSpeed * (SpotId == TEXT("__jog") ? 2.f : 1.f);
	if (IsRaining()) Spd *= 1.25f;
	const vila::Json& St = vila::jobj(vila::game().world_state.npcs, U(NpcId));
	if (vila::jnum(St, "injured_until", 0.0) > vila::clock().total_minutes) Spd *= 0.6f;
	const FVector Dir = To.GetSafeNormal();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();

	// Espera o jogador sair da frente.
	const AVilaWorld* W = GetVilaWorld();
	const AVilaPlayerCharacter* Pl = W ? W->Player.Get() : nullptr;
	if (Pl && Pl->State != TEXT("driving"))
	{
		FVector Tp = Pl->FeetLocation() - Feet;
		Tp.Z = 0.0;
		if (Tp.Size() < 90.0 && FVector::DotProduct(Tp.GetSafeNormal(), Dir) > 0.6)
		{
			WaitPlayerT += Dt;
			MoveComp->StopMovementImmediately();
			Humanoid->SetPose(TEXT("idle"));
			if (WaitPlayerT > 1.2f && BarkCd <= 0.f) Bark(Tr("Com licença..."));
			if (WaitPlayerT < 2.5f) return;
		}
	}
	WaitPlayerT = 0.f;
	if (bLodFar)
	{
		// Simulação simplificada fora da vista: deslocamento direto e mais rápido,
		// para que a agenda continue pontual sem custo de física.
		SetActorLocation(GetActorLocation() + Dir * Spd * 100.f * 3.f * Dt, false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		MoveComp->MaxWalkSpeed = Spd * 100.f;
		AddMovementInput(Dir, 1.f);
	}
	FaceYaw(Dir.Rotation().Yaw, FMath::Min(1.f, Dt * 8.f));
	Humanoid->SetPose(SpotId == TEXT("__jog") ? TEXT("run") : TEXT("walk"));
	Humanoid->SetSpeed(Spd);
	StuckT += Dt;
	if (StuckT > 3.f)
	{
		const FVector Now = FeetLocation();
		if (FVector::Dist(Now, StuckPos) < 50.0)
		{
			SetFeetLocation(FVector(Target.X, Target.Y, Now.Z));
			++PathI;
		}
		StuckT = 0.f;
		StuckPos = FeetLocation();
	}
}

void AVilaNPC::ProcessActivity(float Dt)
{
	ActivityT += Dt;
	AVilaWorld* W = GetVilaWorld();
	if (!W) return;
	if (Activity == TEXT("shop"))
	{
		const FString ShopId = S(Str(Entry, "shop"));
		if (Sub.IsEmpty() && ActivityT > FMath::FRandRange(4.f, 9.f))
		{
			const FString Queue = (W->Spot(TEXT("mercadinho_fila")) && ShopId == TEXT("mercadinho")) ? FString(TEXT("mercadinho_fila")) : SpotId;
			if (W->ShopUnavailableReason(ShopId).IsEmpty())
			{
				Sub = TEXT("queue");
				GoTo(Queue);
			}
			else
			{
				Sub = TEXT("done");
			}
		}
		else if (Sub == TEXT("queue") && ActivityT > 3.f)
		{
			const std::string Bought = vila::game().shops.npc_purchase(U(ShopId), vila::game().rng);
			if (!Bought.empty())
			{
				const FString Name = S(vila::data().item_name(Bought)).ToLower();
				Bark(Tr("Vou levar %s.").Replace(TEXT("%s"), *Name));
				if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->PlayAt(TEXT("cash"), FeetLocation(), -10.f);
			}
			Sub = TEXT("done");
			GoTo(vila::has(Entry, "place") ? S(Str(Entry, "place")) : SpotId);
		}
	}
	else if (Activity == TEXT("leave"))
	{
		if (Sub == TEXT("waiting_bus") && ActivityT > 20.f) SetNpcHidden(true);
	}
	else if (Activity == TEXT("chat"))
	{
		ChatT -= Dt;
		if (ChatT <= 0.f)
		{
			ChatT = FMath::FRandRange(9.f, 15.f);
			AVilaNPC* Partner = W->ChatPartner(this);
			if (Partner && U(NpcId) < U(Partner->NpcId))
			{
				const vila::Json Lines = W->ConversationLines();
				if (Lines.is_array() && Lines.size() >= 2)
				{
					Bark(S(vila::to_str(Lines[0])));
					Partner->BarkLater(S(vila::to_str(Lines[1])), 2.8f);
				}
			}
			Humanoid->SetPose(Partner ? TEXT("talk") : TEXT("idle"));
		}
	}
	else if (Activity == TEXT("wander"))
	{
		if (ActivityT > FMath::FRandRange(25.f, 60.f))
		{
			ActivityT = 0.f;
			GoTo(PlaceFor(Entry));
		}
	}
}

void AVilaNPC::BarkLater(const FString& Text, float Delay)
{
	UWorld* W = GetWorld();
	if (!W) return;
	FTimerHandle Handle;
	W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Text]() { Bark(Text); }), FMath::Max(Delay, 0.01f), false);
}

// Reações: carro do jogador em alta velocidade, cheiro, chuva.
void AVilaNPC::ReactToWorld()
{
	const AVilaWorld* W = GetVilaWorld();
	const AVilaPlayerCharacter* P = W ? W->Player.Get() : nullptr;
	if (!P || State == TEXT("hidden")) return;
	const FVector PF = P->FeetLocation();
	const float D = static_cast<float>(FVector::Dist(FeetLocation(), PF));
	const AVilaVehicle* V = P->Vehicle.Get();
	if (P->State == TEXT("driving") && V && D < 600.f && FMath::Abs(V->Speed) > 600.f && ReactionCd <= 0.f)
	{
		ReactionCd = 6.f;
		Bark(Pick("car_near"));
		if (State == TEXT("moving") || !IsSleeping())
		{
			FVector Away = FeetLocation() - PF;
			Away.Z = 0.0;
			const FVector F = V->GetActorForwardVector();
			FVector Side(-F.Y, F.X, 0.0);
			if (FVector::DotProduct(Side, Away) < 0.0) Side = -Side;
			SetActorLocation(GetActorLocation() + Side.GetSafeNormal() * 120.f, true);
		}
	}
	else if (P->State != TEXT("driving") && D < 220.f && vila::game().player.needs.get_need("higiene") >= 70.0 && ReactionCd <= 0.f && !IsSleeping())
	{
		ReactionCd = 45.f;
		Bark(Pick("smell"));
		vila::Relations& R = vila::game().relations;
		const std::string Key = "smell_" + U(NpcId);
		if (vila::to_int(vila::jget(R.greeted_day, Key), -1) != vila::clock().day())
		{
			R.greeted_day[Key] = vila::clock().day();
			R.change(U(NpcId), -1);
		}
	}
}

// ------------------------------------------------------------------ interação
FVector AVilaNPC::GetInteractPoint() const
{
	return FeetLocation();
}

// Pessoas têm prioridade sobre móveis no foco; feridos, prioridade máxima.
float AVilaNPC::GetFocusPriority() const
{
	return State == TEXT("knocked") ? 1.2f : 0.35f;
}

FString AVilaNPC::GetInteractTitle() const
{
	const std::string Role = vila::jstr(Def, "role");
	return Role.empty() ? DisplayName() : FString::Printf(TEXT("%s — %s"), *DisplayName(), *Tr(Role));
}

FString AVilaNPC::BuildingAtSpot() const
{
	return bHasSpot ? Spot.BuildingId : FString();
}

void AVilaNPC::SetFocused(bool bFocused)
{
	// Contorno de foco: Custom Depth com stencil 1 (lido pelo pós-processo de contorno).
	TArray<UPrimitiveComponent*> Prims;
	GetComponents<UPrimitiveComponent>(Prims, true);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim == GetCapsuleComponent()) continue;
		Prim->SetRenderCustomDepth(bFocused);
		Prim->SetCustomDepthStencilValue(bFocused ? 1 : 0);
	}
}

TArray<FVilaAction> AVilaNPC::GetActions(AVilaPlayerCharacter* Player)
{
	TArray<FVilaAction> Acts;
	if (State == TEXT("hidden")) return Acts;
	const FString Name = DisplayName();
	if (State == TEXT("knocked"))
	{
		vila::PlayerState& PS = vila::game().player;
		Acts.Emplace(TEXT("help"), Trf("Ajudar %s a levantar", U(Name)));
		Acts.Emplace(TEXT("first_aid"),
			S(vila::fmt(vila::tr("Prestar primeiros socorros (kit) — Primeiros socorros %d"), {PS.skills.level("primeiros_socorros")})),
			PS.inventory.has("kit_curativo"), Tr("Precisa de um kit de primeiros socorros"));
		return Acts;
	}
	if (IsSleeping())
	{
		Acts.Emplace(TEXT("none"), Trf("%s está dormindo", U(Name)), false);
		return Acts;
	}
	AVilaWorld* W = GetVilaWorld();
	if (W) Acts = W->ExtraNPCActions(this);
	if (AVilaFurniture* Counter = MyCounter())
		if (IVilaInteractable* CI = Cast<IVilaInteractable>(Counter))
			for (const FVilaAction& A : CI->GetActions(Player))
				if (A.Id != TEXT("talk_staff")) Acts.Add(A);
	Acts.Emplace(TEXT("talk"), Trf("Conversar com %s", U(Name)));
	Acts.Emplace(TEXT("greet"), Tr("Cumprimentar"));
	return Acts;
}

// Atendente no balcão: oferece as ações da loja diretamente.
AVilaFurniture* AVilaNPC::MyCounter() const
{
	if (State != TEXT("at_spot") || Activity != TEXT("work")) return nullptr;
	const AVilaWorld* W = GetVilaWorld();
	return W ? W->CounterForStaff(SpotId) : nullptr;
}

void AVilaNPC::DoAction(AVilaPlayerCharacter* Player, const FString& ActionId)
{
	AVilaWorld* W = GetVilaWorld();
	if (W && W->DoExtraNPCAction(this, ActionId)) return;
	if (AVilaFurniture* Counter = MyCounter())
		if (ActionId == TEXT("shop") || ActionId == TEXT("delivery"))
		{
			if (IVilaInteractable* CI = Cast<IVilaInteractable>(Counter)) CI->DoAction(Player, ActionId);
			return;
		}
	if (ActionId == TEXT("first_aid"))
	{
		if (!Player) return;
		const float T = 4.f * (1.f - 0.06f * vila::game().player.skills.level("primeiros_socorros"));
		TWeakObjectPtr<AVilaNPC> Weak(this);
		Player->StartTimed(Tr("Prestando primeiros socorros..."), T, [Weak]()
		{
			if (AVilaNPC* Self = Weak.Get()) Self->FirstAid();
		});
	}
	else if (ActionId == TEXT("talk"))
	{
		if (W) W->StartDialogue(this);
	}
	else if (ActionId == TEXT("greet"))
	{
		vila::Relations& R = vila::game().relations;
		const std::string Id = U(NpcId);
		const int Greeted = vila::to_int(vila::jget(R.greeted_day, Id), -1);
		const char* Lines = vila::clock().is_night() ? "greet_night" : "greet";
		if (R.get_rel(Id) <= -30) Bark(Tr("Hum."));
		else Bark(Pick(Lines));
		if (Greeted != vila::clock().day())
		{
			R.greeted_day[Id] = vila::clock().day();
			R.change(Id, 1);
			vila::game().player.skills.add_xp("social", 3);
		}
	}
	else if (ActionId == TEXT("help"))
	{
		HelpUp();
	}
}

void AVilaNPC::BeginTalk()
{
	State = TEXT("talking");
	bTalking = true;
	Humanoid->SetPose(TEXT("talk"));
	GetCharacterMovement()->StopMovementImmediately();
}

void AVilaNPC::EndTalk()
{
	bTalking = false;
	State = TEXT("idle");
	Entry = vila::Json::object();
	Decide();
}

void AVilaNPC::OnBumped(AActor* By)
{
	if (BarkCd > 0.f || IsSleeping()) return;
	++Bumps;
	Bark(Pick("bump"));
	if (Bumps >= 3)
	{
		Bumps = 0;
		vila::game().relations.change(U(NpcId), -2);
	}
}

bool AVilaNPC::HasDoorAccess(const AVilaDoor* Door) const
{
	if (State != TEXT("moving") || !Door) return false;
	return !Door->IsLocked() || Door->NpcHasKey(NpcId);
}

bool AVilaNPC::CanWitness() const
{
	return !IsHidden() && State != TEXT("knocked") && State != TEXT("hidden") && !IsSleeping();
}

// ------------------------------------------------------------------ atropelamento
void AVilaNPC::KnockDown(const FVector& FromDir, float Severity, bool bByPlayer, float Duration)
{
	if (State == TEXT("knocked") || State == TEXT("hidden")) return;
	State = TEXT("knocked");
	bHitByPlayer = bByPlayer;
	KnockT = Duration > 0.f ? Duration : 9.f + Severity * 3.f;
	SetBodyEnabled(false);
	Humanoid->SetPose(TEXT("knocked"));
	SetVisualOffset(12.f);
	FVector D = FromDir;
	D.Z = 0.0;
	if (D.Size() > 0.01) SetActorLocation(GetActorLocation() + D.GetSafeNormal() * FMath::Min(3.f, 0.8f + Severity * 0.5f) * 100.f, false, nullptr, ETeleportType::TeleportPhysics);
	Bark(Pick("knocked"));
	vila::Json& St = vila::game().world_state.npcs[U(NpcId)];
	if (!St.is_object()) St = vila::Json::object();
	St["injured_until"] = vila::clock().total_minutes + 360.0;
	if (bByPlayer)
		if (AVilaWorld* W = GetVilaWorld()) W->OnNPCHit(this, Severity);
}

void AVilaNPC::FirstAid()
{
	vila::PlayerState& PS = vila::game().player;
	if (State != TEXT("knocked") || !PS.inventory.has("kit_curativo")) return;
	const std::string Id = U(NpcId);
	PS.inventory.remove("kit_curativo", 1);
	PS.skills.add_xp("primeiros_socorros", 30);
	vila::Relations& R = vila::game().relations;
	R.change(Id, 10);
	R.change_reputation(2);
	R.add_memory(Id, "prestou_socorro", vila::clock().day());
	vila::Json& St = vila::game().world_state.npcs[Id];
	if (!St.is_object()) St = vila::Json::object();
	St.erase("injured_until");
	HelpUp();
}

void AVilaNPC::HelpUp()
{
	if (State != TEXT("knocked")) return;
	Bark(Pick("help_thanks"));
	vila::game().relations.change(U(NpcId), 5);
	if (AVilaWorld* W = GetVilaWorld()) W->OnNPCHelped(this);
	GetUp();
}

void AVilaNPC::GetUp()
{
	SetVisualOffset(0.f);
	Humanoid->SetPose(TEXT("idle"));
	SetBodyEnabled(true);
	State = TEXT("idle");
	Entry = vila::Json::object();
	if (bHitByPlayer) vila::game().relations.add_memory(U(NpcId), "atropelado", vila::clock().day());
	bHitByPlayer = false;
}

// ------------------------------------------------------------------ falas
void AVilaNPC::Bark(const FString& Text)
{
	if (Text.IsEmpty() || IsHidden()) return;
	vila::events().bark.emit(U(NpcId), U(Text));
	if (!vila::truthy(vila::settings().get_value("game", "subtitles"))) return;
	BarkText = Text;
	BarkT = 4.f;
	BarkCd = 3.f;
}

FString AVilaNPC::CurrentBark() const
{
	return BarkT > 0.f ? BarkText : FString();
}

float AVilaNPC::BarkAlpha() const
{
	return FMath::Clamp(BarkT, 0.f, 1.f);
}

FString AVilaNPC::Pick(const char* Key) const
{
	const vila::Json& List = vila::jarr(vila::data().chatter(), Key);
	if (List.empty()) return FString();
	return Tr(vila::to_str(List[static_cast<size_t>(FMath::RandRange(0, static_cast<int32>(List.size()) - 1))]));
}
