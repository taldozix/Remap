// Cena principal da partida (port de src/world/GameWorld.gd). Monta o bairro, cria jogador,
// NPCs, veículos e trânsito, e faz a ponte espacial entre os sistemas de simulação
// (testemunhas, invasão de domicílio, atropelamentos, cortes de parede, marcadores de missão).
// Implementa vila::IWorld: o núcleo o consulta via vila::game().world.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Vila/Core.h"
#include "Vila/World.h"
#include "VilaConv.h"
#include "VilaInteractable.h"

#include "VilaWorld.generated.h"

class AVilaBuilding;
class AVilaCameraRig;
class AVilaDeliveryDrop;
class AVilaDoor;
class AVilaEnvironment;
class AVilaFurniture;
class AVilaGroundItem;
class AVilaNPC;
class AVilaPlayerCharacter;
class AVilaTrafficSystem;
class AVilaVehicle;
class UVilaWorldBuilder;
struct FVilaSpot;

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

UCLASS()
class VILAAURORA_API AVilaWorld : public AActor, public vila::IWorld
{
	GENERATED_BODY()

public:
	AVilaWorld();

	static AVilaWorld* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ------------------------------------------------ estado
	UPROPERTY() TObjectPtr<UVilaWorldBuilder> Builder;
	UPROPERTY() TObjectPtr<AVilaPlayerCharacter> Player;  // nullptr no menu
	UPROPERTY() TObjectPtr<AVilaCameraRig> CameraRig;
	UPROPERTY() TObjectPtr<AVilaTrafficSystem> Traffic;
	UPROPERTY() TObjectPtr<AVilaEnvironment> Environment;
	UPROPERTY() TMap<FString, TObjectPtr<AVilaNPC>> NPCs;
	UPROPERTY() TMap<FString, TObjectPtr<AVilaVehicle>> Vehicles;
	FString PlayerBuilding;  // prédio onde o jogador está ("" = rua)
	bool bReady = false;
	bool IsMenu() const { return bMenu; }

	// ------------------------------------------------ consultas
	bool UIBlocking() const;
	float CameraYaw() const;
	float GroundHeight(float X, float Y) const;
	bool IsInsideBuilding(const FVector& P, const FString& BuildingId) const;
	FString BuildingAt(const FVector& P) const;
	TArray<AVilaNPC*> NPCsNear(const FVector& P, float RadiusCm) const;
	TArray<FVector> FindPath(const FVector& From, const FVector& To) const;
	float NPCLodDistance() const;
	AVilaNPC* ChatPartner(AVilaNPC* Npc) const;
	vila::Json ConversationLines() const;
	FString ShopUnavailableReason(const FString& ShopId) const;
	AVilaFurniture* CounterForStaff(const FString& SpotId) const;
	AVilaNPC* StaffAt(const FString& SpotId) const;
	AVilaVehicle* PlayerVehicleNear(const FVector& P, float RadiusCm) const;
	AVilaVehicle* NearestVehicle(const FVector& P, float RadiusCm) const;
	const FVilaSpot* Spot(const FString& Id) const;
	TArray<FVector> MarkerPositions() const;
	TArray<FVilaMarker> Markers() const;  // com rótulos ("Entrega", nome do NPC)

	// ------------------------------------------------ ações
	void DropItem(int32 Index, int32 Qty);
	void PickGroundItem(AVilaGroundItem* Item);
	void StartDialogue(AVilaNPC* Npc);
	void DeliverDriveOrder();
	void ShowShiftSummary(const vila::Json& Summary);
	void DoSleep(int32 Hours, const FString& BedId);
	void OnNPCHit(AVilaNPC* Npc, float Severity);
	void OnNPCHelped(AVilaNPC* Npc);
	void OnHorn(const FVector& P);
	void OnKnock(const FString& BuildingId);
	void OnPlayerUsedKey(const FString& BuildingId);
	void OnContainerTake(const FString& ContainerId);
	void RefreshDelivery();
	TArray<FVilaAction> ExtraNPCActions(AVilaNPC* Npc);
	bool DoExtraNPCAction(AVilaNPC* Npc, const FString& ActionId);

	// ------------------------------------------------ vila::IWorld
	virtual bool player_outdoors() const override;
	virtual bool is_player_in_job_area() const override;
	virtual void on_player_fainted() override;
	virtual void on_shift_ended(const vila::Json& Summary) override;
	virtual std::vector<std::string> find_witnesses(const vila::Vec3& Pos, double Radius) const override;
	virtual bool is_vehicle_near_spot(const std::string& VehicleId, const std::string& SpotId, double Radius) const override;
	virtual void refresh_vehicle(const std::string& VehicleId) override;
	virtual void sync_to_state() override;
	virtual std::string save_block_reason() const override;
	virtual vila::Json start_event(const std::string& Id, const vila::Json& Def) override;
	virtual void end_event(const std::string& Id, const vila::Json& Info) override;

private:
	// criação
	void SpawnGroundItem(const vila::Json& Entry);
	void SpawnVehicles();
	void SpawnPlayer();
	void SpawnNPCs();
	void SpawnNPC(const FString& Id, const vila::Json& Def);
	void SetupMenu();
	void DispatchPendingBeginPlay();
	void PostBeginSetup();
	void FinishStartup();

	// quadro a quadro
	void UpdateCutaway();
	void ResolveIncident(const FVilaIncident& Inc);
	void CheckTrespass(float Delta);
	void ProcessEvents(float Delta);
	void NotifyNear(const vila::Json& Def, const FVector& P);
	void SleepStep(int32 Hours);
	void SleepDone(double Slept, const FString& WokeReason);
	FVector PlayerFeet() const;

	UPROPERTY() TObjectPtr<AVilaDeliveryDrop> Drop;
	UPROPERTY() TArray<TObjectPtr<AVilaGroundItem>> GroundItems;

	bool bMenu = false;
	bool bPostBegin = false;
	bool bWaitingNav = false;
	float NavWaitT = 0.f;
	int32 NavWaitFrames = 0;
	TArray<FVilaIncident> Incidents;
	float TrespassT = 0.f;
	TSet<FString> TrespassReported;
	float StaffBarkT = 0.f;
	float CutT = 0.f;
	float ArgueT = 0.f;
	int32 ArgueI = 0;
	bool bFainting = false;
	bool bSleeping = false;
	FTimerHandle SleepTimer;
	double SavedTimeScale = 1.0;

	int32 AppearanceConn = 0;
};
