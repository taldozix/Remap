#include "UI/SVilaHUDOverlay.h"

#include "Components/CapsuleComponent.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Math/RotationMatrix.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/VilaUIStyle.h"
#include "VilaBuilding.h"
#include "VilaConv.h"
#include "VilaHUD.h"
#include "VilaInteractable.h"
#include "VilaNPC.h"
#include "VilaPlayerCharacter.h"
#include "VilaPlayerController.h"
#include "VilaTrafficSystem.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "VilaWorldBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Clock.h"
#include "Vila/Data.h"
#include "Vila/Game.h"
#include "Vila/Settings.h"

using namespace VilaConv;

// ================================================================ SVilaPanel
void SVilaPanel::Bind(AVilaHUD* InHUD, const FString& InKind)
{
	HUD = InHUD;
	Kind = InKind;
}

AVilaHUD* SVilaPanel::GetHUD() const { return HUD.Get(); }

AVilaWorld* SVilaPanel::VilaWorld() const
{
	AVilaHUD* H = HUD.Get();
	return H ? H->GetVilaWorld() : nullptr;
}

void SVilaPanel::Close()
{
	if (AVilaHUD* H = HUD.Get()) H->CloseCurrent();
}

void SVilaPanel::TryClose() { Close(); }

FReply SVilaPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || VilaUI::KeyIs(Key, "pause"))
	{
		TryClose();
		return FReply::Handled();
	}
	if (VilaUI::KeyIs(Key, "debug"))
	{
		if (AVilaHUD* H = HUD.Get()) H->ToggleDebug();
		return FReply::Handled();
	}
	// Inventário/diário/mapa fecham com a própria tecla.
	if ((Kind == TEXT("inventory") || Kind == TEXT("journal") || Kind == TEXT("map")) && VilaUI::KeyIs(Key, U(Kind)))
	{
		Close();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SVilaPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Janela modal: o clique não chega ao jogo (MOUSE_FILTER_STOP).
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SVilaPanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();  // sem zoom da câmera por trás da janela
}

TSharedRef<SWidget> SVilaPanel::MakeHeader(const FString& Title, int32 TitleSize, TSharedPtr<SWidget> Extra)
{
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			VilaUI::Label(Title, TitleSize, VilaUI::Accent())
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNullWidget::NullWidget
		];
	if (Extra.IsValid())
	{
		Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			Extra.ToSharedRef()
		];
	}
	TWeakPtr<SVilaPanel> WeakSelf = SharedThis(this);
	Head->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		VilaUI::Button(VilaUI::T("Fechar"), [WeakSelf]()
		{
			if (TSharedPtr<SVilaPanel> P = WeakSelf.Pin()) P->Close();
		})
	];
	return Head;
}

// ================================================================ SVilaSimplePanel
void SVilaSimplePanel::Construct(const FArguments& InArgs)
{
	OnAccept = InArgs._OnAccept;
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		InArgs._Content.Widget
	];
}

FReply SVilaSimplePanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (OnAccept && (Key == EKeys::Enter || Key == EKeys::Virtual_Accept))
	{
		OnAccept();
		return FReply::Handled();
	}
	return SVilaPanel::OnKeyDown(MyGeometry, InKeyEvent);
}

// ================================================================ camada do mundo
// Marcadores de missão (MarkerLayer.gd) e balões de fala dos NPCs (o Label3D do NPC.gd),
// projetados da câmera para a tela.
class SVilaWorldLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaWorldLayer) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AVilaHUD>, HUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { HUD = InArgs._HUD; }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual bool ComputeVolatility() const override { return true; }

private:
	struct FBubble
	{
		FString Text;
		double Start = 0.0;
		TArray<FString> Lines;
		FVector2D Size = FVector2D::ZeroVector;
	};

	void PaintBubbles(AVilaWorld* World, APlayerController* PC, const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const;
	void PaintMarkers(AVilaWorld* World, APlayerController* PC, const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const;

	TWeakObjectPtr<AVilaHUD> HUD;
	mutable TMap<FString, FBubble> Bubbles;
};

namespace
{
	void DrawText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Pos, const FString& Text,
		const FSlateFontInfo& Font, const FLinearColor& Color, bool bShadow = true)
	{
		if (bShadow)
		{
			FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(800.f, 60.f), FSlateLayoutTransform(FVector2f(static_cast<float>(Pos.X + 1.0), static_cast<float>(Pos.Y + 1.0)))),
				Text, Font, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.8f));
		}
		FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(800.f, 60.f), FSlateLayoutTransform(FVector2f(static_cast<float>(Pos.X), static_cast<float>(Pos.Y)))),
			Text, Font, ESlateDrawEffect::None, Color);
	}

	// Polígono convexo preenchido (leque de linhas em zigue-zague: um elemento por triângulo).
	void FillConvex(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2D>& P, const FLinearColor& Color)
	{
		if (P.Num() < 3) return;
		for (int32 i = 1; i + 1 < P.Num(); ++i)
		{
			const FVector2D A = P[0], B = P[i], C = P[i + 1];
			const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector2D::Distance(B, C)), 1, 400);
			TArray<FVector2f> Pts;
			Pts.Reserve(Steps * 2 + 2);
			for (int32 s = 0; s <= Steps; ++s)
			{
				const FVector2D Q = B + (C - B) * (static_cast<double>(s) / Steps);
				Pts.Add(FVector2f(A));
				Pts.Add(FVector2f(Q));
			}
			FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Color, true, 1.5f);
		}
		TArray<FVector2f> Outline;
		for (const FVector2D& V : P) Outline.Add(FVector2f(V));
		Outline.Add(FVector2f(P[0]));
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Outline, ESlateDrawEffect::None, Color, true, 1.f);
	}

	// Posição na tela (unidades locais do Slate) de um ponto do mundo. Falso se atrás da câmera.
	bool Project(APlayerController* PC, const FVector& P, float Scale, FVector2D& Out)
	{
		FVector2D Screen;
		if (!PC->ProjectWorldLocationToScreen(P, Screen, false)) return false;
		Out = Screen / FMath::Max(Scale, 0.01f);
		return true;
	}

	// Rótulos na mesma ordem do AVilaWorld::MarkerPositions (GameWorld.marker_positions).
	TArray<FString> MarkerLabels(AVilaWorld* World)
	{
		TArray<FString> Labels;
		const vila::Json& Active = vila::game().delivery.active;
		if (!Active.empty() && World->Spot(S(vila::jstr(Active, "spot"))) != nullptr) Labels.Add(VilaConv::Tr("Entrega"));
		for (const vila::Json& M : vila::game().quests.active_markers())
		{
			FString Kind, Id;
			if (!S(vila::to_str(M)).Split(TEXT(":"), &Kind, &Id) || Id.Contains(TEXT(":"))) continue;
			if (Kind != TEXT("npc")) continue;
			const TObjectPtr<AVilaNPC>* N = World->NPCs.Find(Id);
			if (N && IsValid(N->Get())) Labels.Add((*N)->DisplayName());
		}
		return Labels;
	}
}

int32 SVilaWorldLayer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	AVilaHUD* H = HUD.Get();
	AVilaWorld* World = H ? H->GetVilaWorld() : nullptr;
	APlayerController* PC = H ? H->GetOwningPlayerController() : nullptr;
	if (!World || !PC || !World->Player) return LayerId;
	PaintBubbles(World, PC, AllottedGeometry, OutDrawElements, LayerId);
	if (!H->IsModalOpen()) PaintMarkers(World, PC, AllottedGeometry, OutDrawElements, LayerId + 3);
	return LayerId + 6;
}

void SVilaWorldLayer::PaintBubbles(AVilaWorld* World, APlayerController* PC, const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const
{
	if (!vila::truthy(vila::settings().get_value("game", "subtitles")))
	{
		Bubbles.Reset();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const FSlateFontInfo Font = VilaUI::Font(16, true);
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector PlayerPos = World->Player->GetActorLocation();
	TSet<FString> Seen;
	for (const TPair<FString, TObjectPtr<AVilaNPC>>& It : World->NPCs)
	{
		AVilaNPC* Npc = It.Value.Get();
		if (!IsValid(Npc) || Npc->IsHidden()) continue;
		const FString Text = Npc->CurrentBark();
		if (Text.IsEmpty()) continue;
		Seen.Add(It.Key);
		FBubble& B = Bubbles.FindOrAdd(It.Key);
		if (B.Text != Text)
		{
			// Quebra de linha manual (largura ~ do Label3D do Godot).
			B.Text = Text;
			B.Start = Now;
			B.Lines.Reset();
			TArray<FString> Words;
			Text.ParseIntoArray(Words, TEXT(" "), true);
			FString Line;
			for (const FString& W : Words)
			{
				const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
				if (!Line.IsEmpty() && Measure->Measure(Try, Font).X > 260.f)
				{
					B.Lines.Add(Line);
					Line = W;
				}
				else
					Line = Try;
			}
			if (!Line.IsEmpty()) B.Lines.Add(Line);
			double MaxW = 0.0, TotalH = 0.0;
			for (const FString& L : B.Lines)
			{
				const auto Sz = Measure->Measure(L, Font);
				MaxW = FMath::Max<double>(MaxW, Sz.X);
				TotalH += Sz.Y;
			}
			B.Size = FVector2D(MaxW, TotalH);
		}
		if (FVector::Dist(PlayerPos, Npc->GetActorLocation()) > 3500.f) continue;
		// Godot: rótulo a 2,25 m dos pés; no Unreal a origem do personagem é o centro da cápsula.
		float Half = 90.f;
		if (const UCapsuleComponent* Cap = Npc->GetCapsuleComponent()) Half = Cap->GetScaledCapsuleHalfHeight();
		const FVector Head = Npc->GetActorLocation() + FVector(0.0, 0.0, 225.0 - Half);
		FVector2D P;
		if (!Project(PC, Head, G.Scale, P)) continue;
		const float Alpha = FMath::Clamp(4.f - static_cast<float>(Now - B.Start), 0.f, 1.f);
		if (Alpha <= 0.f) continue;
		const FVector2D Pad(10.0, 6.0);
		const FVector2D BoxSize = B.Size + Pad * 2.0;
		const FVector2D TopLeft = P - FVector2D(BoxSize.X * 0.5, BoxSize.Y);
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(FVector2f(TopLeft))),
			VilaUI::RoundWhite(8.f), ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.03f, 0.72f * Alpha));
		double Y = TopLeft.Y + Pad.Y;
		for (const FString& L : B.Lines)
		{
			const auto Sz = Measure->Measure(L, Font);
			DrawText(Out, Layer + 1, G, FVector2D(P.X - Sz.X * 0.5, Y), L, Font, FLinearColor(1.f, 1.f, 1.f, Alpha), false);
			Y += Sz.Y;
		}
	}
	for (auto ItB = Bubbles.CreateIterator(); ItB; ++ItB)
		if (!Seen.Contains(ItB.Key())) ItB.RemoveCurrent();
}

void SVilaWorldLayer::PaintMarkers(AVilaWorld* World, APlayerController* PC, const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const
{
	const TArray<FVector> Positions = World->MarkerPositions();
	if (Positions.Num() == 0) return;
	TArray<FString> Labels = MarkerLabels(World);
	if (Labels.Num() != Positions.Num()) Labels.Init(FString(), Positions.Num());

	const FVector2D Vp = G.GetLocalSize();
	const FVector2D Center = Vp * 0.5;
	const FLinearColor Col = VilaUI::Quest();
	const FVector PlayerPos = World->Player->GetActorLocation();
	const float Margin = 40.f;
	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		const FVector& Pos = Positions[i];
		const int32 Dist = static_cast<int32>(FVector::Dist(PlayerPos, Pos) / 100.0);
		FVector2D Sp;
		const bool bFront = Project(PC, Pos, G.Scale, Sp);
		const bool bOnScreen = bFront && Sp.X > Margin && Sp.X < Vp.X - Margin && Sp.Y > Margin && Sp.Y < Vp.Y - Margin;
		if (bOnScreen)
		{
			const double Bob = FMath::Sin(FPlatformTime::Seconds() * 1000.0 / 300.0) * 4.0;
			const FVector2D C = Sp + FVector2D(0.0, Bob);
			FillConvex(Out, Layer, G, {C + FVector2D(0, -12), C + FVector2D(9, 0), C + FVector2D(0, 12), C + FVector2D(-9, 0)}, Col);
			const FString Txt = Labels[i].IsEmpty() ? FString::Printf(TEXT("%dm"), Dist) : FString::Printf(TEXT("%s · %dm"), *Labels[i], Dist);
			DrawText(Out, Layer + 1, G, C + FVector2D(14.0, -10.0), Txt, VilaUI::Font(15), Col);
		}
		else
		{
			FVector2D Dir;
			if (bFront)
				Dir = (Sp - Center).GetSafeNormal();
			else
			{
				// Atrás da câmera: direção no plano da câmera (direita / cima).
				FVector CamLoc;
				FRotator CamRot;
				PC->GetPlayerViewPoint(CamLoc, CamRot);
				const FVector D = Pos - CamLoc;
				const FRotationMatrix M(CamRot);
				Dir = FVector2D(FVector::DotProduct(D, M.GetScaledAxis(EAxis::Y)), -FVector::DotProduct(D, M.GetScaledAxis(EAxis::Z))).GetSafeNormal();
			}
			if (Dir.IsNearlyZero()) Dir = FVector2D(0.0, 1.0);
			const FVector2D Edge = Center + Dir * (FMath::Min(Vp.X, Vp.Y) * 0.45);
			const double Ang = FMath::Atan2(Dir.Y, Dir.X);
			const auto Rot = [Ang](const FVector2D& V) { return FVector2D(V.X * FMath::Cos(Ang) - V.Y * FMath::Sin(Ang), V.X * FMath::Sin(Ang) + V.Y * FMath::Cos(Ang)); };
			FillConvex(Out, Layer, G, {Edge + Rot(FVector2D(16, 0)), Edge + Rot(FVector2D(-8, 9)), Edge + Rot(FVector2D(-8, -9))}, Col);
			const FString Txt = Labels[i].IsEmpty() ? FString::Printf(TEXT("%dm"), Dist) : FString::Printf(TEXT("%s %dm"), *Labels[i], Dist);
			DrawText(Out, Layer + 1, G, Edge + FVector2D(-30.0, 12.0), Txt, VilaUI::Font(14), Col);
		}
	}
}

// ================================================================ SVilaHUDOverlay
void SVilaHUDOverlay::Construct(const FArguments& InArgs)
{
	HUD = InArgs._HUD;
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			SAssignNew(HUDSlot, SBox).Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(MenuSlot, SBox).Visibility(EVisibility::Collapsed)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(ModalSlot, SBox).Visibility(EVisibility::Collapsed)
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(VilaUI::White())
			.Visibility_Lambda([this]()
			{
				const AVilaHUD* H = HUD.Get();
				return H && H->GetFadeAlpha() > 0.001f ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.BorderBackgroundColor_Lambda([this]()
			{
				const AVilaHUD* H = HUD.Get();
				return FSlateColor(FLinearColor(0.f, 0.f, 0.f, H ? H->GetFadeAlpha() : 0.f));
			})
		]
	];
	RebuildHUD();
}

void SVilaHUDOverlay::SetModal(TSharedRef<SWidget> Panel, float Dim)
{
	ModalSlot->SetContent(
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(VilaUI::White())
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.f, 0.f, 0.f, Dim)))
		]
		+ SOverlay::Slot()
		[
			Panel
		]);
	ModalSlot->SetVisibility(EVisibility::SelfHitTestInvisible);
}

void SVilaHUDOverlay::ClearModal()
{
	ModalSlot->SetContent(SNullWidget::NullWidget);
	ModalSlot->SetVisibility(EVisibility::Collapsed);
}

void SVilaHUDOverlay::SetMenu(TSharedPtr<SWidget> Menu)
{
	MenuWidget = Menu;
	MenuSlot->SetContent(Menu.IsValid() ? Menu.ToSharedRef() : SNullWidget::NullWidget);
	MenuSlot->SetVisibility(Menu.IsValid() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void SVilaHUDOverlay::ToggleDebug()
{
	bDebug = !bDebug;
	if (DebugText.IsValid()) DebugText->SetVisibility(bDebug ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void SVilaHUDOverlay::RebuildHUD()
{
	Toasts.Reset();
	LastPromptKey.Reset();
	LastMoodleKey.Reset();
	LastJobKey.Reset();
	using namespace VilaUI;

	TSharedRef<SOverlay> L = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(WorldLayer, SVilaWorldLayer).HUD(HUD)
		]
		// Relógio e dinheiro (canto superior direito)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 14.f, 14.f, 0.f))
		[
			SNew(SBox).MinDesiredWidth(286.f)
			[
				Panel(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ClockText, STextBlock).Font(Font(20)).ColorAndOpacity(FSlateColor(TextColor()))]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(MoneyText, STextBlock).Font(Font(20)).ColorAndOpacity(FSlateColor(Good()))]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(10.f, 0.f, 0.f, 0.f))
						[
							SAssignNew(MoneyDeltaText, STextBlock).Font(Font(16, true)).Visibility(EVisibility::Collapsed)
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(WeatherText, STextBlock).Font(Font(14)).ColorAndOpacity(FSlateColor(Muted()))])
			]
		]
		// Notificações (topo central)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 14.f, 0.f, 0.f))
		[
			SNew(SBox).WidthOverride(520.f)
			[
				SAssignNew(ToastBox, SVerticalBox)
			]
		]
		// Missão ativa e turno (canto superior esquerdo)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(14.f, 14.f, 0.f, 0.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(QuestBox, SBox).WidthOverride(330.f).Visibility(EVisibility::Collapsed)
				[
					Panel(SAssignNew(QuestText, STextBlock).Font(Font(15)).ColorAndOpacity(FSlateColor(Quest())).WrapTextAt(300.f))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 0.f))
			[
				SAssignNew(JobBox, SBox).WidthOverride(330.f).Visibility(EVisibility::Collapsed)
				[
					Panel(SAssignNew(JobLines, SVerticalBox))
				]
			]
		]
		// Diagnóstico (F3)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(14.f, 420.f, 0.f, 0.f))
		[
			SAssignNew(DebugText, STextBlock)
			.Font(Font(13))
			.ColorAndOpacity(FSlateColor(SRGB(0.7f, 1.f, 0.7f)))
			.ShadowOffset(FVector2D(1.0, 1.0))
			.Visibility(bDebug ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
		]
		// Moodles (canto inferior esquerdo)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(14.f, 0.f, 0.f, 20.f))
		[
			SAssignNew(MoodleBox, SVerticalBox)
		]
		// Prompt de interação (inferior central)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 30.f))
		[
			SAssignNew(PromptBox, SBox).MinDesiredWidth(440.f).Visibility(EVisibility::Collapsed)
			[
				Panel(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(PromptTitle, STextBlock).Font(Font(16)).ColorAndOpacity(FSlateColor(Accent()))]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(PromptList, SVerticalBox)])
			]
		]
		// Ação com tempo (centro, um pouco abaixo)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 220.f, 0.f, 0.f))
		[
			SAssignNew(ProgressBox, SBox).WidthOverride(320.f).Visibility(EVisibility::Collapsed)
			[
				Panel(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ProgressText, STextBlock).Font(Font(15)).ColorAndOpacity(FSlateColor(TextColor()))]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
					[
						Bar(TAttribute<TOptional<float>>::CreateLambda([this]() { return TOptional<float>(ProgressValue); }), FSlateColor(Accent()), 290.f)
					])
			]
		]
		// Veículo (inferior direito)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 20.f, 20.f))
		[
			SAssignNew(VehicleBox, SBox).MinDesiredWidth(250.f).Visibility(EVisibility::Collapsed)
			[
				Panel(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(VehicleText, STextBlock).Font(Font(17)).ColorAndOpacity(FSlateColor(TextColor()))]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
					[
						Bar(TAttribute<TOptional<float>>::CreateLambda([this]() { return TOptional<float>(FuelValue); }), FSlateColor(Hex("#e0b050")), 220.f)
					])
			]
		];
	HUDLayer = L;
	HUDSlot->SetContent(L);
	HudTimer = 0.f;
}

TSharedRef<SWidget> SVilaHUDOverlay::Chip(const FString& Text, const FLinearColor& Col) const
{
	const FLinearColor Dark(Col.R * 0.25f, Col.G * 0.25f, Col.B * 0.25f, 0.9f);
	return SNew(SBox).Padding(FMargin(0.f, 2.f))
		[
			SNew(SBorder)
			.BorderImage(VilaUI::RoundWhite(8.f))
			.BorderBackgroundColor(FSlateColor(Col))
			.Padding(FMargin(2.f))
			[
				SNew(SBorder)
				.BorderImage(VilaUI::RoundWhite(7.f))
				.BorderBackgroundColor(FSlateColor(Dark))
				.Padding(FMargin(8.f, 5.f))
				[
					VilaUI::Label(Text, 15, VilaUI::Lighten(Col, 0.2f))
				]
			]
		];
}

void SVilaHUDOverlay::AddToast(const FString& Text, const FString& Kind)
{
	if (!ToastBox.IsValid()) return;
	TSharedRef<SWidget> W = SNew(SBox).Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		[
			VilaUI::Panel(
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Font(VilaUI::Font(16))
				.ColorAndOpacity(FSlateColor(VilaUI::KindColor(Kind)))
				.Justification(ETextJustify::Center)
				.WrapTextAt(480.f))
		];
	ToastBox->AddSlot().AutoHeight()[W];
	Toasts.Add({W, 0.f});
	while (Toasts.Num() > 5)
	{
		ToastBox->RemoveSlot(Toasts[0].Widget.ToSharedRef());
		Toasts.RemoveAt(0);
	}
}

void SVilaHUDOverlay::OnMoneyChanged(int32 Total, int32 Delta)
{
	if (Delta == 0 || !MoneyDeltaText.IsValid()) return;
	// Animação da variação do dinheiro (o Godot só trocava o número).
	MoneyDeltaText->SetText(FText::FromString(Delta > 0 ? FString::Printf(TEXT("+R$ %d"), Delta) : FString::Printf(TEXT("-R$ %d"), -Delta)));
	MoneyDeltaText->SetColorAndOpacity(FSlateColor(Delta > 0 ? VilaUI::Good() : VilaUI::Bad()));
	MoneyDeltaText->SetVisibility(EVisibility::HitTestInvisible);
	MoneyDeltaAge = 0.f;
	HudTimer = 0.f;  // atualiza o total já
}

void SVilaHUDOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	AVilaHUD* H = HUD.Get();
	if (!H) return;
	H->TickUI(InDeltaTime);
	if (InDeltaTime > 0.f) FpsSmoothed = FMath::Lerp(FpsSmoothed, 1.f / InDeltaTime, 0.1f);

	// Notificações: 4,5 s visíveis + 0,6 s sumindo.
	for (int32 i = Toasts.Num() - 1; i >= 0; --i)
	{
		FToast& T = Toasts[i];
		T.Age += InDeltaTime;
		if (T.Age >= 5.1f)
		{
			ToastBox->RemoveSlot(T.Widget.ToSharedRef());
			Toasts.RemoveAt(i);
			continue;
		}
		T.Widget->SetRenderOpacity(T.Age > 4.5f ? 1.f - (T.Age - 4.5f) / 0.6f : 1.f);
	}
	if (MoneyDeltaAge < 99.f)
	{
		MoneyDeltaAge += InDeltaTime;
		const float A = FMath::Clamp(1.8f - MoneyDeltaAge, 0.f, 1.f);
		MoneyDeltaText->SetRenderOpacity(A);
		if (A <= 0.f)
		{
			MoneyDeltaText->SetVisibility(EVisibility::Collapsed);
			MoneyDeltaAge = 99.f;
		}
	}

	AVilaWorld* World = H->GetVilaWorld();
	const bool bPlaying = World && World->Player && !H->IsMenuShown();
	HUDLayer->SetVisibility(bPlaying ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (!bPlaying) return;
	HudTimer -= InDeltaTime;
	if (HudTimer <= 0.f)
	{
		HudTimer = 0.15f;
		RefreshHUD();
	}
	RefreshPrompt();
}

void SVilaHUDOverlay::RefreshHUD()
{
	using namespace VilaUI;
	AVilaHUD* H = HUD.Get();
	AVilaWorld* World = H ? H->GetVilaWorld() : nullptr;
	if (!World || !World->Player) return;
	AVilaPlayerCharacter* Pl = World->Player;
	vila::Game& G = vila::game();
	vila::Clock& Clk = vila::clock();
	vila::PlayerState& P = G.player;

	ClockText->SetText(FText::FromString(FString::Printf(TEXT("%s, %s · %s %d"), *S(Clk.weekday_name()), *S(Clk.time_string()), *T("Dia"), Clk.day())));
	MoneyText->SetText(FText::FromString(FString::Printf(TEXT("R$ %d"), P.money)));
	MoneyText->SetColorAndOpacity(FSlateColor(P.money >= 0 ? Good() : Bad()));
	const std::string W = vila::jstr(G.world_state.weather, "state", "limpo");
	FString WTxt = W == "limpo" ? T("Céu limpo") : W == "nublado" ? T("Nublado") : W == "chuva" ? T("Chovendo") : S(W);
	FString BName = T("Vila Aurora");
	if (!World->PlayerBuilding.IsEmpty() && World->Builder)
		if (const TObjectPtr<AVilaBuilding>* B = World->Builder->Buildings.Find(World->PlayerBuilding))
			if (IsValid(B->Get())) BName = Tr(vila::jstr((*B)->Def, "name"));
	WeatherText->SetText(FText::FromString(FString::Printf(TEXT("%s · %s"), *WTxt, *BName)));

	// Moodles
	{
		TArray<TPair<FString, FLinearColor>> Chips;
		const FLinearColor Orange = Hex("#f08a3a");
		for (const vila::Json& M : P.needs.moodles())
		{
			const int32 Lvl = FMath::Clamp(vila::jint(M, "level") - 1, 0, 2);
			const FLinearColor Col = Lvl == 0 ? Warn() : Lvl == 1 ? Orange : Bad();
			Chips.Add({Tr(vila::jstr(M, "label")), Col});
		}
		if (P.is_overloaded()) Chips.Add({T("Sobrecarregado"), Orange});
		if (G.property.total_debt() > 0) Chips.Add({F("Aluguel atrasado: R$ %d", {G.property.total_debt()}), Bad()});
		FString Key;
		for (const auto& C : Chips) Key += C.Key + C.Value.ToString();
		if (Key != LastMoodleKey)
		{
			LastMoodleKey = Key;
			MoodleBox->ClearChildren();
			for (const auto& C : Chips) MoodleBox->AddSlot().AutoHeight()[Chip(C.Key, C.Value)];
		}
	}

	// Missão
	FString Q;
	for (const auto& It : G.quests.states.items())
	{
		if (G.quests.state_of(It.key()) == "active")
		{
			Q = FString::Printf(TEXT("%s\n%s"), *Tr(vila::jstr(vila::data().quest(It.key()), "title")).ToUpper(), *S(G.quests.objective(It.key())));
			break;
		}
	}
	const auto Append = [&Q](const FString& Part) { Q += (Q.IsEmpty() ? TEXT("") : TEXT("\n\n")) + Part; };
	if (!G.delivery.active.empty())
	{
		const double Left = G.delivery.time_left(Clk.total_minutes);
		const FString When = Left >= 0.0 ? F("%d min restantes", {static_cast<int>(Left)}) : T("atrasada!");
		Append(FString::Printf(TEXT("%s\n%s — %s"), *T("ENTREGA DO MERCADINHO"), *S(vila::jstr(G.delivery.active, "label")), *When));
	}
	if (G.director.is_active("promocao_mercadinho")) Append(T("PROMOÇÃO no Mercadinho (-25%)"));
	if (G.director.is_active("turno_extra") && G.job.employed) Append(T("TURNO EXTRA no Brasa: +50% por hora até 18h"));
	QuestBox->SetVisibility(Q.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	QuestText->SetText(FText::FromString(Q));

	// Turno
	JobBox->SetVisibility(G.job.on_shift ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (G.job.on_shift) RefreshJob();

	// Veículo
	AVilaVehicle* Veh = Pl->Vehicle;
	const bool bDriving = Pl->State == TEXT("driving") && IsValid(Veh);
	VehicleBox->SetVisibility(bDriving ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (bDriving)
	{
		const vila::Json& VS = vila::jobj(G.world_state.vehicles, U(Veh->VehicleId));
		const int32 Kmh = static_cast<int32>(FMath::Abs(Veh->Speed) / 100.f * 3.6f);
		VehicleText->SetText(FText::FromString(FString::Printf(TEXT("%s\n%d km/h · %s %d%%"), *Veh->DisplayName(), Kmh, *T("Dano"), vila::jint(VS, "damage"))));
		const float Cap = Veh->FuelCapacity();
		FuelValue = Cap > 0.f ? FMath::Clamp(static_cast<float>(vila::jnum(VS, "fuel")) / Cap, 0.f, 1.f) : 0.f;
	}

	// Diagnóstico
	if (bDebug)
	{
		int32 Far = 0, Hidden = 0;
		const float Lod = World->NPCLodDistance();
		const FVector PP = Pl->GetActorLocation();
		for (const TPair<FString, TObjectPtr<AVilaNPC>>& It : World->NPCs)
		{
			const AVilaNPC* N = It.Value.Get();
			if (!IsValid(N)) continue;
			if (FVector::Dist(PP, N->GetActorLocation()) > Lod) ++Far;
			if (N->IsHidden()) ++Hidden;
		}
		const int32 Cars = World->Traffic ? World->Traffic->Cars.Num() : 0;
		const vila::Vec3 Pos = FromUE(PP);
		const FString PosTxt = FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Pos.x, Pos.y, Pos.z);
		DebugText->SetText(FText::FromString(FString::Printf(TEXT("FPS %d | NPCs %d (longe %d, ocultos %d) | carros %d | pos %s | atividade %s"),
			FMath::RoundToInt(FpsSmoothed), World->NPCs.Num(), Far, Hidden, Cars, *PosTxt, *S(P.activity))));
	}
}

void SVilaHUDOverlay::RefreshJob()
{
	using namespace VilaUI;
	vila::Game& G = vila::game();
	using FSeg = TPair<FString, FLinearColor>;
	TArray<TArray<FSeg>> Lines;
	const FLinearColor Tx = TextColor();
	Lines.Add({FSeg(T("TURNO NO BRASA"), Accent()), FSeg(TEXT(" — ") + S(G.job.rank_title()), Tx)});
	Lines.Add({FSeg(F("Trabalhado: %d min · Pedidos: %d · Gorjetas: R$ %d",
		{static_cast<int>(vila::jnum(G.job.shift, "minutes")), vila::jint(G.job.shift, "ok"), vila::jint(G.job.shift, "tips")}), Tx)});
	if (G.job.orders.empty()) Lines.Add({FSeg(T("Aguardando clientes..."), Hex("#9a9a9a"))});
	for (const vila::Json& O : G.job.orders)
	{
		const double Max = vila::jnum(O, "max_patience", 1.0);
		const double Frac = Max > 0.0 ? vila::jnum(O, "patience") / Max : 0.0;
		const FLinearColor Col = Frac > 0.5 ? Hex("#7fd17f") : Frac > 0.25 ? Hex("#f2c14e") : Hex("#ef6b5b");
		Lines.Add({FSeg(FString::Printf(TEXT("#%d"), vila::jint(O, "id")), Col),
			FSeg(TEXT(" ") + S(vila::DriveThruJob::order_text(vila::jobj(O, "remaining"))), Tx),
			FSeg(FString::Printf(TEXT("  (%d%%)"), static_cast<int>(Frac * 100.0)), Col)});
	}
	TArray<FSeg> StationLine;
	const vila::Json& Defs = vila::jobj(vila::data().job("brasa"), "stations");
	for (const auto& It : G.job.stations.items())
	{
		const FString Lbl = Tr(vila::jstr(vila::jobj(Defs, It.key()), "label"));
		const std::string St = vila::jstr(It.value(), "state");
		if (St == "cooking")
			StationLine.Add(FSeg(FString::Printf(TEXT("%s: %d%%  "), *Lbl, static_cast<int>(G.job.station_progress(It.key()) * 100.0)), Tx));
		else if (St == "ready")
			StationLine.Add(FSeg(FString::Printf(TEXT("%s: %s  "), *Lbl, *T("PRONTO")), Hex("#7fd17f")));
		else if (St == "burnt")
			StationLine.Add(FSeg(FString::Printf(TEXT("%s: %s  "), *Lbl, *T("QUEIMOU")), Hex("#ef6b5b")));
	}
	if (StationLine.Num() > 0) Lines.Add(StationLine);
	TArray<FString> Hands;
	for (const vila::Json& St : G.player.inventory.items)
	{
		const std::string Id = vila::jstr(St, "id");
		if (vila::jbool(vila::data().item(Id), "job_item"))
			Hands.Add(FString::Printf(TEXT("%dx %s"), vila::jint(St, "qty"), *S(vila::data().item_name(Id))));
	}
	Lines.Add({FSeg(F("Nas mãos: %s", {U(Hands.Num() > 0 ? FString::Join(Hands, TEXT(", ")) : FString(TEXT("—")))}), Tx)});

	FString Key;
	for (const TArray<FSeg>& L : Lines)
	{
		for (const FSeg& Sg : L) Key += Sg.Key + TEXT("|");
		Key += TEXT("\n");
	}
	if (Key == LastJobKey) return;
	LastJobKey = Key;
	JobLines->ClearChildren();
	bool bFirst = true;
	for (const TArray<FSeg>& L : Lines)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < L.Num(); ++i)
		{
			Row->AddSlot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(L[i].Key))
				.Font(Font(15, bFirst && i == 0))
				.ColorAndOpacity(FSlateColor(L[i].Value))
				.WrapTextAt(L.Num() == 1 ? 320.f : 0.f)
			];
		}
		JobLines->AddSlot().AutoHeight()[Row];
		bFirst = false;
	}
}

void SVilaHUDOverlay::RefreshPrompt()
{
	using namespace VilaUI;
	AVilaHUD* H = HUD.Get();
	AVilaWorld* World = H ? H->GetVilaWorld() : nullptr;
	AVilaPlayerCharacter* Pl = World ? World->Player.Get() : nullptr;
	if (!Pl) return;
	const bool bTimed = Pl->IsTimedActive();
	ProgressBox->SetVisibility(bTimed ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (bTimed)
	{
		ProgressText->SetText(FText::FromString(Pl->TimedLabel()));
		ProgressValue = FMath::Clamp(Pl->TimedProgress(), 0.f, 1.f);
	}
	AActor* Focus = Pl->GetFocus();
	const TArray<FVilaAction> Actions = Focus ? Pl->GetFocusActions() : TArray<FVilaAction>();
	const bool bShow = Focus && Actions.Num() > 0 && !H->IsModalOpen() && !bTimed;
	PromptBox->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (!bShow)
	{
		LastPromptKey.Reset();
		return;
	}
	const IVilaInteractable* It = Cast<IVilaInteractable>(Focus);
	const FString Title = It ? It->GetInteractTitle() : FString();
	FString Key = Title;
	for (const FVilaAction& A : Actions) Key += FString::Printf(TEXT("|%s|%s|%d|%s"), *A.Id, *A.Label, A.bEnabled ? 1 : 0, *A.Hint);
	if (Key == LastPromptKey) return;
	LastPromptKey = Key;
	PromptTitle->SetText(FText::FromString(Title));
	PromptList->ClearChildren();
	bool bFirstDone = false;
	for (int32 i = 0; i < Actions.Num(); ++i)
	{
		const FVilaAction& A = Actions[i];
		FString KeyTxt;
		if (A.bEnabled && !bFirstDone)
		{
			KeyTxt = FString::Printf(TEXT("[%s] "), *S(vila::settings().primary_key_text("interact")));
			bFirstDone = true;
		}
		else if (i < 4)
			KeyTxt = FString::Printf(TEXT("[%s] "), *S(vila::settings().primary_key_text(vila::fmt("option_%d", {i + 1}))));
		FString Txt = KeyTxt + A.Label;
		if (!A.bEnabled && !A.Hint.IsEmpty()) Txt += TEXT(" — ") + A.Hint;
		PromptList->AddSlot().AutoHeight()[Label(Txt, 16, A.bEnabled ? TextColor() : Muted())];
	}
}
