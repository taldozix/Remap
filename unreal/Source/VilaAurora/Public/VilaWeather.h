// Clima (parte do DayNight.gd): máquina de estados limpo/nublado/chuva em
// vila::game().world_state.weather {"state", "until"}, que muda a cada hora cheia com mais
// chuva à tarde; cobertura de nuvens, quantidade de chuva, piso molhado e a chuva visível.
// A chuva é um port das partículas do Godot sem Niagara: gotas (cubos finos) num
// UInstancedStaticMeshComponent que segue a câmera, mais o som ambiente "rain".
// Componente do AVilaEnvironment, que chama Update() no seu Tick.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Vila/Core.h"

#include "VilaWeather.generated.h"

class UInstancedStaticMeshComponent;

// Uma gota da chuva (partícula do Godot).
struct FVilaRainDrop
{
	FVector Pos = FVector::ZeroVector;  // cm, mundo
	FVector Vel = FVector::ZeroVector;  // cm/s
	float Life = 0.f;                   // s restantes; <= 0: morta
};

UCLASS()
class VILAAURORA_API UVilaWeather : public UActorComponent
{
	GENERATED_BODY()

public:
	UVilaWeather();

	// Cria a chuva visual e liga o relógio (hour_changed).
	void Setup();
	// Chamado pelo ambiente a cada quadro. `Emitter`: ponto acima do qual a chuva cai (cm).
	void Update(float DeltaSeconds, const FVector& Emitter);
	void ApplySettings();
	// Menu principal: céu limpo fixo e nenhuma mudança de clima.
	void SetMenuMode(bool bInMenu);

	// "limpo", "nublado" ou "chuva" (no menu, sempre "limpo").
	FString State() const;
	float Cloud() const { return CloudAmount; }        // 0 limpo, 0,45 nublado, 0,8 chuva
	float RainAmount() const { return Rain; }          // 0..1 (suavizado)
	float Wetness() const { return Wet; }              // 0..1 (piso molhado)
	bool IsRainVisible() const { return Rain > 0.2f; }

	// Clima muda a cada poucas horas, com regras simples (mais chuva à tarde).
	void OnHour(int32 Hour);
	// Força um estado (depuração/eventos): dura `Minutes` minutos de jogo.
	void ForceState(const FString& NewState, double Minutes);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	float TargetCloud() const;
	void ResizeDrops(int32 Count);
	void SpawnDrop(FVilaRainDrop& D, const FVector& Emitter, bool bStagger);
	void UpdateDrops(float DeltaSeconds, const FVector& Emitter, bool bEmitting);

	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RainMesh;

	TArray<FVilaRainDrop> Drops;
	TArray<FTransform> DropTransforms;
	FQuat DropRot = FQuat::Identity;
	int32 DropCount = 1000;
	bool bAnyDropAlive = false;

	vila::Rng Rng;
	float CloudAmount = 0.f;
	float Rain = 0.f;
	float Wet = 0.f;
	bool bMenu = false;
	bool bFirstUpdate = true;
	int32 HourConn = 0;
};
