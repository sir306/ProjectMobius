// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
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

#include "UI/Components/FloorStatsWidget.h"

#include "Components/TextBlock.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "ImPlot/ImPlotDataSubsystem.h"

namespace
{
	bool IsLocationOnFloor(const AHeatmapPixelTextureVisualizer* Heatmap, const FVector& Location)
	{
		return Heatmap && Heatmap->CheckHeatmapAndLocationValid(Location);
	}

	bool IsLocationBetweenFloors(const AHeatmapPixelTextureVisualizer* BottomHeatmap, const AHeatmapPixelTextureVisualizer* TopHeatmap, const FVector& Location)
	{
		if (!BottomHeatmap || !TopHeatmap)
		{
			return false;
		}
		if (BottomHeatmap->CheckHeatmapAndLocationValid(Location))
		{
			return false;
		}
		return Location.Z > BottomHeatmap->MeshOriginLocation.Z + BottomHeatmap->MaxAddHeight
			&& TopHeatmap->MeshOriginLocation.Z > Location.Z;
	}
}

void UFloorStatsWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	BuildFloorText();
	if (FloorTextBlock && !FloorPrefixText.IsEmpty())
	{
		FloorTextBlock->SetText(FormatTextForTextBlock(FloorPrefixText, CurrentLiveAgentCount));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("The Current Floor Button is invalid"));
	}
}

void UFloorStatsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (GetWorld())
	{
		// Get the heatmap subsystem
		if (UHeatmapSubsystem* HeatmapSubsystem = GetWorld()->GetSubsystem<UHeatmapSubsystem>())
		{
			// bind the correct delegate to the correct method
			if (bIsBetweenFloorWidget)
			{
				// ensure that it is not already bound to the delegate
				HeatmapSubsystem->OnUpdateBetweenFloorStatCount.RemoveDynamic(this, &UFloorStatsWidget::UpdateFloorLiveStatCount);
				// Bind the delegate to the method
				HeatmapSubsystem->OnUpdateBetweenFloorStatCount.AddDynamic(this, &UFloorStatsWidget::UpdateFloorLiveStatCount);
			}
			else
			{
				// ensure that it is not already bound to the delegate
				HeatmapSubsystem->OnUpdateFloorStatCount.RemoveDynamic(this, &UFloorStatsWidget::UpdateFloorLiveStatCount);
				// Bind the delegate to the method
				HeatmapSubsystem->OnUpdateFloorStatCount.AddDynamic(this, &UFloorStatsWidget::UpdateFloorLiveStatCount);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("The Mobius Widget Subsystem is invalid"));
		}

                // for the chart to show the current data for the overlay we can listen to the delegate
                // that broadcasts when file data changes and send the data to the overlay

		// bind the spawn subsystem -> when this delegate is called we know the data has been loaded and processed
		if (auto SpawnSystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>())
		{
			// ensure that it is not already bound to the delegate
                        SpawnSystem->OnPedestrianDataReadyToSpawn.RemoveDynamic(this, &UFloorStatsWidget::BuildDataForImPlotOverlay);
			
                        SpawnSystem->OnPedestrianDataReadyToSpawn.AddDynamic(this, &UFloorStatsWidget::BuildDataForImPlotOverlay);
		}
		
		
		TimeDilationSubSystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
		if (TimeDilationSubSystem == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("The TimeDilationSubSystem is invalid"));
		}
		else
		{
			// ensure that it is not already bound to the delegate
			TimeDilationSubSystem->OnNewCurrentTime.RemoveDynamic(this, &UFloorStatsWidget::UpdateCurrentPlaybackTime);
			
			// Bind the current time delegate
			TimeDilationSubSystem->OnNewCurrentTime.AddDynamic(this, &UFloorStatsWidget::UpdateCurrentPlaybackTime);
		}
		
		
                ImPlotDataSubsystem = GetWorld()->GetSubsystem<UImPlotDataSubsystem>();
                if (ImPlotDataSubsystem == nullptr)
                {
                        UE_LOG(LogTemp, Warning, TEXT("The ImPlot Data Subsystem is invalid"));
                }
        }

	BuildFloorText();
	if (FloorTextBlock && !FloorPrefixText.IsEmpty())
	{
		FloorTextBlock->SetText(FormatTextForTextBlock(FloorPrefixText, CurrentLiveAgentCount));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("The Current Floor Button is invalid"));
	}
}

void UFloorStatsWidget::NativeDestruct()
{
	Super::NativeDestruct();

	// Remove delegates
	if (TimeDilationSubSystem)
	{
		TimeDilationSubSystem->OnNewCurrentTime.RemoveDynamic(this, &UFloorStatsWidget::UpdateCurrentPlaybackTime);
	}
	if (auto SpawnSystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>())
	{
		// ensure that it is not already bound to the delegate
                SpawnSystem->OnPedestrianDataReadyToSpawn.RemoveDynamic(this, &UFloorStatsWidget::BuildDataForImPlotOverlay);
	}
	// Get the heatmap subsystem
	if (UHeatmapSubsystem* HeatmapSubsystem = GetWorld()->GetSubsystem<UHeatmapSubsystem>())
	{
		// bind the correct delegate to the correct method
		if (bIsBetweenFloorWidget)
		{
			// ensure that it is not already bound to the delegate
			HeatmapSubsystem->OnUpdateBetweenFloorStatCount.RemoveDynamic(this, &UFloorStatsWidget::UpdateFloorLiveStatCount);
		}
		else
		{
			// ensure that it is not already bound to the delegate
			HeatmapSubsystem->OnUpdateFloorStatCount.RemoveDynamic(this, &UFloorStatsWidget::UpdateFloorLiveStatCount);
		}
	}
}

void UFloorStatsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UFloorStatsWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	
	BuildFloorText();
	if (FloorTextBlock && !FloorPrefixText.IsEmpty())
	{
		FloorTextBlock->SetText(FormatTextForTextBlock(FloorPrefixText, CurrentLiveAgentCount));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("The Current Floor Button is invalid"));
	}
}

void UFloorStatsWidget::UpdateFloorLiveStatCount(int32 InFloorNumber, int32 AgentCount)
{
	if (InFloorNumber == FloorNumber && CurrentLiveAgentCount != AgentCount)
	{
		CurrentLiveAgentCount = AgentCount;

		if (FloorTextBlock && !FloorPrefixText.IsEmpty())
		{
			FloorTextBlock->SetText(FormatTextForTextBlock(FloorPrefixText, CurrentLiveAgentCount));
		}
                // we have to send a time that is float and divided by 10 to match the logic for the method
                float NewTime = LastSentTimeInt / 10.0f;

                // we can do this as it ensures that the agent count is synced with the overlay
                UpdateCurrentPlaybackTime(NewTime);
		// maybe bring a live graph ui switch
		// // this will change to when we doing all floors etc
		// if (FloorNumber == -1 && WsSubsystem != nullptr && TimeDilationSubSystem != nullptr)
		// {
		// 	float CurrentSimTime = TimeDilationSubSystem->GetCurrentSimTime();
		// 	WsSubsystem->SendAgentDataCount(CurrentSimTime, CurrentLiveAgentCount);
		// }
	}
}

void UFloorStatsWidget::BuildFloorText()
{
        if (FloorNumber == -1)
        {
                FloorPrefixText = FText::FromString("Total Occupants: ");
                ImPlotChartId = NAME_None;
                return;
        }

        if (bIsBetweenFloorWidget)
        {
                const FText BottomFloorText = FText::AsNumber(FloorNumber);
                const FText TopFloorText = FText::AsNumber(FloorNumber + 1);
                FloorPrefixText = FText::Format(FText::FromString("Between Floors {0} & {1}: "), BottomFloorText, TopFloorText);
                ImPlotChartId = FName(*FString::Printf(TEXT("ImPlot_Between_%d_%d"), FloorNumber, FloorNumber + 1));
                return;
        }

        FloorPrefixText = FText::Format(FText::FromString("Floor {0}: "), FText::AsNumber(FloorNumber));
        ImPlotChartId = FName(*FString::Printf(TEXT("ImPlot_Floor_%d"), FloorNumber));
}

void UFloorStatsWidget::BuildImPlotChartTitle() const
{
        if (!ImPlotDataSubsystem)
        {
                return;
        }

        if (FloorNumber == -1)
        {
                ImPlotDataSubsystem->SetChartTitleForChart(
                        ImPlotChartId,
                        FText::FromString("Total Number of People Over Time"));
                return;
        }

        if (bIsBetweenFloorWidget)
        {
                const FText BottomFloorText = FText::AsNumber(FloorNumber);
                const FText TopFloorText = FText::AsNumber(FloorNumber + 1);
                const FText Title = FText::Format(
                        FText::FromString("Between Floors {0} & {1} Occupants Over Time"),
                        BottomFloorText,
                        TopFloorText);
                ImPlotDataSubsystem->SetChartTitleForChart(ImPlotChartId, Title);
                return;
        }

        const FText Title = FText::Format(
                FText::FromString("Floor {0} Occupants Over Time"),
                FText::AsNumber(FloorNumber));
        ImPlotDataSubsystem->SetChartTitleForChart(ImPlotChartId, Title);
}

void UFloorStatsWidget::BuildImPlotAxisSetting()
{
        if (TimeDilationSubSystem == nullptr)
        {
                return;
        }

        // work out max time
        float MaxTime = TimeDilationSubSystem->TotalTime;

        int32 MaxAgentCount = MaxAgentCountToSend;
        if (MaxAgentCount <= 0 && FloorNumber == -1)
        {
                if (auto AgentDataSubSystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>())
                {
                        MaxAgentCount = AgentDataSubSystem->GetMaxAgents();
                }
        }
        // Y-axis always starts at 0 — evacuation count goes from 0 upward
        if (MaxAgentCount <= 0)
        {
                MaxAgentCount = 1;
        }
        if (MaxTime == 0.0f)
        {
                // if max time is 0 then set it to 1
                MaxTime = 1.0f;
        }

        if (ImPlotDataSubsystem)
        {
                ImPlotDataSubsystem->SetAxisSettingsForChart(
                        ImPlotChartId,
                        FText::FromString("Elapsed Time (s)"),
                        FText::FromString("Number of Occupants Evacuated"),
                        0.0,
                        MaxTime,
                        0.0,
                        MaxAgentCount);
        }
}

void UFloorStatsWidget::BuildImPlotGraphData() const
{
        if (ImPlotDataSubsystem)
        {
                ImPlotDataSubsystem->SetPlotPointsForChart(ImPlotChartId, ImPlotPoints);
        }
}

void UFloorStatsWidget::SendImPlotChartData()
{
        if (TimeDilationSubSystem == nullptr || ImPlotDataSubsystem == nullptr)
        {
                return;
        }

        BuildImPlotChartTitle();
        BuildImPlotAxisSetting();

        if (TimeDilationSubSystem->GetCurrentSimTime() == 0.0f && CurrentLiveAgentCount == 0)
        {
                UpdateCurrentPlaybackTime(0.0f);
        }
        else
        {
                // update live data
                UpdateAgentLiveData(); // may need to move checks into this method
        }

        BuildImPlotGraphData();
}

void UFloorStatsWidget::ToggleImPlotOverlay()
{
        if (ImPlotDataSubsystem == nullptr)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotDataSubsystem = World->GetSubsystem<UImPlotDataSubsystem>();
                }
        }

        if (ImPlotDataSubsystem)
        {
                ImPlotDataSubsystem->ToggleOverlayForChart(ImPlotChartId);
        }
        else
        {
                UE_LOG(LogTemp, Warning, TEXT("ToggleImPlotOverlay failed: ImPlot Data Subsystem is invalid."));
                return;
        }

        // build the data for the instant UI
        BuildDataForImPlotOverlay();

        SendImPlotChartData();
}

void UFloorStatsWidget::BuildDataForImPlotOverlay()
{
        if (TimeDilationSubSystem == nullptr)
        {
                return;
        }

        ImPlotPoints.Reset();
        MinAgentCountToSend = 0;
        MaxAgentCountToSend = 0;

        if (FloorNumber == -1)
        {
                if (auto MES_Subsystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>())
                {
                        // if we have no data then smallest count is 0
                        if (MES_Subsystem->NumOfAgentsPerTimeStep.Num() == 0)
                        {
                                SendImPlotChartData();
                                return;
                        }

                        if (MES_Subsystem->AgentDataSubsystem)
                        {
                                MaxAgentCountToSend = MES_Subsystem->AgentDataSubsystem->GetMaxAgents();
                        }

                        // Fallback: if MaxAgents is 0 (e.g. incomplete data), derive from actual data
                        if (MaxAgentCountToSend == 0)
                        {
                                for (int32 i = 0; i < MES_Subsystem->NumOfAgentsPerTimeStep.Num(); i++)
                                {
                                        MaxAgentCountToSend = FMath::Max(MaxAgentCountToSend, MES_Subsystem->NumOfAgentsPerTimeStep[i]);
                                }
                        }

                        int32 SmallestFoundSampleCount = INT32_MAX;

                        // loop through samples
                        for (int32 i = 0; i < MES_Subsystem->NumOfAgentsPerTimeStep.Num(); i++)
                        {
                                const int32 RemainingCount = MES_Subsystem->NumOfAgentsPerTimeStep[i];

                                // New sample smaller than current smallest
                                if (RemainingCount < SmallestFoundSampleCount)
                                {
                                        SmallestFoundSampleCount = RemainingCount;
                                }

                                // minus the sample count from the max to get the number evacuated (clamp to 0)
                                int32 SampleCount = FMath::Max(0, MaxAgentCountToSend - RemainingCount);

                                // get the time frequency from time dilation subsystem (guard against zero)
                                float TimeBetweenSteps = FMath::Max(TimeDilationSubSystem->TimeBetweenSteps, KINDA_SMALL_NUMBER);

                                // time of current sample -> assumes no missing data
                                float CurrentTime = i * TimeBetweenSteps;

                                // build the points array
                                ImPlotPoints.Add(FVector2D(CurrentTime, SampleCount));
                        }
                        // Update the min agent count to send
                        MinAgentCountToSend = SmallestFoundSampleCount == INT32_MAX ? 0 : SmallestFoundSampleCount;

                        SendImPlotChartData();
                }
                else
                {
                        // log runnable null
                        UE_LOG(LogTemp, Warning, TEXT("The spawn system is null"));
                }

                return;
        }

        UMassEntitySpawnSubsystem* SpawnSubsystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
        if (SpawnSubsystem == nullptr)
        {
                UE_LOG(LogTemp, Warning, TEXT("The spawn system is null"));
                return;
        }

        const FSimulationFragment* SimulationFragment = SpawnSubsystem->GetSimulationFragment();
        if (SimulationFragment == nullptr)
        {
                UE_LOG(LogTemp, Warning, TEXT("Simulation fragment is null"));
                return;
        }

        UHeatmapSubsystem* HeatmapSubsystem = GetWorld()->GetSubsystem<UHeatmapSubsystem>();
        if (HeatmapSubsystem == nullptr)
        {
                UE_LOG(LogTemp, Warning, TEXT("The heatmap subsystem is null"));
                return;
        }

        AHeatmapPixelTextureVisualizer* BottomHeatmap = HeatmapSubsystem->GetHeatmapByIndex(FloorNumber);
        AHeatmapPixelTextureVisualizer* TopHeatmap = nullptr;
        if (bIsBetweenFloorWidget)
        {
                TopHeatmap = HeatmapSubsystem->GetHeatmapByIndex(FloorNumber + 1);
        }

        if (BottomHeatmap == nullptr || (bIsBetweenFloorWidget && TopHeatmap == nullptr))
        {
                UE_LOG(LogTemp, Warning, TEXT("Heatmap data is not ready for floor %d."), FloorNumber);
                return;
        }

        if (!SimulationFragment->SimulationData.IsValid())
        {
                SendImPlotChartData();
                return;
        }
        const int32 NumSteps = SimulationFragment->SimulationData->Num();
        if (NumSteps == 0)
        {
                SendImPlotChartData();
                return;
        }

        // Get sorted keys from the TMap to handle non-sequential timestep indices
        TArray<int32> SortedKeys;
        SimulationFragment->SimulationData->GetKeys(SortedKeys);
        SortedKeys.Sort();

        TArray<int32> SampleCounts;
        SampleCounts.SetNum(SortedKeys.Num());

        int32 SmallestFoundSampleCount = INT32_MAX;
        int32 LargestFoundSampleCount = 0;

        for (int32 i = 0; i < SortedKeys.Num(); ++i)
        {
                int32 StepCount = 0;
                if (const TArray<FSimMovementSample>* Samples = SimulationFragment->SimulationData->Find(SortedKeys[i]))
                {
                        for (const FSimMovementSample& Sample : *Samples)
                        {
                                if (bIsBetweenFloorWidget)
                                {
                                        if (IsLocationBetweenFloors(BottomHeatmap, TopHeatmap, Sample.Position))
                                        {
                                                ++StepCount;
                                        }
                                }
                                else if (IsLocationOnFloor(BottomHeatmap, Sample.Position))
                                {
                                        ++StepCount;
                                }
                        }
                }

                SampleCounts[i] = StepCount;
                SmallestFoundSampleCount = FMath::Min(SmallestFoundSampleCount, StepCount);
                LargestFoundSampleCount = FMath::Max(LargestFoundSampleCount, StepCount);
        }

        MinAgentCountToSend = SmallestFoundSampleCount == INT32_MAX ? 0 : SmallestFoundSampleCount;
        MaxAgentCountToSend = LargestFoundSampleCount;

        const float TimeBetweenSteps = FMath::Max(TimeDilationSubSystem->TimeBetweenSteps, KINDA_SMALL_NUMBER);
        ImPlotPoints.Reserve(SortedKeys.Num());
        for (int32 i = 0; i < SortedKeys.Num(); ++i)
        {
                float CurrentTime = SortedKeys[i] * TimeBetweenSteps;
                int32 SampleCount = FMath::Max(0, MaxAgentCountToSend - SampleCounts[i]);
                ImPlotPoints.Add(FVector2D(CurrentTime, SampleCount));
        }

        SendImPlotChartData();
}

void UFloorStatsWidget::UpdateCurrentPlaybackTime(float CurrentTime)
{
        if (TimeDilationSubSystem == nullptr)
        {
                return;
        }
	
        // TODO: currently we have a limitation for updates that are sent to the overlay,
	// We can only send updates when the time changes at 1dp (1 decimal place) to avoid sending too many updates
        // Otherwise the overlay updates too frequently and causes the UI to lag behind
	int32 CurrentTimeCents = FMath::FloorToInt(CurrentTime * 10.0f);

	// log the current timecents and last sent time cents
	//UE_LOG(LogTemp, Log, TEXT("Current Time Cents: %d, Last Sent Time Cents: %d"), CurrentTimeCents, LastSentTimeInt);

	// log the current live agent count and last sent count
	//UE_LOG(LogTemp, Log, TEXT("Current Live Agent Count: %d, Last Sent Count: %d"), CurrentLiveAgentCount, LastSentCount);


	// 1) Check 1 see if we are already checking the same data - Check 2 see if same time and count - check 3 see if time is the same
	if (bCheckingSameData || (CurrentTimeCents == LastSentTimeInt && LastSentCount == CurrentLiveAgentCount))
	{
		// nothing new to send
		return;
	}
	LastSentTimeInt = CurrentTimeCents;
	bCheckingSameData = true;

	LastSentCount = CurrentLiveAgentCount;
	
	// update live data
	UpdateAgentLiveData();

	bCheckingSameData = false;

	
}

void UFloorStatsWidget::UpdateAgentLiveData()
{
        if (ImPlotDataSubsystem)
        {
                ImPlotDataSubsystem->UpdateLiveSampleForChart(ImPlotChartId, LastSentTimeInt / 10.0, CurrentLiveAgentCount);
        }
}

void UFloorStatsWidget::BuildQtAppChartTitle() const
{
        BuildImPlotChartTitle();
}

void UFloorStatsWidget::BuildQtChartAxisSetting()
{
        BuildImPlotAxisSetting();
}

void UFloorStatsWidget::BuildQtChartGraphData() const
{
        BuildImPlotGraphData();
}

void UFloorStatsWidget::SendQtAppChartData()
{
        SendImPlotChartData();
}

void UFloorStatsWidget::LaunchCloseQtApp()
{
        ToggleImPlotOverlay();
}

void UFloorStatsWidget::BuildDataForInstantQtUI()
{
        BuildDataForImPlotOverlay();
}

FText UFloorStatsWidget::FormatTextForTextBlock(const FText& Prefix, int32 Count)
{
	FFormatNamedArguments Args;
	Args.Add("Pre", Prefix);
	Args.Add("Count", FText::AsNumber(Count));
	return FText::Format(FText::FromString("{Pre}{Count}"), Args);
}
