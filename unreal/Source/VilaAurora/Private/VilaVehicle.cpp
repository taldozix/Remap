#include "VilaVehicle.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Settings.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaCarVisual.h"
#include "VilaConv.h"
#include "VilaEnvironment.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaTrafficCar.h"
#include "VilaWorld.h"

namespace
{
	// tr() + formatação estilo GDScript ("%s", "%d", "%%").
	FString VehFmt(const char* Key, std::initializer_list<vila::FmtArg> Args)
	{
		return VilaConv::S(vila::fmt(vila::tr(Key), Args));
	}

	void VehNotify(const FString& Text, const char* Type)
	{
		vila::events().notify.emit(VilaConv::U(Text), std::string(Type));
	}

	FVector VehFlat(FVector V)
	{
		V.Z = 0.0;
		return V;
	}

	float VehDbToLin(float Db) { return FMath::Pow(10.f, Db / 20.f); }

	constexpr float VehHeadlightCandela = 2500.f;  // Godot: light_energy 3.0, spot_range 22 m
}

AVilaVehicle::AVilaVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// Colisão: Godot layer 16 (veículos), mask 1|2|16 (mundo, jogador, veículos).
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetMobility(EComponentMobility::Movable);
	Box->InitBoxExtent(FVector(210.f, 90.f, 56.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_Vehicle);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Box->SetSimulatePhysics(false);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);

	Visual = CreateDefaultSubobject<UVilaCarVisual>(TEXT("Visual"));
	Visual->SetupAttachment(Box);

	auto MakeLight = [this](const TCHAR* Name) {
		USpotLightComponent* L = CreateDefaultSubobject<USpotLightComponent>(Name);
		L->SetupAttachment(Box);
		L->IntensityUnits = ELightUnits::Candelas;
		L->Intensity = 0.f;
		L->AttenuationRadius = 2200.f;
		L->InnerConeAngle = 20.f;
		L->OuterConeAngle = 32.f;
		L->LightColor = FColor(255, 242, 217);
		L->CastShadows = false;
		L->SetVisibility(false);
		return L;
	};
	HeadlightL = MakeLight(TEXT("HeadlightL"));
	HeadlightR = MakeLight(TEXT("HeadlightR"));
}

vila::Json& AVilaVehicle::State() const
{
	return vila::game().world_state.vehicles[VilaConv::U(VehicleId)];
}

void AVilaVehicle::Setup(const FString& InVehicleId, AVilaWorld* InWorld)
{
	VehicleId = InVehicleId;
	VilaWorld = InWorld;
	const vila::Json St = State();
	Def = vila::data().vehicle(vila::jstr(St, "type"));
	Fuel = static_cast<float>(vila::jnum(St, "fuel", 20.0));
	Damage = static_cast<float>(vila::jnum(St, "damage", 0.0));

	const vila::Json& Size = vila::jarr(Def, "size");
	WidthM = Size.size() >= 3 ? static_cast<float>(vila::to_num(Size[0])) : 1.8f;
	HeightM = Size.size() >= 3 ? static_cast<float>(vila::to_num(Size[1])) : 1.4f;
	LengthM = Size.size() >= 3 ? static_cast<float>(vila::to_num(Size[2])) : 4.2f;

	// Godot: caixa (w, h*0.8, l) com centro em y = 0.3 + h*0.4.
	BoxZ = (0.3f + HeightM * 0.4f) * 100.f;
	Box->SetBoxExtent(FVector(LengthM * 50.f, WidthM * 50.f, HeightM * 40.f));

	FVector P = VilaConv::JsonPos(vila::jget(St, "pos"));
	P.Z = BoxZ;  // o corpo fica sempre na altura 0 (o visual acompanha rampas)
	SetActorLocationAndRotation(P, FRotator(0.f, VilaConv::YawFromGodot(vila::jnum(St, "rot", 0.0)), 0.f), false, nullptr, ETeleportType::TeleportPhysics);

	Visual->Build(VilaConv::S(vila::jstr(Def, "kind", "sedan")), WidthM, HeightM, LengthM, VilaConv::Hex(vila::jstr(St, "color", "#888888")));
	VisualZ = VilaWorld ? VilaWorld->GroundHeight(P.X, P.Y) : 0.f;
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualZ - BoxZ));

	// Faróis: Godot (±w*0.32, 0.6, -l/2), inclinados 8° para baixo.
	HeadlightL->SetRelativeLocationAndRotation(FVector(LengthM * 50.f, -WidthM * 32.f, 60.f - BoxZ), FRotator(-8.f, 0.f, 0.f));
	HeadlightR->SetRelativeLocationAndRotation(FVector(LengthM * 50.f, WidthM * 32.f, 60.f - BoxZ), FRotator(-8.f, 0.f, 0.f));

	if (!EngineSound)
		if (UVilaAudio* Audio = UVilaAudio::Get(this))
		{
			EngineSound = Audio->CreateEngineLoop(this);
			if (EngineSound && !Driver) EngineSound->Stop();
		}
	UpdateDamageFx();
}

void AVilaVehicle::BeginPlay()
{
	Super::BeginPlay();
	// O AVilaWorld cria com SpawnActorDeferred + Setup + FinishSpawning (transform no chão):
	// garante a altura do corpo.
	if (!VehicleId.IsEmpty())
	{
		FVector L = GetActorLocation();
		L.Z = BoxZ;
		SetActorLocation(L, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

// ------------------------------------------------------------------ consultas
UVilaCarVisual* AVilaVehicle::GetVisual() const
{
	return Visual.Get();
}

FString AVilaVehicle::DisplayName() const
{
	return VilaConv::Tr(vila::jstr(Def, "name", "Veículo"));
}

float AVilaVehicle::FuelCapacity() const
{
	return static_cast<float>(vila::jnum(Def, "fuel_capacity", 40.0));
}

FString AVilaVehicle::OwnerId() const
{
	return VilaConv::S(vila::jstr(vila::jobj(vila::game().world_state.vehicles, VilaConv::U(VehicleId)), "owner"));
}

bool AVilaVehicle::IsPlayers() const
{
	if (vila::has(vila::jobj(vila::game().world_state.vehicles, VilaConv::U(VehicleId)), "for_sale")) return false;
	return OwnerId() == TEXT("player") || vila::game().has_flag("roubado_" + VilaConv::U(VehicleId));
}

float AVilaVehicle::MaxSpeed() const
{
	return static_cast<float>(vila::jnum(Def, "max_speed", 25.0) * (1.0 - vila::clampf((Damage - 50.0) / 100.0, 0.0, 0.5)));
}

FVector AVilaVehicle::GetGroundLocation() const
{
	FVector P = GetActorLocation();
	P.Z -= BoxZ;
	return P;
}

FVector AVilaVehicle::GetInteractPoint() const
{
	// Godot: global_transform * (-w/2 - 0.4, 0, -0.3) -> lado esquerdo, 30 cm à frente do centro.
	const FVector Fwd = VehFlat(GetActorForwardVector()).GetSafeNormal();
	const FVector Right = VehFlat(GetActorRightVector()).GetSafeNormal();
	return GetGroundLocation() + Fwd * 30.f - Right * (WidthM * 50.f + 40.f);
}

FString AVilaVehicle::GetInteractTitle() const
{
	const FString Owner = OwnerId();
	FString Who = Owner == TEXT("player") ? VilaConv::Tr("seu") : VilaConv::Tr(vila::jstr(vila::data().npc(VilaConv::U(Owner)), "name", "?"));
	if (vila::has(vila::jobj(vila::game().world_state.vehicles, VilaConv::U(VehicleId)), "for_sale")) Who = VilaConv::Tr("à venda");
	return FString::Printf(TEXT("%s (%s)"), *DisplayName(), *Who);
}

TArray<FVilaAction> AVilaVehicle::GetActions(AVilaPlayerCharacter* Player)
{
	TArray<FVilaAction> Out;
	vila::Game& G = vila::game();
	const vila::Json St = vila::jobj(G.world_state.vehicles, VilaConv::U(VehicleId));
	if (vila::has(St, "for_sale"))
	{
		const FString Reason = VilaWorld ? VilaWorld->ShopUnavailableReason(TEXT("oficina")) : FString();
		const int Price = vila::jint(vila::jobj(St, "for_sale"), "price");
		Out.Add(FVilaAction(TEXT("buy"), VehFmt("Comprar %s por R$ %d", {VilaConv::U(DisplayName()), Price}),
			Reason.IsEmpty() && G.player.money >= Price, !Reason.IsEmpty() ? Reason : VilaConv::Tr("Dinheiro insuficiente")));
		return Out;
	}
	if (IsPlayers())
	{
		Out.Add(FVilaAction(TEXT("enter"), VehFmt("Entrar e dirigir [%s]", {vila::settings().primary_key_text("vehicle")}),
			Damage < 100.f, VilaConv::Tr("Motor destruído — precisa de conserto")));
		Out.Add(FVilaAction(TEXT("trunk"), VilaConv::Tr("Abrir porta-malas")));
		if (Damage > 0.5f)
		{
			const bool bHasKit = G.player.inventory.has("kit_reparo");
			Out.Add(FVilaAction(TEXT("repair"), VehFmt("Consertar com kit de reparo (Mecânica %d)", {G.player.skills.level("mecanica")}),
				bHasKit, VilaConv::Tr("Precisa de um kit de reparo (Oficina do Rafael)")));
		}
		if (OwnerId() == TEXT("player") && VilaWorld &&
			static_cast<vila::IWorld*>(VilaWorld.Get())->is_vehicle_near_spot(VilaConv::U(VehicleId), "oficina_rafael", 18.0))
		{
			const FString Reason2 = VilaWorld->ShopUnavailableReason(TEXT("oficina"));
			Out.Add(FVilaAction(TEXT("sell"), VehFmt("Vender ao Rafael por R$ %d", {G.vehicle_sale_value(VilaConv::U(VehicleId))}), Reason2.IsEmpty(), Reason2));
		}
	}
	else
	{
		Out.Add(FVilaAction(TEXT("locked"), VehFmt("Trancado (pertence a %s)", {vila::tr(vila::jstr(vila::data().npc(VilaConv::U(OwnerId())), "name", "?"))}), false));
		Out.Add(FVilaAction(TEXT("breakin"), VilaConv::Tr("Arrombar a porta (crime)")));
	}
	return Out;
}

TArray<FVilaAction> AVilaVehicle::GetDriverActions(AVilaPlayerCharacter* Player)
{
	TArray<FVilaAction> Out;
	Out.Add(FVilaAction(TEXT("exit"), VehFmt("Sair do veículo [%s]", {vila::settings().primary_key_text("vehicle")})));
	return Out;
}

float AVilaVehicle::RepairAmount() const
{
	const int Lvl = vila::game().player.skills.level("mecanica");
	float Amount = 12.f + 4.f * Lvl;
	const FString Bid = VilaWorld ? VilaWorld->BuildingAt(GetGroundLocation()) : FString();
	if (!Bid.IsEmpty())
	{
		const std::string B = VilaConv::U(Bid);
		if (vila::jstr(vila::data().property(B), "type") == "garagem" && vila::game().property.owns(B)) Amount *= 1.5f;
	}
	return Amount;
}

void AVilaVehicle::RepairWithKit()
{
	vila::Game& G = vila::game();
	if (!G.player.inventory.has("kit_reparo")) return;
	G.player.inventory.remove("kit_reparo", 1);
	const float Amount = RepairAmount();
	Damage = FMath::Max(0.f, Damage - Amount);
	G.player.skills.add_xp("mecanica", 20.0);
	G.player.needs.consume(vila::Json{{"higiene", 8}});
	UpdateDamageFx();
	SaveState();
	if (UVilaAudio* A = UVilaAudio::Get(this)) A->PlayAt(TEXT("door_locked"), GetActorLocation(), -4.f, 0.7f);
	VehNotify(VehFmt("Conserto feito: -%d%% de dano (agora %d%%).", {static_cast<int>(Amount), static_cast<int>(Damage)}), "good");
}

void AVilaVehicle::DoAction(AVilaPlayerCharacter* Player, const FString& ActionId)
{
	vila::Game& G = vila::game();
	UVilaAudio* Audio = UVilaAudio::Get(this);
	TWeakObjectPtr<AVilaVehicle> WeakThis(this);
	if (ActionId == TEXT("enter"))
	{
		if (Damage >= 100.f || !Player) return;
		Visual->SetDoor(true);
		if (Audio) Audio->PlayAt(TEXT("door_close"), GetActorLocation(), -6.f, 1.3f);
		Player->EnterVehicle(this);
		GetWorldTimerManager().SetTimer(DoorTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
			if (Visual) Visual->SetDoor(false);
		}), 0.35f, false);
	}
	else if (ActionId == TEXT("buy"))
	{
		if (G.buy_vehicle(VilaConv::U(VehicleId)) && Audio) Audio->Play(TEXT("cash"));
	}
	else if (ActionId == TEXT("sell"))
	{
		const int Value = G.sell_vehicle(VilaConv::U(VehicleId));
		if (Value > 0)
		{
			if (Audio) Audio->Play(TEXT("cash"));
			VehNotify(VehFmt("Vendido por R$ %d.", {Value}), "money");
		}
	}
	else if (ActionId == TEXT("repair"))
	{
		if (!Player) return;
		const float T = 6.f * (1.f - 0.05f * G.player.skills.level("mecanica")) * static_cast<float>(G.player.needs.work_multiplier());
		Player->StartTimed(VilaConv::Tr("Consertando o carro..."), T, [WeakThis]() {
			if (WeakThis.IsValid()) WeakThis->RepairWithKit();
		});
	}
	else if (ActionId == TEXT("trunk"))
	{
		// O HUD escuta request_container e abre o painel (AVilaHUD::OpenContainer).
		vila::events().request_container.emit("trunk_" + VilaConv::U(VehicleId), VilaConv::U(VehFmt("Porta-malas — %s", {VilaConv::U(DisplayName())})));
	}
	else if (ActionId == TEXT("breakin"))
	{
		if (!Player) return;
		Player->StartTimed(VilaConv::Tr("Forçando a fechadura..."), 4.f * static_cast<float>(G.player.needs.work_multiplier()), [WeakThis]() {
			if (WeakThis.IsValid()) WeakThis->BreakIn();
		});
	}
	else if (ActionId == TEXT("exit"))
	{
		if (Player) Player->ExitVehicle();
	}
}

void AVilaVehicle::BreakIn()
{
	if (UVilaAudio* A = UVilaAudio::Get(this)) A->PlayAt(TEXT("door_locked"), GetActorLocation(), 4.f);
	vila::game().set_flag("roubado_" + VilaConv::U(VehicleId));
	const vila::Vec3 Pos = VilaConv::FromUE(GetGroundLocation());
	vila::game().report_crime("furto_veiculo", VilaConv::U(OwnerId()), false, &Pos);
	VehNotify(VilaConv::Tr("Porta aberta. Este carro agora está com você — mas não é seu."), "bad");
}

void AVilaVehicle::SetFocused(bool bFocused)
{
	if (Visual) Visual->SetCustomDepth(bFocused);
}

// ------------------------------------------------------------------ motorista
void AVilaVehicle::SetDriver(AVilaPlayerCharacter* D)
{
	AVilaPlayerCharacter* Old = Driver;
	Driver = D;
	Box->ClearMoveIgnoreActors();
	if (D)
	{
		Box->IgnoreActorWhenMoving(D, true);
		if (EngineSound) EngineSound->Play();
	}
	else
	{
		if (EngineSound) EngineSound->Stop();
		Speed = 0.f;
		LatSpeed = 0.f;
		SaveState();
		// O Godot deixava as luzes como estavam ao descer; aqui apagam.
		Visual->SetLights(false, false);
		UpdateHeadlights(false);
		if (Old)
		{
			// porta abre e fecha ao descer
			Visual->SetDoor(true);
			GetWorldTimerManager().SetTimer(DoorTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
				if (Visual) Visual->SetDoor(false);
			}), 0.35f, false);
		}
	}
}

FVector AVilaVehicle::ExitPosition() const
{
	const FVector Fwd = VehFlat(GetActorForwardVector()).GetSafeNormal();
	const FVector Right = VehFlat(GetActorRightVector()).GetSafeNormal();
	const FVector G = GetGroundLocation();
	const float HalfW = WidthM * 50.f, HalfL = LengthM * 50.f;
	// Godot (x, z) local -> UE (frente = -z, direita = x): esquerda, direita, trás, frente.
	const FVector2D Offsets[] = {
		FVector2D(30.f, -(HalfW + 70.f)),
		FVector2D(30.f, HalfW + 70.f),
		FVector2D(-(HalfL + 100.f), 0.f),
		FVector2D(HalfL + 100.f, 0.f),
	};
	const UWorld* W = GetWorld();
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	Obj.AddObjectTypesToQuery(ECC_Pawn);
	Obj.AddObjectTypesToQuery(ECC_Vehicle);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VilaVehicleExit), false, this);
	if (Driver) Params.AddIgnoredActor(Driver.Get());
	for (const FVector2D& O : Offsets)
	{
		FVector P = G + Fwd * O.X + Right * O.Y;
		const float Ground = VilaWorld ? VilaWorld->GroundHeight(P.X, P.Y) : 0.f;
		P.Z = Ground + 5.f;
		if (!W || !W->OverlapAnyTestByObjectType(P + FVector(0.f, 0.f, 90.f), FQuat::Identity, Obj, FCollisionShape::MakeSphere(35.f), Params))
			return P;
	}
	FVector P = G - Right * (HalfW + 70.f);
	P.Z = (VilaWorld ? VilaWorld->GroundHeight(P.X, P.Y) : 0.f) + 5.f;
	return P;
}

void AVilaVehicle::SetDriveInput(float Throttle, float Steer, bool bHandbrake)
{
	InThrottle = FMath::Clamp(Throttle, -1.f, 1.f);
	InSteer = FMath::Clamp(Steer, -1.f, 1.f);
	bInHandbrake = bHandbrake;
	InputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void AVilaVehicle::Horn()
{
	if (!Driver || !VilaWorld || VilaWorld->UIBlocking()) return;
	if (UVilaAudio* A = UVilaAudio::Get(this)) A->PlayAt(TEXT("horn"), GetActorLocation(), 2.f);
	VilaWorld->OnHorn(GetGroundLocation());
}

// ------------------------------------------------------------------ física
void AVilaVehicle::MoveWithSlide(const FVector& Delta, TArray<FHitResult>& OutHits)
{
	// move_and_slide: até 4 deslizes ao longo das superfícies atingidas.
	FVector Remaining = VehFlat(Delta);
	for (int32 Iter = 0; Iter < 4 && !Remaining.IsNearlyZero(0.01); ++Iter)
	{
		FHitResult Hit;
		SetActorLocation(GetActorLocation() + Remaining, true, &Hit, ETeleportType::None);
		if (!Hit.bBlockingHit) break;
		OutHits.Add(Hit);
		FVector N = VehFlat(Hit.Normal);
		if (!N.Normalize()) break;
		if (Hit.bStartPenetrating)
		{
			// começou dentro de algo (girou contra a parede): empurra para fora
			SetActorLocation(GetActorLocation() + N * (Hit.PenetrationDepth + 0.5f), false, nullptr, ETeleportType::TeleportPhysics);
			break;
		}
		Remaining *= (1.f - Hit.Time);
		Remaining -= (Remaining | N) * N;
	}
}

void AVilaVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = DeltaSeconds;
	if (Dt <= 0.0 || Def.empty()) return;
	HitCd -= DeltaSeconds;
	AVilaWorld* W = VilaWorld;
	vila::Game& G = vila::game();

	double SpeedM = Speed / 100.0;
	double Throttle = 0.0, Brake = 0.0, SteerIn = 0.0;
	bool bHand = false;
	const bool bDriving = Driver != nullptr && W && !W->UIBlocking();
	if (bDriving)
	{
		// entrada velha (o controller parou de mandar) vale zero
		if (GetWorld()->GetTimeSeconds() - InputTime < 0.25)
		{
			Throttle = FMath::Max(0.f, InThrottle);
			Brake = FMath::Max(0.f, -InThrottle);
			SteerIn = -InSteer;  // Godot: esquerda positiva
			bHand = bInHandbrake;
		}
	}
	else if (FMath::Abs(SpeedM) < 0.05 && FMath::Abs(LatSpeed) < 0.05)
	{
		return;
	}

	const bool bEngineOk = Fuel > 0.f && Damage < 100.f && Driver != nullptr;
	const double MaxSpd = MaxSpeed();
	const double Accel = vila::jnum(Def, "accel", 6.0);
	const double Reverse = vila::jnum(Def, "reverse_speed", 6.0);
	if (!bEngineOk) Throttle = 0.0;
	// Longitudinal
	if (Throttle > 0.0 && SpeedM >= -0.5)
		SpeedM += Accel * Throttle * Dt * (1.0 - vila::clampf(SpeedM / MaxSpd, 0.0, 1.0) * 0.85);
	if (Brake > 0.0)
	{
		if (SpeedM > 0.5)
			SpeedM -= vila::jnum(Def, "brake", 13.0) * Brake * Dt;
		else if (bEngineOk)
			SpeedM = FMath::Max(-Reverse, SpeedM - Accel * 0.6 * Brake * Dt);
	}
	if ((Throttle == 0.0 && Brake == 0.0) || !bEngineOk)
		SpeedM = vila::move_toward(SpeedM, 0.0, (0.8 + 0.015 * SpeedM * SpeedM) * Dt * (Driver == nullptr ? 3.0 : 1.0));
	if (bHand) SpeedM = vila::move_toward(SpeedM, 0.0, 8.0 * Dt);
	SpeedM = vila::clampf(SpeedM, -Reverse, MaxSpd);

	// Direção (habilidade melhora a resposta)
	const int Skill = G.player.skills.level("direcao");
	const double MaxSteer = vila::jnum(Def, "steer", 0.58) * (1.0 - 0.55 * vila::clampf(FMath::Abs(SpeedM) / MaxSpd, 0.0, 1.0));
	SteerRad = static_cast<float>(vila::move_toward(SteerRad, SteerIn * MaxSteer, (2.2 + Skill * 0.15) * Dt));
	const double YawRate = SpeedM / vila::jnum(Def, "wheelbase", 2.7) * FMath::Tan(SteerRad);
	// Godot gira anti-horário (rotation.y += yaw_rate); o yaw do UE é horário.
	FRotator Rot = GetActorRotation();
	Rot.Pitch = 0.0;
	Rot.Roll = 0.0;
	Rot.Yaw = FRotator::NormalizeAxis(Rot.Yaw - FMath::RadiansToDegrees(YawRate * Dt));
	SetActorRotation(Rot);

	// Aderência lateral: com freio de mão o carro escorrega.
	const double Grip = vila::jnum(Def, "grip", 7.0) * (bHand ? 0.25 : 1.0) * (1.0 + Skill * 0.03);
	LatSpeed += static_cast<float>(-YawRate * SpeedM * 0.08 * Dt * (bHand ? 3.0 : 1.0));
	LatSpeed = static_cast<float>(vila::lerpf(LatSpeed, 0.0, FMath::Min(1.0, Grip * Dt)));

	const FVector Fwd = VehFlat(GetActorForwardVector()).GetSafeNormal();
	const FVector Right = VehFlat(GetActorRightVector()).GetSafeNormal();
	const FVector Before = Fwd * SpeedM + Right * LatSpeed;  // m/s

	// Atropelamento: NPCs não bloqueiam o sweep (no Godot não estavam na máscara).
	Box->ClearMoveIgnoreActors();
	if (Driver) Box->IgnoreActorWhenMoving(Driver.Get(), true);
	if (W)
		for (AVilaNPC* N : W->NPCsNear(GetActorLocation(), 1500.f))
			if (N) Box->IgnoreActorWhenMoving(N, true);

	// mantém o corpo na altura fixa
	if (!FMath::IsNearlyEqual(GetActorLocation().Z, static_cast<double>(BoxZ), 0.5))
	{
		FVector L = GetActorLocation();
		L.Z = BoxZ;
		SetActorLocation(L, false, nullptr, ETeleportType::TeleportPhysics);
	}
	TArray<FHitResult> Hits;
	MoveWithSlide(Before * 100.0 * Dt, Hits);

	// Colisões
	for (const FHitResult& Hit : Hits)
	{
		const FVector N = VehFlat(Hit.Normal);
		const double Impact = N.Size() > 0.01 ? -(Before | N.GetSafeNormal()) : 0.0;
		if (Impact > 2.5 && HitCd <= 0.f)
		{
			HitCd = 0.6f;
			OnCrash(Impact, Hit.GetActor());
		}
		SpeedM *= Impact < 2.5 ? 0.9 : 0.25;
	}

	// Pedestres
	if (FMath::Abs(SpeedM) > 1.5) HitPedestrians(SpeedM, Fwd, Right);

	// Combustível, distância e habilidade
	const double Moved = FMath::Abs(SpeedM) * Dt;
	if (Driver)
	{
		Fuel = static_cast<float>(FMath::Max(0.0, Fuel - (Moved / 1000.0) * vila::jnum(Def, "fuel_per_km", 0.1) * 40.0 *
			vila::jnum(G.player.needs.mods, "fuel", 1.0) * (0.4 + Throttle * 0.6) - Dt * 0.0004));
		if (Fuel <= 0.f && Throttle > 0.0 && HitCd <= 0.f)
		{
			HitCd = 3.f;
			VehNotify(VilaConv::Tr("Sem combustível!"), "bad");
		}
		DistAcc += Moved;
		if (DistAcc > 100.0)
		{
			DistAcc = 0.0;
			G.player.skills.add_xp("direcao", 3.0);
			G.player.stats["km_dirigidos"] = vila::jnum(G.player.stats, "km_dirigidos", 0.0) + 0.1;
		}
		if (EngineSound)
		{
			EngineSound->SetPitchMultiplier(static_cast<float>(0.55 + FMath::Abs(SpeedM) / MaxSpd * 1.3));
			EngineSound->SetVolumeMultiplier(VehDbToLin(static_cast<float>(-12.0 + Throttle * 4.0)));
		}
		// o HUD/diário leem o combustível do estado
		State()["fuel"] = Fuel;
	}

	Speed = static_cast<float>(SpeedM * 100.0);
	Visual->Animate(Speed, SteerRad, DeltaSeconds);
	const bool bNight = vila::clock().is_night();
	Visual->SetLights(bNight && Driver != nullptr, Brake > 0.0 && SpeedM > 0.3);
	UpdateHeadlights(bNight && Driver != nullptr);
	UpdateGround(DeltaSeconds);
}

void AVilaVehicle::HitPedestrians(double& SpeedM, const FVector& Fwd, const FVector& Right)
{
	AVilaWorld* W = VilaWorld;
	if (!W) return;
	// Área de atropelamento do Godot: caixa (w + 0.3, 1.2, l + 0.6) + raio da cápsula do NPC.
	const float HalfLen = (LengthM + 0.6f) * 50.f + 30.f;
	const float HalfWid = (WidthM + 0.3f) * 50.f + 30.f;
	const FVector Loc = VehFlat(GetActorLocation());
	for (AVilaNPC* N : W->NPCsNear(GetActorLocation(), HalfLen + 200.f))
	{
		if (!N || N->IsHidden()) continue;
		// estado fora de "knocked"/"hidden" (CanWitness exclui também quem dorme)
		if (!N->CanWitness() && !N->IsSleeping()) continue;
		const FVector Rel = VehFlat(N->GetActorLocation()) - Loc;
		if (FMath::Abs(Rel | Fwd) > HalfLen || FMath::Abs(Rel | Right) > HalfWid) continue;
		if (FMath::Abs(SpeedM) > 3.0)
		{
			N->KnockDown(Fwd * FMath::Sign(SpeedM), static_cast<float>(FMath::Abs(SpeedM) / 3.0), Driver != nullptr);
			SpeedM *= 0.7;
			if (UVilaAudio* A = UVilaAudio::Get(this)) A->PlayAt(TEXT("crash"), GetActorLocation(), -6.f, 1.4f);
		}
		else
		{
			N->OnBumped(this);
		}
	}
}

void AVilaVehicle::OnCrash(double Impact, AActor* Other)
{
	vila::Game& G = vila::game();
	const int Skill = G.player.skills.level("direcao");
	const double Dmg = (Impact - 2.5) * 4.0 * (1.0 - Skill * 0.04);
	Damage = static_cast<float>(vila::clampf(Damage + Dmg, 0.0, 100.0));
	if (UVilaAudio* A = UVilaAudio::Get(this))
		A->PlayAt(TEXT("crash"), GetActorLocation(), static_cast<float>(vila::clampf(Impact * 1.5 - 12.0, -12.0, 4.0)));
	if (Driver && Impact > 9.0)
	{
		G.player.needs.consume(vila::Json{{"saude", -(Impact - 9.0) * 3.0}});
		VehNotify(VilaConv::Tr("Você se machucou na batida!"), "bad");
	}
	if (AVilaTrafficCar* T = Cast<AVilaTrafficCar>(Other))
		if (Driver) T->OnHitByPlayer(static_cast<float>(Impact));
	if (Damage >= 100.f)
		VehNotify(VilaConv::Tr("O motor morreu. O carro precisa de conserto."), "bad");
	else if (Dmg > 8.0 && Driver)
		VehNotify(VehFmt("Batida! Dano do veículo: %d%%", {static_cast<int>(Damage)}), "bad");
	UpdateDamageFx();
	SaveState();
}

void AVilaVehicle::UpdateDamageFx()
{
	if (Visual) Visual->SetSmoke(Damage >= 65.f);
}

void AVilaVehicle::UpdateHeadlights(bool bOn)
{
	float Night = 1.f;
	if (bOn && VilaWorld && VilaWorld->Environment) Night = FMath::Max(0.35f, VilaWorld->Environment->NightAmount());
	const float I = bOn ? VehHeadlightCandela * Night : 0.f;
	for (USpotLightComponent* L : {HeadlightL.Get(), HeadlightR.Get()})
	{
		if (!L) continue;
		if (bOn != bHeadlightsOn) L->SetVisibility(bOn);
		if (bOn) L->SetIntensity(I);
	}
	bHeadlightsOn = bOn;
}

void AVilaVehicle::UpdateGround(float DeltaSeconds)
{
	if (!VilaWorld) return;
	const FVector L = GetActorLocation();
	VisualZ = FMath::Lerp(VisualZ, VilaWorld->GroundHeight(L.X, L.Y), FMath::Min(1.f, DeltaSeconds * 8.f));
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualZ - BoxZ));
}

// ------------------------------------------------------------------ estado
void AVilaVehicle::SaveState()
{
	if (VehicleId.IsEmpty()) return;
	vila::Json& St = State();
	const vila::Vec3 P = VilaConv::FromUE(GetActorLocation());
	St["pos"] = vila::Json::array({P.x, 0.0, P.z});
	St["rot"] = VilaConv::GodotYawFromUE(GetActorRotation().Yaw);
	St["fuel"] = Fuel;
	St["damage"] = Damage;
}

void AVilaVehicle::RefreshFromState()
{
	const vila::Json St = vila::jobj(vila::game().world_state.vehicles, VilaConv::U(VehicleId));
	Fuel = static_cast<float>(vila::jnum(St, "fuel", Fuel));
	Damage = static_cast<float>(vila::jnum(St, "damage", Damage));
	UpdateDamageFx();
}
