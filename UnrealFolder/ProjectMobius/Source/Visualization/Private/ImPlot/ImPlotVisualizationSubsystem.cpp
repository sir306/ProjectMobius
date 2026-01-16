/**
 * ImPlot visualization subsystem implementation.
 */
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "ImPlot/SImPlotOverlay.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Core/MobiusWidgetSubsystem.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

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
	if (!State.OverlayWindow.IsValid() || !FSlateApplication::IsInitialized())
	{
		State.OverlayWindow.Reset();
		return;
	}
	FSlateApplication::Get().RequestDestroyWindow(State.OverlayWindow.ToSharedRef());
	State.OverlayWindow.Reset();
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
		UnregisterMoveableWindowActivity(*State);
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
