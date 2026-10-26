// Criação de personagem do menu principal (o _new_game_panel do MainMenu.gd): nome, corpo,
// cores de pele/cabelo/camisa, penteado e até 2 traços (vila::data().traits()).
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Vila/Core.h"

class AVilaHUD;
class SEditableTextBox;
class SVerticalBox;

class VILAAURORA_API SVilaCharacterCreator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaCharacterCreator) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AVilaHUD>, HUD)
		SLATE_ARGUMENT(TFunction<void()>, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Humanoid.infer_sex do Godot.
	static FString InferSex(const vila::Json& App);

private:
	TSharedRef<SWidget> Swatches(const FString& Label, const TArray<FString>& Colors, const std::string& Key);
	TSharedRef<SWidget> TraitsBox();
	TSharedRef<SWidget> Preview();
	FString TraitsText() const;
	bool HasTrait(const FString& Id) const;
	void ToggleTrait(const FString& Id, bool bOn);
	FLinearColor AppColor(const std::string& Key, const char* Default) const;
	void Start();

	TWeakObjectPtr<AVilaHUD> HUD;
	TFunction<void()> OnBack;
	vila::Json App = vila::Json::object();
	TArray<FString> Traits;  // ids de traits.json, na ordem escolhida
	TSharedPtr<SEditableTextBox> NameEdit;
};
