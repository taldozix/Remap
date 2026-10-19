// Opções (port de src/ui/OptionsMenu.gd): gráficos (recursos do Unreal: escalabilidade, Lumen,
// reflexos, TSR...), áudio, jogabilidade/acessibilidade e controles remapeáveis. Tudo vai para
// vila::settings().set_value (o UVilaGameInstance aplica em Settings::changed) e é salvo ao voltar.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Vila/Core.h"

class SBox;
class SVilaTabs;
class SVerticalBox;
class STextBlock;

class VILAAURORA_API SVilaOptionsMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaOptionsMenu) {}
		SLATE_ARGUMENT(TFunction<void()>, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	// Captura da tecla nova (OptionsMenu._input): antes de qualquer outro widget.
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	void Build();
	TSharedRef<SWidget> Graphics();
	TSharedRef<SWidget> Audio();
	TSharedRef<SWidget> Game();
	TSharedRef<SWidget> Controls();

	void Line(const TSharedRef<SVerticalBox>& Parent, const FString& Label, const TSharedRef<SWidget>& Ctrl);
	TSharedRef<SWidget> Option(const std::string& Section, const std::string& Key, const TArray<FString>& Items, const vila::Json& Values = vila::Json::array());
	TSharedRef<SWidget> Slider(const std::string& Section, const std::string& Key, float Min, float Max, float Step, const char* Fmt = "%d%%", float Mult = 100.f);
	TSharedRef<SWidget> CheckBox(const std::string& Section, const std::string& Key);

	void StartRebind(const std::string& Action, TSharedPtr<STextBlock> Text);
	void FinishRebind(const FString& KeyName);
	void ResetTab();
	void Back();

	TFunction<void()> OnBack;
	TSharedPtr<SVilaTabs> Tabs;
	TSharedPtr<SBox> TabsSlot;
	std::string WaitingAction;
	TSharedPtr<STextBlock> WaitingText;
};
