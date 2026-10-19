// Personagem 3D montado em peças (port de Humanoid.gd): SK_Body é o líder e cada roupa, cabelo
// ou acessório é um USkeletalMeshComponent seguindo a pose dele (SetLeaderPoseComponent).
// A aparência liga/desliga peças, pinta os slots de material, aplica morphs (heavy, thin, old,
// face_a, face_b) e esconde a pele sob a roupa (parâmetro HideBits do M_Skin).
// Animações: idle, walk, run, talk, work, eat, search, sit, lie, knocked (UVilaAnimInstance).
// Sem o conteúdo importado, monta um boneco de cápsulas/cubos animado proceduralmente.
// A raiz fica nos pés e a frente é +X (a do ator).
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Vila/Core.h"

#include "VilaHumanoid.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UVilaAnimInstance;

UCLASS(ClassGroup = (Vila))
class VILAAURORA_API UVilaHumanoid : public USceneComponent
{
	GENERATED_BODY()

public:
	UVilaHumanoid();

	void Build(const vila::Json& Appearance);          // mesmo formato do Godot (visual_appearance)
	void SetPose(const FString& Pose);                 // "idle","walk","run","talk","work","eat","search","sit","lie","knocked"
	void SetMoveSpeed(float CmPerSec);                 // escolhe idle/walk/run e a velocidade da animação

	// Só a velocidade usada no ritmo dos ciclos walk/run (visual.speed do Godot, em m/s).
	void SetSpeed(float MetersPerSec) { SpeedMs = MetersPerSec; }
	// LOD distante: congela a animação (anim.speed_scale = 0).
	void SetLodFar(bool bFar);
	const FString& GetPose() const { return Pose; }
	const FString& GetSex() const { return Sex; }
	const vila::Json& GetAppearance() const { return Appearance; }
	bool UsesFallback() const { return bFallback; }

	static FString InferSex(const vila::Json& App);

	// Correção de yaw das malhas importadas (graus) para a frente ficar em +X.
	// O export do Blender deixa a frente em -Y do UE (convenção usual de personagens importados).
	UPROPERTY(EditAnywhere, Category = "Vila")
	float MeshYawOffset = -90.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void Clear();
	bool BuildSkeletal(const vila::Json& App);
	void BuildFallback(const vila::Json& App);
	void PlayCurrent(float Blend, float StartTime);
	float AnimRate() const;
	void TickFallback(float Dt);

	USceneComponent* AddPivot(USceneComponent* Parent, const FVector& Loc);
	UStaticMeshComponent* AddPart(USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Color, float Roughness = 0.85f);

	vila::Json Appearance = vila::Json::object();
	FString Pose = TEXT("idle");
	FString Sex = TEXT("m");
	float SpeedMs = 0.f;
	float HeightScale = 1.f;
	bool bElderly = false;
	bool bLodFar = false;
	bool bFallback = false;
	bool bBuilt = false;

	UPROPERTY() TObjectPtr<USceneComponent> ModelRoot;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Pieces;
	UPROPERTY() TObjectPtr<UVilaAnimInstance> Anim;

	// ---------------------------------------------------- boneco procedural (sem conteúdo)
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> FallbackParts;
	UPROPERTY() TObjectPtr<USceneComponent> FbRoot;
	UPROPERTY() TObjectPtr<USceneComponent> FbPelvis;
	UPROPERTY() TObjectPtr<USceneComponent> FbTorso;
	UPROPERTY() TObjectPtr<USceneComponent> FbHead;
	UPROPERTY() TObjectPtr<USceneComponent> FbArmL;
	UPROPERTY() TObjectPtr<USceneComponent> FbArmR;
	UPROPERTY() TObjectPtr<USceneComponent> FbForeL;
	UPROPERTY() TObjectPtr<USceneComponent> FbForeR;
	UPROPERTY() TObjectPtr<USceneComponent> FbThighL;
	UPROPERTY() TObjectPtr<USceneComponent> FbThighR;
	UPROPERTY() TObjectPtr<USceneComponent> FbShinL;
	UPROPERTY() TObjectPtr<USceneComponent> FbShinR;

	// Estado atual das juntas (graus / cm), suavizado em direção ao alvo de cada pose.
	struct FJoints
	{
		float ThighL = 0, ThighR = 0, ShinL = 0, ShinR = 0;
		float ArmL = 0, ArmR = 0, ForeL = 0, ForeR = 0, ArmRoll = 6;
		float Torso = 0, Head = 0;
		float PelvisZ = 92;
		float RootPitch = 0, RootX = 0, RootZ = 0;
	};
	FJoints Joints;
	float Phase = 0.f;
	bool bSnapPose = true;
};
