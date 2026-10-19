// Malhas estáticas geradas em runtime (FMeshDescription -> UStaticMesh) para as formas que
// as primitivas do motor não cobrem: triângulo das empenas, tufo de grama e maços de folhas.
// Equivale às partes de SurfaceTool do Godot (MeshBatcher.add_tri, GrassField._make_tuft,
// FoliageBatcher.add_canopy). As malhas ficam em cache por chave de forma.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"

#include "VilaProcMesh.generated.h"

class UStaticMesh;

// Acumulador de triângulos (vértices não compartilhados, espaço UE em cm).
struct VILAAURORA_API FVilaProcBuilder
{
	TArray<FVector3f> Pos;
	TArray<FVector3f> Nrm;
	TArray<FVector2f> UV;
	TArray<FLinearColor> Col;

	// Triângulo com a face da frente voltada para `N` (a ordem dos vértices é corrigida).
	void AddTri(const FVector& A, const FVector& B, const FVector& C, const FVector& N,
		const FVector2f& UA = FVector2f(0, 0), const FVector2f& UB = FVector2f(1, 0), const FVector2f& UC = FVector2f(0, 1),
		const FLinearColor& CA = FLinearColor::White, const FLinearColor& CB = FLinearColor::White, const FLinearColor& CC = FLinearColor::White);
	// Os dois lados (normal `N` e `-N`).
	void AddTriTwoSided(const FVector& A, const FVector& B, const FVector& C, const FVector& N,
		const FVector2f& UA = FVector2f(0, 0), const FVector2f& UB = FVector2f(1, 0), const FVector2f& UC = FVector2f(0, 1));
	// Quadrilátero p0..p3 (em ordem ao redor), face para `N`.
	void AddQuad(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& N,
		const FLinearColor& C = FLinearColor::White);
	int32 NumTris() const { return Pos.Num() / 3; }
};

UCLASS()
class VILAAURORA_API UVilaProcMesh : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UVilaProcMesh* Get(const UObject* WorldContext);

	virtual void Deinitialize() override;

	// Malha em cache pela chave; `Fill` só roda na primeira vez.
	UStaticMesh* GetOrBuild(const FString& Key, TFunctionRef<void(FVilaProcBuilder&)> Fill);
	// Cria a UStaticMesh a partir dos triângulos (um slot de material, "Mat0").
	UStaticMesh* BuildMesh(const FString& Key, const FVilaProcBuilder& B);

	// Triângulo isósceles de dois lados: base de (-50,0,0) a (50,0,0), ápice em (0,100,0), normal ±Z.
	UStaticMesh* Triangle();
	// Tufo de grama (GrassField._make_tuft): 11 lâminas. UV.y e a alfa da cor = 0 na raiz, 1 na ponta.
	UStaticMesh* GrassTuft();
	// Maço de folhas (FoliageBatcher.add_canopy) num elipsoide de raio 100 cm centrado na origem.
	// `CardRatio`: tamanho do cartão / raio do maço. Variantes 0..2 (sementes diferentes).
	// UV no atlas 2x2 de folhas; cor de vértice = sombreamento, alfa = peso do vento.
	UStaticMesh* LeafCluster(int32 Variant, int32 Cards, float CardRatio);

private:
	UPROPERTY() TMap<FString, TObjectPtr<UStaticMesh>> Cache;
};
