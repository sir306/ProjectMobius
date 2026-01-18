/**
 * ImPlot data subsystem implementation.
 */
#include "ImPlot/ImPlotDataSubsystem.h"
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "Engine/World.h"

namespace
{
	const FName DefaultChartId = NAME_None;
}

void UImPlotDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UWorld* World = GetWorld())
	{
		ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
	}
}

void UImPlotDataSubsystem::Deinitialize()
{
	ImPlotSubsystem = nullptr;
	Super::Deinitialize();
}

void UImPlotDataSubsystem::SetChartTitle(const FText& InTitle)
{
	SetChartTitleForChart(DefaultChartId, InTitle);
}

void UImPlotDataSubsystem::SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	SetAxisSettingsForChart(DefaultChartId, InXTitle, InYTitle, InXMin, InXMax, InYMin, InYMax);
}

void UImPlotDataSubsystem::SetPlotPoints(const TArray<FVector2D>& InPoints)
{
	SetPlotPointsForChart(DefaultChartId, InPoints);
}

void UImPlotDataSubsystem::UpdateLiveSample(double InTimeSeconds, double InCount)
{
	UpdateLiveSampleForChart(DefaultChartId, InTimeSeconds, InCount);
}

void UImPlotDataSubsystem::ToggleOverlay()
{
	ToggleOverlayForChart(DefaultChartId);
}

void UImPlotDataSubsystem::SetChartTitleForChart(const FName& ChartId, const FText& InTitle)
{
	if (!ImPlotSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
		}
	}
	if (ImPlotSubsystem)
	{
		ImPlotSubsystem->SetChartTitleForChart(ChartId, InTitle);
	}
}

void UImPlotDataSubsystem::SetAxisSettingsForChart(const FName& ChartId, const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	if (!ImPlotSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
		}
	}
	if (ImPlotSubsystem)
	{
		ImPlotSubsystem->SetAxisSettingsForChart(ChartId, InXTitle, InYTitle, InXMin, InXMax, InYMin, InYMax);
	}
}

void UImPlotDataSubsystem::SetPlotPointsForChart(const FName& ChartId, const TArray<FVector2D>& InPoints)
{
	if (!ImPlotSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
		}
	}
	if (ImPlotSubsystem)
	{
		ImPlotSubsystem->SetPlotPointsForChart(ChartId, InPoints);
	}
}

void UImPlotDataSubsystem::UpdateLiveSampleForChart(const FName& ChartId, double InTimeSeconds, double InCount)
{
	if (!ImPlotSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
		}
	}
	if (ImPlotSubsystem)
	{
		ImPlotSubsystem->UpdateLiveSampleForChart(ChartId, InTimeSeconds, InCount);
	}
}

void UImPlotDataSubsystem::ToggleOverlayForChart(const FName& ChartId)
{
	if (!ImPlotSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
		}
	}
	if (ImPlotSubsystem)
	{
		ImPlotSubsystem->ToggleOverlayForChart(ChartId);
	}
}
