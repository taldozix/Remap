#include "VilaCarVisual.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "VilaAssets.h"
#include "VilaAurora.h"
#include "VilaConv.h"

namespace
{
	// Peças exportadas por modelo (tools/art/blender/export_unreal.py).
	const TCHAR* const CVPartNames[] = {
		TEXT("Body"), TEXT("Details"), TEXT("Door_FL"), TEXT("Handles"), TEXT("Interior"), TEXT("Lights"),
		TEXT("Mirror_L"), TEXT("Mirror_R"), TEXT("Wheel_FL"), TEXT("Wheel_FR"), TEXT("Wheel_RL"), TEXT("Wheel_RR"), TEXT("Wipers"),
	};

	// CarVisual.MODEL_LENGTH / WHEEL_RADIUS (m): usados se o layout não trouxer os valores.
	float CVModelLengthM(const FString& Kind)
	{
		if (Kind == TEXT("hatch")) return 3.9f;
		if (Kind == TEXT("pickup")) return 4.9f;
		if (Kind == TEXT("van")) return 4.8f;
		return 4.4f;
	}
	float CVModelWheelRadiusM(const FString& Kind)
	{
		if (Kind == TEXT("hatch")) return 0.3f;
		if (Kind == TEXT("pickup")) return 0.37f;
		if (Kind == TEXT("van")) return 0.33f;
		return 0.31f;
	}

	// Cor do Godot (floats em sRGB) -> linear.
	FLinearColor CVSRGB(float R, float G, float B)
	{
		auto Q = [](float V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255)); };
		return FLinearColor::FromSRGBColor(FColor(Q(R), Q(G), Q(B)));
	}

	const FLinearColor& CVHeadColor() { static const FLinearColor C = CVSRGB(1.f, 0.95f, 0.82f); return C; }
	const FLinearColor& CVBrakeColor() { static const FLinearColor C = CVSRGB(1.f, 0.08f, 0.04f); return C; }
	const FLinearColor& CVIndicatorColor() { static const FLinearColor C = CVSRGB(1.f, 0.5f, 0.05f); return C; }

	constexpr float CVDoorOpenDeg = 65.f;     // Godot: rotation.y = -65° (abre para fora, lado esquerdo)
	constexpr float CVDoorTime = 0.25f;
	constexpr float CVBlinkPeriod = 0.7f;
	constexpr float CVIndicatorIdle = 0.15f;  // emission_energy do material "indicator" no Godot
	constexpr float CVIndicatorOn = 3.f;
	constexpr int32 CVPuffCount = 8;
	constexpr float CVPuffLife = 1.8f;

	FVector CVJsonVec3(const vila::Json& A, const FVector& Def)
	{
		if (!A.is_array() || A.size() < 3) return Def;
		return FVector(vila::to_num(A[0]), vila::to_num(A[1]), vila::to_num(A[2]));
	}

	// Proporções do carro procedural por tipo: altura da carroceria (fração de H),
	// comprimento da cabine (fração de L) e deslocamento da cabine (fração de L, + = frente).
	struct FCVProcShape
	{
		float Lower;
		float CabinLen;
		float CabinX;
	};
	FCVProcShape CVProcShape(const FString& Kind)
	{
		if (Kind == TEXT("hatch")) return {0.5f, 0.55f, -0.1f};
		if (Kind == TEXT("pickup")) return {0.52f, 0.33f, 0.1f};
		if (Kind == TEXT("van")) return {0.5f, 0.82f, -0.06f};
		return {0.48f, 0.45f, -0.03f};
	}
}

UVilaCarVisual::UVilaCarVisual()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UVilaCarVisual::Clear()
{
	for (USceneComponent* C : Created)
		if (IsValid(C)) C->DestroyComponent();
	Created.Reset();
	Meshes.Reset();
	WheelSpin.Reset();
	FrontSteer.Reset();
	Puffs.Reset();
	PuffAge.Reset();
	PuffVel.Reset();
	PuffSize.Reset();
	Model = nullptr;
	DoorPivot = nullptr;
	PaintMID = HeadMID = TailMID = IndicatorLMID = IndicatorRMID = nullptr;
	HeadCache = TailCache = IndLCache = IndRCache = -1.f;
	SpinRad = 0.f;
	DoorAngle = DoorTarget = 0.f;
	bSmoke = false;
}

USceneComponent* UVilaCarVisual::AddPivot(USceneComponent* Parent, const FTransform& T)
{
	AActor* Owner = GetOwner();
	USceneComponent* P = NewObject<USceneComponent>(Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(this));
	P->SetupAttachment(Parent);
	P->SetRelativeTransform(T);
	P->SetMobility(EComponentMobility::Movable);
	P->RegisterComponent();
	Created.Add(P);
	return P;
}

UStaticMeshComponent* UVilaCarVisual::AddMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& T, UMaterialInterface* Mat)
{
	AActor* Owner = GetOwner();
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(this));
	C->SetupAttachment(Parent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	C->SetRelativeTransform(T);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(true);
	if (Mat) C->SetMaterial(0, Mat);
	C->RegisterComponent();
	Created.Add(C);
	Meshes.Add(C);
	return C;
}

UStaticMeshComponent* UVilaCarVisual::AddBox(USceneComponent* Parent, const FVector& Center, const FVector& SizeCm, UMaterialInterface* Mat)
{
	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (!Assets || !Assets->Cube()) return nullptr;
	return AddMesh(Parent, Assets->Cube(), FTransform(FRotator::ZeroRotator, Center, SizeCm / 100.f), Mat);
}

void UVilaCarVisual::Build(const FString& InKind, float WidthM, float HeightM, float LengthM, const FLinearColor& InPaint)
{
	Clear();
	Kind = InKind;
	if (Kind != TEXT("hatch") && Kind != TEXT("sedan") && Kind != TEXT("pickup") && Kind != TEXT("van")) Kind = TEXT("sedan");
	Paint = InPaint;
	WidthCm = WidthM * 100.f;
	HeightCm = HeightM * 100.f;
	LengthCm = LengthM * 100.f;

	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (!Assets)
	{
		UE_LOG(LogVila, Warning, TEXT("UVilaCarVisual: UVilaAssets indisponível."));
		return;
	}
	Model = AddPivot(this, FTransform::Identity);
	bImported = BuildImported(Assets);
	if (!bImported) BuildProcedural(Assets);
	BuildSmoke(Assets);
	SetLights(false, false);
	ApplyIndicators(false);
}

bool UVilaCarVisual::BuildImported(UVilaAssets* Assets)
{
	UStaticMesh* Body = Assets->VehiclePart(Kind, TEXT("Body"));
	if (!Body) return false;

	const vila::Json& Layout = vila::jobj(Assets->VehicleLayout(), VilaConv::U(Kind));
	const vila::Json& PartsJ = vila::jobj(Layout, "parts");
	const float LayoutLen = static_cast<float>(vila::jnum(Layout, "length", CVModelLengthM(Kind) * 100.0));
	// Ajusta o modelo ao comprimento dos dados (como model.scale no Godot).
	const float S = LayoutLen > 1.f ? LengthCm / LayoutLen : 1.f;
	Model->SetRelativeScale3D(FVector(S));
	WheelRadius = static_cast<float>(vila::jnum(Layout, "wheel_radius", CVModelWheelRadiusM(Kind) * 100.0)) * S;
	bRealEmissive = true;

	for (const TCHAR* Part : CVPartNames)
	{
		const FString P(Part);
		UStaticMesh* Mesh = P == TEXT("Body") ? Body : Assets->VehiclePart(Kind, P);
		if (!Mesh) continue;
		const vila::Json& PJ = vila::jobj(PartsJ, VilaConv::U(P));
		const FVector Loc = CVJsonVec3(vila::jget(PJ, "location"), FVector::ZeroVector);
		const FVector R = CVJsonVec3(vila::jget(PJ, "rotation"), FVector::ZeroVector);
		const FRotator Rot(R.X, R.Y, R.Z);
		UStaticMeshComponent* C = nullptr;
		if (P.StartsWith(TEXT("Wheel_")))
		{
			// esterço (yaw) -> giro (eixo Y do carro) -> malha com a rotação do layout
			USceneComponent* Steer = AddPivot(Model, FTransform(Loc));
			USceneComponent* Spin = AddPivot(Steer, FTransform::Identity);
			C = AddMesh(Spin, Mesh, FTransform(Rot));
			WheelSpin.Add(Spin);
			if (P.StartsWith(TEXT("Wheel_F"))) FrontSteer.Add(Steer);
		}
		else if (P == TEXT("Door_FL"))
		{
			// pivô na dobradiça (location do layout)
			DoorPivot = AddPivot(Model, FTransform(Loc));
			C = AddMesh(DoorPivot, Mesh, FTransform(Rot));
		}
		else
		{
			C = AddMesh(Model, Mesh, FTransform(Rot, Loc));
		}
		if (C) AssignSlotMaterials(C);
	}
	return true;
}

void UVilaCarVisual::AssignSlotMaterials(UStaticMeshComponent* C)
{
	const TArray<FName> Slots = C->GetMaterialSlotNames();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FString N = Slots[i].ToString().ToLower();
		UMaterialInterface* Src = C->GetMaterial(i);
		if (!Src) continue;
		if (N.Contains(TEXT("paint")))
		{
			if (!PaintMID)
			{
				PaintMID = UMaterialInstanceDynamic::Create(Src, this);
				PaintMID->SetVectorParameterValue(TEXT("Color"), Paint);
			}
			C->SetMaterial(i, PaintMID);
		}
		else if (N.Contains(TEXT("headlight")))
		{
			if (!HeadMID) HeadMID = UMaterialInstanceDynamic::Create(Src, this);
			C->SetMaterial(i, HeadMID);
		}
		else if (N.Contains(TEXT("taillight")))
		{
			if (!TailMID) TailMID = UMaterialInstanceDynamic::Create(Src, this);
			C->SetMaterial(i, TailMID);
		}
		else if (N.Contains(TEXT("indicator")))
		{
			if (!IndicatorLMID)
			{
				IndicatorLMID = UMaterialInstanceDynamic::Create(Src, this);
				IndicatorRMID = IndicatorLMID;
			}
			C->SetMaterial(i, IndicatorLMID);
		}
	}
}

void UVilaCarVisual::BuildProcedural(UVilaAssets* Assets)
{
	UStaticMesh* Cyl = Assets->Cylinder();
	if (!Assets->Cube() || !Cyl) return;

	const float L = LengthCm, W = WidthCm, H = HeightCm;
	WheelRadius = CVModelWheelRadiusM(Kind) * 100.f * (L / (CVModelLengthM(Kind) * 100.f));
	const float R = WheelRadius;
	const FCVProcShape Sh = CVProcShape(Kind);

	bRealEmissive = Assets->Master(TEXT("M_Emissive")) != nullptr;
	PaintMID = Assets->MakeColorMID(TEXT("M_CarPaint"), Paint, this);
	HeadMID = Assets->MakeColorMID(TEXT("M_Emissive"), CVHeadColor(), this);
	TailMID = Assets->MakeColorMID(TEXT("M_Emissive"), CVBrakeColor(), this);
	IndicatorLMID = Assets->MakeColorMID(TEXT("M_Emissive"), CVIndicatorColor(), this);
	IndicatorRMID = Assets->MakeColorMID(TEXT("M_Emissive"), CVIndicatorColor(), this);
	UMaterialInterface* Glass = Assets->Glass(CVSRGB(0.07f, 0.09f, 0.11f), 0.62f);
	UMaterialInterface* Trim = Assets->Color(CVSRGB(0.03f, 0.03f, 0.035f), 0.55f);
	UMaterialInterface* Tire = Assets->Color(CVSRGB(0.035f, 0.035f, 0.038f), 0.92f);
	UMaterialInterface* Rim = Assets->Color(CVSRGB(0.7f, 0.71f, 0.73f), 0.25f, 1.f);

	// carroceria
	const float Bottom = R * 0.7f;
	const float LowerTop = H * Sh.Lower;
	AddBox(Model, FVector(0.f, 0.f, (Bottom + LowerTop) * 0.5f), FVector(L, W, LowerTop - Bottom), PaintMID);
	// cabine (vidros) + teto
	const float CabinLen = L * Sh.CabinLen;
	const float CabinX = L * Sh.CabinX;
	const float CabinTop = H * 0.97f;
	AddBox(Model, FVector(CabinX, 0.f, (LowerTop + CabinTop) * 0.5f), FVector(CabinLen, W * 0.86f, CabinTop - LowerTop), Glass);
	AddBox(Model, FVector(CabinX, 0.f, H - 3.f), FVector(CabinLen * 0.92f, W * 0.84f, 6.f), PaintMID);
	// caçamba da picape: laterais e tampa
	if (Kind == TEXT("pickup"))
	{
		const float BedFront = CabinX - CabinLen * 0.5f;
		const float BedLen = BedFront + L * 0.5f;
		const float BedX = -L * 0.5f + BedLen * 0.5f;
		for (const float Side : {-1.f, 1.f})
			AddBox(Model, FVector(BedX, Side * (W * 0.5f - 3.f), LowerTop + 12.f), FVector(BedLen, 6.f, 24.f), PaintMID);
		AddBox(Model, FVector(-L * 0.5f + 3.f, 0.f, LowerTop + 12.f), FVector(6.f, W, 24.f), PaintMID);
	}
	// para-choques
	AddBox(Model, FVector(L * 0.5f, 0.f, Bottom + 8.f), FVector(8.f, W * 0.98f, 16.f), Trim);
	AddBox(Model, FVector(-L * 0.5f, 0.f, Bottom + 8.f), FVector(8.f, W * 0.98f, 16.f), Trim);
	// faróis, lanternas e setas
	const float LightZ = LowerTop - 15.f;
	for (const float Side : {-1.f, 1.f})
	{
		AddBox(Model, FVector(L * 0.5f + 1.f, Side * W * 0.32f, LightZ), FVector(4.f, W * 0.2f, 10.f), HeadMID);
		AddBox(Model, FVector(-L * 0.5f - 1.f, Side * W * 0.32f, LightZ), FVector(4.f, W * 0.2f, 10.f), TailMID);
		UMaterialInterface* Ind = Side < 0.f ? IndicatorLMID.Get() : IndicatorRMID.Get();
		AddBox(Model, FVector(L * 0.5f + 1.f, Side * W * 0.45f, LightZ), FVector(4.f, 8.f, 6.f), Ind);
		AddBox(Model, FVector(-L * 0.5f - 1.f, Side * W * 0.45f, LightZ), FVector(4.f, 8.f, 6.f), Ind);
	}
	// rodas: pivô de esterço -> pivô de giro -> pneu (cilindro deitado) + barra do aro (mostra o giro)
	const float WheelX = L * 0.5f - L * 0.18f;
	const float TireW = 22.f;
	for (const float Fx : {1.f, -1.f})
		for (const float Side : {-1.f, 1.f})
		{
			USceneComponent* Steer = AddPivot(Model, FTransform(FVector(Fx * WheelX, Side * (W * 0.5f - 12.f), R)));
			USceneComponent* Spin = AddPivot(Steer, FTransform::Identity);
			AddMesh(Spin, Cyl, FTransform(FRotator(0.f, 0.f, 90.f), FVector::ZeroVector, FVector(2.f * R / 100.f, 2.f * R / 100.f, TireW / 100.f)), Tire);
			AddBox(Spin, FVector(0.f, Side * 2.f, 0.f), FVector(R * 1.2f, TireW + 2.f, 5.f), Rim);
			WheelSpin.Add(Spin);
			if (Fx > 0.f) FrontSteer.Add(Steer);
		}
	// porta do motorista (lado esquerdo = -Y), dobradiça na frente da cabine
	const float DoorLen = FMath::Min(110.f, CabinLen * 0.6f);
	DoorPivot = AddPivot(Model, FTransform(FVector(CabinX + CabinLen * 0.5f - 5.f, -W * 0.5f - 1.f, 0.f)));
	const float DoorH = (LowerTop - Bottom) * 0.9f;
	AddBox(DoorPivot, FVector(-DoorLen * 0.5f, -1.5f, Bottom + DoorH * 0.5f + 2.f), FVector(DoorLen, 3.f, DoorH), PaintMID);
}

void UVilaCarVisual::BuildSmoke(UVilaAssets* Assets)
{
	UStaticMesh* Sphere = Assets->Sphere();
	if (!Sphere) return;
	// Godot: smoke.position = (0, h*0.65, -l*0.38) -> frente do carro (motor)
	SmokeOrigin = FVector(LengthCm * 0.38f, 0.f, HeightCm * 0.65f);
	UMaterialInterface* Mat = Assets->Glass(CVSRGB(0.25f, 0.25f, 0.25f), 0.45f);
	for (int32 i = 0; i < CVPuffCount; ++i)
	{
		UStaticMeshComponent* P = AddMesh(this, Sphere, FTransform(SmokeOrigin), Mat);
		P->SetCastShadow(false);
		P->SetVisibility(false);
		Meshes.Remove(P);  // fumaça não recebe contorno
		Puffs.Add(P);
		PuffAge.Add(CVPuffLife);
		PuffVel.Add(FVector::ZeroVector);
		PuffSize.Add(0.5f);
	}
}

void UVilaCarVisual::Animate(float SpeedCm, float SteerRad, float DeltaTime)
{
	if (WheelRadius > 0.1f)
	{
		// rodando para frente (+X), o topo da roda avança: giro positivo em torno de +Y
		SpinRad = FMath::Fmod(SpinRad + SpeedCm * DeltaTime / WheelRadius, UE_TWO_PI);
		const FQuat Q(FVector::YAxisVector, SpinRad);
		for (USceneComponent* S : WheelSpin)
			if (S) S->SetRelativeRotation(Q);
	}
	// Godot: esterço positivo = esquerda; no UE yaw positivo = direita.
	const FRotator SteerRot(0.f, -FMath::RadiansToDegrees(SteerRad), 0.f);
	for (USceneComponent* S : FrontSteer)
		if (S) S->SetRelativeRotation(SteerRot);
}

void UVilaCarVisual::SetLightEnergy(UMaterialInstanceDynamic* MID, const FLinearColor& Base, float Energy, float& Cache)
{
	if (!MID || FMath::IsNearlyEqual(Cache, Energy)) return;
	Cache = Energy;
	MID->SetScalarParameterValue(TEXT("Intensity"), Energy);
	// Sem o mestre emissivo, só a cor indica a luz acesa.
	if (!bRealEmissive) MID->SetVectorParameterValue(TEXT("Color"), Base * FMath::Clamp(0.25f + Energy * 0.25f, 0.f, 1.f));
}

void UVilaCarVisual::SetLights(bool bHead, bool bBraking)
{
	SetLightEnergy(HeadMID, CVHeadColor(), bHead ? 3.f : 0.3f, HeadCache);
	SetLightEnergy(TailMID, CVBrakeColor(), bBraking ? 3.5f : (bHead ? 1.2f : 0.2f), TailCache);
}

void UVilaCarVisual::SetDoor(bool bOpen)
{
	DoorTarget = bOpen ? CVDoorOpenDeg : 0.f;
}

void UVilaCarVisual::SetIndicator(int32 Dir)
{
	Dir = FMath::Clamp(Dir, -1, 1);
	if (Dir == IndicatorDir) return;
	IndicatorDir = Dir;
	BlinkT = 0.f;
	ApplyIndicators(Dir != 0);
}

void UVilaCarVisual::ApplyIndicators(bool bLit)
{
	bIndicatorLit = bLit;
	const bool bLeft = bLit && (IndicatorDir < 0 || IndicatorLMID == IndicatorRMID);
	const bool bRight = bLit && (IndicatorDir > 0 || IndicatorLMID == IndicatorRMID);
	SetLightEnergy(IndicatorLMID, CVIndicatorColor(), bLeft ? CVIndicatorOn : CVIndicatorIdle, IndLCache);
	if (IndicatorRMID != IndicatorLMID) SetLightEnergy(IndicatorRMID, CVIndicatorColor(), bRight ? CVIndicatorOn : CVIndicatorIdle, IndRCache);
}

void UVilaCarVisual::SetSmoke(bool bOn)
{
	bSmoke = bOn;
}

void UVilaCarVisual::SetCustomDepth(bool bOn)
{
	for (UStaticMeshComponent* M : Meshes)
	{
		if (!M) continue;
		M->SetRenderCustomDepth(bOn);
		M->SetCustomDepthStencilValue(1);
	}
}

void UVilaCarVisual::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// porta (tween linear de 0,25 s)
	if (DoorPivot && !FMath::IsNearlyEqual(DoorAngle, DoorTarget))
	{
		const float Step = CVDoorOpenDeg / CVDoorTime * DeltaTime;
		DoorAngle = DoorTarget > DoorAngle ? FMath::Min(DoorTarget, DoorAngle + Step) : FMath::Max(DoorTarget, DoorAngle - Step);
		// lado esquerdo, dobradiça na frente: yaw positivo leva a traseira da porta para -Y (fora)
		DoorPivot->SetRelativeRotation(FRotator(0.f, DoorAngle, 0.f));
	}

	// setas
	if (IndicatorDir != 0)
	{
		BlinkT += DeltaTime;
		const bool bLit = FMath::Fmod(BlinkT, CVBlinkPeriod) < CVBlinkPeriod * 0.5f;
		if (bLit != bIndicatorLit) ApplyIndicators(bLit);
	}

	// fumaça (CPUParticles3D do Godot: 24 partículas, 1,8 s, sobe 0,6..1,2 m/s, gravidade +0,4)
	if (Puffs.Num() > 0)
	{
		if (bSmoke)
		{
			PuffSpawnT -= DeltaTime;
			if (PuffSpawnT <= 0.f)
			{
				PuffSpawnT = CVPuffLife / Puffs.Num();
				const int32 i = NextPuff;
				NextPuff = (NextPuff + 1) % Puffs.Num();
				PuffAge[i] = 0.f;
				const FVector Dir = FMath::VRandCone(FVector::UpVector, FMath::DegreesToRadians(25.f));
				PuffVel[i] = Dir * FMath::FRandRange(60.f, 120.f);
				PuffSize[i] = FMath::FRandRange(0.4f, 0.9f) * 0.5f;  // esfera de 0,5 m
				if (Puffs[i])
				{
					Puffs[i]->SetRelativeLocation(SmokeOrigin);
					Puffs[i]->SetVisibility(true);
				}
			}
		}
		for (int32 i = 0; i < Puffs.Num(); ++i)
		{
			UStaticMeshComponent* P = Puffs[i];
			if (!P || PuffAge[i] >= CVPuffLife) continue;
			PuffAge[i] += DeltaTime;
			if (PuffAge[i] >= CVPuffLife)
			{
				P->SetVisibility(false);
				continue;
			}
			PuffVel[i].Z += 40.f * DeltaTime;
			const float T = PuffAge[i] / CVPuffLife;
			P->SetRelativeLocation(P->GetRelativeLocation() + PuffVel[i] * DeltaTime);
			P->SetRelativeScale3D(FVector(PuffSize[i] * (0.6f + T) * (1.f - T * T * 0.6f)));
		}
	}
}
