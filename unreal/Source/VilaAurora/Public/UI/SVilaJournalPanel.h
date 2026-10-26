// Diário (port de src/ui/JournalPanel.gd): missões, personagem, habilidades, relações,
// trabalho e casa, e o registro de ocorrências.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"

class SVilaJournalPanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaJournalPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> Quests();
	TSharedRef<SWidget> Character();
	TSharedRef<SWidget> Skills();
	TSharedRef<SWidget> Relations();
	TSharedRef<SWidget> Work();
	TSharedRef<SWidget> Law();
	static FString MemoryText(const std::string& Tag);
};
