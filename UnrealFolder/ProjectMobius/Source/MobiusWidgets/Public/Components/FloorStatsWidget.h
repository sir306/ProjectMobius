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

#pragma once

#include "CoreMinimal.h"
#include "WidgetInterface.h"
#include "Blueprint/UserWidget.h"
#include "FloorStatsWidget.generated.h"

class UBaseButton;
class UTextBlock;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UFloorStatsWidget : public UUserWidget, public IWidgetInterface
{
	GENERATED_BODY()
	
#pragma region METHODS
public:
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Native Destruct
	virtual void NativeDestruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Method to keep the design properties synchronized */
	virtual void SynchronizeProperties() override;

	/**
	 * Update the agent count variable, is bound to the heatmap subsystem delegate
	 *
	 * @param[int32] InFloorNumber - The floor number to update
	 * @param[int32] AgentCount - The number of agents on the floor
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void UpdateFloorLiveStatCount(int32 InFloorNumber, int32 AgentCount);

	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void BuildFloorText();
	void BuildQtAppChartTitle() const;
	void BuildQtChartAxisSetting();
	void BuildQtChartGraphData() const;

	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void SendQtAppChartData();

	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void LaunchCloseQtApp();

	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void BuildDataForInstantQtUI();

	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void UpdateCurrentPlaybackTime(float CurrentTime);

	UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
	void UpdateAgentLiveData();

	static FText FormatTextForTextBlock(const FText& Prefix, int32 Count);
	
#pragma endregion METHODS

#pragma region PROPERTYS

	/** pointer to the websocket subsystem */
	UPROPERTY()
	TObjectPtr<class UWebSocketSubsystem> WsSubsystem;

	/** pointer to the time subsystem */
	UPROPERTY()
	TObjectPtr<class UTimeDilationSubSystem> TimeDilationSubSystem;

	/** Current Active agent text */
	UPROPERTY()
	FText FloorPrefixText;

	/** Current Active agent text block */
	UPROPERTY(meta= (BindWidget))
	TObjectPtr<UTextBlock> FloorTextBlock;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UBaseButton> CurrentFloorBtn;

	/** Floor Number */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Mobius|Widgets|FloorStatsWidget", Meta = (ExposeOnSpawn=true))
	int32 FloorNumber = -1;

	/** Current Live Agent Count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Widgets|FloorStatsWidget")
	int32 CurrentLiveAgentCount = 0;

	/** Is this a between floor widget counter */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Mobius|Widgets|FloorStatsWidget", Meta = (ExposeOnSpawn=true))
	bool bIsBetweenFloorWidget = false;

private:
	/** Complete Sim data for Qt UI so can be sent at Launch */
	TArray<TSharedPtr<FJsonValue>> CompleteUIData;

	/** The min number of agents to send to the Qt UI */
	int32 MinAgentCountToSend = 0;
	
	int32 LastSentTimeInt = -1;
	int32 LastSentCount = -1;

	bool bCheckingSameData = false;
	
#pragma endregion PROPERTYS
	
};
