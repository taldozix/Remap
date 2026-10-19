#include "VilaGameMode.h"

#include "Engine/World.h"
#include "VilaHUD.h"
#include "VilaPlayerController.h"
#include "VilaWorld.h"

AVilaGameMode::AVilaGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AVilaPlayerController::StaticClass();
	HUDClass = AVilaHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

void AVilaGameMode::StartPlay()
{
	FActorSpawnParameters Params;
	Params.Name = TEXT("VilaWorld");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VilaWorld = GetWorld()->SpawnActor<AVilaWorld>(AVilaWorld::StaticClass(), FTransform::Identity, Params);
	Super::StartPlay();
}
