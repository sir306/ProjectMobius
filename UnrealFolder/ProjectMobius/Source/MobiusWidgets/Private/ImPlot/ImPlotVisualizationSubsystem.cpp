/**
 * ImPlot visualization subsystem implementation.
 */
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "Slate/Components/SImPlotOverlay.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Core/MobiusWidgetSubsystem.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "Misc/App.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/RenderingCommon.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"

#include "imgui.h"
#include "implot.h"
namespace
{
	const FName DefaultChartId = NAME_None;
}

void UImPlotVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UImPlotVisualizationSubsystem::Deinitialize()
{
        for (auto& Pair : OverlayStates)
        {
                CloseOverlayWindow(Pair.Value);
                Pair.Value.OverlayWidget.Reset();
        }
        OverlayStates.Empty();
        SharedFontAtlas = nullptr;
        SharedFontBrush.Reset();
        SharedFontTextureId = 0;
        SharedFontTextureName = NAME_None;

        Super::Deinitialize();
}

void UImPlotVisualizationSubsystem::ShowOverlay(bool bShow)
{
	ShowOverlayForChart(DefaultChartId, bShow);
}

void UImPlotVisualizationSubsystem::ToggleOverlay()
{
	ToggleOverlayForChart(DefaultChartId);
}

void UImPlotVisualizationSubsystem::CloseOverlay()
{
	CloseOverlayForChart(DefaultChartId);
}

void UImPlotVisualizationSubsystem::SetChartTitle(const FText& InTitle)
{
	SetChartTitleForChart(DefaultChartId, InTitle);
}

void UImPlotVisualizationSubsystem::SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	SetAxisSettingsForChart(DefaultChartId, InXTitle, InYTitle, InXMin, InXMax, InYMin, InYMax);
}

void UImPlotVisualizationSubsystem::SetPlotPoints(const TArray<FVector2D>& InPoints)
{
	SetPlotPointsForChart(DefaultChartId, InPoints);
}

void UImPlotVisualizationSubsystem::UpdateLiveSample(double InTimeSeconds, double InCount)
{
	UpdateLiveSampleForChart(DefaultChartId, InTimeSeconds, InCount);
}

void UImPlotVisualizationSubsystem::ShowOverlayForChart(const FName& ChartId, bool bShow)
{
        FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
        State.bOverlayVisible = bShow;

        if (bShow)
        {
                State.bWindowOpen = true;
                EnsureOverlayWidget(State, ChartId);
                OpenOverlayWindow(State, ChartId);
        }
        else
        {
                CloseOverlayWindow(State);
        }

        InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::ToggleOverlayForChart(const FName& ChartId)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	ShowOverlayForChart(ChartId, !State.bOverlayVisible);
}

void UImPlotVisualizationSubsystem::CloseOverlayForChart(const FName& ChartId)
{
	ShowOverlayForChart(ChartId, false);
}

void UImPlotVisualizationSubsystem::SetChartTitleForChart(const FName& ChartId, const FText& InTitle)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	State.ChartTitle = InTitle;
	InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::SetAxisSettingsForChart(const FName& ChartId, const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	State.XAxisTitle = InXTitle;
	State.YAxisTitle = InYTitle;
	State.XMin = InXMin;
	State.XMax = InXMax;
	State.YMin = InYMin;
	State.YMax = InYMax;
	State.bHasAxisSettings = true;
	InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::SetPlotPointsForChart(const FName& ChartId, const TArray<FVector2D>& InPoints)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	State.PlotPoints = InPoints;
	InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::UpdateLiveSampleForChart(const FName& ChartId, double InTimeSeconds, double InCount)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	if (State.bHasLiveSample)
	{
		State.LiveSampleThickness = FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(InTimeSeconds - State.LiveTimeSeconds));
		State.bHasLiveSampleThickness = true;
	}
	else
	{
		State.LiveSampleThickness = 0.0;
		State.bHasLiveSampleThickness = false;
	}

	State.LiveTimeSeconds = InTimeSeconds;
	State.LiveCount = InCount;
	State.bHasLiveSample = true;
	InvalidateOverlay(ChartId);
}

bool UImPlotVisualizationSubsystem::IsOverlayVisible() const
{
	return IsOverlayVisibleForChart(DefaultChartId);
}

const FText& UImPlotVisualizationSubsystem::GetChartTitle() const
{
	return GetChartTitleForChart(DefaultChartId);
}

const FText& UImPlotVisualizationSubsystem::GetXAxisTitle() const
{
	return GetXAxisTitleForChart(DefaultChartId);
}

const FText& UImPlotVisualizationSubsystem::GetYAxisTitle() const
{
	return GetYAxisTitleForChart(DefaultChartId);
}

void UImPlotVisualizationSubsystem::GetAxisLimits(double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const
{
	GetAxisLimitsForChart(DefaultChartId, OutXMin, OutXMax, OutYMin, OutYMax);
}

bool UImPlotVisualizationSubsystem::HasAxisSettings() const
{
	return HasAxisSettingsForChart(DefaultChartId);
}

const TArray<FVector2D>& UImPlotVisualizationSubsystem::GetPlotPoints() const
{
	return GetPlotPointsForChart(DefaultChartId);
}

bool UImPlotVisualizationSubsystem::HasLiveSample() const
{
	return HasLiveSampleForChart(DefaultChartId);
}

void UImPlotVisualizationSubsystem::GetLiveSample(double& OutTimeSeconds, double& OutCount) const
{
	GetLiveSampleForChart(DefaultChartId, OutTimeSeconds, OutCount);
}

bool UImPlotVisualizationSubsystem::HasLiveSampleThickness() const
{
	return HasLiveSampleThicknessForChart(DefaultChartId);
}

double UImPlotVisualizationSubsystem::GetLiveSampleThickness() const
{
	return GetLiveSampleThicknessForChart(DefaultChartId);
}

bool UImPlotVisualizationSubsystem::IsOverlayVisibleForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bOverlayVisible : false;
}

const FText& UImPlotVisualizationSubsystem::GetChartTitleForChart(const FName& ChartId) const
{
	static const FText EmptyText = FText::GetEmpty();
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->ChartTitle : EmptyText;
}

const FText& UImPlotVisualizationSubsystem::GetXAxisTitleForChart(const FName& ChartId) const
{
	static const FText EmptyText = FText::GetEmpty();
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->XAxisTitle : EmptyText;
}

const FText& UImPlotVisualizationSubsystem::GetYAxisTitleForChart(const FName& ChartId) const
{
	static const FText EmptyText = FText::GetEmpty();
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->YAxisTitle : EmptyText;
}

void UImPlotVisualizationSubsystem::GetAxisLimitsForChart(const FName& ChartId, double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	if (State)
	{
		OutXMin = State->XMin;
		OutXMax = State->XMax;
		OutYMin = State->YMin;
		OutYMax = State->YMax;
		return;
	}
	OutXMin = 0.0;
	OutXMax = 1.0;
	OutYMin = 0.0;
	OutYMax = 1.0;
}

bool UImPlotVisualizationSubsystem::HasAxisSettingsForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bHasAxisSettings : false;
}

const TArray<FVector2D>& UImPlotVisualizationSubsystem::GetPlotPointsForChart(const FName& ChartId) const
{
	static const TArray<FVector2D> EmptyPoints;
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->PlotPoints : EmptyPoints;
}

bool UImPlotVisualizationSubsystem::HasLiveSampleForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bHasLiveSample : false;
}

void UImPlotVisualizationSubsystem::GetLiveSampleForChart(const FName& ChartId, double& OutTimeSeconds, double& OutCount) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	if (State)
	{
		OutTimeSeconds = State->LiveTimeSeconds;
		OutCount = State->LiveCount;
		return;
	}
	OutTimeSeconds = 0.0;
	OutCount = 0.0;
}

bool UImPlotVisualizationSubsystem::HasLiveSampleThicknessForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bHasLiveSampleThickness : false;
}

double UImPlotVisualizationSubsystem::GetLiveSampleThicknessForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->LiveSampleThickness : 0.0;
}

UImPlotVisualizationSubsystem::FImPlotOverlayState& UImPlotVisualizationSubsystem::GetOrCreateOverlayState(const FName& ChartId)
{
	return OverlayStates.FindOrAdd(ChartId);
}

UImPlotVisualizationSubsystem::FImPlotOverlayState* UImPlotVisualizationSubsystem::FindOverlayState(const FName& ChartId)
{
	return OverlayStates.Find(ChartId);
}

const UImPlotVisualizationSubsystem::FImPlotOverlayState* UImPlotVisualizationSubsystem::FindOverlayState(const FName& ChartId) const
{
	return OverlayStates.Find(ChartId);
}

void UImPlotVisualizationSubsystem::EnsureOverlayWidget(FImPlotOverlayState& State, const FName& ChartId)
{
	if (!State.OverlayWidget.IsValid())
	{
		State.OverlayWidget = SNew(SImPlotOverlay)
			.Subsystem(this)
			.ChartId(ChartId);
	}
}

void UImPlotVisualizationSubsystem::OpenOverlayWindow(FImPlotOverlayState& State, const FName& ChartId)
{
	if (!State.OverlayWidget.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	if (!State.OverlayWindow.IsValid())
	{
		const FText WindowTitle = FText::FromString(TEXT("UE Plot Overlay"));

		SAssignNew(State.OverlayWindow, SMoveableWindow)
			.Title(WindowTitle)
			.SizingRule(ESizingRule::UserSized)
			//.SizingRule(ESizingRule::FixedSize)
			.FocusWhenFirstShown(false)
			.ActivationPolicy(EWindowActivationPolicy::Never)
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.IsTopmostWindow(false)
			.CreateTitleBar(true)
			.HasCloseButton(true)
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.UseOSWindowBorder(false)
			.ClientSize(FVector2D(640.0f, 420.0f))
			.WindowPanelContent(State.OverlayWidget);

		// Don't Set Content on windows as this sets the content for titlebar, and the window area
		//State.OverlayWindow->SetContent(State.OverlayWidget.ToSharedRef());
		State.OverlayWindow->SetOnWindowClosed(FOnWindowClosed::CreateUObject(this, &UImPlotVisualizationSubsystem::HandleWindowClosed, ChartId));

		FSlateApplication::Get().AddWindow(State.OverlayWindow.ToSharedRef());
		RegisterMoveableWindowActivity(State);
	}
	else
	{
		State.OverlayWindow->BringToFront(true);
	}
}

void UImPlotVisualizationSubsystem::CloseOverlayWindow(FImPlotOverlayState& State)
{
        UnregisterMoveableWindowActivity(State);
        State.bWindowOpen = false;
        if (!State.OverlayWindow.IsValid() || !FSlateApplication::IsInitialized())
        {
                State.OverlayWindow.Reset();
                DestroyOverlayContext(State);
                return;
        }
        FSlateApplication::Get().RequestDestroyWindow(State.OverlayWindow.ToSharedRef());
        State.OverlayWindow.Reset();
        DestroyOverlayContext(State);
}

void UImPlotVisualizationSubsystem::HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow, FName ChartId)
{
        FImPlotOverlayState* State = FindOverlayState(ChartId);
        if (!State)
        {
                return;
        }

        if (State->OverlayWindow == ClosedWindow)
        {
                State->OverlayWindow.Reset();
                State->bOverlayVisible = false;
                State->bWindowOpen = false;
                UnregisterMoveableWindowActivity(*State);
                DestroyOverlayContext(*State);
        }
}

void UImPlotVisualizationSubsystem::InvalidateOverlay(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	if (State && State->OverlayWidget.IsValid())
	{
		State->OverlayWidget->Invalidate(EInvalidateWidget::Paint);
	}
}

void UImPlotVisualizationSubsystem::RegisterMoveableWindowActivity(FImPlotOverlayState& State)
{
	if (State.bMoveableWindowActivityRegistered)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UMobiusWidgetSubsystem* WidgetSubsystem = World->GetSubsystem<UMobiusWidgetSubsystem>())
		{
			State.MoveableWindowSubsystem = WidgetSubsystem;
			WidgetSubsystem->RegisterMoveableWindowActivity();
			State.bMoveableWindowActivityRegistered = true;
		}
	}
}

void UImPlotVisualizationSubsystem::UnregisterMoveableWindowActivity(FImPlotOverlayState& State)
{
	if (!State.bMoveableWindowActivityRegistered)
	{
		return;
	}

	if (State.MoveableWindowSubsystem.IsValid())
	{
		State.MoveableWindowSubsystem->UnregisterMoveableWindowActivity();
		State.MoveableWindowSubsystem.Reset();
	}
	else if (UWorld* World = GetWorld())
	{
		if (UMobiusWidgetSubsystem* WidgetSubsystem = World->GetSubsystem<UMobiusWidgetSubsystem>())
		{
			WidgetSubsystem->UnregisterMoveableWindowActivity();
		}
	}

	State.bMoveableWindowActivityRegistered = false;
}


int32 UImPlotVisualizationSubsystem::PaintOverlayForChart(const FName& ChartId, const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
        const FWidgetStyle& InWidgetStyle, bool bParentEnabled, const TSharedRef<const SWidget>& Widget,
        const FSimpleDelegate& OnRequestClose)
{
        FImPlotOverlayState* State = FindOverlayState(ChartId);
        if (!State || !State->bOverlayVisible)
        {
                return LayerId;
        }

        EnsureOverlayContext(*State);
        if (!State->ImGuiContext || !State->ImPlotContext)
        {
                return LayerId;
        }

        ImGui::SetCurrentContext(State->ImGuiContext);
        ImPlot::SetCurrentContext(State->ImPlotContext);
        EnsureSharedFontAtlas();

        ImGuiIO& IO = ImGui::GetIO();
        // Calculate display size - will be used for both IO.DisplaySize and ImGui window size
        FVector2f DisplaySize = FVector2f(AllottedGeometry.GetLocalSize());
        if (FSlateApplication::IsInitialized())
        {
                const FVector2f CursorPos = FVector2f(FSlateApplication::Get().GetCursorPos());
                const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Widget);
                if (Window.IsValid())
                {
                        const FSlateRect ClientRect = Window->GetClientRectInScreen();
                        const float DpiScale = Window->GetDPIScaleFactor();
                        const FVector2f ClientOrigin = FVector2f(ClientRect.Left, ClientRect.Top);
                        const FVector2f LocalCursorPos = (CursorPos - ClientOrigin) / DpiScale;
                        IO.MousePos = ImVec2(LocalCursorPos.X, LocalCursorPos.Y);
                        // Use DPI-scaled client size for display
                        DisplaySize = FVector2f(ClientRect.GetSize()) / DpiScale;
                }
                else
                {
                        const FVector2f LocalCursorPos = FVector2f(AllottedGeometry.AbsoluteToLocal(CursorPos));
                        IO.MousePos = ImVec2(LocalCursorPos.X, LocalCursorPos.Y);
                }
                IO.MouseDown[0] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
                IO.MouseDown[1] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
                IO.MouseDown[2] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::MiddleMouseButton);
        }
        // Use the same DisplaySize for both IO and ImGui window
        IO.DisplaySize = ImVec2(DisplaySize.X, DisplaySize.Y);
        IO.DeltaTime = FMath::Max(1.0e-6f, static_cast<float>(FApp::GetDeltaTime()));

        ImGui::NewFrame();

        // Use DisplaySize (same as IO.DisplaySize) for consistent sizing
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(DisplaySize.X, DisplaySize.Y), ImGuiCond_Always);
        bool bOpen = State->bWindowOpen;
        ImGui::Begin("UE Plot Overlay", &bOpen,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

        const FString TitleString = GetChartTitleForChart(ChartId).ToString();
        if (!TitleString.IsEmpty())
        {
                ImGui::TextUnformatted(TCHAR_TO_UTF8(*TitleString));
                ImGui::Spacing();
        }

        if (ImPlot::BeginPlot("##MobiusPlot", ImVec2(-1.0f, -1.0f)))
        {
                if (HasAxisSettingsForChart(ChartId))
                {
                        const FString XTitleString = GetXAxisTitleForChart(ChartId).ToString();
                        const FString YTitleString = GetYAxisTitleForChart(ChartId).ToString();
                        ImPlot::SetupAxes(
                                XTitleString.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*XTitleString),
                                YTitleString.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*YTitleString),
                                ImPlotAxisFlags_AutoFit,
                                ImPlotAxisFlags_AutoFit);

                        double OutXMin = 0.0;
                        double OutXMax = 0.0;
                        double OutYMin = 0.0;
                        double OutYMax = 0.0;
                        GetAxisLimitsForChart(ChartId, OutXMin, OutXMax, OutYMin, OutYMax);
                        ImPlot::SetupAxesLimits(OutXMin, OutXMax, OutYMin, OutYMax, ImPlotCond_Always);
                }

                const TArray<FVector2D>& Points = GetPlotPointsForChart(ChartId);
                if (Points.Num() > 0)
                {
                        TArray<double> XValues;
                        TArray<double> YValues;
                        XValues.SetNum(Points.Num());
                        YValues.SetNum(Points.Num());
                        for (int32 Index = 0; Index < Points.Num(); ++Index)
                        {
                                XValues[Index] = Points[Index].X;
                                YValues[Index] = Points[Index].Y;
                        }

                        ImPlot::PlotLine("Evacuated", XValues.GetData(), YValues.GetData(), XValues.Num());
                }

                double LiveX = 0.0;
                double LiveY = 0.0;
                const bool bHasLiveSample = HasLiveSampleForChart(ChartId);
                if (bHasLiveSample)
                {
                        GetLiveSampleForChart(ChartId, LiveX, LiveY);
                        ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), 2.0f);
                        ImPlot::PlotInfLines("Live##CurrentTime", &LiveX, 1);
                }

                if (ImPlot::IsPlotHovered())
                {
                        const ImPlotPoint MousePos = ImPlot::GetPlotMousePos();
                        FVector2D NearestPoint = FVector2D::ZeroVector;
                        const bool bHasNearest = TryGetNearestPointForChart(ChartId, MousePos.x, NearestPoint);
                        double TotalAgents = 0.0;
                        if (HasAxisSettingsForChart(ChartId))
                        {
                                double XMin = 0.0;
                                double XMax = 0.0;
                                double YMin = 0.0;
                                double YMax = 0.0;
                                GetAxisLimitsForChart(ChartId, XMin, XMax, YMin, YMax);
                                TotalAgents = YMax;
                        }
                        const double EvacuatedValue = bHasNearest
                                ? NearestPoint.Y
                                : (bHasLiveSample && TotalAgents > 0.0 ? TotalAgents - LiveY : 0.0);
                        const double RemainingValue = TotalAgents > 0.0
                                ? FMath::Max(0.0, TotalAgents - EvacuatedValue)
                                : 0.0;
                        ImGui::BeginTooltip();
                        ImGui::Text("Time: %.2f", MousePos.x);
                        if (bHasNearest || bHasLiveSample)
                        {
                                ImGui::Text("Evacuated: %.0f", EvacuatedValue);
                                if (TotalAgents > 0.0)
                                {
                                        ImGui::Text("Remaining: %.0f", RemainingValue);
                                }
                        }
                        ImGui::EndTooltip();
                }

                ImPlot::EndPlot();
        }

        ImGui::End();
        if (State->bWindowOpen && !bOpen)
        {
                State->bWindowOpen = false;
                if (OnRequestClose.IsBound())
                {
                        OnRequestClose.Execute();
                }
        }
        ImGui::Render();

        const ImDrawData* DrawData = ImGui::GetDrawData();
        const FVector2f WindowOffset = FVector2f(AllottedGeometry.GetAccumulatedLayoutTransform().GetTranslation());

        // Get DPI scale for rendering - needed to scale ImGui output to match Slate's render surface
        float RenderDpiScale = 1.0f;
        if (FSlateApplication::IsInitialized())
        {
                const TSharedPtr<SWindow> RenderWindow = FSlateApplication::Get().FindWidgetWindow(Widget);
                if (RenderWindow.IsValid())
                {
                        RenderDpiScale = RenderWindow->GetDPIScaleFactor();
                }
        }

        RenderDrawData(DrawData, WindowOffset, RenderDpiScale, OutDrawElements, LayerId + 1);

        return LayerId + 1;
}

void UImPlotVisualizationSubsystem::EnsureOverlayContext(FImPlotOverlayState& State)
{
        if (State.ImGuiContext && State.ImPlotContext)
        {
                return;
        }
        if (!SharedFontAtlas)
        {
                SharedFontAtlas = IM_NEW(ImFontAtlas)();
        }

        State.ImGuiContext = ImGui::CreateContext(SharedFontAtlas);
        State.ImPlotContext = ImPlot::CreateContext();
        ImGui::SetCurrentContext(State.ImGuiContext);
        ImPlot::SetCurrentContext(State.ImPlotContext);

        ImGuiIO& IO = ImGui::GetIO();
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        EnsureSharedFontAtlas();
}

void UImPlotVisualizationSubsystem::DestroyOverlayContext(FImPlotOverlayState& State)
{
        if (State.ImGuiContext)
        {
                const bool bReleaseSharedAtlas = SharedFontAtlas && SharedFontAtlas->RefCount == 1;
                ImGui::SetCurrentContext(State.ImGuiContext);
                if (State.ImPlotContext)
                {
                        ImPlot::DestroyContext(State.ImPlotContext);
                        State.ImPlotContext = nullptr;
                }
                ImGui::DestroyContext(State.ImGuiContext);
                State.ImGuiContext = nullptr;
                if (bReleaseSharedAtlas)
                {
                        SharedFontAtlas = nullptr;
                        SharedFontBrush.Reset();
                        SharedFontTextureId = 0;
                        SharedFontTextureName = NAME_None;
                }
        }
        State.bWindowOpen = true;
}

void UImPlotVisualizationSubsystem::EnsureSharedFontAtlas()
{
        if (SharedFontBrush.IsValid() || !FSlateApplication::IsInitialized())
        {
                return;
        }
        if (!SharedFontAtlas)
        {
                SharedFontAtlas = IM_NEW(ImFontAtlas)();
        }

        unsigned char* Pixels = nullptr;
        int32 Width = 0;
        int32 Height = 0;
        SharedFontAtlas->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

        if (!Pixels || Width <= 0 || Height <= 0)
        {
                return;
        }

        TArray<uint8> ImageData;
        ImageData.Append(Pixels, Width * Height * 4);

        if (SharedFontTextureName.IsNone())
        {
                SharedFontTextureName = FName(*FString::Printf(TEXT("ImGuiFontAtlas_Shared_%p"), this));
        }

        SharedFontBrush = FSlateDynamicImageBrush::CreateWithImageData(SharedFontTextureName, FVector2D(Width, Height), ImageData);
        if (SharedFontBrush.IsValid())
        {
                SharedFontTextureId = static_cast<uint64>(reinterpret_cast<uintptr_t>(SharedFontBrush.Get()));
                SharedFontAtlas->SetTexID(static_cast<ImTextureID>(SharedFontTextureId));
        }
}

bool UImPlotVisualizationSubsystem::TryGetNearestPointForChart(const FName& ChartId, double TimeSeconds, FVector2D& OutPoint) const
{
        const TArray<FVector2D>& Points = GetPlotPointsForChart(ChartId);
        if (Points.Num() == 0)
        {
                return false;
        }

        double BestDistance = TNumericLimits<double>::Max();
        for (const FVector2D& Point : Points)
        {
                const double Distance = FMath::Abs(Point.X - TimeSeconds);
                if (Distance < BestDistance)
                {
                        BestDistance = Distance;
                        OutPoint = Point;
                }
        }

        return true;
}

void UImPlotVisualizationSubsystem::RenderDrawData(const ImDrawData* DrawData, const FVector2f& WindowOffset,
        float DpiScale, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
        if (!DrawData || !SharedFontBrush.IsValid())
        {
                return;
        }

        if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
        {
                return;
        }

        const FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*SharedFontBrush);
        const FSlateRenderTransform RenderTransform;

        for (int32 ListIndex = 0; ListIndex < DrawData->CmdListsCount; ++ListIndex)
        {
                const ImDrawList* DrawList = DrawData->CmdLists[ListIndex];
                const ImDrawVert* Vertices = DrawList->VtxBuffer.Data;
                const ImDrawIdx* Indices = DrawList->IdxBuffer.Data;

                for (int32 CmdIndex = 0; CmdIndex < DrawList->CmdBuffer.Size; ++CmdIndex)
                {
                        const ImDrawCmd& DrawCmd = DrawList->CmdBuffer[CmdIndex];
                        if (DrawCmd.UserCallback != nullptr || DrawCmd.ElemCount == 0)
                        {
                                continue;
                        }

                        const ImTextureID CmdTexId = DrawCmd.GetTexID();
                        if (CmdTexId != static_cast<ImTextureID>(SharedFontTextureId) && CmdTexId != ImTextureID_Invalid)
                        {
                                continue;
                        }

                        TArray<FSlateVertex> CmdVertices;
                        TArray<SlateIndex> CmdIndices;
                        CmdVertices.Reserve(DrawCmd.ElemCount);
                        CmdIndices.Reserve(DrawCmd.ElemCount);

                        for (uint32 ElemIndex = 0; ElemIndex < DrawCmd.ElemCount; ++ElemIndex)
                        {
                                const ImDrawIdx DrawIndex = Indices[DrawCmd.IdxOffset + ElemIndex] + static_cast<ImDrawIdx>(DrawCmd.VtxOffset);
                                const ImDrawVert& Vertex = Vertices[DrawIndex];

                                const uint8 R = (Vertex.col >> IM_COL32_R_SHIFT) & 0xFF;
                                const uint8 G = (Vertex.col >> IM_COL32_G_SHIFT) & 0xFF;
                                const uint8 B = (Vertex.col >> IM_COL32_B_SHIFT) & 0xFF;
                                const uint8 A = (Vertex.col >> IM_COL32_A_SHIFT) & 0xFF;
                                const FColor Color(R, G, B, A);

                                // Scale ImGui vertices by DPI to match Slate's physical render surface
                                const FVector2f Position = FVector2f(Vertex.pos.x * DpiScale, Vertex.pos.y * DpiScale) + WindowOffset;
                                const FVector2f UV(Vertex.uv.x, Vertex.uv.y);

                                CmdVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Position, UV, Color));
                                CmdIndices.Add(static_cast<SlateIndex>(ElemIndex));
                        }

                        // Scale clip rect by DPI to match scaled vertices
                        const FSlateRect ClipRect(
                                DrawCmd.ClipRect.x * DpiScale + WindowOffset.X,
                                DrawCmd.ClipRect.y * DpiScale + WindowOffset.Y,
                                DrawCmd.ClipRect.z * DpiScale + WindowOffset.X,
                                DrawCmd.ClipRect.w * DpiScale + WindowOffset.Y);

                        OutDrawElements.PushClip(FSlateClippingZone(ClipRect));
                        FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ResourceHandle, CmdVertices, CmdIndices, nullptr, 0, 0);
                        OutDrawElements.PopClip();
                }
        }
}





