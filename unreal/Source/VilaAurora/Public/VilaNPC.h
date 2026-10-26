// NPC com rotina diária (port de NPC.gd, data/npcs.json): dorme, trabalha, compra, conversa,
// corre, pega ônibus, reage ao jogador, ao trânsito e à chuva, e testemunha crimes.
// Simulação em níveis: perto = física + animação; longe = deslocamento simplificado e tick
// reduzido; dentro de prédios fechados = oculto (apenas a agenda avança).
// O caminho vem da navmesh (AVilaWorld::FindPath, Recast; reta se falhar) e é seguido com o
// UCharacterMovementComponent.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Vila/Core.h"
#include "VilaInteractable.h"
#include "VilaWorldBuilder.h"

#include "VilaNPC.generated.h"

class AVilaDoor;
class AVilaFurniture;
class AVilaWorld;
class AVilaPlayerCharacter;
class UVilaHumanoid;

UCLASS()
class VILAAURORA_API AVilaNPC : public ACharacter, public IVilaInteractable
{
	GENERATED_BODY()

public:
	AVilaNPC();

	void Setup(const FString& InNpcId, const vila::Json& InDef, AVilaWorld* InWorld);
	FString NpcId;
	vila::Json Def;
	FString DisplayName() const;
	bool IsSleeping() const;
	bool CanWitness() const;
	void BeginTalk();
	void EndTalk();
	void Bark(const FString& Text);                    // balão de fala (o HUD desenha)
	void BarkLater(const FString& Text, float Delay);
	FString CurrentBark() const;                       // "" sem balão
	float BarkAlpha() const;                           // opacidade do balão (some no último segundo)
	void GoTo(const FString& InSpotId);
	void KnockDown(const FVector& FromDir, float Severity, bool bByPlayer, float Duration = -1.f);
	void HelpUp();
	void OnBumped(AActor* By);
	bool HasDoorAccess(const AVilaDoor* Door) const;
	FString HomeSpot() const;
	FString BuildingAtSpot() const;

	// Estado público (o AVilaWorld consulta, como no Godot):
	FString State = TEXT("idle");     // idle, moving, at_spot, talking, knocked, hidden
	FString Activity;                 // atividade da agenda (sleep, work, shop, chat, wander, jog...)
	FString SpotId;                   // ponto atual/destino ("__jog" no circuito de corrida)
	bool bIsAmbient = false;          // pedestre gerado (id "pedestre_N")
	bool bTalking = false;
	bool bLodFar = false;
	float WalkSpeed = 1.3f;           // m/s
	UPROPERTY() TObjectPtr<UVilaHumanoid> Humanoid;

	// Ponto atual (pos/rot/pose/oculto/prédio/móvel). nullptr se não houver.
	const FVilaSpot* CurrentSpot() const { return bHasSpot ? &Spot : nullptr; }
	// Pés do NPC (a origem do Godot). A origem do ator UE é o centro da cápsula.
	FVector FeetLocation() const;
	// Força a reavaliação da agenda (o _decide() que o GameWorld chamava após criar o NPC).
	void Decide();

	// IVilaInteractable
	virtual FVector GetInteractPoint() const override;
	virtual FString GetInteractTitle() const override;
	virtual float GetInteractRadius() const override { return 180.f; }
	virtual float GetFocusPriority() const override;
	virtual TArray<FVilaAction> GetActions(AVilaPlayerCharacter* Player) override;
	virtual void DoAction(AVilaPlayerCharacter* Player, const FString& ActionId) override;
	virtual void SetFocused(bool bFocused) override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AVilaWorld* GetVilaWorld() const;
	vila::Json ScheduleEntry() const;
	vila::Json EffectiveEntry() const;
	FString PlaceFor(const vila::Json& E) const;
	bool SpotData(const FString& Id, FVilaSpot& Out) const;
	void SnapTo(const FString& Id);
	void Arrive();
	void ApplyPose();
	void SitAtSpot();
	void LieAtSpot();
	void SetNpcHidden(bool bHide);
	void UpdateLod();
	void RefreshMoveIgnores();
	void StepMove(float Dt);
	void ProcessActivity(float Dt);
	void ReactToWorld();
	AVilaFurniture* MyCounter() const;
	void FirstAid();
	void GetUp();
	FString Pick(const char* Key) const;
	void SetBodyEnabled(bool bEnabled);
	void SetVisualOffset(float ZCm);
	void SetFeetLocation(const FVector& Feet);
	void FaceYaw(float Yaw, float Alpha);

	TWeakObjectPtr<AVilaWorld> VilaWorld;
	vila::Json Entry = vila::Json::object();
	UPROPERTY() FVilaSpot Spot;
	bool bHasSpot = false;

	TArray<FVector> Path;
	int32 PathI = 0;
	float DecideT = 0.f;
	bool bSnapPending = true;
	float ActivityT = 0.f;
	FString Sub;
	float StuckT = 0.f;
	FVector StuckPos = FVector::ZeroVector;
	int32 JogI = 0;
	float KnockT = 0.f;
	bool bHitByPlayer = false;
	float BarkCd = 0.f;
	float ReactionCd = 0.f;
	float ChatT = 0.f;
	int32 Bumps = 0;
	float WaitPlayerT = 0.f;
	FString BarkText;
	float BarkT = 0.f;
	bool bSetupDone = false;
	int32 ClockConn = 0;
	int32 IgnoredNpcCount = -1;
	TWeakObjectPtr<AActor> IgnoredPlayer;
};
