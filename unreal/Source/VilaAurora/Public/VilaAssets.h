// Acesso centralizado aos assets. Equivale ao src/util/Mat.gd do Godot + carregamento de
// modelos. Os caminhos abaixo são o contrato com Scripts/build_content.py, que importa as
// texturas/modelos de assets/ e cria os materiais-mestre. Sem esse passo o jogo ainda roda:
// cada função cai num material de cor sólida (BasicShapeMaterial do motor) com a cor média
// da textura, e as malhas de carro/personagem retornam nullptr (os atores usam primitivas).
//
// Caminhos (/Game = unreal/Content):
//   /Game/Vila/Textures/T_<set>_A | _N | _ORM               (set = nome em assets/textures)
//   /Game/Vila/Materials/M_Surface                          projeção em caixa no espaço do mundo
//   /Game/Vila/Materials/M_SurfaceLocal                     o mesmo no espaço do objeto (peças que se movem)
//   /Game/Vila/Materials/M_Color | M_Emissive | M_Glass | M_CarPaint | M_Skin | M_Cloth | M_Hair
//   /Game/Vila/Materials/M_Eye | M_Leaf | M_Grass | M_Water | M_Windows | M_Rain | M_Outline
//   /Game/Vila/Materials/MPC_Vila                           escalares globais: Wetness, Night, Wind, Snow
//   /Game/Vila/Vehicles/<modelo>/SM_<Parte>                 modelo = hatch|sedan|pickup|van
//   /Game/Vila/Characters/<m|f>/SK_<Peça> | SKEL_Human | Anims/A_<anim>
//   /Game/Vila/Maps/Vila
// Parâmetros de material: ver Scripts/build_content.py (MATERIAL_PARAMS) — os nomes usados
// aqui (Albedo, Normal, ORM, Tint, TileMeters, Macro, Detile, SwapTop, Outdoor,
// NormalStrength, RoughBias, UseCustomTint, Color, Roughness, Metallic, Intensity, Opacity...)
// precisam bater com os de lá.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Vila/Core.h"

#include "VilaAssets.generated.h"

class UStaticMesh;
class USkeletalMesh;
class USkeleton;
class UAnimSequence;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UTexture2D;

UCLASS()
class VILAAURORA_API UVilaAssets : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UVilaAssets* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// true se o build_content.py já rodou (materiais-mestre presentes).
	bool HasContent() const { return bHasContent; }

	// ------------------------------------------------ malhas básicas do motor (pivô no centro, 100 cm)
	UStaticMesh* Cube() const { return CubeMesh; }
	UStaticMesh* Cylinder() const { return CylinderMesh; }
	UStaticMesh* Sphere() const { return SphereMesh; }
	UStaticMesh* Plane() const { return PlaneMesh; }  // 100x100, normal +Z
	UStaticMesh* Cone() const { return ConeMesh; }

	// ------------------------------------------------ materiais (compartilhados, cacheados)
	// Superfície com a direção de arte do Godot (Mat.SURFACES): "asphalt", "sidewalk", "plaza",
	// "concrete", "curb", "plaster", "plaster_in", "brick", "siding", "roof_tiles", "roof_tiles_z",
	// "roof_flat", "roof_fiber", "roof_fiber_z", "wood_floor", "porcelain", "wall_tiles", "wood",
	// "wood_out", "fabric", "metal", "steel", "corrugated", "canvas", "dirt", "rubber", ou o nome
	// direto de um conjunto de texturas. `bInstanceTint`: multiplica pela PerInstanceCustomData
	// 0..2 (para HISM com várias cores numa só malha).
	UMaterialInterface* Surface(const FString& Key, const FLinearColor& Tint = FLinearColor::White, bool bInstanceTint = false);
	// Projeção no espaço do objeto (Mat.textured): portas, carrinhos, móveis que se movem.
	UMaterialInterface* SurfaceLocal(const FString& Set, const FLinearColor& Tint = FLinearColor::White, float Scale = 1.f);
	UMaterialInterface* Color(const FLinearColor& Color, float Roughness = 0.85f, float Metallic = 0.f, bool bInstanceTint = false);
	UMaterialInterface* Emissive(const FLinearColor& Color, float Intensity = 1.5f);
	UMaterialInterface* Glass(const FLinearColor& Tint = FLinearColor(0.55f, 0.72f, 0.85f), float Opacity = 0.45f);
	// Mestres especiais sem parâmetros obrigatórios: "M_Water", "M_Grass", "M_Leaf", "M_Windows",
	// "M_Rain", "M_Outline"... nullptr se ausente.
	UMaterialInterface* Master(const FString& Name);

	// Instância dinâmica própria (para quem anima parâmetros: tinta do carro, pele, luzes).
	UMaterialInstanceDynamic* MakeMID(UMaterialInterface* Parent, UObject* Outer) const;
	// MID de um mestre com cor (cai no BasicShapeMaterial se faltar o mestre).
	UMaterialInstanceDynamic* MakeColorMID(const FString& MasterName, const FLinearColor& Color, UObject* Outer);

	// Escalares globais (MPC_Vila): Wetness 0..1, Night 0..1, Wind 0..1.
	void SetGlobal(const UObject* WorldContext, FName Param, float Value);
	float GetGlobal(FName Param) const;

	// ------------------------------------------------ modelos importados
	UStaticMesh* VehiclePart(const FString& Model, const FString& Part);
	// Layout das peças (pivôs, rodas, porta) exportado por tools/art/blender/export_unreal.py
	// em Content/Data/art/vehicles_layout.json. Objeto vazio se não existir.
	const vila::Json& VehicleLayout();
	USkeletalMesh* CharacterPiece(const FString& Sex, const FString& Piece);
	UAnimSequence* CharacterAnim(const FString& Sex, const FString& Anim);
	USkeleton* CharacterSkeleton(const FString& Sex);
	UTexture2D* Texture(const FString& Path);

	// Diretório de dados (Content/Data) em caminho absoluto, com barra final.
	static FString DataDir();

private:
	template <typename T>
	T* LoadCached(const FString& Path);

	UMaterialInterface* Fallback(const FLinearColor& Color);
	float SetTileMeters(const FString& Set);

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BasicMaterial;
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> Collection;

	// Tudo o que foi carregado/criado fica referenciado aqui (não é coletado pelo GC).
	UPROPERTY() TMap<FString, TObjectPtr<UObject>> Loaded;
	UPROPERTY() TMap<FString, TObjectPtr<UMaterialInterface>> Materials;

	TMap<FName, float> Globals;
	TMap<FString, float> TileCache;
	vila::Json Layout;
	bool bLayoutLoaded = false;
	bool bHasContent = false;
};
