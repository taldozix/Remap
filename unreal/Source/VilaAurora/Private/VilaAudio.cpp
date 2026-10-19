#include "VilaAudio.h"

#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Misc/PackageName.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundGroups.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/Package.h"
#include "VilaAurora.h"
#include "Vila/Settings.h"

namespace VilaAudioImpl
{
	// ------------------------------------------------------------------ síntese (Audio._synth)
	constexpr int32 MIX_RATE = 22050;
	constexpr float TAU_F = 6.28318530718f;

	using FSamples = TArray<float>;

	int32 N(float Seconds) { return static_cast<int32>(Seconds * MIX_RATE); }

	FSamples Zeros(int32 Count)
	{
		FSamples Out;
		Out.SetNumZeroed(FMath::Max(0, Count));
		return Out;
	}

	struct FSynth
	{
		FRandomStream Rng;

		explicit FSynth(const FString& Id) : Rng(static_cast<int32>(GetTypeHash(Id))) {}

		float Rand() { return Rng.FRandRange(-1.f, 1.f); }

		FSamples Tone(float Sec, float Freq, float Amp, float Decay)
		{
			FSamples Out = Zeros(N(Sec));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				Out[I] = FMath::Sin(TAU_F * Freq * T) * Amp * FMath::Exp(-Decay * T) * FMath::Min(1.f, T * 200.f);
			}
			return Out;
		}

		FSamples Square(float Sec, float Freq, float Amp)
		{
			FSamples Out = Zeros(N(Sec));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				const float Env = FMath::Min(1.f, T * 80.f) * FMath::Min(1.f, (Sec - T) * 30.f);
				Out[I] = (FMath::Fmod(T * Freq, 1.f) < 0.5f ? 1.f : -1.f) * Amp * Env;
			}
			return Out;
		}

		FSamples Sweep(float Sec, float F0, float F1, float Amp)
		{
			FSamples Out = Zeros(N(Sec));
			float Phase = 0.f;
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				Phase += TAU_F * FMath::Lerp(F0, F1, T / Sec) / MIX_RATE;
				Out[I] = FMath::Sin(Phase) * Amp * (1.f - T / Sec);
			}
			return Out;
		}

		FSamples NoiseBurst(float Sec, float Amp, float Cutoff)
		{
			FSamples Out = Zeros(N(Sec));
			const float A = FMath::Clamp(TAU_F * Cutoff / MIX_RATE, 0.f, 1.f);
			float Y = 0.f;
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				Y += A * (Rand() - Y);
				Out[I] = Y * Amp * FMath::Square(1.f - T / Sec) * 2.f;
			}
			return Out;
		}

		static FSamples Mix(std::initializer_list<FSamples> Parts)
		{
			int32 Count = 0;
			for (const FSamples& P : Parts) Count = FMath::Max(Count, P.Num());
			FSamples Out = Zeros(Count);
			for (const FSamples& P : Parts)
				for (int32 I = 0; I < P.Num(); ++I) Out[I] += P[I];
			return Out;
		}

		static FSamples Delay(const FSamples& P, float Sec)
		{
			const int32 D = N(Sec);
			FSamples Out = Zeros(P.Num() + D);
			for (int32 I = 0; I < P.Num(); ++I) Out[I + D] = P[I];
			return Out;
		}

		FSamples Thump(float Sec, float Freq)
		{
			FSamples A = Tone(Sec, Freq, 0.6f, 14.f);
			FSamples B = NoiseBurst(Sec * 0.5f, 0.3f, 1500.f);
			return Mix({A, B});
		}

		FSamples Creak(float Sec)
		{
			FSamples Out = Zeros(N(Sec));
			float Phase = 0.f;
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				const float F = 260.f + 140.f * FMath::Sin(T * 9.f) + 80.f * T;
				Phase += TAU_F * F / MIX_RATE;
				const float Saw = FMath::Fmod(Phase / TAU_F, 1.f) * 2.f - 1.f;
				Out[I] = Saw * 0.12f * FMath::Sin(PI * T / Sec);
			}
			return Out;
		}

		FSamples Rattle(float Sec)
		{
			FSamples Out = Zeros(N(Sec));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				const float Pulse = FMath::Fmod(T, 0.06f) < 0.012f ? 1.f : 0.f;
				Out[I] = Rand() * 0.35f * Pulse;
			}
			return Out;
		}

		FSamples Crunch(float Sec)
		{
			FSamples Out = Zeros(N(Sec));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				const float Gate = FMath::Fmod(T, 0.16f) < 0.05f ? 1.f : 0.f;
				Out[I] = Rand() * 0.25f * Gate;
			}
			return Out;
		}

		FSamples Gulp(float Sec)
		{
			FSamples Out = Zeros(N(Sec));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				const float Local = FMath::Fmod(T, 0.18f);
				Out[I] = FMath::Sin(TAU_F * (180.f + 400.f * Local) * T) * 0.3f * FMath::Exp(-Local * 25.f);
			}
			return Out;
		}

		FSamples Engine(float Sec)
		{
			static const int32 Harmonics[] = {1, 2, 3, 4, 6};
			FSamples Out = Zeros(N(Sec));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				float S = 0.f;
				for (const int32 H : Harmonics) S += FMath::Sin(TAU_F * 60.f * H * T) / static_cast<float>(H);
				const float Putt = 0.6f + 0.4f * FMath::Sin(TAU_F * 30.f * T);
				Out[I] = S * 0.18f * Putt;
			}
			return Out;
		}

		// Ruído em laço com emenda suave (crossfade de 0,1 s entre o fim e o começo).
		FSamples LoopNoise(float Sec, float Cutoff, float Amp, bool bHighpass)
		{
			const int32 Count = N(Sec);
			const int32 Fade = N(0.1f);
			FSamples Raw = Zeros(Count + Fade);
			const float A = FMath::Clamp(TAU_F * Cutoff / MIX_RATE, 0.f, 1.f);
			float Y = 0.f;
			for (int32 I = 0; I < Raw.Num(); ++I)
			{
				const float X = Rand();
				Y += A * (X - Y);
				Raw[I] = bHighpass ? (X - Y) : Y * 3.f;
			}
			FSamples Out = Zeros(Count);
			for (int32 I = 0; I < Count; ++I) Out[I] = Raw[I] * Amp;
			for (int32 I = 0; I < Fade && I < Count; ++I)
			{
				const float K = static_cast<float>(I) / Fade;
				Out[I] = (Raw[I] * K + Raw[Count + I] * (1.f - K)) * Amp;
			}
			return Out;
		}

		// Progressão original Lá menor – Fá – Dó – Sol, timbre suave (4 s por acorde).
		FSamples PadMusic()
		{
			static const float Chords[4][3] = {
				{220.f, 261.63f, 329.63f}, {174.61f, 220.f, 261.63f}, {261.63f, 329.63f, 392.f}, {196.f, 246.94f, 293.66f}};
			const float Per = 4.f;
			FSamples Out = Zeros(N(Per * 4));
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const float T = static_cast<float>(I) / MIX_RATE;
				const int32 Ci = FMath::Clamp(static_cast<int32>(T / Per), 0, 3);
				const float Lt = T - Ci * Per;
				const float Env = FMath::Min(1.f, Lt / 1.2f) * FMath::Min(1.f, (Per - Lt) / 1.f);
				float S = 0.f;
				for (const float F : Chords[Ci]) S += FMath::Sin(TAU_F * F * T) * 0.5f + FMath::Sin(TAU_F * F * 2.f * T) * 0.12f;
				const float Bass = FMath::Sin(TAU_F * Chords[Ci][0] * 0.5f * T) * 0.35f;
				const float ArpF = Chords[Ci][static_cast<int32>(Lt * 2.f) % 3] * 2.f;
				const float Arp = FMath::Sin(TAU_F * ArpF * T) * 0.18f * FMath::Exp(-FMath::Fmod(Lt, 0.5f) * 6.f);
				Out[I] = (S * 0.09f + Bass * 0.25f + Arp) * Env * 0.8f;
			}
			return Out;
		}

		// Receita de cada id (mesma tabela do Godot). bLoop: som contínuo.
		FSamples Make(const FString& Id, bool& bLoop)
		{
			bLoop = false;
			if (Id == TEXT("step")) return NoiseBurst(0.07f, 0.35f, 2200.f);
			if (Id == TEXT("door_open")) return Creak(0.45f);
			if (Id == TEXT("door_close")) return Thump(0.25f, 85.f);
			if (Id == TEXT("door_locked")) return Rattle(0.25f);
			if (Id == TEXT("pickup")) return Sweep(0.12f, 700.f, 1300.f, 0.35f);
			if (Id == TEXT("drop")) return Thump(0.15f, 140.f);
			if (Id == TEXT("cash")) return Mix({Tone(0.5f, 1568.f, 0.25f, 6.f), Delay(Tone(0.5f, 2093.f, 0.22f, 6.f), 0.09f)});
			if (Id == TEXT("notify")) return Mix({Tone(0.45f, 660.f, 0.2f, 5.f), Delay(Tone(0.4f, 990.f, 0.18f, 6.f), 0.08f)});
			if (Id == TEXT("bad")) return Square(0.28f, 196.f, 0.18f);
			if (Id == TEXT("order")) return Mix({Tone(0.15f, 1046.f, 0.3f, 10.f), Delay(Tone(0.15f, 1046.f, 0.3f, 10.f), 0.2f)});
			if (Id == TEXT("crash")) return NoiseBurst(0.7f, 0.9f, 900.f);
			if (Id == TEXT("horn")) return Mix({Square(0.5f, 392.f, 0.16f), Square(0.5f, 494.f, 0.14f)});
			if (Id == TEXT("ui_click")) return NoiseBurst(0.025f, 0.25f, 4000.f);
			if (Id == TEXT("eat")) return Crunch(0.5f);
			if (Id == TEXT("drink")) return Gulp(0.5f);
			bLoop = true;
			if (Id == TEXT("engine")) return Engine(0.5f);
			if (Id == TEXT("sizzle")) return LoopNoise(1.5f, 6000.f, 0.12f, true);
			if (Id == TEXT("city")) return LoopNoise(4.f, 350.f, 0.35f, false);
			if (Id == TEXT("rain")) return LoopNoise(2.f, 5000.f, 0.22f, true);
			if (Id == TEXT("music_menu")) return PadMusic();
			bLoop = false;
			return Tone(0.1f, 440.f, 0.2f, 10.f);
		}
	};

	float ReadVolume(const char* Key, float Default)
	{
		const float V = static_cast<float>(vila::to_num(vila::settings().get_value("audio", Key), Default));
		// Godot: barramento mudo abaixo de 0,001.
		return V <= 0.001f ? 0.f : FMath::Clamp(V, 0.f, 1.f);
	}
}

// ---------------------------------------------------------------- ciclo de vida
UVilaAudio* UVilaAudio::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UVilaAudio>() : nullptr;
}

void UVilaAudio::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Godot AudioStreamPlayer3D: max_distance 45 m, unit_size 6 m (queda inversa).
	Attenuation3D = NewObject<USoundAttenuation>(this, TEXT("VilaAttenuation3D"));
	{
		FSoundAttenuationSettings& A = Attenuation3D->Attenuation;
		A.bAttenuate = true;
		A.bSpatialize = true;
		A.AttenuationShape = EAttenuationShape::Sphere;
		A.AttenuationShapeExtents = FVector(600.f, 0.f, 0.f);
		A.FalloffDistance = 4500.f - 600.f;
		A.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		A.dBAttenuationAtMax = -18.f;  // 6/45 m na lei inversa do Godot ≈ -17,5 dB
		A.FalloffMode = ENaturalSoundFalloffMode::Silent;
	}
	// Motor do veículo: unit_size 5 m, sem distância máxima no Godot.
	EngineAttenuation = NewObject<USoundAttenuation>(this, TEXT("VilaAttenuationEngine"));
	{
		FSoundAttenuationSettings& A = EngineAttenuation->Attenuation;
		A.bAttenuate = true;
		A.bSpatialize = true;
		A.AttenuationShape = EAttenuationShape::Sphere;
		A.AttenuationShapeExtents = FVector(500.f, 0.f, 0.f);
		A.FalloffDistance = 6000.f;
		A.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		A.dBAttenuationAtMax = -24.f;
		A.FalloffMode = ENaturalSoundFalloffMode::Silent;
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UVilaAudio::Tick));
	ApplyVolumes();
}

void UVilaAudio::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	StopMusic();
	StopAllAmbient();
	Active.Reset();
	EngineLoops.Reset();
	Super::Deinitialize();
}

UWorld* UVilaAudio::GetGameWorld() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetWorld() : nullptr;
}

bool UVilaAudio::CanPlay() const
{
	// Equivale ao "headless" do Godot: sem dispositivo de áudio, nada toca.
	return GEngine && GEngine->UseSound() && GetGameWorld() != nullptr;
}

float UVilaAudio::DbToLinear(float Db)
{
	return FMath::Pow(10.f, Db / 20.f);
}

float UVilaAudio::BusGain(EVilaAudioBus Bus) const
{
	const int32 I = FMath::Clamp(static_cast<int32>(Bus), 0, static_cast<int32>(EVilaAudioBus::Count) - 1);
	return Master * Gains[I];
}

// ---------------------------------------------------------------- sons
USoundBase* UVilaAudio::FindAsset(const FString& Id)
{
	if (TObjectPtr<USoundBase>* Found = Assets.Find(Id)) return Found->Get();
	if (MissingAssets.Contains(Id)) return nullptr;
	const FString Package = TEXT("/Game/Vila/Audio/") + Id;
	USoundBase* Sound = FPackageName::DoesPackageExist(Package) ? LoadObject<USoundBase>(nullptr, *(Package + TEXT(".") + Id)) : nullptr;
	if (Sound) Assets.Add(Id, Sound);
	else MissingAssets.Add(Id);
	return Sound;
}

FVilaPcmPtr UVilaAudio::GetPcm(const FString& Id)
{
	if (const FVilaPcmPtr* Found = PcmCache.Find(Id)) return *Found;
	VilaAudioImpl::FSynth Synth(Id);
	bool bLoop = false;
	const VilaAudioImpl::FSamples Samples = Synth.Make(Id, bLoop);
	TSharedPtr<FVilaPcm, ESPMode::ThreadSafe> Pcm = MakeShared<FVilaPcm, ESPMode::ThreadSafe>();
	Pcm->bLoop = bLoop;
	Pcm->NumSamples = Samples.Num();
	Pcm->Bytes.SetNumUninitialized(Samples.Num() * sizeof(int16));
	int16* Out = reinterpret_cast<int16*>(Pcm->Bytes.GetData());
	for (int32 I = 0; I < Samples.Num(); ++I)
		Out[I] = static_cast<int16>(FMath::Clamp(Samples[I], -1.f, 1.f) * 32000.f);
	FVilaPcmPtr Result = Pcm;
	PcmCache.Add(Id, Result);
	return Result;
}

USoundWaveProcedural* UVilaAudio::MakeProcedural(const FVilaPcmPtr& Pcm, bool bOneShot) const
{
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	Wave->SetSampleRate(VilaAudioImpl::MIX_RATE);
	Wave->NumChannels = 1;
	Wave->SoundGroup = SOUNDGROUP_Default;
	const bool bLoop = Pcm->bLoop && !bOneShot;
	Wave->bLooping = bLoop;
	Wave->Duration = bLoop ? INDEFINITELY_LOOPING_DURATION : static_cast<float>(Pcm->NumSamples) / VilaAudioImpl::MIX_RATE;
	if (bLoop)
	{
		// Laços não podem ser virtualizados (onda procedural): continuam mesmo inaudíveis.
		Wave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		// Roda na thread de áudio: só toca o buffer imutável e a onda recebida.
		FVilaPcmPtr Data = Pcm;
		Wave->OnSoundWaveProceduralUnderflow = FOnSoundWaveProceduralUnderflow::CreateLambda(
			[Data](USoundWaveProcedural* InWave, int32 SamplesRequired)
			{
				if (!InWave || Data->Bytes.Num() == 0) return;
				int32 Queued = 0;
				const int32 Needed = FMath::Max(1, SamplesRequired) * static_cast<int32>(sizeof(int16));
				while (Queued < Needed)
				{
					InWave->QueueAudio(Data->Bytes.GetData(), Data->Bytes.Num());
					Queued += Data->Bytes.Num();
				}
			});
	}
	// Um disparo: o som inteiro entra na fila de uma vez; o Tick para o componente no fim.
	if (Pcm->Bytes.Num() > 0) Wave->QueueAudio(Pcm->Bytes.GetData(), Pcm->Bytes.Num());
	return Wave;
}

USoundBase* UVilaAudio::MakeSound(const FString& Id, bool bOneShot)
{
	if (USoundBase* Asset = FindAsset(Id)) return Asset;
	return MakeProcedural(GetPcm(Id), bOneShot);
}

double UVilaAudio::SoundSeconds(const FString& Id, USoundBase* Sound)
{
	if (Cast<USoundWaveProcedural>(Sound))
	{
		// Play/PlayAt tocam uma volta só, mesmo de um som contínuo (no Godot ele ficava em laço).
		const FVilaPcmPtr Pcm = GetPcm(Id);
		return static_cast<double>(Pcm->NumSamples) / VilaAudioImpl::MIX_RATE;
	}
	return 0.0;  // asset: termina sozinho
}

int32 UVilaAudio::CountActive(bool b3D) const
{
	int32 Count = 0;
	for (const FVilaActiveSound& A : Active)
		if (A.b3D == b3D && A.Comp.IsValid() && A.Comp->IsPlaying()) ++Count;
	return Count;
}

UAudioComponent* UVilaAudio::Create2D(USoundBase* Sound, float Volume, float Pitch, bool bAutoDestroy)
{
	UWorld* World = GetGameWorld();
	if (!World || !Sound) return nullptr;
	UAudioComponent* Comp = UGameplayStatics::CreateSound2D(World, Sound, Volume, Pitch, 0.f, nullptr, false, bAutoDestroy);
	if (Comp)
	{
		// Como o autoload do Godot (PROCESS_MODE_ALWAYS): sons 2D seguem tocando com o jogo pausado.
		Comp->bIsUISound = true;
		Comp->bAllowSpatialization = false;
	}
	return Comp;
}

// ---------------------------------------------------------------- API
void UVilaAudio::Play(const FString& Id, float VolumeDb, float Pitch)
{
	PlayOnBus(Id, EVilaAudioBus::SFX, VolumeDb, Pitch);
}

void UVilaAudio::PlayOnBus(const FString& Id, EVilaAudioBus Bus, float VolumeDb, float Pitch)
{
	if (!CanPlay()) return;
	if (CountActive(false) >= 10) return;  // pool 2D do Godot: 10 players
	USoundBase* Sound = MakeSound(Id, true);
	const float Base = DbToLinear(VolumeDb);
	UAudioComponent* Comp = Create2D(Sound, Base * BusGain(Bus), Pitch, true);
	if (!Comp) return;
	Comp->Play();
	const double Len = SoundSeconds(Id, Sound);
	FVilaActiveSound& A = Active.AddDefaulted_GetRef();
	A.Comp = Comp;
	A.Bus = Bus;
	A.BaseLinear = Base;
	A.EndTime = Len > 0.0 ? FPlatformTime::Seconds() + Len / FMath::Max(0.05f, Pitch) + 0.1 : 0.0;
	A.b3D = false;
}

void UVilaAudio::PlayAt(const FString& Id, const FVector& Location, float VolumeDb, float Pitch)
{
	if (!CanPlay()) return;
	UWorld* World = GetGameWorld();
	PruneActive();
	if (CountActive(true) >= 24) return;  // pool 3D do Godot: até 24 players
	USoundBase* Sound = MakeSound(Id, true);
	const float Base = DbToLinear(VolumeDb);
	UAudioComponent* Comp = UGameplayStatics::SpawnSoundAtLocation(
		World, Sound, Location, FRotator::ZeroRotator, Base * BusGain(EVilaAudioBus::SFX), Pitch, 0.f, Attenuation3D, nullptr, true);
	if (!Comp) return;
	const double Len = SoundSeconds(Id, Sound);
	FVilaActiveSound& A = Active.AddDefaulted_GetRef();
	A.Comp = Comp;
	A.Bus = EVilaAudioBus::SFX;
	A.BaseLinear = Base;
	A.EndTime = Len > 0.0 ? FPlatformTime::Seconds() + Len / FMath::Max(0.05f, Pitch) + 0.1 : 0.0;
	A.b3D = true;
}

void UVilaAudio::PlayMusic(const FString& Id)
{
	if (!CanPlay()) return;
	if (MusicId == Id && IsValid(Music) && Music->GetWorld() == GetGameWorld() && Music->IsPlaying()) return;
	StopMusic();
	Music = Create2D(MakeSound(Id), BusGain(EVilaAudioBus::Music), 1.f, false);
	MusicId = Id;
	if (Music) Music->Play();
}

void UVilaAudio::StopMusic()
{
	if (IsValid(Music))
	{
		Music->Stop();
		Music->DestroyComponent();
	}
	Music = nullptr;
	MusicId.Reset();
}

void UVilaAudio::SetAmbient(const FString& Id, bool bOn, float VolumeDb)
{
	if (!CanPlay()) return;
	TObjectPtr<UAudioComponent>* Found = Ambient.Find(Id);
	UAudioComponent* Comp = Found ? Found->Get() : nullptr;
	if (!bOn)
	{
		AmbientOn.Remove(Id);
		if (IsValid(Comp) && Comp->IsPlaying()) Comp->Stop();
		return;
	}
	// O componente pertence ao mundo atual: depois de uma troca de nível é recriado.
	if (!IsValid(Comp) || Comp->GetWorld() != GetGameWorld())
	{
		Comp = Create2D(MakeSound(Id), 1.f, 1.f, false);
		if (!Comp) return;
		Ambient.Add(Id, Comp);
	}
	AmbientOn.Add(Id);
	AmbientDb.Add(Id, VolumeDb);
	Comp->SetVolumeMultiplier(DbToLinear(VolumeDb) * BusGain(EVilaAudioBus::Ambient));
	if (!Comp->IsPlaying())
	{
		// Onda procedural nova a cada recomeço (a fila da anterior pode ter sido descartada).
		if (Cast<USoundWaveProcedural>(Comp->Sound)) Comp->SetSound(MakeSound(Id));
		Comp->Play();
	}
}

void UVilaAudio::StopAllAmbient()
{
	for (TPair<FString, TObjectPtr<UAudioComponent>>& P : Ambient)
		if (IsValid(P.Value) && P.Value->IsPlaying()) P.Value->Stop();
	AmbientOn.Reset();
}

void UVilaAudio::ApplyVolumes()
{
	Master = VilaAudioImpl::ReadVolume("master", 0.8f);
	Gains[static_cast<int32>(EVilaAudioBus::Music)] = VilaAudioImpl::ReadVolume("music", 0.6f);
	Gains[static_cast<int32>(EVilaAudioBus::SFX)] = VilaAudioImpl::ReadVolume("sfx", 0.8f);
	Gains[static_cast<int32>(EVilaAudioBus::Voice)] = VilaAudioImpl::ReadVolume("voice", 0.8f);
	Gains[static_cast<int32>(EVilaAudioBus::Ambient)] = VilaAudioImpl::ReadVolume("ambient", 0.7f);

	for (const FVilaActiveSound& A : Active)
		if (A.Comp.IsValid()) A.Comp->SetVolumeMultiplier(A.BaseLinear * BusGain(A.Bus));
	if (IsValid(Music)) Music->SetVolumeMultiplier(BusGain(EVilaAudioBus::Music));
	for (TPair<FString, TObjectPtr<UAudioComponent>>& P : Ambient)
		if (IsValid(P.Value))
		{
			const float* Db = AmbientDb.Find(P.Key);
			P.Value->SetVolumeMultiplier(DbToLinear(Db ? *Db : -8.f) * BusGain(EVilaAudioBus::Ambient));
		}
	for (const FVilaEngineLoop& E : EngineLoops)
		if (USoundWave* Wave = Cast<USoundWave>(E.Sound.Get()))
			if (Cast<USoundWaveProcedural>(Wave)) Wave->Volume = BusGain(EVilaAudioBus::SFX);
}

UAudioComponent* UVilaAudio::CreateEngineLoop(AActor* Owner)
{
	if (!Owner) return nullptr;
	USoundBase* Sound = MakeSound(TEXT("engine"));
	UAudioComponent* Comp = NewObject<UAudioComponent>(Owner, NAME_None);
	Comp->bAutoActivate = false;
	Comp->bAutoDestroy = false;
	Comp->bStopWhenOwnerDestroyed = true;
	Comp->bAllowSpatialization = true;
	Comp->AttenuationSettings = EngineAttenuation;
	Comp->SetSound(Sound);
	if (USoundWaveProcedural* Wave = Cast<USoundWaveProcedural>(Sound))
	{
		// Onda própria deste veículo: o volume do barramento vai nela.
		Wave->Volume = BusGain(EVilaAudioBus::SFX);
		Comp->SetVolumeMultiplier(DbToLinear(-10.f));  // Vehicle.gd: _engine.volume_db = -10
	}
	else
	{
		Comp->SetVolumeMultiplier(DbToLinear(-10.f) * BusGain(EVilaAudioBus::SFX));
	}
	if (USceneComponent* Root = Owner->GetRootComponent()) Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	FVilaEngineLoop& E = EngineLoops.AddDefaulted_GetRef();
	E.Comp = Comp;
	E.Sound = Sound;
	return Comp;
}

// ---------------------------------------------------------------- manutenção
void UVilaAudio::PruneActive()
{
	Active.RemoveAll([](const FVilaActiveSound& A) { return !A.Comp.IsValid() || (!A.Comp->IsPlaying() && A.EndTime == 0.0); });
	EngineLoops.RemoveAll([](const FVilaEngineLoop& E) { return !E.Comp.IsValid(); });
}

bool UVilaAudio::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	for (FVilaActiveSound& A : Active)
	{
		if (!A.Comp.IsValid()) continue;
		// Ondas procedurais de um disparo não terminam sozinhas: para no fim do som.
		if (A.EndTime > 0.0 && Now >= A.EndTime)
		{
			A.Comp->Stop();
			A.EndTime = 0.0;
		}
	}
	PruneActive();
	return true;
}
