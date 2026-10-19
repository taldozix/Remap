// Port do MeshBatcher/ChunkBatcher do Godot: junta caixas, cilindros, tubos, esferas e
// quadriláteros estáticos e, no Flush, cria um UHierarchicalInstancedStaticMeshComponent
// por (malha, material). A cor de vértice do Godot vira PerInstanceCustomData 0..2 (tinta
// multiplicada pelo material criado com bInstanceTint). Sem o conteúdo importado
// (UVilaAssets::HasContent() == false), cada cor vira um material próprio.
//
// Tudo em espaço do mundo UE (cm). As malhas básicas têm 100 cm e pivô no centro; a escala
// da instância é o tamanho final / 100.
//
// Chaves de material (Mat.named do Godot): "" = só cor; "glass"; "water"; ou uma chave de
// UVilaAssets::Surface ("brick", "wood", "steel", ...).
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UObject/Object.h"

#include "VilaMeshBatch.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UVilaAssets;

USTRUCT()
struct FVilaMeshGroup
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;
	bool bTinted = false;   // usa PerInstanceCustomData (3 floats RGB)
	float CullDistance = 0.f;  // cm; 0 = sem corte
	TArray<FTransform> Transforms;  // mundo
	TArray<FLinearColor> Tints;
};

UCLASS()
class VILAAURORA_API UVilaMeshBatch : public UObject
{
	GENERATED_BODY()

public:
	// `Roughness`: rugosidade do material de cor pura (chave ""). `bShadows`: projeta sombra.
	void Init(UVilaAssets* InAssets, float InRoughness = 0.85f, bool bInShadows = true);

	// Configuração (antes do Flush).
	bool bCollision = false;          // colisão BlockAll (ignora a câmera) + afeta a navegação
	bool bHidden = false;             // só colisão (paredes invisíveis, colisores)
	bool bShadows = true;
	bool bSizeCull = true;            // distância de corte pelo tamanho da peça
	float CullScale = 1.f;            // multiplicador (opção view_distance)
	float Roughness = 0.85f;
	TEnumAsByte<EComponentMobility::Type> Mobility = EComponentMobility::Static;
	// Substitui o material de todas as peças (vidro das janelas, cabeças de poste). Sem tinta.
	UPROPERTY() TObjectPtr<UMaterialInterface> MaterialOverride;

	// ------------------------------------------------ peças (espaço do mundo UE, cm)
	// Instância genérica: `Xf` já com a escala relativa à malha. `Explicit` fixa o material
	// (sem tinta, a não ser com `bExplicitTint`: o material lê a PerInstanceCustomData).
	void AddMesh(UStaticMesh* Mesh, const FTransform& Xf, const FLinearColor& Tint, const FString& MatKey, UMaterialInterface* Explicit = nullptr, bool bExplicitTint = false);
	void AddBox(const FVector& Center, const FQuat& Rot, const FVector& SizeCm, const FLinearColor& Tint, const FString& MatKey = FString(), UMaterialInterface* Explicit = nullptr);
	// Cilindro com eixo no Z local de `Rot`.
	void AddCylinder(const FVector& Center, const FQuat& Rot, float RadiusCm, float HeightCm, const FLinearColor& Tint, const FString& MatKey = FString());
	void AddTube(const FVector& A, const FVector& B, float RadiusCm, const FLinearColor& Tint, const FString& MatKey = FString());
	void AddSphere(const FVector& Center, const FQuat& Rot, const FVector& RadiiCm, const FLinearColor& Tint, const FString& MatKey = FString());
	// Retângulo P0..P3 (ordem ao redor) com a face visível para `Facing`.
	void AddQuad(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& Facing, const FLinearColor& Tint, const FString& MatKey = FString());
	// Triângulo isósceles (C = ápice sobre o meio de AB), visível dos dois lados.
	void AddTri(const FVector& A, const FVector& B, const FVector& C, const FLinearColor& Tint, const FString& MatKey = FString());

	int32 Num() const { return Count; }

	// Cria os componentes presos a `Parent` (dono = ator do Parent) e esvazia o lote.
	TArray<UHierarchicalInstancedStaticMeshComponent*> Flush(USceneComponent* Parent, const FString& BaseName);

	// Material para uma chave de cor/superfície (usado também por quem não passa por lote).
	static UMaterialInterface* ResolveMaterial(UVilaAssets* Assets, const FString& MatKey, const FLinearColor& Tint, float Roughness, bool bAllowInstanceTint, bool& bOutTinted);
	// Distância de corte (cm) para uma peça com a maior dimensão `MaxDimCm`. 0 = nunca.
	static float CullForSize(float MaxDimCm, float Scale);
	// Multiplicador de distância da opção graphics.view_distance (0 curta, 1 média, 2 longa).
	static float ViewDistanceScale();

private:
	UPROPERTY() TObjectPtr<UVilaAssets> Assets;
	UPROPERTY() TMap<FString, FVilaMeshGroup> Groups;
	int32 Count = 0;
};
