#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "VilaGameMode.generated.h"

class AVilaWorld;

// O nível é vazio: o modo só cria o AVilaWorld, que monta a vila, o jogador, os NPCs e o
// trânsito a partir dos dados (como o GameWorld.tscn do Godot).
UCLASS()
class VILAAURORA_API AVilaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVilaGameMode();

	virtual void StartPlay() override;
	// O jogador é criado pelo AVilaWorld (posição/estado vêm do save); nada de pawn padrão.
	virtual void RestartPlayer(AController* NewPlayer) override {}

	AVilaWorld* GetVilaWorld() const { return VilaWorld; }

private:
	UPROPERTY() TObjectPtr<AVilaWorld> VilaWorld;
};
