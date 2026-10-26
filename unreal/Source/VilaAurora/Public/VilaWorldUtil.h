// Utilitários e tipos simples do AVilaWorld, fora do cabeçalho refletido: o UHT só
// analisa arquivos com macros de reflexão, então namespaces, templates e a base C++ pura
// vila::IWorld ficam aqui.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vila/Core.h"
#include "Vila/World.h"
#include "VilaConv.h"

// Base sem namespace para o UCLASS (o UHT lê o nome das bases da classe).
class FVilaWorldBase : public vila::IWorld
{
};

// Utilitários compartilhados pelos atores do mundo (porta, móvel, item no chão, entrega).
namespace VilaW
{
	// tr() + formatação estilo GDScript: tr("Pegar %s") % [x]  ->  Trf("Pegar %s", x)
	template <typename... TArgs>
	FString Trf(const std::string& Key, TArgs&&... Args)
	{
		return VilaConv::S(vila::fmt(vila::tr(Key), {vila::FmtArg(Args)...}));
	}

	// Posição "global_position" do Godot: pé do personagem (o ACharacter tem a origem no
	// centro da cápsula). Para outros atores, a própria localização.
	VILAAURORA_API FVector Feet(const AActor* Actor);
	// Teleporta um ator para que o pé fique em `FeetPos`.
	VILAAURORA_API void SetFeet(AActor* Actor, const FVector& FeetPos);
	inline float Dist2D(const FVector& A, const FVector& B) { return static_cast<float>(FVector::Dist2D(A, B)); }
	// Cor sRGB (como Color(r, g, b) do Godot) -> linear.
	VILAAURORA_API FLinearColor SRGB(float R, float G, float B);
	// Color.lerp do Godot (no espaço sRGB) entre uma cor linear e uma sRGB.
	VILAAURORA_API FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& BLinear, float T);
	// Liga/desliga o contorno de foco em todos os componentes primitivos do ator.
	VILAAURORA_API void SetActorOutline(AActor* Actor, bool bOn);
	// Fala aleatória de data/chatter.json (traduzida).
	VILAAURORA_API FString PickChatter(const std::string& Key);
}

// Marcador de missão (posição + rótulo) para o HUD/mapa.
struct FVilaMarker
{
	FVector Pos = FVector::ZeroVector;
	FString Label;
};

// Atropelamento pendente: resolvido após 30 s (ou ao prestar socorro).
struct FVilaIncident
{
	FString Npc;
	float T = 30.f;
	vila::Json Witnesses = vila::Json::array();
	bool bHelped = false;
	float Severity = 0.f;
};
