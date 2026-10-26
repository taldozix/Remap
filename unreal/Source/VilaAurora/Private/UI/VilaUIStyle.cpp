#include "UI/VilaUIStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "VilaAudio.h"
#include "VilaConv.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Settings.h"

namespace VilaUI
{
	// ------------------------------------------------------------ cores
	FLinearColor SRGB(float R, float G, float B, float A)
	{
		const auto Q = [](float V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255)); };
		FLinearColor C = FLinearColor::FromSRGBColor(FColor(Q(R), Q(G), Q(B), 255));
		C.A = A;
		return C;
	}

	FLinearColor Hex(const std::string& HexStr, const FLinearColor& Default) { return VilaConv::Hex(HexStr, Default); }

	const FLinearColor& Accent() { static const FLinearColor C = VilaConv::Hex("#e8913a"); return C; }
	const FLinearColor& Bg() { static const FLinearColor C = SRGB(0.07f, 0.08f, 0.1f, 0.93f); return C; }
	const FLinearColor& BgLight() { static const FLinearColor C = SRGB(0.13f, 0.15f, 0.18f, 0.95f); return C; }
	const FLinearColor& TextColor() { static const FLinearColor C = SRGB(0.94f, 0.93f, 0.9f); return C; }
	const FLinearColor& Muted() { static const FLinearColor C = SRGB(0.62f, 0.64f, 0.66f); return C; }
	const FLinearColor& Good() { static const FLinearColor C = VilaConv::Hex("#7fd17f"); return C; }
	const FLinearColor& Bad() { static const FLinearColor C = VilaConv::Hex("#ef6b5b"); return C; }
	const FLinearColor& Warn() { static const FLinearColor C = VilaConv::Hex("#f2c14e"); return C; }
	const FLinearColor& Quest() { static const FLinearColor C = VilaConv::Hex("#8fc7ff"); return C; }

	FLinearColor KindColor(const FString& Kind)
	{
		if (Kind == TEXT("good") || Kind == TEXT("money")) return Good();
		if (Kind == TEXT("bad")) return Bad();
		if (Kind == TEXT("quest")) return Quest();
		if (Kind == TEXT("skill")) return Warn();
		return TextColor();
	}

	FLinearColor Lerp(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return A + (B - A) * FMath::Clamp(T, 0.f, 1.f);
	}

	FLinearColor Darken(const FLinearColor& C, float Amount)
	{
		FColor S = C.ToFColor(true);
		const float K = 1.f - Amount;
		S.R = static_cast<uint8>(S.R * K);
		S.G = static_cast<uint8>(S.G * K);
		S.B = static_cast<uint8>(S.B * K);
		FLinearColor Out = FLinearColor::FromSRGBColor(S);
		Out.A = C.A;
		return Out;
	}

	FLinearColor Lighten(const FLinearColor& C, float Amount)
	{
		FColor S = C.ToFColor(true);
		const auto L = [Amount](uint8 V) { return static_cast<uint8>(V + (255 - V) * Amount); };
		S.R = L(S.R);
		S.G = L(S.G);
		S.B = L(S.B);
		FLinearColor Out = FLinearColor::FromSRGBColor(S);
		Out.A = C.A;
		return Out;
	}

	FLinearColor WithAlpha(const FLinearColor& C, float A)
	{
		FLinearColor Out = C;
		Out.A = A;
		return Out;
	}

	// ------------------------------------------------------------ fontes
	int32 FontSize(int32 Base) { return FMath::RoundToInt(Base * static_cast<float>(vila::settings().text_scale())); }

	FSlateFontInfo Font(int32 Px, bool bBold)
	{
		// px do Godot -> pontos do Slate (o Slate desenha a 96 DPI: 1 pt = 4/3 px).
		const int32 Size = FMath::Max(6, FMath::RoundToInt(FontSize(Px) * 0.75f));
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	// ------------------------------------------------------------ pincéis
	namespace
	{
		TMap<FString, TUniquePtr<FSlateBrush>>& BrushCache()
		{
			static TMap<FString, TUniquePtr<FSlateBrush>> Cache;
			return Cache;
		}
		TWeakObjectPtr<UObject> GContext;
	}

	const FSlateBrush* White()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		const FString Key = FString::Printf(TEXT("%s|%.1f|%s|%.1f"), *Fill.ToString(), Radius, *Outline.ToString(), OutlineWidth);
		TMap<FString, TUniquePtr<FSlateBrush>>& Cache = BrushCache();
		if (TUniquePtr<FSlateBrush>* Found = Cache.Find(Key)) return Found->Get();
		TUniquePtr<FSlateBrush> Brush = OutlineWidth > 0.f
			? MakeUnique<FSlateRoundedBoxBrush>(Fill, Radius, Outline, OutlineWidth)
			: MakeUnique<FSlateRoundedBoxBrush>(Fill, Radius);
		const FSlateBrush* Out = Brush.Get();
		Cache.Add(Key, MoveTemp(Brush));
		return Out;
	}

	const FSlateBrush* RoundWhite(float Radius) { return Rounded(FLinearColor::White, Radius); }

	const FSlateBrush* PanelBrush(const FLinearColor& InBg, float Radius)
	{
		return Rounded(InBg, Radius, FLinearColor(1.f, 1.f, 1.f, 0.07f), 1.f);
	}

	FMargin PanelMargin(float Margin) { return FMargin(Margin, Margin * 0.75f); }

	// ------------------------------------------------------------ estilos
	const FButtonStyle& ButtonStyle()
	{
		static FButtonStyle Style = []
		{
			FButtonStyle S;
			S.SetNormal(*Rounded(SRGB(0.18f, 0.2f, 0.24f), 7.f));
			S.SetHovered(*Rounded(SRGB(0.27f, 0.3f, 0.36f), 7.f));
			S.SetPressed(*Rounded(Darken(Accent(), 0.15f), 7.f));
			S.SetDisabled(*Rounded(SRGB(0.12f, 0.12f, 0.14f), 7.f));
			S.SetNormalPadding(FMargin(12.f, 6.f));
			S.SetPressedPadding(FMargin(12.f, 6.f));
			S.SetNormalForeground(FSlateColor(TextColor()));
			S.SetHoveredForeground(FSlateColor(FLinearColor::White));
			S.SetPressedForeground(FSlateColor(FLinearColor::White));
			S.SetDisabledForeground(FSlateColor(SRGB(0.45f, 0.45f, 0.47f)));
			return S;
		}();
		return Style;
	}

	const FButtonStyle& FlatButtonStyle()
	{
		static FButtonStyle Style = []
		{
			static const FSlateNoResource None;
			FButtonStyle S;
			S.SetNormal(None);
			S.SetHovered(None);
			S.SetPressed(None);
			S.SetDisabled(None);
			S.SetNormalPadding(FMargin(0.f));
			S.SetPressedPadding(FMargin(0.f));
			return S;
		}();
		return Style;
	}

	const FProgressBarStyle& BarStyle()
	{
		static FProgressBarStyle Style = []
		{
			FProgressBarStyle S;
			S.SetBackgroundImage(*Rounded(SRGB(0.15f, 0.16f, 0.18f), 5.f));
			S.SetFillImage(*RoundWhite(5.f));
			S.SetMarqueeImage(*RoundWhite(5.f));
			return S;
		}();
		return Style;
	}

	// ------------------------------------------------------------ widgets
	TSharedRef<STextBlock> Label(const FString& Text, int32 Size, const FLinearColor& Color, float WrapAt)
	{
		TSharedRef<STextBlock> L = SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(Font(Size))
			.ColorAndOpacity(FSlateColor(Color));
		if (WrapAt > 0.f) L->SetWrapTextAt(WrapAt);
		return L;
	}

	TSharedRef<SButton> Button(const FString& Text, TFunction<void()> OnClick, int32 Size, bool bLeftAlign, float WrapAt)
	{
		TSharedRef<STextBlock> L = SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(Font(Size))
			.Justification(bLeftAlign ? ETextJustify::Left : ETextJustify::Center);
		if (WrapAt > 0.f) L->SetWrapTextAt(WrapAt);
		return SNew(SButton)
			.ButtonStyle(&ButtonStyle())
			.ContentPadding(FMargin(0.f))
			.HAlign(bLeftAlign ? HAlign_Left : HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([OnClick]()
			{
				PlaySound(TEXT("ui_click"), -10.f);
				if (OnClick) OnClick();
				return FReply::Handled();
			})
			[
				L
			];
	}

	TSharedRef<SWidget> Bar(TAttribute<TOptional<float>> Percent, TAttribute<FSlateColor> Color, float Width, float Height)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.HeightOverride(Height)
			.VAlign(VAlign_Center)
			[
				SNew(SProgressBar)
				.Style(&BarStyle())
				.BarFillType(EProgressBarFillType::LeftToRight)
				.Percent(Percent)
				.FillColorAndOpacity(Color)
			];
	}

	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, const FLinearColor& InBg, float Radius, float Margin)
	{
		return SNew(SBorder)
			.BorderImage(PanelBrush(InBg, Radius))
			.Padding(PanelMargin(Margin))
			[
				Content
			];
	}

	TSharedRef<SWidget> Gap(float W, float H) { return SNew(SSpacer).Size(FVector2D(W, H)); }

	TSharedRef<SWidget> Separator()
	{
		return SNew(SBox).Padding(FMargin(0.f, 6.f))
			[
				SNew(SBox).HeightOverride(1.f)
				[
					SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.15f)))
				]
			];
	}

	TSharedRef<SWidget> Check(bool bInitial, TFunction<void(bool)> OnChanged)
	{
		return SNew(SCheckBox)
			.IsChecked(bInitial ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.OnCheckStateChanged_Lambda([OnChanged](ECheckBoxState State)
			{
				PlaySound(TEXT("ui_click"), -10.f);
				if (OnChanged) OnChanged(State == ECheckBoxState::Checked);
			});
	}

	TSharedRef<SWidget> Slider(float Min, float Max, float Step, float Value, TFunction<void(float)> OnChanged, float Width)
	{
		// Guarda o último valor enviado para não repetir set_value a cada pixel arrastado.
		TSharedRef<float> Last = MakeShared<float>(Value);
		return SNew(SBox)
			.MinDesiredWidth(Width)
			.HeightOverride(24.f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.MinValue(Min)
				.MaxValue(Max)
				.StepSize(Step)
				.MouseUsesStep(true)
				.Value(Value)
				.OnValueChanged_Lambda([OnChanged, Last, Min, Max, Step](float V)
				{
					float Snapped = Step > 0.f ? Min + FMath::RoundToFloat((V - Min) / Step) * Step : V;
					Snapped = FMath::Clamp(Snapped, Min, Max);
					if (FMath::IsNearlyEqual(Snapped, *Last, 1e-4f)) return;
					*Last = Snapped;
					if (OnChanged) OnChanged(Snapped);
				})
			];
	}

	// ------------------------------------------------------------ texto
	FString F(const char* Pattern, std::initializer_list<vila::FmtArg> Args)
	{
		return VilaConv::S(vila::fmt(vila::tr(Pattern), Args));
	}

	// ------------------------------------------------------------ entrada
	FString KeyName(const FKey& Key) { return Key.GetFName().ToString(); }

	bool KeyIs(const FKey& Key, const std::string& Action)
	{
		const std::string Name = VilaConv::U(KeyName(Key));
		for (const vila::Json& K : vila::jarr(vila::settings().bindings, Action))
			if (vila::to_str(K) == Name) return true;
		return false;
	}

	// ------------------------------------------------------------ contexto
	void SetContext(UObject* Ctx) { GContext = Ctx; }
	UObject* Context() { return GContext.Get(); }

	void PlaySound(const FString& Id, float VolumeDb, float Pitch)
	{
		if (UObject* Ctx = GContext.Get())
			if (UVilaAudio* Audio = UVilaAudio::Get(Ctx)) Audio->Play(Id, VolumeDb, Pitch);
	}
}

// ---------------------------------------------------------------- SVilaOption
void SVilaOption::Construct(const FArguments& InArgs)
{
	Items = InArgs._Items;
	Selected = Items.Num() > 0 ? FMath::Clamp(InArgs._Selected, 0, Items.Num() - 1) : 0;
	OnChanged = InArgs._OnChanged;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VilaUI::Button(TEXT("‹"), [this]() { Step(-1); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f))
		[
			SNew(STextBlock)
			.Font(VilaUI::Font(16))
			.ColorAndOpacity(FSlateColor(VilaUI::TextColor()))
			.Justification(ETextJustify::Center)
			.Text_Lambda([this]() { return Items.IsValidIndex(Selected) ? FText::FromString(Items[Selected]) : FText::GetEmpty(); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VilaUI::Button(TEXT("›"), [this]() { Step(1); })
		]
	];
}

void SVilaOption::Step(int32 Dir)
{
	if (Items.Num() == 0) return;
	Selected = (Selected + Dir + Items.Num()) % Items.Num();
	if (OnChanged) OnChanged(Selected);
}

// ---------------------------------------------------------------- SVilaTabs
void SVilaTabs::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(Header, SHorizontalBox)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(InArgs._ContentHeight)
			[
				SNew(SBorder)
				.BorderImage(VilaUI::PanelBrush(VilaUI::BgLight(), 8.f))
				.Padding(VilaUI::PanelMargin(12.f))
				[
					SAssignNew(Switcher, SWidgetSwitcher)
				]
			]
		]
	];
}

void SVilaTabs::AddTab(const FString& Title, TSharedRef<SWidget> Content)
{
	const int32 Index = Count++;
	const FSlateBrush* Sel = VilaUI::PanelBrush(VilaUI::BgLight(), 6.f);
	const FSlateBrush* Unsel = VilaUI::PanelBrush(VilaUI::SRGB(0.1f, 0.11f, 0.13f, 0.9f), 6.f);
	Header->AddSlot().AutoWidth().Padding(FMargin(0.f, 0.f, 4.f, 0.f))
	[
		SNew(SButton)
		.ButtonStyle(&VilaUI::FlatButtonStyle())
		.ContentPadding(FMargin(0.f))
		.OnClicked_Lambda([this, Index]()
		{
			VilaUI::PlaySound(TEXT("ui_click"), -10.f);
			SetCurrent(Index);
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage_Lambda([this, Index, Sel, Unsel]() { return Index == Current ? Sel : Unsel; })
			.Padding(VilaUI::PanelMargin(10.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Title))
				.Font(VilaUI::Font(16))
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(Index == Current ? VilaUI::Accent() : VilaUI::TextColor()); })
			]
		]
	];
	Switcher->AddSlot()
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Content
		]
	];
	if (Index == Current) Switcher->SetActiveWidgetIndex(Current);
}

void SVilaTabs::SetCurrent(int32 Index)
{
	if (Count == 0) return;
	Current = FMath::Clamp(Index, 0, Count - 1);
	Switcher->SetActiveWidgetIndex(Current);
}
