#include "UI/SVilaSaveSlots.h"

#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Vila/Saves.h"

using namespace VilaConv;

void SVilaSaveSlots::Construct(const FArguments& InArgs)
{
	const bool bSaving = InArgs._Saving;
	const TFunction<void(const FString&)> OnPick = InArgs._OnPick;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	for (const std::string& Slot : vila::Saves::slots())
	{
		if (bSaving && Slot == "auto") continue;
		const vila::Json Meta = vila::saves().read_meta(Slot);
		FString Label = S(vila::Saves::slot_label(Slot));
		if (Meta.empty())
			Label += TEXT(" — ") + Tr("vazio");
		else
		{
			const FString SavedAt = S(vila::jstr(Meta, "saved_at")).Replace(TEXT("T"), TEXT(" "));
			Label += TEXT(" — ") + S(vila::fmt("%s, %s %d %s · R$ %d · %s",
				{vila::jstr(Meta, "weekday"), vila::tr("Dia"), vila::jint(Meta, "day", 1), vila::jstr(Meta, "time"), vila::jint(Meta, "money"), U(SavedAt)}));
		}
		const FString SlotId = S(Slot);
		TSharedRef<SButton> B = VilaUI::Button(Label, [OnPick, SlotId]() { if (OnPick) OnPick(SlotId); }, 16, true);
		B->SetEnabled(bSaving || !Meta.empty());
		V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[B];
	}
	ChildSlot[V];
}
