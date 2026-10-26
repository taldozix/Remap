#include "UI/SVilaDialoguePanel.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "VilaHUD.h"
#include "VilaNPC.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Events.h"
#include "Vila/Game.h"
#include "Vila/Settings.h"

using namespace VilaConv;
using VilaUI::T;

void SVilaDialoguePanel::Construct(const FArguments& InArgs)
{
	Npc = InArgs._Npc;
	DialogueId = InArgs._DialogueId;

	AVilaNPC* N = Npc.Get();
	const vila::Json App = N ? vila::jobj(N->Def, "appearance") : vila::Json::object();
	const FLinearColor Shirt = VilaUI::Darken(VilaUI::Hex(vila::jstr(App, "shirt", "#446688")), 0.2f);
	const FLinearColor Skin = VilaUI::Hex(vila::jstr(App, "skin", "#d9a98a"));
	const FString Name = N ? N->DisplayName() : FString();

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(0.f, 0.f, 0.f, 30.f))
	[
		SNew(SBox)
		.WidthOverride(980.f)
		.MinDesiredHeight(200.f)
		[
			SNew(SBorder)
			.BorderImage(VilaUI::Rounded(VilaUI::SRGB(0.05f, 0.06f, 0.08f, 0.9f), 16.f, FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f))
			.Padding(VilaUI::PanelMargin(20.f))
			[
				SNew(SHorizontalBox)
				// Retrato: círculo com a cor da camisa, borda da cor da pele e as iniciais.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(FMargin(0.f, 0.f, 18.f, 0.f))
				[
					SNew(SBox).WidthOverride(110.f).HeightOverride(110.f)
					[
						SNew(SBorder)
						.BorderImage(VilaUI::RoundWhite(55.f))
						.BorderBackgroundColor(FSlateColor(Skin))
						.Padding(FMargin(5.f))
						[
							SNew(SBorder)
							.BorderImage(VilaUI::RoundWhite(50.f))
							.BorderBackgroundColor(FSlateColor(Shirt))
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								VilaUI::Label(Initials(Name), 40)
							]
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 18.f, 0.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						SNew(SBorder)
						.BorderImage(VilaUI::Rounded(VilaUI::Accent(), 12.f))
						.Padding(VilaUI::PanelMargin(6.f))
						[
							SAssignNew(NameLabel, STextBlock).Font(VilaUI::Font(18)).ColorAndOpacity(FSlateColor(VilaUI::SRGB(0.1f, 0.08f, 0.05f)))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
					[
						SAssignNew(PrefixLabel, STextBlock).Font(VilaUI::Font(19)).Visibility(EVisibility::Collapsed)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).MinDesiredHeight(90.f)
						[
							SAssignNew(TextLabel, STextBlock).Font(VilaUI::Font(19)).ColorAndOpacity(FSlateColor(VilaUI::TextColor())).AutoWrapText(true)
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(HintLabel, STextBlock).Font(VilaUI::Font(14)).ColorAndOpacity(FSlateColor(VilaUI::Muted()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(320.f)
					[
						SAssignNew(ChoicesBox, SVerticalBox).Visibility(EVisibility::Collapsed)
					]
				]
			]
		]
	];
}

FString SVilaDialoguePanel::Initials(const FString& Name)
{
	TArray<FString> Parts;
	Name.ParseIntoArray(Parts, TEXT(" "), true);
	FString Out;
	for (const FString& P : Parts)
	{
		if (P.Len() > 0 && Out.Len() < 2 && FChar::IsUpper(P[0])) Out.AppendChar(P[0]);
	}
	return Out.IsEmpty() ? Name.Left(1) : Out;
}

void SVilaDialoguePanel::OnOpen()
{
	AVilaNPC* N = Npc.Get();
	if (AVilaHUD* H = GetHUD())
	{
		H->HoldClock();
		bHeld = true;
	}
	if (!N)
	{
		Finish();
		return;
	}
	N->BeginTalk();
	// (Godot: NPCs importantes aproximavam a câmera; o AVilaCameraRig não expõe a distância.)
	const std::string Dlg = DialogueId.IsEmpty() ? vila::jstr(N->Def, "dialogue", "pedestre") : U(DialogueId);
	bStarted = Runner.start(U(N->NpcId), Dlg);
	if (!bStarted)
	{
		TWeakPtr<SVilaDialoguePanel> Weak = StaticCastSharedRef<SVilaDialoguePanel>(AsShared());
		if (AVilaHUD* H = GetHUD()) H->Defer([Weak]() { if (TSharedPtr<SVilaDialoguePanel> P = Weak.Pin()) P->Finish(); });
		return;
	}
	ShowNode();
}

void SVilaDialoguePanel::ShowNode()
{
	if (Runner.finished)
	{
		Finish();
		return;
	}
	NameLabel->SetText(FText::FromString(S(Runner.speaker_name())));
	Prefix.Reset();
	if (!Runner.last_check.empty())
	{
		const bool bOk = vila::jbool(Runner.last_check, "success");
		PrefixColor = bOk ? VilaUI::Hex("#7fd17f") : VilaUI::Hex("#ef6b5b");
		Prefix = FString::Printf(TEXT("[%s: %s]"), *S(vila::jstr(Runner.last_check, "label")), bOk ? *T("sucesso") : *T("falhou"));
	}
	PrefixLabel->SetColorAndOpacity(FSlateColor(PrefixColor));
	PrefixLabel->SetVisibility(Prefix.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	FullText = S(Runner.text());
	FullLen = Prefix.Len() + FullText.Len();
	Typing = 0.f;
	UpdateTyped();
	ChoicesBox->ClearChildren();
	ChoicesBox->SetVisibility(EVisibility::Collapsed);
	bChoicesVisible = false;
	HintLabel->SetText(FText::GetEmpty());
}

void SVilaDialoguePanel::UpdateTyped()
{
	const int32 N = FMath::Clamp(static_cast<int32>(Typing), 0, FullLen);
	PrefixLabel->SetText(FText::FromString(Prefix.Left(N)));
	TextLabel->SetText(FText::FromString(FullText.Left(FMath::Max(0, N - Prefix.Len()))));
}

void SVilaDialoguePanel::ShowChoices()
{
	bChoicesVisible = true;
	ChoicesBox->SetVisibility(EVisibility::SelfHitTestInvisible);
	const vila::Json Cs = Runner.choices();
	if (Cs.empty())
	{
		HintLabel->SetText(FText::FromString(VilaUI::F("[%s / clique] continuar", {vila::settings().primary_key_text("interact")})));
		return;
	}
	HintLabel->SetText(FText::FromString(T("[Esc] encerrar conversa")));
	int32 Num = 1;
	TSharedPtr<SWidget> FirstEnabled;
	for (const vila::Json& C : Cs)
	{
		FString Text = FString::Printf(TEXT("%d. %s"), Num, *S(vila::jstr(C, "text")));
		const bool bEnabled = vila::jbool(C, "enabled", true);
		const FString Hint = S(vila::jstr(C, "hint"));
		if (!bEnabled && !Hint.IsEmpty()) Text += FString::Printf(TEXT(" (%s)"), *Hint);
		const int32 Index = vila::jint(C, "index");
		TSharedRef<SButton> B = VilaUI::Button(Text, [this, Index]() { Choose(Index); }, 17, true, 296.f);
		B->SetEnabled(bEnabled);
		if (bEnabled && !FirstEnabled.IsValid()) FirstEnabled = B;
		ChoicesBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[B];
		++Num;
	}
	if (FirstEnabled.IsValid() && FSlateApplication::IsInitialized())
		FSlateApplication::Get().SetKeyboardFocus(FirstEnabled, EFocusCause::SetDirectly);
}

void SVilaDialoguePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SVilaPanel::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bStarted || bChoicesVisible) return;
	Typing += InDeltaTime * 55.f;
	if (Typing >= FullLen)
	{
		Typing = static_cast<float>(FullLen);
		UpdateTyped();
		ShowChoices();
		return;
	}
	UpdateTyped();
}

void SVilaDialoguePanel::SkipOrAdvance()
{
	if (!bStarted) return;
	if (!bChoicesVisible)
	{
		Typing = static_cast<float>(FullLen);
		return;
	}
	if (Runner.choices().empty())
	{
		Runner.advance();
		ShowNode();
	}
}

void SVilaDialoguePanel::Choose(int32 Index)
{
	vila::game().player.skills.add_xp("social", 1);
	Runner.choose(Index);
	ShowNode();
}

FReply SVilaDialoguePanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (VilaUI::KeyIs(Key, "interact") || Key == EKeys::SpaceBar)
	{
		SkipOrAdvance();
		return FReply::Handled();
	}
	if (bChoicesVisible)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			if (!VilaUI::KeyIs(Key, vila::fmt("option_%d", {i + 1}))) continue;
			const vila::Json Cs = Runner.choices();
			if (i < static_cast<int32>(Cs.size()) && vila::jbool(Cs[static_cast<size_t>(i)], "enabled", true))
				Choose(vila::jint(Cs[static_cast<size_t>(i)], "index"));
			return FReply::Handled();
		}
	}
	return SVilaPanel::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SVilaDialoguePanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) SkipOrAdvance();
	return SVilaPanel::OnMouseButtonDown(MyGeometry, MouseEvent);
}

// Encerrar naturalmente (Esc): o NPC se despede.
void SVilaDialoguePanel::TryClose()
{
	if (AVilaNPC* N = Npc.Get()) N->Bark(T("Até mais."));
	Finish();
}

void SVilaDialoguePanel::Finish() { Close(); }

void SVilaDialoguePanel::OnClose()
{
	if (bHeld)
	{
		if (AVilaHUD* H = GetHUD()) H->ReleaseClock();
		bHeld = false;
	}
	if (AVilaNPC* N = Npc.Get()) N->EndTalk();
	vila::events().dialogue_closed.emit();
}
