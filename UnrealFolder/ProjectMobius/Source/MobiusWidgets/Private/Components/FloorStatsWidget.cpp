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

#include "Components/FloorStatsWidget.h"

#include "Components/ButtonWithText.h"
#include "Components/TextBlock.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "ProjectMobius/Public/GameInstances/WebSocketSubsystem.h"
#include "MassAI/Subsystems/TimeDilationSubSystem.h"
#include "Visualization/Public/HeatmapSubsystem.h"


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

		// for the chart to show the current data for qt app we can listen to the delegate that broadcasts when file data changes
		// and send the data to the qt app

		// bind the spawn subsystem -> when this delegate is called we know the data has been loaded and processed
		if (auto SpawnSystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>())
		{
			// ensure that it is not already bound to the delegate
			SpawnSystem->OnPedestrianDataReadyToSpawn.RemoveDynamic(this, &UFloorStatsWidget::BuildDataForInstantQtUI);
			
			SpawnSystem->OnPedestrianDataReadyToSpawn.AddDynamic(this, &UFloorStatsWidget::BuildDataForInstantQtUI);
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
		
		
		WsSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UWebSocketSubsystem>();
		
		if (WsSubsystem == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("The WebSocket Subsystem is invalid"));
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
		SpawnSystem->OnPedestrianDataReadyToSpawn.RemoveDynamic(this, &UFloorStatsWidget::BuildDataForInstantQtUI);
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

		if (FloorNumber == -1)
		{
			// we have to send a time that is float and divided by 10 to match the logic for the method
			float NewTime = LastSentTimeInt / 10.0f;
			
			// we can do this as it ensures that the agent count is synced with QT app
			UpdateCurrentPlaybackTime(NewTime);
		}
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
	}
	else
	{
		if (bIsBetweenFloorWidget)
		{
			FText BottomFloorText = FText::AsNumber(FloorNumber);
			FText TopFloorText = FText::AsNumber(FloorNumber + 1);
			FloorPrefixText = FText::Format(FText::FromString("Between Floors {0} & {1}: "), BottomFloorText, TopFloorText);
		}
		else
		{
			FloorPrefixText = FText::Format(FText::FromString("Floor {0}: "), FText::AsNumber(FloorNumber));
		}
	}
}

void UFloorStatsWidget::BuildQtAppChartTitle() const
{
	// build the headings
	TSharedPtr<FJsonObject> ChartTitle = MakeShared<FJsonObject>();
	ChartTitle->SetStringField(TEXT("action"),     TEXT("updateChartTitle"));
	
	ChartTitle->SetStringField(TEXT("chartTitle"), TEXT("Chart Metrics"));

	WsSubsystem->SendJsonMessage(ChartTitle);
}

void UFloorStatsWidget::BuildQtChartAxisSetting()
{
	// calculate the min and max values for x and y axis
	TSharedPtr<FJsonObject> AxisSettings = MakeShared<FJsonObject>();

	// work out max time
	float MaxTime = TimeDilationSubSystem->TotalTime;

	// get the max agent count
	int32 MaxAgentCount = 1; // default to 1 to avoid division by zero
	if(auto AgentDataSubSystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>())
	{
		MaxAgentCount = AgentDataSubSystem->GetMaxAgents();
	}

	// we cant have axis mins and max == the same or be min > max
	if (MinAgentCountToSend > MaxAgentCount)
	{
		// if greater then swap them
		int32 temp = MinAgentCountToSend;
		MinAgentCountToSend = MaxAgentCount;
		MaxAgentCount = temp;
	}

	if (MinAgentCountToSend == MaxAgentCount)
	{
		// they cant be equal so increase max
		MaxAgentCount += 1;
	}
	if (MaxTime == 0.0f)
	{
		// if max time is 0 then set it to 1
		MaxTime = 1.0f;
	}

	AxisSettings->SetStringField(TEXT("action"), TEXT("updateAxis"));
	AxisSettings->SetStringField(TEXT("xTitle"),TEXT("Elapsed Time (s)"));
	AxisSettings->SetStringField(TEXT("yTitle"),TEXT("Occupants"));
	AxisSettings->SetNumberField(TEXT("xMin"), 0.0); // for now assume always start at 0
	AxisSettings->SetNumberField(TEXT("xMax"), MaxTime);
	AxisSettings->SetNumberField(TEXT("yMin"), MinAgentCountToSend); // what was the smallest value we found in the data
	AxisSettings->SetNumberField(TEXT("yMax"), MaxAgentCount);

	// todo: need better place for this but we know, when time is 0 and count is 0 we want count to be max agent count
	// if (CurrentLiveAgentCount != LastSentCount && LastSentTime == 0.0f)
	// {
	// 	CurrentLiveAgentCount = MaxAgentCount;
	// }

	WsSubsystem->SendJsonMessage(AxisSettings);
}

void UFloorStatsWidget::BuildQtChartGraphData() const
{
	// should only send point data if we have any
	if (CompleteUIData.Num() != 0)
	{
		TSharedPtr<FJsonObject> CompleteDataMsg = MakeShared<FJsonObject>();
		CompleteDataMsg->SetStringField(TEXT("action"), TEXT("setData"));
			
		CompleteDataMsg->SetArrayField(TEXT("points"), CompleteUIData);
		WsSubsystem->SendJsonMessage(CompleteDataMsg);
	}
}

void UFloorStatsWidget::SendQtAppChartData()
{
	// don't send data if subsystems are not valid or if this is not the total occupants widget
	if (FloorNumber == -1 && TimeDilationSubSystem != nullptr && WsSubsystem != nullptr)
	{
		BuildQtAppChartTitle();

		BuildQtChartAxisSetting();

		if (TimeDilationSubSystem->GetCurrentSimTime() == 0.0f && CurrentLiveAgentCount == 0)
		{
			UpdateCurrentPlaybackTime(0.0f);
		}
		else
		{
			// update live data
			UpdateAgentLiveData();// may need to move checks into this method
		}
		
		BuildQtChartGraphData();
	}
}

void UFloorStatsWidget::LaunchCloseQtApp()
{
	if (FloorNumber == -1 && WsSubsystem != nullptr)
	{
		// launch or close the qt app
		WsSubsystem->OpenOrCloseQtStatApp();

		// TODO: change the open close method to return a bool so we can check if the app is open or not and then send the data if it is open
		
		// build the data for the instant UI
		BuildDataForInstantQtUI();
		
		SendQtAppChartData();
	}
}

void UFloorStatsWidget::BuildDataForInstantQtUI()
{
	// Only doing all data for now
	if (FloorNumber == -1 && TimeDilationSubSystem != nullptr && WsSubsystem != nullptr)
	{
		CompleteUIData.Reset();
		
		
		if(auto MES_Subsystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>())
		{
			

			// if we have no data then smallest count is 0
			if (MES_Subsystem->SimulationFragment.SimulationData.Num() == 0)
			{
				// send empty data
				SendQtAppChartData();
				return;
			}

			CompleteUIData.Reserve(MES_Subsystem->SimulationFragment.SimulationData.Num()); // reserve some space for the data

			int32 SmallestFoundSampleCount = INT32_MAX;
			
			// loop through samples
			for (int32 i = 0; i < MES_Subsystem->SimulationFragment.SimulationData.Num(); i++)
			{
				// New sample smaller than current smallest
				if (MES_Subsystem->SimulationFragment.SimulationData[i].Num() < SmallestFoundSampleCount)
				{
					SmallestFoundSampleCount = MES_Subsystem->SimulationFragment.SimulationData[i].Num();
				}
				
				// get the sample count
				int32 SampleCount = MES_Subsystem->SimulationFragment.SimulationData[i].Num();

				// get the time frequency from time dilation subsystem
				float TimeBetweenSteps = TimeDilationSubSystem->TimeBetweenSteps;

				// time of current sample -> assumes no missing data
				float CurrentTime = i * TimeBetweenSteps;

				// build the points array
				auto NewPoint = MakeShared<FJsonObject>();
				NewPoint->SetNumberField(TEXT("x"), CurrentTime);
				NewPoint->SetNumberField(TEXT("y"), SampleCount);
				
				
				// add to the array
				CompleteUIData.Add(MakeShared<FJsonValueObject>(NewPoint));

				
			}
			// Update the min agent count to send
			MinAgentCountToSend = SmallestFoundSampleCount;
			
			// send the data in case the qt app is already open
			SendQtAppChartData();
		}
		else
		{
			// log runnable null
			UE_LOG(LogTemp, Warning, TEXT("The spawn system is null"));
		}
		
	}
}

void UFloorStatsWidget::UpdateCurrentPlaybackTime(float CurrentTime)
{
	
	// for now doing total so if floor number is not -1 then do nothing
	if (FloorNumber != -1 || WsSubsystem == nullptr || TimeDilationSubSystem == nullptr)
	{
		return;
	}
	
	// TODO: currently we have a limitation for updates that are sent to the Qt app,
	// We can only send updates when the time changes at 1dp (1 decimal place) to avoid sending too many updates
	// Otherwise the Qt app updates too frequently and causes the UI to lag behind
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
	TSharedPtr<FJsonObject> UpdateLiveDataMsg = MakeShared<FJsonObject>();
	UpdateLiveDataMsg->SetStringField(TEXT("action"), TEXT("updateLiveData"));
	UpdateLiveDataMsg->SetNumberField(TEXT("currentTime"), LastSentTimeInt);
	UpdateLiveDataMsg->SetNumberField(TEXT("count"), CurrentLiveAgentCount);
	WsSubsystem->SendJsonMessage(UpdateLiveDataMsg);
}

FText UFloorStatsWidget::FormatTextForTextBlock(const FText& Prefix, int32 Count)
{
	FFormatNamedArguments Args;
	Args.Add("Pre", Prefix);
	Args.Add("Count", FText::AsNumber(Count));
	return FText::Format(FText::FromString("{Pre}{Count}"), Args);
}
