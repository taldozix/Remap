// Gerenciador de áudio (port de src/audio/Audio.gd). Cada som tem um id; se existir o asset
// /Game/Vila/Audio/<id> ele é usado, senão o som é sintetizado proceduralmente (recursos
// 100% originais) e tocado num USoundWaveProcedural novo a cada reprodução.
//
// Volumes: o Godot usava barramentos (Master, Music, SFX, Voice, Ambient). Aqui cada som
// guarda o seu volume-base e o barramento; o VolumeMultiplier final = base * master * barramento.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SharedPointer.h"

#include "VilaAudio.generated.h"

class AActor;
class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundWaveProcedural;
class UWorld;

// Barramentos do Godot (Audio.BUSES).
enum class EVilaAudioBus : uint8
{
	Music,
	SFX,
	Voice,
	Ambient,
	Count
};

UCLASS()
class VILAAURORA_API UVilaAudio : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UVilaAudio* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ------------------------------------------------ API (Audio.gd)
	void Play(const FString& Id, float VolumeDb = 0.f, float Pitch = 1.f);
	// Som posicional (passos, portas, buzinas). Até 24 simultâneos, como o pool 3D do Godot.
	void PlayAt(const FString& Id, const FVector& Location, float VolumeDb = 0.f, float Pitch = 1.f);
	void PlayMusic(const FString& Id);
	void StopMusic();
	void SetAmbient(const FString& Id, bool bOn, float VolumeDb = -8.f);
	void StopAllAmbient();
	// vila::settings() audio.master/music/sfx/voice/ambient (0..1).
	void ApplyVolumes();
	// Motor contínuo de um veículo (tom segue a rotação). O componente volta parado
	// (bAutoActivate = false), preso à raiz do dono; o veículo chama Play/Stop,
	// SetPitchMultiplier e SetVolumeMultiplier. O volume do barramento SFX vai no próprio
	// som (USoundWave::Volume), então o veículo pode mexer no VolumeMultiplier à vontade.
	UAudioComponent* CreateEngineLoop(AActor* Owner);

	// ------------------------------------------------ extras
	// Play() com barramento explícito (o `bus` opcional do Godot).
	void PlayOnBus(const FString& Id, EVilaAudioBus Bus, float VolumeDb = 0.f, float Pitch = 1.f);
	// Som tocável do id: o asset importado (compartilhado) ou uma onda procedural nova.
	USoundBase* MakeSound(const FString& Id);
	// master * barramento (0..1).
	float BusGain(EVilaAudioBus Bus) const;
	static float DbToLinear(float Db);

private:
	// PCM 16 bits mono a MIX_RATE, imutável depois de criado (lido também na thread de áudio).
	struct FPcm
	{
		TArray<uint8> Bytes;
		int32 NumSamples = 0;
		bool bLoop = false;
	};
	using FPcmPtr = TSharedPtr<const FPcm, ESPMode::ThreadSafe>;

	struct FActive
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		EVilaAudioBus Bus = EVilaAudioBus::SFX;
		float BaseLinear = 1.f;
		double EndTime = 0.0;  // relógio da plataforma; 0 = deixa o som terminar sozinho
		bool b3D = false;
	};

	struct FEngineLoop
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		TWeakObjectPtr<USoundBase> Sound;  // onda procedural própria (volume do barramento)
	};

	bool Tick(float DeltaTime);
	UWorld* GetGameWorld() const;
	bool CanPlay() const;
	USoundBase* FindAsset(const FString& Id);
	FPcmPtr GetPcm(const FString& Id);
	USoundWaveProcedural* MakeProcedural(const FPcmPtr& Pcm) const;
	double SoundSeconds(const FString& Id, USoundBase* Sound);
	int32 CountActive(bool b3D) const;
	UAudioComponent* Create2D(USoundBase* Sound, float Volume, float Pitch, bool bAutoDestroy);
	void PruneActive();

	UPROPERTY() TMap<FString, TObjectPtr<USoundBase>> Assets;
	TSet<FString> MissingAssets;
	TMap<FString, FPcmPtr> PcmCache;

	UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation3D;
	UPROPERTY() TObjectPtr<USoundAttenuation> EngineAttenuation;

	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	FString MusicId;

	UPROPERTY() TMap<FString, TObjectPtr<UAudioComponent>> Ambient;
	TMap<FString, float> AmbientDb;
	TSet<FString> AmbientOn;

	TArray<FActive> Active;
	TArray<FEngineLoop> EngineLoops;

	float Master = 0.8f;
	float Gains[static_cast<int32>(EVilaAudioBus::Count)] = {0.6f, 0.8f, 0.8f, 0.7f};

	FTSTicker::FDelegateHandle TickHandle;
};
