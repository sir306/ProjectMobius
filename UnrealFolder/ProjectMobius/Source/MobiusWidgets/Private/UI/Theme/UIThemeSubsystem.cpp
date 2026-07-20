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

#include "UI/Theme/UIThemeSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Slate/SlateBrushAsset.h"
#include "UserConfig/UserProjectSettings.h"
#include "Style/MobiusStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "UI/Components/ButtonWithText.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusTheme, Log, All);

namespace MobiusTheme
{
	struct FColorPair
	{
		FLinearColor Dark;
		FLinearColor Light;
	};

	// -------------------------------------------------------------------------------------------
	// AUTHORITATIVE PALETTE (P1) — one entry per mockup CSS var, design-tokens.json v2 linearRGBA
	// VERBATIM (never hex/255). Indexed by EMobiusPaletteRole; read via GetPaletteColor(). This is
	// the single source of truth for phases P2-P7. The value-remap SurfaceMap/TextMap arrays BELOW
	// are the curated subset the by-value theme walker can distinguish; roles absent from those
	// arrays are applied EXPLICITLY per theme by style code (see EMobiusPaletteRole docs).
	// -------------------------------------------------------------------------------------------
	struct FThemeColor
	{
		FLinearColor Light;
		FLinearColor Dark;
	};

	static const FThemeColor GMobiusPalette[] =
	{
		/* TitlebarBg        */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.03955f, 0.03955f, 0.03955f) },
		/* TitlebarBorder    */ { FLinearColor(0.76052f, 0.76052f, 0.76052f),     FLinearColor(0.01938f, 0.01938f, 0.01938f) },
		/* TitlebarText      */ { FLinearColor(0.0331f, 0.0331f, 0.0331f),        FLinearColor(0.55201f, 0.55201f, 0.55201f) },
		/* TabstripBg        */ { FLinearColor(0.7913f, 0.7913f, 0.7913f),        FLinearColor(0.02843f, 0.02843f, 0.02843f) },
		/* TabstripBorder    */ { FLinearColor(0.63076f, 0.63076f, 0.63076f),     FLinearColor(0.01764f, 0.01764f, 0.01764f) },
		/* TabActiveBg       */ { FLinearColor(0.9131f, 0.9131f, 0.9131f),        FLinearColor(0.04519f, 0.04519f, 0.04519f) },
		/* TabActiveText     */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.82279f, 0.82279f, 0.82279f) },
		/* TabInactiveText   */ { FLinearColor(0.05781f, 0.05781f, 0.05781f),     FLinearColor(0.32314f, 0.32314f, 0.32314f) },
		/* TabActiveOutline  */ { FLinearColor(0.63076f, 0.63076f, 0.63076f),     FLinearColor(0.01764f, 0.01764f, 0.01764f) }, // dark: no tab outline; = dark line
		/* Accent            */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* RibbonBg          */ { FLinearColor(0.9131f, 0.9131f, 0.9131f),        FLinearColor(0.03955f, 0.03955f, 0.03955f) },
		/* PanelHeaderBg     */ { FLinearColor(0.82279f, 0.82279f, 0.82279f),     FLinearColor(0.05286f, 0.05286f, 0.05286f) },
		/* PanelHeaderText   */ { FLinearColor(0.05781f, 0.05781f, 0.05781f),     FLinearColor(0.46208f, 0.46208f, 0.46208f) },
		/* PanelHeaderBorder */ { FLinearColor(0.7454f, 0.7454f, 0.7454f),        FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* PanelDivider      */ { FLinearColor(0.76815f, 0.76815f, 0.76815f),     FLinearColor(0.05951f, 0.05951f, 0.05951f) },
		/* LabelText         */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.62396f, 0.62396f, 0.62396f) },
		/* SublabelText      */ { FLinearColor(0.13287f, 0.13287f, 0.13287f),     FLinearColor(0.32314f, 0.32314f, 0.32314f) },
		/* MicroText         */ { FLinearColor(0.2462f, 0.2462f, 0.2462f),        FLinearColor(0.25818f, 0.25818f, 0.25818f) },
		/* InputBg           */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.02416f, 0.02416f, 0.02416f) },
		/* InputBorder       */ { FLinearColor(0.19462f, 0.19462f, 0.19462f),     FLinearColor(0.09084f, 0.09084f, 0.09084f) },
		/* InputText         */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.62396f, 0.62396f, 0.62396f) },
		/* InputPlaceholder  */ { FLinearColor(0.40198f, 0.40198f, 0.40198f),     FLinearColor(0.14413f, 0.14413f, 0.14413f) },
		/* InputMonoText     */ { FLinearColor(0.09084f, 0.09084f, 0.09084f),     FLinearColor(0.32314f, 0.32314f, 0.32314f) },
		/* ButtonBg          */ { FLinearColor(0.93869f, 0.93869f, 0.93869f),     FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* ButtonBorder      */ { FLinearColor(0.41789f, 0.41789f, 0.41789f),     FLinearColor(0.10224f, 0.10224f, 0.10224f) },
		/* ButtonText        */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.7454f, 0.7454f, 0.7454f) },
		/* ButtonHoverBg     */ { FLinearColor(0.81485f, 0.87962f, 0.95597f),     FLinearColor(0.09306f, 0.09306f, 0.09306f) },
		/* ButtonHoverBorder */ { FLinearColor(0.25415f, 0.47932f, 0.72306f),     FLinearColor(0.14413f, 0.14413f, 0.14413f) },
		/* ButtonPressedBg   */ { FLinearColor(0.69387f, 0.7991f, 0.9131f),       FLinearColor(0.04971f, 0.04971f, 0.04971f) },
		/* CheckboxBg        */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.02416f, 0.02416f, 0.02416f) },
		/* CheckboxBorder    */ { FLinearColor(0.19462f, 0.19462f, 0.19462f),     FLinearColor(0.13287f, 0.13287f, 0.13287f) },
		/* CheckboxCheckedBg */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* CheckboxCheckmark */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(1.0f, 1.0f, 1.0f) },
		/* SliderTrack       */ { FLinearColor(0.58408f, 0.58408f, 0.58408f),     FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* SliderThumb       */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* KbdBg             */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.05951f, 0.05951f, 0.05951f) },
		/* KbdBorder         */ { FLinearColor(0.41789f, 0.41789f, 0.41789f),     FLinearColor(0.10224f, 0.10224f, 0.10224f) },
		/* KbdText           */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.7454f, 0.7454f, 0.7454f) },
		/* HelpRowDivider    */ { FLinearColor(0.7913f, 0.7913f, 0.7913f),        FLinearColor(0.05951f, 0.05951f, 0.05951f) },
		/* HelpRowText       */ { FLinearColor(0.0331f, 0.0331f, 0.0331f),        FLinearColor(0.62396f, 0.62396f, 0.62396f) },
		/* Zebra             */ { FLinearColor(0.85499f, 0.85499f, 0.85499f),     FLinearColor(0.04971f, 0.04971f, 0.04971f) },
		/* ChipOutline       */ { FLinearColor(0.0f, 0.0f, 0.0f, 0.25f),          FLinearColor(1.0f, 1.0f, 1.0f, 0.30f) },
		/* WellBg            */ { FLinearColor(0.85499f, 0.87137f, 0.88792f),     FLinearColor(0.05286f, 0.05286f, 0.05286f) },
		/* IconTint          */ { FLinearColor(0.04374f, 0.04374f, 0.04374f),     FLinearColor(0.69387f, 0.69387f, 0.69387f) },
		/* HoverBg           */ { FLinearColor(0.69387f, 0.69387f, 0.69387f),     FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* HintText          */ { FLinearColor(0.31855f, 0.31855f, 0.31855f),     FLinearColor(0.14413f, 0.14413f, 0.14413f) },
		/* WindowBorder      */ { FLinearColor(0.43415f, 0.43415f, 0.43415f),     FLinearColor(0.01033f, 0.01033f, 0.01033f) },
	};

	static_assert(UE_ARRAY_COUNT(GMobiusPalette) == static_cast<int32>(EMobiusPaletteRole::Count),
		"GMobiusPalette must have exactly one entry per EMobiusPaletteRole (order must match).");

	static FLinearColor PaletteColor(const EMobiusPaletteRole Role, const bool bLight)
	{
		const int32 Index = static_cast<int32>(Role);
		if (Index < 0 || Index >= UE_ARRAY_COUNT(GMobiusPalette))
		{
			return FLinearColor::Black;
		}
		return bLight ? GMobiusPalette[Index].Light : GMobiusPalette[Index].Dark;
	}

	// -------------------------------------------------------------------------------------------
	// NAME -> ROLE explicit-reapply table (D25/D26 + P3/P4 EXPLICIT-REAPPLY queues). These roles the
	// value-remap walker CANNOT distinguish: the dark-grey chrome region collapses several roles into
	// one bucket at Epsilon (zebra/hairline), the neutral-white guard skips them (kbd bg), or they
	// carry alpha (chip outline). So they are set EXPLICITLY per theme, keyed by widget-name substring
	// (all these targets are UBorders). Substrings are distinctive — verified against the live trees
	// (WBP_HeatmapColourBands, WBP_HelpPanel, WBP_LoadDataFiles, WBP_DisplayPanel, WBP_HeatmapSettingPanel).
	// P5: append rows here for HoverBg / control-state roles as those controls are authored.
	// -------------------------------------------------------------------------------------------
	enum class EThemeRoleTarget : uint8
	{
		BorderFill,     // UBorder brush TintColor (SetBrushColor)
		BorderOutline,  // UBorder brush OutlineSettings.Color (fill left untouched — e.g. LoS data chips)
	};

	struct FNameRole
	{
		const TCHAR* Substr;
		EMobiusPaletteRole Role;
		EThemeRoleTarget Target;
	};

	static const FNameRole GNameRoleMap[] =
	{
		{ TEXT("Zebra"),             EMobiusPaletteRole::Zebra,             EThemeRoleTarget::BorderFill },    // LoS ZebraA/C/E row tints
		{ TEXT("HdrLine"),           EMobiusPaletteRole::PanelHeaderBorder, EThemeRoleTarget::BorderFill },    // panel-header hairlines: HdrLine_* / LosHdrLine / HeatHdrLine / HHdrLine_*
		{ TEXT("HeatmapColourBand"), EMobiusPaletteRole::ChipOutline,       EThemeRoleTarget::BorderOutline }, // LoS chips 1..6 (outline only; fill is data colour)
		{ TEXT("HChip"),             EMobiusPaletteRole::KbdBg,             EThemeRoleTarget::BorderFill },    // Help keycap chip backgrounds
		// P6/BW2: WellBg (#eef0f2 / #414141) is NOT a SurfaceMap value bucket → the value walker skips
		// it. Any Border whose name contains "Well" (floor-stats Total-occupants well, flow-counter
		// "Move markers" well) gets the well fill explicitly per theme.
		{ TEXT("Well"),              EMobiusPaletteRole::WellBg,            EThemeRoleTarget::BorderFill },
		// BW3/Q34: collapse-pill (WBP_MobiusBottomBar CollapsePillBg, D80) fill = ButtonBg (#f8f8f8 /
		// #4a4a4a). Authored light-only in the asset; the value walker has no button_bg bucket, so flip
		// it here. Outline is already value-walker covered (chip-line). Substring "PillBg" hits only
		// CollapsePillBg (CollapsePillOverlay/CollapsePillBox do not contain it).
		{ TEXT("PillBg"),            EMobiusPaletteRole::ButtonBg,          EThemeRoleTarget::BorderFill },
	};

	/** App typeface (composite Inter/JetBrains-Mono UFont). Cached; loaded once game content is mounted. */
	static UFont* GetInterFont()
	{
		static TWeakObjectPtr<UFont> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter"));
		}
		return Cached.Get();
	}

	// Linear-space role pairs, dark (7a) <-> light (4b). Values must match the literals applied to
	// the widgets exactly (within Epsilon) or the walker will skip them.
	static const FColorPair SurfaceMap[] =
	{
		{ FLinearColor(0.0395f, 0.0395f, 0.0395f),  FLinearColor(0.9131f, 0.9131f, 0.9131f) }, // panel body   #383838 -> #f5f5f5
		{ FLinearColor(0.0284f, 0.0284f, 0.0284f),  FLinearColor(0.7913f, 0.7913f, 0.7913f) }, // tab strip    #2f2f2f -> #e6e6e6
		{ FLinearColor(0.052861f, 0.052861f, 0.052861f), FLinearColor(0.8228f, 0.8228f, 0.8228f) }, // header bar #414141 -> #eaeaea
		{ FLinearColor(0.0595f, 0.0595f, 0.0595f),  FLinearColor(0.7681f, 0.7681f, 0.7681f) }, // divider/chip #454545 -> #e3e3e3
		// Field bg is #fcfcfc, NOT pure white: pure white is the neutral multiplier on almost
		// every brush (the UBorder double-tint convention), and a 1.0 light entry would remap all
		// of those to #2b2b2b on the light->dark pass, blacking out the chrome.
		{ FLinearColor(0.0243f, 0.0243f, 0.0243f),  FLinearColor(0.973f, 0.973f, 0.973f) },    // field bg     #2b2b2b -> #fcfcfc
		{ FLinearColor(0.091f, 0.091f, 0.091f),     FLinearColor(0.1945f, 0.1945f, 0.1945f) }, // field line   #555555 -> #7a7a7a
		{ FLinearColor(0.1023f, 0.1023f, 0.1023f),  FLinearColor(0.4179f, 0.4179f, 0.4179f) }, // chip line    #5a5a5a -> #adadad
		{ FLinearColor(0.068f, 0.068f, 0.068f),     FLinearColor(0.58408f, 0.58408f, 0.58408f) }, // slider track #4a4a4a -> #c9c9c9 (D22: token slider_track, was #d0d0d0)
		{ FLinearColor(0.045f, 0.045f, 0.045f),     FLinearColor(0.9131f, 0.9131f, 0.9131f) }, // active tab   #3c3c3c -> #f5f5f5
		{ FLinearColor(0.010f, 0.012f, 0.015f),     FLinearColor(0.973f, 0.973f, 0.973f) },    // legacy field / combo bg #1a1c20 -> #fcfcfc
		{ FLinearColor(0.007f, 0.007f, 0.009f),     FLinearColor(0.7913f, 0.7913f, 0.7913f) }, // bottom bar   -> #e6e6e6
		{ FLinearColor(0.172f, 0.172f, 0.172f),     FLinearColor(0.4179f, 0.4179f, 0.4179f) }, // icon border  -> #adadad
		{ FLinearColor(0.132f, 0.132f, 0.132f),     FLinearColor(0.4179f, 0.4179f, 0.4179f) }, // box outline  #666666 -> #adadad
		{ FLinearColor(0.100f, 0.330f, 0.661f),     FLinearColor(0.0f, 0.1356f, 0.5271f) },    // accent       #5a9bd5 -> #0067c0
		// CR item C (SYSTEMIC_AUDIT rank-11): these two rows were hand-derived/invented "accent hover/press"
		// values with no matching token. Corrected to the authoritative GMobiusPalette button hover/press
		// tokens ({dark, light}) — button_hover_bg #e9f1fa / button_pressed_bg #d9e7f5 (light). Reversible.
		{ FLinearColor(0.09306f, 0.09306f, 0.09306f), FLinearColor(0.81485f, 0.87962f, 0.95597f) }, // button hover bg  (ButtonHoverBg token)
		{ FLinearColor(0.04971f, 0.04971f, 0.04971f), FLinearColor(0.69387f, 0.7991f, 0.9131f) },   // button pressed bg (ButtonPressedBg token)
	};

	static const FColorPair TextMap[] =
	{
		{ FLinearColor(1.0f, 1.0f, 1.0f),                    FLinearColor(0.0f, 0.0f, 0.0f) },          // playbar text  -> #000000
		{ FLinearColor(0.462077f, 0.462077f, 0.462077f),     FLinearColor(0.0578f, 0.0578f, 0.0578f) }, // header text   #b5b5b5 -> #444444
		{ FLinearColor(0.323f, 0.323f, 0.323f),              FLinearColor(0.1329f, 0.1329f, 0.1329f) }, // dim text      #9a9a9a -> #666666
		{ FLinearColor(0.323143f, 0.351533f, 0.391572f),     FLinearColor(0.1329f, 0.1329f, 0.1329f) }, // dim text (bluish variant)
		{ FLinearColor(0.625f, 0.625f, 0.625f),              FLinearColor(0.0160f, 0.0160f, 0.0160f) }, // primary text  #cfcfcf -> #222222
		{ FLinearColor(0.745f, 0.745f, 0.745f),              FLinearColor(0.0331f, 0.0331f, 0.0331f) }, // chip/button   #e0e0e0 -> #333333
		{ FLinearColor(0.6867f, 0.7084f, 0.7454f),           FLinearColor(0.0160f, 0.0160f, 0.0160f) }, // legacy body text
		{ FLinearColor(0.925f, 0.933f, 0.945f),              FLinearColor(0.0160f, 0.0160f, 0.0160f) }, // bright mono   #e6e8eb -> #222222
		{ FLinearColor(0.100f, 0.330f, 0.661f),              FLinearColor(0.0f, 0.13563f, 0.52712f) },  // accent text/link (CR item D) #5a9bd5 <-> #0067c0
	};

	static bool NearlyEqual(const FLinearColor& A, const FLinearColor& B)
	{
		constexpr float Epsilon = 0.012f;
		return FMath::Abs(A.R - B.R) < Epsilon
			&& FMath::Abs(A.G - B.G) < Epsilon
			&& FMath::Abs(A.B - B.B) < Epsilon;
	}

	static bool Remap(FLinearColor& InOut, const bool bLight, const TArrayView<const FColorPair> Map,
	                  const bool bGuardNeutralWhite = true)
	{
		// Pure white is the neutral multiplier on brushes/brush-colors project-wide (the UBorder
		// double-tint convention) — never a surface role. Text maps opt out: white text IS a role.
		if (bGuardNeutralWhite && InOut.R > 0.99f && InOut.G > 0.99f && InOut.B > 0.99f)
		{
			return false;
		}
		for (const FColorPair& Pair : Map)
		{
			const FLinearColor& From = bLight ? Pair.Dark : Pair.Light;
			const FLinearColor& To = bLight ? Pair.Light : Pair.Dark;
			if (NearlyEqual(InOut, From))
			{
				const float Alpha = InOut.A;
				InOut = To;
				InOut.A = Alpha;
				return true;
			}
		}
		return false;
	}

	static bool RemapSlate(FSlateColor& InOut, const bool bLight, const TArrayView<const FColorPair> Map,
	                       const bool bGuardNeutralWhite = true)
	{
		if (!InOut.IsColorSpecified())
		{
			return false;
		}
		FLinearColor Color = InOut.GetSpecifiedColor();
		if (Remap(Color, bLight, Map, bGuardNeutralWhite))
		{
			InOut = FSlateColor(Color);
			return true;
		}
		return false;
	}

	/** Tints, outlines and the DarkTheme<->LightTheme chrome material swap for one brush. */
	static bool RemapBrush(FSlateBrush& Brush, const bool bLight)
	{
		bool bChanged = false;
		FSlateColor Tint = Brush.TintColor;
		if (RemapSlate(Tint, bLight, SurfaceMap))
		{
			Brush.TintColor = Tint;
			bChanged = true;
		}
		FSlateColor Outline = Brush.OutlineSettings.Color;
		if (RemapSlate(Outline, bLight, SurfaceMap))
		{
			Brush.OutlineSettings.Color = Outline;
			bChanged = true;
		}
		if (const UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject()))
		{
			const TCHAR* From = bLight ? TEXT("/Master/Instances/DarkTheme/") : TEXT("/Master/Instances/LightTheme/");
			const TCHAR* To = bLight ? TEXT("/Master/Instances/LightTheme/") : TEXT("/Master/Instances/DarkTheme/");
			FString Path = Material->GetPathName();
			if (Path.Contains(From))
			{
				Path.ReplaceInline(From, To);
				if (UMaterialInterface* Swapped = Cast<UMaterialInterface>(FSoftObjectPath(Path).TryLoad()))
				{
					Brush.SetResourceObject(Swapped);
					bChanged = true;
				}
			}
		}
		return bChanged;
	}

	/** Bottom-bar icon materials expose glyph/background/border params — theme via a dynamic instance. */
	static bool ThemeIconBrush(FSlateBrush& Brush, UObject* MidOuter, const bool bLight)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject());
		if (!Material)
		{
			return false;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Material);
		const FString SourcePath = Mid ? (Mid->Parent ? Mid->Parent->GetPathName() : FString()) : Material->GetPathName();
		if (!SourcePath.Contains(TEXT("MobiusBottomBarIconMats")))
		{
			return false;
		}
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(Material, MidOuter);
			Brush.SetResourceObject(Mid);
		}
		// Reset all overrides first — only the three COLOUR params are themed. Other params
		// ("Inset Inner Button Texture", "TextureSize", ...) are geometry and must stay at the
		// parent instance's values.
		Mid->ClearParameterValues();
		// Glyph tint = icon_tint token per theme (D24): light #3b3b3b, dark #d9d9d9 (was #0a0a0a / pure white).
		Mid->SetVectorParameterValue(TEXT("Texture Colour"), PaletteColor(EMobiusPaletteRole::IconTint, bLight));
		Mid->SetVectorParameterValue(TEXT("BackgroundColour"), bLight ? FLinearColor(0.9131f, 0.9131f, 0.9131f) : FLinearColor(0.007f, 0.007f, 0.009f));
		// BW7 (D135/Q55 — OWNER RULING): the play/pause accent-ring mockup design is OVERRULED. The play/pause
		// MIDs get the SAME grey border as every other bottom-bar icon (no accent BorderColour, no pinned
		// BorderThickness). The former BW3/D86 special-case (accent ring + thickness 2) is removed so a theme
		// flip can never re-introduce the ring. Do NOT restore the ring without an owner say-so.
		Mid->SetVectorParameterValue(TEXT("BorderColour"), bLight ? FLinearColor(0.4179f, 0.4179f, 0.4179f) : FLinearColor(0.172f, 0.172f, 0.172f));
		return true;
	}

	/**
	 * Panel/popup/bar backgrounds all instance M_WidgetBackground (BackgroundMaterials folder):
	 * cog popup (MI_PlayBarBackground), flow-counter/floor-stats header bars, loading + egress
	 * panels. Light: retint via a dynamic instance, preserving each param's authored ALPHA (some
	 * variants are deliberately transparent). Dark: clearing the overrides restores the parent
	 * instance's authored values exactly.
	 */
	static bool ThemeBackgroundBrush(FSlateBrush& Brush, UObject* MidOuter, const bool bLight)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject());
		if (!Material)
		{
			return false;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Material);
		const FString SourcePath = Mid ? (Mid->Parent ? Mid->Parent->GetPathName() : FString()) : Material->GetPathName();
		if (!SourcePath.Contains(TEXT("/WidgetMaterials/BackgroundMaterials/")))
		{
			return false;
		}
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(Material, MidOuter);
			Brush.SetResourceObject(Mid);
		}
		if (bLight)
		{
			// Override ONLY the two colour params — clearing everything here would also drop the
			// parent's scalar overrides (opacity, corner radius) and popups turn translucent.
			FLinearColor Background = FLinearColor::Black;
			FLinearColor BorderTint = FLinearColor::White;
			Mid->Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Background Color Tint")), Background);
			Mid->Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Border Color Tint")), BorderTint);
			Mid->SetVectorParameterValue(TEXT("Background Color Tint"), FLinearColor(0.9131f, 0.9131f, 0.9131f, Background.A));
			Mid->SetVectorParameterValue(TEXT("Border Color Tint"), FLinearColor(0.4179f, 0.4179f, 0.4179f, BorderTint.A));
		}
		else
		{
			// Dropping the overrides restores the parent instance's authored values exactly.
			Mid->ClearParameterValues();
		}
		return true;
	}

	/**
	 * Agent-visibility pill toggle (D51): M_RadialToggleButton / MI_AgentToggleViewer exposes
	 * InnerTrackColourOn / InnerTrackColourOff / ThumbColour. Theme them per CurrentTheme:
	 * On = accent, Off = slider_track, Thumb = white. NEVER ClearParameterValues here — the pill's
	 * SliderState (BP-driven on/off) and geometry params must survive; we only override the 3 colours.
	 */
	static bool ThemePillBrush(FSlateBrush& Brush, UObject* MidOuter, const bool bLight)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject());
		if (!Material)
		{
			return false;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Material);
		const FString SourcePath = Mid ? (Mid->Parent ? Mid->Parent->GetPathName() : FString()) : Material->GetPathName();
		if (!SourcePath.Contains(TEXT("RadialToggleButton")) && !SourcePath.Contains(TEXT("AgentToggleViewer")))
		{
			return false;
		}
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(Material, MidOuter);
			Brush.SetResourceObject(Mid);
		}
		Mid->SetVectorParameterValue(TEXT("InnerTrackColourOn"), PaletteColor(EMobiusPaletteRole::Accent, bLight));
		Mid->SetVectorParameterValue(TEXT("InnerTrackColourOff"), PaletteColor(EMobiusPaletteRole::SliderTrack, bLight));
		Mid->SetVectorParameterValue(TEXT("ThumbColour"), FLinearColor::White);
		return true;
	}
}

void UUIThemeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// LIGHT is the product default; the saved choice in UUserProjectSettings (the project's
	// GameUserSettings class) overrides it. Widgets are not constructed yet —
	// UThemeToggleWidget::NativeConstruct triggers the deferred ReapplyTheme() that actually
	// paints a non-dark theme onto the UI.
	const UUserProjectSettings* Settings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	CurrentTheme = (!Settings || Settings->GetUseLightUITheme()) ? EMobiusUITheme::Light : EMobiusUITheme::Dark;

	// Retint the SHARED styles before any widget constructs: several Slate widgets (ButtonWithText
	// labels) copy their style at construction, so the first paint must already be themed.
	ApplySharedStyles(CurrentTheme == EMobiusUITheme::Light);

	// D172: reliably re-theme the LIVE widgets once the UI is up. Widgets (ribbon tabs, lazily-built
	// panels) construct AFTER this subsystem initialises, and the only other startup re-theme
	// (UThemeToggleWidget::NativeConstruct) can fire before them or live in a popup that is closed at
	// launch — leaving late labels at their white design-time colour until the user clicks. Re-run the
	// live-widget pass a handful of times over the first few seconds so late widgets pick up the saved
	// theme. Each pass no-ops until a game world + widgets exist. Light only (dark is the design-time
	// default — nothing to repaint). One-shot: the ticker unregisters itself after the last pass.
	if (CurrentTheme == EMobiusUITheme::Light)
	{
		const TSharedRef<int32> Passes = MakeShared<int32>(0);
		const TSharedRef<int32> ThemedPasses = MakeShared<int32>(0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Passes, ThemedPasses](float) -> bool
		{
			// Returns the number of live leaf widgets themed; >0 means the UI has actually constructed
			// (it can appear seconds after launch, behind shader compilation). Keep re-applying until we
			// have themed a live UI a few times (settle late stragglers), then stop. Hard cap ~30s.
			if (ApplyToLiveWidgets(CurrentTheme == EMobiusUITheme::Light) > 0)
			{
				++(*ThemedPasses);
				// Force a repaint — labels re-coloured behind an InvalidationBox (ribbon tabs) keep their
				// cached white paint until invalidated (this is what a tab CLICK does via ApplyTheme).
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().InvalidateAllWidgets(false);
				}
			}
			return (*ThemedPasses < 3) && (++(*Passes) < 100);
		}), 0.3f);
	}
}

void UUIThemeSubsystem::SetTheme(const EMobiusUITheme NewTheme)
{
	CurrentTheme = NewTheme;
	ApplyTheme(CurrentTheme == EMobiusUITheme::Light);

	// Persist through the project's GameUserSettings so the choice survives sessions alongside
	// the other user preferences (logger flags, render tier, UI scale).
	if (UUserProjectSettings* Settings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
	{
		Settings->SetUseLightUITheme(CurrentTheme == EMobiusUITheme::Light);
	}
}

void UUIThemeSubsystem::ToggleTheme()
{
	SetTheme(CurrentTheme == EMobiusUITheme::Dark ? EMobiusUITheme::Light : EMobiusUITheme::Dark);
}

void UUIThemeSubsystem::ReapplyTheme()
{
	ApplyTheme(CurrentTheme == EMobiusUITheme::Light);
}

FLinearColor UUIThemeSubsystem::GetPaletteColor(const EMobiusPaletteRole Role) const
{
	return MobiusTheme::PaletteColor(Role, CurrentTheme == EMobiusUITheme::Light);
}

FLinearColor UUIThemeSubsystem::GetPaletteColorForTheme(const EMobiusPaletteRole Role, const EMobiusUITheme Theme) const
{
	return MobiusTheme::PaletteColor(Role, Theme == EMobiusUITheme::Light);
}

UMaterialInterface* UUIThemeSubsystem::GetThemedTabMaterial(const bool bSelected) const
{
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;
	const TCHAR* ThemeFolder = bLight ? TEXT("LightTheme") : TEXT("DarkTheme");
	const TCHAR* Name = bSelected ? TEXT("MI_TabSelected") : TEXT("MI_TabDefault");
	const FString Path = FString::Printf(
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/Master/Instances/%s/%s.%s"),
		ThemeFolder, Name, Name);
	return LoadObject<UMaterialInterface>(nullptr, *Path);
}

FButtonStyle UUIThemeSubsystem::GetThemedTabStyle(const bool bSelected) const
{
	using namespace MobiusTheme;
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;

	// Base off the shared tab SWS so padding / sound / non-brush params stay identical to today.
	FButtonStyle Style;
	if (const USlateWidgetStyleAsset* TabStyleAsset = LoadObject<USlateWidgetStyleAsset>(nullptr,
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/SlateStyleSheets/UI_Styles/SWS_SettingButtonStyle.SWS_SettingButtonStyle")))
	{
		if (const FButtonStyle* Base = TabStyleAsset->GetStyle<FButtonStyle>())
		{
			Style = *Base;
		}
	}

	// Swap each state's brush to the themed tab material (selected vs default) for the current theme.
	if (UMaterialInterface* TabMaterial = GetThemedTabMaterial(bSelected))
	{
		FSlateBrush* Brushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
		for (FSlateBrush* Brush : Brushes)
		{
			Brush->SetResourceObject(TabMaterial);
			Brush->TintColor = FSlateColor(FLinearColor::White); // material carries the colour; tint neutral
		}
	}

	// Foreground: selected tab = accent text (light) / bright (dark); inactive = muted.
	const FLinearColor SelectedFg = PaletteColor(EMobiusPaletteRole::TabActiveText, bLight);
	const FLinearColor InactiveFg = PaletteColor(EMobiusPaletteRole::TabInactiveText, bLight);
	const FSlateColor Fg = FSlateColor(bSelected ? SelectedFg : InactiveFg);
	Style.NormalForeground = Fg;
	Style.HoveredForeground = FSlateColor(SelectedFg);
	Style.PressedForeground = FSlateColor(SelectedFg);
	Style.DisabledForeground = Fg;
	return Style;
}

FWindowStyle UUIThemeSubsystem::GetThemedWindowStyle() const
{
	using namespace MobiusTheme;
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;

	FWindowStyle Style = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	const FLinearColor TitleBg = PaletteColor(EMobiusPaletteRole::TitlebarBg, bLight);
	const FLinearColor Border = PaletteColor(EMobiusPaletteRole::WindowBorder, bLight);
	const FLinearColor TitleText = PaletteColor(EMobiusPaletteRole::TitlebarText, bLight);

	Style.ActiveTitleBrush.TintColor = FSlateColor(TitleBg);
	Style.InactiveTitleBrush.TintColor = FSlateColor(TitleBg);
	Style.FlashTitleBrush.TintColor = FSlateColor(TitleBg);
	Style.BorderBrush.TintColor = FSlateColor(Border);
	Style.BackgroundBrush.TintColor = FSlateColor(TitleBg);
	Style.OutlineBrush.TintColor = FSlateColor(Border);
	Style.TitleTextStyle.ColorAndOpacity = FSlateColor(TitleText);
	return Style;
}

void UUIThemeSubsystem::ApplyTheme(const bool bLight)
{
	ApplySharedStyles(bLight);
	ApplyToLiveWidgets(bLight);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}
}

int32 UUIThemeSubsystem::ApplyToLiveWidgets(const bool bLight)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return 0;
	}

	// TopLevelOnly=false returns every live UUserWidget, embedded ones included, so each widget
	// only needs its OWN tree walked (embedded user widgets are skipped as tree nodes below).
	TArray<UUserWidget*> AllUserWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, AllUserWidgets, UUserWidget::StaticClass(), false);
	int32 WidgetsVisited = 0;
	for (UUserWidget* UserWidget : AllUserWidgets)
	{
		if (!UserWidget || !UserWidget->WidgetTree)
		{
			continue;
		}
		UserWidget->WidgetTree->ForEachWidget([this, bLight, &WidgetsVisited](UWidget* Widget)
		{
			if (Widget && !Widget->IsA<UUserWidget>())
			{
				ApplyToWidget(Widget, bLight);
				++WidgetsVisited;
			}
		});
	}
	UE_LOG(LogMobiusTheme, Display, TEXT("ApplyToLiveWidgets(%s): %d user widgets, %d leaf widgets visited"),
		bLight ? TEXT("light") : TEXT("dark"), AllUserWidgets.Num(), WidgetsVisited);

	return WidgetsVisited;
}

void UUIThemeSubsystem::ApplyToWidget(UWidget* Widget, const bool bLight)
{
	using namespace MobiusTheme;

	// Roles the value walker can't reach are set explicitly per theme by widget name FIRST; if a name
	// matches we fully own that widget and skip the generic walk (which would otherwise mis-remap it,
	// e.g. a header hairline #4a4a4a colliding with the slider-track SurfaceMap row).
	if (ApplyNameRoleOverride(Widget, bLight))
	{
		return;
	}

	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		FLinearColor BrushColor = Border->GetBrushColor();
		if (Remap(BrushColor, bLight, SurfaceMap))
		{
			Border->SetBrushColor(BrushColor);
		}
		FSlateBrush Brush = Border->Background;
		bool bChanged = RemapBrush(Brush, bLight);
		bChanged |= ThemeBackgroundBrush(Brush, Border, bLight);
		// D169: kill the UBorder double-tint. SBorder paints Background.TintColor * BrushColor
		// (SBorder::OnPaint) — a border carrying a non-white colour in BOTH fields multiplies them and
		// renders far too dark (~0.63x in light, near-black in dark; e.g. RailBg / RailRightBorder are
		// authored 0.7913 in both). Collapse to a single multiplier: BrushColor keeps the themed colour
		// (the SurfaceMap-remapped primary), the brush tint goes neutral white. Guarded to plain colour
		// fills (no texture/material resource) and only when BOTH are non-white — so convention-A
		// (colour + white tint) and convention-B (white BrushColor + colour tint) borders, and all
		// material-backed chrome, are left untouched. Runs each theme pass; the asset is not mutated.
		if (Brush.GetResourceObject() == nullptr
			&& !Border->GetBrushColor().Equals(FLinearColor::White, 0.02f)
			&& !Brush.TintColor.GetSpecifiedColor().Equals(FLinearColor::White, 0.02f))
		{
			Brush.TintColor = FSlateColor(FLinearColor::White);
			bChanged = true;
		}
		if (bChanged)
		{
			Border->SetBrush(Brush);
		}
	}
	else if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		FSlateColor Color = Text->GetColorAndOpacity();
		if (RemapSlate(Color, bLight, TextMap, /*bGuardNeutralWhite*/ false))
		{
			Text->SetColorAndOpacity(Color);
		}
	}
	else if (USlider* Slider = Cast<USlider>(Widget))
	{
		if (Slider->GetName() == TEXT("PlaybackSlider"))
		{
			// Q51/C4 (CR item B): the scrub track + accent-@35% fill are now drawn by a UProgressBar
			// (ScrubFillBar) layered BEHIND this slider, so the slider itself must be bar-transparent —
			// only its accent thumb shows on top. Forced here (a plain transparent value would otherwise
			// be pulled to grey by the SurfaceMap bottom-bar bucket on the value walk).
			Slider->SetSliderBarColor(FLinearColor::Transparent);
		}
		else
		{
			// Track stays on the value-walk so gradient/data bars (HSV colour pickers) are not flattened
			// to grey; only greyscale tracks that match a SurfaceMap row are remapped.
			FLinearColor Bar = Slider->GetSliderBarColor();
			if (Remap(Bar, bLight, SurfaceMap))
			{
				Slider->SetSliderBarColor(Bar);
			}
		}
		// Q24: force the thumb to the accent per theme (design: slider thumb = accent). Not a value
		// remap — the stock thumbs are grey and match no accent bucket, so set it explicitly.
		Slider->SetSliderHandleColor(PaletteColor(EMobiusPaletteRole::SliderThumb, bLight));
		// D171: neutralize the slider double-tint. SetSliderHandleColor (above) and the bar colour are
		// multipliers applied ON TOP of the style's brush TintColors. A slider that baked the accent/track
		// INTO its brush tint (e.g. OpacitySlider: thumb tint = accent) multiplies twice -> accent^2 (a
		// dark navy handle) / dark track. Force the style brush tints to white so the colour multipliers
		// render cleanly. Thumbs always (flat); bars only when resource-less (leave gradient / HSV-picker
		// bars, which legitimately carry their colour in the brush, untouched).
		FSliderStyle SliderStyle = Slider->GetWidgetStyle();
		bool bSliderStyleChanged = false;
		FSlateBrush* ThumbBrushes[] = { &SliderStyle.NormalThumbImage, &SliderStyle.HoveredThumbImage, &SliderStyle.DisabledThumbImage };
		for (FSlateBrush* ThumbBrush : ThumbBrushes)
		{
			if (!ThumbBrush->TintColor.GetSpecifiedColor().Equals(FLinearColor::White, 0.02f))
			{
				ThumbBrush->TintColor = FSlateColor(FLinearColor::White);
				bSliderStyleChanged = true;
			}
		}
		FSlateBrush* BarBrushes[] = { &SliderStyle.NormalBarImage, &SliderStyle.HoveredBarImage, &SliderStyle.DisabledBarImage };
		for (FSlateBrush* BarBrush : BarBrushes)
		{
			if (BarBrush->GetResourceObject() == nullptr
				&& !BarBrush->TintColor.GetSpecifiedColor().Equals(FLinearColor::White, 0.02f))
			{
				BarBrush->TintColor = FSlateColor(FLinearColor::White);
				bSliderStyleChanged = true;
			}
		}
		if (bSliderStyleChanged)
		{
			Slider->SetWidgetStyle(SliderStyle);
		}
	}
	else if (UCheckBox* CheckBox = Cast<UCheckBox>(Widget))
	{
		FCheckBoxStyle Style = CheckBox->GetWidgetStyle();
		bool bChanged = false;
		FSlateBrush* Brushes[] =
		{
			&Style.UncheckedImage, &Style.UncheckedHoveredImage, &Style.UncheckedPressedImage,
			&Style.CheckedImage, &Style.CheckedHoveredImage, &Style.CheckedPressedImage,
			&Style.UndeterminedImage, &Style.UndeterminedHoveredImage, &Style.UndeterminedPressedImage,
		};
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
		}

		// Q24 (C4): checkbox checked = accent fill; unchecked = input-bg box + checkbox border, radius 3.
		// The ThemeToggle is a bespoke pill/slider control — leave its brushes to the value walk.
		if (!Widget->GetName().Contains(TEXT("ThemeToggle")))
		{
			auto ApplyRoundedBox = [](FSlateBrush& B, const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth)
			{
				// Mutate in place so the asset's authored ImageSize (D43 = 20x20) survives.
				B.DrawAs = ESlateBrushDrawType::RoundedBox;
				B.SetResourceObject(nullptr);
				B.TintColor = FSlateColor(Fill);
				B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
				B.OutlineSettings.CornerRadii = FVector4(3.0, 3.0, 3.0, 3.0);
				B.OutlineSettings.Color = FSlateColor(Outline);
				B.OutlineSettings.Width = OutlineWidth;
			};
			const FLinearColor Accent = PaletteColor(EMobiusPaletteRole::CheckboxCheckedBg, bLight);
			const FLinearColor BoxBg = PaletteColor(EMobiusPaletteRole::CheckboxBg, bLight);
			const FLinearColor BoxBorder = PaletteColor(EMobiusPaletteRole::CheckboxBorder, bLight);
			// Checked = accent fill + white 1u outline (accent-on signal; the white-check glyph needs a
			// composite/checkmark brush asset — logged as the remaining Q24 limitation).
			ApplyRoundedBox(Style.CheckedImage, Accent, FLinearColor::White, 1.0f);
			ApplyRoundedBox(Style.CheckedHoveredImage, Accent, FLinearColor::White, 1.0f);
			ApplyRoundedBox(Style.CheckedPressedImage, Accent, FLinearColor::White, 1.0f);
			ApplyRoundedBox(Style.UncheckedImage, BoxBg, BoxBorder, 1.0f);
			ApplyRoundedBox(Style.UncheckedHoveredImage, BoxBg, BoxBorder, 1.0f);
			ApplyRoundedBox(Style.UncheckedPressedImage, BoxBg, BoxBorder, 1.0f);
			bChanged = true;
		}

		if (bChanged)
		{
			CheckBox->SetWidgetStyle(Style);
		}
	}
	else if (UButton* Button = Cast<UButton>(Widget))
	{
		FButtonStyle Style = Button->GetStyle();
		bool bChanged = false;
		FSlateBrush* Brushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
			bChanged |= ThemeIconBrush(*Brush, Button, bLight);
			bChanged |= ThemeBackgroundBrush(*Brush, Button, bLight);
			bChanged |= ThemePillBrush(*Brush, Button, bLight); // agent-visibility pill toggle (D51)
		}
		bChanged |= RemapSlate(Style.NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		if (bChanged)
		{
			Button->SetStyle(Style);
		}
		// Some buttons (floor-stat bars et al) get their colour from UButton::BackgroundColor,
		// which MULTIPLIES the (white) style brushes — remap it too or they stay dark.
		FLinearColor ButtonBackground = Button->GetBackgroundColor();
		if (Remap(ButtonBackground, bLight, SurfaceMap))
		{
			Button->SetBackgroundColor(ButtonBackground);
		}
		// ButtonWithText labels bake their style at construction (STextBlock copies it) — re-push
		// so they pick up the retinted "Mobius.Text.Label" / SWS text styles.
		if (UButtonWithText* ButtonWithText = Cast<UButtonWithText>(Widget))
		{
			ButtonWithText->RefreshTextStyle();

			// Q49/R4: RefreshTextStyle()/SetTextStyle re-pushes the style struct but does NOT re-land the
			// STextBlock's resolved ColorAndOpacity, so tab + Browse labels keep their light-mode colour in
			// dark theme (D118). Re-land it directly.
			const FString BtnName = ButtonWithText->GetName();
			const bool bIsRibbonTab =
				BtnName.Contains(TEXT("FilesPanelBtn")) ||
				BtnName.Contains(TEXT("DisplaylPanelBTN")) || // sic: asset typo (D13)
				BtnName.Contains(TEXT("HelpPanelBtn"));

			if (bIsRibbonTab)
			{
				// D173: ribbon tabs are theme-MANAGED regardless of any custom SWS text style. This used to
				// be gated behind (MobiusButtonTextStyle == null), so tabs carrying a custom text style never
				// had their label colour re-landed here — only the ACTIVE tab got coloured (by the ribbon's
				// activation BP), leaving INACTIVE tabs at their white design-time colour until clicked. Set
				// the colour directly (the custom font/size still comes from RefreshTextStyle above).
				// Active tab = current Normal-brush material named "TabSelected" (name-based so it holds
				// whichever theme folder the tab-material swap left on the brush).
				const UObject* NormalRes = Style.Normal.GetResourceObject();
				const bool bActive = NormalRes && NormalRes->GetName().Contains(TEXT("TabSelected"));
				ButtonWithText->ApplyThemedLabelColor(PaletteColor(
					bActive ? EMobiusPaletteRole::TabActiveText : EMobiusPaletteRole::TabInactiveText,
					bLight));
			}
			else if (!ButtonWithText->MobiusButtonTextStyle)
			{
				// Browse et al (no custom SWS text style): mirror the shared Mobius.Text.Label colour
				// ApplySharedStyles just set. Buttons carrying a custom SWS text style own their colour.
				const FSlateColor LabelColor =
					FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label").ColorAndOpacity;
				ButtonWithText->ApplyThemedLabelColor(LabelColor.GetSpecifiedColor());
			}
		}
	}
	else if (UComboBoxString* ComboBox = Cast<UComboBoxString>(Widget))
	{
		FComboBoxStyle Style = ComboBox->GetWidgetStyle();
		bool bChanged = false;
		FSlateBrush* Brushes[] =
		{
			&Style.ComboButtonStyle.ButtonStyle.Normal, &Style.ComboButtonStyle.ButtonStyle.Hovered,
			&Style.ComboButtonStyle.ButtonStyle.Pressed, &Style.ComboButtonStyle.ButtonStyle.Disabled,
			&Style.ComboButtonStyle.MenuBorderBrush, &Style.ComboButtonStyle.DownArrowImage,
		};
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
			// The dark combo uses a pure-white 1px outline (which the neutral-white guard
			// protects) — swap it explicitly for a readable border on the light chrome.
			FSlateColor Outline = Brush->OutlineSettings.Color;
			if (Outline.IsColorSpecified())
			{
				const FLinearColor OutlineColor = Outline.GetSpecifiedColor();
				if (bLight && OutlineColor.R > 0.99f && OutlineColor.G > 0.99f && OutlineColor.B > 0.99f)
				{
					Brush->OutlineSettings.Color = FSlateColor(FLinearColor(0.1945f, 0.1945f, 0.1945f)); // #7a7a7a
					bChanged = true;
				}
				else if (!bLight && FMath::IsNearlyEqual(OutlineColor.R, 0.1945f, 0.012f)
					&& FMath::IsNearlyEqual(OutlineColor.G, 0.1945f, 0.012f)
					&& FMath::IsNearlyEqual(OutlineColor.B, 0.1945f, 0.012f)
					&& Brush != &Style.ComboButtonStyle.MenuBorderBrush)
				{
					Brush->OutlineSettings.Color = FSlateColor(FLinearColor::White);
					bChanged = true;
				}
			}
		}
		bChanged |= RemapSlate(Style.ComboButtonStyle.ButtonStyle.NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.ComboButtonStyle.ButtonStyle.HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.ComboButtonStyle.ButtonStyle.PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		if (bChanged)
		{
			ComboBox->SetWidgetStyle(Style);
		}
		// Dropdown MENU rows + item text (FTableRowStyle) — explicit per theme; the authored values
		// are dark-only.
		{
			FTableRowStyle Items = ComboBox->GetItemStyle();
			const FLinearColor RowBg = bLight ? FLinearColor(0.964f, 0.964f, 0.964f) : FLinearColor(0.010f, 0.012f, 0.015f);
			const FLinearColor RowHover = bLight ? FLinearColor(0.7913f, 0.7913f, 0.7913f) : FLinearColor(0.068f, 0.068f, 0.068f);
			const FLinearColor RowSelected = bLight ? FLinearColor(0.0f, 0.1356f, 0.5271f) : FLinearColor(0.100f, 0.330f, 0.661f);
			auto SetRowBrush = [](FSlateBrush& InBrush, const FLinearColor& InColor)
			{
				InBrush.TintColor = FSlateColor(InColor);
				InBrush.DrawAs = ESlateBrushDrawType::Image;
				InBrush.SetResourceObject(nullptr);
			};
			SetRowBrush(Items.EvenRowBackgroundBrush, RowBg);
			SetRowBrush(Items.OddRowBackgroundBrush, RowBg);
			SetRowBrush(Items.EvenRowBackgroundHoveredBrush, RowHover);
			SetRowBrush(Items.OddRowBackgroundHoveredBrush, RowHover);
			SetRowBrush(Items.ActiveBrush, RowSelected);
			SetRowBrush(Items.ActiveHoveredBrush, RowSelected);
			SetRowBrush(Items.InactiveBrush, RowBg);
			SetRowBrush(Items.InactiveHoveredBrush, RowHover);
			Items.TextColor = FSlateColor(bLight ? FLinearColor(0.016f, 0.016f, 0.016f) : FLinearColor(0.625f, 0.625f, 0.625f));
			Items.SelectedTextColor = FSlateColor(FLinearColor::White);
			ComboBox->SetItemStyle(Items);

			FComboBoxStyle MenuStyle = ComboBox->GetWidgetStyle();
			MenuStyle.ComboButtonStyle.MenuBorderBrush.TintColor = FSlateColor(RowBg);
			ComboBox->SetWidgetStyle(MenuStyle);
		}
		// Themed item/content generator — the default one bakes construction-time colours.
		ComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UUIThemeSubsystem::HandleGenerateThemedComboEntry);
	}
	else if (UImage* Image = Cast<UImage>(Widget))
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FSlateBrush Brush = Image->Brush;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		bool bChanged = RemapBrush(Brush, bLight);
		bChanged |= ThemeIconBrush(Brush, Image, bLight);
		bChanged |= ThemeBackgroundBrush(Brush, Image, bLight);
		bChanged |= ThemePillBrush(Brush, Image, bLight); // agent-visibility pill toggle (D51)
		if (bChanged)
		{
			Image->SetBrush(Brush);
		}
	}
	else if (UProgressBar* ProgressBar = Cast<UProgressBar>(Widget))
	{
		if (ProgressBar->GetName() == TEXT("ScrubFillBar"))
		{
			// Q51/C4 (CR item B): scrub fill behind PlaybackSlider — a flat SliderTrack-grey track with an
			// accent @35%-alpha fill (NOT the §3.8 loading-bar look). Both themes via palette. The fill is
			// white-tinted in the style and coloured via FillColorAndOpacity so the 0.35 alpha is exact.
			FProgressBarStyle Style = ProgressBar->GetWidgetStyle();
			Style.EnableFillAnimation = false;
			Style.BackgroundImage.DrawAs = ESlateBrushDrawType::Box;
			Style.BackgroundImage.SetResourceObject(nullptr);
			Style.BackgroundImage.OutlineSettings.Width = 0.0f;
			Style.BackgroundImage.TintColor = FSlateColor(PaletteColor(EMobiusPaletteRole::SliderTrack, bLight));
			Style.FillImage.DrawAs = ESlateBrushDrawType::Box;
			Style.FillImage.SetResourceObject(nullptr);
			Style.FillImage.OutlineSettings.Width = 0.0f;
			Style.FillImage.TintColor = FSlateColor(FLinearColor::White);
			ProgressBar->SetWidgetStyle(Style);
			FLinearColor Fill = PaletteColor(EMobiusPaletteRole::Accent, bLight);
			Fill.A = 0.35f;
			ProgressBar->SetFillColorAndOpacity(Fill);
			return;
		}
		// §3.8 (D55): progress bars = accent fill on an input-bg track with a 1u input-border. The fill
		// image is a material/texture on some bars — tint it to accent (keeps any authored shape); the
		// background becomes a rounded input-bg box. Height (11u) is asset geometry, not style.
		FProgressBarStyle Style = ProgressBar->GetWidgetStyle();
		Style.FillImage.TintColor = FSlateColor(PaletteColor(EMobiusPaletteRole::Accent, bLight));
		Style.BackgroundImage.DrawAs = ESlateBrushDrawType::RoundedBox;
		Style.BackgroundImage.SetResourceObject(nullptr);
		Style.BackgroundImage.TintColor = FSlateColor(PaletteColor(EMobiusPaletteRole::InputBg, bLight));
		Style.BackgroundImage.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Style.BackgroundImage.OutlineSettings.CornerRadii = FVector4(3.0, 3.0, 3.0, 3.0);
		Style.BackgroundImage.OutlineSettings.Color = FSlateColor(PaletteColor(EMobiusPaletteRole::InputBorder, bLight));
		Style.BackgroundImage.OutlineSettings.Width = 1.0f;
		ProgressBar->SetWidgetStyle(Style);
		ProgressBar->SetFillColorAndOpacity(PaletteColor(EMobiusPaletteRole::Accent, bLight));
	}
	else if (UEditableTextBox* EditBox = Cast<UEditableTextBox>(Widget))
	{
		// Q26: numeric/path edit boxes → Font_Inter Mono 14 (input_mono token). Font is
		// theme-independent, but the walk is the only C++ hook that touches every live widget, and
		// FEditableTextBoxStyle.Font is not settable from Python (protected). Convert only boxes not
		// already on Inter, so a deliberate Inter face/size is never re-stomped (idempotent on toggle).
		if (UFont* Inter = GetInterFont())
		{
			if (EditBox->WidgetStyle.TextStyle.Font.FontObject != Inter)
			{
				EditBox->WidgetStyle.TextStyle.Font = FSlateFontInfo(Inter, 11, FName(TEXT("Mono"))); // BW6 density: 14->11
				EditBox->SynchronizeProperties(); // push the style to the live SEditableTextBox
			}
		}
	}
}

bool UUIThemeSubsystem::ApplyNameRoleOverride(UWidget* Widget, const bool bLight)
{
	using namespace MobiusTheme;

	UBorder* Border = Cast<UBorder>(Widget);
	if (!Border)
	{
		return false;
	}

	const FString Name = Widget->GetName();
	for (const FNameRole& Entry : GNameRoleMap)
	{
		if (Name.Contains(Entry.Substr))
		{
			const FLinearColor Color = PaletteColor(Entry.Role, bLight);
			if (Entry.Target == EThemeRoleTarget::BorderFill)
			{
				Border->SetBrushColor(Color);
			}
			else // BorderOutline — recolour only the 1u outline; the fill is a data colour (LoS band).
			{
				FSlateBrush Brush = Border->Background;
				Brush.OutlineSettings.Color = FSlateColor(Color);
				Border->SetBrush(Brush);
			}
			return true;
		}
	}
	return false;
}

void UUIThemeSubsystem::ApplySharedStyles(const bool bLight)
{
	using namespace MobiusTheme;

	// Ribbon tab style (inactive/hover/press states) — mutate the loaded asset's style struct in
	// place; live SButtons hold a pointer to it and repaint on the InvalidateAllWidgets that follows.
	if (const USlateWidgetStyleAsset* TabStyleAsset = LoadObject<USlateWidgetStyleAsset>(nullptr,
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/SlateStyleSheets/UI_Styles/SWS_SettingButtonStyle.SWS_SettingButtonStyle")))
	{
		if (FButtonStyle* TabStyle = const_cast<FButtonStyle*>(TabStyleAsset->GetStyle<FButtonStyle>()))
		{
			TabStyle->Normal.TintColor = bLight ? FLinearColor(0.7913f, 0.7913f, 0.7913f) : FLinearColor(0.0284f, 0.0284f, 0.0284f);
			TabStyle->Hovered.TintColor = bLight ? FLinearColor(0.8500f, 0.8500f, 0.8500f) : FLinearColor(0.0370f, 0.0370f, 0.0370f);
			TabStyle->Pressed.TintColor = bLight ? FLinearColor(0.9131f, 0.9131f, 0.9131f) : FLinearColor(0.0452f, 0.0452f, 0.0452f);
			// 7a/4b tab text: inactive dim, active/hover bright(er).
			TabStyle->NormalForeground = bLight ? FLinearColor(0.1329f, 0.1329f, 0.1329f) : FLinearColor(0.323f, 0.323f, 0.323f);   // #666666 / #9a9a9a
			TabStyle->HoveredForeground = bLight ? FLinearColor(0.0160f, 0.0160f, 0.0160f) : FLinearColor(0.8228f, 0.8228f, 0.8228f); // #222222 / #eaeaea
			TabStyle->PressedForeground = TabStyle->HoveredForeground;
		}
	}

	// Sweep every Slate style/brush ASSET under the widget folder. Several widgets (playbar
	// play/pause, gear button, hide/show bar) re-copy their style from these shared assets at
	// runtime, so theming the live widget copy alone is undone on the next state change —
	// the source assets must carry the theme.
	int32 StyleAssetsThemed = 0;
	const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> StyleAssets;
	AssetRegistry.Get().GetAssetsByPath(TEXT("/Game/01_Dev/Widgets"), StyleAssets, true);
	for (const FAssetData& AssetData : StyleAssets)
	{
		if (AssetData.AssetClassPath == USlateWidgetStyleAsset::StaticClass()->GetClassPathName())
		{
			// The tab style is handled explicitly above with exact palette values.
			if (AssetData.AssetName == TEXT("SWS_SettingButtonStyle"))
			{
				continue;
			}
			USlateWidgetStyleAsset* StyleAsset = Cast<USlateWidgetStyleAsset>(AssetData.GetAsset());
			if (!StyleAsset)
			{
				continue;
			}
			bool bChanged = false;
			if (FButtonStyle* ButtonStyle = const_cast<FButtonStyle*>(StyleAsset->GetStyle<FButtonStyle>()))
			{
				FSlateBrush* Brushes[] = { &ButtonStyle->Normal, &ButtonStyle->Hovered, &ButtonStyle->Pressed, &ButtonStyle->Disabled };
				for (FSlateBrush* Brush : Brushes)
				{
					bChanged |= RemapBrush(*Brush, bLight);
					bChanged |= ThemeIconBrush(*Brush, StyleAsset, bLight);
					bChanged |= ThemeBackgroundBrush(*Brush, StyleAsset, bLight);
				}
				bChanged |= RemapSlate(ButtonStyle->NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				bChanged |= RemapSlate(ButtonStyle->HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				bChanged |= RemapSlate(ButtonStyle->PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				// Scalability/panel buttons: their dark fill shares the panel-body value so the
				// generic remap can't give them contrast — set the full style explicitly per theme.
				if (AssetData.AssetName == TEXT("SWS_PanelButtonStyle"))
				{
					ButtonStyle->Normal.TintColor = FSlateColor(bLight ? FLinearColor(0.964f, 0.964f, 0.964f) : FLinearColor(0.052861f, 0.052861f, 0.052861f));
					ButtonStyle->Hovered.TintColor = FSlateColor(bLight ? FLinearColor(0.8228f, 0.8228f, 0.8228f) : FLinearColor(0.068f, 0.068f, 0.068f));
					ButtonStyle->Pressed.TintColor = FSlateColor(bLight ? FLinearColor(0.7913f, 0.7913f, 0.7913f) : FLinearColor(0.0452f, 0.0452f, 0.0452f));
					const FLinearColor OutlineColor = bLight
						? FLinearColor(0.4179f, 0.4179f, 0.4179f)   // #adadad
						: FLinearColor(0.1023f, 0.1023f, 0.1023f);  // #5a5a5a
					for (FSlateBrush* Brush : Brushes)
					{
						// Outline WIDTH is SWS-owned geometry (the asset carries Width=1 on all four
						// brushes on disk); C++ sets only the theme-dependent outline COLOUR here. Do NOT
						// re-add a Width clobber: it re-overwrites the asset geometry and breaks the
						// owner's split (colours = C++, padding + sound + geometry = SWS asset).
						Brush->OutlineSettings.Color = FSlateColor(OutlineColor);
					}
					bChanged = true;
				}
				// The "current tier" chip: replace the baked black material with a flat rounded box
				// carrying an accent ring — readable in both themes with the shared dark/light labels.
				else if (AssetData.AssetName == TEXT("SWS_ScaleabilityButtonCurrentSet"))
				{
					const FLinearColor ChipFill = bLight ? FLinearColor(0.964f, 0.964f, 0.964f) : FLinearColor(0.0452f, 0.0452f, 0.0452f);
					const FLinearColor Accent = bLight ? FLinearColor(0.0f, 0.1356f, 0.5271f) : FLinearColor(0.100f, 0.330f, 0.661f);
					for (FSlateBrush* Brush : Brushes)
					{
						Brush->SetResourceObject(nullptr);
						Brush->DrawAs = ESlateBrushDrawType::RoundedBox;
						Brush->TintColor = FSlateColor(ChipFill);
						Brush->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
						Brush->OutlineSettings.CornerRadii = FVector4(2.0f, 2.0f, 2.0f, 2.0f);
						Brush->OutlineSettings.Color = FSlateColor(Accent);
						Brush->OutlineSettings.Width = 2.0f;
					}
					bChanged = true;
				}
				// Bottom-bar play/pause (§3.6 round-53 accent ring): the play/pause glyph is a MATERIAL brush
				// (MI_PlayButton/MI_PauseButton). Slate's RoundedBox draw type routes materials through the
				// material shader and does not apply the rounded-corner mask/outline, so converting DrawAs
				// here would give no ring at best and a dropped glyph at worst. Deferred to asset/material
				// work (round the MI background + bake an accent ring, OR a texture glyph on a RoundedBox).
				// Only the ACCENT-RING colour is queued; the C++ swap path (SetPlayButtonStyle) stays intact.
			}
			// Button LABELS come from separate text-style assets (SWS_*TextStyle) — without this
			// the light theme leaves white labels on light buttons.
			else if (FTextBlockStyle* TextStyle = const_cast<FTextBlockStyle*>(StyleAsset->GetStyle<FTextBlockStyle>()))
			{
				bChanged |= RemapSlate(TextStyle->ColorAndOpacity, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				// Q28/B8: SWS text styles (rail labels, agent-window rows, ButtonWithText labels) → Font_Inter.
				// Idempotent (only converts non-Inter faces), preserving each asset's authored size; a "Mono"
				// / "Field" / "Value" asset name picks the JetBrains-Mono face for numeric/path readouts.
				if (UFont* Inter = GetInterFont())
				{
					if (TextStyle->Font.FontObject != Inter)
					{
						const FString AssetName = AssetData.AssetName.ToString();
						const bool bMono = AssetName.Contains(TEXT("Mono")) || AssetName.Contains(TEXT("Field")) || AssetName.Contains(TEXT("Value"));
						int32 Size = FMath::RoundToInt(static_cast<float>(TextStyle->Font.Size));
						if (Size <= 0)
						{
							Size = 15;
						}
						TextStyle->Font = FSlateFontInfo(Inter, Size, FName(bMono ? TEXT("Mono") : TEXT("Regular")));
						bChanged = true;
					}
				}
			}
			StyleAssetsThemed += bChanged ? 1 : 0;
		}
		else if (AssetData.AssetClassPath == USlateBrushAsset::StaticClass()->GetClassPathName())
		{
			if (USlateBrushAsset* BrushAsset = Cast<USlateBrushAsset>(AssetData.GetAsset()))
			{
				bool bChanged = RemapBrush(BrushAsset->Brush, bLight);
				bChanged |= ThemeIconBrush(BrushAsset->Brush, BrushAsset, bLight);
				bChanged |= ThemeBackgroundBrush(BrushAsset->Brush, BrushAsset, bLight);
				StyleAssetsThemed += bChanged ? 1 : 0;
			}
		}
	}
	UE_LOG(LogMobiusTheme, Display, TEXT("ApplySharedStyles(%s): %d style assets themed"),
		bLight ? TEXT("light") : TEXT("dark"), StyleAssetsThemed);

	// "Mobius.Button" (Browse et al) — 4b light buttons are white-ish with #adadad outline, #222 label.
	FButtonStyle& MobiusButton = const_cast<FButtonStyle&>(FMobiusStyle::Get().GetWidgetStyle<FButtonStyle>("Mobius.Button"));
	// CR item C (SYSTEMIC_AUDIT rank-11): the light literals were neutral grey (Fill 0.9647 / Hover 0.8714 /
	// Press 0.7867), off the design tokens. Sourced from GMobiusPalette VERBATIM now so light hover/press are
	// the Win11-bluish button tokens (#e9f1fa / #d9e7f5); dark values are numerically identical to before.
	// Owner-checkable/reversible.
	const FLinearColor Fill  = PaletteColor(EMobiusPaletteRole::ButtonBg, bLight);
	const FLinearColor Hover = PaletteColor(EMobiusPaletteRole::ButtonHoverBg, bLight);
	const FLinearColor Press = PaletteColor(EMobiusPaletteRole::ButtonPressedBg, bLight);
	const FLinearColor Line  = PaletteColor(EMobiusPaletteRole::ButtonBorder, bLight);
	const FLinearColor Label = PaletteColor(EMobiusPaletteRole::ButtonText, bLight);
	MobiusButton.Normal.TintColor = Fill;
	MobiusButton.Hovered.TintColor = Hover;
	MobiusButton.Pressed.TintColor = Press;
	MobiusButton.Disabled.TintColor = Fill;
	MobiusButton.Normal.OutlineSettings.Color = Line;
	MobiusButton.Hovered.OutlineSettings.Color = Line;
	MobiusButton.Pressed.OutlineSettings.Color = Line;
	MobiusButton.Disabled.OutlineSettings.Color = Line;
	MobiusButton.NormalForeground = Label;
	MobiusButton.HoveredForeground = bLight ? FLinearColor::Black : FLinearColor::White;
	MobiusButton.PressedForeground = Label;

	// ButtonWithText labels (ribbon tabs, Browse) read "Mobius.Text.Label". The stock UseForeground
	// colour resolves to plain white here (the buttons' per-state foreground never reaches these
	// STextBlocks), which is unreadable on the light chrome — so pin an EXPLICIT colour per theme.
	// Explicit both ways (no capture/restore) so a stuck value self-heals on the next switch.
	FTextBlockStyle& LabelText = const_cast<FTextBlockStyle&>(FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label"));
	LabelText.ColorAndOpacity = bLight
		? FSlateColor(FLinearColor(0.0331f, 0.0331f, 0.0331f))  // #333333
		: FSlateColor(FLinearColor(0.745f, 0.745f, 0.745f));    // #e0e0e0

	// BW7/D138: the rail-button labels (Floor Stats / Flow Counter VerticalTextBlock) fall back to the
	// dedicated "Mobius.Text.RailButton" style — retint it per theme alongside "Mobius.Text.Label" so both
	// themes hold (the rails read this shared style on rebuild; they have no per-widget colour handling).
	FTextBlockStyle& RailButtonText = const_cast<FTextBlockStyle&>(FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.RailButton"));
	RailButtonText.ColorAndOpacity = LabelText.ColorAndOpacity;
}

UWidget* UUIThemeSubsystem::HandleGenerateThemedComboEntry(const FString Item)
{
	UTextBlock* Text = NewObject<UTextBlock>(this);
	Text->SetText(FText::FromString(Item));
	Text->SetFont(FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Field").Font);
	Text->SetColorAndOpacity(FSlateColor(CurrentTheme == EMobiusUITheme::Light
		? FLinearColor(0.016f, 0.016f, 0.016f)
		: FLinearColor(0.625f, 0.625f, 0.625f)));
	return Text;
}

// Dev diagnostic: dump colour-relevant state of every live ButtonWithText.
static FAutoConsoleCommandWithWorldAndArgs GMobiusDumpButtonsCmd(
	TEXT("Mobius.DumpButtons"),
	TEXT("Log colour state of live ButtonWithText widgets."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TArray<UUserWidget*> All;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), false);
		for (UUserWidget* UW : All)
		{
			if (!UW || !UW->WidgetTree)
			{
				continue;
			}
			UW->WidgetTree->ForEachWidget([](UWidget* W)
			{
				if (UButtonWithText* B = Cast<UButtonWithText>(W))
				{
					const FLinearColor CO = B->GetColorAndOpacity();
					const FLinearColor BG = B->GetBackgroundColor();
					const FSlateColor NF = B->GetStyle().NormalForeground;
					UE_LOG(LogMobiusTheme, Display, TEXT("BTN %s: ColorAndOpacity=(%.3f,%.3f,%.3f,%.2f) BackgroundColor=(%.3f,%.3f,%.3f) NormalTint=%s NormalFg=(%.3f,%.3f,%.3f)"),
						*B->GetName(), CO.R, CO.G, CO.B, CO.A, BG.R, BG.G, BG.B,
						*B->GetStyle().Normal.TintColor.GetSpecifiedColor().ToString(),
						NF.IsColorSpecified() ? NF.GetSpecifiedColor().R : -1.0f,
						NF.IsColorSpecified() ? NF.GetSpecifiedColor().G : -1.0f,
						NF.IsColorSpecified() ? NF.GetSpecifiedColor().B : -1.0f);
				}
			});
		}
	}));

// Headless/dev verification hook: Mobius.SetUITheme 0|1 from the console.
static FAutoConsoleCommandWithWorldAndArgs GMobiusSetUIThemeCmd(
	TEXT("Mobius.SetUITheme"),
	TEXT("Set the Mobius UI theme at runtime: 0 = dark, 1 = light."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			return;
		}
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* ThemeSubsystem = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				ThemeSubsystem->SetTheme(Args[0] == TEXT("1") ? EMobiusUITheme::Light : EMobiusUITheme::Dark);
			}
		}
	}));
