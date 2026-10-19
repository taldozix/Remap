#include "VilaTrafficSystem.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreGlobals.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Vila/Data.h"
#include "Vila/Game.h"
#include "Vila/Settings.h"
#include "VilaAssets.h"
#include "VilaAurora.h"
#include "VilaConv.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaTrafficCar.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "VilaWorldBuilder.h"

namespace
{
	FLinearColor TSSRGB(float R, float G, float B)
	{
		auto Q = [](float V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255)); };
		return FLinearColor::FromSRGBColor(FColor(Q(R), Q(G), Q(B)));
	}

	// Cores das lâmpadas do Godot: vermelho, amarelo, verde.
	FLinearColor TSLampColor(int32 C)
	{
		switch (C)
		{
		case 0: return TSSRGB(1.f, 0.15f, 0.1f);
		case 1: return TSSRGB(1.f, 0.75f, 0.1f);
		default: return TSSRGB(0.2f, 1.f, 0.35f);
		}
	}

	constexpr float TSLampOn = 3.f;     // emission_energy 3.0
	constexpr float TSLampOff = 0.06f;  // apagada (no Godot ficava só o albedo escuro)
}

AVilaTrafficSystem::AVilaTrafficSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Rng.randomize();
}

FString AVilaTrafficSystem::Key(double X, double Z)
{
	return FString::Printf(TEXT("%d,%d"), static_cast<int32>(X), static_cast<int32>(Z));
}

void AVilaTrafficSystem::Setup(AVilaWorld* World, const vila::Json& Map)
{
	VilaWorld = World;
	const vila::Json& R = vila::jobj(Map, "roads");
	const float Off = static_cast<float>(vila::jnum(R, "lane_offset", 1.75)) * 100.f;
	LaneSpeedCm = static_cast<float>(vila::jnum(R, "speed_limit", LaneSpeed / 100.0)) * 100.f;
	const vila::Json& Xs = vila::jarr(R, "xs");
	const vila::Json& Zs = vila::jarr(R, "zs");
	for (const vila::Json& X : Xs)
		for (const vila::Json& Z : Zs)
		{
			FVilaTrafficNode N;
			N.Pos = VilaConv::Map2D(vila::to_num(X), vila::to_num(Z));
			Nodes.Add(Key(vila::to_num(X), vila::to_num(Z)), N);
		}
	for (const vila::Json& S : vila::jarr(R, "signals"))
	{
		if (!S.is_array() || S.size() < 2) continue;
		if (FVilaTrafficNode* N = Nodes.Find(Key(vila::to_num(S[0]), vila::to_num(S[1]))))
		{
			N->bSignal = true;
			SignalCenters.Add(N->Pos);
		}
	}
	for (size_t i = 0; i < Xs.size(); ++i)
		for (size_t j = 0; j < Zs.size(); ++j)
		{
			const FString A = Key(vila::to_num(Xs[i]), vila::to_num(Zs[j]));
			if (i + 1 < Xs.size()) AddEdgePair(A, Key(vila::to_num(Xs[i + 1]), vila::to_num(Zs[j])), Off);
			if (j + 1 < Zs.size()) AddEdgePair(A, Key(vila::to_num(Xs[i]), vila::to_num(Zs[j + 1])), Off);
		}
	const vila::Json& Dt = vila::jobj(Map, "drivethru");
	for (const vila::Json& P : vila::jarr(Dt, "path"))
		if (P.is_array() && P.size() >= 2) DrivePath.Add(VilaConv::Map2D(vila::to_num(P[0]), vila::to_num(P[1])));
	WindowIndex = vila::jint(Dt, "window_index", 9);
	const vila::Json& J = vila::jobj(Dt, "join");
	if (!J.empty())
	{
		const vila::Json& From = vila::jarr(J, "from");
		const vila::Json& To = vila::jarr(J, "to");
		if (From.size() >= 2 && To.size() >= 2)
			JoinEdge = FindEdge(Key(vila::to_num(From[0]), vila::to_num(From[1])), Key(vila::to_num(To[0]), vila::to_num(To[1])));
	}

	SetupLamps();
	UpdateDensity();
	TWeakObjectPtr<AVilaTrafficSystem> WeakThis(this);
	SettingsConn = vila::settings().changed.connect([WeakThis](const std::string&) {
		if (WeakThis.IsValid()) WeakThis->UpdateDensity();
	});
	for (int32 k = 0; k < TargetCount; ++k) SpawnRandom(true);
}

void AVilaTrafficSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SettingsConn) vila::settings().changed.disconnect(SettingsConn);
	SettingsConn = 0;
	Super::EndPlay(EndPlayReason);
}

void AVilaTrafficSystem::UpdateDensity()
{
	TargetCount = FMath::RoundToInt(7.0 * vila::to_num(vila::settings().get_value("graphics", "traffic_density"), 1.0));
}

void AVilaTrafficSystem::AddEdgePair(const FString& A, const FString& B, float OffCm)
{
	const FString Pairs[2][2] = {{A, B}, {B, A}};
	for (const auto& Pair : Pairs)
	{
		const FVilaTrafficNode* NA = Nodes.Find(Pair[0]);
		const FVilaTrafficNode* NB = Nodes.Find(Pair[1]);
		if (!NA || !NB) continue;
		const FVector D = (NB->Pos - NA->Pos).GetSafeNormal();
		// Godot: right = (-d.z, 0, d.x) -> UE (X = x, Y = z): (-D.Y, D.X)
		const FVector Right(-D.Y, D.X, 0.0);
		FVilaTrafficEdge E;
		E.A = Pair[0];
		E.B = Pair[1];
		E.Dir = D;
		E.Start = NA->Pos + D * StopDist + Right * OffCm;
		E.End = NB->Pos - D * StopDist + Right * OffCm;
		E.Axis = FMath::Abs(D.X) > 0.5 ? TEXT("ew") : TEXT("ns");
		const int32 Index = Edges.Add(E);
		OutEdges.FindOrAdd(Pair[0]).Add(Index);
	}
}

int32 AVilaTrafficSystem::FindEdge(const FString& A, const FString& B) const
{
	for (int32 i = 0; i < Edges.Num(); ++i)
		if (Edges[i].A == A && Edges[i].B == B) return i;
	return -1;
}

int32 AVilaTrafficSystem::ChooseNext(int32 EdgeIndex)
{
	if (!Edges.IsValidIndex(EdgeIndex)) return -1;
	const FVilaTrafficEdge& E = Edges[EdgeIndex];
	TArray<int32> Options;
	if (const TArray<int32>* Out = OutEdges.Find(E.B))
		for (const int32 O : *Out)
			if (Edges[O].B != E.A) Options.Add(O);
	if (Options.Num() == 0) return FindEdge(E.B, E.A);
	return Options[static_cast<int32>(Rng.randi() % static_cast<uint32>(Options.Num()))];
}

TArray<FVector> AVilaTrafficSystem::Connector(int32 E1, int32 E2) const
{
	TArray<FVector> Out;
	if (!Edges.IsValidIndex(E1) || !Edges.IsValidIndex(E2)) return Out;
	const FVector A = Edges[E1].End;
	const FVector B = Edges[E2].Start;
	const FVector D1 = Edges[E1].Dir;
	FVector C = A + D1 * ((B - A) | D1);
	if ((D1 | Edges[E2].Dir) > 0.99) C = (A + B) * 0.5;
	for (int32 i = 1; i < 9; ++i)
	{
		const double T = i / 8.0;
		Out.Add(FMath::Lerp(FMath::Lerp(A, C, T), FMath::Lerp(C, B, T), T));
	}
	return Out;
}

// ------------------------------------------------------------------ semáforo
FString AVilaTrafficSystem::LightState(const FString& Axis) const
{
	const float Cycle = 2.f * (Green + Yellow);
	const float T = FMath::Fmod(Phase, Cycle);
	const TCHAR* Ew = T < Green ? TEXT("green") : (T < Green + Yellow ? TEXT("yellow") : TEXT("red"));
	const TCHAR* Ns = T < Green + Yellow ? TEXT("red") : (T < 2.f * Green + Yellow ? TEXT("green") : TEXT("yellow"));
	return Axis == TEXT("ew") ? FString(Ew) : FString(Ns);
}

bool AVilaTrafficSystem::CanEnter(AVilaTrafficCar* Car, const FString& NodeKey, const FString& Axis, float DistCm)
{
	FVilaTrafficNode* N = Nodes.Find(NodeKey);
	if (!N) return true;
	if (N->bSignal)
	{
		const FString S = LightState(Axis);
		if (S == TEXT("red") || (S == TEXT("yellow") && DistCm > 300.f)) return false;
	}
	AVilaTrafficCar* Occ = N->Occupant.Get();
	if (Occ && Occ != Car && N->OccT < 7.f)
	{
		if (!(N->bSignal && Occ->CurAxis == Axis)) return false;
	}
	N->Occupant = Car;
	N->OccT = 0.f;
	return true;
}

bool AVilaTrafficSystem::NearNode(const FVector& P, float RadiusCm) const
{
	for (const auto& KV : Nodes)
	{
		const FVector& Q = KV.Value.Pos;
		if (FMath::Abs(Q.X - P.X) < RadiusCm && FMath::Abs(Q.Y - P.Y) < RadiusCm) return true;
	}
	return false;
}

void AVilaTrafficSystem::Release(AVilaTrafficCar* Car, const FString& NodeKey)
{
	FVilaTrafficNode* N = Nodes.Find(NodeKey);
	if (N && N->Occupant.Get() == Car) N->Occupant.Reset();
}

UStaticMeshComponent* AVilaTrafficSystem::AddPart(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& T, UMaterialInterface* Mat)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(Mesh);
	C->SetRelativeTransform(T);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	if (Mat) C->SetMaterial(0, Mat);
	C->RegisterComponent();
	SignalParts.Add(C);
	return C;
}

void AVilaTrafficSystem::SetupLamps()
{
	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (!Assets) return;
	bRealEmissive = Assets->Master(TEXT("M_Emissive")) != nullptr;
	LampMIDs.Reset();
	for (int32 Axis = 0; Axis < 2; ++Axis)
		for (int32 C = 0; C < 3; ++C)
		{
			UMaterialInstanceDynamic* MID = Assets->MakeColorMID(TEXT("M_Emissive"), TSLampColor(C), this);
			MID->SetScalarParameterValue(TEXT("Intensity"), TSLampOff);
			if (!bRealEmissive) MID->SetVectorParameterValue(TEXT("Color"), TSLampColor(C) * 0.25f);
			LampMIDs.Add(MID);
		}
	LampState[0].Reset();
	LampState[1].Reset();

	// Lâmpadas montadas pelo construtor do mundo (W1), que cuida do material; sem elas,
	// monta os semáforos aqui (_build_signal) com os materiais acima.
	UVilaWorldBuilder* B = VilaWorld ? VilaWorld->Builder.Get() : nullptr;
	SignalBuilder = (B && B->SignalLamps.Num() > 0) ? B : nullptr;
	if (!SignalBuilder)
		for (const FVector& C : SignalCenters) BuildSignal(C);
	UpdateLamps();
}

void AVilaTrafficSystem::BuildSignal(const FVector& Center)
{
	UVilaAssets* Assets = UVilaAssets::Get(this);
	if (!Assets || !Assets->Cube() || !Assets->Cylinder() || !Assets->Sphere()) return;
	const float Hw = 4.5f + 1.2f;
	struct FHead
	{
		float X, Z, Facing;
		int32 Axis;
	};
	// Godot: posições (x, z) em metros e rotação em graus (eixo Y).
	const FHead Heads[] = {{Hw, Hw, 180.f, 1}, {-Hw, -Hw, 0.f, 1}, {Hw, -Hw, -90.f, 0}, {-Hw, Hw, 90.f, 0}};
	UMaterialInterface* PoleMat = Assets->SurfaceLocal(TEXT("metal_painted"), TSSRGB(0.3f, 0.33f, 0.33f));
	UMaterialInterface* BoxMat = Assets->SurfaceLocal(TEXT("metal_painted"), TSSRGB(0.14f, 0.15f, 0.16f));
	for (const FHead& H : Heads)
	{
		USceneComponent* HeadRoot = NewObject<USceneComponent>(this);
		HeadRoot->SetupAttachment(Root);
		HeadRoot->SetWorldLocationAndRotation(Center + VilaConv::Map2D(H.X, H.Z), VilaConv::RotFromGodot(H.Facing));
		HeadRoot->RegisterComponent();
		SignalParts.Add(HeadRoot);
		// No espaço local do UE, +X é a frente da cabeça (o -Z local do Godot).
		AddPart(HeadRoot, Assets->Cylinder(), FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 170.0), FVector(0.18, 0.18, 3.4)), PoleMat);
		AddPart(HeadRoot, Assets->Cube(), FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 320.0), FVector(0.25, 0.35, 0.95)), BoxMat);
		for (int32 i = 0; i < 3; ++i)
		{
			AddPart(HeadRoot, Assets->Cube(), FTransform(FRotator::ZeroRotator, FVector(20.0, 0.0, 362.0 - i * 29.0), FVector(0.14, 0.24, 0.03)), BoxMat);
			AddPart(HeadRoot, Assets->Sphere(), FTransform(FRotator::ZeroRotator, FVector(14.0, 0.0, 350.0 - i * 29.0), FVector(0.18)), LampMIDs[H.Axis * 3 + i]);
		}
	}
}

void AVilaTrafficSystem::UpdateLamps()
{
	if (SignalBuilder)
	{
		const FString Ew = LightState(TEXT("ew"));
		const FString Ns = LightState(TEXT("ns"));
		if (Ew != LampState[0] || Ns != LampState[1])
		{
			LampState[0] = Ew;
			LampState[1] = Ns;
			SignalBuilder->SetSignalStates(Ew, Ns);
		}
		return;
	}
	if (LampMIDs.Num() < 6) return;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const FString S = LightState(Axis == 0 ? TEXT("ew") : TEXT("ns"));
		if (S == LampState[Axis]) continue;
		LampState[Axis] = S;
		const bool bOn[3] = {S == TEXT("red"), S == TEXT("yellow"), S == TEXT("green")};
		for (int32 C = 0; C < 3; ++C)
		{
			UMaterialInstanceDynamic* MID = LampMIDs[Axis * 3 + C];
			if (!MID) continue;
			MID->SetScalarParameterValue(TEXT("Intensity"), bOn[C] ? TSLampOn : TSLampOff);
			if (!bRealEmissive) MID->SetVectorParameterValue(TEXT("Color"), TSLampColor(C) * (bOn[C] ? 1.f : 0.25f));
		}
	}
}

// ------------------------------------------------------------------ carros
bool AVilaTrafficSystem::PlayerPos(FVector& Out) const
{
	if (!VilaWorld || !VilaWorld->Player) return false;
	AVilaPlayerCharacter* P = VilaWorld->Player;
	Out = (P->State == TEXT("driving") && P->Vehicle) ? P->Vehicle->GetActorLocation() : P->GetActorLocation();
	Out.Z = 0.0;
	return true;
}

AVilaTrafficCar* AVilaTrafficSystem::SpawnRandom(bool bInitial)
{
	if (Edges.Num() == 0) return nullptr;
	FVector PP;
	const bool bHasPlayer = PlayerPos(PP);
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		const int32 Ei = static_cast<int32>(Rng.randi() % static_cast<uint32>(Edges.Num()));
		const FVilaTrafficEdge& E = Edges[Ei];
		const double T = bInitial ? Rng.randf_range(0.0, 0.7) : 0.0;
		const FVector Pos = FMath::Lerp(E.Start, E.End, T);
		if (bHasPlayer && FVector::Dist2D(Pos, PP) < (bInitial ? 2500.f : 4000.f)) continue;
		if (!IsClear(Pos, 900.f)) continue;
		return SpawnCar(Ei, Pos, false);
	}
	return nullptr;
}

AVilaTrafficCar* AVilaTrafficSystem::SpawnCustomer()
{
	if (JoinEdge < 0) return nullptr;
	const int32 Origin = FindEdge(Key(-60, 0), Key(0, 0));
	for (const int32 Ei : {Origin, JoinEdge})
	{
		if (!Edges.IsValidIndex(Ei)) continue;
		const FVector Pos = Edges[Ei].Start;
		if (IsClear(Pos, 900.f)) return SpawnCar(Ei, Pos, true);
	}
	return nullptr;
}

bool AVilaTrafficSystem::IsClear(const FVector& Pos, float RadiusCm) const
{
	for (const AVilaTrafficCar* C : Cars)
		if (IsValid(C) && FVector::Dist2D(C->GetActorLocation(), Pos) < RadiusCm) return false;
	if (VilaWorld)
		for (const auto& KV : VilaWorld->Vehicles)
			if (KV.Value && FVector::Dist2D(KV.Value->GetActorLocation(), Pos) < RadiusCm) return false;
	return true;
}

AVilaTrafficCar* AVilaTrafficSystem::SpawnCar(int32 EdgeIndex, const FVector& Pos, bool bCustomer)
{
	static const TCHAR* const Types[] = {TEXT("sedan"), TEXT("brisa"), TEXT("picape"), TEXT("vanza"), TEXT("sedan")};
	static const char* const Colors[] = {"c94c4c", "e8e4d8", "3b5b8c", "2f2f33", "9fb7c9", "d9a441", "5d7a4a"};
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVilaTrafficCar* Car = W->SpawnActor<AVilaTrafficCar>(AVilaTrafficCar::StaticClass(), FTransform(Pos), Params);
	if (!Car) return nullptr;
	const FString Id = FString::Printf(TEXT("trafego_%d"), NextId++);
	const FString Type = Types[Rng.randi() % UE_ARRAY_COUNT(Types)];
	const FLinearColor Color = VilaConv::Hex(Colors[Rng.randi() % UE_ARRAY_COUNT(Colors)]);
	Car->Init(this, VilaWorld, Id, Type, Color, bCustomer);
	Car->StartOnEdge(EdgeIndex, Pos);
	Cars.Add(Car);
	return Car;
}

void AVilaTrafficSystem::RemoveCar(AVilaTrafficCar* Car)
{
	if (!Car) return;
	for (auto& KV : Nodes)
		if (KV.Value.Occupant.Get() == Car) KV.Value.Occupant.Reset();
	Cars.Remove(Car);
	if (Car->bCustomer && Car->OrderState.IsEmpty())
		vila::game().job.customers_requested = FMath::Max(0, vila::game().job.customers_requested - 1);
	Car->Destroy();
}

void AVilaTrafficSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	for (auto& KV : Nodes) KV.Value.OccT += DeltaSeconds;
	UpdateLamps();

	SpawnT -= DeltaSeconds;
	if (SpawnT > 0.f || !VilaWorld) return;
	SpawnT = 2.f;
	Cars.RemoveAll([](const TObjectPtr<AVilaTrafficCar>& C) { return !IsValid(C); });
	int32 Normal = 0;
	for (const AVilaTrafficCar* C : Cars)
		if (!C->bCustomer) ++Normal;
	if (Normal < TargetCount)
	{
		SpawnRandom();
	}
	else if (Normal > TargetCount)
	{
		FVector PP;
		if (PlayerPos(PP))
		{
			const TArray<TObjectPtr<AVilaTrafficCar>> Copy = Cars;
			for (AVilaTrafficCar* C : Copy)
				if (C && !C->bCustomer && FVector::Dist2D(C->GetActorLocation(), PP) > 5000.f)
				{
					RemoveCar(C);
					break;
				}
		}
	}
	// Clientes do drive-thru
	vila::Game& G = vila::game();
	if (G.job.wants_customer())
		if (SpawnCustomer()) G.job.customer_dispatched(G.rng);
}

const TArray<FVilaTrafficObstacle>& AVilaTrafficSystem::Obstacles()
{
	if (ObstacleFrame == GFrameCounter) return ObstacleCache;
	ObstacleFrame = GFrameCounter;
	ObstacleCache.Reset();
	auto Add = [this](AActor* A, float Half, float Width, bool bPlayer, bool bTraffic) {
		FVilaTrafficObstacle O;
		O.Actor = A;
		O.Pos = A->GetActorLocation();
		O.Pos.Z = 0.0;
		O.Half = Half;
		O.Width = Width;
		O.bPlayer = bPlayer;
		O.bTraffic = bTraffic;
		ObstacleCache.Add(O);
	};
	for (AVilaTrafficCar* C : Cars)
		if (IsValid(C)) Add(C, 230.f, 100.f, false, true);
	if (!VilaWorld) return ObstacleCache;
	for (const auto& KV : VilaWorld->Vehicles)
		if (AVilaVehicle* V = KV.Value) Add(V, 240.f, 100.f, V->Driver != nullptr, false);
	if (AVilaPlayerCharacter* P = VilaWorld->Player)
		if (P->State != TEXT("driving")) Add(P, 40.f, 40.f, true, false);
	for (const auto& KV : VilaWorld->NPCs)
		if (AVilaNPC* N = KV.Value)
			if (!N->IsHidden()) Add(N, 40.f, 40.f, false, false);
	return ObstacleCache;
}
