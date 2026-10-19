#include "VilaTrafficCar.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/DriveThruJob.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaCarVisual.h"
#include "VilaConv.h"
#include "VilaPlayerCharacter.h"
#include "VilaTrafficSystem.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"

namespace
{
	FVector TCFlat(FVector V)
	{
		V.Z = 0.0;
		return V;
	}

	// Desaceleração para parar a `D` cm de um ponto com `A` cm/s² (Godot: sqrt(2·a·(d - margem))).
	float TCStopSpeed(float A, float D) { return FMath::Sqrt(FMath::Max(0.f, 2.f * A * D)); }
}

AVilaTrafficCar::AVilaTrafficCar()
{
	PrimaryActorTick.bCanEverTick = true;

	// Godot: AnimatableBody3D na camada 16 (veículos), sem máscara; move sem sweep.
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetMobility(EComponentMobility::Movable);
	Box->InitBoxExtent(FVector(220.f, 90.f, 56.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_Vehicle);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Box->SetSimulatePhysics(false);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);

	Visual = CreateDefaultSubobject<UVilaCarVisual>(TEXT("Visual"));
	Visual->SetupAttachment(Box);

	// Balão de fala (Label3D do Godot): billboard manual no Tick.
	BarkText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Bark"));
	BarkText->SetupAttachment(Box);
	BarkText->SetHorizontalAlignment(EHTA_Center);
	BarkText->SetVerticalAlignment(EVRTA_TextBottom);
	BarkText->SetWorldSize(19.f);  // font_size 34 * pixel_size 0.0055 m
	BarkText->SetTextRenderColor(FColor::White);
	BarkText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BarkText->SetCastShadow(false);
	BarkText->SetVisibility(false);
}

void AVilaTrafficCar::Init(AVilaTrafficSystem* InSys, AVilaWorld* InWorld, const FString& InCarId, const FString& InType, const FLinearColor& InColor, bool bInCustomer)
{
	Sys = InSys;
	VilaWorld = InWorld;
	CustomerId = InCarId;
	VType = InType;
	Color = InColor;
	bCustomer = bInCustomer;

	const vila::Json& Data = vila::data().vehicle(VilaConv::U(VType));
	const vila::Json& Size = vila::jarr(Data, "size");
	const float W = Size.size() >= 3 ? static_cast<float>(vila::to_num(Size[0])) : 1.8f;
	const float H = Size.size() >= 3 ? static_cast<float>(vila::to_num(Size[1])) : 1.4f;
	const float L = Size.size() >= 3 ? static_cast<float>(vila::to_num(Size[2])) : 4.4f;
	LengthCm = L * 100.f;
	BoxZ = (0.3f + H * 0.4f) * 100.f;
	Box->SetBoxExtent(FVector(L * 50.f, W * 50.f, H * 40.f));
	Visual->Build(VilaConv::S(vila::jstr(Data, "kind", "sedan")), W, H, L, Color);
	const FVector P = GetActorLocation();
	VisualZ = VilaWorld ? VilaWorld->GroundHeight(P.X, P.Y) : 0.f;
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualZ - BoxZ));
	BarkText->SetRelativeLocation(FVector(0.f, 0.f, 240.f - BoxZ));
}

void AVilaTrafficCar::StartOnEdge(int32 EdgeIndex, const FVector& Pos)
{
	if (!Sys || !Sys->Edges.IsValidIndex(EdgeIndex)) return;
	const FVilaTrafficEdge& E = Sys->Edges[EdgeIndex];
	Edge = EdgeIndex;
	Mode = TEXT("edge");
	CurAxis = E.Axis;
	Points = {TCFlat(Pos), E.End};
	Idx = 1;
	SetActorLocation(FVector(Pos.X, Pos.Y, BoxZ), false, nullptr, ETeleportType::TeleportPhysics);
	Face(E.Dir, 1.f);
	Speed = 600.f;
}

void AVilaTrafficCar::Face(const FVector& Dir, float Weight)
{
	if (Dir.Size2D() < 1.f) return;
	const float Target = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	const float Cur = GetActorRotation().Yaw;
	const float Yaw = Cur + FMath::FindDeltaAngleDegrees(Cur, Target) * Weight;
	SetActorRotation(FRotator(0.f, FRotator::NormalizeAxis(Yaw), 0.f));
}

float AVilaTrafficCar::LaneSpeedFor(const FString& M) const
{
	if (M == TEXT("turn")) return AVilaTrafficSystem::TurnSpeed;
	if (M == TEXT("custom")) return 450.f;
	return Sys ? Sys->LaneSpeedCm : AVilaTrafficSystem::LaneSpeed;
}

void AVilaTrafficCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AVilaTrafficSystem* S = Sys;
	AVilaWorld* W = VilaWorld;
	if (!S || !W || DeltaSeconds <= 0.f) return;
	const float Dt = DeltaSeconds;
	HornCd -= Dt;
	if (!Bark.IsEmpty())
	{
		BarkT -= Dt;
		if (BarkT <= 0.f)
		{
			Bark.Reset();
			BarkText->SetVisibility(false);
		}
		else if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			// o texto do TextRender lê-se de +X: aponta para a câmera
			BarkText->SetWorldRotation((-Cam->GetCameraRotation().Vector()).Rotation());
		}
	}
	if (Idx >= Points.Num())
	{
		OnPointsEnd();
		return;
	}
	float TargetSpeed = LaneSpeedFor(Mode);
	const FVector Pos = TCFlat(GetActorLocation());

	// Desvio para o drive-thru quando passa pelo ponto de entrada.
	if (bCustomer && Mode == TEXT("edge") && Edge == S->JoinEdge && S->DrivePath.Num() > 0 && OrderState.IsEmpty())
	{
		const float X0 = S->DrivePath[0].X;
		if (Pos.X >= X0 - 60.f && Pos.X < X0 + 300.f)
		{
			Mode = TEXT("custom");
			Points = S->DrivePath;
			Idx = 1;
			Visual->SetIndicator(0);
			return;
		}
	}
	// Faixa de retenção / pedido de passagem no cruzamento.
	if (Mode == TEXT("edge") && S->Edges.IsValidIndex(Edge))
	{
		const FVilaTrafficEdge& E = S->Edges[Edge];
		const float DistEnd = FVector::Dist2D(Pos, E.End);
		if (DistEnd < 1400.f && !bGranted)
		{
			if (NextEdge < 0)
			{
				const bool bToJoin = bCustomer && S->JoinEdge >= 0 && E.B == S->Edges[S->JoinEdge].A;
				NextEdge = bToJoin ? S->JoinEdge : S->ChooseNext(Edge);
				UpdateIndicator();
			}
			if (S->CanEnter(this, E.B, CurAxis, DistEnd))
				bGranted = true;
			else
				TargetSpeed = FMath::Min(TargetSpeed, TCStopSpeed(500.f, DistEnd - 60.f));
		}
	}
	// Janela do drive-thru
	if (Mode == TEXT("custom") && bCustomer && S->DrivePath.IsValidIndex(S->WindowIndex))
	{
		const FVector WPos = S->DrivePath[S->WindowIndex];
		const float Dw = FVector::Dist2D(Pos, WPos);
		if (Idx <= S->WindowIndex && OrderState != TEXT("leaving"))
		{
			if (Dw < 900.f) TargetSpeed = FMath::Min(TargetSpeed, TCStopSpeed(300.f, Dw - 20.f));
			if (Dw < 60.f)
			{
				AtWindow();
				if (OrderState != TEXT("leaving")) TargetSpeed = 0.f;
			}
		}
		else
		{
			TargetSpeed = FMath::Min(TargetSpeed, 400.f);
		}
	}
	// Obstáculos à frente
	const FVector Fwd = TCFlat(GetActorForwardVector()).GetSafeNormal();
	const FVector Right = TCFlat(GetActorRightVector()).GetSafeNormal();
	const float Look = 500.f + Speed * 1.8f;
	bool bBlocker = false;
	bool bBlockerPlayer = false;
	float Gap = 1e9f;
	// Desfaz nó no cruzamento: parado há tempo dentro ou na boca de um cruzamento, ignora
	// os outros carros do trânsito (pedestres e o jogador continuam sendo respeitados);
	// senão carros fazendo a curva travam uns aos outros para sempre.
	const bool bTurn = Mode == TEXT("turn");
	const bool bJammed = StoppedT > (bTurn ? 3.f : 6.f) && (bTurn || bGranted) && S->NearNode(Pos, 1200.f);
	for (const FVilaTrafficObstacle& O : S->Obstacles())
	{
		if (O.Actor.Get() == this) continue;
		if (bJammed && O.bTraffic) continue;
		const FVector Rel = TCFlat(O.Pos - Pos);
		const float Along = Rel | Fwd;
		if (Along <= 0.f || Along > Look + LengthCm) continue;
		if (FMath::Abs(Rel | Right) > 110.f + O.Width) continue;
		const float G = Along - LengthCm * 0.5f - O.Half;
		if (G < Gap)
		{
			Gap = G;
			bBlocker = true;
			bBlockerPlayer = O.bPlayer;
		}
	}
	if (bBlocker) TargetSpeed = FMath::Min(TargetSpeed, TCStopSpeed(600.f, Gap - 180.f));
	// Aceleração/frenagem
	if (Speed < TargetSpeed)
		Speed = FMath::Min(TargetSpeed, Speed + 300.f * Dt);
	else
		Speed = FMath::Max(TargetSpeed, Speed - 800.f * Dt);
	if (Speed < 10.f)
	{
		StoppedT += Dt;
		if (bBlocker && bBlockerPlayer && StoppedT > 3.f && HornCd <= 0.f)
		{
			HornCd = 5.f;
			if (UVilaAudio* A = UVilaAudio::Get(this)) A->PlayAt(TEXT("horn"), GetActorLocation(), -2.f, FMath::FRandRange(0.9f, 1.1f));
			const vila::Json& Lines = vila::jarr(vila::data().chatter(), "horn");
			Say(Lines.empty() ? VilaConv::Tr("Anda!") : VilaConv::Tr(vila::to_str(vila::game().rng.pick(Lines))));
		}
		if (StoppedT > 30.f && W->Player)
		{
			FVector PP = W->Player->GetActorLocation();
			if (W->Player->State == TEXT("driving") && W->Player->Vehicle) PP = W->Player->Vehicle->GetActorLocation();
			if (FVector::Dist2D(Pos, PP) > 4500.f)
			{
				S->RemoveCar(this);
				return;
			}
		}
	}
	else
	{
		StoppedT = 0.f;
	}
	// Avança pelo polígono de pontos
	float Step = Speed * Dt;
	FVector P = Pos;
	while (Step > 0.f && Idx < Points.Num())
	{
		const FVector To = TCFlat(Points[Idx] - P);
		const float D = To.Size();
		if (D <= Step)
		{
			P = TCFlat(Points[Idx]);
			Step -= D;
			++Idx;
		}
		else
		{
			P += To / D * Step;
			Step = 0.f;
		}
	}
	const FVector Mv = P - Pos;
	if (Mv.Size() > 0.1f) Face(Mv, FMath::Min(1.f, Dt * 8.f));
	SetActorLocation(FVector(P.X, P.Y, BoxZ), false, nullptr, ETeleportType::None);
	Visual->Animate(Speed, 0.f, Dt);
	Visual->SetLights(vila::clock().is_night(), TargetSpeed < Speed - 50.f);
	VisualZ = FMath::Lerp(VisualZ, W->GroundHeight(P.X, P.Y), FMath::Min(1.f, Dt * 8.f));
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualZ - BoxZ));
}

void AVilaTrafficCar::UpdateIndicator()
{
	// Seta para o lado da curva (melhoria visual; o Godot não tinha).
	AVilaTrafficSystem* S = Sys;
	if (!S || !S->Edges.IsValidIndex(Edge) || !S->Edges.IsValidIndex(NextEdge))
	{
		Visual->SetIndicator(0);
		return;
	}
	const FVector& D1 = S->Edges[Edge].Dir;
	const FVector& D2 = S->Edges[NextEdge].Dir;
	const double Cross = D1.X * D2.Y - D1.Y * D2.X;  // UE (Y à direita de X): > 0 = direita
	Visual->SetIndicator(Cross > 0.5 ? 1 : (Cross < -0.5 ? -1 : 0));
}

void AVilaTrafficCar::OnPointsEnd()
{
	AVilaTrafficSystem* S = Sys;
	if (!S) return;
	if (Mode == TEXT("edge"))
	{
		if (!bGranted) return;
		if (NextEdge < 0) NextEdge = S->ChooseNext(Edge);
		Mode = TEXT("turn");
		Points = S->Connector(Edge, NextEdge);
		Idx = 0;
	}
	else if (Mode == TEXT("turn"))
	{
		if (S->Edges.IsValidIndex(Edge)) S->Release(this, S->Edges[Edge].B);
		Edge = NextEdge;
		NextEdge = -1;
		bGranted = false;
		Visual->SetIndicator(0);
		if (!S->Edges.IsValidIndex(Edge))
		{
			S->RemoveCar(this);
			return;
		}
		CurAxis = S->Edges[Edge].Axis;
		Mode = TEXT("edge");
		Points = {TCFlat(GetActorLocation()), S->Edges[Edge].End};
		Idx = 1;
	}
	else if (Mode == TEXT("custom"))
	{
		// Volta para a avenida como um carro comum.
		if (bCustomer) vila::game().job.customer_left(VilaConv::U(CustomerId));
		bCustomer = false;
		Edge = S->JoinEdge;
		if (!S->Edges.IsValidIndex(Edge))
		{
			S->RemoveCar(this);
			return;
		}
		CurAxis = S->Edges[Edge].Axis;
		bGranted = false;
		Mode = TEXT("edge");
		Points = {TCFlat(GetActorLocation()), S->Edges[Edge].End};
		Idx = 1;
	}
}

void AVilaTrafficCar::AtWindow()
{
	vila::Game& G = vila::game();
	vila::DriveThruJob& Job = G.job;
	const std::string Id = VilaConv::U(CustomerId);
	const std::string Status = Job.status_of(Id);
	UVilaAudio* Audio = UVilaAudio::Get(this);
	if (OrderState.IsEmpty())
	{
		if (Job.on_shift)
		{
			Job.create_order(Id, G.rng);
			OrderState = TEXT("ordered");
			if (Audio) Audio->PlayAt(TEXT("order"), GetActorLocation(), 0.f);
			const vila::Json Order = Job.order_for(Id);
			Say(VilaConv::S(vila::fmt(vila::tr("Boa tarde! Quero %s."), {vila::DriveThruJob::order_text(vila::jobj(Order, "items"))})));
		}
		else
		{
			Job.customers_requested = FMath::Max(0, Job.customers_requested - 1);
			OrderState = TEXT("leaving");
			Say(VilaConv::Tr("Ué, fechado?"));
		}
	}
	else if (OrderState == TEXT("ordered"))
	{
		if (Status == "done")
		{
			OrderState = TEXT("leaving");
			Say(VilaConv::Tr("Valeu! Até a próxima."));
		}
		else if (Status == "failed")
		{
			OrderState = TEXT("leaving");
			Say(VilaConv::Tr("Demorou demais! Nunca mais volto."));
			if (Audio) Audio->PlayAt(TEXT("horn"), GetActorLocation(), 0.f);
		}
		else if (Status == "none")
		{
			OrderState = TEXT("leaving");
		}
	}
}

void AVilaTrafficCar::OnHitByPlayer(float Impact)
{
	Damage += Impact;
	Say(VilaConv::Tr("Ô, barbeiro! Olha o que você fez!"));
	if (UVilaAudio* A = UVilaAudio::Get(this)) A->PlayAt(TEXT("horn"), GetActorLocation(), 2.f);
	if (Impact > 6.f)
	{
		vila::Game& G = vila::game();
		G.law.report("batida", "", vila::Json::array({"motorista_" + VilaConv::U(CustomerId)}), vila::clock().day(), G.relations);
		vila::events().notify.emit(vila::tr("O motorista anotou sua placa. Você será multado."), std::string("bad"));
	}
}

void AVilaTrafficCar::Say(const FString& Text)
{
	Bark = Text;
	BarkT = 3.5f;
	BarkText->SetText(FText::FromString(Text));
	BarkText->SetVisibility(true);
}

FString AVilaTrafficCar::CurrentBark() const
{
	return Bark;
}
