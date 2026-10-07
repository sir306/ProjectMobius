/**
 * MIT License
 * Copyright (c) 2026 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * Console equivalents of the UI-only actions, so a packaged build can be smoke-tested end to end from
 * -ExecCmds with nobody at the mouse: the screenshot button, the per-floor chart toggles, "Copy chart",
 * heatmap PNG export and camera save points. Each calls the same function the button does.
 *
 * -ExecCmds runs every command at startup, before any file has loaded, so Mobius.Test.Delay defers one:
 *   -ExecCmds="Mobius.Test.Delay 30 Mobius.Chart.Toggle all, Mobius.Test.Delay 35 Mobius.Chart.CopyImage all"
 *
 * Not compiled into Shipping.
 */

#if !UE_BUILD_SHIPPING

#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "Controller/MobiusController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "UI/Components/FloorStatsWidget.h"
#include "UObject/UObjectIterator.h"

namespace MobiusUiCommands
{
	AMobiusController* FindController(UWorld* World)
	{
		return World ? Cast<AMobiusController>(World->GetFirstPlayerController()) : nullptr;
	}

	void ExecDelay(const TArray<FString>& Args, UWorld* World)
	{
		double Seconds = 0.0;
		if (Args.Num() < 2 || !LexTryParseString(Seconds, *Args[0]))
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: Mobius.Test.Delay <seconds> <command...>"));
			return;
		}

		FString Command = Args[1];
		for (int32 Index = 2; Index < Args.Num(); ++Index)
		{
			Command += TEXT(" ") + Args[Index];
		}
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakWorld, Command](float)
			{
				UE_LOG(LogTemp, Display, TEXT("Mobius.Test.Delay: running '%s'"), *Command);
				if (GEngine)
				{
					GEngine->Exec(WeakWorld.Get(), *Command);
				}
				return false; // one shot
			}), static_cast<float>(Seconds));
	}

	void ExecScreenshot(const TArray<FString>& Args, UWorld* World)
	{
		if (AMobiusController* Controller = FindController(World))
		{
			Controller->TakeScreenshot();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Mobius.Screenshot: no AMobiusController in this world."));
		}
	}

	void ExecChartList(const TArray<FString>& Args, UWorld* World)
	{
		for (TObjectIterator<UFloorStatsWidget> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				UE_LOG(LogTemp, Display, TEXT("Mobius.Chart.List: floor widget %d"), It->FloorNumber);
			}
		}

		const UImPlotVisualizationSubsystem* Charts = World ? World->GetSubsystem<UImPlotVisualizationSubsystem>() : nullptr;
		if (!Charts)
		{
			return;
		}
		for (const FName& Id : Charts->GetChartIds())
		{
			UE_LOG(LogTemp, Display, TEXT("Mobius.Chart.List: chart %s open=%d points=%d title='%s'"),
				*Id.ToString(), Charts->IsOverlayVisibleForChart(Id) ? 1 : 0,
				Charts->GetPlotPointsForChart(Id).Num(), *Charts->GetChartTitleForChart(Id).ToString());
		}
	}

	void ExecChartToggle(const TArray<FString>& Args, UWorld* World)
	{
		const bool bAll = Args.Num() == 0 || Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);
		const int32 Floor = bAll ? INDEX_NONE : FCString::Atoi(*Args[0]);

		int32 Toggled = 0;
		for (TObjectIterator<UFloorStatsWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && (bAll || It->FloorNumber == Floor))
			{
				It->ToggleImPlotOverlay();
				++Toggled;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Mobius.Chart.Toggle: toggled %d floor chart(s)."), Toggled);
	}

	void ExecChartCopyImage(const TArray<FString>& Args, UWorld* World)
	{
		UImPlotVisualizationSubsystem* Charts = World ? World->GetSubsystem<UImPlotVisualizationSubsystem>() : nullptr;
		if (!Charts)
		{
			return;
		}

		const bool bAll = Args.Num() == 0 || Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);
		int32 Requested = 0;
		for (const FName& Id : Charts->GetChartIds())
		{
			if ((bAll || Id.ToString().Equals(Args[0], ESearchCase::IgnoreCase)) && Charts->CopyChartImageNow(Id))
			{
				++Requested;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Mobius.Chart.CopyImage: copied %d open chart(s)."), Requested);
	}

	void ExecHeatmapSavePng(const TArray<FString>& Args, UWorld* World)
	{
		int32 Saved = 0;
		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			It->SaveHeatmapToPNG();
			++Saved;
		}
		UE_LOG(LogTemp, Display, TEXT("Mobius.Heatmap.SavePNG: exported %d heatmap(s) under %s"), Saved,
			*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Heatmap")));
	}

	void ExecCameraSavePoint(const TArray<FString>& Args, UWorld* World)
	{
		AMobiusController* Controller = FindController(World);
		if (!Controller || !Controller->PlayerCameraManager)
		{
			UE_LOG(LogTemp, Warning, TEXT("Mobius.Camera.SavePoint: no AMobiusController camera in this world."));
			return;
		}
		Controller->SaveCameraSavePoint(FTransform(
			Controller->PlayerCameraManager->GetCameraRotation(), Controller->PlayerCameraManager->GetCameraLocation()));
		UE_LOG(LogTemp, Display, TEXT("Mobius.Camera.SavePoint: saved."));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMobiusTestDelayCommand(
	TEXT("Mobius.Test.Delay"),
	TEXT("Run a console command after a delay, so -ExecCmds can script steps that need a loaded file.\n")
	TEXT("Usage: Mobius.Test.Delay <seconds> <command...>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecDelay));

static FAutoConsoleCommandWithWorldAndArgs GMobiusScreenshotCommand(
	TEXT("Mobius.Screenshot"),
	TEXT("Take the same screenshot as the UI's screenshot button (MobiusCaptures beside the pedestrian file)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecScreenshot));

static FAutoConsoleCommandWithWorldAndArgs GMobiusChartListCommand(
	TEXT("Mobius.Chart.List"),
	TEXT("Log the floor chart widgets and every chart's open state, point count and title."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecChartList));

static FAutoConsoleCommandWithWorldAndArgs GMobiusChartToggleCommand(
	TEXT("Mobius.Chart.Toggle"),
	TEXT("Toggle a floor's data chart, as its chart button does.\n")
	TEXT("Usage: Mobius.Chart.Toggle <floor number | all>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecChartToggle));

static FAutoConsoleCommandWithWorldAndArgs GMobiusChartCopyImageCommand(
	TEXT("Mobius.Chart.CopyImage"),
	TEXT("Copy an open chart to the clipboard as an image, as its \"Copy chart\" button does.\n")
	TEXT("Usage: Mobius.Chart.CopyImage <chart id | all>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecChartCopyImage));

static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapSavePngCommand(
	TEXT("Mobius.Heatmap.SavePNG"),
	TEXT("Export every heatmap in the world to Saved/Heatmap, as the heatmap save button does."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecHeatmapSavePng));

static FAutoConsoleCommandWithWorldAndArgs GMobiusCameraSavePointCommand(
	TEXT("Mobius.Camera.SavePoint"),
	TEXT("Save the current camera as a camera save point (MobiusCaptures/ beside the pedestrian file)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MobiusUiCommands::ExecCameraSavePoint));

#endif // !UE_BUILD_SHIPPING
