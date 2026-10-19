// Lista de espaços de salvamento (port de src/ui/SaveSlots.gd), usada na pausa e no menu.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class VILAAURORA_API SVilaSaveSlots : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaSaveSlots) : _Saving(false) {}
		SLATE_ARGUMENT(bool, Saving)
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnPick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
