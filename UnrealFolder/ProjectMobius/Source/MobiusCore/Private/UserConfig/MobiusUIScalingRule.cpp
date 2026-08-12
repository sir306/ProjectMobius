// Fill out your copyright notice in the Description page of Project Settings.

#include "UserConfig/MobiusUIScalingRule.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SWindow.h"

/**
 * Testing hook: emulate a high-DPI monitor by forcing the OS-scale input of the rule
 * (2 = 4K at 200% Windows scaling, 4 = 400%). 0 = off. The curve/fit-clamp still apply,
 * so behavior matches a real monitor at that scale — including the small-window fit clamp.
 */
static TAutoConsoleVariable<float> CVarMobiusUIScaleOverride(
	TEXT("Mobius.UIScaleOverride"),
	0.0f,
	TEXT("Force the OS DPI scale seen by the Mobius UI scaling rule (0 = off). e.g. 2 emulates 4K@200%, 4 emulates 400% Windows scaling."));

float UMobiusUIScalingRule::GetDPIScaleBasedOnSize(FIntPoint Size) const
{
	// Called before the viewport has a real size during startup — never scale a degenerate viewport.
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return 1.0f;
	}

	// OS per-monitor DPI scale for the actual game window; the window's value tracks WM_DPICHANGED,
	// which is what handles monitor moves and dual-monitor-spanning windows.
	float OsScale = CVarMobiusUIScaleOverride.GetValueOnAnyThread();
	if (OsScale <= 0.0f && FSlateApplication::IsInitialized())
	{
		if (GEngine && GEngine->GameViewport)
		{
			if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
			{
				OsScale = Window->GetDPIScaleFactor();
			}
		}

		if (OsScale <= 0.0f)
		{
			OsScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(0.0f, 0.0f);
		}
	}
	OsScale = (OsScale > 0.0f) ? OsScale : 1.0f;

	// v2: OS scale x window factor. v1 used max(curve, OS), which floored the scale at the OS value —
	// sub-fullscreen windows kept full-size text while their panels shrank, squishing every layout
	// (the app's proportional design relies on the UI scaling DOWN with the window, as the old
	// engine-default curve did). The window factor restores that: it is the window's OS-normalized
	// shortest side against the 1080p design frame, capped at 1 so fullscreen never over-scales, and
	// floored at 0.4 so tiny/low-res windows scale down enough for dense panels to FIT (was 0.5,
	// which crammed content at small resolutions) while staying legible. High-DPI monitors keep
	// their OS multiplier, and UUserInterfaceSettings::ApplicationScale (the user slider) multiplies on top.
	const float ShortestSide = static_cast<float>(FMath::Min(Size.X, Size.Y));
	const float LogicalShortestSide = ShortestSide / OsScale;
	const float WindowFactor = FMath::Clamp(LogicalShortestSide / 1080.0f, 0.4f, 1.0f);

	float Scale = OsScale * WindowFactor;

	// Fit clamp: never let the logical viewport drop below ~600px on its shortest side (a small
	// window on a 400%-scaled monitor would otherwise have almost no logical space to lay out in).
	Scale = FMath::Min(Scale, ShortestSide / 600.0f);

	return FMath::Max(Scale, 0.01f);
}
