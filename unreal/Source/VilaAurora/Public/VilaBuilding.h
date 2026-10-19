// Prédio com interior acessível (port de Building.gd). Um ator por prédio, com os
// componentes de malha dele. Cada lado ("n", "s", "e", "w") tem duas versões de parede
// (inteira e baixa) para o corte estilo diorama quando o jogador está dentro ou atrás.
// O UVilaWorldBuilder monta a geometria e preenche os contêineres abaixo.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vila/Core.h"

#include "VilaBuilding.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class USceneComponent;

UCLASS()
class VILAAURORA_API AVilaBuilding : public AActor
{
	GENERATED_BODY()

public:
	AVilaBuilding();

	// O builder chama antes de adicionar a geometria (lê id, centro, tamanho e altura).
	void Setup(const vila::Json& Def);

	FString BuildingId;
	vila::Json Def;
	FBox2D Rect = FBox2D(ForceInit);  // área no plano XY do UE (cm)
	float HeightCm = 300.f;

	// Dentro do retângulo do prédio (XY), com margem em cm.
	bool Contains(const FVector& P, float MarginCm = 0.f) const;
	// Corta as paredes voltadas para a câmera (`ViewDir` = da câmera para a cena) e esconde o telhado.
	void SetCut(bool bOn, const FVector& ViewDir);
	// Janelas acesas e luz interna (0 dia … 1 noite).
	void SetNight(float Amount);

	bool IsCut() const { return bCut; }
	float NightAmount() const { return Night; }

	// ------------------------------------------------ montado pelo builder
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TMap<FString, TObjectPtr<USceneComponent>> SideFull;  // "n"/"s"/"e"/"w" -> contêiner
	UPROPERTY() TMap<FString, TObjectPtr<USceneComponent>> SideLow;
	UPROPERTY() TObjectPtr<USceneComponent> Roof;
	UPROPERTY() TObjectPtr<USceneComponent> Interior;
	UPROPERTY() TObjectPtr<UPointLightComponent> Lamp;
	// Vidro das janelas deste prédio (M_Windows; parâmetros "Intensity" e "Night").
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WindowMID;
	// Origem do WindowMID: 0 = cor pura (sem conteúdo), 1 = M_Windows, 2 = M_Emissive.
	int32 WindowMode = 0;

	// Cria (se preciso) um contêiner de componentes preso à raiz.
	USceneComponent* MakeContainer(const FString& Name, bool bVisible = true);

private:
	bool bCut = false;
	TArray<FString> CutSides;
	float Night = -1.f;
};
