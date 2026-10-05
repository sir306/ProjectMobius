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
#include "Brushes/SlateColorBrush.h" // GetThemedWindowStyle: flat frame brushes (a tint cannot brighten)
#include "Brushes/SlateImageBrush.h" // StyleCheckBoxForTheme: the engine's knocked-out tick art
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
#include "Components/PanelWidget.h"   // S8: data-chip label resolution walks panel children
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/MobiusWindowButtonStyle.h"   // A18: shared DangerText close-glyph stamp
#include "Styling/CoreStyle.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Engine/Engine.h"
#include "Slate/SlateBrushAsset.h"
#include "UserConfig/UserProjectSettings.h"
#include "Style/MobiusStyle.h"
#include "Style/MobiusButtonGeometry.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "UI/Components/ButtonWithText.h"
#include "UI/Components/MobiusThemedBorder.h"   // A6b: walk guard — self-theming borders
#include "UI/Components/VerticalTextBlock.h"
#include "UI/Components/FieldAndTextWidget.h"
#include "UI/Theme/MobiusThemedUserWidget.h"   // A6b-7: recursion guard — self-theming user widgets
// A6b-5: owner-scoped role write for the play bar's background. MobiusWidgets already depends on
// ProjectMobius (see MobiusWidgets.Build.cs), so this direction is fine — the reverse is the cycle.
#include "Widgets/Simulation/SimulationPlayBar.h"
#include "Widgets/SCompoundWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusTheme, Log, All);

namespace MobiusTheme
{
	struct FColorPair
	{
		FLinearColor Dark;
		FLinearColor Light;
	};

	// AUTHORITATIVE PALETTE + FThemeColor moved to UI/Theme/MobiusThemePalette.h (owner directive
	// 2026-07-21 — theme data lives in a dedicated header). Thin forwarder keeps every existing
	// MobiusTheme::PaletteColor(...) call site unchanged; new MPC writer reads the same header table.
	static FLinearColor PaletteColor(const EMobiusPaletteRole Role, const bool bLight)
	{
		return MobiusThemePalette::Color(Role, bLight);
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

	/** First map row whose substring the widget name contains, or null. One lookup, two callers. */
	static const FNameRole* FindNameRole(const FString& WidgetName)
	{
		for (const FNameRole& Entry : GNameRoleMap)
		{
			if (WidgetName.Contains(Entry.Substr))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/**
	 * S8: a DATA CHIP is a UBorder whose fill carries a data value rather than a palette role — today only
	 * the LoS band chips. `BorderOutline` is exactly that set by construction: that target exists BECAUSE
	 * the fill is data and must not be themed, so it is the test rather than a second name list.
	 */
	static bool IsDataChipBorder(const UWidget* Widget)
	{
		const FNameRole* Entry = Widget ? FindNameRole(Widget->GetName()) : nullptr;
		return Entry && Entry->Target == EThemeRoleTarget::BorderOutline;
	}

	/** Every UTextBlock in this subtree. Stops at a nested UUserWidget — its WidgetTree is its own to theme. */
	static void CollectTextBlocks(UWidget* Widget, TArray<UTextBlock*>& Out)
	{
		if (!Widget)
		{
			return;
		}
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			Out.Add(Text);
			return;
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				CollectTextBlocks(Panel->GetChildAt(Index), Out);
			}
		}
	}

	/**
	 * S8: the band letter belonging to `Chip`. Child first (UBorder is a UContentWidget, so a letter drawn
	 * INSIDE the chip is simply its content); failing that, the sole letter under the chip's parent, which
	 * covers the layout where `ChipBox_N` holds the border and the letter as siblings.
	 *
	 * Returns null unless the match is UNAMBIGUOUS — exactly one UTextBlock. Taking the first of several
	 * would be silently order-dependent, and a wrong-widget recolour is worse than no recolour: the chip is
	 * still readable with its authored label, and a missing write is visible at the pixel gate.
	 */
	static UTextBlock* ResolveDataChipLabel(UBorder* Chip)
	{
		if (!Chip)
		{
			return nullptr;
		}

		TArray<UTextBlock*> Found;
		CollectTextBlocks(Chip, Found);
		if (Found.Num() == 0)
		{
			CollectTextBlocks(Chip->GetParent(), Found);
		}
		return Found.Num() == 1 ? Found[0] : nullptr;
	}

	/** App typeface (composite Inter/JetBrains-Mono UFont). Cached; loaded once game content is mounted. */
	static UFont* GetInterFont()
	{
		static TWeakObjectPtr<UFont> Cached;
		if (!Cached.IsValid())
		{
			UFont* Loaded = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter"));
			// Permanent GC root: this shared font is stored as a raw FSlateFontInfo.FontObject across many Slate
			// widgets that keep no GC reference. Without rooting, a file-switch GC collects it and those pointers
			// dangle (0xdd) -> SIGSEGV in font measure. AddToRoot pins the single instance for the process.
			if (Loaded && !Loaded->IsRooted())
			{
				Loaded->AddToRoot();
			}
			Cached = Loaded;
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
		// A6b-5 (2026-07-28): the `bright mono` row (#e6e8eb <-> #222222) was DELETED. It had zero consumers
		// and was one of three rows colliding on light #222222, which is what made the table lossy across a
		// toggle. Verified before removing, not assumed: no design-time UTextBlock carried it (census), and
		// neither did any of the 10 SWS FTextBlockStyle assets or the 7 FButtonStyle foreground sets, which
		// are TextMap's other two consumers. Removal is a no-op in BOTH directions — light #222222 already
		// resolved to `primary text` on first match, so nothing ever routed back to #e6e8eb anyway.
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

	/**
	 * Which button state a brush paints. Both 4-brush loops that feed ThemeIconBrush build their arrays in
	 * this exact order, so the loop index casts straight to this.
	 */
	enum class EIconBrushState : uint8 { Normal = 0, Hovered = 1, Pressed = 2, Disabled = 3 };

	/** Bottom-bar icon materials expose glyph/background/border params — theme via a dynamic instance. */
	static bool ThemeIconBrush(FSlateBrush& Brush, UObject* MidOuter, const bool bLight,
		const EIconBrushState State = EIconBrushState::Normal)
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
		// S10 (2026-08-10) — THE icon-hover defect. This line used to write ONE hardcoded background for
		// every state, and the loops above call it for Normal/Hovered/Pressed/Disabled alike, so the theme
		// pass FLATTENED every icon button's hover onto its rest colour in both themes. That is the
		// stakeholder's "inconsistent hover affect on icon buttons": the only difference left between rest
		// and hover was whichever NON-colour param survived ClearParameterValues() via the parent MIC
		// (`PressT`, a 5% glyph scale) — and only 8 of the 13 icon MIs authored one, so the affordance was
		// present on some icons and absent on others. Inconsistent, exactly as reported.
		//
		// Note this outranks the asset layer: because each brush gets its OWN MID above, per-state colour
		// works even where two states share a source MI. The per-state MI wiring done alongside this row is
		// still required, but for the params this function does NOT set — `PressT` and the pressed
		// `MaxPressPct` / `BorderColour` come from the parent MIC, so a state pointing at the base MI has no
		// press animation no matter what colour lands here.
		//
		// REST IS DELIBERATELY BYTE-UNCHANGED in both themes (light 0.9131, dark 0.007/0.007/0.009): the icon
		// chrome is signed off, and this row is only asked for a hover. Hover/pressed use the app's existing
		// interaction greys rather than ButtonHoverBg/ButtonPressedBg — those are BLUE-tinted in light theme
		// (0.81485, 0.87962, 0.95597) and would restyle the chips, and their dark PressedBg (0.04971) is
		// DARKER than the icon rest value, so pressed would move the wrong way. Light steps are the same
		// #d9d9d9 / #c8c8c8 pair S2 gave the dropdown rows; dark steps are the icon family's own recipe,
		// already authored in the 8 working `MI_*_Hovered` / `_Pressed` instances.
		FLinearColor Background;
		switch (State)
		{
		case EIconBrushState::Hovered:
			Background = bLight ? FLinearColor(0.69387f, 0.69387f, 0.69387f) : FLinearColor(0.097281f, 0.097281f, 0.097281f);
			break;
		case EIconBrushState::Pressed:
			Background = bLight ? FLinearColor(0.57757f, 0.57757f, 0.57757f) : FLinearColor(0.201556f, 0.201556f, 0.201556f);
			break;
		default:
			Background = bLight ? FLinearColor(0.9131f, 0.9131f, 0.9131f) : FLinearColor(0.007f, 0.007f, 0.009f);
			break;
		}
		Mid->SetVectorParameterValue(TEXT("BackgroundColour"), Background);
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
		// HISTORY 2026-07-30 .. 2026-08-03 — a "Loading" material-path carve-out used to sit here, returning
		// false so the loading card kept its owner-pull colours. Recorded because the defect it fixed was
		// owner-reported and will look familiar if it ever returns. The card is owner-pull:
		// UImprovedLoadingNotifyWidget / UBaseLoadingWidget write their role colours through the MID from
		// ApplyMobiusTheme (via ThemeMaterialCard). An untargeted write here overwrote that — dark hit
		// ClearParameterValues() below and reverted the card to MI_LoadingOuterBackground's baked near-black
		// (0.006995 -> #141517), light forced the hard-coded 0.9131 (#f5f5f5). That IS the owner's "black in
		// dark, white in light" report — one bug, both halves (commit 31641401).
		//
		// The clobberer was ThemeStandardControlsInTree recursing into the card's subtree without calling the
		// child's ApplyMobiusTheme. WBP_ImprovedLoadingNotify is a BindWidget child of UTopMainUiWrapper,
		// itself a UMobiusThemedUserWidget, and in UMG a child constructs during its parent's RebuildWidget —
		// so the card's own owner-pull ran FIRST and the wrapper's recursive control pass ran LAST, on a FRESH
		// LAUNCH. A6b-7 (a6030f99) fixed that structurally: the recursion now STOPS at any child that
		// IsA<UMobiusThemedUserWidget>, which is exactly what the card's class is. With the clobberer gone the
		// carve-out had no work left to do, and it was deleted here on 2026-08-03 after a re-gate — fresh PIE
		// per theme, card measured dark #414141 outer / #2B2B2B inner, light #EAEAEA / #FFFFFF, all exact.
		//
		// If a loading-card regression ever reappears, do NOT re-add a material-path guard. Such a test cannot
		// tell the loading card from any other widget instancing the same MI — WBP_ASET-RSET_Base does exactly
		// that, and the old carve-out silently excluded it too. The real bug would be a lost owner-pull
		// dispatch or a new untargeted writer. Same ownership rule as StyleBorderForTheme's UMobiusThemedBorder
		// guard and StyleButtonForTheme's UBaseButton guard, and as recorded for UMobiusThemedBorder in
		// BaseLoadingWidget.cpp:96-98: a declared role must beat an untargeted sweep, keyed on TYPE not path.
		//
		// The other cards in this folder (LoadDataFiles, FlowCounterTop/Bottom, PlayBar, Egress) have no
		// owner-pull driver, so they rely on the generic branches below and must keep falling through.
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(Material, MidOuter);
			Brush.SetResourceObject(Mid);
		}
		// The in-world flow-counter card must conform to the PALETTE in BOTH themes (the generic dark branch
		// below just restores the material's authored values, which the owner flagged as off-palette in dark).
		// Set an explicit panel surface (RibbonBg) + input-border edge per theme; preserve the authored alpha.
		if (SourcePath.Contains(TEXT("FlowCounter")))
		{
			FLinearColor Background = FLinearColor::Black;
			FLinearColor BorderTint = FLinearColor::White;
			Mid->Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Background Color Tint")), Background);
			Mid->Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Border Color Tint")), BorderTint);
			const FLinearColor CardBg = PaletteColor(EMobiusPaletteRole::RibbonBg, bLight);
			const FLinearColor CardBorder = PaletteColor(EMobiusPaletteRole::InputBorder, bLight);
			Mid->SetVectorParameterValue(TEXT("Background Color Tint"), FLinearColor(CardBg.R, CardBg.G, CardBg.B, Background.A));
			Mid->SetVectorParameterValue(TEXT("Border Color Tint"), FLinearColor(CardBorder.R, CardBorder.G, CardBorder.B, BorderTint.A));
			return true;
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
	//
	// A6b-2: this call usually themes ZERO assets, because the registry scan it depends on is still
	// running here. It is kept anyway (it is the one that works in a cooked build, where the registry is
	// already loaded) and is now backed by an explicit registry-ready hook instead of ticker repetition.
	ApplySharedStylesWhenRegistryReady();

	// D172: reliably re-theme the LIVE widgets once the UI is up. Widgets (ribbon tabs, lazily-built
	// panels) construct AFTER this subsystem initialises, and the only other startup re-theme
	// (UThemeToggleWidget::NativeConstruct) can fire before them or live in a popup that is closed at
	// launch — leaving late labels at their white design-time colour until the user clicks. Re-run the
	// startup re-theme a handful of times over the first few seconds so late widgets pick up the saved
	// theme. Each pass no-ops until a game world + widgets exist. BOTH themes: "dark is the design-time
	// default" turned out false for parts of the UI (Browse buttons, checkbox labels, snapshot-at-construct
	// button styles) — a dark start with no re-theme left light traces everywhere. One-shot: the ticker
	// unregisters itself after the last pass.
	//
	// A6b-6 (2026-07-31): the re-theme is now a BROADCAST, not the walk. This is the fix for the ordering
	// ASYMMETRY that caused the loading-card two-writer race: this ticker used to call ApplyToLiveWidgets
	// and never broadcast, so at launch the walk got the last word over every owner-pull widget, whereas
	// ApplyTheme walks first and broadcasts last so the owner wins there. A theme TOGGLE therefore looked
	// correct while a FRESH LAUNCH did not — the single hardest class of theming bug to notice. Broadcasting
	// here makes launch ordering identical to ApplyTheme's by construction, because there is now only one
	// writer family left. Both delegates fire, matching ApplyTheme: the native one has non-widget listeners
	// (the ImPlot chart windows re-fetch GetThemedWindowStyle from it).
	//
	// NEW HAZARD, accepted: OnThemeChanged goes from ZERO fires at launch to one per pass (up to 100 over
	// ~30 s, the same cadence the walk ran at). Every handler is idempotent within one theme — owner writes
	// are absolute (SetVectorParameterValue / SetColorAndOpacity / SetStyle), and the value remaps in
	// ThemeStandardControlsInTree have no cross-column collision, so a second pass in the same direction is
	// a no-op. Adding a non-idempotent OnThemeChanged handler would now be a launch-time defect.
	{
		const TSharedRef<int32> Passes = MakeShared<int32>(0);
		const TSharedRef<int32> ThemedPasses = MakeShared<int32>(0);
		StartupThemeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Passes, ThemedPasses](float) -> bool
		{
			// PIE-CLOSE GUARD: the core ticker is GLOBAL and outlives the PIE world. If the world is
			// gone or tearing down, STOP immediately — walking half-destroyed widgets reads a dying
			// combo's SMenuAnchor delegate and trips the data-race ensure (the "crash on PIE close").
			UWorld* TickWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			if (!TickWorld || TickWorld->bIsTearingDown)
			{
				StartupThemeTickerHandle.Reset();
				return false;
			}
			// A6b-6: this block is a REGISTRY-RACE BACKSTOP and nothing else. Its other former purpose — an
			// ordering guard so the walk could not re-copy disk-authored (light, magenta-foreground) SWS
			// styles into every snapshot-at-construct button before they were retinted — died with the walk.
			// The race it still closes is independent of the walk and still real: OnFilesLoaded is a ONE-SHOT
			// broadcast, so a scan that completes between ApplySharedStylesWhenRegistryReady's
			// IsLoadingAssets() test and its bind is missed outright, and then nothing ever retints the SWS
			// assets. Buttons that snapshot at construct would keep the disk-authored styles for the whole
			// session. Checked unconditionally for exactly that reason — the flag, not the bind, is the proof
			// the retint actually saw the assets. Ordering within the tick still matters and is unchanged:
			// retint the shared assets FIRST, then broadcast, so a button re-copying on OnThemeChanged reads
			// a themed asset.
			if (!bSharedStylesAppliedAfterRegistryScan)
			{
				ApplySharedStylesWhenRegistryReady();
			}
			ThemeInWorldWidgetComponents();
			// A pass only COUNTS when the live UI was HUD-sized: on a cold start the loading screen's handful
			// of widgets used to eat all three passes, the ticker unregistered before the ribbon ever
			// constructed, and the tab labels kept their construct-time white (invisible on light). The full
			// Mobius HUD holds ~600 leaf widgets; 200 cleanly separates it from a load screen. A6b-6 kept the
			// counter as the walk's exact traversal minus the styling call (CountLiveLeafWidgets) rather than
			// inventing a cheaper one, because any other counting shape decalibrates that 200 — too low and
			// the ticker unregisters on the loading screen again, too high and it never crosses at all.
			const bool bHudSized = CountLiveLeafWidgets() > 200;
			if (bHudSized)
			{
				++(*ThemedPasses);
				// Force a repaint — labels re-coloured behind an InvalidationBox (ribbon tabs) keep their
				// cached white paint until invalidated (this is what a tab CLICK does via ApplyTheme).
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().InvalidateAllWidgets(false);
				}
			}
			// Unconditional, and AFTER the invalidate, mirroring ApplyTheme's own order. Not gated on
			// bHudSized: the count is a loop-control signal for "has the HUD constructed yet", not a
			// permission to theme, and a sub-200 UI (the load screen itself, a VR menu) needs the re-pull
			// just as much.
			OnThemeChanged.Broadcast();
			OnThemeChangedNative.Broadcast();
			const bool bKeepTicking = (*ThemedPasses < 3) && (++(*Passes) < 100);
			if (!bKeepTicking)
			{
				StartupThemeTickerHandle.Reset();  // self-unregistered; clear so Deinitialize won't double-remove
			}
			return bKeepTicking;
		}), 0.3f);
	}
}

void UUIThemeSubsystem::ApplySharedStylesWhenRegistryReady()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Run it now regardless. In a cooked build the registry is already loaded by this point, so this IS the
	// call that does the work and no hook is needed at all.
	ApplySharedStyles(CurrentTheme == EMobiusUITheme::Light);

	if (!Registry.IsLoadingAssets())
	{
		// The scan was done, so that call actually saw the SWS assets. Nothing further to wait for, and any
		// hook left over from an earlier attempt is now redundant.
		bSharedStylesAppliedAfterRegistryScan = true;
		if (AssetRegistryFilesLoadedHandle.IsValid())
		{
			Registry.OnFilesLoaded().Remove(AssetRegistryFilesLoadedHandle);
			AssetRegistryFilesLoadedHandle.Reset();
		}
		return;
	}

	// Still scanning: that call themed nothing, so re-run once the registry says it is done. AddUnique is
	// not available on a TS multicast delegate, so guard on the handle — PIE creates a fresh subsystem per
	// session against the same module-level registry, and a stacked binding would fire into a dead one.
	if (AssetRegistryFilesLoadedHandle.IsValid())
	{
		return;
	}
	AssetRegistryFilesLoadedHandle = Registry.OnFilesLoaded().AddLambda([this]()
	{
		// Weak-safe by construction: the handle is removed in Deinitialize, so this cannot outlive the
		// subsystem. Re-entering the same function settles the flag and clears the hook.
		ApplySharedStylesWhenRegistryReady();
	});
}

void UUIThemeSubsystem::Deinitialize()
{
	// Remove the startup re-theme ticker so it cannot fire after the world/subsystem tears down —
	// the core ticker is global and outlives the PIE world (see Initialize: the "crash on PIE close").
	if (StartupThemeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StartupThemeTickerHandle);
		StartupThemeTickerHandle.Reset();
	}

	// A6b-2: same hazard, same fix. IAssetRegistry is module-level and outlives this subsystem, so a hook
	// left bound would fire into freed memory on the next scan and would also accumulate one binding per
	// PIE session. The module may already be gone during shutdown, so ask for it without forcing a load.
	if (AssetRegistryFilesLoadedHandle.IsValid())
	{
		if (const FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
		{
			Module->Get().OnFilesLoaded().Remove(AssetRegistryFilesLoadedHandle);
		}
		AssetRegistryFilesLoadedHandle.Reset();
	}

	Super::Deinitialize();
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

void UUIThemeSubsystem::ReapplyToUserWidget(UUserWidget* UserWidget)
{
	if (!UserWidget || !UserWidget->WidgetTree)
	{
		return;
	}
	// In-world cards live on a UWidgetComponent and are world-space, so GetAllWidgetsOfClass never returned
	// them and the deleted walk could not reach them — this function is their re-theme route. It is now a
	// single call: ThemeStandardControlsInTree already recurses into embedded user widgets, which is what the
	// hand-rolled ForEachWidget/ApplyToWidget recursion here used to add (a flow-counter card themes its
	// section-counter children through it). Not a construct pass — bConstruct=false.
	//
	// The in-world card has no OWNER hook to call and never did — CORRECTED 2026-07-31 (A6b-7). An earlier
	// version of this comment claimed UFlowCounterWidget binds OnThemeChanged itself; it does not.
	// UFlowCounterWidget (FlowCounterWidget.h:15) and its child UFlowSectionCounter (FlowSectionCounter.h:16)
	// are both plain UUserWidget, neither derives from UMobiusThemedUserWidget, and neither .cpp mentions
	// OnThemeChanged at all. This function is consequently their ONLY theme writer, which is also why the
	// A6b-7 recursion guard cannot prune anything here (nothing in an in-world tree matches its cast).
	// Whatever drives a card created mid-session is therefore an external caller of this function, NOT a
	// self-bind — establish which one before changing anything on this path.
	ThemeStandardControlsInTree(UserWidget, /*bConstruct*/false);
}

FLinearColor UUIThemeSubsystem::GetPaletteColor(const EMobiusPaletteRole Role) const
{
	return MobiusTheme::PaletteColor(Role, CurrentTheme == EMobiusUITheme::Light);
}

FLinearColor UUIThemeSubsystem::GetPaletteColorForTheme(const EMobiusPaletteRole Role, const EMobiusUITheme Theme) const
{
	return MobiusTheme::PaletteColor(Role, Theme == EMobiusUITheme::Light);
}

UMaterialInterface* UUIThemeSubsystem::GetThemedTabMaterial(const bool bSelected, const bool bRightEdge) const
{
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;
	const TCHAR* ThemeFolder = bLight ? TEXT("LightTheme") : TEXT("DarkTheme");
	// bRightEdge selects the *Right variants (AccentEdge=1) for the vertical side rail; default = bottom.
	const FString Name = FString::Printf(TEXT("MI_Tab%s%s"),
		bSelected ? TEXT("Selected") : TEXT("Default"),
		bRightEdge ? TEXT("Right") : TEXT(""));
	const FString Path = FString::Printf(
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/Master/Instances/%s/%s.%s"),
		ThemeFolder, *Name, *Name);
	return LoadObject<UMaterialInterface>(nullptr, *Path);
}

FButtonStyle UUIThemeSubsystem::GetThemedTabStyle(const bool bSelected, const bool bRightEdge) const
{
	using namespace MobiusTheme;
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;

	// A10b step 6 (2026-08-14): base the tab on the NAMED C++ geometry instead of loading
	// SWS_SettingButtonStyle. That asset supplied shape + PressedSlateSound = click_Cue; both now come
	// from MobiusButtonGeometry::Tab / MobiusButtonSound::ApplyPressedCue, the same pair UBaseButton
	// already builds its panel and tab families from. Its authored 0,20,0,20 padding was overridden
	// below to FMargin(0,4) anyway, so nothing it carried survived to a pixel except the cue.
	FButtonStyle Style;
	MobiusButtonGeometry::Tab.ApplyToButtonStyle(Style);
	MobiusButtonSound::ApplyPressedCue(Style);

	// Swap each state's brush to the themed tab material (selected vs default) for the current theme.
	// NOTE: this material is NOT what hid the label — the real cause was the 40px vertical padding below
	// (measured: label allocated 2px of 46px). Restored per owner after the padding fix so the active/
	// inactive tab keeps its original material look.
	if (UMaterialInterface* TabMaterial = GetThemedTabMaterial(bSelected, bRightEdge))
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

	// THE invisible-tab-text fix (measured 2026-07-21): the base SWS_SettingButtonStyle padding is
	// 0,20,0,20 — 40px of VERTICAL padding, designed for a much taller tab. On the live ~46px tab button
	// that squeezed the label to ~2px of allocated height (desired 19px) and clipped it to nothing — which
	// is why the label was correctly coloured + in the live tree yet drew no pixels. Give the label its
	// height back with modest padding (horizontal kept 0 so the label width is unchanged).
	Style.NormalPadding = FMargin(0.0f, 4.0f);
	Style.PressedPadding = FMargin(0.0f, 4.0f);
	return Style;
}

void UUIThemeSubsystem::ApplyTabStateFills(FButtonStyle& Style, UObject* Outer, const bool bLight) const
{
	using namespace MobiusTheme;

	// A16 + A17 (2026-07-28): ribbon tabs had NO hover fill and NO pressed fill. GetThemedTabStyle assigns
	// the SAME MI_Tab* material to Normal/Hovered/Pressed/Disabled, so a hovered (or held) tab was
	// byte-identical to an idle one and only the label colour changed. Fix: give the HOVERED and PRESSED
	// brushes their own MaterialInstanceDynamic off that same MI and override just FillColour with the
	// app-wide hover / pressed roles, so ribbon tabs speak the same interaction language as every other
	// Mobius button (owner: "expand this onto the ribbon buttons too"). Hover shipped first as A16; A17
	// added pressed, because once hover landed a press DROPPED back to the base fill and read as the hover
	// vanishing rather than as a press. Both live in one helper so the two states cannot drift apart.
	//
	// Why a MID and not simply a brighter Brush.TintColor: Slate packs the brush tint into the vertex
	// colour (FColor), so a tint > 1.0 clamps and CANNOT brighten a material — that route works in light
	// mode and silently does nothing in dark. Overriding the material's own FillColour works both ways.
	//
	// The MID's parent is the very MI the Normal brush uses, so every other tab parameter (UseUnderline,
	// AccentEdge, UseTopBar, UnderlineColour, UnderlineThickness) inherits per-theme/per-state for free.
	// That is also why hover/press do not muddy the active tab: the accent UNDERLINE is the active
	// signifier (UseUnderline 1 vs 0), not fill brightness, so a hovered inactive tab reads
	// brighter-but-not-active.
	//
	// NOT touched here: NormalPadding / PressedPadding, which GetThemedTabStyle leaves EQUAL at (0,4).
	// Keep them equal — a smaller PressedPadding shrinks the hit rect mid-press, Slate fires OnMouseLeave
	// and discards the click (the A15 "unresponsive button" trap). Fill is the whole press affordance here.
	auto FillState = [Outer, bLight](FSlateBrush& Brush, const EMobiusPaletteRole FillRole,
	                                 const EMobiusPaletteRole BorderRole)
	{
		UMaterialInterface* TabMaterial = Cast<UMaterialInterface>(Brush.GetResourceObject());
		if (!TabMaterial)
		{
			return; // no tab material (SWS fallback) — leave this state alone
		}

		// Outer = the button, so the MID is GC-rooted through its WidgetStyle UPROPERTY and dies with it.
		// No subsystem-side cache: ~8 tabs re-themed only on construct / activation / theme toggle, and
		// two MIDs per tab is still nothing next to a per-frame allocation.
		UMaterialInstanceDynamic* StateMaterial = UMaterialInstanceDynamic::Create(TabMaterial, Outer);
		if (!StateMaterial)
		{
			return;
		}
		StateMaterial->SetVectorParameterValue(TEXT("FillColour"), PaletteColor(FillRole, bLight));
		Brush.SetResourceObject(StateMaterial);
		if (Brush.OutlineSettings.Width > 0.0f)
		{
			Brush.OutlineSettings.Color = FSlateColor(PaletteColor(BorderRole, bLight));
		}
	};

	// Hover border = ButtonHoverBorder (its own spec-4 role). Pressed border = ButtonBorder, matching
	// UBaseButton::RefreshThemedButtonStyle, which themes every state's outline with the plain border role
	// — there is no ButtonPressedBorder in the palette. Both branches are inert while the tab brushes carry
	// OutlineSettings.Width 0 (they do today); they exist so a future outlined tab stays theme-correct.
	FillState(Style.Hovered, EMobiusPaletteRole::ButtonHoverBg, EMobiusPaletteRole::ButtonHoverBorder);
	FillState(Style.Pressed, EMobiusPaletteRole::ButtonPressedBg, EMobiusPaletteRole::ButtonBorder);
}

void UUIThemeSubsystem::ApplyRibbonTabStyle(UButtonWithText* Button, const bool bActive)
{
	using namespace MobiusTheme;
	if (!Button)
	{
		return;
	}
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;

	// Single authoritative source for a ribbon tab's look. Style carries the tab material + per-state
	// foregrounds; the label colour is ALSO set explicitly (UseForeground did not resolve to the button
	// foreground for these buttons — the invisible-tab-text bug), so the label is driven directly.
	const FLinearColor LabelColor = PaletteColor(
		bActive ? EMobiusPaletteRole::TabActiveText : EMobiusPaletteRole::TabInactiveText, bLight);
	// bRightEdgeAccent picks the right-edge tab material variant (side tool-rail) vs bottom (top ribbon).
	FButtonStyle TabStyle = GetThemedTabStyle(bActive, Button->bRightEdgeAccent);
	ApplyTabStateFills(TabStyle, Button, bLight);
	Button->SetStyle(TabStyle);
	// Top-ribbon labels are MyButtonText; colour them here as before.
	Button->ApplyThemedLabelColor(LabelColor);
	// Side-rail ribbon buttons show their label via a CHILD UVerticalTextBlock (not MyButtonText), so
	// colour that too: active = TabActiveText, inactive = TabInactiveText (matches the top ribbon).
	// A5: the style refresh is now done HERE rather than relying on the walk having run first. The button
	// owns this label outright — RefreshThemedStyle re-lands the themed base style, then the override paints
	// the active/inactive accent over it, in that order, every time. (UVerticalTextBlock's own
	// OnThemeChanged handler deliberately stands down for ribbon-owned labels so the two cannot race.)
	if (UWidget* Content = Button->GetChildAt(0))
	{
		if (UVerticalTextBlock* VLabel = Cast<UVerticalTextBlock>(Content))
		{
			VLabel->RefreshThemedStyle();
			VLabel->SetThemedLabelColor(LabelColor);
		}
	}
}

FWindowStyle UUIThemeSubsystem::GetThemedWindowStyle() const
{
	using namespace MobiusTheme;
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;

	FWindowStyle Style = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	const FLinearColor TitleBg = PaletteColor(EMobiusPaletteRole::TitlebarBg, bLight);
	const FLinearColor Border = PaletteColor(EMobiusPaletteRole::WindowBorder, bLight);
	const FLinearColor TitleText = PaletteColor(EMobiusPaletteRole::TitlebarText, bLight);

	// 2026-07-31: these were TINTS. A brush TintColor MULTIPLIES, and FCoreStyle's window brushes are dark
	// TEXTURES, so tinting toward a light WindowBorder could only darken — every SMoveableWindow popup rendered
	// a 3px DARK GRADIENT RING outside its grey border. Measured on the legal-notice window at mid-height:
	// x=0..2 #323334 / #0E0F10 / #494B4E (the ring), then x=3..5 #B0B0B0 (the border that was supposed to be
	// the outer edge). Owner reported it as "a small border outline between the actual border and the window",
	// on ALL popups — which is exactly the blast radius of this function.
	//
	// REPLACE the frame brushes with flat colour brushes so there is no texture to fight. The windows this
	// styles are square-cornered, so no corner geometry is lost. See MEMORY
	// reference-slate-brush-tint-cannot-brighten — third instance of the same trap.
	Style.BorderBrush = FSlateColorBrush(Border);
	Style.OutlineBrush = FSlateColorBrush(Border);
	Style.BackgroundBrush = FSlateColorBrush(TitleBg);

	// The three TITLE brushes are left as tints deliberately: SWindowTitleBarWidget overwrites all three with
	// NoBrush and paints its own SColorBlock polled live from TitlebarBg, so whatever is set here is discarded
	// for any SMoveableWindow. Kept (rather than deleted) because a caller that builds a stock SWindow from
	// this style still gets a title colour out of them — a stock SWindow ignores TitleTextStyle and the title
	// brushes for its BAR, but not for the FlashTitleBrush path.
	Style.ActiveTitleBrush.TintColor = FSlateColor(TitleBg);
	Style.InactiveTitleBrush.TintColor = FSlateColor(TitleBg);
	Style.FlashTitleBrush.TintColor = FSlateColor(TitleBg);
	Style.TitleTextStyle.ColorAndOpacity = FSlateColor(TitleText);

	// A18: the title-bar × is a destructive affordance, so its glyph takes DangerText. Callers that
	// re-fetch this style on OnThemeChanged (the ImPlot chart windows) therefore follow a live toggle.
	MobiusWindowButtonStyle::ApplyDangerCloseGlyph(Style, this);

	return Style;
}

void UUIThemeSubsystem::ApplyTheme(const bool bLight)
{
	// A6b-6 (2026-07-31): the ApplyToLiveWidgets call that used to sit between these two is DELETED. Every
	// widget family it covered now themes from its own construct + OnThemeChanged (the broadcast at the end
	// of this function), so what is left here is: retint the shared assets, re-theme the in-world cards the
	// broadcast cannot reach on its own, push the palette to the MPC, invalidate, broadcast.
	ApplySharedStyles(bLight);
	ThemeInWorldWidgetComponents();

	// NEW ARCHITECTURE (additive, migration Phase 2+): push the palette into MPC_UITheme so
	// material-backed chrome repaints GPU-side. No-op until the MPC asset exists.
	WriteThemeToMPC(bLight);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}

	// Event replacement for the walk: event-driven widgets re-pull their role colours on this.
	OnThemeChanged.Broadcast();
	OnThemeChangedNative.Broadcast();
}

void UUIThemeSubsystem::ThemeInWorldWidgetComponents()
{
	// In-world cards (flow counters) render via a UWidgetComponent and are world-space plain-BP widgets —
	// GetAllWidgetsOfClass never returns them, so the live-widget walk cannot reach them. Iterate the
	// widget components directly and re-theme each hosted widget's tree (recursive, so section children
	// theme too). Runs on every ApplyTheme + the startup ticker; the container also calls ReapplyTheme
	// when it places a counter so a freshly-spawned card themes immediately.
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	for (TObjectIterator<UWidgetComponent> It; It; ++It)
	{
		UWidgetComponent* Component = *It;
		if (!Component || Component->GetWorld() != World)
		{
			continue;
		}
		if (UUserWidget* HostedWidget = Component->GetWidget())
		{
			ReapplyToUserWidget(HostedWidget);
		}
	}
}

void UUIThemeSubsystem::WriteThemeToMPC(const bool bLight)
{
	// MPC_UITheme is authored in migration Phase 2; until then LoadObject returns null and this no-ops.
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/Game/01_Dev/Widgets/WidgetMaterials/MPC_UITheme.MPC_UITheme"));
	if (!Collection)
	{
		return;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// Role -> MPC vector-parameter name. Extend as the MPC grows (PRD §8). Parameter names MUST match
	// the collection's authored vector params. Kept as data so the writer stays a simple loop.
	struct FRoleParam { EMobiusPaletteRole Role; const TCHAR* Param; };
	static const FRoleParam Params[] =
	{
		{ EMobiusPaletteRole::Accent,          TEXT("Accent") },
		{ EMobiusPaletteRole::TabstripBg,      TEXT("ChromeTabBar") },
		{ EMobiusPaletteRole::RibbonBg,        TEXT("ChromeBody") },
		{ EMobiusPaletteRole::PanelHeaderBg,   TEXT("HeaderBar") },
		{ EMobiusPaletteRole::PanelDivider,    TEXT("Divider") },
		{ EMobiusPaletteRole::InputBg,         TEXT("Field") },
		{ EMobiusPaletteRole::LabelText,       TEXT("TextPrimary") },
		{ EMobiusPaletteRole::SublabelText,    TEXT("TextDim") },
		{ EMobiusPaletteRole::ButtonBg,        TEXT("ButtonFill") },
		{ EMobiusPaletteRole::ButtonHoverBg,   TEXT("ButtonHover") },
		{ EMobiusPaletteRole::ButtonPressedBg, TEXT("ButtonPressed") },
		{ EMobiusPaletteRole::ButtonBorder,    TEXT("ButtonBorder") },
	};
	for (const FRoleParam& RP : Params)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(
			World, Collection, FName(RP.Param), MobiusThemePalette::Color(RP.Role, bLight));
	}
}

void UUIThemeSubsystem::StyleComboBoxForBuild(UComboBoxString* Combo, const bool bLight)
{
	using namespace MobiusTheme;
	if (!Combo)
	{
		return;
	}

	// W1 CONTRACT: writes the combo's UPROPERTY style members, then nudges via SetWidgetStyle/SetItemStyle.
	// Called both PRE-BUILD (from UMobiusThemedComboBox::RebuildWidget, before Super — MyComboBox null, the
	// setters just store the members) AND LIVE (from UMobiusThemedComboBox::HandleThemeChanged on a
	// deliberate toggle). The live path is crash-safe: SetWidgetStyle/SetItemStyle only Invalidate(Layout)
	// on the SComboBox — they NEVER touch the SMenuAnchor delegates the FMRSWRecursiveAccessDetector ensure
	// guards (verified against UE 5.5 Slate source). The old crash was per-CLICK timing re-entering an
	// in-flight SMenuAnchor::SetIsOpen — a mode this single-fire/menu-closed path does not create.

	// CLOSED-COMBO SURFACE — a FLAT RoundedBox: fill=InputBg, 1px InputBorder outline, rounded corners
	// (CornerRadii 5, matching the flow-counter combo the owner flagged as the more professional look; the
	// colours are already the InputBg/InputBorder roles this uses). It is NOT a material: a material Image
	// brush cannot carry a Slate outline, and the combo no longer needs GPU-follow — it re-themes via
	// HandleThemeChanged re-running this (crash-safe SetWidgetStyle → Invalidate(Layout) only). All four
	// button states share the look (static input-field style; no hover flash).
	FComboBoxStyle Style = Combo->GetWidgetStyle();
	{
		const FLinearColor SurfaceFill    = PaletteColor(EMobiusPaletteRole::InputBg, bLight);
		const FLinearColor SurfaceOutline = PaletteColor(EMobiusPaletteRole::InputBorder, bLight);
		FSlateBrush* Buttons[] = {
			&Style.ComboButtonStyle.ButtonStyle.Normal, &Style.ComboButtonStyle.ButtonStyle.Hovered,
			&Style.ComboButtonStyle.ButtonStyle.Pressed, &Style.ComboButtonStyle.ButtonStyle.Disabled,
		};
		for (FSlateBrush* Brush : Buttons)
		{
			Brush->SetResourceObject(nullptr);                 // flat colour, no material
			Brush->DrawAs = ESlateBrushDrawType::RoundedBox;   // RoundedBox so OutlineSettings draw a real edge
			Brush->TintColor = FSlateColor(SurfaceFill);
			Brush->OutlineSettings.Color = FSlateColor(SurfaceOutline);
			Brush->OutlineSettings.Width = 1.0f;
			Brush->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush->OutlineSettings.CornerRadii = FVector4(5.0, 5.0, 5.0, 5.0);
		}
	}

	// Dropdown popup OUTLINE. MenuBorderBrush is the FSlateBrush the popup's SBorder holds BY POINTER, so
	// mutating it here (and on a live toggle) is picked up on the next open with no live SMenuAnchor touch.
	// 1px InputBorder outline + InputBg fill gives the dropdown a crisp edge (it had none). The fill IS the
	// brush TintColor: SComboButton wraps the popup in SBorder.BorderImage(&MenuBorderBrush) and never sets
	// BorderBackgroundColor, so tint alone is the fill — this is NOT the UBorder double-tint case.
	{
		FSlateBrush& MenuBorder = Style.ComboButtonStyle.MenuBorderBrush;
		MenuBorder.DrawAs = ESlateBrushDrawType::RoundedBox;
		MenuBorder.TintColor = FSlateColor(PaletteColor(EMobiusPaletteRole::InputBg, bLight));
		MenuBorder.OutlineSettings.Color = FSlateColor(PaletteColor(EMobiusPaletteRole::InputBorder, bLight));
		MenuBorder.OutlineSettings.Width = 1.0f;
		MenuBorder.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius; // HalfHeightRadius would pill it
		MenuBorder.OutlineSettings.CornerRadii = FVector4(0.0, 0.0, 0.0, 0.0);          // square dropdown corners
		Style.ComboButtonStyle.MenuBorderPadding = FMargin(1.0f);                       // keep rows off the 1px edge
	}

	// BUTTON FOREGROUNDS — theme ALL FOUR states to InputText. The authored leftovers (cyan Normal, grey
	// Disabled) otherwise show through: a combo that gets SetIsEnabled(false)/(true) (e.g. the flow-counter
	// type combo) would keep its stale grey DisabledForeground after re-enabling. With every state = InputText
	// the enabled text is always crisp; the disabled cue comes from Slate's own whole-combo dimming, not a
	// stale foreground. (The selected-item text inherits this via UseForeground when a block is regenerated
	// by a programmatic SetSelectedOption, which does not run the subclass's explicit reland.)
	{
		const FSlateColor FgColor(PaletteColor(EMobiusPaletteRole::InputText, bLight));
		Style.ComboButtonStyle.ButtonStyle.NormalForeground   = FgColor;
		Style.ComboButtonStyle.ButtonStyle.HoveredForeground  = FgColor;
		Style.ComboButtonStyle.ButtonStyle.PressedForeground  = FgColor;
		Style.ComboButtonStyle.ButtonStyle.DisabledForeground = FgColor;
	}
	Combo->SetWidgetStyle(Style); // one push: button surface + menu-border outline + themed foregrounds

	// Dropdown ROW colours — flat (only visible while the menu is open). STableRow reads ItemStyle live via
	// its const FTableRowStyle* pointer, so re-setting these on a toggle updates the rows on the next open.
	// S2: hover and selected were BOTH Accent blue, so the two states were indistinguishable - which is what
	// the stakeholder reported ("change drop down selected and hover colors to light gray and slightly darker
	// gray for selected"). They are now two greys: HoverBg for the transient pointer-over, ListSelectedBg for
	// the row that stays chosen after the pointer moves away.
	FTableRowStyle Items = Combo->GetItemStyle();
	const FLinearColor RowBg = PaletteColor(EMobiusPaletteRole::InputBg, bLight);
	const FLinearColor RowText = PaletteColor(EMobiusPaletteRole::InputText, bLight);
	const FLinearColor RowHover = PaletteColor(EMobiusPaletteRole::HoverBg, bLight);
	const FLinearColor RowSel = PaletteColor(EMobiusPaletteRole::ListSelectedBg, bLight);
	auto Row = [](FSlateBrush& B, const FLinearColor& C)
	{
		B.TintColor = FSlateColor(C);
		B.DrawAs = ESlateBrushDrawType::Image;
		B.SetResourceObject(nullptr);
	};
	// Slate splits these six by (selected?, list focused?, hovered?). Unselected rows take the hover cue;
	// every SELECTED variant takes the selected fill, focused or not. InactiveBrush was RowBg, which meant a
	// selected row lost its fill entirely as soon as the list gave up focus - fixed here rather than left,
	// since it is the same "you cannot see what is selected" defect the row was raised for.
	Row(Items.EvenRowBackgroundBrush, RowBg);          Row(Items.OddRowBackgroundBrush, RowBg);
	Row(Items.EvenRowBackgroundHoveredBrush, RowHover); Row(Items.OddRowBackgroundHoveredBrush, RowHover);
	Row(Items.ActiveBrush, RowSel);                     Row(Items.ActiveHoveredBrush, RowSel);
	Row(Items.InactiveBrush, RowSel);                   Row(Items.InactiveHoveredBrush, RowSel);
	Items.TextColor = FSlateColor(RowText);
	// MUST move with the fill: white was legible on Accent blue and is not on a light grey (#c8c8c8 gives
	// about 1.3:1). InputText keeps the selected row at the same contrast as every other row.
	Items.SelectedTextColor = FSlateColor(RowText);
	Combo->SetItemStyle(Items);

	// Selected-item TEXT foreground (InputText role) is set by the subclass via the engine's protected
	// InitForegroundColor() — the only pre-build foreground setter, and not reachable from here.
}

// =================================================================================================
// A5 (2026-07-28) — event-driven control theming (rebuild Phase 4). See the header for the contract.
// Each helper is the walker's branch for that control TYPE, lifted verbatim except where noted, so a
// widget can theme its own controls on construct / OnThemeChanged and the global walk is no longer the
// only thing that reaches them. Idempotent — a control themed twice in one pass lands the same values.
// =================================================================================================

void UUIThemeSubsystem::ThemeStandardControlsInTree(UUserWidget* Root, const bool bConstruct)
{
	if (!Root || !Root->WidgetTree)
	{
		return;
	}
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;
	// S8: collected in the pass below, written after it. See the RefreshDataChipLabel block for why the
	// letter cannot be written from inside the loop.
	TArray<UBorder*> DataChips;
	// Recurse into embedded user widgets: a panel's controls are usually a level or two down in nested
	// WBPs that have no C++ owner of their own, and those are exactly the ones that would otherwise be
	// left behind when the walk goes. Overlap with a nested themed widget's own call is harmless.
	//
	// The recursion is also the ONLY route to the widgets whose C++ class lives in the ProjectMobius
	// module — USimulationPlayBar (PlaybackSlider + ScrubFillBar) and USimulationSetupWidget (the time-
	// dilation input). MobiusWidgets already depends on ProjectMobius, so those classes cannot derive from
	// UMobiusThemedUserWidget without a circular module dependency. They are reached because
	// WBP_CompleteMobiusUI (UTopMainUiWrapper, this module) embeds them: the root themes the whole tree.
	Root->WidgetTree->ForEachWidget([this, bLight, bConstruct, &DataChips](UWidget* Widget)
	{
		if (!Widget)
		{
			return;
		}
		if (UUserWidget* ChildUserWidget = Cast<UUserWidget>(Widget))
		{
			// A6b-7 (2026-07-31): STOP at a child that is itself a UMobiusThemedUserWidget. Such a child runs
			// this exact pass over its own subtree from UMobiusThemedUserWidget::NativeConstruct and again from
			// HandleThemeChanged, controls-first then ApplyMobiusTheme — so descending into it adds no coverage
			// and instead lands an UNTARGETED write AFTER the child's declared role write. That is the same
			// ownership rule as StyleBorderForTheme's UMobiusThemedBorder guard and StyleButtonForTheme's
			// UBaseButton guard, lifted one level up to the widget itself.
			//
			// Coverage is unchanged, not merely "probably fine": every widget is themed by its nearest themed
			// ANCESTOR's pass (or by its own, if themed), and pruning only at themed boundaries leaves that
			// union identical. The precondition was verified by sweep before this landed — 18 direct + 6
			// indirect UMobiusThemedUserWidget subclasses, and every one of the overrides of NativeConstruct
			// calls Super::NativeConstruct, so no themed widget is missing its own pass. Re-verify that if a
			// new subclass appears.
			//
			// Non-themed embedded WBPs are still recursed, which keeps the ONLY route to the two ProjectMobius-
			// module widgets named below intact.
			//
			// The one thing the skip loses: a themed child whose NativeConstruct runs while
			// GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() is null never binds and never self-passes,
			// where the parent walk used to reach its standard controls. Deliberate — a widget that cannot see
			// the subsystem has no palette to pull either, so it was half-themed before regardless.
			//
			// The in-world path is NOT affected, which retires the reason this was deferred out of A6b-6:
			// ReapplyToUserWidget's roots are UFlowCounterWidget (FlowCounterWidget.h:15) and its child
			// UFlowSectionCounter (FlowSectionCounter.h:16), and BOTH are plain UUserWidget — nothing there
			// matches this cast, so the guard cannot prune any in-world subtree.
			if (!ChildUserWidget->IsA<UMobiusThemedUserWidget>())
			{
				ThemeStandardControlsInTree(ChildUserWidget, bConstruct);
			}
		}
		else if (USlider* Slider = Cast<USlider>(Widget))
		{
			StyleSliderForTheme(Slider, bLight);
		}
		else if (UCheckBox* CheckBox = Cast<UCheckBox>(Widget))
		{
			StyleCheckBoxForTheme(CheckBox, bLight);
		}
		else if (UScrollBox* ScrollBox = Cast<UScrollBox>(Widget))
		{
			StyleScrollBoxForTheme(ScrollBox, bLight);
		}
		else if (UProgressBar* ProgressBar = Cast<UProgressBar>(Widget))
		{
			StyleProgressBarForTheme(ProgressBar, bLight);
		}
		else if (UEditableTextBox* EditBox = Cast<UEditableTextBox>(Widget))
		{
			StyleEditableTextBoxForTheme(EditBox, bLight, bConstruct);
		}
		// A6b-5: UImage. No project class derives from UImage, so there is no self-theming family to skip.
		else if (UImage* Image = Cast<UImage>(Widget))
		{
			StyleImageForTheme(Image, bLight);
		}
		// A6b-6b: UBorder. No-ops on a UMobiusThemedBorder, which self-themes — so this covers exactly the
		// residue the walk was still carrying, and it is the only reason three of the four background-material
		// cards keep theming once the walk goes (they sit on plain borders). Ordering against the branches
		// above is irrelevant: a widget matches at most one of these casts.
		else if (UBorder* Border = Cast<UBorder>(Widget))
		{
			StyleBorderForTheme(Border, bLight);
			if (MobiusTheme::IsDataChipBorder(Border))
			{
				DataChips.Add(Border);
			}
		}
		// A6b-5: plain UButton. No-ops on a UBaseButton, which self-themes — so this covers exactly the
		// residue the walk was still carrying. 8 of the 13 belong to USimulationPlayBar, which is reached
		// only because the recursion above descends into it.
		else if (UButton* Button = Cast<UButton>(Widget))
		{
			StyleButtonForTheme(Button, bLight);
		}
		// A6b-5: text LAST, so any control-specific branch above still wins its own subtree.
		//
		// Widgets that colour their own labels (UFlowCounterListRow paints its text white when the row is
		// selected) are not in conflict with this remap, and the reason is the recursion's reach rather than
		// any ordering luck: ForEachWidget walks the DESIGN-TIME WidgetTree, so a row spawned into a list at
		// runtime is never visited by its container's pass at all — it is themed only through its own bind,
		// where the base runs this control pass first and the widget's ApplyMobiusTheme second. The explicit
		// write therefore always lands after the remap WITHIN one widget, which is the guarantee that matters
		// and does not depend on the order two different owners happen to be bound in.
		else if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			StyleTextBlockForTheme(Text, bLight);
		}
	});

	// S8, LAST: the text branch above remaps every UTextBlock in this tree, band letters included, so the
	// contrast write has to outlive it. Nested plain WBPs are covered by their own recursive call, which
	// runs its own copy of this post-pass over its own tree.
	for (UBorder* Chip : DataChips)
	{
		RefreshDataChipLabel(Chip);
	}
}

void UUIThemeSubsystem::StyleImageForTheme(UImage* Image, const bool bLight)
{
	using namespace MobiusTheme;

	if (!Image)
	{
		return;
	}

	// A6b-5 CASUALTY FIX (owner ruling, 2026-07-29 — RibbonBg; a final design consult is still owed on
	// whether the authored colour was the intended one). The play bar's background must NOT go through the
	// value remap. Its authored 0.9730 is SurfaceMap's `field bg` LIGHT value, and `field bg` is one of NINE
	// rows that do not survive a light->dark->light round trip: field-bg DARK 0.0243 sits 0.0041 away from
	// tab-strip DARK 0.0284, inside the 0.012 epsilon, and tab strip is scanned first — so two toggles turned
	// a near-white surface into mid-grey 0.7913. A declared role cannot collide, which is the entire argument
	// for the rebuild; the 73 migrated borders escaped this the same way.
	//
	// Scoped by OWNER rather than by name on purpose: another widget is also called BackgroundImage
	// (WBP_MoveableWidgetTest) and a bare name test would recolour it too. Two more used to be — the
	// WBP_ErrorPopup / WBP_ErrorPopup1 pair, deleted as dead assets in A19 (2026-08-03). Keep the owner
	// scoping regardless; it is correct independently of how many name collisions happen to exist today.
	//
	// The fix lives HERE, not in the play bar, because USimulationPlayBar is in ProjectMobius and cannot see
	// this subsystem at all — MobiusWidgets depends on ProjectMobius, so the reverse is a module cycle. That
	// is the same constraint that keeps the play bar off UMobiusThemedUserWidget.
	if (Image->GetName() == TEXT("BackgroundImage") && Image->GetTypedOuter<USimulationPlayBar>())
	{
		FSlateBrush RoleBrush = Image->GetBrush();
		const FLinearColor Surface = PaletteColor(EMobiusPaletteRole::RibbonBg, bLight);
		if (!RoleBrush.TintColor.GetSpecifiedColor().Equals(Surface, 0.001f))
		{
			RoleBrush.TintColor = FSlateColor(Surface);
			Image->SetBrush(RoleBrush);
		}
		return;   // and NOT through the remap below, which is what broke it
	}

	// GetBrush() rather than the deprecated Brush member the walk read behind a pragma — same value, and it
	// lets this compile without suppressing a warning. Copied because the helpers mutate in place.
	FSlateBrush Brush = Image->GetBrush();
	bool bChanged = RemapBrush(Brush, bLight);
	bChanged |= ThemeIconBrush(Brush, Image, bLight);
	bChanged |= ThemeBackgroundBrush(Brush, Image, bLight);
	bChanged |= ThemePillBrush(Brush, Image, bLight); // agent-visibility pill toggle (D51)
	if (bChanged)
	{
		Image->SetBrush(Brush);
	}
}

void UUIThemeSubsystem::StyleBorderForTheme(UBorder* Border, const bool bLight)
{
	using namespace MobiusTheme;

	// A UMobiusThemedBorder self-themes from its DECLARED role (RefreshThemedBorder, bound to
	// OnThemeChanged). Guard the TYPE, not the name — see the header for why the value remap below is
	// actively harmful on one rather than merely redundant.
	if (!Border || Border->IsA<UMobiusThemedBorder>())
	{
		return;
	}

	// Roles the value remap cannot distinguish are set explicitly per theme by widget-name substring, and a
	// match fully OWNS the widget — so return instead of falling through, as the deleted walk did. After
	// the border migration this table is down to six live targets (HeatmapColourBand_1..6, outline only);
	// every other name-rule target is already a UMobiusThemedBorder and was excluded above.
	if (ApplyNameRoleOverride(Border, bLight))
	{
		return;
	}

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
	//
	// Reads GetBrushColor() back rather than the local, so it sees the POST-remap value — the original
	// walk branch did the same and the distinction matters: a border remapped to white this pass must not
	// then have its tint neutralised as well.
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

void UUIThemeSubsystem::StyleButtonForTheme(UButton* Button, const bool bLight)
{
	using namespace MobiusTheme;

	// A UBaseButton self-themes (its own OnThemeChanged bind -> RefreshThemedButtonStyle), and a ribbon tab
	// is a UButtonWithText, so this single test excludes both. See the header for why touching them here
	// would be actively harmful rather than merely redundant.
	if (!Button || Button->IsA<UBaseButton>())
	{
		return;
	}

	FButtonStyle Style = Button->GetStyle();
	bool bChanged = false;
	// Index rather than range-for: ThemeIconBrush needs to know WHICH state it is painting, or every icon
	// button's hover collapses onto its rest colour (S10). Array order is the enum order.
	FSlateBrush* Brushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
	for (int32 StateIndex = 0; StateIndex < UE_ARRAY_COUNT(Brushes); ++StateIndex)
	{
		FSlateBrush* Brush = Brushes[StateIndex];
		// Button (not Style) is the MID outer, exactly as the walk passed it: the icon/pill/background
		// helpers create their MIDs against it, and an outer that dies takes the themed material with it.
		bChanged |= RemapBrush(*Brush, bLight);
		bChanged |= ThemeIconBrush(*Brush, Button, bLight, static_cast<EIconBrushState>(StateIndex));
		bChanged |= ThemeBackgroundBrush(*Brush, Button, bLight);
		bChanged |= ThemePillBrush(*Brush, Button, bLight); // agent-visibility pill toggle (D51)
	}

	// EXPLICIT, not a remap: the authored foregrounds are per-widget sentinels (cyan on the icon buttons)
	// that no TextMap pair matches, so any UseForeground content resolving through these styles never
	// themed in either direction. Guarded by the inequality so a second pass in one theme writes nothing.
	const FSlateColor ButtonForeground(PaletteColor(EMobiusPaletteRole::ButtonText, bLight));
	if (Style.NormalForeground != ButtonForeground)
	{
		Style.NormalForeground = ButtonForeground;
		Style.HoveredForeground = ButtonForeground;
		Style.PressedForeground = ButtonForeground;
		bChanged = true;
	}

	if (bChanged)
	{
		Button->SetStyle(Style);
	}

	// Some buttons (floor-stat bars et al) get their colour from UButton::BackgroundColor, which
	// MULTIPLIES the (white) style brushes — remap it too or they stay dark.
	FLinearColor ButtonBackground = Button->GetBackgroundColor();
	if (Remap(ButtonBackground, bLight, SurfaceMap))
	{
		Button->SetBackgroundColor(ButtonBackground);
	}
}

void UUIThemeSubsystem::StyleTextBlockForTheme(UTextBlock* TextBlock, const bool bLight)
{
	using namespace MobiusTheme;

	if (!TextBlock)
	{
		return;
	}

	// bGuardNeutralWhite stays false: white text is a real role (the single largest bucket — 90 of the 192
	// matched blocks ride it), unlike white on a brush, where it is the neutral multiplier.
	FSlateColor Color = TextBlock->GetColorAndOpacity();
	if (RemapSlate(Color, bLight, TextMap, /*bGuardNeutralWhite*/ false))
	{
		TextBlock->SetColorAndOpacity(Color);
	}
}

void UUIThemeSubsystem::StyleSliderForTheme(USlider* Slider, const bool bLight)
{
	using namespace MobiusTheme;

	if (!Slider)
	{
		return;
	}

	if (Slider->GetName() == TEXT("PlaybackSlider"))
	{
		// Q51/C4 (CR item B): the scrub track + accent-@35% fill are drawn by a UProgressBar (ScrubFillBar)
		// layered BEHIND this slider, so the slider itself must be bar-transparent — only its accent thumb
		// shows on top.
		Slider->SetSliderBarColor(FLinearColor::Transparent);
	}
	else
	{
		// A5: the walk used to run this through the SurfaceMap value pairs, which protected gradient / data
		// bars only as a side effect of them matching no palette row. Test the property directly instead:
		// a GREYSCALE bar is chrome and takes the SliderTrack role; a saturated one is data (the material
		// picker's HSV sliders) and is left exactly as authored. Same outcome, no value table.
		const FLinearColor Bar = Slider->GetSliderBarColor();
		const bool bGreyscaleBar = FMath::IsNearlyEqual(Bar.R, Bar.G, 0.02f) && FMath::IsNearlyEqual(Bar.G, Bar.B, 0.02f);
		if (bGreyscaleBar && Bar.A > 0.0f)
		{
			Slider->SetSliderBarColor(PaletteColor(EMobiusPaletteRole::SliderTrack, bLight));
		}
	}

	// Q24: force the thumb to the accent per theme (design: slider thumb = accent). Explicit, not a
	// remap — the stock thumbs are grey and match no accent bucket.
	Slider->SetSliderHandleColor(PaletteColor(EMobiusPaletteRole::SliderThumb, bLight));

	// D171: neutralize the slider double-tint. SetSliderHandleColor (above) and the bar colour are
	// multipliers applied ON TOP of the style's brush TintColors. A slider that baked the accent/track
	// INTO its brush tint (e.g. OpacitySlider: thumb tint = accent) multiplies twice -> accent^2 (a dark
	// navy handle) / dark track. Force the style brush tints to white so the colour multipliers render
	// cleanly. Thumbs always (flat); bars only when resource-less (leave gradient / HSV-picker bars,
	// which legitimately carry their colour in the brush, untouched).
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

void UUIThemeSubsystem::StyleCheckBoxForTheme(UCheckBox* CheckBox, const bool bLight)
{
	using namespace MobiusTheme;

	if (!CheckBox)
	{
		return;
	}

	// A20 (2026-08-03): the `*ThemeToggle*` early-return that used to sit here is GONE. It protected a
	// bespoke pill/track control assembled out of a checkbox; the UI-theme control is now a two-segment
	// Light|Dark button pair (UThemeToggleWidget), so there is no pill left to protect and one fewer
	// widget-NAME special case in this file.

	// A5: the walk also ran RemapBrush over all nine brushes first. Dropped — the six explicit writes
	// below fully overwrite every brush that mattered, and the three Undetermined* brushes are dead
	// (nothing in Mobius uses ECheckBoxState::Undetermined; grepped). That removes the last value-match
	// from the checkbox path.
	//
	// PANEL REBUILD (2026-08-04) — the checked state is now an IMAGE brush again, restoring the tick.
	// Q24/C4 used DrawAs=RoundedBox for all six brushes and recorded "the white-check glyph needs a
	// composite/checkmark brush asset" as an open limitation. It never needed one: Slate's RoundedBox
	// renderer ignores a brush's texture, so the tick the engine already ships was simply not being drawn.
	// Measured from the engine art itself — Engine/Content/Slate/Common/CheckBox_Checked.png is a solid
	// 16x16 white box with the check KNOCKED OUT (alpha 0), so one Image brush tinted CheckboxCheckedBg
	// gives an accent box with the surface behind showing through as the tick. Owner ruling: use the image,
	// adjust size + tint.
	//
	// TRAP: CheckBox_Checked_Hovered.png has NO tick (verified pixel by pixel — it is a plain filled box).
	// Every checked state therefore points at CheckBox_Checked.png; do not "fix" hovered to use the
	// _Hovered art, that is the mis-assignment WBP_SettingToggleCompBase shipped with.
	FCheckBoxStyle Style = CheckBox->GetWidgetStyle();

	const FLinearColor Accent = PaletteColor(EMobiusPaletteRole::CheckboxCheckedBg, bLight);
	const FLinearColor BoxBg = PaletteColor(EMobiusPaletteRole::CheckboxBg, bLight);
	const FLinearColor BoxBorder = PaletteColor(EMobiusPaletteRole::CheckboxBorder, bLight);

	// Radius 0: the mockup draws square 16x16 boxes, and square is also the checked PNG's silhouette, so
	// ticking a box does not change its outline shape.
	auto ApplyRoundedBox = [](FSlateBrush& B, const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth)
	{
		// Mutate in place so the asset's authored ImageSize survives.
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.SetResourceObject(nullptr);
		B.TintColor = FSlateColor(Fill);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.CornerRadii = FVector4(0.0, 0.0, 0.0, 0.0);
		B.OutlineSettings.Color = FSlateColor(Outline);
		B.OutlineSettings.Width = OutlineWidth;
	};

	auto ApplyTickImage = [&Accent](FSlateBrush& B)
	{
		// Rebuild rather than mutate: ResourceName has no setter, and the asset's authored name points at
		// the tickless _Hovered art. FSlateImageBrush's ctor is the only way to (re)name the resource.
		const FVector2D Size = B.GetImageSize().IsNearlyZero() ? FVector2D(16.0, 16.0) : FVector2D(B.GetImageSize());
		B = FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Slate/Common/CheckBox_Checked.png"), Size, Accent);
	};

	ApplyTickImage(Style.CheckedImage);
	ApplyTickImage(Style.CheckedHoveredImage);
	ApplyTickImage(Style.CheckedPressedImage);
	ApplyRoundedBox(Style.UncheckedImage, BoxBg, BoxBorder, 1.0f);
	ApplyRoundedBox(Style.UncheckedHoveredImage, BoxBg, BoxBorder, 1.0f);
	ApplyRoundedBox(Style.UncheckedPressedImage, BoxBg, BoxBorder, 1.0f);
	CheckBox->SetWidgetStyle(Style);
}

void UUIThemeSubsystem::StyleScrollBoxForTheme(UScrollBox* ScrollBox, const bool bLight)
{
	if (!ScrollBox)
	{
		return;
	}

	// Audit #4: a UScrollBox's authored scrollbar thumb colour stayed fixed across a toggle (dark thumb on
	// light chrome / vice-versa) whenever a list overflowed. Tint the three thumb states per theme (grey
	// thumb readable on either chrome); track/background left to the box style. No dedicated scrollbar
	// palette role, so explicit greys: light #adadad, dark #9a9a9a.
	FScrollBarStyle BarStyle = ScrollBox->GetWidgetBarStyle();
	const FLinearColor ThumbTint = bLight ? FLinearColor(0.4179f, 0.4179f, 0.4179f) : FLinearColor(0.323f, 0.323f, 0.323f);
	FSlateBrush* ThumbBrushes[] = { &BarStyle.NormalThumbImage, &BarStyle.HoveredThumbImage, &BarStyle.DraggedThumbImage };
	for (FSlateBrush* ThumbBrush : ThumbBrushes)
	{
		ThumbBrush->TintColor = FSlateColor(ThumbTint);
	}
	ScrollBox->SetWidgetBarStyle(BarStyle);
}

void UUIThemeSubsystem::StyleProgressBarForTheme(UProgressBar* ProgressBar, const bool bLight)
{
	using namespace MobiusTheme;

	if (!ProgressBar)
	{
		return;
	}

	if (ProgressBar->GetName() == TEXT("ScrubFillBar"))
	{
		// Q51/C4 (CR item B): scrub fill behind PlaybackSlider — a flat SliderTrack-grey track with an
		// accent @35%-alpha fill (NOT the §3.8 loading-bar look). Both themes via palette. The fill is
		// white-tinted in the style and coloured via FillColorAndOpacity so the 0.35 alpha is exact.
		FProgressBarStyle ScrubStyle = ProgressBar->GetWidgetStyle();
		ScrubStyle.EnableFillAnimation = false;
		ScrubStyle.BackgroundImage.DrawAs = ESlateBrushDrawType::Box;
		ScrubStyle.BackgroundImage.SetResourceObject(nullptr);
		ScrubStyle.BackgroundImage.OutlineSettings.Width = 0.0f;
		ScrubStyle.BackgroundImage.TintColor = FSlateColor(PaletteColor(EMobiusPaletteRole::SliderTrack, bLight));
		ScrubStyle.FillImage.DrawAs = ESlateBrushDrawType::Box;
		ScrubStyle.FillImage.SetResourceObject(nullptr);
		ScrubStyle.FillImage.OutlineSettings.Width = 0.0f;
		ScrubStyle.FillImage.TintColor = FSlateColor(FLinearColor::White);
		ProgressBar->SetWidgetStyle(ScrubStyle);
		FLinearColor ScrubFill = PaletteColor(EMobiusPaletteRole::Accent, bLight);
		ScrubFill.A = 0.35f;
		ProgressBar->SetFillColorAndOpacity(ScrubFill);
		return;
	}

	// §3.8 (D55): progress bars = accent fill on an input-bg track with a 1u input-border. The fill image
	// is a material/texture on some bars — tint it to accent (keeps any authored shape); the background
	// becomes a rounded input-bg box. Height (11u) is asset geometry, not style.
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

void UUIThemeSubsystem::StyleEditableTextBoxForTheme(UEditableTextBox* EditBox, const bool bLight, const bool bApplyFont)
{
	using namespace MobiusTheme;

	if (!EditBox)
	{
		return;
	}

	bool bStyleChanged = false;

	// Q26: numeric/path edit boxes → Font_Inter Mono (input_mono token). THEME-INDEPENDENT, so it runs on
	// the construct pass only — there is nothing for a theme change to re-decide. Convert only boxes not
	// already on Inter, so a deliberate Inter face/size is never re-stomped. (This lived in the walk
	// because that was the one C++ hook touching every live widget; FEditableTextBoxStyle.Font is not
	// settable from Python either.)
	if (bApplyFont)
	{
		if (UFont* Inter = GetInterFont())
		{
			if (EditBox->WidgetStyle.TextStyle.Font.FontObject != Inter)
			{
				EditBox->WidgetStyle.TextStyle.Font = FSlateFontInfo(Inter, 11, FName(TEXT("Mono"))); // BW6 density: 14->11
				bStyleChanged = true;
			}
		}
	}

	// Edit boxes carry their surface in FEditableTextBoxStyle, which the Border/Image value walk never
	// reaches — a dark-authored input (e.g. the flow-counter rename field) stayed dark in light mode. Set
	// the input palette roles EXPLICITLY: a generic SurfaceMap remap is not safe here — the input values
	// collide with other rows (0.0243 matched a rail pair), so each theme toggle walked the fill to a
	// different bucket (0.0243 -> 0.791 -> 0.913 drift).
	const FLinearColor InputBg = PaletteColor(EMobiusPaletteRole::InputBg, bLight);
	const FLinearColor InputBorderColor = PaletteColor(EMobiusPaletteRole::InputBorder, bLight);
	FSlateBrush* BoxBrushes[] =
	{
		&EditBox->WidgetStyle.BackgroundImageNormal, &EditBox->WidgetStyle.BackgroundImageHovered,
		&EditBox->WidgetStyle.BackgroundImageFocused, &EditBox->WidgetStyle.BackgroundImageReadOnly,
	};
	for (FSlateBrush* Brush : BoxBrushes)
	{
		// Flat colour fills only — leave any texture/material-backed input art alone.
		if (Brush->GetResourceObject() != nullptr)
		{
			continue;
		}
		if (!Brush->TintColor.GetSpecifiedColor().Equals(InputBg, 0.004f))
		{
			Brush->TintColor = FSlateColor(InputBg);
			bStyleChanged = true;
		}
		// 2026-08-11 (owner: "no outline/border on text input to indicate it clickable in light mode"). This
		// used to recolour an outline ONLY where one already existed (`Width > 0`), so an input authored with
		// no outline — the playback speed box, and every box authored from the engine default — stayed a
		// borderless white rectangle on the light card, i.e. invisible as a control. GIVE it the border
		// instead of merely tinting it, matching the closed-combo surface convention (InputBg fill, 1px
		// InputBorder, 3u radius) so an edit box and a combo read as the same kind of control.
		//
		// Width is only ever RAISED to 1: an asset that deliberately authored a heavier rule keeps it.
		// RoundedBox is set only on brushes that are already flat colour — the texture/material guard above
		// has returned by this point, so authored input art is untouched.
		if (Brush->DrawAs != ESlateBrushDrawType::RoundedBox)
		{
			Brush->DrawAs = ESlateBrushDrawType::RoundedBox;
			Brush->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush->OutlineSettings.CornerRadii = FVector4(3.0, 3.0, 3.0, 3.0);
			bStyleChanged = true;
		}
		if (Brush->OutlineSettings.Width < 1.0f)
		{
			Brush->OutlineSettings.Width = 1.0f;
			bStyleChanged = true;
		}
		if (!Brush->OutlineSettings.Color.GetSpecifiedColor().Equals(InputBorderColor, 0.004f))
		{
			Brush->OutlineSettings.Color = FSlateColor(InputBorderColor);
			bStyleChanged = true;
		}
	}

	// Text colour: set the InputText palette role EXPLICITLY. Do NOT value-remap these — edit boxes
	// commonly leave ForegroundColor/TextStyle colour on an inheritance rule, and reading
	// GetSpecifiedColor() off those yields the magenta sentinel, which a remap-write then bakes in.
	const FSlateColor InputTextColor(PaletteColor(EMobiusPaletteRole::InputText, bLight));
	if (EditBox->WidgetStyle.TextStyle.ColorAndOpacity != InputTextColor
		|| EditBox->WidgetStyle.ForegroundColor != InputTextColor
		|| EditBox->WidgetStyle.FocusedForegroundColor != InputTextColor)
	{
		EditBox->WidgetStyle.TextStyle.ColorAndOpacity = InputTextColor;
		EditBox->WidgetStyle.ForegroundColor = InputTextColor;
		EditBox->WidgetStyle.FocusedForegroundColor = InputTextColor;
		bStyleChanged = true;
	}

	if (bStyleChanged)
	{
		EditBox->SynchronizeProperties(); // push the style to the live SEditableTextBox
	}
}

/**
 * A6b-6 (2026-07-31): this WAS ApplyToLiveWidgets, the legacy value-matching walk. Everything it styled is
 * now owner-pull (each widget's construct + OnThemeChanged), so the styling call is gone and what remains is
 * the traversal, used ONLY as the startup ticker's "has the HUD constructed yet" signal.
 *
 * Kept as the walk's traversal verbatim rather than replaced by something cheaper, deliberately: the ticker
 * compares the result against a hard-coded 200, and that number was calibrated against THIS shape — the
 * TopLevelOnly=false enumeration, the 16-deep on-screen ancestor filter, and the non-UUserWidget leaf
 * predicate. Any other counting shape decalibrates it into one of the two documented failures — count too
 * low and the ticker unregisters while the load screen is still up (the ribbon never gets a pass and its tab
 * labels keep construct-time white), count too high and it never crosses at all.
 */
int32 UUIThemeSubsystem::CountLiveLeafWidgets()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// Guard PIE-teardown: a tearing-down world can be non-null while its widgets are destroyed. This
	// function no longer writes to them, but it still READS IsInViewport/GetParent on each, and that was
	// enough to trip the dying-combo SMenuAnchor data-race ensure.
	if (!World || World->bIsTearingDown)
	{
		return 0;
	}

	// TopLevelOnly=false returns every live UUserWidget, embedded ones included, so each widget contributes
	// only its OWN tree (embedded user widgets are skipped as tree nodes below, so nothing double-counts).
	TArray<UUserWidget*> AllUserWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, AllUserWidgets, UUserWidget::StaticClass(), false);
	int32 WidgetsVisited = 0;
	for (UUserWidget* UserWidget : AllUserWidgets)
	{
		if (!UserWidget || !UserWidget->WidgetTree)
		{
			continue;
		}
		// In-world widgets on a UWidgetComponent are world-space and are NOT returned by
		// GetAllWidgetsOfClass anyway; the ones that ARE returned but live outside the viewport (outer =
		// game instance, not the component; child user widgets have parents, so climb to the owning
		// top-level before deciding) get skipped here — an off-screen widget must not contribute to the
		// "is the HUD up" count. Flow-counter cards theme via their own OnThemeChanged bind
		// (UFlowCounterWidget) plus ThemeInWorldWidgetComponents, neither of which needs this count.
		{
			bool bOnScreenUI = false;
			UUserWidget* Current = UserWidget;
			for (int32 Guard = 0; Guard < 16 && Current; ++Guard)
			{
				if (Current->IsInViewport())
				{
					bOnScreenUI = true;
					break;
				}
				UWidget* Root = Current;
				while (UWidget* Up = Root->GetParent())
				{
					Root = Up;
				}
				UUserWidget* Owner = Root->GetTypedOuter<UUserWidget>();
				if (Owner == Current)
				{
					break;
				}
				Current = Owner;
			}
			if (!bOnScreenUI)
			{
				continue;
			}
		}
		UserWidget->WidgetTree->ForEachWidget([&WidgetsVisited](UWidget* Widget)
		{
			if (Widget && !Widget->IsA<UUserWidget>())
			{
				++WidgetsVisited;
			}
		});
	}
	// A11/A6a (2026-07-28): per-pass UE_LOG removed. The startup ticker calls this up to 100 times in the
	// first 30 s, so it was ~100 Display lines before the user sees anything. The ticker's own pass-count
	// logic uses the RETURN value, not the log, so nothing depended on it.
	return WidgetsVisited;
}

bool UUIThemeSubsystem::ApplyNameRoleOverride(UWidget* Widget, const bool bLight)
{
	using namespace MobiusTheme;

	UBorder* Border = Cast<UBorder>(Widget);
	if (!Border)
	{
		return false;
	}

	const FNameRole* Entry = FindNameRole(Widget->GetName());
	if (!Entry)
	{
		return false;
	}

	const FLinearColor Color = PaletteColor(Entry->Role, bLight);
	if (Entry->Target == EThemeRoleTarget::BorderFill)
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

// =================================================================================================
// S8 — CONTRASTING BAND LETTERS (2026-08-07)
//
// Stakeholder: "the banding letters need contrasting font to the selected color for band unsure how to
// know when to use black or white font depending upon its selected color". The rule is exact and lives in
// MobiusThemePalette.h (ContrastingLabelColor) — see that header for the derivation and its two traps.
//
// ONE WRITER. The band letter is a UTextBlock, so ThemeStandardControlsInTree's text branch would
// otherwise remap it like any other label; that is why the refresh runs as a POST-PASS after that loop
// and not from inside the UBorder branch. Order within the loop cannot be relied on — the letter is a
// separate widget and ForEachWidget may reach it either side of its chip.
//
// The theme pass is also the only reason the letters are correct at construct without any Blueprint
// change. The contrast itself is theme-INDEPENDENT (it is computed from a data colour, not a palette
// role), so re-running it per theme is not about the theme — it is about being the last writer.
// =================================================================================================

void UUIThemeSubsystem::RefreshDataChipLabel(UBorder* Chip)
{
	UTextBlock* Label = Chip ? MobiusTheme::ResolveDataChipLabel(Chip) : nullptr;
	if (!Label)
	{
		return;
	}

	// THE DOUBLE-TINT. SBorder paints its brush's TintColor MULTIPLIED by the widget's BrushColor, so
	// neither one alone is the colour on screen. Feeding a single channel to ContrastingLabelColor would
	// pick against a background that is not being drawn. A brush whose tint is "use foreground" specifies
	// no colour of its own and multiplies by white.
	const FSlateColor& Tint = Chip->Background.TintColor;
	const FLinearColor TintColor = Tint.IsColorSpecified() ? Tint.GetSpecifiedColor() : FLinearColor::White;
	const FLinearColor EffectiveFill = TintColor * Chip->GetBrushColor();

	Label->SetColorAndOpacity(FSlateColor(MobiusThemePalette::ContrastingLabelColor(EffectiveFill)));
}

void UUIThemeSubsystem::SetDataChipFill(UBorder* Chip, const FLinearColor Fill)
{
	if (!Chip)
	{
		return;
	}

	// Normalise the double-tint before writing, so `Fill` is the colour that actually appears whichever
	// channel the asset happened to author the band colour in. Without this, a chip whose BRUSH tint
	// carries the old band colour would render TintColor x Fill — a colour nobody asked for, and the
	// label would then be contrasted against it "correctly", which is the worst kind of right.
	// This is the project's standing rule for themed borders (colour in BrushColor, tint white).
	FSlateBrush Brush = Chip->Background;
	if (!Brush.TintColor.GetSpecifiedColor().Equals(FLinearColor::White, 0.001f))
	{
		Brush.TintColor = FSlateColor(FLinearColor::White);
		Chip->SetBrush(Brush);
	}

	Chip->SetBrushColor(Fill);
	RefreshDataChipLabel(Chip);
}

void UUIThemeSubsystem::ApplySharedStyles(const bool bLight)
{
	using namespace MobiusTheme;

	// A10b step 6 (2026-08-14): the SWS_SettingButtonStyle colour block that used to sit here was a DEAD
	// WRITE and is deleted. Its only reader was GetThemedTabStyle, which copied the asset and then
	// overwrote every field it had just been given: all four brushes get SetResourceObject(TabMaterial)
	// + TintColor = White, all four foregrounds are re-derived, and both paddings are replaced. So none
	// of the tints or foregrounds above ever reached a pixel. Same shape as the SWS_PanelButtonStyle
	// branch deleted in 6b176090; GetThemedTabStyle now builds from MobiusButtonGeometry::Tab instead.

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
			// Nothing READS SWS_SettingButtonStyle any more (GetThemedTabStyle builds from
			// MobiusButtonGeometry::Tab as of 2026-08-14), but it is still on disk — and letting it into
			// the branch below would MUTATE the shared asset object in memory on every theme apply, which
			// dirties a public-repo .uasset and lets a Save All in that editor bake theme colours to disk.
			// Skip it until the asset itself is deleted, then delete this too.
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
				// Indexed for the same reason as StyleButtonForTheme: this is the path that themes
				// SWS_PlayButtonActive / SWS_PlayButtonPaused, whose Hovered brush is the play/pause
				// button's ONLY hover (USimulationPlayBar::SetPlayButtonStyle swaps the whole style from
				// these assets), so a state-blind call here silently flattens it.
				FSlateBrush* Brushes[] = { &ButtonStyle->Normal, &ButtonStyle->Hovered, &ButtonStyle->Pressed, &ButtonStyle->Disabled };
				for (int32 StateIndex = 0; StateIndex < UE_ARRAY_COUNT(Brushes); ++StateIndex)
				{
					FSlateBrush* Brush = Brushes[StateIndex];
					bChanged |= RemapBrush(*Brush, bLight);
					bChanged |= ThemeIconBrush(*Brush, StyleAsset, bLight, static_cast<EIconBrushState>(StateIndex));
					bChanged |= ThemeBackgroundBrush(*Brush, StyleAsset, bLight);
				}
				// Round 11: EXPLICIT foregrounds. The authored values on these SWS assets are magenta
				// sentinels no TextMap pair matches — they sat magenta in BOTH themes and any
				// UseForeground text resolving through them (combo default content, generated entries)
				// never themed. (SWS_SettingButtonStyle keeps its bespoke tab foregrounds above.)
				{
					const FSlateColor AssetButtonForeground(PaletteColor(EMobiusPaletteRole::ButtonText, bLight));
					if (ButtonStyle->NormalForeground != AssetButtonForeground)
					{
						ButtonStyle->NormalForeground = AssetButtonForeground;
						ButtonStyle->HoveredForeground = AssetButtonForeground;
						ButtonStyle->PressedForeground = AssetButtonForeground;
						bChanged = true;
					}
				}
				// A10b (2026-08-06): the SWS_PanelButtonStyle branch that used to sit here — stamping
				// Normal/Hovered/Pressed TintColor plus an outline colour onto the shared asset — is GONE,
				// because it was a DEAD WRITE. UBaseButton::ApplyMobiusButtonStyle snapshots this asset and
				// RefreshThemedButtonStyle then overwrites all four brush tints from ButtonBg/ButtonHoverBg/
				// ButtonPressedBg and the outline from ButtonBorder, so nothing it wrote ever reached a
				// pixel. That is why light renders the bluish palette hover and not the neutral 0.8228 this
				// branch wrote — confirmed on screen by the owner before it was removed.
				//
				// Gated first, because four escapes would have kept the asset colour visible: a consumer
				// that is not a UBaseButton, bFollowThemePalette cleared, bIsRibbonButton set (ribbon tabs
				// route to ApplyRibbonTabStyle), or Normal outline width > 1.5 (the accent-ring early-out) —
				// plus Recolour skipping any brush that carries a resource object. All 37 consumers came
				// back UButtonWithText, palette-following, non-ribbon, ROUNDED_BOX, outline 1.0, no resource
				// object: every escape empty.
				//
				// This also carried the last tokenless literals in the function. The 0.964/0.052861 fill
				// matched no design token and had an open owner ruling, which deleting the write closed
				// rather than answered — no role was invented (EMobiusPaletteRole static_asserts its count).
				// Do not re-add: colour for these buttons belongs to the palette subsystem, per the
				// 2026-08-06 ruling that widgets theme from the subsystem and not from an SWS asset. The
				// asset is still the source of GEOMETRY (corner radii, outline width, padding, sounds) until
				// that half moves to FMobiusButtonGeometry.
				//
				// A10 (2026-08-05): the "current tier" chip branch that used to sit here — matching
				// SWS_ScaleabilityButtonCurrentSet and rebuilding it as a flat rounded box with an accent
				// ring — is GONE, because the asset is. The panel rebuild replaced the five tier buttons
				// with a segmented control that styles itself in C++ (UGlobalQualitySegmentWidget), which
				// left the asset with no consumer; it now lives in /Game/99_Old/. This sweep only walks
				// /Game/01_Dev/Widgets, so the branch could never have matched again. Retiring it is what
				// finally makes this whole function's remaining behaviour asset-colour-only.
				//
				// Bottom-bar play/pause (§3.6 round-53 accent ring): the play/pause glyph is a MATERIAL brush
				// (MI_PlayButton/MI_PauseButton). Slate's RoundedBox draw type routes materials through the
				// material shader and does not apply the rounded-corner mask/outline, so converting DrawAs
				// here would give no ring at best and a dropped glyph at worst. Deferred to asset/material
				// work (round the MI background + bake an accent ring, OR a texture glyph on a RoundedBox).
				// Only the ACCENT-RING colour is queued; the C++ swap path (SetPlayButtonStyle) stays intact.
			}
			// A10b step 6 (2026-08-14): the SWS_*TextStyle branch that stood here is DELETED. It retinted and
			// re-faced seven text-style assets that no widget reads any more — UButtonWithText and
			// UFieldAndTextWidget both build from the shared Mobius.Text.* ramp now, and each pushes its own
			// colour (ApplyThemedLabelColor / RefreshThemedStyle), size (per-consumer fit-to-box) and Mono face
			// (SetFieldFontFace). The branch also carried the last writer that MUTATED a shared .uasset in
			// memory on every theme apply, which is what let a Save All bake theme colours into the public repo.
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
	// A11/A6a (2026-07-28): per-pass UE_LOG removed, same reason as the one in ApplyToLiveWidgets — the
	// startup ticker re-runs this function on every tick until the UI settles. StyleAssetsThemed is now
	// unused; kept as a local so the counting code above stays intact for A6b, which deletes the sweep.
	(void)StyleAssetsThemed;

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

	// Audit #5: the rest of the shared text ramp (Title/Header/Body/Field/Caption) was authored from the
	// FCoreStyle "NormalText" near-white and NEVER retinted per theme, so any consumer that falls back to it
	// (FieldAndTextWidget, the error/log dialogs) showed fixed near-white text. Re-stamp each per theme from
	// the palette so the fallback path is theme-correct in both directions. Explicit both ways (self-heals).
	auto RetintRamp = [bLight](const char* Key, EMobiusPaletteRole Role)
	{
		FTextBlockStyle& S = const_cast<FTextBlockStyle&>(FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>(Key));
		S.ColorAndOpacity = FSlateColor(MobiusThemePalette::Color(Role, bLight));
	};
	RetintRamp("Mobius.Text.Title",   EMobiusPaletteRole::LabelText);       // primary heading text
	RetintRamp("Mobius.Text.Header",  EMobiusPaletteRole::PanelHeaderText); // section-header (muted)
	RetintRamp("Mobius.Text.Body",    EMobiusPaletteRole::LabelText);       // body copy
	RetintRamp("Mobius.Text.Field",   EMobiusPaletteRole::InputText);       // value readouts
	RetintRamp("Mobius.Text.Caption", EMobiusPaletteRole::SublabelText);    // captions / hints
	// A19: the error window's reporter/source line. Was "Mobius.Text.Error.Location", hard-tinted red and
	// deliberately NOT in this list, which is why it read the same raw red in both themes. Demoted to
	// SublabelText — measured 5.3:1 light / 4.2:1 dark on the error body surface, against MicroText's
	// 3.25:1 and HintText's 2.6:1 in light, which is why the A19 row's suggestion of those two was not taken.
	RetintRamp("Mobius.Text.Source",  EMobiusPaletteRole::SublabelText);    // source / reporter attribution
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
