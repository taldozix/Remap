// Ciclo de dia e noite + pós-processamento (port de DayNight.gd e PostFX.gd).
// Sol e lua (UDirectionalLightComponent, luzes de atmosfera 0 e 1), SkyAtmosphere,
// VolumetricCloud, ExponentialHeightFog (névoa volumétrica), SkyLight em captura em tempo
// real e um PostProcess global (exposição, bloom, vinheta, gradação por hora e clima).
// O clima fica no componente UVilaWeather.
//
// Regra do usuário: entre 9h e 15h a luz é neutra (branca), nunca amarelada; o dourado só
// aparece perto do nascer (6h) e do pôr do sol (18h30). Para isso a cor do sol compensa a
// transmitância da atmosfera (que avermelha a luz) e a gradação é neutra no meio do dia.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "VilaEnvironment.generated.h"

class AVilaWorld;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USceneComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVilaWeather;
class UVolumetricCloudComponent;

UCLASS()
class VILAAURORA_API AVilaEnvironment : public AActor
{
	GENERATED_BODY()

public:
	AVilaEnvironment();

	void Setup(AVilaWorld* World);
	void ApplySettings();
	// PostFX.set_mood: humor da cena conforme hora e clima (dourado só no amanhecer/entardecer,
	// neutro de dia, frio à noite, cinza na chuva). Cores como no Godot (espaço de exibição).
	void SetMood(const FLinearColor& Highlight, const FLinearColor& Shadow, float Saturation);
	float NightAmount() const { return Night; }  // 0 dia … 1 noite
	// Menu principal: céu limpo, sem mudanças de clima; a hora segue o relógio (acelerado pelo mundo).
	void SetMenuMode(bool bMenu);

	float GoldenAmount() const { return Golden; }
	UVilaWeather* GetWeather() const { return Weather; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetupClouds();
	void UpdateCycle(float DeltaSeconds);
	void UpdateSky(float Cloud);
	void UpdateWorldLights(float Cloud);
	void ApplyGrading();
	void SetExposure(float EV100);
	FVector RainEmitter() const;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Moon;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Sky;
	UPROPERTY() TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY() TObjectPtr<UVilaWeather> Weather;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMID;

	TWeakObjectPtr<AVilaWorld> VilaWorld;

	// estado do ciclo
	float Night = 0.f;
	float Golden = 0.f;
	float ExposureEV = 14.f;
	float AtmoTimer = 0.f;  // céu, névoa e luz do céu são atualizados a cada 0,1 s
	bool bMenuMode = false;
	bool bSetupDone = false;

	// gradação (PostFX)
	FLinearColor MoodHighlight = FLinearColor::White;
	FLinearColor MoodShadow = FLinearColor::White;
	float MoodSaturation = 0.93f;
	bool bGrading = true;

	// opções
	float FogBase = 0.002f;  // densidade do Godot (por metro)
	bool bExtendedLuminance = true;

	// nuvens: parâmetro escalar do material que controla a cobertura (achado por nome)
	FName CloudParam;
	float CloudParamBase = 1.f;

	// últimos valores enviados (evita trabalho a cada quadro)
	float LastSkyCloud = -1.f;
	float LastNightSent = -1.f;
	float LastWetSent = -1.f;
	float LastWindSent = -1.f;
	float LastBuildingsNight = -1.f;
	int32 LampsState = -1;  // -1 desconhecido, 0 apagados, 1 acesos

	int32 SettingsConn = 0;
};
