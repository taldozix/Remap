#include "VilaCameraRig.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/SpringArmComponent.h"
#include "VilaPlayerCharacter.h"
#include "VilaVehicle.h"
#include "Vila/Settings.h"

namespace
{
	constexpr float VerticalFov = 34.f;  // Camera3D.fov do Godot (vertical)

	float LerpYaw(float From, float To, float Alpha)
	{
		return FRotator::NormalizeAxis(From + FMath::FindDeltaAngleDegrees(From, To) * Alpha);
	}
}

AVilaCameraRig::AVilaCameraRig()
{
	PrimaryActorTick.bCanEverTick = true;
	// Depois do jogador e dos veículos, para não atrasar um quadro.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(Root);
	Arm->bDoCollisionTest = false;  // como no Godot: a câmera atravessa; as paredes são cortadas
	Arm->bEnableCameraLag = false;
	Arm->bEnableCameraRotationLag = false;
	Arm->bUsePawnControlRotation = false;
	Arm->SetUsingAbsoluteRotation(true);
	Arm->TargetArmLength = Distance;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(57.f);
}

void AVilaCameraRig::BeginPlay()
{
	Super::BeginPlay();
	ReadSettings();
	TWeakObjectPtr<AVilaCameraRig> Weak(this);
	SettingsConn = vila::settings().changed.connect([Weak](const std::string& Section)
	{
		if (AVilaCameraRig* Self = Weak.Get())
			if (Section == "graphics" || Section == "game") Self->ReadSettings();
	});
	UpdateView(0.f);
}

void AVilaCameraRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	vila::settings().changed.disconnect(SettingsConn);
	Super::EndPlay(EndPlayReason);
}

void AVilaCameraRig::ReadSettings()
{
	TiltShift = vila::to_int(vila::settings().get_value("graphics", "tilt_shift"), 1);
	Sensitivity = static_cast<float>(vila::to_num(vila::settings().get_value("game", "camera_sensitivity"), 1.0));
	ApplyLens();
}

UCameraComponent* AVilaCameraRig::GetCamera() const
{
	return Camera;
}

void AVilaCameraRig::RotateStep(int32 Dir)
{
	// Godot: cam_left soma 45° ao yaw (anti-horário) = yaw UE diminui.
	YawGoal = FRotator::NormalizeAxis(YawGoal - 45.f * static_cast<float>(Dir));
}

void AVilaCameraRig::ZoomStep(int32 Dir)
{
	DistGoal = FMath::Clamp(DistGoal - 200.f * Sensitivity * static_cast<float>(Dir), MinDist, MaxDist);
}

void AVilaCameraRig::SetMenuOrbit(const FVector& Center, float RadiusCm)
{
	bMenuOrbit = true;
	OrbitCenter = Center;
	OrbitRadius = FMath::Max(RadiusCm, 500.f);
	Target = nullptr;
	Distance = DistGoal = OrbitRadius;
	Pitch = -32.f;
	SetActorLocation(OrbitCenter);
	UpdateView(0.f);
}

AActor* AVilaCameraRig::ResolveFocus(FVector& OutFocus) const
{
	AActor* T = Target.Get();
	if (!IsValid(T)) return nullptr;
	// O jogador dirigindo: segue o veículo (com antecipação na direção do movimento).
	if (const AVilaPlayerCharacter* P = Cast<AVilaPlayerCharacter>(T))
		if (P->State == TEXT("driving") && IsValid(P->Vehicle.Get())) T = P->Vehicle.Get();

	if (const AVilaVehicle* V = Cast<AVilaVehicle>(T))
	{
		// Ponto no chão do veículo (a origem do Godot) + antecipação de 0,35 s.
		const float Ahead = FMath::Clamp(V->Speed / 100.f * 0.35f, -3.f, 6.f) * 100.f;
		OutFocus = V->GetGroundLocation() + FVector(0, 0, 100) + V->GetActorForwardVector().GetSafeNormal2D() * Ahead;
	}
	else if (const AVilaPlayerCharacter* P = Cast<AVilaPlayerCharacter>(T))
	{
		OutFocus = P->FeetLocation() + FVector(0, 0, 100);
	}
	else
	{
		OutFocus = T->GetActorLocation() + FVector(0, 0, 100);
	}
	return T;
}

void AVilaCameraRig::Snap()
{
	FVector Focus;
	if (bMenuOrbit || ResolveFocus(Focus))
	{
		if (!bMenuOrbit) SetActorLocation(Focus);
		Yaw = YawGoal;
		Distance = DistGoal;
		UpdateView(0.f);
	}
}

void AVilaCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateView(DeltaSeconds);
}

void AVilaCameraRig::UpdateView(float Dt)
{
	if (bMenuOrbit)
	{
		// Menu principal: volta lenta em torno da praça.
		Yaw = YawGoal = FRotator::NormalizeAxis(Yaw + Dt * 4.f);
		SetActorLocation(OrbitCenter);
	}
	else
	{
		FVector Focus;
		if (!ResolveFocus(Focus)) return;
		Yaw = LerpYaw(Yaw, YawGoal, FMath::Min(1.f, Dt * 6.f * Sensitivity));
		Distance = FMath::Lerp(Distance, DistGoal, FMath::Min(1.f, Dt * 6.f));
		const FVector Cur = GetActorLocation();
		SetActorLocation(FVector::Dist(Cur, Focus) < 3000.f ? FMath::Lerp(Cur, Focus, FMath::Min(1.f, Dt * 7.f)) : Focus);
	}
	Arm->TargetArmLength = Distance;
	Arm->SetWorldRotation(FRotator(Pitch, Yaw, 0.f));

	// Godot usa FOV vertical; o UE, horizontal: converte pelo aspecto da janela.
	float Aspect = 16.f / 9.f;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D ViewSize;
		GEngine->GameViewport->GetViewportSize(ViewSize);
		if (ViewSize.Y > 1.0) Aspect = static_cast<float>(ViewSize.X / ViewSize.Y);
	}
	const float HFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFov * 0.5f)) * Aspect));
	Camera->SetFieldOfView(FMath::Clamp(HFov, 20.f, 120.f));

	if (TiltShift > 0) Camera->PostProcessSettings.DepthOfFieldFocalDistance = Distance;
}

void AVilaCameraRig::ApplyLens()
{
	if (!Camera) return;
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	Camera->PostProcessBlendWeight = 1.f;
	const bool bOn = TiltShift > 0;
	PP.bOverride_DepthOfFieldFocalDistance = bOn;
	PP.bOverride_DepthOfFieldFstop = bOn;
	PP.bOverride_DepthOfFieldSensorWidth = bOn;
	if (!bOn) return;
	// Efeito miniatura: sensor grande e diafragma aberto deixam só o alvo nítido.
	// Sutil: ~±4 m de nitidez a 21 m; forte: ~±1 m.
	PP.DepthOfFieldFocalDistance = Distance;
	PP.DepthOfFieldFstop = TiltShift >= 2 ? 2.8f : 4.f;
	PP.DepthOfFieldSensorWidth = TiltShift >= 2 ? 150.f : 100.f;
}

FVector AVilaCameraRig::ViewDir() const
{
	FVector D = Camera ? Camera->GetForwardVector() : FRotator(0.f, Yaw, 0.f).Vector();
	D.Z = 0.0;
	return D.GetSafeNormal();
}

float AVilaCameraRig::MoveYaw() const
{
	return Yaw;
}
