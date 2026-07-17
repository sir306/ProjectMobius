// Fill out your copyright notice in the Description page of Project Settings.

#include "Style/MobiusStyle.h"

#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
	Style->Set("Mobius.Text.Title", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Bold", 12)));   // BW6 density: 16->12
	Style->Set("Mobius.Text.Header", FTextBlockStyle(NormalText).SetFont(InterFontStyle("SemiBold", 11))); // BW6 density: 14->11
	// Q28 (P4/D40 font sweep): "Mobius.Text.Label" → token `label` Font_Inter Regular 15. This is the
	// ButtonWithText fallback (used when no SWS text style is assigned). Its live consumer is the three
	// ribbon tabs (FilesPanelBtn / DisplaylPanelBTN / HelpPanelBtn all have MobiusButtonTextStyle=None);
	// Browse / Add / Remove / Reset carry their own SWS text style and are unaffected. NOTE: the tab
	// token is 16 SemiBold(active)/Regular(inactive); a single shared fallback style cannot express the
	// per-state weight, and tab active-emphasis is already carried by the accent underline+text colour,
	// so tabs land on Regular 15 here — for exact SemiBold-16 tabs, give them a dedicated tab text style
	// (SWS or a new "Mobius.Text.Tab" key) as follow-up asset/BP work. (Was SemiBold 12, a 2026-07-05 trim.)
	// BW2/B8: tab label token size = 16 (spec §2 tab token). Single fallback style can't do per-state
	// weight (active SemiBold / inactive Regular), so it lands Regular 16; tab active-emphasis rides the
	// accent underline + colour. A dedicated tab text style remains the exact-SemiBold follow-up.
	// BW6 density: Label (ribbon tabs + Browse) 16->12; Body 11->10; Field 11->10. Owner map 16->12/14->11/13->10/12->10 floor 10.
	Style->Set("Mobius.Text.Label", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Regular", 12)));
	// BW7/D138: dedicated rail-button label style (Floor Stats / Flow Counter VerticalTextBlock rails).
	// Decoupled from "Mobius.Text.Label" (ribbon tabs + Browse = 12) so the vertical rail labels land on
	// the owner's 10 without shrinking the tabs. Retinted per theme in UIThemeSubsystem::ApplySharedStyles
	// alongside "Mobius.Text.Label" (VerticalTextBlock has no per-widget colour handling; it reads this
	// shared style on rebuild).
	Style->Set("Mobius.Text.RailButton", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Regular", 10)));
	Style->Set("Mobius.Text.Body", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Regular", 10)));
	Style->Set("Mobius.Text.Field", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Regular", 10)));
	Style->Set("Mobius.Text.Caption", FTextBlockStyle(NormalText).SetFont(InterFontStyle("Medium", 9)));
	Style->Set("Mobius.Text.Error.Location", FTextBlockStyle(NormalText)
		.SetFont(InterFontStyle("Regular", 10))
		.SetColorAndOpacity(FSlateColor(ErrorColor)));

	// ---- Widget styles --------------------------------------------------------------------------
	// 7a "AutoCAD dark" toolbar button (drives ButtonWithText Browse etc.; ribbon tabs override their
	// brush via the tab material at runtime so they are unaffected). #4a4a4a fill / #5a5a5a 1px outline
	// / #e0e0e0 label, with subtle hover/press.
	const FLinearColor BtnFill    = FLinearColor::FromSRGBColor(FColor(0x4A, 0x4A, 0x4A));
	const FLinearColor BtnHover   = FLinearColor::FromSRGBColor(FColor(0x56, 0x56, 0x56));
	const FLinearColor BtnPress   = FLinearColor::FromSRGBColor(FColor(0x3A, 0x3A, 0x3A));
	const FLinearColor BtnOutline = FLinearColor::FromSRGBColor(FColor(0x5A, 0x5A, 0x5A));
	const FLinearColor BtnText    = FLinearColor::FromSRGBColor(FColor(0xE0, 0xE0, 0xE0));
	FButtonStyle MobiusButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(BtnFill,   2.0f, BtnOutline, 1.0f))
		.SetHovered(FSlateRoundedBoxBrush(BtnHover, 2.0f, BtnOutline, 1.0f))
		.SetPressed(FSlateRoundedBoxBrush(BtnPress, 2.0f, BtnOutline, 1.0f))
		.SetDisabled(FSlateRoundedBoxBrush(BtnFill, 2.0f, BtnOutline, 1.0f))
		.SetNormalForeground(FSlateColor(BtnText))
		.SetHoveredForeground(FSlateColor(FLinearColor::White))
		.SetPressedForeground(FSlateColor(BtnText))
		.SetNormalPadding(FMargin(8.0f, 3.0f))
		.SetPressedPadding(FMargin(8.0f, 3.0f));
	Style->Set("Mobius.Button", MobiusButton);

	// Red-themed window chrome for the error window (previously hand-built in ErrorWindow.cpp).
	FWindowStyle ErrorWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	ErrorWindowStyle.ActiveTitleBrush.TintColor = FSlateColor(ErrorColor);
	ErrorWindowStyle.InactiveTitleBrush.TintColor = FSlateColor(ErrorInactiveColor);
	ErrorWindowStyle.FlashTitleBrush.TintColor = FSlateColor(ErrorFlashColor);
	ErrorWindowStyle.TitleTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::Red);
	Style->Set("Mobius.Window.Error", ErrorWindowStyle);

	return Style;
}
