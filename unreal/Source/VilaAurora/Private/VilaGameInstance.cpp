#include "VilaGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "VilaAssets.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaConv.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Game.h"
#include "Vila/Saves.h"
#include "Vila/Settings.h"

using namespace VilaConv;

UVilaGameInstance* UVilaGameInstance::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? Cast<UVilaGameInstance>(World->GetGameInstance()) : nullptr;
}

void UVilaGameInstance::Init()
{
	Super::Init();
	vila::warning_sink = [](const std::string& Msg) { UE_LOG(LogVila, Warning, TEXT("%s"), *S(Msg)); };

	const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Vila");
	vila::make_dirs(U(Saved / TEXT("Saves")));
	vila::saves().dir = U(Saved / TEXT("Saves"));
	vila::settings().path = U(Saved / TEXT("settings.json"));
	vila::settings().load();

	if (!vila::data().load(U(UVilaAssets::DataDir())))
		UE_LOG(LogVila, Error, TEXT("Falha ao carregar os dados de %s"), *UVilaAssets::DataDir());
	for (const std::string& E : vila::data().errors)
	{
		DataErrors.Add(S(E));
		UE_LOG(LogVila, Warning, TEXT("Dados: %s"), *S(E));
	}
	vila::game().init();
	bCoreReady = true;

	SettingsConn = vila::settings().changed.connect([this](const std::string& Section) { ApplySettings(Section); });
	ApplySettings("graphics");
	ApplySettings("audio");
	ApplySettings("game");
}

void UVilaGameInstance::Shutdown()
{
	vila::settings().changed.disconnect(SettingsConn);
	vila::game().world = nullptr;
	vila::warning_sink = nullptr;
	Super::Shutdown();
}

void UVilaGameInstance::StartNewGame(const FString& PlayerName, const vila::Json& Appearance, const vila::Json& Traits)
{
	vila::saves().prepare_new_game(U(PlayerName), Appearance, Traits);
	Flow = EVilaFlow::Playing;
	ReloadLevel();
}

bool UVilaGameInstance::LoadSlot(const FString& Slot)
{
	vila::game().world = nullptr;  // o mundo atual vai ser destruído; o load não deve tocá-lo
	if (!vila::saves().prepare_load(U(Slot))) return false;
	Flow = EVilaFlow::Playing;
	ReloadLevel();
	return true;
}

void UVilaGameInstance::ReturnToMenu()
{
	vila::game().world = nullptr;
	vila::clock().running = false;
	vila::clock().reset_holds();
	Flow = EVilaFlow::Menu;
	ReloadLevel();
}

void UVilaGameInstance::QuitGame()
{
	vila::settings().save();
	UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
}

void UVilaGameInstance::ReloadLevel()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World, true)));
}

// ---------------------------------------------------------------- opções
void UVilaGameInstance::ApplySettings(const std::string& Section)
{
	if (Section == "graphics") ApplyGraphics();
	else if (Section == "audio") ApplyAudio();
	else if (Section == "game") vila::set_locale(vila::to_str(vila::settings().get_value("game", "language")));
}

namespace
{
	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, ECVF_SetByGameSetting);
	}
	void SetCVarF(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, ECVF_SetByGameSetting);
	}
	int32 G(const char* Key, int32 Def) { return vila::to_int(vila::settings().get_value("graphics", Key), Def); }
	double GF(const char* Key, double Def) { return vila::to_num(vila::settings().get_value("graphics", Key), Def); }
}

void UVilaGameInstance::ApplyGraphics()
{
	UGameUserSettings* US = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!US) return;

	const int32 Quality = FMath::Clamp(G("quality", 2), 0, 3);
	US->SetOverallScalabilityLevel(Quality);
	US->SetShadowQuality(FMath::Clamp(G("shadows", Quality), 0, 3));
	US->SetVisualEffectQuality(FMath::Clamp(G("effects", Quality), 0, 3));
	US->SetViewDistanceQuality(FMath::Clamp(G("view_distance", Quality), 0, 3));
	US->SetFoliageQuality(FMath::Clamp(G("grass_density", 2), 0, 3));
	const int32 GI = G("gi", 1);
	US->SetGlobalIlluminationQuality(GI == 0 ? 0 : FMath::Max(2, Quality));
	US->SetReflectionQuality(G("reflections", 1) == 0 ? 1 : FMath::Max(2, Quality));
	US->SetResolutionScaleNormalized(FMath::Clamp(static_cast<float>(GF("render_scale", 1.0)), 0.5f, 1.f));
	US->SetVSyncEnabled(vila::truthy(vila::settings().get_value("graphics", "vsync")));
	US->SetFrameRateLimit(static_cast<float>(G("fps_limit", 60)));

	const int32 Mode = G("window_mode", 0);
	US->SetFullscreenMode(Mode == 1 ? EWindowMode::Fullscreen : Mode == 2 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
	const FString Res = S(vila::to_str(vila::settings().get_value("graphics", "resolution")));
	FString W, H;
	if (Res.Split(TEXT("x"), &W, &H)) US->SetScreenResolution(FIntPoint(FCString::Atoi(*W), FCString::Atoi(*H)));
	US->ApplySettings(false);

	// Lumen / ray tracing / AA / motion blur (por cima da escalabilidade).
	SetCVar(TEXT("r.DynamicGlobalIlluminationMethod"), GI == 0 ? 0 : 1);
	SetCVar(TEXT("r.ReflectionMethod"), G("reflections", 1) == 0 ? 2 : 1);
	SetCVar(TEXT("r.Lumen.HardwareRayTracing"), GI == 2 ? 1 : 0);
	const int32 AA = G("anti_aliasing", 2);
	SetCVar(TEXT("r.AntiAliasingMethod"), AA == 0 ? 0 : AA == 1 ? 1 : 4);
	SetCVar(TEXT("r.MotionBlurQuality"), vila::truthy(vila::settings().get_value("graphics", "motion_blur")) ? 4 : 0);
	SetCVar(TEXT("r.VolumetricCloud"), vila::truthy(vila::settings().get_value("graphics", "volumetric_clouds")) ? 1 : 0);
	const int32 Detail = FMath::Clamp(G("model_detail", 2), 0, 2);
	SetCVarF(TEXT("r.StaticMeshLODDistanceScale"), Detail == 0 ? 2.f : Detail == 1 ? 1.3f : 1.f);
	SetCVarF(TEXT("r.SkeletalMeshLODBias"), Detail == 0 ? 1.f : 0.f);
}

void UVilaGameInstance::ApplyAudio()
{
	if (UVilaAudio* Audio = GetSubsystem<UVilaAudio>()) Audio->ApplyVolumes();
}
