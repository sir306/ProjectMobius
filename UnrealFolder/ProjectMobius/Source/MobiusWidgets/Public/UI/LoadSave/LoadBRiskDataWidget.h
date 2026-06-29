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
#include "UI/LoadSave/LoadDataParentWidget.h"
#include "LoadBRiskDataWidget.generated.h"

class UCheckBox;

/**
 * Load widget for B-Risk .smv scenarios, plus the B-Risk visualization toggles.
 * Mirrors ULoadAgentDataWidget / ULoadMeshWidget but uses the B-Risk file dialog and the game
 * instance's B-Risk path (which drives the existing load chain). It also hosts toggles that set
 * UBRiskDataSubsystem load-time flags (timeline timing, room geometry).
 */
UCLASS()
class MOBIUSWIDGETS_API ULoadBRiskDataWidget : public ULoadDataParentWidget
{
	GENERATED_BODY()

public:
	// Constructor
	virtual void NativeConstruct() override;

	/**
	 * Method to call when the SelectFileButton is clicked.
	 * Overridden from the parent to open the B-Risk .smv file dialog.
	 */
	virtual void OnSelectFileButtonClicked() override;

	/**
	 * Get Mobius Game Instance data --
	 * Overridden to read the current B-Risk .smv path.
	 */
	virtual void GetMobiusGameInstanceData() override;

	/**
	 * Update the Mobius Game Instance data --
	 * Overridden to write the selected B-Risk .smv path (which triggers the B-Risk load chain).
	 */
	virtual void UpdateMobiusGameInstanceData() override;

	/**
	 * Callback for the B-Risk file dialog. NOTE: the B-Risk dialog delegate is 2-param
	 * (SmvFilePath, bSuccess), unlike the parent's 4-param DialogClosed, so this is a separate
	 * handler rather than an override.
	 */
	UFUNCTION()
	void OnBRiskFileDialogClosed(const FString& SmvFilePath, bool bSuccess);

	/** Handler for file dialog errors. Displays error popup to user. */
	UFUNCTION()
	void OnDialogError(const FString& ErrorTitle, const FString& ErrorMessage);

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
