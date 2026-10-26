// Mapa do bairro (port de src/ui/MapPanel.gd): desenhado em OnPaint a partir do JSON do mapa
// (áreas, calçadas, ruas, prédios), com você, seu carro e os marcadores de missão.
#pragma once

#include "CoreMinimal.h"
#include "UI/SVilaHUDOverlay.h"

class SVilaMapPanel : public SVilaPanel
{
public:
	SLATE_BEGIN_ARGS(SVilaMapPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
