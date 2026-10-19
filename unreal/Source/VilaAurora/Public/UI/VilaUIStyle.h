// Tema visual da interface em Slate (port de src/ui/UITheme.gd). Tudo é gerado em código:
// pincéis arredondados, estilos de botão/barra e fontes padrão do motor, sem .uasset.
// Os tamanhos de fonte seguem os "px" do Godot (convertidos para pontos do Slate) e
// respeitam o tamanho de texto das opções (game.text_scale).
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Misc/Attribute.h"
#include "Widgets/SCompoundWidget.h"
#include "VilaConv.h"
#include "Vila/Core.h"

class SButton;
class SHorizontalBox;
class STextBlock;
class SWidget;
class SWidgetSwitcher;
class UObject;
struct FButtonStyle;
struct FKey;
struct FProgressBarStyle;
struct FSlateBrush;

namespace VilaUI
{
	// ------------------------------------------------ cores (UITheme.gd, sRGB -> linear)
	FLinearColor SRGB(float R, float G, float B, float A = 1.f);
	FLinearColor Hex(const std::string& HexStr, const FLinearColor& Default = FLinearColor::White);
	const FLinearColor& Accent();
	const FLinearColor& Bg();
	const FLinearColor& BgLight();
	const FLinearColor& TextColor();
	const FLinearColor& Muted();
	const FLinearColor& Good();
	const FLinearColor& Bad();
	const FLinearColor& Warn();
	const FLinearColor& Quest();
	FLinearColor KindColor(const FString& Kind);
	FLinearColor Lerp(const FLinearColor& A, const FLinearColor& B, float T);
	// Color.darkened/lightened do Godot (aplicados no espaço sRGB).
	FLinearColor Darken(const FLinearColor& C, float Amount);
	FLinearColor Lighten(const FLinearColor& C, float Amount);
	FLinearColor WithAlpha(const FLinearColor& C, float A);

	// ------------------------------------------------ fontes
	int32 FontSize(int32 Base = 17);  // UITheme.font_size: base * text_scale (px do Godot)
	FSlateFontInfo Font(int32 Px = 17, bool bBold = false);

	// ------------------------------------------------ pincéis (cacheados; ponteiros estáveis)
	const FSlateBrush* White();
	const FSlateBrush* Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
	// Caixa branca arredondada para tingir com BorderBackgroundColor (cores dinâmicas).
	const FSlateBrush* RoundWhite(float Radius);
	// UITheme.panel_style: fundo + contorno sutil.
	const FSlateBrush* PanelBrush(const FLinearColor& Bg, float Radius = 10.f);
	FMargin PanelMargin(float Margin = 14.f);

	// ------------------------------------------------ estilos
	const FButtonStyle& ButtonStyle();
	const FButtonStyle& FlatButtonStyle();  // sem fundo (amostras de cor, abas)
	const FProgressBarStyle& BarStyle();

	// ------------------------------------------------ widgets prontos (UITheme.label/button/bar)
	TSharedRef<STextBlock> Label(const FString& Text, int32 Size = 17, const FLinearColor& Color = TextColor(), float WrapAt = 0.f);
	TSharedRef<SButton> Button(const FString& Text, TFunction<void()> OnClick, int32 Size = 17, bool bLeftAlign = false, float WrapAt = 0.f);
	// Barra 0..1 (o Godot usa 0..100; aqui já dividido).
	TSharedRef<SWidget> Bar(TAttribute<TOptional<float>> Percent, TAttribute<FSlateColor> Color, float Width = 160.f, float Height = 12.f);
	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, const FLinearColor& Bg = VilaUI::Bg(), float Radius = 10.f, float Margin = 14.f);
	TSharedRef<SWidget> Gap(float W, float H);
	TSharedRef<SWidget> Separator();
	TSharedRef<SWidget> Check(bool bInitial, TFunction<void(bool)> OnChanged);
	// HSlider com valor inicial; OnChanged recebe o valor já arredondado ao passo.
	TSharedRef<SWidget> Slider(float Min, float Max, float Step, float Value, TFunction<void(float)> OnChanged, float Width = 260.f);

	// ------------------------------------------------ texto
	// tr(padrão) % [args] do GDScript.
	FString F(const char* Pattern, std::initializer_list<vila::FmtArg> Args);
	inline FString T(const char* Key) { return VilaConv::Tr(Key); }

	// ------------------------------------------------ entrada
	FString KeyName(const FKey& Key);                  // nome do FKey (mesmo formato de settings.bindings)
	bool KeyIs(const FKey& Key, const std::string& Action);  // a tecla está ligada à ação?

	// ------------------------------------------------ contexto (som de clique, consultas)
	void SetContext(UObject* Ctx);
	UObject* Context();
	void PlaySound(const FString& Id, float VolumeDb = 0.f, float Pitch = 1.f);
}

// Seletor "‹ valor ›" (equivale ao OptionButton do Godot, sem janela suspensa).
class VILAAURORA_API SVilaOption : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaOption) : _Selected(0) {}
		SLATE_ARGUMENT(TArray<FString>, Items)
		SLATE_ARGUMENT(int32, Selected)
		SLATE_ARGUMENT(TFunction<void(int32)>, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	int32 GetSelected() const { return Selected; }

private:
	void Step(int32 Dir);

	TArray<FString> Items;
	int32 Selected = 0;
	TFunction<void(int32)> OnChanged;
};

// Abas (TabContainer): cabeçalho de botões + conteúdo com rolagem no painel claro.
class VILAAURORA_API SVilaTabs : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaTabs) : _ContentHeight(440.f) {}
		SLATE_ARGUMENT(float, ContentHeight)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void AddTab(const FString& Title, TSharedRef<SWidget> Content);
	int32 GetCurrent() const { return Current; }
	void SetCurrent(int32 Index);
	int32 Num() const { return Count; }

private:
	TSharedPtr<SHorizontalBox> Header;
	TSharedPtr<SWidgetSwitcher> Switcher;
	int32 Current = 0;
	int32 Count = 0;
};
