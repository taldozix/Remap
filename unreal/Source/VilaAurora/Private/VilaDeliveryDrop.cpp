#include "VilaDeliveryDrop.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "VilaAssets.h"
#include "VilaAudio.h"
#include "VilaConv.h"
#include "VilaNPC.h"
#include "VilaWorld.h"
#include "Vila/Clock.h"
#include "Vila/Events.h"
#include "Vila/Game.h"

using namespace VilaConv;

AVilaDeliveryDrop::AVilaDeliveryDrop()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AVilaDeliveryDrop::Setup(AVilaWorld* World, const FString& Label)
{
	VilaWorld = World;
	Title = VilaW::Trf("Entrega: %s", U(Label));
	UVilaAssets* A = UVilaAssets::Get(this);
	if (!A) return;
	// disco luminoso no chão (raio 0,6 m, 4 cm de altura, a 14 cm)
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(A->Cylinder());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(false);
	C->SetupAttachment(Root);
	C->SetRelativeLocation(FVector(0.0, 0.0, 14.0));
	C->SetRelativeScale3D(FVector(1.2, 1.2, 0.04));
	C->SetMaterial(0, A->Emissive(VilaW::SRGB(0.5f, 0.8f, 1.f), 1.2f));
	C->RegisterComponent();
	AddInstanceComponent(C);
}

TArray<FVilaAction> AVilaDeliveryDrop::GetActions(AVilaPlayerCharacter* /*Player*/)
{
	const bool bHas = vila::game().player.inventory.has("sacola_entrega");
	return {FVilaAction(TEXT("deliver"), Tr("Deixar a entrega"), bHas, Tr("Você está sem a sacola"))};
}

void AVilaDeliveryDrop::DoAction(AVilaPlayerCharacter* /*Player*/, const FString& /*ActionId*/)
{
	const vila::Json R = vila::game().delivery.complete(vila::clock().total_minutes, vila::game().player);
	if (!vila::jbool(R, "ok"))
	{
		vila::events().notify.emit(vila::jstr(R, "reason"), "bad");
		return;
	}
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(TEXT("cash"));
	const bool bOnTime = vila::jbool(R, "on_time");
	vila::events().notify.emit(vila::fmt(vila::tr("Entrega concluída: R$ %d%s"), {vila::jint(R, "pay"), bOnTime ? vila::tr(" (no prazo!)") : vila::tr(" (atrasada)")}),
		bOnTime ? "good" : "info");
	AVilaWorld* W = VilaWorld;
	const FString Rec = S(vila::jstr(R, "recipient"));
	if (W && !Rec.IsEmpty())
		if (AVilaNPC* N = W->NPCs.FindRef(Rec))
			if (!N->IsHidden()) N->Bark(bOnTime ? Tr("Obrigado(a)! Chegou certinho.") : Tr("Demorou, hein..."));
	// Remove este ponto (Destroy) e cria o próximo, se houver.
	if (W) W->RefreshDelivery();
}

void AVilaDeliveryDrop::SetFocused(bool bFocused)
{
	VilaW::SetActorOutline(this, bFocused);
}
