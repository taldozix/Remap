#include "VilaPlayerController.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "VilaAurora.h"
#include "VilaCameraRig.h"
#include "VilaConv.h"
#include "VilaHUD.h"
#include "VilaPlayerCharacter.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "Vila/Settings.h"

using namespace VilaConv;

AVilaPlayerController::AVilaPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	// A câmera é o AVilaCameraRig, não o pawn possuído.
	bAutoManageActiveCameraTarget = false;
	bShowMouseCursor = false;
}

void AVilaPlayerController::BeginPlay()
{
	Super::BeginPlay();
	TWeakObjectPtr<AVilaPlayerController> Weak(this);
	SettingsConn = vila::settings().changed.connect([Weak](const std::string& Section)
	{
		if (Section != "input") return;
		if (AVilaPlayerController* Self = Weak.Get()) Self->bMappingDirty = true;
	});
	bMappingDirty = !ApplyMappings();
}

void AVilaPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	vila::settings().changed.disconnect(SettingsConn);
	Super::EndPlay(EndPlayReason);
}

AVilaWorld* AVilaPlayerController::GetVilaWorld() const
{
	return AVilaWorld::Get(this);
}

AVilaPlayerCharacter* AVilaPlayerController::GetVilaPlayer() const
{
	if (AVilaPlayerCharacter* P = Cast<AVilaPlayerCharacter>(GetPawn())) return P;
	const AVilaWorld* W = GetVilaWorld();
	return W ? W->Player.Get() : nullptr;
}

// ------------------------------------------------------------------ ações e mapeamento
void AVilaPlayerController::CreateActions()
{
	if (Actions.Num() > 0) return;
	for (const auto& [Name, ActionDef] : vila::Settings::actions().items())
	{
		const FString ActionName = S(Name);
		UInputAction* A = NewObject<UInputAction>(this, FName(*(TEXT("IA_") + ActionName)));
		A->ValueType = EInputActionValueType::Boolean;
		Actions.Add(ActionName, A);
	}
}

void AVilaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateActions();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogVila, Error, TEXT("InputComponent não é UEnhancedInputComponent (ver Config/DefaultInput.ini)."));
		return;
	}
	for (const TPair<FString, TObjectPtr<UInputAction>>& KV : Actions)
		EIC->BindAction(KV.Value.Get(), ETriggerEvent::Started, this, &AVilaPlayerController::OnActionStarted);
	bMappingDirty = true;
}

bool AVilaPlayerController::ApplyMappings()
{
	UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Sub) return false;
	CreateActions();

	UInputMappingContext* Ctx = NewObject<UInputMappingContext>(this);
	AxisActions.Reset();
	AxisBindings.Reset();
	for (const auto& [Name, Keys] : vila::settings().bindings.items())
	{
		const FString ActionName = S(Name);
		UInputAction* A = Actions.FindRef(ActionName);
		if (!A || !Keys.is_array()) continue;
		for (const vila::Json& K : Keys)
		{
			const FString KeyName = S(vila::to_str(K));
			if (KeyName.IsEmpty()) continue;
			const FKey Key{FName(*KeyName)};
			if (Key.IsValid())
			{
				Ctx->MapKey(A, Key);
				continue;
			}
			// "Gamepad_LeftY+" / "Gamepad_LeftX-": metade de um eixo.
			const bool bPos = KeyName.EndsWith(TEXT("+"));
			const bool bNeg = KeyName.EndsWith(TEXT("-"));
			const FKey Base{FName(*KeyName.LeftChop(1))};
			if ((bPos || bNeg) && Base.IsValid() && Base.IsAxis1D())
			{
				UInputAction* AA = NewObject<UInputAction>(this);
				AA->ValueType = EInputActionValueType::Axis1D;
				AxisActions.Add(AA);
				FEnhancedActionKeyMapping& Mapping = Ctx->MapKey(AA, Base);
				UInputModifierDeadZone* Dz = NewObject<UInputModifierDeadZone>(Ctx);
				Dz->LowerThreshold = 0.2f;
				Mapping.Modifiers.Add(Dz);
				if (bNeg) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
				FVilaAxisBinding B;
				B.Action = ActionName;
				B.InputAction = AA;
				AxisBindings.Add(B);
				continue;
			}
			UE_LOG(LogVila, Warning, TEXT("Tecla desconhecida '%s' na ação '%s' (ignorada)."), *KeyName, *ActionName);
		}
	}
	Context = Ctx;
	Sub->ClearAllMappings();
	Sub->AddMappingContext(Ctx, 0);
	return true;
}

void AVilaPlayerController::RebuildInputMappings()
{
	bMappingDirty = !ApplyMappings();
}

float AVilaPlayerController::RawStrength(const FString& Action) const
{
	const UEnhancedPlayerInput* EPI = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (!EPI) return 0.f;
	float V = 0.f;
	if (const TObjectPtr<UInputAction>* A = Actions.Find(Action))
		if (*A) V = EPI->GetActionValue(A->Get()).GetMagnitude() > 0.f ? 1.f : 0.f;
	for (const FVilaAxisBinding& B : AxisBindings)
		if (B.Action == Action && B.InputAction.IsValid())
			V = FMath::Max(V, FMath::Clamp(EPI->GetActionValue(B.InputAction.Get()).Get<float>(), 0.f, 1.f));
	return V;
}

float AVilaPlayerController::ActionStrength(const FString& Action) const
{
	return RawStrength(Action);
}

bool AVilaPlayerController::IsActionDown(const FString& Action) const
{
	return RawStrength(Action) >= 0.5f;
}

FVector2D AVilaPlayerController::GetMoveAxis() const
{
	if (bUIModal) return FVector2D::ZeroVector;
	FVector2D V(RawStrength(TEXT("move_right")) - RawStrength(TEXT("move_left")), RawStrength(TEXT("move_up")) - RawStrength(TEXT("move_down")));
	// Input.get_vector: limitado ao círculo unitário.
	if (V.SizeSquared() > 1.0) V.Normalize();
	return V;
}

// ------------------------------------------------------------------ quadro
void AVilaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (bMappingDirty) bMappingDirty = !ApplyMappings();

	// Bordas de subida dos eixos com sinal (o Started das ações comuns vem do Enhanced Input).
	for (int32 i = 0; i < AxisBindings.Num(); ++i)
	{
		const UEnhancedPlayerInput* EPI = Cast<UEnhancedPlayerInput>(PlayerInput);
		const bool bDown = EPI && AxisBindings[i].InputAction.IsValid() && EPI->GetActionValue(AxisBindings[i].InputAction.Get()).Get<float>() >= 0.5f;
		const bool bPressed = bDown && !AxisBindings[i].bPrevDown;
		AxisBindings[i].bPrevDown = bDown;
		if (bPressed) HandlePressed(AxisBindings[i].Action);
	}

	AVilaWorld* World = GetVilaWorld();
	if (World && World->CameraRig && GetViewTarget() != World->CameraRig.Get()) SetViewTarget(World->CameraRig.Get());

	// Dirigindo: o veículo recebe o input a cada quadro (Vehicle.gd lia Input no _physics_process).
	AVilaPlayerCharacter* P = GetVilaPlayer();
	if (P && P->State == TEXT("driving") && P->Vehicle)
	{
		const bool bBlocked = bUIModal || !World || World->UIBlocking();
		if (bBlocked)
		{
			P->Vehicle->SetDriveInput(0.f, 0.f, false);
		}
		else
		{
			const float Throttle = RawStrength(TEXT("move_up")) - RawStrength(TEXT("move_down"));
			const float Steer = RawStrength(TEXT("move_right")) - RawStrength(TEXT("move_left"));  // + = direita
			P->Vehicle->SetDriveInput(Throttle, Steer, IsActionDown(TEXT("handbrake")));
		}
	}
}

void AVilaPlayerController::OnActionStarted(const FInputActionInstance& Instance)
{
	const UInputAction* Src = Instance.GetSourceAction();
	for (const TPair<FString, TObjectPtr<UInputAction>>& KV : Actions)
		if (KV.Value.Get() == Src)
		{
			HandlePressed(KV.Key);
			return;
		}
}

void AVilaPlayerController::HandlePressed(const FString& Action)
{
	AVilaWorld* World = GetVilaWorld();
	AVilaPlayerCharacter* P = GetVilaPlayer();
	AVilaHUD* HUD = AVilaHUD::Get(this);
	const bool bModal = HUD && HUD->IsModalOpen();
	if (!bModal) LastPanel.Reset();
	const bool bPlaying = World && P;  // no menu principal não há jogador

	// UIManager._unhandled_input
	if (Action == TEXT("pause"))
	{
		if (!HUD || !bPlaying) return;
		if (bModal) HUD->CloseCurrent();
		else HUD->OpenPause();
		return;
	}
	// F3 (diagnóstico) e F5 (salvamento rápido): o AVilaHUD lê IsActionDown por borda de subida.
	if (Action == TEXT("debug") || Action == TEXT("quick_save")) return;
	if (Action == TEXT("inventory") || Action == TEXT("journal") || Action == TEXT("map"))
	{
		if (!HUD || !bPlaying) return;
		if (bModal)
		{
			// Inventário/diário/mapa fecham com a própria tecla; outras janelas ignoram.
			if (LastPanel == Action)
			{
				HUD->CloseCurrent();
				LastPanel.Reset();
			}
			return;
		}
		HUD->OpenPanel(Action);
		LastPanel = Action;
		return;
	}
	if (bModal || bUIModal || !World) return;

	// CameraRig._unhandled_input
	if (AVilaCameraRig* Rig = World->CameraRig.Get())
	{
		if (!Rig->IsMenuOrbit())
		{
			if (Action == TEXT("cam_left")) { Rig->RotateStep(1); return; }
			if (Action == TEXT("cam_right")) { Rig->RotateStep(-1); return; }
			if (Action == TEXT("zoom_in")) { Rig->ZoomStep(1); return; }
			if (Action == TEXT("zoom_out")) { Rig->ZoomStep(-1); return; }
		}
	}

	// Player._unhandled_input (+ buzina do Vehicle.gd)
	if (!P || P->bInputLocked || World->UIBlocking()) return;
	if (Action == TEXT("interact")) P->DoFocusAction(-1);
	else if (Action == TEXT("cycle_target")) P->CycleTarget();
	else if (Action == TEXT("vehicle")) P->OnVehicleKey();
	else if (Action == TEXT("horn"))
	{
		if (P->State == TEXT("driving") && P->Vehicle) P->Vehicle->Horn();
	}
	else if (Action.StartsWith(TEXT("option_")))
	{
		const int32 N = FCString::Atoi(*Action.RightChop(7));
		if (N >= 1 && N <= 4) P->DoFocusAction(N - 1);
	}
}

// ------------------------------------------------------------------ modo de interface
void AVilaPlayerController::SetUIMode(bool bModal)
{
	if (bModal == bUIModal && bShowMouseCursor == bModal) return;
	bUIModal = bModal;
	SetShowMouseCursor(bModal);
	// Teclas seguradas ao abrir/fechar a janela não ficam presas.
	FlushPressedKeys();
	if (bModal)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}
