// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"

/**
 * Code-defined Slate style set: the single source of truth for Mobius UI typography, colors and
 * spacing on the native side. Registered in FMobiusWidgetsModule::StartupModule, so it exists
 * before any widget constructs.
 *
 * Native Slate widgets (error window, moveable windows) read styles from here directly.
 * UMG-facing native widgets (ButtonWithText, FieldAndTextWidget, ...) use this as their FALLBACK
 * when no USlateWidgetStyleAsset is assigned in the editor — assigned SWS_* assets still override.
 *
 * Keys:
 *   Text:    "Mobius.Text.Title" (Bold 18), "Mobius.Text.Header" (Bold 16), "Mobius.Text.Label" (Regular 14),
 *            "Mobius.Text.Body" (Regular 12), "Mobius.Text.Field" (Regular 12), "Mobius.Text.Caption" (Regular 10),
 *            "Mobius.Text.Error.Location" (Regular 11, error tint)
 *   Colors:  "Mobius.Color.Error" / ".ErrorInactive" / ".ErrorFlash" / ".Surface" / ".Outline" / ".Foreground"
 *   Margins: "Mobius.Padding.Window" (16), "Mobius.Padding.Button" (4,2)
 *   Widgets: "Mobius.Button" (FButtonStyle), "Mobius.Window.Error" (FWindowStyle, red title theme)
 *
 * Integer font sizes only — fractional sizes defeat the Slate font cache and shimmer under DPI scale.
 */
class MOBIUSWIDGETS_API FMobiusStyle
{
public:
	/** Create + register the style set. Safe to call more than once. */
	static void Initialize();

	/** Unregister + release. */
	static void Shutdown();

	static const ISlateStyle& Get();

	static FName GetStyleSetName();

private:
	static TSharedRef<class FSlateStyleSet> Create();

	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
