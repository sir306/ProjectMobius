// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DPICustomScalingRule.h"
#include "MobiusUIScalingRule.generated.h"

/**
 * Hybrid UI scaling rule for a desktop viewer that must behave like a native application:
 * takes the larger of a resolution curve (designed-at-1080p) and the OS per-monitor DPI scale.
 *
 * - OS scale handles Windows display scaling (100%..400%) and per-monitor DPI, including windows
 *   spanning monitors (the window reports the DPI Windows arbitrates for it).
 * - The resolution curve is the floor for large low-DPI displays (4K monitor at 100% OS scaling),
 *   where pure OS scale would render the UI unreadably small.
 * - A fit clamp keeps the logical viewport from collapsing when a small window sits on a heavily
 *   scaled monitor.
 *
 * Registered via DefaultEngine.ini [/Script/Engine.UserInterfaceSettings] UIScaleRule=Custom.
 * The engine multiplies UUserInterfaceSettings::ApplicationScale (the user UI-scale setting,
 * see UUserProjectSettings::UIScaleFactor) on top of this rule's result.
 * On Mac, SWindow::GetDPIScaleFactor() reports 1.0 (AppKit pre-scales via the backing factor),
 * so the rule degrades to the resolution curve — correct behavior on retina displays.
 */
UCLASS()
class MOBIUSCORE_API UMobiusUIScalingRule : public UDPICustomScalingRule
{
	GENERATED_BODY()

public:
	/** Size arrives in physical pixels because bAllowHighDPIInGameMode=True. */
	virtual float GetDPIScaleBasedOnSize(FIntPoint Size) const override;
};
