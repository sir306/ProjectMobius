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
#include "Blueprint/UserWidget.h"
#include "SimulationSettingsWidget.generated.h"

class UCheckBox;

/**
 * Simulation-settings panel: B-Risk load-time toggles (shared playback timing, room geometry).
 * Decoupled from ULoadBRiskDataWidget so the toggles can be laid out beside the file inputs in the
 * SETUP tab rather than nested inside the B-Risk file loader. Drives UBRiskDataSubsystem flags live.
 */
UCLASS()
class MOBIUSWIDGETS_API USimulationSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Constructor
	virtual void NativeConstruct() override;

protected:
	/** Toggle: use the B-Risk zone-CSV Time column to drive shared playback timing on the next load. */
	UFUNCTION()
	void OnUseBRiskTimingChanged(bool bIsChecked);

	/** Toggle: generate solid room-wall geometry from the B-Risk scenario on the next load. */
	UFUNCTION()
	void OnLoadRoomGeometryChanged(bool bIsChecked);

private:
	/** "Use B-Risk playback timing" toggle. Optional so the C++ works before the WBP adds the control. */
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UCheckBox> UseBRiskTimingCheckBox;

	/** "Load room geometry" toggle. Optional so the C++ works before the WBP adds the control. */
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UCheckBox> LoadRoomGeometryCheckBox;
};
