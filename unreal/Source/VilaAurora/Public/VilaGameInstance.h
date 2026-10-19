// Dono do núcleo de simulação no Unreal: carrega os dados (Content/Data), as opções e os
// saves, inicializa vila::game() e controla o fluxo menu -> jogo -> menu (equivale aos
// autoloads do Godot + troca de cena entre MainMenu.tscn e GameWorld.tscn).
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Vila/Core.h"

#include "VilaGameInstance.generated.h"

UENUM()
enum class EVilaFlow : uint8
{
	Menu,     // mundo montado como cenário de fundo, sem jogador; HUD mostra o menu principal
	Playing,  // partida em andamento (nova ou carregada: ver vila::game().pending_load)
};

UCLASS()
class VILAAURORA_API UVilaGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	static UVilaGameInstance* Get(const UObject* WorldContext);

	virtual void Init() override;
	virtual void Shutdown() override;

	EVilaFlow GetFlow() const { return Flow; }
	bool IsCoreReady() const { return bCoreReady; }
	// Erros de carregamento dos dados (para a tela de diagnóstico / menu).
	const TArray<FString>& GetDataErrors() const { return DataErrors; }

	// Fluxo. Cada um recarrega o nível; AVilaWorld remonta tudo a partir do estado do núcleo.
	void StartNewGame(const FString& PlayerName, const vila::Json& Appearance, const vila::Json& Traits);
	bool LoadSlot(const FString& Slot);  // false (e saves().last_error) se o save for inválido
	void ReturnToMenu();
	void QuitGame();

	// Aplica as opções de vídeo/áudio/jogo do vila::settings() ao motor (escalabilidade,
	// Lumen, sombras, resolução, vsync, fps, volumes, idioma). Chamado no Init e a cada
	// Settings::changed.
	void ApplySettings(const std::string& Section);

private:
	void ReloadLevel();
	void ApplyGraphics();
	void ApplyAudio();

	EVilaFlow Flow = EVilaFlow::Menu;
	bool bCoreReady = false;
	TArray<FString> DataErrors;
	int32 SettingsConn = 0;
};
