#include "UI/SVilaCharacterCreator.h"

#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "VilaGameInstance.h"
#include "VilaHUD.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Data.h"
#include "Vila/Game.h"

using namespace VilaConv;
using VilaUI::T;

namespace
{
	// Constantes do MainMenu.gd.
	const TArray<FString>& Skins() { static const TArray<FString> V = {TEXT("#f0c8a8"), TEXT("#e0b494"), TEXT("#c68e6a"), TEXT("#a8765a"), TEXT("#8a5a3c"), TEXT("#5e3b28")}; return V; }
	const TArray<FString>& Hairs() { static const TArray<FString> V = {TEXT("#1a1a1a"), TEXT("#2b1d12"), TEXT("#6b3d1e"), TEXT("#a0522d"), TEXT("#d8c080"), TEXT("#9a9a9a")}; return V; }
	const TArray<FString>& Shirts() { static const TArray<FString> V = {TEXT("#3f7f9f"), TEXT("#b8432f"), TEXT("#2d7d46"), TEXT("#e0b521"), TEXT("#7a5ea8"), TEXT("#eeeeea")}; return V; }
	const char* const Styles[] = {"short", "long", "curly", "bob", "ponytail", "bald", "bun"};
	const char* const StyleNames[] = {"Curto", "Longo", "Cacheado", "Chanel", "Rabo de cavalo", "Careca", "Coque"};
	constexpr int32 NumStyles = 7;
	constexpr int32 MaxTraits = 2;
	constexpr int32 MaxNameLen = 20;
}

FString SVilaCharacterCreator::InferSex(const vila::Json& InApp)
{
	if (vila::has(InApp, "sex")) return vila::to_str(vila::jget(InApp, "sex")) == "f" ? TEXT("f") : TEXT("m");
	const std::string Style = vila::jstr(InApp, "hair_style", "short");
	const bool bFem = Style == "long" || Style == "bob" || Style == "bun" || Style == "ponytail";
	return bFem && !vila::jbool(InApp, "beard") ? TEXT("f") : TEXT("m");
}

void SVilaCharacterCreator::Construct(const FArguments& InArgs)
{
	HUD = InArgs._HUD;
	OnBack = InArgs._OnBack;
	App = vila::game().player.appearance;
	if (!App.is_object()) App = vila::Json::object();

	int32 StyleIndex = 0;
	const std::string CurStyle = vila::jstr(App, "hair_style", "short");
	for (int32 i = 0; i < NumStyles; ++i)
		if (CurStyle == Styles[i]) StyleIndex = i;
	TArray<FString> StyleLabels;
	for (int32 i = 0; i < NumStyles; ++i) StyleLabels.Add(Tr(StyleNames[i]));

	const auto Muted = [](const FString& Text) { return VilaUI::Label(Text, 15, VilaUI::Muted(), 480.f); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const auto Add = [&V](const TSharedRef<SWidget>& W, float Gap = 6.f) { V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, Gap))[W]; };

	Add(VilaUI::Label(T("Novo personagem"), 24, VilaUI::Accent()), 10.f);
	Add(Muted(T("Nome")), 2.f);
	Add(SAssignNew(NameEdit, SEditableTextBox)
		.Text(FText::FromString(S(vila::game().player.name)))
		.Font(VilaUI::Font(17))
		.OnTextChanged_Lambda([this](const FText& NewText)
		{
			// LineEdit.max_length = 20
			const FString Str = NewText.ToString();
			if (Str.Len() > MaxNameLen && NameEdit.IsValid()) NameEdit->SetText(FText::FromString(Str.Left(MaxNameLen)));
		}), 10.f);
	Add(Muted(T("Corpo")), 2.f);
	Add(SNew(SVilaOption)
		.Items({T("Masculino"), T("Feminino")})
		.Selected(InferSex(App) == TEXT("f") ? 1 : 0)
		.OnChanged([this](int32 i) { App["sex"] = i == 1 ? "f" : "m"; }), 10.f);
	Add(Swatches(T("Tom de pele"), Skins(), "skin"), 8.f);
	Add(Swatches(T("Cor do cabelo"), Hairs(), "hair"), 8.f);
	Add(Swatches(T("Camisa"), Shirts(), "shirt"), 8.f);
	Add(Muted(T("Cabelo")), 2.f);
	Add(SNew(SVilaOption)
		.Items(StyleLabels)
		.Selected(StyleIndex)
		.OnChanged([this](int32 i) { App["hair_style"] = Styles[FMath::Clamp(i, 0, NumStyles - 1)]; }), 10.f);
	Add(Muted(T("Traços (escolha até 2 — cada um tem vantagem e desvantagem)")), 4.f);
	Add(TraitsBox(), 4.f);
	Add(SNew(STextBlock)
		.Font(VilaUI::Font(13))
		.ColorAndOpacity(FSlateColor(VilaUI::Muted()))
		.WrapTextAt(480.f)
		.Text_Lambda([this]() { return FText::FromString(TraitsText()); }), 10.f);
	Add(VilaUI::Label(T("Você acaba de se mudar para a casa 8 da Vila Aurora. Sem emprego, com R$ 60 no bolso e o aluguel vencendo no domingo."), 14, VilaUI::Muted(), 480.f), 10.f);
	Add(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 6.f, 0.f))
		[
			VilaUI::Button(T("Começar"), [this]() { Start(); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VilaUI::Button(T("Voltar"), [this]() { if (OnBack) OnBack(); })
		]);

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VilaUI::Panel(SNew(SBox).WidthOverride(520.f)[V])
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(24.f, 0.f, 0.f, 0.f))
		[
			Preview()
		]
	];
}

TSharedRef<SWidget> SVilaCharacterCreator::Swatches(const FString& Label, const TArray<FString>& Colors, const std::string& Key)
{
	TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
	for (const FString& C : Colors)
	{
		const std::string Hex = U(C);
		const FLinearColor Col = VilaUI::Hex(Hex);
		H->AddSlot().AutoWidth().Padding(FMargin(0.f, 0.f, 6.f, 0.f))
		[
			SNew(SButton)
			.ButtonStyle(&VilaUI::FlatButtonStyle())
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([this, Key, Hex]()
			{
				VilaUI::PlaySound(TEXT("ui_click"), -10.f);
				App[Key] = Hex;
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(40.f).HeightOverride(32.f)
				[
					// Contorno branco de 3 px na cor escolhida.
					SNew(SBorder)
					.BorderImage(VilaUI::RoundWhite(6.f))
					.BorderBackgroundColor_Lambda([this, Key, Hex]()
					{
						return FSlateColor(vila::jstr(App, Key) == Hex ? FLinearColor::White : FLinearColor::Transparent);
					})
					.Padding(FMargin(3.f))
					[
						SNew(SBorder)
						.BorderImage(VilaUI::RoundWhite(5.f))
						.BorderBackgroundColor(FSlateColor(Col))
					]
				]
			]
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))[VilaUI::Label(Label, 15, VilaUI::Muted())]
		+ SVerticalBox::Slot().AutoHeight()[H];
}

TSharedRef<SWidget> SVilaCharacterCreator::TraitsBox()
{
	TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
	for (const auto& It : vila::data().traits().items())
	{
		const FString Tid = S(It.key());
		const vila::Json& Td = vila::data().trait_def(It.key());
		const FString Tip = FString::Printf(TEXT("+ %s\n− %s"), *Tr(vila::jstr(Td, "pro")), *Tr(vila::jstr(Td, "con")));
		W->AddSlot().Padding(FMargin(0.f, 0.f, 12.f, 4.f))
		[
			SNew(SCheckBox)
			.ToolTipText(FText::FromString(Tip))
			.IsChecked_Lambda([this, Tid]() { return HasTrait(Tid) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Tid](ECheckBoxState State) { ToggleTrait(Tid, State == ECheckBoxState::Checked); })
			[
				VilaUI::Label(Tr(vila::jstr(Td, "name")), 15)
			]
		];
	}
	return W;
}

bool SVilaCharacterCreator::HasTrait(const FString& Id) const { return Traits.Contains(Id); }

void SVilaCharacterCreator::ToggleTrait(const FString& Id, bool bOn)
{
	VilaUI::PlaySound(TEXT("ui_click"), -10.f);
	if (bOn && !HasTrait(Id))
	{
		if (Traits.Num() >= MaxTraits) return;  // a caixa continua desmarcada (IsChecked lê o estado)
		Traits.Add(Id);
	}
	else if (!bOn)
		Traits.Remove(Id);
}

FString SVilaCharacterCreator::TraitsText() const
{
	if (Traits.Num() == 0) return T("Nenhum traço escolhido.");
	TArray<FString> Parts;
	for (const FString& Id : Traits)
	{
		const vila::Json& Td = vila::data().trait_def(U(Id));
		Parts.Add(FString::Printf(TEXT("%s: + %s − %s"), *Tr(vila::jstr(Td, "name")), *Tr(vila::jstr(Td, "pro")), *Tr(vila::jstr(Td, "con"))));
	}
	return FString::Join(Parts, TEXT("\n"));
}

FLinearColor SVilaCharacterCreator::AppColor(const std::string& Key, const char* Default) const
{
	return VilaUI::Hex(vila::jstr(App, Key, Default), VilaUI::Hex(Default));
}

// Prévia 2D simples (o Godot girava um Humanoid 3D num SubViewport).
TSharedRef<SWidget> SVilaCharacterCreator::Preview()
{
	const auto Part = [this](const std::string& Key, const char* Default, float W, float H, float Radius)
	{
		return SNew(SBox).WidthOverride(W).HeightOverride(H).HAlign(HAlign_Fill)
			[
				SNew(SBorder)
				.BorderImage(VilaUI::RoundWhite(Radius))
				.BorderBackgroundColor_Lambda([this, Key, Default]() { return FSlateColor(AppColor(Key, Default)); })
			];
	};
	return SNew(SBorder)
		.BorderImage(VilaUI::PanelBrush(VilaUI::WithAlpha(VilaUI::Bg(), 0.6f), 12.f))
		.Padding(FMargin(28.f, 20.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				// Cabelo: some no "careca".
				SNew(SBox).Visibility_Lambda([this]() { return vila::jstr(App, "hair_style") == "bald" ? EVisibility::Hidden : EVisibility::Visible; })
				[
					Part("hair", "#2b1d12", 64.f, 18.f, 9.f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Part("skin", "#c68e6a", 56.f, 56.f, 28.f)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))[Part("shirt", "#3f7f9f", 96.f, 110.f, 14.f)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 2.f, 0.f, 0.f))[Part("pants", "#2f3a4a", 80.f, 120.f, 10.f)]
		];
}

void SVilaCharacterCreator::Start()
{
	UVilaGameInstance* GI = HUD.IsValid() ? UVilaGameInstance::Get(HUD.Get()) : nullptr;
	if (!GI) return;
	vila::Json TraitIds = vila::Json::array();
	for (const FString& Id : Traits) TraitIds.push_back(U(Id));
	GI->StartNewGame(NameEdit.IsValid() ? NameEdit->GetText().ToString() : S(vila::game().player.name), App, TraitIds);
}
