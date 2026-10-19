#include "VilaEnvironment.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Scene.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "VilaAssets.h"
#include "VilaAurora.h"
#include "VilaBuilding.h"
#include "VilaCameraRig.h"
#include "VilaGameInstance.h"
#include "VilaWeather.h"
#include "VilaWorld.h"
#include "VilaWorldBuilder.h"
#include "Vila/Clock.h"
#include "Vila/Settings.h"

namespace VilaEnvironmentImpl
{
	// ------------------------------------------------------------------ conversão de unidades
	// Energia do sol no Godot (1,05 ao meio-dia limpo) -> lux no chão.
	constexpr float NoonLux = 75000.f;
	constexpr float NoonEnergy = 1.05f;
	// Lua: no Godot o próprio sol virava luar (energia 0,22, azulado). Aqui é uma segunda luz
	// de atmosfera; lux já "artístico" (a lua real tem ~0,3 lux) para a noite ser legível.
	constexpr float MoonLux = 2.f;
	// Escurece a exposição à noite (EV a mais): noite escura, mas legível.
	constexpr float NightExposureOffset = 1.8f;
	// Valores-padrão do USkyAtmosphereComponent.
	constexpr float RayleighDefault = 0.0331f;
	constexpr float MieDefault = 0.003996f;

	const TCHAR* CloudPackage = TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst");
	const TCHAR* CloudObject = TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst");

	// Cores do Godot (Color) estão em sRGB; a interpolação é feita nesses números, como lá.
	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T) { return A + (B - A) * T; }

	float SrgbToLinear(float C)
	{
		C = FMath::Max(0.f, C);
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}
	FLinearColor Lin(const FLinearColor& Srgb) { return FLinearColor(SrgbToLinear(Srgb.R), SrgbToLinear(Srgb.G), SrgbToLinear(Srgb.B), 1.f); }

	// smoothstep do Godot 4 (aceita bordas invertidas: from > to).
	float GodotSmoothstep(float From, float To, float X)
	{
		if (FMath::IsNearlyEqual(From, To)) return From;
		const float S = FMath::Clamp((X - From) / (To - From), 0.f, 1.f);
		return S * S * (3.f - 2.f * S);
	}

	// Transmitância aproximada da atmosfera-padrão do SkyAtmosphere (Rayleigh + Mie + ozônio,
	// profundidades ópticas verticais) para o sol a `ElevDeg` graus. A massa de ar segue
	// Kasten–Young. Serve para compensar a cor do sol: o motor avermelha a luz do sol que
	// atravessa a atmosfera, e a regra do usuário pede luz neutra no meio do dia.
	FLinearColor Transmittance(float ElevDeg)
	{
		const float E = FMath::Clamp(ElevDeg, 0.5f, 90.f);
		const float AirMass = 1.f / (FMath::Sin(FMath::DegreesToRadians(E)) + 0.50572f * FMath::Pow(E + 6.07995f, -1.6364f));
		const FVector Tau(0.0614f, 0.1416f, 0.2716f);
		return FLinearColor(FMath::Exp(-AirMass * Tau.X), FMath::Exp(-AirMass * Tau.Y), FMath::Exp(-AirMass * Tau.Z), 1.f);
	}

	// Direção da luz a partir dos ângulos do Godot (rotation_degrees = (-elevação, yaw, 0);
	// a luz aponta para -Z local). Godot (x, y, z) -> UE (x, z, y).
	FRotator LightRotation(float ElevDeg, float YawDeg)
	{
		const float A = FMath::DegreesToRadians(ElevDeg);
		const float Y = FMath::DegreesToRadians(YawDeg);
		const FVector GodotDir(-FMath::Cos(A) * FMath::Sin(Y), -FMath::Sin(A), -FMath::Cos(A) * FMath::Cos(Y));
		return FVector(GodotDir.X, GodotDir.Z, GodotDir.Y).Rotation();
	}

	int32 GraphicsInt(const char* Key, int32 Def) { return vila::to_int(vila::settings().get_value("graphics", Key), Def); }
	bool GraphicsBool(const char* Key) { return vila::truthy(vila::settings().get_value("graphics", Key)); }

	FVector4 Gain4(const FLinearColor& C, float W = 1.f) { return FVector4(C.R, C.G, C.B, W); }
	// Ganho de exibição (shader do Godot, depois do tonemap) -> ganho linear (antes do tonemap).
	FLinearColor DisplayGainToLinear(const FLinearColor& C)
	{
		return FLinearColor(FMath::Pow(FMath::Max(C.R, 0.f), 2.2f), FMath::Pow(FMath::Max(C.G, 0.f), 2.2f), FMath::Pow(FMath::Max(C.B, 0.f), 2.2f), 1.f);
	}
}

AVilaEnvironment::AVilaEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Sol: luz de atmosfera 0, sombras dinâmicas (sombras virtuais pelo DefaultEngine.ini).
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->bAtmosphereSunLight = true;
	Sun->AtmosphereSunLightIndex = 0;
	Sun->bUseTemperature = false;
	Sun->Intensity = VilaEnvironmentImpl::NoonLux;

	// Lua: luz de atmosfera 1, fraca e azulada.
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(Root);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->bAtmosphereSunLight = true;
	Moon->AtmosphereSunLightIndex = 1;
	Moon->bUseTemperature = false;
	Moon->Intensity = 0.f;

	Sky = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	Sky->SetupAttachment(Root);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricClouds"));
	Clouds->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	Fog->SetupAttachment(Root);
	Fog->bEnableVolumetricFog = true;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->bRealTimeCapture = true;

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;

	Weather = CreateDefaultSubobject<UVilaWeather>(TEXT("Weather"));
}

void AVilaEnvironment::BeginPlay()
{
	Super::BeginPlay();

	// Com a faixa estendida (padrão do UE5), os limites da exposição automática são em EV100;
	// sem ela, em luminância (cd/m²).
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange")))
		bExtendedLuminance = CVar->GetInt() != 0;

	FPostProcessSettings& S = Post->Settings;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = 3.f;
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = 1.f;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = FMath::Log2(0.92f);  // tonemap_exposure 0,92 do Godot
	S.bOverride_BloomIntensity = true;
	S.bOverride_VignetteIntensity = true;
	S.bOverride_AmbientOcclusionIntensity = true;
	// Balanço de branco fixo e neutro: o calor vem só da cor do sol perto do nascer/pôr.
	S.bOverride_WhiteTemp = true;
	S.WhiteTemp = 6500.f;
	S.bOverride_WhiteTint = true;
	S.WhiteTint = 0.f;
	S.bOverride_ColorSaturation = true;
	S.bOverride_ColorContrast = true;
	S.bOverride_ColorGainShadows = true;
	S.bOverride_ColorGainMidtones = true;
	S.bOverride_ColorGainHighlights = true;
	// Faixas de sombra/realce aproximando o smoothstep(0,15, 0,85) do PostFX.
	S.bOverride_ColorCorrectionShadowsMax = true;
	S.ColorCorrectionShadowsMax = 0.09f;
	S.bOverride_ColorCorrectionHighlightsMin = true;
	S.ColorCorrectionHighlightsMin = 0.5f;

	Fog->SetFogHeightFalloff(0.05f);  // a névoa do Godot não dependia da altura
	Fog->SetFogMaxOpacity(1.f);
	Fog->SetStartDistance(0.f);
	Fog->SetVolumetricFogDistance(6000.f);
	Fog->SetDirectionalInscatteringExponent(8.f);

	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);
	Moon->SetAtmosphereSunLight(true);
	Moon->SetAtmosphereSunLightIndex(1);
	Moon->SetLightColor(VilaEnvironmentImpl::Lin(FLinearColor(0.55f, 0.65f, 0.95f)));

	SetupClouds();
}

void AVilaEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SettingsConn != 0) vila::settings().changed.disconnect(SettingsConn);
	SettingsConn = 0;
	Super::EndPlay(EndPlayReason);
}

void AVilaEnvironment::Setup(AVilaWorld* World)
{
	VilaWorld = World ? World : AVilaWorld::Get(this);
	if (!bSetupDone)
	{
		bSetupDone = true;
		if (const UVilaGameInstance* GI = UVilaGameInstance::Get(this))
			if (GI->GetFlow() == EVilaFlow::Menu) SetMenuMode(true);
		Weather->Setup();
		TWeakObjectPtr<AVilaEnvironment> WeakThis(this);
		SettingsConn = vila::settings().changed.connect([WeakThis](const std::string& Section)
		{
			if (Section != "graphics") return;
			if (AVilaEnvironment* Self = WeakThis.Get()) Self->ApplySettings();
		});
	}
	ApplySettings();
	UpdateCycle(0.f);
}

void AVilaEnvironment::SetupClouds()
{
	UMaterialInterface* CloudMat = FPackageName::DoesPackageExist(VilaEnvironmentImpl::CloudPackage) ? LoadObject<UMaterialInterface>(nullptr, VilaEnvironmentImpl::CloudObject) : nullptr;
	if (!CloudMat)
	{
		UE_LOG(LogVila, Log, TEXT("Material de nuvem volumétrica do motor ausente; nuvens desligadas."));
		Clouds->SetVisibility(false);
		return;
	}
	CloudMID = UMaterialInstanceDynamic::Create(CloudMat, this);
	Clouds->SetMaterial(CloudMID);
	Clouds->SetVisibility(VilaEnvironmentImpl::GraphicsBool("volumetric_clouds"));
	// O nome do parâmetro de cobertura não é garantido: procura por "cover", depois "density".
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	CloudMat->GetAllScalarParameterInfo(Infos, Ids);
	for (const TCHAR* Key : {TEXT("cover"), TEXT("density")})
	{
		for (const FMaterialParameterInfo& Info : Infos)
			if (Info.Name.ToString().Contains(Key, ESearchCase::IgnoreCase))
			{
				CloudParam = Info.Name;
				break;
			}
		if (!CloudParam.IsNone()) break;
	}
	if (!CloudParam.IsNone())
	{
		float Base = 1.f;
		if (CloudMat->GetScalarParameterValue(FHashedMaterialParameterInfo(CloudParam), Base)) CloudParamBase = Base;
	}
}

void AVilaEnvironment::SetMenuMode(bool bMenu)
{
	bMenuMode = bMenu;
	if (Weather) Weather->SetMenuMode(bMenu);
	LastSkyCloud = -1.f;
}

// ---------------------------------------------------------------- opções (DayNight/PostFX.apply_settings)
void AVilaEnvironment::ApplySettings()
{
	const int32 Fx = FMath::Clamp(VilaEnvironmentImpl::GraphicsInt("effects", 2), 0, 3);
	const int32 Shadows = FMath::Clamp(VilaEnvironmentImpl::GraphicsInt("shadows", 2), 0, 3);
	const int32 ViewDist = FMath::Clamp(VilaEnvironmentImpl::GraphicsInt("view_distance", 2), 0, 3);
	bGrading = VilaEnvironmentImpl::GraphicsBool("color_grading");

	static const float ShadowDist[] = {40.f, 55.f, 75.f, 110.f};  // directional_shadow_max_distance (m)
	Sun->SetCastShadows(Shadows > 0);
	Sun->SetDynamicShadowDistanceMovableLight(ShadowDist[Shadows] * 100.f);
	Sun->SetDynamicShadowCascades(FMath::Clamp(Shadows + 1, 1, 4));
	Moon->SetCastShadows(Shadows > 0);

	static const float FogDensity[] = {0.006f, 0.0035f, 0.002f, 0.0015f};  // env.fog_density por view_distance
	FogBase = FogDensity[ViewDist];
	Fog->SetVolumetricFog(Fx >= 2);

	FPostProcessSettings& S = Post->Settings;
	S.BloomIntensity = Fx >= 1 ? 0.45f : 0.f;           // glow_enabled (effects >= 1)
	S.AmbientOcclusionIntensity = Fx >= 1 ? 0.5f : 0.f; // ssao_enabled (só vale sem Lumen)
	S.VignetteIntensity = bGrading ? 0.3f : 0.f;        // vinheta 0,22 do PostFX

	if (Clouds) Clouds->SetVisibility(CloudMID != nullptr && VilaEnvironmentImpl::GraphicsBool("volumetric_clouds"));
	if (Weather) Weather->ApplySettings();
	LastSkyCloud = -1.f;
	AtmoTimer = 0.f;
	ApplyGrading();
}

// ---------------------------------------------------------------- gradação (PostFX)
void AVilaEnvironment::SetMood(const FLinearColor& Highlight, const FLinearColor& Shadow, float Saturation)
{
	MoodHighlight = Highlight;
	MoodShadow = Shadow;
	MoodSaturation = Saturation;
	ApplyGrading();
}

void AVilaEnvironment::ApplyGrading()
{
	if (!Post) return;
	FPostProcessSettings& S = Post->Settings;
	// Ajustes fixos do Environment do Godot (adjustment_saturation 0,98, contrast 1,04).
	const float BaseSat = 0.98f;
	const float BaseContrast = 1.04f;
	if (bGrading)
	{
		const FLinearColor HL = VilaEnvironmentImpl::DisplayGainToLinear(MoodHighlight);
		const FLinearColor SH = VilaEnvironmentImpl::DisplayGainToLinear(MoodShadow);
		S.ColorGainHighlights = VilaEnvironmentImpl::Gain4(HL);
		S.ColorGainShadows = VilaEnvironmentImpl::Gain4(SH);
		S.ColorGainMidtones = VilaEnvironmentImpl::Gain4((HL + SH) * 0.5f);  // o shader misturava sombra e realce nos meios-tons
		S.ColorSaturation = FVector4(1.f, 1.f, 1.f, BaseSat * MoodSaturation);
		S.ColorContrast = FVector4(1.f, 1.f, 1.f, BaseContrast * 1.05f);
	}
	else
	{
		S.ColorGainHighlights = FVector4(1.f, 1.f, 1.f, 1.f);
		S.ColorGainShadows = FVector4(1.f, 1.f, 1.f, 1.f);
		S.ColorGainMidtones = FVector4(1.f, 1.f, 1.f, 1.f);
		S.ColorSaturation = FVector4(1.f, 1.f, 1.f, BaseSat);
		S.ColorContrast = FVector4(1.f, 1.f, 1.f, BaseContrast);
	}
}

void AVilaEnvironment::SetExposure(float EV100)
{
	ExposureEV = EV100;
	FPostProcessSettings& S = Post->Settings;
	// Faixa estreita (±1 EV) em volta do alvo da hora: exposição "quase manual".
	const float Lo = EV100 - 1.f;
	const float Hi = EV100 + 1.f;
	S.AutoExposureMinBrightness = bExtendedLuminance ? Lo : FMath::Pow(2.f, Lo - 3.f);
	S.AutoExposureMaxBrightness = bExtendedLuminance ? Hi : FMath::Pow(2.f, Hi - 3.f);
}

// ---------------------------------------------------------------- ciclo
void AVilaEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSetupDone) Setup(nullptr);
	UpdateCycle(DeltaSeconds);
}

FVector AVilaEnvironment::RainEmitter() const
{
	const FVector Up(0.f, 0.f, 1400.f);  // rain.global_position = camera_rig + (0, 14, 0)
	if (const AVilaWorld* W = VilaWorld.Get())
		if (const AVilaCameraRig* Rig = W->CameraRig)
		{
			const AActor* Target = Rig->Target;
			return (IsValid(Target) ? Target->GetActorLocation() : Rig->GetActorLocation()) + Up;
		}
	if (const APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(this, 0))
		return PCM->GetCameraLocation() + Up;
	return GetActorLocation() + Up;
}

void AVilaEnvironment::UpdateSky(float Cloud)
{
	if (LastSkyCloud >= 0.f && FMath::Abs(Cloud - LastSkyCloud) < 0.005f) return;
	LastSkyCloud = Cloud;
	// Céu encoberto: menos azul (Rayleigh), mais névoa clara (Mie), céu mais escuro.
	Sky->SetRayleighScatteringScale(VilaEnvironmentImpl::RayleighDefault * (1.f - 0.45f * Cloud));
	Sky->SetMieScatteringScale(VilaEnvironmentImpl::MieDefault * (1.f + 5.f * Cloud));
	const float L = 1.f - 0.3f * Cloud;
	Sky->SetSkyLuminanceFactor(FLinearColor(L, L, L, 1.f));
	// Sombras difusas e quase sem contorno (shadow_opacity = 1 - cloud * 0,8 no Godot).
	Sun->SetLightSourceAngle(0.5357f + Cloud * 4.f);
	if (CloudMID && !CloudParam.IsNone())
		CloudMID->SetScalarParameterValue(CloudParam, CloudParamBase * (0.55f + 1.1f * Cloud));
}

void AVilaEnvironment::UpdateCycle(float DeltaSeconds)
{
	if (!Weather || !Post) return;
	Weather->Update(DeltaSeconds, RainEmitter());
	const float Cloud = Weather->Cloud();

	const float H = static_cast<float>(vila::clock().hour_f());
	// Altura do sol: nasce às 6h, se põe às 18h30.
	const float DayT = FMath::Clamp((H - 6.f) / 12.5f, 0.f, 1.f);
	const float Elev = FMath::Sin(DayT * PI);
	const bool bDay = H >= 6.f && H <= 18.5f;

	// Sol: alto e branco no meio do dia (sombras curtas), baixo e dourado só perto do
	// nascer (6h) e do pôr do sol (18h30).
	Golden = bDay ? VilaEnvironmentImpl::GodotSmoothstep(0.42f, 0.06f, Elev) : 0.f;
	Golden *= (1.f - Cloud);
	float N = 0.f;
	if (!bDay) N = 1.f;
	else if (Elev < 0.2f) N = 1.f - Elev / 0.2f;
	Night = FMath::Clamp(N, 0.f, 1.f);

	// Posição e energia (unidades do Godot). Fora do dia do Godot, um crepúsculo curto
	// (5h15–6h e 18h30–19h15) leva o sol até o horizonte e apaga, para o céu não "saltar".
	float SunElev = 7.f;
	float SunYaw = FMath::Lerp(80.f, 250.f, DayT);
	float SunEnergy = 0.f;
	float SunGold = Golden;
	if (bDay)
	{
		SunElev = FMath::Max(7.f, Elev * 66.f);
		SunEnergy = FMath::Lerp(0.2f, 1.05f, FMath::Clamp(Elev * 2.2f, 0.f, 1.f)) * (1.f - Cloud * 0.72f);
	}
	else if (H > 18.5f && H < 19.25f)
	{
		const float K = (H - 18.5f) / 0.75f;
		SunElev = FMath::Lerp(7.f, 1.f, K);
		SunEnergy = 0.2f * (1.f - Cloud * 0.72f) * (1.f - K);
		SunGold = 1.f - Cloud;
	}
	else if (H >= 5.25f && H < 6.f)
	{
		const float K = (H - 5.25f) / 0.75f;
		SunElev = FMath::Lerp(1.f, 7.f, K);
		SunEnergy = 0.2f * (1.f - Cloud * 0.72f) * K;
		SunGold = 1.f - Cloud;
	}

	// Cor desejada no chão: branca neutra de dia (regra do usuário; o Godot usava
	// (1, 0.98, 0.95)), dourada só com `golden`.
	const FLinearColor SunCol = VilaEnvironmentImpl::Mix(FLinearColor(1.f, 1.f, 1.f), FLinearColor(1.f, 0.64f, 0.34f), SunGold);
	const float SunLux = VilaEnvironmentImpl::NoonLux * SunEnergy / VilaEnvironmentImpl::NoonEnergy;
	float SunGroundLux = 0.f;  // iluminância horizontal efetiva (para a exposição)
	const bool bSunOn = SunEnergy > 0.0005f;
	if (Sun->IsVisible() != bSunOn) Sun->SetVisibility(bSunOn);
	if (bSunOn)
	{
		// Compensa a transmitância da atmosfera: a cor efetiva no chão fica a do Godot.
		const float CompElev = FMath::Max(SunElev, 7.f);
		const FLinearColor T = VilaEnvironmentImpl::Transmittance(CompElev);
		const FLinearColor D = VilaEnvironmentImpl::Lin(SunCol);
		const FLinearColor V(D.R / T.R, D.G / T.G, D.B / T.B, 1.f);
		const float MaxV = FMath::Max3(V.R, V.G, V.B);
		Sun->SetWorldRotation(VilaEnvironmentImpl::LightRotation(SunElev, SunYaw));
		Sun->SetLightColor(V / MaxV);
		Sun->SetIntensity(SunLux * MaxV);
		const float Extra = VilaEnvironmentImpl::Transmittance(SunElev).GetLuminance() / FMath::Max(0.001f, T.GetLuminance());
		SunGroundLux = SunLux * Extra * FMath::Sin(FMath::DegreesToRadians(SunElev));
	}

	// Lua (o luar do Godot), azimute contínuo durante a noite.
	const float NightT = FMath::Clamp(H >= 12.f ? (H - 18.5f) / 11.5f : (H + 5.5f) / 11.5f, 0.f, 1.f);
	const float MoonNow = VilaEnvironmentImpl::MoonLux * (1.f - Cloud * 0.5f) * Night;
	const bool bMoonOn = MoonNow > 0.001f;
	if (Moon->IsVisible() != bMoonOn) Moon->SetVisibility(bMoonOn);
	if (bMoonOn)
	{
		Moon->SetWorldRotation(VilaEnvironmentImpl::LightRotation(40.f, FMath::Lerp(80.f, 250.f, NightT)));
		Moon->SetIntensity(MoonNow);
	}

	UpdateSky(bMenuMode ? 0.f : Cloud);

	// Exposição: EV de um fotômetro incidente (EV = log2(E / 2,5)), um pouco abaixo,
	// e mais escura à noite.
	const float SkyLux = SunGroundLux > 0.f ? SunLux * 0.25f * (1.f + Cloud) * FMath::Clamp(FMath::Sin(FMath::DegreesToRadians(SunElev)) * 2.5f, 0.05f, 1.f) * (1.f - Night) : 0.f;
	const float Lux = SunGroundLux + SkyLux + MoonNow * 0.6f + 0.5f * Night;
	const float EV = FMath::Clamp(FMath::Log2(FMath::Max(Lux, 0.05f) / 2.5f) - 0.5f + VilaEnvironmentImpl::NightExposureOffset * Night, -3.f, 16.f);
	SetExposure(EV);

	// Luz ambiente, névoa e céu: a cada 0,1 s.
	AtmoTimer -= DeltaSeconds;
	if (AtmoTimer <= 0.f)
	{
		AtmoTimer = 0.1f;
		// env.ambient_light_energy / color do Godot. A luz do céu capturada à noite (só luar) é
		// fraca demais em relação à direta, então ganha reforço.
		const float Amb = FMath::Lerp(0.5f + Cloud * 0.25f - Golden * 0.08f, 0.3f, Night);
		SkyLight->SetIntensity(Amb / 0.5f * FMath::Lerp(1.f, 6.f, Night));
		SkyLight->SetLightColor(VilaEnvironmentImpl::Lin(VilaEnvironmentImpl::Mix(FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.5f, 0.58f, 0.85f), Night * 0.5f)));

		// fog_light_color = cor do horizonte (clima, dourado, noite).
		FLinearColor Hor = VilaEnvironmentImpl::Mix(VilaEnvironmentImpl::Mix(FLinearColor(0.72f, 0.82f, 0.93f), FLinearColor(0.66f, 0.68f, 0.71f), Cloud), FLinearColor(1.f, 0.68f, 0.45f), Golden * 0.8f);
		Hor = VilaEnvironmentImpl::Mix(Hor, FLinearColor(0.16f, 0.18f, 0.28f), Night);
		const FLinearColor FogCol = VilaEnvironmentImpl::Mix(Hor, FLinearColor(1.f, 0.78f, 0.55f), Golden * 0.5f * (1.f - Night));
		// A névoa do UE é em luminância: escala pela exposição atual (cinza médio = 1,2 * 2^EV * 0,18).
		const float LumScale = 1.2f * FMath::Pow(2.f, EV);
		Fog->SetFogDensity(FogBase * 10.f * (1.f + Cloud * 0.8f));  // Godot (por m) -> UE
		Fog->SetFogInscatteringColor(VilaEnvironmentImpl::Lin(FogCol) * (LumScale * 0.5f));
		Fog->SetDirectionalInscatteringColor(VilaEnvironmentImpl::Lin(FLinearColor(1.f, 0.78f, 0.55f)) * (LumScale * 0.25f * Golden));
	}

	// Gradação de cor da câmera acompanha a hora e o clima (PostFX.set_mood).
	FLinearColor HL = VilaEnvironmentImpl::Mix(FLinearColor(1.f, 1.f, 1.f), FLinearColor(1.08f, 1.f, 0.85f), Golden);
	FLinearColor SH = VilaEnvironmentImpl::Mix(FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.86f, 0.95f, 1.04f), Golden);
	float Sat = 0.93f + Golden * 0.08f;
	HL = VilaEnvironmentImpl::Mix(HL, FLinearColor(0.95f, 0.98f, 1.06f), Night * 0.8f);
	SH = VilaEnvironmentImpl::Mix(SH, FLinearColor(0.85f, 0.92f, 1.1f), Night * 0.8f);
	Sat = FMath::Lerp(Sat, 0.85f, Night * 0.6f);
	HL = VilaEnvironmentImpl::Mix(HL, FLinearColor(0.97f, 0.99f, 1.02f), Cloud);
	SH = VilaEnvironmentImpl::Mix(SH, FLinearColor(0.95f, 0.98f, 1.02f), Cloud);
	Sat = FMath::Lerp(Sat, 0.8f, Cloud);
	SetMood(HL, SH, Sat);

	UpdateWorldLights(Cloud);
}

void AVilaEnvironment::UpdateWorldLights(float Cloud)
{
	// Escalares globais dos materiais (Mat.set_night, Mat/Nature.set_wetness, vento).
	if (UVilaAssets* Assets = UVilaAssets::Get(this))
	{
		if (FMath::Abs(Night - LastNightSent) > 0.002f)
		{
			LastNightSent = Night;
			Assets->SetGlobal(this, TEXT("Night"), Night);
		}
		const float Wet = Weather->Wetness();
		if (FMath::Abs(Wet - LastWetSent) > 0.002f)
		{
			LastWetSent = Wet;
			Assets->SetGlobal(this, TEXT("Wetness"), Wet);
		}
		const float Wind = FMath::Clamp(0.35f + 0.25f * Cloud + 0.4f * Weather->RainAmount(), 0.f, 1.f);
		if (FMath::Abs(Wind - LastWindSent) > 0.005f)
		{
			LastWindSent = Wind;
			Assets->SetGlobal(this, TEXT("Wind"), Wind);
		}
	}

	AVilaWorld* W = VilaWorld.Get();
	UVilaWorldBuilder* Builder = W ? W->Builder.Get() : nullptr;
	if (!Builder) return;

	// Postes: acesos à noite, ou com chuva forte ao entardecer.
	const bool bLamps = Night > 0.35f || (Cloud > 0.7f && Night > 0.1f);
	if (LampsState != (bLamps ? 1 : 0))
	{
		LampsState = bLamps ? 1 : 0;
		for (const TObjectPtr<ULocalLightComponent>& L : Builder->Streetlights)
			if (IsValid(L)) L->SetVisibility(bLamps);
	}
	// Janelas acesas e luzes internas.
	const bool bEdge = (Night <= 0.f || Night >= 1.f) && Night != LastBuildingsNight;
	if (bEdge || FMath::Abs(Night - LastBuildingsNight) > 0.01f)
	{
		LastBuildingsNight = Night;
		for (const TPair<FString, TObjectPtr<AVilaBuilding>>& P : Builder->Buildings)
			if (IsValid(P.Value)) P.Value->SetNight(Night);
	}
}
