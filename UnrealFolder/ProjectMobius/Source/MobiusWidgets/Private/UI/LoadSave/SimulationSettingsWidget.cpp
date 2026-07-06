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

#include "UI/LoadSave/SimulationSettingsWidget.h"
#include "Components/CheckBox.h"
#include "BRisk/BRiskDataSubsystem.h"

void USimulationSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UBRiskDataSubsystem* BRiskSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UBRiskDataSubsystem>() : nullptr;

	// Toggles are optional (added in the WBP designer). Bind change events + seed the initial
	// checked state from the subsystem so the UI reflects the current flags.
	if (UseBRiskTimingCheckBox)
	{
		UseBRiskTimingCheckBox->OnCheckStateChanged.AddDynamic(this, &USimulationSettingsWidget::OnUseBRiskTimingChanged);
		if (BRiskSubsystem)
		{
			UseBRiskTimingCheckBox->SetIsChecked(BRiskSubsystem->GetConfigureSharedPlaybackOnLoad());
		}
	}

	if (LoadRoomGeometryCheckBox)
	{
		LoadRoomGeometryCheckBox->OnCheckStateChanged.AddDynamic(this, &USimulationSettingsWidget::OnLoadRoomGeometryChanged);
		if (BRiskSubsystem)
		{
			LoadRoomGeometryCheckBox->SetIsChecked(BRiskSubsystem->GetAutoGenerateRoomGeometryOnLoad());
		}
	}
}

void USimulationSettingsWidget::OnUseBRiskTimingChanged(bool bIsChecked)
{
	if (UWorld* World = GetWorld())
	{
		if (UBRiskDataSubsystem* BRiskSubsystem = World->GetSubsystem<UBRiskDataSubsystem>())
		{
			// Live: sets the flag AND re-evaluates the active clock source now (no reload),
			// preserving the current play position and play/pause state.
			BRiskSubsystem->SetUseBRiskTiming(bIsChecked);
		}
	}
}

void USimulationSettingsWidget::OnLoadRoomGeometryChanged(bool bIsChecked)
{
	if (UWorld* World = GetWorld())
	{
		if (UBRiskDataSubsystem* BRiskSubsystem = World->GetSubsystem<UBRiskDataSubsystem>())
		{
			// Live: sets the flag AND generates/tears down room geometry immediately when a
			// scenario is already loaded.
			BRiskSubsystem->SetRoomGeometryEnabled(bIsChecked);
		}
	}
}
