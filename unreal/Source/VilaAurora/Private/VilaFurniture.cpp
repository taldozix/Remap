#include "VilaFurniture.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "VilaAudio.h"
#include "VilaConv.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Events.h"
#include "Vila/Game.h"

using namespace VilaConv;
using VilaW::Trf;

namespace
{
	const TMap<FString, FString>& Titles()
	{
		static const TMap<FString, FString> T = {
			{TEXT("bed"), TEXT("Cama")}, {TEXT("wardrobe"), TEXT("Guarda-roupa")}, {TEXT("fridge"), TEXT("Geladeira")}, {TEXT("chest"), TEXT("Baú")},
			{TEXT("crate"), TEXT("Caixas")}, {TEXT("trash"), TEXT("Lixeira")}, {TEXT("stove"), TEXT("Fogão")}, {TEXT("sink"), TEXT("Pia")},
			{TEXT("shower"), TEXT("Chuveiro")}, {TEXT("sofa"), TEXT("Sofá")}, {TEXT("chair"), TEXT("Cadeira")}, {TEXT("bench"), TEXT("Banco")},
			{TEXT("note"), TEXT("Bilhete")}, {TEXT("counter"), TEXT("Balcão")}, {TEXT("grill"), TEXT("Chapa")}, {TEXT("fryer"), TEXT("Fritadeira")},
			{TEXT("soda_machine"), TEXT("Máquina de refrigerante")}, {TEXT("drive_window"), TEXT("Janela do drive-thru")},
			{TEXT("time_clock"), TEXT("Relógio de ponto")}, {TEXT("pump"), TEXT("Bomba de combustível")}, {TEXT("doormat"), TEXT("Tapete")},
			{TEXT("sale_sign"), TEXT("Placa de \"Vende-se\"")}, {TEXT("mailbox"), TEXT("Caixa de correio")},
		};
		return T;
	}

	bool KindIn(const FString& K, std::initializer_list<const TCHAR*> L)
	{
		for (const TCHAR* Item : L)
			if (K == Item) return true;
		return false;
	}

	constexpr int32 FuelPrice = 6;

	void PlaySound(const UObject* Ctx, const TCHAR* Id)
	{
		if (UVilaAudio* Audio = UVilaAudio::Get(Ctx)) Audio->Play(Id);
	}
}

AVilaFurniture::AVilaFurniture()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	VisualRoot->SetupAttachment(Root);
}

void AVilaFurniture::Setup(const vila::Json& InDef, AVilaWorld* World, const FString& InBuildingId)
{
	Def = InDef;
	Kind = S(vila::jstr(InDef, "type"));
	VilaWorld = World;
	BuildingId = InBuildingId;
	// str(p_def.get("id", "")) / str(p_def.get("owner", ""))
	ItemId = vila::has(InDef, "id") ? S(vila::to_str(vila::jget(InDef, "id"))) : FString();
	OwnerId = vila::has(InDef, "owner") ? S(vila::to_str(vila::jget(InDef, "owner"))) : FString();
	Facing = static_cast<float>(vila::jnum(InDef, "rot", 0.0));
	const FString* KindTitle = Titles().Find(Kind);
	const std::string DefaultTitle = U(KindTitle ? *KindTitle : Kind);
	if (vila::has(InDef, "container"))
	{
		const vila::Json& C = vila::jobj(InDef, "container");
		ContainerId = S(vila::jstr(C, "id"));
		OwnerId = vila::has(C, "owner") ? S(vila::to_str(vila::jget(C, "owner"))) : FString();
		Title = Tr(vila::jstr(C, "name", DefaultTitle));
	}
	else
	{
		Title = Tr(DefaultTitle);
	}
	if (Kind == TEXT("counter")) Title = Tr(vila::jstr(vila::data().shop(vila::jstr(InDef, "shop")), "name", "Balcão"));
	if (Kind == TEXT("bench"))
		SeatHeightCm = 45.f;
	else if (Kind == TEXT("sofa"))
		SeatHeightCm = 42.f;
	RadiusCm = KindIn(Kind, {TEXT("counter"), TEXT("drive_window"), TEXT("pump"), TEXT("bed")}) ? 190.f : 160.f;
	if (KindIn(Kind, {TEXT("grill"), TEXT("fryer")}) && !StatusLabel)
	{
		// Rótulo de estado da estação (Label3D com billboard no Godot).
		StatusLabel = NewObject<UTextRenderComponent>(this, TEXT("StatusLabel"));
		StatusLabel->SetupAttachment(Root);
		StatusLabel->SetRelativeLocation(FVector(0.0, 0.0, 160.0));
		StatusLabel->SetHorizontalAlignment(EHTA_Center);
		StatusLabel->SetVerticalAlignment(EVRTA_TextCenter);
		StatusLabel->SetWorldSize(24.f);
		StatusLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StatusLabel->SetCanEverAffectNavigation(false);
		StatusLabel->SetCastShadow(false);
		StatusLabel->SetText(FText::GetEmpty());
		StatusLabel->RegisterComponent();
		AddInstanceComponent(StatusLabel);
	}
	SetActorTickEnabled(StatusLabel != nullptr);
}

void AVilaFurniture::BeginPlay()
{
	Super::BeginPlay();
	// Só as estações com rótulo precisam de Tick.
	SetActorTickEnabled(StatusLabel != nullptr);
}

FVector AVilaFurniture::Forward() const
{
	return FRotator(0.0, YawFromGodot(Facing), 0.0).Vector();
}

// Ponto de uso: um pouco à frente do móvel.
FVector AVilaFurniture::GetInteractPoint() const
{
	float Off = 50.f;
	if (Kind == TEXT("counter")) Off = 75.f;
	if (KindIn(Kind, {TEXT("doormat"), TEXT("pump"), TEXT("trash"), TEXT("bench"), TEXT("sofa"), TEXT("chair"), TEXT("note"), TEXT("bed"), TEXT("drive_window"),
			TEXT("sale_sign"), TEXT("mailbox")}))
		Off = 0.f;
	return GetActorLocation() + Forward() * Off;
}

FTransform AVilaFurniture::SeatTransform() const
{
	FVector P = GetActorLocation() + Forward() * 12.0;
	P.Z = 0.0;
	return FTransform(FRotator(0.0, YawFromGodot(Facing), 0.0), P);
}

void AVilaFurniture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!StatusLabel)
	{
		SetActorTickEnabled(false);
		return;
	}
	UpdateStatusLabel();
	// Billboard: o texto do TextRender fica de frente para +X; gira para a câmera.
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager)
		{
			const FVector To = PC->PlayerCameraManager->GetCameraLocation() - StatusLabel->GetComponentLocation();
			if (!To.IsNearlyZero()) StatusLabel->SetWorldRotation(To.Rotation());
		}
}

void AVilaFurniture::UpdateStatusLabel()
{
	const std::string Station = vila::jstr(Def, "station");
	const vila::Json& St = vila::jobj(vila::game().job.stations, Station);
	const std::string State = vila::jstr(St, "state", "idle");
	if (!vila::game().job.on_shift || State == "idle")
	{
		StatusLabel->SetText(FText::GetEmpty());
		return;
	}
	if (State == "cooking")
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), static_cast<int32>(vila::game().job.station_progress(Station) * 100))));
		StatusLabel->SetTextRenderColor(FLinearColor(1.f, 0.85f, 0.3f).ToFColor(true));
	}
	else if (State == "ready")
	{
		StatusLabel->SetText(FText::FromString(Tr("PRONTO!")));
		StatusLabel->SetTextRenderColor(FLinearColor(0.4f, 1.f, 0.4f).ToFColor(true));
	}
	else if (State == "burnt")
	{
		StatusLabel->SetText(FText::FromString(Tr("QUEIMOU")));
		StatusLabel->SetTextRenderColor(FLinearColor(1.f, 0.3f, 0.3f).ToFColor(true));
	}
}

bool AVilaFurniture::IsMine() const
{
	return OwnerId.IsEmpty() || vila::game().is_mine(U(OwnerId), U(BuildingId));
}

bool AVilaFurniture::JobActionsAllowed() const
{
	return vila::game().job.on_shift;
}

double AVilaFurniture::VehicleFuel(AVilaVehicle* V) const
{
	// O contrato do AVilaVehicle não expõe o combustível: grava o estado e lê de lá.
	if (!V) return 0.0;
	V->SaveState();
	return vila::jnum(vila::jobj(vila::game().world_state.vehicles, U(V->VehicleId)), "fuel", 0.0);
}

TArray<FVilaAction> AVilaFurniture::GetActions(AVilaPlayerCharacter* Player)
{
	TArray<FVilaAction> Out;
	vila::Game& G = vila::game();
	if (Kind == TEXT("bed"))
	{
		if (G.is_mine(U(OwnerId), U(BuildingId)))
		{
			Out.Add(FVilaAction(TEXT("sleep"), Tr("Dormir...")));
			Out.Add(FVilaAction(TEXT("lie"), Tr("Deitar e descansar")));
		}
		else
		{
			Out.Add(FVilaAction(TEXT("none"), Tr("Dormir"), false, Tr("Não é a sua cama")));
		}
	}
	else if (KindIn(Kind, {TEXT("wardrobe"), TEXT("fridge"), TEXT("chest"), TEXT("crate")}))
	{
		if (!ContainerId.IsEmpty())
		{
			FString Label = Trf("Abrir %s", U(Title));
			if (!OwnerId.IsEmpty() && !G.is_mine(U(OwnerId), U(BuildingId)) && OwnerId != TEXT("owner"))
				Label += TEXT(" ") + Trf("(pertence a %s)", vila::tr(vila::jstr(vila::data().npc(U(OwnerId)), "name", U(OwnerId))));
			Out.Add(FVilaAction(TEXT("open"), Label));
		}
	}
	else if (Kind == TEXT("trash"))
	{
		Out.Add(FVilaAction(TEXT("open"), Tr("Vasculhar a lixeira")));
	}
	else if (Kind == TEXT("stove"))
	{
		const FString CanCook = PlayerHasCookable();
		Out.Add(FVilaAction(TEXT("cook"), Tr("Preparar macarrão (precisa de água)"), !CanCook.IsEmpty(), Tr("Você precisa de macarrão instantâneo e água")));
	}
	else if (Kind == TEXT("sink"))
	{
		Out.Add(FVilaAction(TEXT("drink"), Tr("Beber água da torneira")));
		Out.Add(FVilaAction(TEXT("wash"), Tr("Lavar o rosto e as mãos")));
	}
	else if (Kind == TEXT("shower"))
	{
		Out.Add(FVilaAction(TEXT("shower"), Tr("Tomar banho"), IsMine(), Tr("Não é o seu banheiro")));
	}
	else if (KindIn(Kind, {TEXT("sofa"), TEXT("chair"), TEXT("bench")}))
	{
		Out.Add(FVilaAction(TEXT("sit"), Tr("Sentar")));
	}
	else if (Kind == TEXT("note"))
	{
		Out.Add(FVilaAction(TEXT("read"), Tr("Ler bilhete")));
	}
	else if (Kind == TEXT("counter"))
	{
		if (!VilaWorld) return Out;
		const FString ShopId = S(vila::jstr(Def, "shop"));
		const FString Reason = VilaWorld->ShopUnavailableReason(ShopId);
		Out.Add(FVilaAction(TEXT("shop"), Tr("Comprar / vender"), Reason.IsEmpty(), Reason));
		if (AVilaNPC* Staff = VilaWorld->StaffAt(S(vila::jstr(Def, "staff_spot"))))
		{
			Out.Append(VilaWorld->ExtraNPCActions(Staff));
			Out.Add(FVilaAction(TEXT("talk_staff"), Trf("Conversar com %s", U(Staff->DisplayName()))));
		}
		if (ShopId == TEXT("mercadinho") && G.delivery.hired)
		{
			vila::Json C = G.delivery.can_take();
			if (!Reason.IsEmpty()) C = vila::Json{{"ok", false}, {"reason", U(Reason)}};
			Out.Add(FVilaAction(TEXT("delivery"), Tr("Pegar uma entrega"), vila::jbool(C, "ok"), S(vila::jstr(C, "reason"))));
		}
	}
	else if (KindIn(Kind, {TEXT("grill"), TEXT("fryer")}))
	{
		Out.Append(StationActions());
	}
	else if (Kind == TEXT("soda_machine"))
	{
		Out.Add(FVilaAction(TEXT("soda"), Tr("Servir refrigerante no copo"), JobActionsAllowed(), Tr("Só durante o turno")));
	}
	else if (Kind == TEXT("drive_window"))
	{
		const vila::Json O = G.job.front_order();
		if (!G.job.on_shift)
			Out.Add(FVilaAction(TEXT("none"), Tr("Janela do drive-thru"), false, Tr("Só durante o turno")));
		else if (O.empty())
			Out.Add(FVilaAction(TEXT("none"), Tr("Nenhum cliente na janela"), false));
		else
			Out.Add(FVilaAction(TEXT("deliver"), Trf("Entregar pedido #%d (falta: %s)", vila::jget(O, "id"), vila::DriveThruJob::order_text(vila::jget(O, "remaining")))));
	}
	else if (Kind == TEXT("time_clock"))
	{
		if (G.job.on_shift)
			Out.Add(FVilaAction(TEXT("clock_out"), Tr("Bater ponto (saída)")));
		else
			Out.Add(FVilaAction(TEXT("clock_in"), Tr("Bater ponto (entrada)"), G.job.employed, !G.job.fired ? Tr("Você não trabalha aqui") : Tr("Você foi demitido(a)")));
	}
	else if (Kind == TEXT("pump"))
	{
		AVilaVehicle* V = VilaWorld ? VilaWorld->PlayerVehicleNear(GetActorLocation(), 600.f) : nullptr;
		if (!V)
		{
			Out.Add(FVilaAction(TEXT("none"), Trf("Abastecer (R$ %d/L)", FuelPrice), false, Tr("Estacione o seu carro ao lado da bomba")));
		}
		else
		{
			const double Need = V->FuelCapacity() - VehicleFuel(V);
			Out.Add(FVilaAction(TEXT("fuel"), Trf("Abastecer %s (R$ %d/L, faltam %.0f L)", U(V->DisplayName()), FuelPrice, Need), Need > 0.5));
		}
	}
	else if (Kind == TEXT("doormat"))
	{
		Out.Add(FVilaAction(TEXT("search"), Tr("Olhar debaixo do tapete")));
	}
	else if (Kind == TEXT("sale_sign"))
	{
		Out.Append(SaleActions());
	}
	else if (Kind == TEXT("mailbox"))
	{
		const std::string Pid = vila::jstr(Def, "property");
		Out.Add(FVilaAction(TEXT("mail"), Tr("Ver correspondência")));
		if (vila::jstr(vila::jobj(G.property.props, Pid), "status") == "alugada")
			Out.Add(FVilaAction(TEXT("end_lease"), Tr("Entregar a casa ao senhorio (encerrar aluguel)"), G.property.owned_homes().size() > 1, Tr("Você precisa de outra casa antes")));
	}
	return Out;
}

TArray<FVilaAction> AVilaFurniture::SaleActions() const
{
	TArray<FVilaAction> Out;
	vila::Game& G = vila::game();
	if (vila::has(Def, "vehicle"))
	{
		const std::string Vid = vila::jstr(Def, "vehicle");
		const vila::Json& Vst = vila::jobj(G.world_state.vehicles, Vid);
		if (vila::has(Vst, "for_sale"))
			Out.Add(FVilaAction(TEXT("info"),
				Trf("À venda: %s — R$ %d (fale com o Rafael ou use o carro)", vila::tr(vila::jstr(vila::data().vehicle(vila::jstr(Vst, "type")), "name", Vid)),
					vila::jint(vila::jobj(Vst, "for_sale"), "price")),
				false));
		else
			Out.Add(FVilaAction(TEXT("none"), Tr("Vendido."), false));
		return Out;
	}
	const std::string Pid = vila::jstr(Def, "property");
	const vila::Json& PDef = vila::data().property(Pid);
	if (G.property.owns(Pid))
	{
		Out.Add(FVilaAction(TEXT("sell_property"), Trf("Vender %s por R$ %d", vila::tr(vila::jstr(PDef, "name")), G.property.sale_value(Pid))));
	}
	else if (G.property.is_for_sale(Pid))
	{
		const int Price = G.property.price(Pid);
		Out.Add(FVilaAction(TEXT("buy_property"), Trf("Comprar %s por R$ %d", vila::tr(vila::jstr(PDef, "name")), Price), G.player.money >= Price,
			Trf("Dinheiro insuficiente (você tem R$ %d)", G.player.money)));
		Out.Add(FVilaAction(TEXT("property_info"), Tr("Ver detalhes do imóvel")));
	}
	return Out;
}

TArray<FVilaAction> AVilaFurniture::StationActions() const
{
	TArray<FVilaAction> Out;
	vila::Game& G = vila::game();
	if (!G.job.on_shift)
	{
		Out.Add(FVilaAction(TEXT("none"), Title, false, Tr("Só durante o turno")));
		return Out;
	}
	const std::string Sname = vila::jstr(Def, "station");
	const vila::Json& St = vila::jobj(G.job.stations, Sname);
	const std::string Product = vila::data().item_name(vila::jstr(vila::jobj(vila::jobj(vila::data().job("brasa"), "stations"), Sname), "product"));
	const std::string State = vila::jstr(St, "state", "idle");
	if (State == "idle")
		Out.Add(FVilaAction(TEXT("start"), Trf("Começar a preparar: %s", Product)));
	else if (State == "cooking")
		Out.Add(FVilaAction(TEXT("none"), Trf("Preparando... %d%%", static_cast<int>(G.job.station_progress(Sname) * 100)), false));
	else if (State == "ready")
		Out.Add(FVilaAction(TEXT("collect"), Trf("Retirar %s", Product)));
	else if (State == "burnt")
		Out.Add(FVilaAction(TEXT("collect"), Tr("Jogar fora o que queimou")));
	return Out;
}

FString AVilaFurniture::PlayerHasCookable() const
{
	const vila::Inventory& Inv = vila::game().player.inventory;
	for (const vila::Json& St : Inv.items)
	{
		const std::string Id = vila::jstr(St, "id");
		const vila::Json& Cook = vila::jobj(vila::data().item(Id), "cook");
		if (Cook.empty()) continue;
		bool bOk = true;
		for (const auto& [Need, Qty] : vila::jobj(Cook, "needs").items())
			if (!Inv.has(Need, vila::to_int(Qty))) bOk = false;
		if (bOk) return S(Id);
	}
	return FString();
}

float AVilaFurniture::JobTime(const std::string& Key) const
{
	const double Base = vila::jnum(vila::jobj(vila::data().job("brasa"), "actions"), Key, 1.5);
	const int Skill = vila::game().player.skills.level("culinaria");
	return static_cast<float>(Base * vila::game().player.needs.work_multiplier() * (1.0 - 0.05 * Skill));
}

void AVilaFurniture::DoAction(AVilaPlayerCharacter* Player, const FString& Id)
{
	vila::Game& G = vila::game();
	if (Kind == TEXT("counter") && VilaWorld)
		if (AVilaNPC* Staff = VilaWorld->StaffAt(S(vila::jstr(Def, "staff_spot"))))
			if (VilaWorld->DoExtraNPCAction(Staff, Id)) return;
	TWeakObjectPtr<AVilaFurniture> Self(this);
	const std::string Station = vila::jstr(Def, "station");

	if (Id == TEXT("sleep"))
	{
		vila::events().request_sleep.emit(U(ItemId), 1.0);
	}
	else if (Id == TEXT("lie"))
	{
		if (Player) Player->RestOnBed(this);
	}
	else if (Id == TEXT("open"))
	{
		if (Kind == TEXT("trash") && !G.world_state.searched.contains(U(ContainerId))) G.world_state.searched[U(ContainerId)] = vila::clock().day();
		vila::events().request_container.emit(U(ContainerId), U(Title));
	}
	else if (Id == TEXT("cook"))
	{
		const FString Item = PlayerHasCookable();
		if (!Item.IsEmpty() && Player)
			Player->StartTimed(Tr("Cozinhando..."), 4.f, [Self, Item]() { if (Self.IsValid()) Self->Cook(Item); }, TEXT("sizzle"));
	}
	else if (Id == TEXT("drink"))
	{
		if (Player) Player->StartTimed(Tr("Bebendo água..."), 1.5f, [Self]() { if (Self.IsValid()) Self->ConsumeFx(vila::Json{{"sede", -25}}, TEXT("drink")); });
	}
	else if (Id == TEXT("wash"))
	{
		if (Player) Player->StartTimed(Tr("Lavando o rosto..."), 2.f, [Self]() { if (Self.IsValid()) Self->ConsumeFx(vila::Json{{"higiene", -12}}, FString()); });
	}
	else if (Id == TEXT("shower"))
	{
		if (Player) Player->StartTimed(Tr("Tomando banho..."), 5.f, [Self]() { if (Self.IsValid()) Self->Shower(); });
	}
	else if (Id == TEXT("sit"))
	{
		if (Player) Player->SitOn(this);
	}
	else if (Id == TEXT("read"))
	{
		const vila::Json& Note = vila::jobj(vila::jobj(vila::data().map(), "notes"), vila::jstr(Def, "note"));
		vila::events().request_message.emit(vila::tr(vila::jstr(Note, "title", "Bilhete")), vila::tr(vila::jstr(Note, "body", "")));
	}
	else if (Id == TEXT("shop"))
	{
		vila::events().request_shop.emit(vila::jstr(Def, "shop"));
	}
	else if (Id == TEXT("start"))
	{
		if (Player) Player->StartTimed(Tr("Preparando..."), JobTime(Station + "_start"), [Self]() { if (Self.IsValid()) Self->StationStart(); }, TEXT("sizzle"));
	}
	else if (Id == TEXT("collect"))
	{
		if (Player) Player->StartTimed(Tr("Retirando..."), JobTime(Station + "_collect"), [Self]() { if (Self.IsValid()) Self->StationCollect(); });
	}
	else if (Id == TEXT("soda"))
	{
		if (Player) Player->StartTimed(Tr("Servindo refrigerante..."), JobTime("refri"), [Self]() { if (Self.IsValid()) Self->Soda(); });
	}
	else if (Id == TEXT("deliver"))
	{
		TWeakObjectPtr<AVilaWorld> W(VilaWorld.Get());
		if (Player) Player->StartTimed(Tr("Entregando..."), JobTime("deliver"), [W]() { if (W.IsValid()) W->DeliverDriveOrder(); });
	}
	else if (Id == TEXT("clock_in"))
	{
		const vila::Json R = G.job.clock_in(vila::clock().hour_f(), G.player.needs);
		if (!vila::jbool(R, "ok"))
			vila::events().notify.emit(vila::jstr(R, "reason"), "bad");
		else
			PlaySound(this, TEXT("order"));
	}
	else if (Id == TEXT("clock_out"))
	{
		const vila::Json Sum = G.clock_out_player();
		if (!Sum.empty() && VilaWorld) VilaWorld->ShowShiftSummary(Sum);
	}
	else if (Id == TEXT("fuel"))
	{
		AVilaVehicle* V = VilaWorld ? VilaWorld->PlayerVehicleNear(GetActorLocation(), 600.f) : nullptr;
		if (V && Player)
		{
			TWeakObjectPtr<AVilaVehicle> WV(V);
			Player->StartTimed(Tr("Abastecendo..."), 3.f, [Self, WV]() { if (Self.IsValid() && WV.IsValid()) Self->Fuel(WV.Get()); });
		}
	}
	else if (Id == TEXT("talk_staff"))
	{
		if (VilaWorld)
			if (AVilaNPC* Staff = VilaWorld->StaffAt(S(vila::jstr(Def, "staff_spot")))) VilaWorld->StartDialogue(Staff);
	}
	else if (Id == TEXT("delivery"))
	{
		const vila::Json R = G.delivery.take(vila::clock().total_minutes, G.rng, G.player.inventory);
		if (!vila::jbool(R, "ok"))
		{
			vila::events().notify.emit(vila::jstr(R, "reason"), "bad");
		}
		else
		{
			if (VilaWorld) VilaWorld->RefreshDelivery();
			PlaySound(this, TEXT("pickup"));
		}
	}
	else if (Id == TEXT("buy_property"))
	{
		const std::string Pid = vila::jstr(Def, "property");
		const vila::Json R = G.property.buy(Pid, G.player);
		if (!vila::jbool(R, "ok"))
		{
			vila::events().notify.emit(vila::jstr(R, "reason"), "bad");
		}
		else
		{
			PlaySound(this, TEXT("cash"));
			const vila::Json& PD = vila::data().property(Pid);
			vila::events().request_message.emit(vila::tr(vila::jstr(PD, "name")),
				vila::fmt(vila::tr("O imóvel é seu. As portas agora abrem com a sua chave, e a cama, o chuveiro, o fogão e os armários podem ser usados livremente.\n\nIPTU: R$ %d por semana (domingo)."),
					{vila::jint(PD, "tax", 0)}));
		}
	}
	else if (Id == TEXT("sell_property"))
	{
		const vila::Json R = G.property.sell(vila::jstr(Def, "property"), G.player);
		if (!vila::jbool(R, "ok"))
			vila::events().notify.emit(vila::jstr(R, "reason"), "bad");
		else
			PlaySound(this, TEXT("cash"));
	}
	else if (Id == TEXT("property_info"))
	{
		const vila::Json& PD = vila::data().property(vila::jstr(Def, "property"));
		vila::events().request_message.emit(vila::tr(vila::jstr(PD, "name")),
			vila::fmt(vila::tr("%s\n\nPreço: R$ %d · IPTU semanal: R$ %d · Revenda: %d%% do valor pago."),
				{vila::tr(vila::jstr(PD, "description", "")), vila::jint(PD, "price", 0), vila::jint(PD, "tax", 0), static_cast<int>(vila::jnum(PD, "sale_rate", 0.8) * 100)}));
	}
	else if (Id == TEXT("mail"))
	{
		const int Debt = G.property.total_debt();
		std::string Body = Debt == 0 ? vila::tr("Nada de novo. Só propaganda do Brasa Burger.") : vila::fmt(vila::tr("Aviso do senhorio: você deve R$ %d. Pague até domingo."), {Debt});
		if (G.law.pending_fines > 0) Body += "\n" + vila::tr("Há uma notificação da Guarda Municipal a caminho.");
		vila::events().request_message.emit(vila::tr("Correspondência"), Body);
	}
	else if (Id == TEXT("end_lease"))
	{
		const vila::Json R = G.property.end_lease(vila::jstr(Def, "property"));
		const bool bOk = vila::jbool(R, "ok");
		vila::events().notify.emit(bOk ? vila::tr("Contrato encerrado. As chaves voltaram para o senhorio.") : vila::jstr(R, "reason"), bOk ? "info" : "bad");
	}
	else if (Id == TEXT("search"))
	{
		const std::string Hidden = vila::jstr(Def, "hidden_item");
		if (!Hidden.empty() && !G.world_state.searched.contains(U(ItemId)))
		{
			G.world_state.searched[U(ItemId)] = vila::clock().day();
			G.player.inventory.add(Hidden, 1, 0.0, false);
			vila::events().notify.emit(vila::fmt(vila::tr("Você encontrou: %s"), {vila::data().item_name(Hidden)}), "good");
			PlaySound(this, TEXT("pickup"));
		}
		else
		{
			vila::events().notify.emit(vila::tr("Nada além de poeira."), "info");
		}
	}
}

void AVilaFurniture::Cook(const FString& InItem)
{
	vila::Inventory& Inv = vila::game().player.inventory;
	const std::string Item = U(InItem);
	const vila::Json Cook = vila::jobj(vila::data().item(Item), "cook");
	if (!Inv.has(Item) || Cook.empty()) return;
	const vila::Json& Needs = vila::jobj(Cook, "needs");
	for (const auto& [Need, Qty] : Needs.items())
		if (!Inv.has(Need, vila::to_int(Qty))) return;
	Inv.remove(Item, 1);
	for (const auto& [Need, Qty] : Needs.items()) Inv.remove(Need, vila::to_int(Qty));
	const std::string Result = vila::jstr(Cook, "result");
	Inv.add(Result, 1, 0.0, false);
	vila::clock().advance(vila::jnum(Cook, "minutes", 10.0));
	vila::game().player.skills.add_xp("culinaria", 8);
	vila::events().notify.emit(vila::fmt(vila::tr("Pronto: %s"), {vila::data().item_name(Result)}), "good");
}

void AVilaFurniture::ConsumeFx(const vila::Json& Effects, const FString& Sound)
{
	vila::game().player.needs.consume(Effects);
	vila::events().needs_changed.emit();
	if (!Sound.IsEmpty())
		if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->Play(Sound);
}

void AVilaFurniture::Shower()
{
	vila::game().player.needs.set_need("higiene", 0.0);
	vila::game().player.needs.consume(vila::Json{{"fadiga", -10}});
	vila::clock().advance(15.0);
	vila::events().notify.emit(vila::tr("Banho tomado. Que alívio!"), "good");
}

void AVilaFurniture::StationStart()
{
	if (vila::game().job.station_start(vila::jstr(Def, "station"), vila::game().player.skills.level("culinaria")))
		if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->PlayAt(TEXT("sizzle"), GetActorLocation(), -6.f);
}

void AVilaFurniture::StationCollect()
{
	const std::string R = vila::game().job.station_collect(vila::jstr(Def, "station"), vila::game().player.inventory);
	if (R == "burnt")
	{
		vila::events().notify.emit(vila::tr("Descartado. Comece de novo."), "bad");
	}
	else if (!R.empty())
	{
		vila::game().player.skills.add_xp("culinaria", 6);
		PlaySound(this, TEXT("pickup"));
	}
}

void AVilaFurniture::Soda()
{
	if (!vila::game().job.on_shift) return;
	vila::game().player.inventory.add("job_refri", 1, 0.0, false);
	PlaySound(this, TEXT("pickup"));
}

void AVilaFurniture::Fuel(AVilaVehicle* V)
{
	if (!V) return;
	vila::PlayerState& PS = vila::game().player;
	const double Need = V->FuelCapacity() - VehicleFuel(V);
	const double Liters = FMath::Min(Need, FMath::FloorToDouble(static_cast<double>(PS.money) / FuelPrice));
	if (Liters < 0.5)
	{
		vila::events().notify.emit(vila::tr("Dinheiro insuficiente."), "bad");
		return;
	}
	const int Cost = static_cast<int>(FMath::CeilToDouble(Liters * FuelPrice));
	if (PS.spend(Cost, "Combustível"))
	{
		// v.fuel += liters; v.save_state()  ->  grava no estado e o veículo relê.
		const std::string Vid = U(V->VehicleId);
		vila::Json& St = vila::game().world_state.vehicles[Vid];
		St["fuel"] = vila::jnum(St, "fuel", 0.0) + Liters;
		V->RefreshFromState();
		vila::events().notify.emit(vila::fmt(vila::tr("Abastecido: %.1f L"), {Liters}), "good");
	}
}

void AVilaFurniture::SetFocused(bool bFocused)
{
	VilaW::SetActorOutline(this, bFocused);
	// O rótulo de estado não recebe contorno.
	if (StatusLabel) StatusLabel->SetRenderCustomDepth(false);
}
