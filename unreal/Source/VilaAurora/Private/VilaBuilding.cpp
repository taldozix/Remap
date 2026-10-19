#include "VilaBuilding.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "VilaConv.h"

namespace
{
	float ToLin(float C) { return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f); }
	FLinearColor SRGB(float R, float G, float B) { return FLinearColor(ToLin(R), ToLin(G), ToLin(B), 1.f); }
	const TCHAR* const SideNames[4] = {TEXT("n"), TEXT("s"), TEXT("e"), TEXT("w")};
}

AVilaBuilding::AVilaBuilding()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

void AVilaBuilding::Setup(const vila::Json& InDef)
{
	Def = InDef;
	BuildingId = VilaConv::S(vila::jstr(Def, "id"));
	const vila::Json& C = vila::jget(Def, "center");
	const vila::Json& S = vila::jget(Def, "size");
	const double CX = C.is_array() && C.size() >= 2 ? vila::to_num(C[0]) : 0.0;
	const double CZ = C.is_array() && C.size() >= 2 ? vila::to_num(C[1]) : 0.0;
	const double W = S.is_array() && S.size() >= 2 ? vila::to_num(S[0]) : 1.0;
	const double D = S.is_array() && S.size() >= 2 ? vila::to_num(S[1]) : 1.0;
	Rect = FBox2D(FVector2D((CX - W * 0.5) * VilaConv::M, (CZ - D * 0.5) * VilaConv::M),
		FVector2D((CX + W * 0.5) * VilaConv::M, (CZ + D * 0.5) * VilaConv::M));
	HeightCm = static_cast<float>(vila::jnum(Def, "height", 3.0) * VilaConv::M);
#if WITH_EDITOR
	SetActorLabel(TEXT("Building_") + BuildingId);
#endif
}

USceneComponent* AVilaBuilding::MakeContainer(const FString& Name, bool bVisible)
{
	USceneComponent* C = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), FName(*Name)));
	C->SetMobility(EComponentMobility::Static);
	C->SetupAttachment(Root);
	C->RegisterComponent();
	AddInstanceComponent(C);
	if (!bVisible) C->SetVisibility(false, true);
	return C;
}

bool AVilaBuilding::Contains(const FVector& P, float MarginCm) const
{
	return P.X >= Rect.Min.X - MarginCm && P.X <= Rect.Max.X + MarginCm && P.Y >= Rect.Min.Y - MarginCm && P.Y <= Rect.Max.Y + MarginCm;
}

void AVilaBuilding::SetCut(bool bOn, const FVector& ViewDir)
{
	TArray<FString> Sides;
	if (bOn)
	{
		const FVector V = FVector(ViewDir.X, ViewDir.Y, 0.0).GetSafeNormal();
		for (const TCHAR* S : SideNames)
			if (FVector::DotProduct(VilaConv::SideNormal(VilaConv::U(S)), -V) > 0.15) Sides.Add(S);
	}
	if (bOn == bCut && Sides == CutSides) return;
	bCut = bOn;
	CutSides = Sides;
	for (const TPair<FString, TObjectPtr<USceneComponent>>& Pair : SideFull)
	{
		const bool bC = Sides.Contains(Pair.Key);
		if (Pair.Value) Pair.Value->SetVisibility(!bC, true);
		if (const TObjectPtr<USceneComponent>* Low = SideLow.Find(Pair.Key))
			if (*Low) (*Low)->SetVisibility(bC, true);
	}
	if (Roof) Roof->SetVisibility(!bOn, true);
}

void AVilaBuilding::SetNight(float Amount)
{
	if (FMath::Abs(Amount - Night) < 0.005f) return;
	Night = Amount;
	if (Lamp) Lamp->SetVisibility(Amount > 0.3f);
	if (!WindowMID) return;
	// Mat.windows(): vidro azulado de dia; emissão quente (1.0, 0.78, 0.45) x noite x 1,4.
	const FLinearColor Day = SRGB(0.42f, 0.55f, 0.66f);
	const FLinearColor Warm = SRGB(1.f, 0.78f, 0.45f);
	switch (WindowMode)
	{
	case 1:
		WindowMID->SetScalarParameterValue(TEXT("Intensity"), Amount * 1.4f);
		WindowMID->SetScalarParameterValue(TEXT("Night"), Amount);
		break;
	case 2:
		WindowMID->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Day, Warm, FMath::Clamp(Amount, 0.f, 1.f)));
		WindowMID->SetScalarParameterValue(TEXT("Intensity"), Amount * 1.4f);
		break;
	default:
		WindowMID->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Day, Warm * 1.4f, FMath::Clamp(Amount, 0.f, 1.f)));
		break;
	}
}
