#include "VilaHUD.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/SVilaContainerPanel.h"
#include "UI/SVilaDialoguePanel.h"
#include "UI/SVilaHUDOverlay.h"
#include "UI/SVilaInventoryPanel.h"
#include "UI/SVilaJournalPanel.h"
#include "UI/SVilaMainMenu.h"
#include "UI/SVilaMapPanel.h"
#include "UI/SVilaPauseMenu.h"
#include "UI/SVilaShopPanel.h"
#include "UI/VilaUIStyle.h"
#include "VilaAudio.h"
#include "VilaAurora.h"
#include "VilaConv.h"
#include "VilaGameInstance.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaPlayerController.h"
#include "VilaWorld.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Clock.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Saves.h"
#include "Vila/Settings.h"

using namespace VilaConv;

namespace
{
	constexpr float FadeSeconds = 0.4f;  // API.md (o Godot usava 0,6 s)
}

AVilaHUD::AVilaHUD()
{
	PrimaryActorTick.bCanEverTick = false;  // a interface roda no Tick do Slate (inclusive na pausa)
}

AVilaHUD* AVilaHUD::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AVilaHUD>(PC->GetHUD()) : nullptr;
}

void AVilaHUD::BeginPlay()
{
	Super::BeginPlay();
	VilaUI::SetContext(this);

	SAssignNew(Overlay, SVilaHUDOverlay).HUD(TWeakObjectPtr<AVilaHUD>(this));
	ViewportWidget = SNew(SWeakWidget).PossiblyNullContent(Overlay);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->AddViewportWidgetContent(ViewportWidget.ToSharedRef(), 10);

	ConnectEvents();

	// O menu principal entra no primeiro quadro (depois do BeginPlay do controller e do mundo).
	const UVilaGameInstance* GI = UVilaGameInstance::Get(this);
	if (GI && GI->GetFlow() == EVilaFlow::Menu)
	{
		TWeakObjectPtr<AVilaHUD> Self(this);
		Defer([Self]() { if (Self.IsValid()) Self->ShowMainMenu(); });
	}
}

void AVilaHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisconnectEvents();
	CurrentPanel.Reset();
	CurrentKind.Reset();
	MsgQueue.Empty();
	PendingShop.Reset();
	Deferred.Empty();
	while (ClockHolds > 0) ReleaseClock();
	if (bPausedGame)
	{
		UGameplayStatics::SetGamePaused(this, false);
		bPausedGame = false;
	}
	if (ViewportWidget.IsValid() && GEngine && GEngine->GameViewport)
		GEngine->GameViewport->RemoveViewportWidgetContent(ViewportWidget.ToSharedRef());
	ViewportWidget.Reset();
	Overlay.Reset();
	if (VilaUI::Context() == this) VilaUI::SetContext(nullptr);
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------- sinais do núcleo
void AVilaHUD::ConnectEvents()
{
	TWeakObjectPtr<AVilaHUD> Self(this);
	vila::Events& Ev = vila::events();
	ConnNotify = Ev.notify.connect([Self](const std::string& Text, const std::string& Kind)
	{
		if (Self.IsValid()) Self->OnNotify(S(Text), S(Kind));
	});
	ConnContainer = Ev.request_container.connect([Self](const std::string& Id, const std::string& Title)
	{
		if (Self.IsValid()) Self->OpenContainer(S(Id), S(Title));
	});
	ConnShop = Ev.request_shop.connect([Self](const std::string& Id)
	{
		if (Self.IsValid()) Self->OnRequestShop(S(Id));
	});
	ConnMessage = Ev.request_message.connect([Self](const std::string& Title, const std::string& Body)
	{
		if (Self.IsValid()) Self->ShowMessage(S(Title), S(Body));
	});
	ConnSleep = Ev.request_sleep.connect([Self](const std::string& Bed, double Quality)
	{
		if (Self.IsValid()) Self->OpenSleep(S(Bed), static_cast<float>(Quality));
	});
	ConnDialogue = Ev.request_dialogue.connect([Self](const std::string& Npc, const std::string& Dlg)
	{
		if (Self.IsValid()) Self->OnRequestDialogue(S(Npc), S(Dlg));
	});
	ConnSkill = Ev.skill_level_up.connect([](const std::string&, int) { VilaUI::PlaySound(TEXT("notify")); });
	ConnMoney = Ev.money_changed.connect([Self](int Total, int Delta)
	{
		if (Self.IsValid() && Self->Overlay.IsValid()) Self->Overlay->OnMoneyChanged(Total, Delta);
	});
	// Settings.changed -> _apply_theme: o tamanho do texto muda o HUD inteiro.
	ConnSettings = vila::settings().changed.connect([Self](const std::string& Section)
	{
		if (Section == "game" && Self.IsValid() && Self->Overlay.IsValid()) Self->Overlay->RebuildHUD();
	});
}

void AVilaHUD::DisconnectEvents()
{
	vila::Events& Ev = vila::events();
	Ev.notify.disconnect(ConnNotify);
	Ev.request_container.disconnect(ConnContainer);
	Ev.request_shop.disconnect(ConnShop);
	Ev.request_message.disconnect(ConnMessage);
	Ev.request_sleep.disconnect(ConnSleep);
	Ev.request_dialogue.disconnect(ConnDialogue);
	Ev.skill_level_up.disconnect(ConnSkill);
	Ev.money_changed.disconnect(ConnMoney);
	vila::settings().changed.disconnect(ConnSettings);
	ConnNotify = ConnContainer = ConnShop = ConnMessage = ConnSleep = ConnDialogue = ConnSkill = ConnMoney = ConnSettings = 0;
}

void AVilaHUD::OnNotify(const FString& Text, const FString& Kind)
{
	if (Overlay.IsValid()) Overlay->AddToast(Text, Kind);
	if (Kind == TEXT("quest") || Kind == TEXT("skill"))
		VilaUI::PlaySound(TEXT("notify"));
	else if (Kind == TEXT("bad"))
		VilaUI::PlaySound(TEXT("bad"), -8.f);
}

void AVilaHUD::OnRequestShop(const FString& ShopId)
{
	if (CurrentKind == TEXT("dialogue"))
		PendingShop = ShopId;
	else
		OpenShop(ShopId);
}

// Pedido de diálogo vindo das regras (o Godot declarava o sinal sem ouvinte).
void AVilaHUD::OnRequestDialogue(const FString& NpcId, const FString& DialogueId)
{
	AVilaWorld* World = GetVilaWorld();
	if (!World) return;
	const TObjectPtr<AVilaNPC>* Found = World->NPCs.Find(NpcId);
	AVilaNPC* Npc = Found ? Found->Get() : nullptr;
	if (!IsValid(Npc) || Npc->IsSleeping()) return;
	OpenDialogueWith(Npc, DialogueId);
}

// ---------------------------------------------------------------- consultas
bool AVilaHUD::IsModalOpen() const { return CurrentPanel.IsValid(); }

bool AVilaHUD::IsDialogueOpen() const { return CurrentKind == TEXT("dialogue"); }

bool AVilaHUD::IsMenuShown() const { return Overlay.IsValid() && Overlay->GetMenu().IsValid(); }

AVilaWorld* AVilaHUD::GetVilaWorld() const { return AVilaWorld::Get(this); }

AVilaPlayerCharacter* AVilaHUD::GetPlayer() const
{
	AVilaWorld* World = GetVilaWorld();
	return World ? World->Player.Get() : nullptr;
}

AVilaPlayerController* AVilaHUD::GetVilaController() const { return Cast<AVilaPlayerController>(GetOwningPlayerController()); }

// ---------------------------------------------------------------- janelas
void AVilaHUD::OpenModal(const FString& Kind, const TSharedRef<SVilaPanel>& Panel)
{
	CloseCurrent();
	Panel->Bind(this, Kind);
	CurrentPanel = Panel;
	CurrentKind = Kind;
	if (Overlay.IsValid()) Overlay->SetModal(Panel, Kind == TEXT("dialogue") ? 0.f : 0.35f);
	SetUIMode(true);
	if (FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> Focus = Panel;
		FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
	}
	Panel->OnOpen();
}

void AVilaHUD::CloseCurrent()
{
	if (!CurrentPanel.IsValid()) return;
	const FString Kind = CurrentKind;
	const TSharedPtr<SVilaPanel> Panel = CurrentPanel;
	CurrentPanel.Reset();
	CurrentKind.Reset();
	if (Overlay.IsValid()) Overlay->ClearModal();
	Panel->OnClose();
	if (Kind == TEXT("pause"))
	{
		if (bPausedGame)
		{
			UGameplayStatics::SetGamePaused(this, false);
			bPausedGame = false;
		}
		if (bPauseHold)
		{
			ReleaseClock();
			bPauseHold = false;
		}
	}
	if (!CurrentPanel.IsValid())
	{
		SetUIMode(false);
		RestoreFocus();
	}
	TWeakObjectPtr<AVilaHUD> Self(this);
	if (Kind == TEXT("dialogue") && !PendingShop.IsEmpty())
	{
		const FString Shop = PendingShop;
		PendingShop.Reset();
		Defer([Self, Shop]() { if (Self.IsValid()) Self->OpenShop(Shop); });
		return;
	}
	if (MsgQueue.Num() > 0)
	{
		const TPair<FString, FString> M = MsgQueue[0];
		MsgQueue.RemoveAt(0);
		Defer([Self, M]() { if (Self.IsValid()) Self->ShowMessage(M.Key, M.Value); });
	}
}

void AVilaHUD::CloseAll()
{
	MsgQueue.Empty();
	PendingShop.Reset();
	CloseCurrent();
}

void AVilaHUD::OpenContainer(const FString& ContainerId, const FString& Title)
{
	OpenModal(TEXT("container"), SNew(SVilaContainerPanel).ContainerId(ContainerId).Title(Title));
}

void AVilaHUD::OpenShop(const FString& ShopId)
{
	OpenModal(TEXT("shop"), SNew(SVilaShopPanel).ShopId(ShopId));
}

void AVilaHUD::OpenDialogue(AVilaNPC* Npc) { OpenDialogueWith(Npc, FString()); }

void AVilaHUD::OpenDialogueWith(AVilaNPC* Npc, const FString& DialogueId)
{
	if (!IsValid(Npc)) return;
	OpenModal(TEXT("dialogue"), SNew(SVilaDialoguePanel).Npc(TWeakObjectPtr<AVilaNPC>(Npc)).DialogueId(DialogueId));
}

// Tecla de pausa (UIManager._unhandled_input): com janela aberta, fecha (o diálogo se despede);
// sem janela, abre a pausa e congela o jogo.
void AVilaHUD::OpenPause()
{
	if (CurrentPanel.IsValid())
	{
		if (CurrentKind == TEXT("dialogue"))
			CurrentPanel->TryClose();
		else
			CloseCurrent();
		return;
	}
	if (IsMenuShown() || !GetPlayer()) return;
	OpenModal(TEXT("pause"), SNew(SVilaPauseMenu));
	bPausedGame = UGameplayStatics::SetGamePaused(this, true);
	if (!bPauseHold)
	{
		HoldClock();  // além do SetGamePaused: o relógio não anda mesmo se o mundo tickar na pausa
		bPauseHold = true;
	}
}

void AVilaHUD::OpenPanel(const FString& Kind)
{
	if (CurrentPanel.IsValid())
	{
		const bool bToggle = CurrentKind == TEXT("inventory") || CurrentKind == TEXT("journal") || CurrentKind == TEXT("map");
		if (bToggle && Kind == CurrentKind) CloseCurrent();
		return;
	}
	if (IsMenuShown() || !GetPlayer()) return;
	if (Kind == TEXT("inventory"))
		OpenModal(Kind, SNew(SVilaInventoryPanel));
	else if (Kind == TEXT("journal"))
		OpenModal(Kind, SNew(SVilaJournalPanel));
	else if (Kind == TEXT("map"))
		OpenModal(Kind, SNew(SVilaMapPanel));
	else
		UE_LOG(LogVila, Warning, TEXT("OpenPanel: tipo desconhecido '%s'"), *Kind);
}

void AVilaHUD::OpenSleep(const FString& BedId, float /*Quality*/)
{
	TSharedRef<float> Hours = MakeShared<float>(8.f);
	TWeakObjectPtr<AVilaHUD> Self(this);
	const FString Bed = BedId;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			VilaUI::Label(VilaUI::T("Dormir"), 22, VilaUI::Accent())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(STextBlock)
			.Font(VilaUI::Font(16))
			.ColorAndOpacity(FSlateColor(VilaUI::TextColor()))
			.Text_Lambda([Hours]()
			{
				const double Val = *Hours;
				const double Wake = std::fmod(vila::clock().hour_f() + Val, 24.0);
				return FText::FromString(VilaUI::F("Dormir %d h — acordar por volta das %02d:%02d.\nSono atual: %d%%",
					{static_cast<int>(Val), static_cast<int>(Wake), static_cast<int>(std::fmod(Wake, 1.0) * 60.0),
					 static_cast<int>(vila::game().player.needs.get_need("sono"))}));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			VilaUI::Slider(1.f, 12.f, 1.f, 8.f, [Hours](float V) { *Hours = V; }, 360.f)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				VilaUI::Button(VilaUI::T("Dormir"), [Self, Hours, Bed]()
				{
					if (!Self.IsValid()) return;
					const int32 H = FMath::RoundToInt(*Hours);
					Self->CloseCurrent();
					if (AVilaWorld* World = Self->GetVilaWorld()) World->DoSleep(H, Bed);
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				VilaUI::Button(VilaUI::T("Cancelar"), [Self]() { if (Self.IsValid()) Self->CloseCurrent(); })
			]
		];
	OpenModal(TEXT("sleep"), SNew(SVilaSimplePanel)
		[
			SNew(SBox).MinDesiredWidth(420.f)
			[
				VilaUI::Panel(V)
			]
		]);
}

void AVilaHUD::ShowMessage(const FString& Title, const FString& Body)
{
	if (CurrentPanel.IsValid())
	{
		MsgQueue.Add(TPair<FString, FString>(Title, Body));
		return;
	}
	TWeakObjectPtr<AVilaHUD> Self(this);
	TFunction<void()> Ok = [Self]() { if (Self.IsValid()) Self->CloseCurrent(); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			VilaUI::Label(Title, 22, VilaUI::Accent(), 520.f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			VilaUI::Label(Body, 17, VilaUI::TextColor(), 520.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			VilaUI::Button(VilaUI::T("OK"), Ok)
		];
	OpenModal(TEXT("message"), SNew(SVilaSimplePanel).OnAccept(Ok)
		[
			SNew(SBox).MinDesiredWidth(560.f)
			[
				VilaUI::Panel(V)
			]
		]);
}

void AVilaHUD::Fade(bool bToBlack) { FadeTarget = bToBlack ? 1.f : 0.f; }

void AVilaHUD::ShowMainMenu()
{
	if (!Overlay.IsValid()) return;
	CloseAll();
	TSharedRef<SVilaMainMenu> Menu = SNew(SVilaMainMenu).HUD(TWeakObjectPtr<AVilaHUD>(this));
	Overlay->SetMenu(Menu);
	SetUIMode(true);
	if (FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> Focus = Menu;
		FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
	}
	if (UVilaAudio* Audio = UVilaAudio::Get(this)) Audio->PlayMusic(TEXT("music_menu"));
}

// ---------------------------------------------------------------- utilidades
void AVilaHUD::QuickSave()
{
	if (CurrentPanel.IsValid() || IsMenuShown() || !GetPlayer()) return;
	const std::string Why = vila::saves().block_reason();
	if (!Why.empty())
		vila::events().notify.emit(Why, std::string("bad"));
	else if (vila::saves().save_game("rapido"))
		vila::events().notify.emit(vila::tr("Jogo salvo (rápido)."), std::string("good"));
}

void AVilaHUD::ToggleDebug()
{
	if (Overlay.IsValid()) Overlay->ToggleDebug();
}

void AVilaHUD::HoldClock()
{
	vila::clock().hold();
	++ClockHolds;
}

void AVilaHUD::ReleaseClock()
{
	if (ClockHolds <= 0) return;
	--ClockHolds;
	vila::clock().release();
}

void AVilaHUD::Defer(TFunction<void()> Fn) { Deferred.Add(MoveTemp(Fn)); }

void AVilaHUD::SetUIMode(bool bModal)
{
	// No menu principal o cursor fica sempre visível.
	if (!bModal && IsMenuShown()) bModal = true;
	if (AVilaPlayerController* PC = GetVilaController()) PC->SetUIMode(bModal);
}

void AVilaHUD::RestoreFocus()
{
	if (!FSlateApplication::IsInitialized()) return;
	if (IsMenuShown())
		FSlateApplication::Get().SetKeyboardFocus(Overlay->GetMenu(), EFocusCause::SetDirectly);
	else
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

// O SetUIMode do controller pode devolver o foco ao viewport; a janela aberta o retoma.
void AVilaHUD::MaintainFocus()
{
	if (!FSlateApplication::IsInitialized()) return;
	TSharedPtr<SWidget> Target = CurrentPanel;
	if (!Target.IsValid() && IsMenuShown()) Target = Overlay->GetMenu();
	if (!Target.IsValid()) return;
	if (Target->HasKeyboardFocus() || Target->HasFocusedDescendants()) return;
	FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
}

void AVilaHUD::TickUI(float DeltaSeconds)
{
	if (Deferred.Num() > 0)
	{
		TArray<TFunction<void()>> Run = MoveTemp(Deferred);
		Deferred.Reset();
		for (TFunction<void()>& Fn : Run)
			if (Fn) Fn();
	}
	FadeAlpha = FMath::FInterpConstantTo(FadeAlpha, FadeTarget, DeltaSeconds, 1.f / FadeSeconds);
	MaintainFocus();

	// F3 e F5 (UIManager._unhandled_input) lidos por borda de subida no controller.
	if (AVilaPlayerController* PC = GetVilaController())
	{
		const bool bDebug = PC->IsActionDown(TEXT("debug"));
		if (bDebug && !bPrevDebug) ToggleDebug();
		bPrevDebug = bDebug;
		const bool bSave = PC->IsActionDown(TEXT("quick_save"));
		if (bSave && !bPrevQuickSave) QuickSave();
		bPrevQuickSave = bSave;
	}
}
