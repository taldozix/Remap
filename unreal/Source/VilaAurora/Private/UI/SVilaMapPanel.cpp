#include "UI/SVilaMapPanel.h"

#include "Rendering/DrawElements.h"
#include "UI/VilaUIStyle.h"
#include "VilaBuilding.h"
#include "VilaConv.h"
#include "VilaPlayerCharacter.h"
#include "VilaVehicle.h"
#include "VilaWorld.h"
#include "VilaWorldBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Vila/Data.h"

using namespace VilaConv;
using VilaUI::T;

// Tela de desenho do mapa (o Control com draw.connect(_draw_map) do Godot).
class SVilaMapCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SVilaMapCanvas) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(600.0, 600.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual bool ComputeVolatility() const override { return true; }
};

namespace
{
	struct FMapXf
	{
		double B0 = -85, B1 = -85, B2 = 85, B3 = 85;
		FVector2D Size;
		// _w2m: metros (x, z) -> pixels do mapa.
		FVector2D W2M(double X, double Z) const
		{
			return FVector2D((X - B0) / (B2 - B0) * Size.X, (Z - B1) / (B3 - B1) * Size.Y);
		}
		FVector2D FromUE(const FVector& P) const { return W2M(P.X / VilaConv::M, P.Y / VilaConv::M); }
	};

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& A, const FVector2D& E, const FLinearColor& Col)
	{
		const FVector2D Min(FMath::Min(A.X, E.X), FMath::Min(A.Y, E.Y));
		const FVector2D Sz(FMath::Abs(E.X - A.X), FMath::Abs(E.Y - A.Y));
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(Min))), VilaUI::White(), ESlateDrawEffect::None, Col);
	}

	void Outline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& A, const FVector2D& E, const FLinearColor& Col, float Thick)
	{
		TArray<FVector2f> P = {FVector2f(A), FVector2f(static_cast<float>(E.X), static_cast<float>(A.Y)), FVector2f(E),
			FVector2f(static_cast<float>(A.X), static_cast<float>(E.Y)), FVector2f(A)};
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Col, true, Thick);
	}

	void Fill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2D>& P, const FLinearColor& Col)
	{
		for (int32 i = 1; i + 1 < P.Num(); ++i)
		{
			const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector2D::Distance(P[i], P[i + 1])), 1, 200);
			TArray<FVector2f> Pts;
			for (int32 s = 0; s <= Steps; ++s)
			{
				Pts.Add(FVector2f(P[0]));
				Pts.Add(FVector2f(P[i] + (P[i + 1] - P[i]) * (static_cast<double>(s) / Steps)));
			}
			FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Col, true, 1.5f);
		}
	}
}

int32 SVilaMapCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FGeometry& G = AllottedGeometry;
	FMapXf X;
	X.Size = G.GetLocalSize();
	const vila::Json& Map = vila::data().map();
	const vila::Json& B = vila::jarr(Map, "bounds");
	if (B.size() >= 4)
	{
		X.B0 = vila::to_num(B[0]);
		X.B1 = vila::to_num(B[1]);
		X.B2 = vila::to_num(B[2]);
		X.B3 = vila::to_num(B[3]);
	}
	int32 L = LayerId;
	Box(OutDrawElements, L, G, FVector2D::ZeroVector, X.Size, VilaUI::SRGB(0.22f, 0.32f, 0.18f));
	++L;
	for (const vila::Json& A : vila::jarr(Map, "areas"))
	{
		const vila::Json& R = vila::jarr(A, "rect");
		if (R.size() < 4) continue;
		const std::string Type = vila::jstr(A, "type");
		const FLinearColor Col = Type == "park" ? VilaUI::SRGB(0.3f, 0.42f, 0.24f) : Type == "asphalt" ? VilaUI::SRGB(0.25f, 0.25f, 0.27f) : VilaUI::SRGB(0.6f, 0.58f, 0.54f);
		Box(OutDrawElements, L, G, X.W2M(vila::to_num(R[0]), vila::to_num(R[1])), X.W2M(vila::to_num(R[2]), vila::to_num(R[3])), Col);
	}
	++L;
	// O painel ainda não conhece o HUD quando a tela é criada: usa o contexto da interface.
	UObject* Ctx = VilaUI::Context();
	AVilaWorld* World = Ctx ? AVilaWorld::Get(Ctx) : nullptr;
	if (World && World->Builder)
	{
		for (const FBox2D& R : World->Builder->SidewalkRects)
			Box(OutDrawElements, L, G, X.W2M(R.Min.X / VilaConv::M, R.Min.Y / VilaConv::M), X.W2M(R.Max.X / VilaConv::M, R.Max.Y / VilaConv::M), VilaUI::SRGB(0.62f, 0.6f, 0.56f));
	}
	++L;
	const vila::Json& Roads = vila::jobj(Map, "roads");
	const vila::Json& Xs = vila::jarr(Roads, "xs");
	const vila::Json& Zs = vila::jarr(Roads, "zs");
	const double Hw = vila::jnum(Roads, "width", 9.0) * 0.5;
	const FLinearColor RoadCol = VilaUI::SRGB(0.17f, 0.17f, 0.19f);
	if (!Xs.empty() && !Zs.empty())
	{
		const double X0 = vila::to_num(Xs.front()), X1 = vila::to_num(Xs.back());
		const double Z0 = vila::to_num(Zs.front()), Z1 = vila::to_num(Zs.back());
		for (const vila::Json& Z : Zs)
			Box(OutDrawElements, L, G, X.W2M(X0 - Hw, vila::to_num(Z) - Hw), X.W2M(X1 + Hw, vila::to_num(Z) + Hw), RoadCol);
		for (const vila::Json& Xv : Xs)
			Box(OutDrawElements, L, G, X.W2M(vila::to_num(Xv) - Hw, Z0 - Hw), X.W2M(vila::to_num(Xv) + Hw, Z1 + Hw), RoadCol);
	}
	++L;

	// Prédios: os construídos pelo mundo; sem mundo, os do JSON.
	TArray<const vila::Json*> Defs;
	if (World && World->Builder && World->Builder->Buildings.Num() > 0)
	{
		for (const auto& It : World->Builder->Buildings)
			if (IsValid(It.Value.Get())) Defs.Add(&It.Value->Def);
	}
	else
	{
		for (const vila::Json& Bd : vila::jarr(Map, "buildings")) Defs.Add(&Bd);
	}
	const FSlateFontInfo NameFont = VilaUI::Font(11);
	for (const vila::Json* D : Defs)
	{
		const vila::Json& C = vila::jarr(*D, "center");
		const vila::Json& Sz = vila::jarr(*D, "size");
		if (C.size() < 2 || Sz.size() < 2) continue;
		const double Cx = vila::to_num(C[0]), Cz = vila::to_num(C[1]);
		const double W = vila::to_num(Sz[0]), Dp = vila::to_num(Sz[1]);
		const FVector2D A = X.W2M(Cx - W * 0.5, Cz - Dp * 0.5);
		const FVector2D E = X.W2M(Cx + W * 0.5, Cz + Dp * 0.5);
		Box(OutDrawElements, L, G, A, E, VilaUI::Hex(vila::jstr(vila::jobj(*D, "style"), "roof", "#888888")));
		Outline(OutDrawElements, L + 1, G, A, E, FLinearColor(0.f, 0.f, 0.f, 0.5f), 1.5f);
		FSlateDrawElement::MakeText(OutDrawElements, L + 2,
			G.ToPaintGeometry(FVector2f(static_cast<float>(FMath::Max(E.X - A.X + 60.0, 90.0)), 16.f), FSlateLayoutTransform(FVector2f(static_cast<float>(A.X + 2.0), static_cast<float>(E.Y + 1.0)))),
			Tr(vila::jstr(*D, "name")), NameFont, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.9f));
	}
	L += 3;

	if (World)
	{
		if (const TObjectPtr<AVilaVehicle>* Car = World->Vehicles.Find(TEXT("carro_jogador")))
		{
			if (IsValid(Car->Get()))
			{
				const FVector2D Cp = X.FromUE((*Car)->GetActorLocation());
				Box(OutDrawElements, L, G, Cp - FVector2D(5.0, 5.0), Cp + FVector2D(5.0, 5.0), VilaUI::Hex("#4f8fe0"));
			}
		}
		for (const FVector& Mk : World->MarkerPositions())
		{
			const FVector2D Mp = X.FromUE(Mk);
			Fill(OutDrawElements, L, G, {Mp + FVector2D(0, -8), Mp + FVector2D(6, 0), Mp + FVector2D(0, 8), Mp + FVector2D(-6, 0)}, VilaUI::Quest());
		}
		if (World->Player)
		{
			const FVector2D Pp = X.FromUE(World->Player->GetActorLocation());
			// Frente do jogador no plano do mapa (UE X/Y = Godot x/z).
			const FVector Fwd = World->Player->GetActorForwardVector();
			FVector2D F2(Fwd.X, Fwd.Y);
			F2 = F2.GetSafeNormal();
			if (F2.IsNearlyZero()) F2 = FVector2D(0.0, -1.0);
			const FVector2D Rt(-F2.Y, F2.X);
			Fill(OutDrawElements, L + 1, G, {Pp + F2 * 9.0, Pp - F2 * 6.0 + Rt * 6.0, Pp - F2 * 6.0 - Rt * 6.0}, VilaUI::Accent());
		}
	}
	return L + 2;
}

void SVilaMapPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		VilaUI::Panel(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
			[
				MakeHeader(T("Vila Aurora — Porto Serra"), 22)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(600.f).HeightOverride(600.f)
				[
					SNew(SVilaMapCanvas)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				VilaUI::Label(T("▲ você   ■ seu carro   ◆ missão"), 14, VilaUI::Muted())
			])
	];
}
