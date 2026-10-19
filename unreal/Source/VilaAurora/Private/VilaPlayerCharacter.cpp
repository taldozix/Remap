#include "VilaPlayerCharacter.h"

#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaCameraRig.h"
#include "VilaConv.h"
#include "VilaFurniture.h"
#include "VilaHumanoid.h"
#include "VilaNPC.h"
#include "VilaPlayerController.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "Vila/Clock.h"
#include "Vila/Events.h"
#include "Vila/Game.h"

using namespace VilaConv;

namespace
{
	constexpr float CapsuleRadius = 30.f;      // 0,3 m
	constexpr float CapsuleHalfHeight = 87.5f;  // altura 1,75 m

	float LerpYaw(float From, float To, float Alpha)
	{
		return FRotator::NormalizeAxis(From + FMath::FindDeltaAngleDegrees(From, To) * Alpha);
	}

	void VilaNotify(const FString& Text, const char* Kind)
	{
		vila::events().notify.emit(U(Text), Kind);
	}
}

AVilaPlayerCharacter::AVilaPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->bUseControllerDesiredRotation = false;
	// O Godot troca a velocidade instantaneamente (velocity = dir * spd).
	Move->MaxAcceleration = 20000.f;
	Move->BrakingDecelerationWalking = 20000.f;
	Move->GroundFriction = 8.f;
	Move->MaxWalkSpeed = WalkSpeed * 100.f;
	Move->MaxStepHeight = 35.f;
	Move->GravityScale = 1.f;  // 9,8 m/s² como no Godot

	Humanoid = CreateDefaultSubobject<UVilaHumanoid>(TEXT("Humanoid"));
	Humanoid->SetupAttachment(GetCapsuleComponent());
	Humanoid->SetRelativeLocation(FVector(0.f, 0.f, -CapsuleHalfHeight));
}

void AVilaPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	Tags.AddUnique(FName(TEXT("player")));
	Humanoid->Build(vila::game().player.visual_appearance());
	Humanoid->SetPose(TEXT("idle"));
	// A troca de roupa (appearance_changed) é ligada pelo AVilaWorld, como no GameWorld.gd.
}

void AVilaPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Old = Outlined.Get())
		if (IVilaInteractable* I = Cast<IVilaInteractable>(Old)) I->SetFocused(false);
	Super::EndPlay(EndPlayReason);
}

AVilaWorld* AVilaPlayerCharacter::GetVilaWorld() const
{
	if (!CachedWorld.IsValid()) CachedWorld = AVilaWorld::Get(this);
	return CachedWorld.Get();
}

AVilaPlayerController* AVilaPlayerCharacter::GetVilaController() const
{
	return Cast<AVilaPlayerController>(GetController());
}

FVector AVilaPlayerCharacter::FeetLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

void AVilaPlayerCharacter::SetFeetLocation(const FVector& Feet)
{
	SetActorLocation(Feet + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);
}

void AVilaPlayerCharacter::SetVisualOffset(float ZCm)
{
	VisualOffset = ZCm;
	Humanoid->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + ZCm));
}

// _shape.disabled do Godot. Sem colisão, o movimento também para (senão a gravidade derruba).
void AVilaPlayerCharacter::SetBodyEnabled(bool bEnabled)
{
	GetCapsuleComponent()->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->StopMovementImmediately();
	if (bEnabled) Move->SetMovementMode(MOVE_Walking);
	else Move->DisableMovement();
}

void AVilaPlayerCharacter::PlaceFromState()
{
	const vila::PlayerState& P = vila::game().player;
	SetFeetLocation(ToUE(P.position) + FVector(0.f, 0.f, 5.f));
	SetActorRotation(FRotator(0.f, YawFromGodot(FMath::RadiansToDegrees(P.rotation_y)), 0.f));
}

// Troca de roupa: reconstrói o visual mantendo pose, posição e visibilidade.
void AVilaPlayerCharacter::RebuildVisual()
{
	const FString Pose = Humanoid->GetPose();
	const bool bVisible = Humanoid->IsVisible();
	Humanoid->Build(vila::game().player.visual_appearance());
	Humanoid->SetPose(Pose);
	Humanoid->SetVisibility(bVisible, true);
	SetVisualOffset(VisualOffset);
}

// ------------------------------------------------------------------ quadro
void AVilaPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ScanT -= DeltaSeconds;
	if (ScanT <= 0.f)
	{
		ScanT = 0.1f;
		ScanInteractables();
	}
	StepMovement(DeltaSeconds);
}

void AVilaPlayerCharacter::StepMovement(float Dt)
{
	BumpT -= Dt;
	vila::PlayerState& PS = vila::game().player;
	if (State == TEXT("driving"))
	{
		if (Vehicle) SetFeetLocation(Vehicle->GetGroundLocation());
		PS.activity = "drive";
		return;
	}
	if (bTimed) ProcessTimed(Dt);
	AVilaWorld* World = GetVilaWorld();
	AVilaPlayerController* PC = GetVilaController();
	FVector2D Input = FVector2D::ZeroVector;
	if (!bInputLocked && World && !World->UIBlocking() && PC) Input = PC->GetMoveAxis();
	if (State == TEXT("sitting") || State == TEXT("lying") || State == TEXT("sleeping"))
	{
		PS.activity = State == TEXT("sleeping") ? "sleep" : "rest";
		return;
	}
	vila::Needs& N = PS.needs;
	const float InLen = static_cast<float>(Input.Size());
	const bool bRunning = PC && PC->IsActionDown(TEXT("run")) && N.can_run() && !PS.is_overloaded() && InLen > 0.1f && !bInputLocked;
	float Spd = (bRunning ? RunSpeed * (1.f + 0.03f * PS.skills.level("condicionamento")) : WalkSpeed) * static_cast<float>(N.speed_multiplier());
	if (PS.is_overloaded()) Spd *= 0.6f;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	FVector Dir = FVector::ZeroVector;
	if (InLen > 0.05f)
	{
		// "Cima" na tela = direção da câmera no chão (X = direita, Y = cima).
		AVilaCameraRig* Rig = World ? World->CameraRig.Get() : nullptr;
		const float Yaw = Rig ? Rig->MoveYaw() : (World ? World->CameraYaw() : 0.f);
		Dir = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(Input.Y, Input.X, 0.f));
		Dir.Z = 0.0;
		Dir = Dir.GetSafeNormal() * FMath::Min(1.f, InLen);
		if (bTimed) CancelTimed();
	}
	Move->MaxWalkSpeed = Spd * 100.f;
	if (!Dir.IsNearlyZero()) AddMovementInput(Dir.GetSafeNormal(), static_cast<float>(Dir.Size()));

	const float Hv = static_cast<float>(GetVelocity().Size2D()) / 100.f;
	if (Hv > 0.2f)
	{
		if (!Dir.IsNearlyZero())
			SetActorRotation(FRotator(0.f, LerpYaw(GetActorRotation().Yaw, Dir.Rotation().Yaw, FMath::Min(1.f, Dt * 12.f)), 0.f));
		Humanoid->SetPose(bRunning ? TEXT("run") : TEXT("walk"));
		Humanoid->SetSpeed(Hv);
		StepT -= Dt;
		if (StepT <= 0.f)
		{
			StepT = bRunning ? 0.3f : 0.48f;
			if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->PlayAt(TEXT("step"), FeetLocation(), -16.f, FMath::FRandRange(0.85f, 1.15f));
		}
		if (bRunning)
		{
			RunDist += Hv * Dt;
			if (RunDist > 25.f)
			{
				RunDist = 0.f;
				PS.skills.add_xp("condicionamento", 2.0);
			}
		}
	}
	else if (!bTimed)
	{
		Humanoid->SetPose(TEXT("idle"));
	}
	const bool bOnShiftWork = vila::game().job.on_shift && World && static_cast<vila::IWorld*>(World)->is_player_in_job_area();
	if (bRunning) PS.activity = "run";
	else if (bTimed || bOnShiftWork) PS.activity = "work";
	else if (Hv > 0.2f) PS.activity = "walk";
	else PS.activity = "idle";
	// A cápsula já segue o chão real (rampas incluídas): sem o ajuste de visual.position.y do Godot.
}

// Esbarrões em NPCs (get_slide_collision do Godot): só quando o jogador se move contra eles.
void AVilaPlayerCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
	FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (!bSelfMoved || BumpT > 0.f) return;
	if (AVilaNPC* Npc = Cast<AVilaNPC>(Other))
	{
		BumpT = 2.f;
		Npc->OnBumped(this);
	}
}

// ------------------------------------------------------------------ interação
void AVilaPlayerCharacter::SetFocusTarget(AActor* NewFocus, bool bOutline)
{
	Focus = NewFocus;
	AActor* Want = bOutline ? NewFocus : nullptr;
	if (Outlined.Get() == Want) return;
	if (AActor* Old = Outlined.Get())
		if (IVilaInteractable* I = Cast<IVilaInteractable>(Old)) I->SetFocused(false);
	Outlined = Want;
	if (Want)
		if (IVilaInteractable* I = Cast<IVilaInteractable>(Want)) I->SetFocused(true);
}

void AVilaPlayerCharacter::ScanInteractables()
{
	FocusActions.Reset();
	AVilaWorld* World = GetVilaWorld();
	if (State == TEXT("driving") || !World)
	{
		if (State == TEXT("driving") && Vehicle)
		{
			SetFocusTarget(Vehicle, false);
			FocusActions = Vehicle->GetDriverActions(this);
		}
		else
		{
			SetFocusTarget(nullptr, false);
		}
		return;
	}
	if (State == TEXT("sitting") || State == TEXT("lying"))
	{
		SetFocusTarget(Seat, false);
		FocusActions = SeatedActions();
		return;
	}

	struct FCand
	{
		float Score;
		AActor* Actor;
	};
	TArray<FCand> Cands;
	const FVector Feet = FeetLocation();
	FVector Fwd = GetActorForwardVector();
	Fwd.Z = 0.0;
	Fwd = Fwd.GetSafeNormal();
	UWorld* W = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VilaInteractScan), false, this);
	const FCollisionObjectQueryParams Statics(ECC_WorldStatic);
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (A == this || !IsValid(A) || A->IsHidden() || !A->Implements<UVilaInteractable>()) continue;
		IVilaInteractable* I = Cast<IVilaInteractable>(A);
		if (!I) continue;
		const FVector P = I->GetInteractPoint();
		FVector Diff = P - Feet;
		Diff.Z = 0.0;
		const float D = static_cast<float>(Diff.Size());
		if (D > I->GetInteractRadius()) continue;
		const float Dm = D / 100.f;
		float Score = Dm - (Dm > 0.05f ? static_cast<float>(FVector::DotProduct(Diff.GetSafeNormal(), Fwd)) * 0.7f : 0.7f);
		Score -= I->GetFocusPriority();
		// Linha de visão até o alvo (paredes bloqueiam), com 95 cm de tolerância.
		FHitResult Hit;
		FCollisionQueryParams Q = Params;
		Q.AddIgnoredActor(A);
		const FVector To = P + FVector(0, 0, 100);
		if (W->LineTraceSingleByObjectType(Hit, Feet + FVector(0, 0, 110), To, Statics, Q) && FVector::Dist(Hit.Location, To) > 95.f) continue;
		Cands.Add({Score, A});
	}
	Cands.Sort([](const FCand& L, const FCand& R) { return L.Score < R.Score; });

	TArray<TWeakObjectPtr<AActor>> Nodes;
	for (const FCand& C : Cands) Nodes.Add(C.Actor);
	if (Nodes != FocusCandidates)
	{
		// Mantém o alvo escolhido com [T] enquanto ele continuar ao alcance.
		AActor* Keep = FocusCandidates.Num() > 0 ? FocusCandidates[Cycle % FocusCandidates.Num()].Get() : nullptr;
		FocusCandidates = Nodes;
		const int32 Idx = Keep ? Nodes.IndexOfByKey(TWeakObjectPtr<AActor>(Keep)) : INDEX_NONE;
		Cycle = Idx != INDEX_NONE ? Idx : 0;
	}
	if (Nodes.Num() > 0)
	{
		AActor* F = Nodes[Cycle % Nodes.Num()].Get();
		SetFocusTarget(F, true);
		if (IVilaInteractable* I = Cast<IVilaInteractable>(F)) FocusActions = I->GetActions(this);
	}
	else
	{
		SetFocusTarget(nullptr, true);
	}
}

// Alterna entre os alvos ao alcance (útil em lugares cheios de gente e objetos).
void AVilaPlayerCharacter::CycleTarget()
{
	if (FocusCandidates.Num() > 1)
	{
		Cycle = (Cycle + 1) % FocusCandidates.Num();
		ScanT = 0.f;
		ScanInteractables();
	}
}

AActor* AVilaPlayerCharacter::GetFocus() const
{
	return Focus.Get();
}

TArray<FVilaAction> AVilaPlayerCharacter::GetFocusActions() const
{
	return Focus.IsValid() ? FocusActions : TArray<FVilaAction>();
}

TArray<FVilaAction> AVilaPlayerCharacter::SeatedActions() const
{
	TArray<FVilaAction> Out;
	Out.Emplace(TEXT("stand"), Tr("Levantar"));
	Out.Emplace(TEXT("wait"), Tr("Esperar 1 hora"));
	if (State == TEXT("lying") && Seat && Seat->Kind == TEXT("bed"))
	{
		const IVilaInteractable* I = Cast<IVilaInteractable>(Seat.Get());
		const FString Building = I ? I->GetBuildingId() : FString();
		if (vila::game().is_mine(U(Seat->OwnerId), U(Building))) Out.Emplace(TEXT("sleep"), Tr("Dormir..."));
	}
	return Out;
}

void AVilaPlayerCharacter::DoFocusAction(int32 Index)
{
	if (bTimed) return;
	ScanInteractables();
	AActor* F = Focus.Get();
	if (!F || FocusActions.Num() == 0) return;
	const FVilaAction* Act = nullptr;
	if (Index < 0)
	{
		for (const FVilaAction& A : FocusActions)
			if (A.bEnabled)
			{
				Act = &A;
				break;
			}
	}
	else if (Index < FocusActions.Num())
	{
		Act = &FocusActions[Index];
	}
	if (!Act || !Act->bEnabled)
	{
		if (Act && !Act->Hint.IsEmpty()) VilaNotify(Act->Hint, "bad");
		return;
	}
	const FString Id = Act->Id;
	if (F == Seat.Get() && (State == TEXT("sitting") || State == TEXT("lying")))
	{
		if (Id == TEXT("stand")) StandUp();
		else if (Id == TEXT("wait")) WaitHour();
		else if (Id == TEXT("sleep")) vila::events().request_sleep.emit(U(Seat->ItemId), 1.0);
		return;
	}
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(TEXT("ui_click"), -12.f);
	if (IVilaInteractable* I = Cast<IVilaInteractable>(F)) I->DoAction(this, Id);
}

void AVilaPlayerCharacter::OnVehicleKey()
{
	if (State == TEXT("driving"))
	{
		ExitVehicle();
		return;
	}
	AVilaWorld* World = GetVilaWorld();
	AVilaVehicle* V = World ? World->NearestVehicle(FeetLocation(), 320.f) : nullptr;
	IVilaInteractable* I = Cast<IVilaInteractable>(V);
	if (!I) return;
	for (const FVilaAction& A : I->GetActions(this))
		if (A.bEnabled && (A.Id == TEXT("enter") || A.Id == TEXT("breakin")))
		{
			I->DoAction(this, A.Id);
			break;
		}
}

// Executa uma ação programaticamente (usado pelo teste de aceitação).
void AVilaPlayerCharacter::Perform(AActor* Target, const FString& ActionId)
{
	if (IVilaInteractable* I = Cast<IVilaInteractable>(Target)) I->DoAction(this, ActionId);
}

// ------------------------------------------------------------------ ações com duração
void AVilaPlayerCharacter::StartTimed(const FString& Label, float Seconds, TFunction<void()> OnDone, const FString& Sound)
{
	bTimed = true;
	TimedText = Label;
	TimedT = 0.f;
	TimedDur = FMath::Max(0.1f, Seconds);
	TimedCallback = MoveTemp(OnDone);
	Humanoid->SetPose(TEXT("work"));
	if (!Sound.IsEmpty())
		if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->PlayAt(Sound, FeetLocation(), -8.f);
}

void AVilaPlayerCharacter::ProcessTimed(float Dt)
{
	TimedT += Dt;
	if (TimedT >= TimedDur)
	{
		TFunction<void()> Cb = MoveTemp(TimedCallback);
		TimedCallback = nullptr;
		bTimed = false;
		TimedText.Reset();
		Humanoid->SetPose(TEXT("idle"));
		if (Cb) Cb();
	}
}

void AVilaPlayerCharacter::CancelTimed()
{
	bTimed = false;
	TimedText.Reset();
	TimedCallback = nullptr;
	VilaNotify(Tr("Ação cancelada."), "info");
}

bool AVilaPlayerCharacter::IsTimedActive() const
{
	return bTimed;
}

float AVilaPlayerCharacter::TimedProgress() const
{
	return bTimed ? FMath::Clamp(TimedT / TimedDur, 0.f, 1.f) : 0.f;
}

FString AVilaPlayerCharacter::TimedLabel() const
{
	return bTimed ? TimedText : FString();
}

// ------------------------------------------------------------------ sentar, deitar
void AVilaPlayerCharacter::SitOn(AVilaFurniture* F)
{
	if (!F) return;
	Seat = F;
	State = TEXT("sitting");
	SetBodyEnabled(false);
	const FTransform T = F->SeatTransform();
	const FVector Loc = T.GetLocation();
	SetFeetLocation(FVector(Loc.X, Loc.Y, F->GetActorLocation().Z));
	SetActorRotation(FRotator(0.f, F->Forward().Rotation().Yaw, 0.f));
	Humanoid->SetPose(TEXT("sit"));
	SetVisualOffset(F->SeatHeightCm - 47.f);
}

void AVilaPlayerCharacter::RestOnBed(AVilaFurniture* F)
{
	if (!F) return;
	Seat = F;
	State = TEXT("lying");
	SetBodyEnabled(false);
	const FVector Fwd = F->Forward().GetSafeNormal2D();
	SetActorRotation(FRotator(0.f, Fwd.Rotation().Yaw, 0.f));
	// Godot: Basis(facing) * (0, 0, 0.85) = 85 cm para trás da frente da cama.
	SetFeetLocation(F->GetActorLocation() - Fwd * 85.f);
	Humanoid->SetPose(TEXT("lie"));
	SetVisualOffset(62.f);
}

void AVilaPlayerCharacter::StandUp()
{
	if (AVilaFurniture* F = Seat.Get())
	{
		const FVector Fwd = F->Forward().GetSafeNormal2D();
		FVector Out = F->GetActorLocation() + Fwd * (F->Kind != TEXT("bed") ? 75.f : 130.f);
		if (F->Kind == TEXT("bed"))
		{
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
			Out = F->GetActorLocation() + Right * 120.f - Fwd * 20.f;
		}
		SetFeetLocation(FVector(Out.X, Out.Y, F->GetActorLocation().Z + 5.f));
	}
	Seat = nullptr;
	State = TEXT("free");
	SetBodyEnabled(true);
	SetVisualOffset(0.f);
	Humanoid->SetPose(TEXT("idle"));
}

void AVilaPlayerCharacter::WaitHour()
{
	vila::game().player.activity = "rest";
	vila::clock().advance(60.0);
	VilaNotify(Tr("Uma hora se passou."), "info");
}

void AVilaPlayerCharacter::BeginSleep()
{
	State = TEXT("sleeping");
	Humanoid->SetPose(TEXT("lie"));
}

void AVilaPlayerCharacter::EndSleep()
{
	State = Seat ? TEXT("lying") : TEXT("free");
}

// ------------------------------------------------------------------ veículos
void AVilaPlayerCharacter::EnterVehicle(AVilaVehicle* V)
{
	if (!V) return;
	if (bTimed) CancelTimed();
	Vehicle = V;
	State = TEXT("driving");
	SetBodyEnabled(false);
	Humanoid->SetVisibility(false, true);
	vila::game().player.vehicle_id = U(V->VehicleId);
	V->SetDriver(this);
	SetFeetLocation(V->GetGroundLocation());
	vila::events().player_vehicle_changed.emit(U(V->VehicleId));
}

void AVilaPlayerCharacter::ExitVehicle()
{
	AVilaVehicle* V = Vehicle.Get();
	if (!V) return;
	if (FMath::Abs(V->Speed) > 300.f)
	{
		VilaNotify(Tr("Pare o carro antes de sair."), "bad");
		return;
	}
	const FVector Spot = V->ExitPosition();
	V->SetDriver(nullptr);
	SetFeetLocation(Spot);
	SetActorRotation(FRotator(0.f, V->GetActorRotation().Yaw, 0.f));
	Vehicle = nullptr;
	State = TEXT("free");
	SetBodyEnabled(true);
	Humanoid->SetVisibility(true, true);
	vila::game().player.vehicle_id.clear();
	vila::events().player_vehicle_changed.emit(std::string());
}

void AVilaPlayerCharacter::SyncToState()
{
	FVector P = FeetLocation();
	if (State == TEXT("driving") && Vehicle)
	{
		P = Vehicle->GetGroundLocation();
	}
	else if ((State == TEXT("sitting") || State == TEXT("lying") || State == TEXT("sleeping")) && Seat)
	{
		const FVector Out = Seat->GetActorLocation() + Seat->Forward().GetSafeNormal2D() * 80.f;
		P = FVector(Out.X, Out.Y, Seat->GetActorLocation().Z);
	}
	vila::PlayerState& PS = vila::game().player;
	PS.position = FromUE(P);
	PS.rotation_y = FMath::DegreesToRadians(GodotYawFromUE(GetActorRotation().Yaw));  // radianos, como o Godot
}
