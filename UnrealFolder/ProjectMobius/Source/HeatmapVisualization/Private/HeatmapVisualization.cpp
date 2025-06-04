#include "HeatmapVisualization.h"

DEFINE_LOG_CATEGORY(HeatmapVisualization);

#define LOCTEXT_NAMESPACE "FHeatmapVisualization"

void FHeatmapVisualization::StartupModule()
{
	UE_LOG(HeatmapVisualization, Warning, TEXT("HeatmapVisualization module has been loaded"));
}

void FHeatmapVisualization::ShutdownModule()
{
	UE_LOG(HeatmapVisualization, Warning, TEXT("HeatmapVisualization module has been unloaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHeatmapVisualization, HeatmapVisualization)