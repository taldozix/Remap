#include "UI/SVilaPauseMenu.h"

#include "Kismet/GameplayStatics.h"
#include "UI/SVilaOptionsMenu.h"
#include "UI/SVilaSaveSlots.h"
#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "VilaGameInstance.h"
#include "VilaHUD.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Vila/Clock.h"
#include "Vila/Events.h"
#include "Vila/Saves.h"

using namespace VilaConv;
using VilaUI::T;

void SVilaPauseMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(VilaUI::PanelBrush(VilaUI::Bg(), 10.f))
		.Padding(VilaUI::PanelMargin(14.f))
		[
			SAssignNew(Content, SBox).MinDesiredWidth(560.f)
		]
	];
	ShowMain();
}

TSharedRef<SVerticalBox> SVilaPauseMenu::Clear()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	Content->SetContent(V);
	return V;
}

namespace
{
	void Add(const TSharedRef<SVerticalBox>& V, const TSharedRef<SWidget>& W)
	{
		V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[W];
	}
}

void SVilaPauseMenu::ShowMain()
{
	TSharedRef<SVerticalBox> V = Clear();
	const vila::Clock& Clk = vila::clock();
	Add(V, VilaUI::Label(T("Pausado"), 26, VilaUI::Accent()));
	Add(V, VilaUI::Label(FString::Printf(TEXT("%s, %s · %s %d"), *S(Clk.weekday_name()), *S(Clk.time_string()), *T("Dia"), Clk.day()), 15, VilaUI::Muted()));
	Add(V, VilaUI::Button(T("Continuar"), [this]() { Close(); }));
	Add(V, VilaUI::Button(T("Salvar jogo"), [this]() { ShowSave(); }));
	Add(V, VilaUI::Button(T("Carregar jogo"), [this]() { ShowLoad(); }));
	Add(V, VilaUI::Button(T("Opções"), [this]() { ShowOptions(); }));
	Add(V, VilaUI::Button(T("Sair para o menu principal"), [this]()
	{
		AVilaHUD* H = GetHUD();
		if (!H) return;
		UGameplayStatics::SetGamePaused(H, false);
		if (UVilaGameInstance* GI = UVilaGameInstance::Get(H)) GI->ReturnToMenu();
	}));
	Add(V, VilaUI::Button(T("Sair do jogo"), [this]()
	{
		if (AVilaHUD* H = GetHUD())
			if (UVilaGameInstance* GI = UVilaGameInstance::Get(H)) GI->QuitGame();
	}));
}

void SVilaPauseMenu::ShowSave()
{
	TSharedRef<SVerticalBox> V = Clear();
	Add(V, VilaUI::Label(T("Salvar jogo"), 24, VilaUI::Accent()));
	const std::string Why = vila::saves().block_reason();
	if (!Why.empty())
		Add(V, VilaUI::Label(S(Why), 16, VilaUI::Bad(), 540.f));
	else
	{
		Add(V, SNew(SVilaSaveSlots).Saving(true).OnPick([this](const FString& Slot)
		{
			if (vila::saves().save_game(U(Slot)))
			{
				vila::events().notify.emit(vila::tr("Jogo salvo."), std::string("good"));
				ShowMain();
			}
			else
				vila::events().notify.emit(vila::saves().last_error, std::string("bad"));
		}));
	}
	Add(V, VilaUI::Button(T("Voltar"), [this]() { ShowMain(); }));
}

void SVilaPauseMenu::ShowLoad()
{
	TSharedRef<SVerticalBox> V = Clear();
	Add(V, VilaUI::Label(T("Carregar jogo"), 24, VilaUI::Accent()));
	Add(V, VilaUI::Label(T("O progresso não salvo será perdido."), 14, VilaUI::Warn()));
	Add(V, SNew(SVilaSaveSlots).Saving(false).OnPick([this](const FString& Slot)
	{
		AVilaHUD* H = GetHUD();
		UVilaGameInstance* GI = H ? UVilaGameInstance::Get(H) : nullptr;
		if (!GI || !GI->LoadSlot(Slot)) vila::events().notify.emit(vila::saves().last_error, std::string("bad"));
	}));
	Add(V, VilaUI::Button(T("Voltar"), [this]() { ShowMain(); }));
}

void SVilaPauseMenu::ShowOptions()
{
	Content->SetContent(SNew(SVilaOptionsMenu).OnBack([this]() { ShowMain(); }));
}
