// Fill out your copyright notice in the Description page of Project Settings.

#include "Style/MobiusStyle.h"

#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Engine/Font.h"

TSharedPtr<FSlateStyleSet> FMobiusStyle::StyleInstance = nullptr;

void FMobiusStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FMobiusStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		StyleInstance.Reset();
	}
}

const ISlateStyle& FMobiusStyle::Get()
{
	// Late-initialize defensively: widget CDOs can be constructed surprisingly early in some
	// commandlet/cook paths before StartupModule-ordered code has run.
	Initialize();
	return *StyleInstance;
}

FName FMobiusStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("MobiusStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FMobiusStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	// ---- Colors -------------------------------------------------------------------------------
	// Palette values pending owner sign-off; error tints lifted verbatim from the previous
	// hardcoded ErrorWindow theme so visuals do not change with the migration.
	const FLinearColor ErrorColor(0.8f, 0.1f, 0.1f);
	const FLinearColor ErrorInactiveColor(0.6f, 0.1f, 0.1f);
	const FLinearColor ErrorFlashColor(0.9f, 0.2f, 0.2f);
	const FLinearColor SurfaceColor = FLinearColor::FromSRGBColor(FColor(0x1E, 0x22, 0x28, 235)); // dark slate @ ~92%
	const FLinearColor OutlineColor = FLinearColor::FromSRGBColor(FColor(0x3A, 0x41, 0x4B));
	const FLinearColor ForegroundColor(0.9f, 0.9f, 0.9f);

	Style->Set("Mobius.Color.Error", ErrorColor);
	Style->Set("Mobius.Color.ErrorInactive", ErrorInactiveColor);
	Style->Set("Mobius.Color.ErrorFlash", ErrorFlashColor);
	Style->Set("Mobius.Color.Surface", SurfaceColor);
	Style->Set("Mobius.Color.Outline", OutlineColor);
	Style->Set("Mobius.Color.Foreground", ForegroundColor);

	// ---- Margins ------------------------------------------------------------------------------
	Style->Set("Mobius.Padding.Window", FMargin(16.0f));
	Style->Set("Mobius.Padding.Button", FMargin(4.0f, 2.0f));

	// ---- Text ramp (integer sizes only) ---------------------------------------------------------
	const FTextBlockStyle NormalText = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

	// App typeface: Inter (composite UFont at /Game). Create() runs lazily on first Get(), by which
	// point game content is mounted, so this LoadObject is safe; falls back to the engine default
	// face if the asset is unavailable (e.g. an early cook/commandlet path).
	UFont* InterFont = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter"));
	auto InterFontStyle = [InterFont](const char* Typeface, int32 Size) -> FSlateFontInfo
	{
		if (InterFont)
		{
			return FSlateFontInfo(InterFont, Size, FName(Typeface));
		}
		const bool bBold = FCStringAnsi::Stricmp(Typeface, "Bold") == 0 || FCStringAnsi::Stricmp(Typeface, "SemiBold") == 0;
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	};

	// Ramp trimmed ~2px (2026-07-05): the ribbon tabs / ButtonWithText read "Mobius.Text.Label",
	// which felt oversized at design scale (1080p). Sizes are logical px; the UI scaling rule scales
	// them per-display on top.
	Style->Set("Mobius.Text.Title", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Bold", 16)));
	Style->Set("Mobius.Text.Header", FTextBlockStyle(NormalText).SetFont(InterFontStyle("SemiBold", 14)));
	Style->Set("Mobius.Text.Label", FTextBlockStyle(NormalText).SetFont(InterFontStyle("SemiBold", 12)));
	Style->Set("Mobius.Text.Body", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Regular", 11)));
	Style->Set("Mobius.Text.Field", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Regular", 11)));
	Style->Set("Mobius.Text.Caption", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Medium", 9)));
	Style->Set("Mobius.Text.Error.Location", FTextBlockStyle(NormalText)
		.SetFont(InterFontStyle("Regular", 10))
		.SetColorAndOpacity(FSlateColor(ErrorColor)));

	// ---- Widget styles --------------------------------------------------------------------------
	Style->Set("Mobius.Button", FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button")));

	// Red-themed window chrome for the error window (previously hand-built in ErrorWindow.cpp).
	FWindowStyle ErrorWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	ErrorWindowStyle.ActiveTitleBrush.TintColor = FSlateColor(ErrorColor);
	ErrorWindowStyle.InactiveTitleBrush.TintColor = FSlateColor(ErrorInactiveColor);
	ErrorWindowStyle.FlashTitleBrush.TintColor = FSlateColor(ErrorFlashColor);
	ErrorWindowStyle.TitleTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::Red);
	Style->Set("Mobius.Window.Error", ErrorWindowStyle);

	return Style;
}
