#include "UI/SVilaOptionsMenu.h"

#include "InputCoreTypes.h"
#include "UI/VilaUIStyle.h"
#include "VilaConv.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Vila/Settings.h"

using namespace VilaConv;
using VilaUI::T;

void SVilaOptionsMenu::Construct(const FArguments& InArgs)
{
	OnBack = InArgs._OnBack;
	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(820.f)
		.MinDesiredHeight(560.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
			[
				VilaUI::Label(T("Opções"), 24, VilaUI::Accent())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
			[
				SAssignNew(TabsSlot, SBox).WidthOverride(780.f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					VilaUI::Button(T("Restaurar padrões desta aba"), [this]() { ResetTab(); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					VilaUI::Button(T("Voltar"), [this]() { Back(); })
				]
			]
		]
	];
	Build();
}

void SVilaOptionsMenu::Build()
{
	const int32 Cur = Tabs.IsValid() ? Tabs->GetCurrent() : 0;
	WaitingAction.clear();
	WaitingText.Reset();
	TSharedRef<SVilaTabs> NewTabs = SNew(SVilaTabs).ContentHeight(440.f);
	NewTabs->AddTab(T("Gráficos"), Graphics());
	NewTabs->AddTab(T("Áudio"), Audio());
	NewTabs->AddTab(T("Jogabilidade"), Game());
	NewTabs->AddTab(T("Controles"), Controls());
	NewTabs->SetCurrent(Cur);
	Tabs = NewTabs;
	TabsSlot->SetContent(NewTabs);
}

void SVilaOptionsMenu::Line(const TSharedRef<SVerticalBox>& Parent, const FString& Label, const TSharedRef<SWidget>& Ctrl)
{
	Parent->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(300.f)
			[
				VilaUI::Label(Label, 16, VilaUI::TextColor(), 290.f)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			Ctrl
		]
	];
}

// OptionButton: índice salvo como inteiro, ou o valor da lista `Values` quando houver.
TSharedRef<SWidget> SVilaOptionsMenu::Option(const std::string& Section, const std::string& Key, const TArray<FString>& Items, const vila::Json& Values)
{
	TArray<FString> Labels;
	for (const FString& It : Items) Labels.Add(Tr(U(It)));
	const vila::Json Cur = vila::settings().get_value(Section, Key);
	int32 Index = 0;
	if (!Values.empty())
	{
		for (size_t i = 0; i < Values.size(); ++i)
			if (Values[i] == Cur) Index = static_cast<int32>(i);
	}
	else
		Index = vila::to_int(Cur);
	const vila::Json Vals = Values;
	return SNew(SVilaOption)
		.Items(Labels)
		.Selected(FMath::Max(0, Index))
		.OnChanged([Section, Key, Vals](int32 i)
		{
			vila::settings().set_value(Section, Key, Vals.empty() ? vila::Json(i) : Vals[static_cast<size_t>(i)]);
		});
}

TSharedRef<SWidget> SVilaOptionsMenu::Slider(const std::string& Section, const std::string& Key, float Min, float Max, float Step, const char* Fmt, float Mult)
{
	const float Value = static_cast<float>(vila::to_num(vila::settings().get_value(Section, Key)));
	TSharedRef<float> Shown = MakeShared<float>(Value);
	const std::string Pattern = Fmt;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			VilaUI::Slider(Min, Max, Step, Value, [Section, Key, Shown](float V)
			{
				*Shown = V;
				vila::settings().set_value(Section, Key, vila::Json(static_cast<double>(V)));
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f, 0.f, 0.f))
		[
			SNew(SBox).WidthOverride(70.f)
			[
				SNew(STextBlock)
				.Font(VilaUI::Font(15))
				.ColorAndOpacity(FSlateColor(VilaUI::Muted()))
				.Text_Lambda([Shown, Pattern, Mult]() { return FText::FromString(S(vila::fmt(Pattern, {static_cast<double>(*Shown * Mult) + 1e-4}))); })
			]
		];
}

TSharedRef<SWidget> SVilaOptionsMenu::CheckBox(const std::string& Section, const std::string& Key)
{
	return SNew(SBox).HAlign(HAlign_Left)
		[
			VilaUI::Check(vila::truthy(vila::settings().get_value(Section, Key)), [Section, Key](bool bOn)
			{
				vila::settings().set_value(Section, Key, vila::Json(bOn));
			})
		];
}

TSharedRef<SWidget> SVilaOptionsMenu::Graphics()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	vila::Json Res = vila::Json::array();
	TArray<FString> ResNames;
	for (const std::string& R : vila::Settings::resolutions())
	{
		Res.push_back(R);
		ResNames.Add(S(R));
	}
	const TArray<FString> Levels = {TEXT("Baixa"), TEXT("Média"), TEXT("Alta"), TEXT("Épica")};
	Line(V, T("Resolução (janela)"), Option("graphics", "resolution", ResNames, Res));
	Line(V, T("Modo de tela"), Option("graphics", "window_mode", {TEXT("Janela"), TEXT("Tela cheia"), TEXT("Tela cheia sem bordas")}));
	Line(V, T("Escala de renderização"), Slider("graphics", "render_scale", 0.5f, 1.f, 0.05f));
	Line(V, T("Qualidade geral"), Option("graphics", "quality", Levels));
	Line(V, T("Qualidade das sombras"), Option("graphics", "shadows", Levels));
	Line(V, T("Qualidade dos efeitos"), Option("graphics", "effects", Levels));
	Line(V, T("Distância de visão / simulação detalhada"), Option("graphics", "view_distance", {TEXT("Curta"), TEXT("Média"), TEXT("Longa"), TEXT("Máxima")}));
	Line(V, T("Iluminação global"), Option("graphics", "gi", {TEXT("Desligada"), TEXT("Lumen"), TEXT("Lumen + RT por hardware")}));
	Line(V, T("Reflexos"), Option("graphics", "reflections", {TEXT("Espaço de tela (SSR)"), TEXT("Lumen")}));
	Line(V, T("Antisserrilhamento"), Option("graphics", "anti_aliasing", {TEXT("Nenhum"), TEXT("FXAA"), TEXT("TSR")}));
	Line(V, T("Detalhe dos modelos"), Option("graphics", "model_detail", {TEXT("Baixo"), TEXT("Médio"), TEXT("Alto")}));
	Line(V, T("Densidade da grama"), Option("graphics", "grass_density", {TEXT("Sem grama"), TEXT("Baixa"), TEXT("Média"), TEXT("Alta")}));
	Line(V, T("Desfoque de miniatura (tilt-shift)"), Option("graphics", "tilt_shift", {TEXT("Desligado"), TEXT("Sutil"), TEXT("Forte")}));
	Line(V, T("Gradação de cor e vinheta"), CheckBox("graphics", "color_grading"));
	Line(V, T("Desfoque de movimento"), CheckBox("graphics", "motion_blur"));
	Line(V, T("Nuvens volumétricas"), CheckBox("graphics", "volumetric_clouds"));
	Line(V, T("Densidade de pedestres (ao carregar)"), Slider("graphics", "npc_density", 0.f, 1.5f, 0.25f));
	Line(V, T("Densidade do trânsito"), Slider("graphics", "traffic_density", 0.f, 1.5f, 0.25f));
	Line(V, T("Sincronização vertical"), CheckBox("graphics", "vsync"));
	Line(V, T("Limite de FPS"), Option("graphics", "fps_limit", {TEXT("Sem limite"), TEXT("30"), TEXT("60"), TEXT("120"), TEXT("144")}, vila::Json::array({0, 30, 60, 120, 144})));
	return V;
}

TSharedRef<SWidget> SVilaOptionsMenu::Audio()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	Line(V, T("Volume geral"), Slider("audio", "master", 0.f, 1.f, 0.05f));
	Line(V, T("Música"), Slider("audio", "music", 0.f, 1.f, 0.05f));
	Line(V, T("Efeitos sonoros"), Slider("audio", "sfx", 0.f, 1.f, 0.05f));
	Line(V, T("Vozes"), Slider("audio", "voice", 0.f, 1.f, 0.05f));
	Line(V, T("Sons ambientes"), Slider("audio", "ambient", 0.f, 1.f, 0.05f));
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
	[
		VilaUI::Label(T("Os sons do protótipo são sintetizados pelo próprio jogo (originais). Arquivos em assets/audio substituem automaticamente."), 13, VilaUI::Muted(), 740.f)
	];
	return V;
}

TSharedRef<SWidget> SVilaOptionsMenu::Game()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	vila::Json Langs = vila::Json::array();
	TArray<FString> Names;
	for (const auto& It : vila::Settings::languages().items())
	{
		Langs.push_back(It.key());
		Names.Add(S(vila::to_str(It.value())));
	}
	Line(V, T("Idioma"), Option("game", "language", Names, Langs));
	Line(V, T("Legendas (falas sobre os personagens)"), CheckBox("game", "subtitles"));
	Line(V, T("Tamanho do texto"), Slider("game", "text_scale", 0.8f, 1.5f, 0.05f));
	Line(V, T("Sensibilidade da câmera"), Slider("game", "camera_sensitivity", 0.5f, 2.f, 0.1f, "%.1fx", 1.f));
	Line(V, T("Mostrar dicas"), CheckBox("game", "show_hints"));
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
	[
		VilaUI::Label(T("A tradução para inglês cobre menus e interface; diálogos do protótipo estão em português."), 13, VilaUI::Muted(), 740.f)
	];
	return V;
}

TSharedRef<SWidget> SVilaOptionsMenu::Controls()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
	[
		VilaUI::Label(T("Clique em uma ação e pressione a nova tecla (Esc cancela)."), 14, VilaUI::Muted(), 740.f)
	];
	for (const auto& It : vila::Settings::actions().items())
	{
		const std::string Action = It.key();
		const TSharedRef<STextBlock> Txt = SNew(STextBlock)
			.Font(VilaUI::Font(16))
			.Text(FText::FromString(S(vila::settings().binding_text(Action))));
		const TSharedPtr<STextBlock> TxtPtr = Txt;
		TSharedRef<SButton> B = SNew(SButton)
			.ButtonStyle(&VilaUI::ButtonStyle())
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([this, Action, TxtPtr]()
			{
				VilaUI::PlaySound(TEXT("ui_click"), -10.f);
				StartRebind(Action, TxtPtr);
				return FReply::Handled();
			})
			[
				Txt
			];
		Line(V, Tr(vila::jstr(It.value(), "label")), B);
	}
	return V;
}

void SVilaOptionsMenu::StartRebind(const std::string& Action, TSharedPtr<STextBlock> Text)
{
	if (WaitingText.IsValid() && !WaitingAction.empty())
		WaitingText->SetText(FText::FromString(S(vila::settings().binding_text(WaitingAction))));
	WaitingAction = Action;
	WaitingText = Text;
	if (Text.IsValid()) Text->SetText(FText::FromString(T("Pressione uma tecla...")));
}

void SVilaOptionsMenu::FinishRebind(const FString& KeyName)
{
	const std::string Action = WaitingAction;
	WaitingAction.clear();
	WaitingText.Reset();
	vila::settings().rebind(Action, U(KeyName));
	Build();
	Tabs->SetCurrent(3);
}

FReply SVilaOptionsMenu::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (WaitingAction.empty()) return FReply::Unhandled();
	if (InKeyEvent.IsRepeat()) return FReply::Handled();
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		if (WaitingText.IsValid()) WaitingText->SetText(FText::FromString(S(vila::settings().binding_text(WaitingAction))));
		WaitingAction.clear();
		WaitingText.Reset();
	}
	else
		FinishRebind(VilaUI::KeyName(Key));
	return FReply::Handled();
}

FReply SVilaOptionsMenu::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (WaitingAction.empty() || MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) return FReply::Unhandled();
	FinishRebind(VilaUI::KeyName(MouseEvent.GetEffectingButton()));
	return FReply::Handled();
}

FReply SVilaOptionsMenu::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (WaitingAction.empty()) return FReply::Unhandled();
	FinishRebind(MouseEvent.GetWheelDelta() > 0.f ? TEXT("MouseScrollUp") : TEXT("MouseScrollDown"));
	return FReply::Handled();
}

void SVilaOptionsMenu::ResetTab()
{
	static const char* Sections[] = {"graphics", "audio", "game", "input"};
	const int32 Cur = Tabs.IsValid() ? FMath::Clamp(Tabs->GetCurrent(), 0, 3) : 0;
	vila::settings().reset_section(Sections[Cur]);
	Build();
}

void SVilaOptionsMenu::Back()
{
	vila::settings().save();
	if (OnBack) OnBack();
}
