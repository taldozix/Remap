#include "VilaWorld.h"

#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaBuilding.h"
#include "VilaCameraRig.h"
#include "VilaDeliveryDrop.h"
#include "VilaDoor.h"
#include "VilaEnvironment.h"
#include "VilaFurniture.h"
#include "VilaGameInstance.h"
#include "VilaGameMode.h"
#include "VilaGroundItem.h"
#include "VilaHUD.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaTrafficSystem.h"
#include "VilaVehicle.h"
#include "VilaWorldBuilder.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Saves.h"
#include "Vila/Settings.h"

using namespace VilaConv;
using VilaW::Trf;

namespace
{
	const TCHAR* AmbientNames[] = {TEXT("Carla"), TEXT("Rogério"), TEXT("Fernanda"), TEXT("Seu Zé"), TEXT("Bianca"), TEXT("Diego")};
	constexpr float FadeSeconds = 0.45f;     // AVilaHUD::Fade dura 0,4 s
	constexpr double MenuTimeScale = 4.0;    // relógio do cenário do menu (min de jogo por s)
	constexpr float NavWaitTimeout = 3.f;

	bool InList(const std::string& V, std::initializer_list<const char*> L)
	{
		for (const char* Item : L)
			if (V == Item) return true;
		return false;
	}
}

// ================================================================ utilitários
namespace VilaW
{
	FVector Feet(const AActor* Actor)
	{
		if (!Actor) return FVector::ZeroVector;
		if (const ACharacter* C = Cast<ACharacter>(Actor))
			if (const UCapsuleComponent* Cap = C->GetCapsuleComponent())
				return C->GetActorLocation() - FVector(0.0, 0.0, Cap->GetScaledCapsuleHalfHeight());
		return Actor->GetActorLocation();
	}

	void SetFeet(AActor* Actor, const FVector& FeetPos)
	{
		if (!Actor) return;
		FVector P = FeetPos;
		if (const ACharacter* C = Cast<ACharacter>(Actor))
			if (const UCapsuleComponent* Cap = C->GetCapsuleComponent()) P.Z += Cap->GetScaledCapsuleHalfHeight();
		Actor->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
	}

	FLinearColor SRGB(float R, float G, float B)
	{
		return FLinearColor(FColor(static_cast<uint8>(FMath::Clamp(R, 0.f, 1.f) * 255.f + 0.5f), static_cast<uint8>(FMath::Clamp(G, 0.f, 1.f) * 255.f + 0.5f),
			static_cast<uint8>(FMath::Clamp(B, 0.f, 1.f) * 255.f + 0.5f)));
	}

	FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& BLinear, float T)
	{
		const FColor CA = A.ToFColor(true);
		const FColor CB = BLinear.ToFColor(true);
		return SRGB(FMath::Lerp(CA.R / 255.f, CB.R / 255.f, T), FMath::Lerp(CA.G / 255.f, CB.G / 255.f, T), FMath::Lerp(CA.B / 255.f, CB.B / 255.f, T));
	}

	void SetActorOutline(AActor* Actor, bool bOn)
	{
		if (!Actor) return;
		TArray<UPrimitiveComponent*> Prims;
		Actor->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* P : Prims)
		{
			if (!P) continue;
			P->SetRenderCustomDepth(bOn);
			P->SetCustomDepthStencilValue(bOn ? 1 : 0);
		}
	}

	FString PickChatter(const std::string& Key)
	{
		const vila::Json& L = vila::jarr(vila::data().chatter(), Key);
		if (L.empty()) return FString();
		return Tr(vila::to_str(L[static_cast<size_t>(FMath::RandRange(0, static_cast<int32>(L.size()) - 1))]));
	}
}

// ================================================================ ciclo de vida
AVilaWorld::AVilaWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AVilaWorld* AVilaWorld::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	if (const AVilaGameMode* GM = W->GetAuthGameMode<AVilaGameMode>())
		if (AVilaWorld* VW = GM->GetVilaWorld()) return VW;
	for (TActorIterator<AVilaWorld> It(W); It; ++It)
		return *It;
	return nullptr;
}

void AVilaWorld::BeginPlay()
{
	Super::BeginPlay();
	const UVilaGameInstance* GI = UVilaGameInstance::Get(this);
	bMenu = GI && GI->GetFlow() == EVilaFlow::Menu;

	if (!bMenu) vila::game().world = this;
	vila::clock().running = false;
	vila::clock().reset_holds();
	SavedTimeScale = vila::clock().time_scale;

	// Construção da vila (W1). O builder cria prédios, portas e móveis.
	Builder = NewObject<UVilaWorldBuilder>(this, TEXT("Builder"));
	Builder->Build(this, vila::data().map());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Environment = GetWorld()->SpawnActor<AVilaEnvironment>(AVilaEnvironment::StaticClass(), FTransform::Identity, Params);
	if (Environment) Environment->Setup(this);

	// A grama (AVilaGrass) é criada pelo builder e se reconstrói sozinha quando a densidade
	// muda nas opções (Settings::changed), como o _on_settings_changed do GameWorld.gd.

	for (const vila::Json& G : vila::game().world_state.ground_items) SpawnGroundItem(G);
	SpawnVehicles();

	if (bMenu)
	{
		SetupMenu();
	}
	else
	{
		SpawnPlayer();
		CameraRig = GetWorld()->SpawnActor<AVilaCameraRig>(AVilaCameraRig::StaticClass(), FTransform::Identity, Params);
		if (CameraRig && Player)
		{
			CameraRig->Target = Player.Get();
			CameraRig->Snap();
		}
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->bAutoManageActiveCameraTarget = false;
			if (Player) PC->Possess(Player.Get());
			if (CameraRig) PC->SetViewTargetWithBlend(CameraRig.Get(), 0.f);
		}
	}
	// A navmesh só fica pronta depois de construída: os NPCs e o trânsito esperam (Tick).
	bWaitingNav = true;
	NavWaitT = 0.f;
	NavWaitFrames = 0;
}

void AVilaWorld::SetupMenu()
{
	// Cenário do menu: câmera orbitando a praça, relógio acelerado, sem simulação.
	FVector Center = FVector::ZeroVector;
	for (const vila::Json& P : vila::jarr(vila::data().map(), "props"))
		if (vila::jstr(P, "type") == "fountain")
		{
			Center = JsonPos(vila::jget(P, "pos"));
			break;
		}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CameraRig = GetWorld()->SpawnActor<AVilaCameraRig>(AVilaCameraRig::StaticClass(), FTransform::Identity, Params);
	if (CameraRig) CameraRig->SetMenuOrbit(Center, 3500.f);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (CameraRig) PC->SetViewTargetWithBlend(CameraRig.Get(), 0.f);
	}
	if (Environment) Environment->SetMenuMode(true);
	if (AVilaHUD* HUD = AVilaHUD::Get(this)) HUD->ShowMainMenu();
	// Fim de tarde para o fundo (a partida nova ou carregada redefine o relógio).
	vila::clock().total_minutes = (vila::clock().day() - 1) * 1440.0 + 16.5 * 60.0;
}

void AVilaWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	vila::events().appearance_changed.disconnect(AppearanceConn);
	AppearanceConn = 0;
	GetWorldTimerManager().ClearTimer(SleepTimer);
	if (vila::game().world == this) vila::game().world = nullptr;
	vila::clock().running = false;
	vila::clock().time_scale = SavedTimeScale;
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->StopAllAmbient();
	Super::EndPlay(EndPlayReason);
}

// Atores criados durante o BeginPlay do mundo (antes de o UWorld marcar o início do jogo)
// podem não ter recebido BeginPlay ainda; garante isso antes de usá-los.
void AVilaWorld::DispatchPendingBeginPlay()
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* A = *It;
		if (IsValid(A) && A->GetLevel() == GetLevel() && !A->HasActorBegunPlay() && !A->IsActorBeingDestroyed()) A->DispatchBeginPlay();
	}
}

// Parte do _ready que depende do jogador já iniciado (entrar no carro salvo).
void AVilaWorld::PostBeginSetup()
{
	if (!Player) return;
	const FString Vid = S(vila::game().player.vehicle_id);
	if (!Vid.IsEmpty())
		if (AVilaVehicle* V = Vehicles.FindRef(Vid)) Player->EnterVehicle(V);
	if (CameraRig)
	{
		const bool bDriving = Player->State == TEXT("driving") && Player->Vehicle;
		CameraRig->Target = bDriving ? static_cast<AActor*>(Player->Vehicle.Get()) : static_cast<AActor*>(Player.Get());
		CameraRig->Snap();
	}
}

void AVilaWorld::FinishStartup()
{
	bWaitingNav = false;
	SpawnNPCs();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Traffic = GetWorld()->SpawnActor<AVilaTrafficSystem>(AVilaTrafficSystem::StaticClass(), FTransform::Identity, Params);
	if (Traffic) Traffic->Setup(this, vila::data().map());
	RefreshDelivery();
	TWeakObjectPtr<AVilaWorld> Self(this);
	AppearanceConn = vila::events().appearance_changed.connect([Self]() {
		if (Self.IsValid() && Self->Player) Self->Player->RebuildVisual();
	});
	bReady = true;
	if (bMenu)
	{
		// Só cenário: o relógio anda rápido, mas game().world é nullptr (o núcleo não simula).
		vila::clock().time_scale = MenuTimeScale;
		vila::clock().running = true;
		return;
	}
	vila::clock().running = true;
	if (UVilaAudio* Audio = UVilaAudio::Get(this))
	{
		Audio->StopMusic();
		Audio->SetAmbient(TEXT("city"), true, -16.f);
	}
	vila::events().world_ready.emit();
	vila::Game& G = vila::game();
	if (vila::has(G.pending_load, "new"))
	{
		std::string Intro = vila::tr("Segunda-feira, 7h30. Primeira manhã na casa nova.\n\nTem um bilhete do senhorio na mesa da cozinha. A geladeira tem pouca coisa, a carteira tem R$ 60 e o aluguel vence no domingo.");
		if (vila::truthy(vila::settings().get_value("game", "show_hints")))
		{
			const vila::Settings& St = vila::settings();
			Intro += "\n\n" + vila::fmt(vila::tr("Dica: [%s] interage, [%s] entra/sai do carro, [%s] abre o inventário, [%s] o diário, [%s] o mapa, [%s]/[%s] giram a câmera e [%s] pausa (salvar/carregar)."),
				{St.primary_key_text("interact"), St.primary_key_text("vehicle"), St.primary_key_text("inventory"), St.primary_key_text("journal"), St.primary_key_text("map"),
					St.primary_key_text("cam_left"), St.primary_key_text("cam_right"), St.primary_key_text("pause")});
		}
		vila::events().request_message.emit(vila::tr("Vila Aurora, Porto Serra"), Intro);
	}
	else if (vila::has(G.pending_load, "loaded"))
	{
		vila::events().notify.emit(vila::tr("Jogo carregado."), "good");
		vila::events().game_loaded.emit();
	}
	G.pending_load = vila::Json::object();
}

// ================================================================ criação
void AVilaWorld::SpawnPlayer()
{
	const vila::PlayerState& PS = vila::game().player;
	const FVector FeetPos = ToUE(PS.position + vila::Vec3(0.0, 0.05, 0.0));
	float Half = 88.f;
	if (const AVilaPlayerCharacter* CDO = GetDefault<AVilaPlayerCharacter>())
		if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent()) Half = Cap->GetScaledCapsuleHalfHeight();
	// rotation_y fica em radianos do Godot no estado do núcleo.
	const FRotator Rot(0.0, YawFromGodot(FMath::RadiansToDegrees(PS.rotation_y)), 0.0);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Player = GetWorld()->SpawnActor<AVilaPlayerCharacter>(AVilaPlayerCharacter::StaticClass(), FeetPos + FVector(0.0, 0.0, Half), Rot, Params);
	if (Player) Player->PlaceFromState();
	// A entrada no veículo salvo acontece em PostBeginSetup (depois do BeginPlay do jogador).
}

void AVilaWorld::SpawnVehicles()
{
	for (const auto& [Id, St] : vila::game().world_state.vehicles.items())
	{
		const vila::Json& P = vila::jarr(St, "pos");
		double X = 0.0, Z = 0.0;
		if (P.size() >= 3)
		{
			X = vila::to_num(P[0]);
			Z = vila::to_num(P[2]);
		}
		else if (P.size() == 2)
		{
			X = vila::to_num(P[0]);
			Z = vila::to_num(P[1]);
		}
		const FTransform T(FRotator(0.0, YawFromGodot(vila::jnum(St, "rot")), 0.0), Map2D(X, Z));
		AVilaVehicle* V = GetWorld()->SpawnActorDeferred<AVilaVehicle>(AVilaVehicle::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!V) continue;
		V->Setup(S(Id), this);  // como no Godot: setup antes do _ready
		V->FinishSpawning(T);
		Vehicles.Add(S(Id), V);
	}
}

void AVilaWorld::SpawnNPCs()
{
	const double Density = vila::to_num(vila::settings().get_value("graphics", "npc_density"), 1.0);
	for (const auto& [Id, Def] : vila::data().npcs().items()) SpawnNPC(S(Id), Def);
	const int32 Ambient = FMath::RoundToInt(6.0 * Density);
	static const char* Skins[] = {"#f0c8a8", "#c68e6a", "#8a5a3c", "#5e3b28", "#e0b494", "#a8765a"};
	static const char* Hairs[] = {"#1a1a1a", "#6b3d1e", "#d8c080", "#2b1d12", "#9a9a9a", "#a0522d"};
	static const char* Styles[] = {"short", "long", "curly", "bob", "bald", "ponytail"};
	static const char* Shirts[] = {"#d35d5d", "#5d8fd3", "#e0c050", "#60a070", "#9a6ad0", "#e08a40"};
	static const char* Pants[] = {"#2f3a4a", "#3a3a3a", "#4a5a7a", "#5a4a3a"};
	for (int32 i = 0; i < Ambient; ++i)
	{
		vila::Json App = {
			{"skin", Skins[i % 6]},
			{"hair", Hairs[(i * 5) % 6]},
			{"hair_style", Styles[i % 6]},
			{"shirt", Shirts[(i * 7) % 6]},
			{"pants", Pants[i % 4]},
			{"height", 0.95 + (i % 3) * 0.04},
			{"build", 0.9 + (i % 4) * 0.06},
		};
		const double Start = 6.5 + (i % 4) * 0.5;
		const double End = 19.5 + (i % 3) * 0.7;
		vila::Json Def = {
			{"name", U(AmbientNames[i % 6])},
			{"role", "Morador(a)"},
			{"dialogue", "pedestre"},
			{"appearance", App},
			{"walk_speed", 1.25 + (i % 3) * 0.1},
			{"schedule", vila::Json::array({
				vila::Json{{"from", End}, {"to", Start}, {"place", "predio_interior"}, {"activity", "sleep"}},
				vila::Json{{"from", Start}, {"to", End}, {"place", ""}, {"activity", "wander"}},
			})},
		};
		SpawnNPC(FString::Printf(TEXT("pedestre_%d"), i), Def);
	}
}

void AVilaWorld::SpawnNPC(const FString& Id, const vila::Json& Def)
{
	float Half = 85.f;
	if (const AVilaNPC* CDO = GetDefault<AVilaNPC>())
		if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent()) Half = Cap->GetScaledCapsuleHalfHeight();
	const FTransform T(FVector(0.0, 0.0, Half));
	AVilaNPC* N = GetWorld()->SpawnActorDeferred<AVilaNPC>(AVilaNPC::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!N) return;
	N->Setup(Id, Def, this);  // o NPC decide a posição inicial (snap) pela agenda
	N->FinishSpawning(T);
	NPCs.Add(Id, N);
	N->Decide();  // n._decide() do GameWorld.gd
	// No Godot as portas (camada 2) não colidem com NPCs (máscara 1): eles atravessam.
	if (Builder)
		if (UCapsuleComponent* Cap = N->GetCapsuleComponent())
			for (const auto& KV : Builder->Doors)
				if (AVilaDoor* D = KV.Value) Cap->IgnoreActorWhenMoving(D, true);
}

void AVilaWorld::SpawnGroundItem(const vila::Json& Entry)
{
	const vila::Json& P = vila::jarr(Entry, "pos");
	const double X = P.size() >= 1 ? vila::to_num(P[0]) : 0.0;
	const double Z = P.size() >= 3 ? vila::to_num(P[2]) : (P.size() == 2 ? vila::to_num(P[1]) : 0.0);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVilaGroundItem* GI = GetWorld()->SpawnActor<AVilaGroundItem>(AVilaGroundItem::StaticClass(), Map2D(X, Z), FRotator::ZeroRotator, Params);
	if (!GI) return;
	GI->Setup(Entry, this);
	GroundItems.Add(GI);
}

void AVilaWorld::DropItem(int32 Index, int32 Qty)
{
	if (!Player) return;
	vila::PlayerState& PS = vila::game().player;
	vila::Inventory& Inv = PS.inventory;
	if (Index < 0 || Index >= static_cast<int32>(Inv.items.size())) return;
	const std::string Id = vila::jstr(Inv.items[static_cast<size_t>(Index)], "id");
	if (PS.is_equipped(Id) && Inv.count(Id) <= Qty) PS.unequip(vila::jstr(vila::data().item(Id), "slot", ""));
	const vila::Json Moved = Inv.take_at(Index, Qty);
	if (Moved.empty()) return;
	const FVector P = PlayerFeet() + Player->GetActorForwardVector() * 60.0;
	const vila::Vec3 G = FromUE(P);
	vila::Json E = {{"id", vila::jstr(Moved, "id")}, {"qty", vila::jint(Moved, "qty")}, {"age", vila::jnum(Moved, "age")}, {"pos", vila::Json::array({G.x, 0.0, G.z})}};
	vila::game().world_state.ground_items.push_back(E);
	SpawnGroundItem(E);
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(TEXT("drop"));
}

void AVilaWorld::PickGroundItem(AVilaGroundItem* Item)
{
	if (!Item) return;
	const vila::Json& E = Item->Entry;
	const std::string Id = vila::jstr(E, "id");
	const int Qty = vila::jint(E, "qty");
	vila::PlayerState& PS = vila::game().player;
	if (!PS.inventory.can_fit(Id, Qty, PS.capacity() * 1.5))
	{
		vila::events().notify.emit(vila::tr("Pesado demais para carregar."), "bad");
		return;
	}
	// No Godot a entrada é o mesmo Dictionary de ground_items (a idade envelhece junto);
	// aqui procuramos a entrada correspondente por id, quantidade e posição.
	vila::Json& Items = vila::game().world_state.ground_items;
	const vila::Json& EP = vila::jarr(E, "pos");
	double Age = vila::jnum(E, "age");
	for (size_t i = 0; i < Items.size(); ++i)
	{
		const vila::Json& G = Items[i];
		const vila::Json& GP = vila::jarr(G, "pos");
		if (vila::jstr(G, "id") != Id || vila::jint(G, "qty") != Qty || GP.size() != EP.size()) continue;
		bool bSame = true;
		for (size_t k = 0; k < GP.size(); ++k)
			if (FMath::Abs(vila::to_num(GP[k]) - vila::to_num(EP[k])) > 0.001) bSame = false;
		if (!bSame) continue;
		Age = vila::jnum(G, "age", Age);
		Items.erase(Items.begin() + static_cast<std::ptrdiff_t>(i));
		break;
	}
	PS.inventory.add(Id, Qty, Age, false);
	GroundItems.Remove(Item);
	Item->Destroy();
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(TEXT("pickup"));
}

// ================================================================ consultas
FVector AVilaWorld::PlayerFeet() const
{
	return VilaW::Feet(Player);
}

bool AVilaWorld::UIBlocking() const
{
	const AVilaHUD* HUD = AVilaHUD::Get(this);
	return HUD && HUD->IsModalOpen();
}

float AVilaWorld::CameraYaw() const
{
	return CameraRig ? CameraRig->MoveYaw() : 0.f;
}

float AVilaWorld::GroundHeight(float X, float Y) const
{
	return Builder ? Builder->GroundHeight(X, Y) : 0.f;
}

bool AVilaWorld::IsInsideBuilding(const FVector& P, const FString& BuildingId) const
{
	if (!Builder) return false;
	const TObjectPtr<AVilaBuilding>* B = Builder->Buildings.Find(BuildingId);
	return B && *B && (*B)->Contains(P);
}

FString AVilaWorld::BuildingAt(const FVector& P) const
{
	if (!Builder) return FString();
	for (const auto& KV : Builder->Buildings)
		if (KV.Value && KV.Value->Contains(P)) return KV.Key;
	return FString();
}

TArray<AVilaNPC*> AVilaWorld::NPCsNear(const FVector& P, float RadiusCm) const
{
	TArray<AVilaNPC*> Out;
	for (const auto& KV : NPCs)
	{
		AVilaNPC* N = KV.Value;
		if (N && !N->IsHidden() && VilaW::Dist2D(VilaW::Feet(N), P) <= RadiusCm) Out.Add(N);
	}
	return Out;
}

TArray<FVector> AVilaWorld::FindPath(const FVector& From, const FVector& To) const
{
	TArray<FVector> Out;
	AVilaWorld* Self = const_cast<AVilaWorld*>(this);
	if (const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(Self, From, To))
		if (Path->IsValid()) Out = Path->PathPoints;
	if (Out.Num() == 0) Out.Add(To);
	return Out;
}

float AVilaWorld::NPCLodDistance() const
{
	return static_cast<float>((36.0 + vila::to_num(vila::settings().get_value("graphics", "view_distance")) * 14.0) * M);
}

AVilaNPC* AVilaWorld::ChatPartner(AVilaNPC* Npc) const
{
	if (!Npc) return nullptr;
	for (const auto& KV : NPCs)
	{
		AVilaNPC* N = KV.Value;
		if (N && N != Npc && N->State == TEXT("at_spot") && N->Activity == TEXT("chat") && VilaW::Dist2D(VilaW::Feet(N), VilaW::Feet(Npc)) < 350.f) return N;
	}
	return nullptr;
}

vila::Json AVilaWorld::ConversationLines() const
{
	// Conversas espontâneas refletem o que o jogador fez (fofoca do bairro).
	const vila::Json& C = vila::data().chatter();
	auto Pick = [&C](const char* Key) -> std::string {
		const vila::Json& L = vila::jarr(C, Key);
		return L.empty() ? std::string() : vila::tr(vila::to_str(L[static_cast<size_t>(FMath::RandRange(0, static_cast<int32>(L.size()) - 1))]));
	};
	const int Rep = vila::game().relations.reputation;
	if (FMath::FRand() < 0.35f)
	{
		bool bHit = false;
		for (const vila::Json& E : vila::game().law.record)
			if (vila::jstr(E, "type") == "atropelamento") bHit = true;
		if (bHit) return vila::Json::array({Pick("gossip_hit"), vila::tr("Que perigo. Tem que tomar cuidado.")});
		if (Rep >= 15) return vila::Json::array({Pick("gossip_good"), vila::tr("É, parece gente boa mesmo.")});
		if (Rep <= -10) return vila::Json::array({Pick("gossip_bad"), vila::tr("Vou trancar tudo hoje.")});
	}
	if (vila::jstr(vila::game().world_state.weather, "state") == "chuva") return vila::Json::array({Pick("rain"), vila::tr("Pois é...")});
	const vila::Json& Pairs = vila::jarr(C, "chat_pairs");
	if (Pairs.empty()) return vila::Json::array({"", ""});
	const vila::Json& Pair = Pairs[static_cast<size_t>(FMath::RandRange(0, static_cast<int32>(Pairs.size()) - 1))];
	return vila::Json::array({vila::tr(Pair.size() > 0 ? vila::to_str(Pair[0]) : ""), vila::tr(Pair.size() > 1 ? vila::to_str(Pair[1]) : "")});
}

FString AVilaWorld::ShopUnavailableReason(const FString& ShopId) const
{
	// Loja funciona se está no horário, se o atendente está no balcão e se aceita o jogador.
	const std::string Sid = U(ShopId);
	const vila::Json& Sh = vila::data().shop(Sid);
	if (Sh.empty()) return Tr("Loja inválida");
	if (!vila::game().shops.is_open(Sid, vila::clock().hour_f()))
	{
		const vila::Json& Hours = vila::jarr(Sh, "hours");
		return Trf("Fechado (abre às %dh)", Hours.empty() ? 0 : vila::to_int(Hours[0]));
	}
	bool bStaff = false;
	if (Builder)
		for (const auto& BKV : Builder->Buildings)
		{
			if (!BKV.Value) continue;
			for (const vila::Json& F : vila::jarr(BKV.Value->Def, "furniture"))
			{
				if (vila::jstr(F, "shop") != Sid) continue;
				const FString StaffSpot = S(vila::jstr(F, "staff_spot"));
				for (const auto& KV : NPCs)
					if (KV.Value && KV.Value->SpotId == StaffSpot && KV.Value->State == TEXT("at_spot")) bStaff = true;
			}
		}
	if (!bStaff) return Tr("Ninguém no balcão agora");
	return S(vila::game().shops.refuses_service(Sid, vila::game().relations));
}

AVilaFurniture* AVilaWorld::CounterForStaff(const FString& SpotId) const
{
	if (SpotId.IsEmpty() || !Builder) return nullptr;
	for (const auto& KV : Builder->Furniture)
	{
		AVilaFurniture* F = KV.Value;
		if (F && F->Kind == TEXT("counter") && S(vila::jstr(F->Def, "staff_spot")) == SpotId) return F;
	}
	return nullptr;
}

AVilaNPC* AVilaWorld::StaffAt(const FString& SpotId) const
{
	if (SpotId.IsEmpty()) return nullptr;
	for (const auto& KV : NPCs)
	{
		AVilaNPC* N = KV.Value;
		if (N && N->SpotId == SpotId && N->State == TEXT("at_spot") && !N->IsSleeping()) return N;
	}
	return nullptr;
}

AVilaVehicle* AVilaWorld::PlayerVehicleNear(const FVector& P, float RadiusCm) const
{
	for (const auto& KV : Vehicles)
	{
		AVilaVehicle* V = KV.Value;
		if (V && V->IsPlayers() && VilaW::Dist2D(V->GetActorLocation(), P) <= RadiusCm) return V;
	}
	return nullptr;
}

AVilaVehicle* AVilaWorld::NearestVehicle(const FVector& P, float RadiusCm) const
{
	AVilaVehicle* Best = nullptr;
	float BD = RadiusCm;
	for (const auto& KV : Vehicles)
	{
		AVilaVehicle* V = KV.Value;
		if (!V) continue;
		const float D = VilaW::Dist2D(V->GetInteractPoint(), P);
		if (D <= BD)
		{
			BD = D;
			Best = V;
		}
	}
	return Best;
}

const FVilaSpot* AVilaWorld::Spot(const FString& Id) const
{
	return Builder ? Builder->Spots.Find(Id) : nullptr;
}

TArray<FVilaMarker> AVilaWorld::Markers() const
{
	TArray<FVilaMarker> Out;
	const vila::Json& A = vila::game().delivery.active;
	if (!A.empty())
		if (const FVilaSpot* Sp = Spot(S(vila::jstr(A, "spot")))) Out.Add({Sp->Location + FVector(0.0, 0.0, 150.0), Tr("Entrega")});
	for (const vila::Json& MarkerJ : vila::game().quests.active_markers())
	{
		TArray<FString> Parts;
		S(vila::to_str(MarkerJ)).ParseIntoArray(Parts, TEXT(":"), false);
		if (Parts.Num() != 2) continue;
		if (Parts[0] == TEXT("npc"))
			if (const TObjectPtr<AVilaNPC>* N = NPCs.Find(Parts[1]))
				if (*N)
				{
					const FVilaSpot* NS = (*N)->CurrentSpot();
					const FVector P = !(*N)->IsHidden() || !NS ? VilaW::Feet(*N) : NS->Location;
					Out.Add({P + FVector(0.0, 0.0, 240.0), (*N)->DisplayName()});
				}
	}
	return Out;
}

TArray<FVector> AVilaWorld::MarkerPositions() const
{
	TArray<FVector> Out;
	for (const FVilaMarker& Mk : Markers()) Out.Add(Mk.Pos);
	return Out;
}

// ================================================================ vila::IWorld
bool AVilaWorld::player_outdoors() const
{
	return Player && PlayerBuilding.IsEmpty() && Player->State != TEXT("driving");
}

bool AVilaWorld::is_player_in_job_area() const
{
	if (!Player || !Builder) return false;
	const TObjectPtr<AVilaBuilding>* B = Builder->Buildings.Find(TEXT("brasa"));
	return B && *B && (*B)->Contains(PlayerFeet(), 100.f);
}

std::vector<std::string> AVilaWorld::find_witnesses(const vila::Vec3& Pos, double Radius) const
{
	// Testemunhas: NPCs acordados, visíveis, no raio, com linha de visão livre.
	std::vector<std::string> Out;
	const UWorld* W = GetWorld();
	if (!W) return Out;
	const FVector P = ToUE(Pos);
	for (const auto& KV : NPCs)
	{
		AVilaNPC* N = KV.Value;
		if (!N || !N->CanWitness()) continue;
		const FVector NF = VilaW::Feet(N);
		if (VilaW::Dist2D(NF, P) > Radius * M) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VilaWitness), false);
		Params.AddIgnoredActor(N);
		if (Player)
		{
			Params.AddIgnoredActor(Player.Get());
			if (Player->Vehicle) Params.AddIgnoredActor(Player->Vehicle.Get());
		}
		FHitResult Hit;
		if (!W->LineTraceSingleByChannel(Hit, NF + FVector(0.0, 0.0, 160.0), P + FVector(0.0, 0.0, 120.0), ECC_Visibility, Params)) Out.push_back(U(N->NpcId));
	}
	return Out;
}

bool AVilaWorld::is_vehicle_near_spot(const std::string& VehicleId, const std::string& SpotId, double Radius) const
{
	const TObjectPtr<AVilaVehicle>* V = Vehicles.Find(S(VehicleId));
	const FVilaSpot* Sp = Spot(S(SpotId));
	if (!V || !*V || !Sp) return false;
	return VilaW::Dist2D((*V)->GetActorLocation(), Sp->Location) <= Radius * M;
}

void AVilaWorld::refresh_vehicle(const std::string& VehicleId)
{
	if (AVilaVehicle* V = Vehicles.FindRef(S(VehicleId))) V->RefreshFromState();
}

void AVilaWorld::sync_to_state()
{
	if (Player) Player->SyncToState();
	for (const auto& KV : Vehicles)
		if (KV.Value) KV.Value->SaveState();
}

std::string AVilaWorld::save_block_reason() const
{
	if (Player && Player->State == TEXT("driving") && Player->Vehicle && FMath::Abs(Player->Vehicle->Speed) > 100.f) return vila::tr("Pare o veículo para salvar.");
	if (Incidents.Num() > 0) return vila::tr("Não é possível salvar agora.");
	const AVilaHUD* HUD = AVilaHUD::Get(this);
	if (HUD && HUD->IsDialogueOpen()) return vila::tr("Termine a conversa antes de salvar.");
	return "";
}

void AVilaWorld::on_shift_ended(const vila::Json& Summary)
{
	if (vila::jstr(Summary, "reason") != "normal") ShowShiftSummary(Summary);
}

void AVilaWorld::on_player_fainted()
{
	if (!Player || Player->State == TEXT("sleeping") || bFainting) return;
	bFainting = true;
	vila::Needs& N = vila::game().player.needs;
	vila::push_warning("Jogador desmaiou: " + N.to_dict().dump());
	if (Player->State == TEXT("driving") && Player->Vehicle)
	{
		Player->Vehicle->Speed = 0.f;
		Player->ExitVehicle();
	}
	if (AVilaHUD* HUD = AVilaHUD::Get(this)) HUD->CloseAll();
	const std::string Cause = N.get_need("sono") >= 100.0 ? vila::tr("exaustão") : vila::tr("problemas de saúde");
	vila::clock().advance(8 * 60.0);
	N.set_need("sono", 30.0);
	N.saude = FMath::Max(N.saude, 35.0);
	N.set_need("fome", FMath::Min(N.get_need("fome"), 70.0));
	N.set_need("sede", FMath::Min(N.get_need("sede"), 60.0));
	vila::game().player.add_money(-80, "Atendimento no posto de saúde");
	AVilaFurniture* Bed = nullptr;
	if (Builder)
		for (const vila::Json& Home : vila::game().property.owned_homes())
		{
			const FString HomeId = S(vila::to_str(Home));
			for (const auto& KV : Builder->Furniture)
				if (KV.Value && KV.Value->Kind == TEXT("bed") && KV.Value->GetBuildingId() == HomeId) Bed = KV.Value;
		}
	if (Bed)
	{
		Player->StandUp();
		// Godot: bed.global_position + Vector3(1.2, 0.05, 0.6) (deslocamento no mundo)
		VilaW::SetFeet(Player, Bed->GetActorLocation() + Size(1.2, 0.05, 0.6));
	}
	vila::game().player.needs.fainted = false;
	bFainting = false;
	vila::events().request_message.emit(vila::tr("Você desmaiou"),
		vila::fmt(vila::tr("Por %s, você apagou e foi levado(a) ao posto de saúde. Acordou horas depois em casa, com uma conta de R$ 80.\n\nCuide da alimentação, da hidratação e do sono."), {Cause}));
}

// ================================================================ eventos do mundo
void AVilaWorld::StartDialogue(AVilaNPC* Npc)
{
	if (!Npc || Npc->IsSleeping()) return;
	if (AVilaHUD* HUD = AVilaHUD::Get(this)) HUD->OpenDialogue(Npc);
}

void AVilaWorld::DeliverDriveOrder()
{
	vila::PlayerState& PS = vila::game().player;
	const vila::Json R = vila::game().job.deliver(PS.inventory, PS.skills.level("social"));
	const std::string Status = vila::jstr(R, "status");
	if (Status == "complete")
	{
		PS.stats["pedidos_entregues"] = vila::jint(PS.stats, "pedidos_entregues", 0) + 1;
		PS.skills.add_xp("social", 6);
		PS.skills.add_xp("culinaria", 4);
		if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(TEXT("cash"));
		vila::events().notify.emit(vila::fmt(vila::tr("Pedido #%d entregue! Gorjeta: R$ %d"), {vila::jget(vila::jobj(R, "order"), "id"), vila::jget(R, "tip")}), "good");
	}
	else if (Status == "partial")
	{
		vila::events().notify.emit(vila::fmt(vila::tr("Cliente: \"Tá faltando coisa: %s!\""), {vila::DriveThruJob::order_text(vila::jget(vila::jobj(R, "order"), "remaining"))}), "bad");
	}
	else if (Status == "nothing")
	{
		vila::events().notify.emit(vila::tr("Você não tem nada deste pedido nas mãos."), "bad");
	}
	else if (Status == "none")
	{
		vila::events().notify.emit(vila::tr("Não há cliente na janela."), "info");
	}
}

void AVilaWorld::ShowShiftSummary(const vila::Json& Sum)
{
	std::string Body = vila::fmt(vila::tr("Horas trabalhadas: %.1f\nSalário: R$ %d\nBônus por pedidos: R$ %d\nGorjetas: R$ %d\nPedidos entregues: %d · Clientes perdidos: %d\n\nTotal recebido: R$ %d"),
		{vila::jget(Sum, "hours"), vila::jget(Sum, "wage"), vila::jget(Sum, "bonus"), vila::jget(Sum, "tips"), vila::jget(Sum, "ok"), vila::jget(Sum, "failed"), vila::jget(Sum, "total")});
	if (vila::truthy(vila::jget(Sum, "promoted"))) Body += "\n\n" + vila::fmt(vila::tr("Você foi promovido(a) a %s!"), {vila::game().job.rank_title()});
	vila::events().request_message.emit(vila::tr("Fim do turno — Brasa Burger"), Body);
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(TEXT("cash"));
}

// Dormir: o tempo avança em fatias; acorda cedo se a fome ou a sede ficarem críticas.
void AVilaWorld::DoSleep(int32 Hours, const FString& BedId)
{
	if (!Player || !Builder || bSleeping) return;
	const TObjectPtr<AVilaFurniture>* BedP = Builder->Furniture.Find(BedId);
	AVilaFurniture* Bed = BedP ? BedP->Get() : nullptr;
	if (!Bed) return;
	if (Player->State != TEXT("lying") || Player->Seat != Bed) Player->RestOnBed(Bed);
	Player->BeginSleep();
	bSleeping = true;
	if (AVilaHUD* HUD = AVilaHUD::Get(this)) HUD->Fade(true);
	GetWorldTimerManager().SetTimer(SleepTimer, FTimerDelegate::CreateWeakLambda(this, [this, Hours]() { SleepStep(Hours); }), FadeSeconds, false);
}

void AVilaWorld::SleepStep(int32 Hours)
{
	double Slept = 0.0;
	FString WokeReason;
	vila::PlayerState& PS = vila::game().player;
	PS.activity = "sleep";
	while (Slept < Hours * 60.0)
	{
		vila::clock().advance(30.0);
		PS.activity = "sleep";
		Slept += 30.0;
		const vila::Needs& N = PS.needs;
		if (N.get_need("fome") >= 95.0 || N.get_need("sede") >= 95.0)
		{
			WokeReason = Tr("Você acordou com fome e sede.");
			break;
		}
		if (N.get_need("sono") <= 0.0 && Slept >= 60.0 && Hours >= 12) break;
	}
	if (Player)
	{
		Player->EndSleep();
		Player->StandUp();
	}
	PS.activity = "idle";
	if (AVilaHUD* HUD = AVilaHUD::Get(this)) HUD->Fade(false);
	GetWorldTimerManager().SetTimer(SleepTimer, FTimerDelegate::CreateWeakLambda(this, [this, Slept, WokeReason]() { SleepDone(Slept, WokeReason); }), FadeSeconds, false);
}

void AVilaWorld::SleepDone(double Slept, const FString& WokeReason)
{
	bSleeping = false;
	vila::events().notify.emit(vila::fmt(vila::tr("Você dormiu %.1f h."), {Slept / 60.0}), "info");
	if (!WokeReason.IsEmpty()) vila::events().notify.emit(U(WokeReason), "bad");
	if (vila::saves().save_game("auto")) vila::events().notify.emit(vila::tr("Jogo salvo automaticamente."), "good");
}

void AVilaWorld::OnNPCHit(AVilaNPC* Npc, float Severity)
{
	if (!Npc) return;
	std::vector<std::string> Ws = find_witnesses(FromUE(VilaW::Feet(Npc)), 22.0);
	const std::string Nid = U(Npc->NpcId);
	if (std::find(Ws.begin(), Ws.end(), Nid) == Ws.end()) Ws.push_back(Nid);
	FIncident Inc;
	Inc.Npc = Npc->NpcId;
	Inc.Severity = Severity;
	for (const std::string& W : Ws)
	{
		Inc.Witnesses.push_back(W);
		if (W == Nid) continue;
		if (AVilaNPC* WN = NPCs.FindRef(S(W))) WN->Bark(VilaW::PickChatter("witness_hit"));
	}
	Incidents.Add(Inc);
	vila::events().notify.emit(vila::fmt(vila::tr("Você atropelou %s! Pare e preste socorro."), {U(Npc->DisplayName())}), "bad");
}

void AVilaWorld::OnNPCHelped(AVilaNPC* Npc)
{
	if (!Npc) return;
	for (FIncident& Inc : Incidents)
		if (Inc.Npc == Npc->NpcId)
		{
			Inc.bHelped = true;
			Inc.T = 0.f;
			return;
		}
	vila::game().relations.change_reputation(1);
}

void AVilaWorld::ResolveIncident(const FIncident& Inc)
{
	vila::Game& G = vila::game();
	const std::string Nid = U(Inc.Npc);
	G.law.report("atropelamento", Nid, Inc.Witnesses, vila::clock().day(), G.relations);
	if (Inc.bHelped)
	{
		G.law.mitigate("atropelamento", 100);
		G.relations.change(Nid, 15);
		G.relations.add_memory(Nid, "prestou_socorro", vila::clock().day());
		vila::events().notify.emit(vila::tr("Você prestou socorro. A multa será menor."), "info");
	}
	else
	{
		G.law.report("omissao_socorro", Nid, Inc.Witnesses, vila::clock().day(), G.relations);
		vila::events().notify.emit(vila::tr("Fugir sem prestar socorro foi registrado pelas testemunhas."), "bad");
	}
}

void AVilaWorld::OnHorn(const FVector& P)
{
	for (AVilaNPC* N : NPCsNear(P, 700.f))
		if (N->State != TEXT("knocked") && !N->IsSleeping()) N->Bark(Tr("Que susto!"));
}

void AVilaWorld::OnKnock(const FString& BuildingId)
{
	const TObjectPtr<AVilaBuilding>* BP = Builder ? Builder->Buildings.Find(BuildingId) : nullptr;
	if (!BP || !*BP) return;
	const FString Owner = S(vila::jstr((*BP)->Def, "owner"));
	AVilaNPC* O = Owner.IsEmpty() ? nullptr : NPCs.FindRef(Owner);
	if (O && (*BP)->Contains(VilaW::Feet(O)) && !O->IsSleeping())
		O->Bark(Tr("Quem é? Agora não posso, volta mais tarde!"));
	else
		vila::events().notify.emit(vila::tr("Ninguém atende."), "info");
}

void AVilaWorld::OnPlayerUsedKey(const FString& BuildingId)
{
	const TObjectPtr<AVilaBuilding>* BP = Builder ? Builder->Buildings.Find(BuildingId) : nullptr;
	if (!BP || !*BP || !Player) return;
	const std::string Owner = vila::jstr((*BP)->Def, "owner");
	if (Owner.empty() || Owner == "player") return;
	vila::game().set_flag("entrou_" + U(BuildingId));
	const vila::Vec3 P = FromUE(PlayerFeet());
	if (!find_witnesses(P, 14.0).empty()) vila::game().report_crime("invasao", Owner, false, &P);
}

// Pegar coisas de contêineres alheios é furto (testemunhado ou não).
void AVilaWorld::OnContainerTake(const FString& ContainerId)
{
	const vila::Json Meta = vila::game().world_state.meta(U(ContainerId));
	const std::string Owner = vila::jstr(Meta, "owner");
	const vila::Json& Priv = vila::jget(Meta, "private");
	if (InList(Owner, {"", "player", "owner"}) || !(Priv.is_null() || vila::truthy(Priv))) return;
	const vila::Vec3 P = FromUE(PlayerFeet());
	const vila::Json Entry = vila::game().report_crime("furto", Owner, false, &P);
	if (!vila::jbool(Entry, "solved", true)) vila::game().set_flag("furto_nao_resolvido_" + Owner);
}

// ================================================================ quadro a quadro
void AVilaWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPostBegin)
	{
		bPostBegin = true;
		DispatchPendingBeginPlay();
		PostBeginSetup();
	}
	if (bWaitingNav)
	{
		NavWaitT += DeltaSeconds;
		++NavWaitFrames;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const bool bBuilding = Nav && Nav->IsNavigationBuildInProgress();
		if (NavWaitFrames >= 2 && (!bBuilding || NavWaitT >= NavWaitTimeout)) FinishStartup();
	}
	vila::clock().process(DeltaSeconds);
	if (!bReady || bMenu || !Player) return;

	CutT -= DeltaSeconds;
	if (CutT <= 0.f)
	{
		CutT = 0.1f;
		UpdateCutaway();
	}
	TArray<FIncident> Due;
	for (int32 i = Incidents.Num() - 1; i >= 0; --i)
	{
		Incidents[i].T -= DeltaSeconds;
		if (Incidents[i].T <= 0.f)
		{
			Due.Insert(Incidents[i], 0);
			Incidents.RemoveAt(i);
		}
	}
	for (const FIncident& Inc : Due) ResolveIncident(Inc);
	CheckTrespass(DeltaSeconds);
	ProcessEvents(DeltaSeconds);
}

void AVilaWorld::UpdateCutaway()
{
	if (!Builder || !CameraRig) return;
	const bool bDriving = Player->State == TEXT("driving") && Player->Vehicle;
	const FVector PP = bDriving ? Player->Vehicle->GetActorLocation() : PlayerFeet();
	const FVector VD = CameraRig->ViewDir();
	FString InsideId;
	for (const auto& KV : Builder->Buildings)
	{
		AVilaBuilding* B = KV.Value;
		if (!B) continue;
		const bool bInside = B->Contains(PP);
		if (bInside) InsideId = KV.Key;
		bool bBehind = false;
		if (!bInside)
			for (const double K : {1.2, 2.6, 4.0})
				if (B->Contains(PP - VD * K * M))
				{
					bBehind = true;
					break;
				}
		B->SetCut(bInside || bBehind, VD);
	}
	if (InsideId != PlayerBuilding)
	{
		PlayerBuilding = InsideId;
		vila::events().player_building_changed.emit(U(InsideId));
	}
}

// Invasão: dono presente vê o jogador dentro de casa sem permissão.
void AVilaWorld::CheckTrespass(float Delta)
{
	StaffBarkT -= Delta;
	if (PlayerBuilding.IsEmpty() || !Builder)
	{
		TrespassT = 0.f;
		return;
	}
	const TObjectPtr<AVilaBuilding>* BP = Builder->Buildings.Find(PlayerBuilding);
	if (!BP || !*BP) return;
	AVilaBuilding* B = *BP;
	const vila::Json& Def = B->Def;
	const FVector Feet = PlayerFeet();
	// Cozinha do Brasa: só funcionários.
	if (vila::has(Def, "staff_zone") && !vila::game().job.employed)
	{
		const vila::Json& Z = vila::jarr(Def, "staff_zone");
		const vila::Json& C = vila::jarr(Def, "center");
		if (Z.size() >= 4 && C.size() >= 2)
		{
			const vila::Vec3 G = FromUE(Feet);
			const double LX = G.x - vila::to_num(C[0]);
			const double LZ = G.z - vila::to_num(C[1]);
			if (LX >= vila::to_num(Z[0]) && LX <= vila::to_num(Z[2]) && LZ >= vila::to_num(Z[1]) && LZ <= vila::to_num(Z[3]) && StaffBarkT <= 0.f)
			{
				StaffBarkT = 8.f;
				for (AVilaNPC* N : NPCsNear(Feet, 900.f))
					if (N->NpcId == TEXT("kleber") || N->NpcId == TEXT("sandra"))
					{
						N->Bark(VilaW::PickChatter("staff_only"));
						vila::game().relations.change(U(N->NpcId), -1);
						break;
					}
			}
		}
	}
	const std::string Owner = vila::jstr(Def, "owner");
	if (!vila::truthy(vila::jget(Def, "private")) || InList(Owner, {"", "player", "owner"}) || vila::game().property.owns_or_rents(U(PlayerBuilding))) return;
	AVilaNPC* O = NPCs.FindRef(S(Owner));
	if (!O) return;
	if (B->Contains(VilaW::Feet(O)) && O->CanWitness())
	{
		TrespassT += Delta;
		if (TrespassT > 0.5f && TrespassT - Delta <= 0.5f) O->Bark(VilaW::PickChatter("trespass"));
		const FString Key = PlayerBuilding + FString::FromInt(vila::clock().day());
		if (TrespassT > 5.f && !TrespassReported.Contains(Key))
		{
			TrespassReported.Add(Key);
			const vila::Vec3 P = FromUE(Feet);
			vila::game().report_crime("invasao", Owner, false, &P);
		}
	}
	else
	{
		TrespassT = 0.f;
	}
}

// ================================================================ entregas
void AVilaWorld::RefreshDelivery()
{
	if (IsValid(Drop)) Drop->Destroy();
	Drop = nullptr;
	const vila::Json& A = vila::game().delivery.active;
	if (A.empty()) return;
	const FVilaSpot* Sp = Spot(S(vila::jstr(A, "spot")));
	if (!Sp) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Drop = GetWorld()->SpawnActor<AVilaDeliveryDrop>(AVilaDeliveryDrop::StaticClass(), Sp->Location, FRotator::ZeroRotator, Params);
	if (Drop) Drop->Setup(this, S(vila::jstr(A, "label")));
}

// ================================================================ eventos dinâmicos
// Cria a situação no mundo. Retorna {} se as condições espaciais não existem agora.
vila::Json AVilaWorld::start_event(const std::string& Id, const vila::Json& Def)
{
	vila::Rng& Rng = vila::game().director.rng;
	if (Id == "carteira_perdida")
	{
		TArray<AVilaNPC*> Owners;
		for (const auto& KV : NPCs)
			if (KV.Value && !KV.Value->IsHidden() && !KV.Value->IsSleeping() && KV.Value->State != TEXT("knocked") && KV.Value->Activity != TEXT("leave")) Owners.Add(KV.Value);
		if (Owners.Num() == 0) return vila::Json::object();
		AVilaNPC* Owner = Owners[static_cast<int32>(Rng.randi() % static_cast<uint32>(Owners.Num()))];
		const vila::Json& Ws = vila::jarr(vila::data().map(), "wander_spots");
		if (Ws.empty()) return vila::Json::object();
		const FString Sp = S(vila::to_str(Ws[static_cast<size_t>(Rng.randi() % static_cast<uint32>(Ws.size()))]));
		const FVilaSpot* Spt = Spot(Sp);
		if (!Spt) return vila::Json::object();
		const double DX = Rng.randf_range(-1.5, 1.5);
		const double DZ = Rng.randf_range(-1.5, 1.5);
		const vila::Vec3 Base = FromUE(Spt->Location);
		const double PX = Base.x + DX, PZ = Base.z + DZ;
		vila::Json E = {{"id", "carteira_perdida"}, {"qty", 1}, {"age", 0.0}, {"pos", vila::Json::array({PX, 0.0, PZ})}};
		vila::game().world_state.ground_items.push_back(E);
		SpawnGroundItem(E);
		vila::game().set_flag("carteira_dono_" + U(Owner->NpcId));
		NotifyNear(Def, Map2D(PX, PZ));
		return {{"owner", U(Owner->NpcId)}, {"pos", vila::Json::array({PX, PZ})}};
	}
	if (Id == "pedestre_ferido")
	{
		TArray<AVilaNPC*> Cands;
		for (const auto& KV : NPCs)
		{
			AVilaNPC* N = KV.Value;
			if (N && N->bIsAmbient && !N->IsHidden() && (N->State == TEXT("moving") || N->State == TEXT("at_spot")) && BuildingAt(VilaW::Feet(N)).IsEmpty()) Cands.Add(N);
		}
		if (Cands.Num() == 0) return vila::Json::object();
		AVilaNPC* N2 = Cands[static_cast<int32>(Rng.randi() % static_cast<uint32>(Cands.Num()))];
		N2->KnockDown(FVector::ZeroVector, 0.5f, false, 240.f);
		for (AVilaNPC* W : NPCsNear(VilaW::Feet(N2), 1500.f))
			if (W != N2)
			{
				W->Bark(Tr("Alguém ajuda aqui!"));
				break;
			}
		NotifyNear(Def, VilaW::Feet(N2));
		return {{"npc", U(N2->NpcId)}};
	}
	if (Id == "discussao_praca")
	{
		const vila::Json& Parts = vila::jarr(Def, "participants");
		if (Parts.size() < 2) return vila::Json::object();
		for (const vila::Json& Pid : Parts)
		{
			AVilaNPC* N = NPCs.FindRef(S(vila::to_str(Pid)));
			if (!N || N->State != TEXT("at_spot") || !N->BuildingAtSpot().IsEmpty()) return vila::Json::object();
		}
		AVilaNPC* A = NPCs.FindRef(S(vila::to_str(Parts[0])));
		AVilaNPC* B = NPCs.FindRef(S(vila::to_str(Parts[1])));
		if (VilaW::Dist2D(VilaW::Feet(A), VilaW::Feet(B)) > 1400.f) return vila::Json::object();
		ArgueT = 0.f;
		ArgueI = 0;
		NotifyNear(Def, VilaW::Feet(A));
		return {{"participants", Parts}};
	}
	return vila::Json::object();
}

void AVilaWorld::end_event(const std::string& Id, const vila::Json& /*Info*/)
{
	if (Id != "discussao_praca") return;
	for (const vila::Json& Pid : vila::jarr(vila::jobj(vila::data().events(), Id), "participants"))
		if (AVilaNPC* N = NPCs.FindRef(S(vila::to_str(Pid)))) N->Bark(Tr("Hunf."));
}

void AVilaWorld::NotifyNear(const vila::Json& Def, const FVector& P)
{
	if (Player && VilaW::Dist2D(PlayerFeet(), P) <= vila::jnum(Def, "notify_radius", 40.0) * M)
		vila::events().notify.emit(vila::tr(vila::jstr(Def, "notify", "")), "info");
}

void AVilaWorld::ProcessEvents(float Delta)
{
	if (!vila::game().director.is_active("discussao_praca")) return;
	ArgueT -= Delta;
	if (ArgueT > 0.f) return;
	ArgueT = 5.f;
	const vila::Json& Def = vila::jobj(vila::data().events(), "discussao_praca");
	const vila::Json& Lines = vila::jarr(Def, "lines");
	const vila::Json& Parts = vila::jarr(Def, "participants");
	if (Lines.empty() || Parts.size() < 2) return;
	const vila::Json& L = Lines[static_cast<size_t>(ArgueI) % Lines.size()];
	AVilaNPC* A = NPCs.FindRef(S(vila::to_str(Parts[static_cast<size_t>(ArgueI % 2)])));
	AVilaNPC* B = NPCs.FindRef(S(vila::to_str(Parts[static_cast<size_t>((ArgueI + 1) % 2)])));
	if (A && L.size() > 0) A->Bark(Tr(vila::to_str(L[0])));
	if (B && L.size() > 1) B->BarkLater(Tr(vila::to_str(L[1])), 2.4f);
	++ArgueI;
}

// Ações extras que eventos e itens acrescentam a um NPC.
TArray<FVilaAction> AVilaWorld::ExtraNPCActions(AVilaNPC* Npc)
{
	TArray<FVilaAction> Out;
	if (!Npc) return Out;
	vila::Game& G = vila::game();
	const std::string Nid = U(Npc->NpcId);
	if (G.player.inventory.has("carteira_perdida") && G.has_flag("carteira_dono_" + Nid))
		Out.Add(FVilaAction(TEXT("return_wallet"), Tr("Devolver a carteira")));
	if (G.director.is_active("discussao_praca") && vila::json_array_has(vila::jarr(vila::jobj(vila::data().events(), "discussao_praca"), "participants"), vila::Json(Nid)))
	{
		const double Ch = vila::clampf(0.35 + G.player.skills.level("social") * 0.1 + G.relations.reputation * 0.003, 0.05, 0.95);
		Out.Add(FVilaAction(TEXT("calm"), Trf("[Social %d%%] Apaziguar a discussão", static_cast<int>(Ch * 100))));
	}
	return Out;
}

bool AVilaWorld::DoExtraNPCAction(AVilaNPC* Npc, const FString& ActionId)
{
	if (!Npc) return false;
	vila::Game& G = vila::game();
	const std::string Nid = U(Npc->NpcId);
	if (ActionId == TEXT("return_wallet"))
	{
		G.player.inventory.remove("carteira_perdida", 1);
		G.clear_flag("carteira_dono_" + Nid);
		G.relations.change(Nid, 15);
		G.relations.change_reputation(3);
		G.relations.add_memory(Nid, "devolveu_carteira", vila::clock().day());
		G.player.add_money(10, "Recompensa");
		Npc->Bark(Tr("Minha carteira! Muito obrigado(a), tem gente honesta no mundo!"));
		return true;
	}
	if (ActionId == TEXT("calm"))
	{
		const double Ch = vila::clampf(0.35 + G.player.skills.level("social") * 0.1 + G.relations.reputation * 0.003, 0.05, 0.95);
		const vila::Json Parts = vila::jarr(vila::jobj(vila::data().events(), "discussao_praca"), "participants");
		if (G.director.rng.randf() < Ch)
		{
			for (const vila::Json& Pid : Parts)
			{
				G.relations.change(vila::to_str(Pid), 5);
				if (AVilaNPC* N = NPCs.FindRef(S(vila::to_str(Pid)))) N->Bark(Tr("...Tá bom, tá bom. Desculpa."));
			}
			G.relations.change_reputation(3);
			G.player.skills.add_xp("social", 25);
			G.director.active.erase("discussao_praca");
			vila::events().notify.emit(vila::tr("Você acalmou os ânimos na praça."), "good");
		}
		else
		{
			G.relations.change("marcos", -3);
			G.player.skills.add_xp("social", 8);
			if (AVilaNPC* Marcos = NPCs.FindRef(TEXT("marcos"))) Marcos->Bark(Tr("Não se mete, não!"));
		}
		return true;
	}
	return false;
}
