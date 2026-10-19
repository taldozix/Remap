#include "VilaGroundItem.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "VilaAssets.h"
#include "VilaConv.h"
#include "VilaWorld.h"
#include "Vila/Data.h"

using namespace VilaConv;

AVilaGroundItem::AVilaGroundItem()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AVilaGroundItem::Setup(const vila::Json& InEntry, AVilaWorld* World)
{
	Entry = InEntry;
	VilaWorld = World;
	const std::string Id = vila::jstr(InEntry, "id");
	Title = S(vila::fmt("%s x%d", {vila::data().item_name(Id), vila::jint(InEntry, "qty")}));

	UVilaAssets* A = UVilaAssets::Get(this);
	if (!A) return;
	const std::string Cat = vila::jstr(vila::data().item(Id), "category");
	FLinearColor Col = Hex("#c9a76a");
	if (Cat == "comida") Col = Hex("#e0b050");
	else if (Cat == "bebida") Col = Hex("#5fa8d3");
	else if (Cat == "valor") Col = Hex("#d4af37");
	else if (Cat == "missao") Col = Hex("#e05050");
	// sacola/pacote de papel pardo com faixa colorida da categoria
	auto AddBox = [this, A](const FVector& Center, const FVector& SizeCm, UMaterialInterface* Mat) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(A->Cube());
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetupAttachment(Root);
		C->SetRelativeLocation(Center);
		C->SetRelativeScale3D(SizeCm / 100.0);
		C->SetMaterial(0, Mat);
		C->RegisterComponent();
		AddInstanceComponent(C);
	};
	AddBox(FVector(0.0, 0.0, 9.0), Size(0.3, 0.18, 0.25), A->SurfaceLocal(TEXT("cloth_canvas"), Hex("#c8a878"), 0.6f));
	AddBox(FVector(0.0, 0.0, 12.0), Size(0.31, 0.05, 0.26), A->SurfaceLocal(TEXT("metal_painted"), Col, 1.f));
}

TArray<FVilaAction> AVilaGroundItem::GetActions(AVilaPlayerCharacter* /*Player*/)
{
	return {FVilaAction(TEXT("take"), VilaW::Trf("Pegar %s", U(Title)))};
}

void AVilaGroundItem::DoAction(AVilaPlayerCharacter* /*Player*/, const FString& /*ActionId*/)
{
	if (VilaWorld) VilaWorld->PickGroundItem(this);
}

void AVilaGroundItem::SetFocused(bool bFocused)
{
	VilaW::SetActorOutline(this, bFocused);
}
