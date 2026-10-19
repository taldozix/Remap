#include "VilaWeather.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "VilaAssets.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaConv.h"
#include "Vila/Clock.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Settings.h"

namespace VilaWeatherImpl
{
	// Partículas de chuva do DayNight.gd (CPUParticles3D), em cm.
	constexpr float DropLifetime = 0.9f;
	constexpr float EmitHalfX = 2600.f;  // emission_box_extents (26, 1, 26) m
	constexpr float EmitHalfZ = 100.f;
	constexpr float SpeedMin = 2200.f;   // initial_velocity 22..26 m/s
	constexpr float SpeedMax = 2600.f;
	constexpr float Gravity = 980.f;     // 9,8 m/s²
	const FVector DropScale(0.02f, 0.02f, 0.5f);  // BoxMesh 0,02 x 0,5 x 0,02 m (cubo do motor = 1 m)
	const int32 DropCounts[] = {500, 1000, 1600};  // rain.amount por graphics.effects

	// direction = (0.15, -1, 0.05) do Godot no espaço UE (X = x, Y = z, Z = y).
	FVector RainDir() { return FVector(0.15f, 0.05f, -1.f).GetSafeNormal(); }

	float MoveToward(float From, float To, float Delta)
	{
		return FMath::Abs(To - From) <= Delta ? To : From + FMath::Sign(To - From) * Delta;
	}
}

UVilaWeather::UVilaWeather()
{
	PrimaryComponentTick.bCanEverTick = false;  // o AVilaEnvironment chama Update()
}

void UVilaWeather::Setup()
{
	AActor* Owner = GetOwner();
	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (Owner && Assets && !RainMesh)
	{
		RainMesh = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("RainDrops"));
		RainMesh->SetMobility(EComponentMobility::Movable);
		RainMesh->SetStaticMesh(Assets->Cube());
		UMaterialInterface* Mat = Assets->Master(TEXT("M_Rain"));
		if (!Mat) Mat = Assets->Glass(FLinearColor::FromSRGBColor(FColor(191, 209, 242)), 0.45f);  // Color(0.75, 0.82, 0.95, 0.45)
		RainMesh->SetMaterial(0, Mat);
		RainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RainMesh->SetCanEverAffectNavigation(false);
		RainMesh->SetCastShadow(false);
		RainMesh->bReceivesDecals = false;
		RainMesh->SetVisibility(false);
		// Sem pai: as gotas ficam no espaço do mundo (partículas globais, como no Godot).
		RainMesh->RegisterComponent();
		Owner->AddInstanceComponent(RainMesh);
	}
	DropRot = FRotationMatrix::MakeFromZ(-VilaWeatherImpl::RainDir()).ToQuat();

	TWeakObjectPtr<UVilaWeather> WeakThis(this);
	if (HourConn == 0)
		HourConn = vila::clock().hour_changed.connect([WeakThis](int Hour)
		{
			if (UVilaWeather* Self = WeakThis.Get()) Self->OnHour(Hour);
		});

	// Nuvens começam já no estado salvo; chuva e piso molhado vêm de 0, como no Godot.
	CloudAmount = TargetCloud();
	ApplySettings();
}

void UVilaWeather::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HourConn != 0) vila::clock().hour_changed.disconnect(HourConn);
	HourConn = 0;
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->SetAmbient(TEXT("rain"), false);
	Super::EndPlay(EndPlayReason);
}

void UVilaWeather::ApplySettings()
{
	const int32 Fx = vila::to_int(vila::settings().get_value("graphics", "effects"), 2);
	const int32 Count = VilaWeatherImpl::DropCounts[FMath::Clamp(Fx, 0, 2)];
	if (Count != DropCount || Drops.Num() != Count || (RainMesh && RainMesh->GetInstanceCount() != Count)) ResizeDrops(Count);
}

void UVilaWeather::SetMenuMode(bool bInMenu)
{
	bMenu = bInMenu;
	CloudAmount = TargetCloud();
}

FString UVilaWeather::State() const
{
	if (bMenu) return TEXT("limpo");
	return VilaConv::S(vila::jstr(vila::game().world_state.weather, "state", "limpo"));
}

float UVilaWeather::TargetCloud() const
{
	const FString S = State();
	if (S == TEXT("nublado")) return 0.45f;
	if (S == TEXT("chuva")) return 0.8f;
	return 0.f;
}

// ---------------------------------------------------------------- máquina de estados
void UVilaWeather::OnHour(int32 Hour)
{
	// O DayNight só existia no GameWorld: nada muda no menu principal.
	if (bMenu || vila::game().world == nullptr) return;
	vila::Json& W = vila::game().world_state.weather;
	const double Now = vila::clock().total_minutes;
	if (Now < vila::jnum(W, "until", 0.0)) return;
	const double R = Rng.randf();
	const double ChanceRain = (Hour >= 13 && Hour <= 19) ? 0.22 : 0.1;
	std::string NewState = "limpo";
	if (R < ChanceRain) NewState = "chuva";
	else if (R < ChanceRain + 0.25) NewState = "nublado";
	const std::string Old = vila::jstr(W, "state", "limpo");
	if (NewState != Old)
	{
		if (NewState == "chuva") vila::events().notify.emit(vila::tr("Começou a chover."), "info");
		else if (Old == "chuva") vila::events().notify.emit(vila::tr("A chuva parou."), "info");
	}
	W = vila::Json{{"state", NewState}, {"until", Now + Rng.randf_range(90.0, 240.0)}};
}

void UVilaWeather::ForceState(const FString& NewState, double Minutes)
{
	vila::game().world_state.weather = vila::Json{{"state", VilaConv::U(NewState)}, {"until", vila::clock().total_minutes + Minutes}};
}

// ---------------------------------------------------------------- quadro a quadro
void UVilaWeather::Update(float DeltaSeconds, const FVector& Emitter)
{
	const bool bRaining = State() == TEXT("chuva");
	if (bFirstUpdate)
	{
		bFirstUpdate = false;
		CloudAmount = TargetCloud();
	}
	// O Godot trocava a cobertura na hora; aqui ela muda devagar (sem salto no céu).
	CloudAmount = VilaWeatherImpl::MoveToward(CloudAmount, TargetCloud(), DeltaSeconds * 0.1f);
	Rain = VilaWeatherImpl::MoveToward(Rain, bRaining ? 1.f : 0.f, DeltaSeconds * 0.3f);
	Wet = VilaWeatherImpl::MoveToward(Wet, bRaining ? 1.f : 0.f, DeltaSeconds * (bRaining ? 0.08f : 0.02f));

	UpdateDrops(DeltaSeconds, Emitter, IsRainVisible());

	if (UVilaAudio* Audio = UVilaAudio::Get(this))
		Audio->SetAmbient(TEXT("rain"), IsRainVisible(), -14.f + Rain * 6.f);
}

void UVilaWeather::ResizeDrops(int32 Count)
{
	DropCount = Count;
	Drops.Reset();
	Drops.SetNum(Count);
	DropTransforms.Reset();
	DropTransforms.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Count);
	bAnyDropAlive = false;
	if (!RainMesh) return;
	RainMesh->ClearInstances();
	for (int32 I = 0; I < Count; ++I) RainMesh->AddInstance(DropTransforms[I], true);
	RainMesh->SetVisibility(false);
}

void UVilaWeather::SpawnDrop(FVilaRainDrop& D, const FVector& Emitter, bool bStagger)
{
	D.Pos = Emitter + FVector(FMath::FRandRange(-VilaWeatherImpl::EmitHalfX, VilaWeatherImpl::EmitHalfX), FMath::FRandRange(-VilaWeatherImpl::EmitHalfX, VilaWeatherImpl::EmitHalfX), FMath::FRandRange(-VilaWeatherImpl::EmitHalfZ, VilaWeatherImpl::EmitHalfZ));
	// spread = 3°
	const float Spread = FMath::Tan(FMath::DegreesToRadians(3.f));
	const FVector Dir = (VilaWeatherImpl::RainDir() + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * Spread).GetSafeNormal();
	D.Vel = Dir * FMath::FRandRange(VilaWeatherImpl::SpeedMin, VilaWeatherImpl::SpeedMax);
	// Ao começar a chover, as gotas nascem com vidas diferentes (o emissor contínuo do Godot).
	D.Life = bStagger ? FMath::FRandRange(0.05f, VilaWeatherImpl::DropLifetime) : VilaWeatherImpl::DropLifetime;
}

void UVilaWeather::UpdateDrops(float DeltaSeconds, const FVector& Emitter, bool bEmitting)
{
	if (!RainMesh || Drops.Num() == 0) return;
	if (!bEmitting && !bAnyDropAlive)
	{
		if (RainMesh->IsVisible()) RainMesh->SetVisibility(false);
		return;
	}
	if (!RainMesh->IsVisible()) RainMesh->SetVisibility(true);

	const bool bStart = !bAnyDropAlive;
	bAnyDropAlive = false;
	for (int32 I = 0; I < Drops.Num(); ++I)
	{
		FVilaRainDrop& D = Drops[I];
		if (D.Life > 0.f)
		{
			D.Vel.Z -= VilaWeatherImpl::Gravity * DeltaSeconds;
			D.Pos += D.Vel * DeltaSeconds;
			D.Life -= DeltaSeconds;
		}
		if (D.Life <= 0.f && bEmitting) SpawnDrop(D, Emitter, bStart);
		if (D.Life > 0.f)
		{
			bAnyDropAlive = true;
			DropTransforms[I] = FTransform(DropRot, D.Pos, VilaWeatherImpl::DropScale);
		}
		else
		{
			DropTransforms[I] = FTransform(FQuat::Identity, Emitter, FVector::ZeroVector);
		}
	}
	RainMesh->BatchUpdateInstancesTransforms(0, DropTransforms, true, true, true);
}
