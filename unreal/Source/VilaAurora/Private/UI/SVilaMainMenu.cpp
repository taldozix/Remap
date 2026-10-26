#include "UI/SVilaMainMenu.h"

#include "InputCoreTypes.h"
#include "UI/SVilaCharacterCreator.h"
#include "UI/SVilaOptionsMenu.h"
#include "UI/SVilaSaveSlots.h"
#include "UI/VilaUIStyle.h"
#include "VilaAssets.h"
#include "VilaConv.h"
#include "VilaGameInstance.h"
#include "VilaHUD.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Saves.h"

using namespace VilaConv;
using VilaUI::T;

void SVilaMainMenu::Construct(const FArguments& InArgs)
{
	HUD = InArgs._HUD;
	Rebuild();
}

void SVilaMainMenu::Rebuild()
{
	ChildSlot
	[
		SNew(SOverlay)
		// Faixa escura à esquerda para ler o texto sobre a vila (o Godot usava um gradiente de fundo).
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			SNew(SBox).WidthOverride(480.f)
			[
				SNew(SBorder)
				.BorderImage(VilaUI::White())
				.BorderBackgroundColor(FSlateColor(VilaUI::SRGB(0.11f, 0.09f, 0.14f, 0.72f)))
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(70.f, 80.f, 0.f, 0.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("VILA AURORA"))).Font(VilaUI::Font(64, true)).ColorAndOpacity(FSlateColor(VilaUI::Accent()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				VilaUI::Label(T("Open World Life RPG — protótipo jogável"), 20)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				VilaUI::Label(T("Porto Serra · um bairro pequeno, porém vivo"), 15, VilaUI::Muted())
			]
			+ SVerticalBox::Slot().AutoHeight()[VilaUI::Gap(0.f, 30.f)]
			+ SVerticalBox::Slot().AutoHeight()[MainButtons()]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(70.f, 0.f, 0.f, 30.f))
		[
			Diagnostics()
		]
		// Área central (painéis de novo jogo, carregar, opções, créditos, mensagens).
		+ SOverlay::Slot().Padding(FMargin(420.f, 0.f, 40.f, 0.f))
		[
			SAssignNew(Center, SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
		]
	];
}

TSharedRef<SWidget> SVilaMainMenu::MainButtons()
{
	const FString Latest = S(vila::saves().latest_slot());
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const auto Add = [&V](const TSharedRef<SButton>& B, bool bEnabled = true)
	{
		B->SetEnabled(bEnabled);
		V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
		[
			SNew(SBox).WidthOverride(320.f).HeightOverride(44.f)[B]
		];
	};
	Add(VilaUI::Button(T("Novo jogo"), [this]() { NewGame(); }));
	Add(VilaUI::Button(T("Continuar"), [this]() { Continue(); }), !Latest.IsEmpty());
	Add(VilaUI::Button(T("Carregar jogo"), [this]() { LoadPanel(); }));
	Add(VilaUI::Button(T("Opções"), [this]() { OptionsPanel(); }));
	Add(VilaUI::Button(T("Créditos"), [this]() { Credits(); }));
	Add(VilaUI::Button(T("Sair"), [this]()
	{
		if (UVilaGameInstance* GI = HUD.IsValid() ? UVilaGameInstance::Get(HUD.Get()) : nullptr) GI->QuitGame();
	}));
	return V;
}

// Erros dos dados (UVilaGameInstance::GetDataErrors) e aviso de conteúdo 3D ausente.
TSharedRef<SWidget> SVilaMainMenu::Diagnostics()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	AVilaHUD* H = HUD.Get();
	if (!H) return V;
	if (const UVilaGameInstance* GI = UVilaGameInstance::Get(H))
	{
		const TArray<FString>& Errors = GI->GetDataErrors();
		const int32 Shown = FMath::Min(Errors.Num(), 6);
		for (int32 i = 0; i < Shown; ++i) V->AddSlot().AutoHeight()[VilaUI::Label(Errors[i], 13, VilaUI::Bad(), 900.f)];
		if (Errors.Num() > Shown)
			V->AddSlot().AutoHeight()[VilaUI::Label(VilaUI::F("(+%d erros nos dados; veja o log)", {Errors.Num() - Shown}), 13, VilaUI::Bad())];
	}
	const UVilaAssets* Assets = UVilaAssets::Get(H);
	if (Assets && !Assets->HasContent())
	{
		V->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
		[
			VilaUI::Label(T("Conteúdo 3D não importado: rode Scripts/build_content.py no editor."), 14, VilaUI::Warn(), 900.f)
		];
	}
	return V;
}

void SVilaMainMenu::ClearCenter()
{
	if (Center.IsValid()) Center->SetContent(SNullWidget::NullWidget);
}

TSharedRef<SVerticalBox> SVilaMainMenu::ShowPanel(float MinWidth)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	Center->SetContent(
		SNew(SBox).MinDesiredWidth(MinWidth)
		[
			VilaUI::Panel(V)
		]);
	return V;
}

void SVilaMainMenu::NewGame()
{
	TWeakPtr<SVilaMainMenu> Weak = StaticCastSharedRef<SVilaMainMenu>(AsShared());
	Center->SetContent(SNew(SVilaCharacterCreator).HUD(HUD).OnBack([Weak]()
	{
		if (TSharedPtr<SVilaMainMenu> M = Weak.Pin()) M->ClearCenter();
	}));
}

void SVilaMainMenu::Continue()
{
	const FString Latest = S(vila::saves().latest_slot());
	UVilaGameInstance* GI = HUD.IsValid() ? UVilaGameInstance::Get(HUD.Get()) : nullptr;
	if (!GI || Latest.IsEmpty() || !GI->LoadSlot(Latest)) Message(T("Erro"), S(vila::saves().last_error));
}

void SVilaMainMenu::LoadPanel()
{
	TSharedRef<SVerticalBox> V = ShowPanel(620.f);
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))[VilaUI::Label(T("Carregar jogo"), 24, VilaUI::Accent())];
	TWeakPtr<SVilaMainMenu> Weak = StaticCastSharedRef<SVilaMainMenu>(AsShared());
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
	[
		SNew(SVilaSaveSlots).Saving(false).OnPick([Weak](const FString& Slot)
		{
			TSharedPtr<SVilaMainMenu> M = Weak.Pin();
			if (!M) return;
			UVilaGameInstance* GI = M->HUD.IsValid() ? UVilaGameInstance::Get(M->HUD.Get()) : nullptr;
			if (!GI || !GI->LoadSlot(Slot)) M->Message(T("Erro ao carregar"), S(vila::saves().last_error));
		})
	];
	V->AddSlot().AutoHeight().HAlign(HAlign_Left)[VilaUI::Button(T("Voltar"), [this]() { ClearCenter(); })];
}

void SVilaMainMenu::OptionsPanel()
{
	TWeakPtr<SVilaMainMenu> Weak = StaticCastSharedRef<SVilaMainMenu>(AsShared());
	Center->SetContent(VilaUI::Panel(SNew(SVilaOptionsMenu).OnBack([Weak]()
	{
		// MainMenu: _clear_center() + theme = UITheme.make() (o tamanho do texto pode ter mudado).
		if (TSharedPtr<SVilaMainMenu> M = Weak.Pin()) M->Rebuild();
	})));
}

void SVilaMainMenu::Credits()
{
	TSharedRef<SVerticalBox> V = ShowPanel(620.f);
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))[VilaUI::Label(T("Créditos"), 24, VilaUI::Accent())];
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
	[
		VilaUI::Label(T("Vila Aurora — protótipo do Open World Life RPG.\n\nDesign, código, mapa, personagens, diálogos e sons: originais, criados para este projeto (sons e modelos gerados proceduralmente).\nMotor: Unreal Engine 5.\n\nInspirações declaradas: a profundidade sistêmica de Project Zomboid, a liberdade urbana de GTA e a imersão de Red Dead Redemption 2 — sem uso de qualquer ativo, nome ou personagem desses jogos."), 15, VilaUI::TextColor(), 580.f)
	];
	V->AddSlot().AutoHeight().HAlign(HAlign_Left)[VilaUI::Button(T("Voltar"), [this]() { ClearCenter(); })];
}

void SVilaMainMenu::Message(const FString& Title, const FString& Body)
{
	TSharedRef<SVerticalBox> V = ShowPanel(480.f);
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))[VilaUI::Label(Title, 22, VilaUI::Bad())];
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))[VilaUI::Label(Body, 16, VilaUI::TextColor(), 440.f)];
	V->AddSlot().AutoHeight().HAlign(HAlign_Left)[VilaUI::Button(T("OK"), [this]() { ClearCenter(); })];
}

FReply SVilaMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		ClearCenter();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SVilaMainMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}
