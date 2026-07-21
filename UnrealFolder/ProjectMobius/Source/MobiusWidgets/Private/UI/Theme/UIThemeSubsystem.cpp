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
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Engine/Engine.h"
#include "Slate/SlateBrushAsset.h"
#include "UserConfig/UserProjectSettings.h"
#include "Style/MobiusStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "UI/Components/ButtonWithText.h"
#include "UI/Components/VerticalTextBlock.h"
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
	// theme. Each pass no-ops until a game world + widgets exist. BOTH themes: "dark is the design-time
	// default" turned out false for parts of the UI (Browse buttons, checkbox labels, snapshot-at-construct
	// button styles) — a dark start with no walk left light traces everywhere. The walk is idempotent, so
	// running it on dark starts is safe. One-shot: the ticker unregisters itself after the last pass.
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
			// Returns the number of live leaf widgets themed; >0 means the UI has actually constructed
			// (it can appear seconds after launch, behind shader compilation). Keep re-applying until we
			// have themed a live UI a few times (settle late stragglers), then stop. Hard cap ~30s.
			// Re-run the SHARED-ASSET pass each tick too: at Initialize the asset registry scan is often
			// not finished, so the startup ApplySharedStyles themes ZERO SWS assets — the walk then
			// stamps disk-authored (light, magenta-foreground) styles onto every snapshot button on a
			// dark start. By these ticks the registry is ready and the assets carry the theme.
			// A pass only COUNTS when the walk was HUD-sized: on a cold start the loading screen's
			// handful of widgets used to eat all three passes, the ticker unregistered before the ribbon
			// ever constructed, and the tab labels kept their construct-time white (invisible on light).
			// The full Mobius HUD walks ~600 leaf widgets; 200 cleanly separates it from load screens.
			ApplySharedStyles(CurrentTheme == EMobiusUITheme::Light);
			if (ApplyToLiveWidgets(CurrentTheme == EMobiusUITheme::Light) > 200)
			{
				++(*ThemedPasses);
				// Force a repaint — labels re-coloured behind an InvalidationBox (ribbon tabs) keep their
				// cached white paint until invalidated (this is what a tab CLICK does via ApplyTheme).
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().InvalidateAllWidgets(false);
				}
			}
			const bool bKeepTicking = (*ThemedPasses < 3) && (++(*Passes) < 100);
			if (!bKeepTicking)
			{
				StartupThemeTickerHandle.Reset();  // self-unregistered; clear so Deinitialize won't double-remove
			}
			return bKeepTicking;
		}), 0.3f);
	}
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

	// NEW ARCHITECTURE (additive, migration Phase 2+): push the palette into MPC_UITheme so
	// material-backed chrome repaints GPU-side. No-op until the MPC asset exists. The legacy
	// value-remap walk above still runs until the migration retires it.
	WriteThemeToMPC(bLight);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}

	// Event replacement for the walk: event-driven widgets re-pull their role colours on this.
	OnThemeChanged.Broadcast();
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

void UUIThemeSubsystem::ApplyThemeToComboBox(UComboBoxString* Combo)
{
	using namespace MobiusTheme;
	if (!Combo)
	{
		return;
	}
	const bool bLight = CurrentTheme == EMobiusUITheme::Light;

	// 1) SURFACE — drive the closed-combo button by M_MobiusInput (samples MPC_UITheme.Field) so a
	//    theme flip repaints it GPU-side. Push the style at most ONCE: guard on the Normal brush already
	//    carrying the material, so a toggle never re-SetWidgetStyle a LIVE combo (that rebuild racing a
	//    menu-open is what tripped the SMenuAnchor delegate-access ensure). The one push happens at
	//    construct, before any dropdown can be open.
	UMaterialInterface* InputMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/01_Dev/Widgets/WidgetMaterials/Master/M_MobiusInput.M_MobiusInput"));
	FComboBoxStyle Style = Combo->GetWidgetStyle();
	const bool bSurfaceThemed = InputMat && Style.ComboButtonStyle.ButtonStyle.Normal.GetResourceObject() == InputMat;
	if (InputMat && !bSurfaceThemed)
	{
		FSlateBrush* Buttons[] = {
			&Style.ComboButtonStyle.ButtonStyle.Normal, &Style.ComboButtonStyle.ButtonStyle.Hovered,
			&Style.ComboButtonStyle.ButtonStyle.Pressed, &Style.ComboButtonStyle.ButtonStyle.Disabled,
		};
		for (FSlateBrush* Brush : Buttons)
		{
			Brush->SetResourceObject(InputMat);
			Brush->DrawAs = ESlateBrushDrawType::Image; // material provides the fill (MPC.Field)
			Brush->TintColor = FSlateColor(FLinearColor::White); // material carries the colour; tint neutral
		}

		// Dropdown row colours — set in the SAME one-time (guarded) pass. Flat colours; only visible
		// while the menu is open. NOTE (W1 limitation): because they are flat, not material, they do not
		// follow a later toggle — a refined menu-row material is deferred (advisor: menu rows secondary).
		FTableRowStyle Items = Combo->GetItemStyle();
		const FLinearColor RowBg = PaletteColor(EMobiusPaletteRole::InputBg, bLight);
		const FLinearColor RowText = PaletteColor(EMobiusPaletteRole::InputText, bLight);
		const FLinearColor RowSel = PaletteColor(EMobiusPaletteRole::Accent, bLight);
		auto Row = [](FSlateBrush& B, const FLinearColor& C)
		{
			B.TintColor = FSlateColor(C);
			B.DrawAs = ESlateBrushDrawType::Image;
			B.SetResourceObject(nullptr);
		};
		Row(Items.EvenRowBackgroundBrush, RowBg);        Row(Items.OddRowBackgroundBrush, RowBg);
		Row(Items.EvenRowBackgroundHoveredBrush, RowSel); Row(Items.OddRowBackgroundHoveredBrush, RowSel);
		Row(Items.ActiveBrush, RowSel);                   Row(Items.ActiveHoveredBrush, RowSel);
		Row(Items.InactiveBrush, RowBg);                  Row(Items.InactiveHoveredBrush, RowSel);
		Items.TextColor = FSlateColor(RowText);
		Items.SelectedTextColor = FSlateColor(FLinearColor::White);
		Combo->SetItemStyle(Items);

		Combo->SetWidgetStyle(Style); // the single, guarded rebuild — construct-time, never mid-open
	}

	// 2) TEXT foreground — the closed combo's selected-item text uses UseForeground, so re-land the
	//    InputText role on the compound widget EVERY call. SetForegroundColor sets an attribute; it does
	//    NOT rebuild the SComboBox / SMenuAnchor, so this is safe even while the dropdown is open.
	const FSlateColor TextColor(PaletteColor(EMobiusPaletteRole::InputText, bLight));
	if (const TSharedPtr<SWidget> Live = Combo->GetCachedWidget())
	{
		StaticCastSharedPtr<SCompoundWidget>(Live)->SetForegroundColor(TextColor);
	}
}

int32 UUIThemeSubsystem::ApplyToLiveWidgets(const bool bLight)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// Guard PIE-teardown: a tearing-down world can be non-null while its widgets are destroyed;
	// walking them reads a dying combo's SMenuAnchor delegate and trips the data-race ensure.
	if (!World || World->bIsTearingDown)
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
		// In-world widgets (flow-counter cards on a UWidgetComponent) are world-space labels with a
		// FIXED light design in both themes — walking them in dark repainted the card dark while its
		// text is pinned black (solid black bars). They are trees whose OUTERMOST user widget was
		// never added to the viewport (outer = game instance, not the component, so an outer check
		// doesn't work — and their child user widgets DO have parents, so climb to the owning
		// top-level before deciding).
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

	// CRASH GUARD + refactor step (2026-07-21): skip UComboBoxString entirely. The per-pass churn on
	// combos (SetWidgetStyle/SetItemStyle/SetForegroundColor rebuilding the live SComboBox + its
	// SMenuAnchor) is the prime suspect for the SMenuAnchor delegate-access ensure (crash on combo-open
	// AND on PIE close — ~SMenuAnchor::Unbind racing this walk). Combos will theme via the MPC/event path
	// instead; until then their dropdown rows fall back to default styling. If this stops the crash, the
	// combo-churn / lifetime (UAF) theory is confirmed.
	if (Widget && Widget->IsA<UComboBoxString>())
	{
		return;
	}

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
	else if (UVerticalTextBlock* VerticalText = Cast<UVerticalTextBlock>(Widget))
	{
		// Round 11: the rail labels (Floor Stats / Flow Counters) copy their style at construct and had
		// no live re-land — after a toggle they kept the previous theme's colour (black-on-dark rail).
		VerticalText->RefreshThemedStyle();
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
		// Buttons with an SWS style asset SNAPSHOT it at construct (BaseButton::ApplyMobiusButtonStyle),
		// so a theme flip leaves the live copy's foregrounds/hover tints stale even though
		// ApplySharedStyles retinted the asset — e.g. the side tool tabs hovered light-on-light in
		// light mode. Re-copy the freshly themed asset FIRST (no-op when no asset is assigned), then
		// let the value remaps below handle asset-less buttons as before.
		// EXCEPT the three ribbon tabs: their Normal brush carries the BP-managed active-tab material
		// ("TabSelected") that the D173 label block below reads for active/inactive detection — a
		// re-copy from the base SWS asset wipes that material every pass and degrades the active look.
		const FString WalkButtonName = Widget->GetName();
		const bool bRibbonTabButton =
			WalkButtonName.Contains(TEXT("FilesPanelBtn")) ||
			WalkButtonName.Contains(TEXT("DisplaylPanelBTN")) ||
			WalkButtonName.Contains(TEXT("HelpPanelBtn"));
		if (!bRibbonTabButton)
		{
			if (UBaseButton* MobiusButton = Cast<UBaseButton>(Widget))
			{
				MobiusButton->ApplyMobiusButtonStyle();
			}
		}
		FButtonStyle Style = Button->GetStyle();
		bool bChanged = false;
		if (bRibbonTabButton)
		{
			// Round 11: skipping the re-copy (round 10) kept the active-tab material alive but nothing
			// re-themed it — the ribbon's click BP swaps MI_TabSelected/MI_TabDefault from the folder of
			// the theme CURRENT AT CLICK TIME, so after a toggle the tabs sat on the previous theme's
			// materials ("traces of the last theme", active File tab white-in-dark). Re-land the
			// same-ROLE material from the current theme's folder on every material-carrying state brush.
			// Name-based, so the D173 active detection below (and the click BP) keep working.
			const UObject* CurrentRes = Style.Normal.GetResourceObject();
			const bool bActiveTab = CurrentRes && CurrentRes->GetName().Contains(TEXT("TabSelected"));
			if (UMaterialInterface* TabMaterial = GetThemedTabMaterial(bActiveTab))
			{
				FSlateBrush* TabBrushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
				for (FSlateBrush* TabBrush : TabBrushes)
				{
					if (TabBrush->GetResourceObject() && TabBrush->GetResourceObject() != TabMaterial)
					{
						TabBrush->SetResourceObject(TabMaterial);
						bChanged = true;
					}
				}
			}
		}
		FSlateBrush* Brushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
			bChanged |= ThemeIconBrush(*Brush, Button, bLight);
			bChanged |= ThemeBackgroundBrush(*Brush, Button, bLight);
			bChanged |= ThemePillBrush(*Brush, Button, bLight); // agent-visibility pill toggle (D51)
		}
		if (bRibbonTabButton)
		{
			// Tab foregrounds stay on the value walk; the label colour is painted explicitly below (D173).
			bChanged |= RemapSlate(Style.NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
			bChanged |= RemapSlate(Style.HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
			bChanged |= RemapSlate(Style.PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		}
		else
		{
			// Round 11: EXPLICIT foregrounds. The authored values are per-widget sentinels (cyan on the
			// icon buttons) that no TextMap pair matches, so any UseForeground content resolving through
			// these styles never themed in either direction.
			const FSlateColor ButtonForeground(PaletteColor(EMobiusPaletteRole::ButtonText, bLight));
			if (Style.NormalForeground != ButtonForeground)
			{
				Style.NormalForeground = ButtonForeground;
				Style.HoveredForeground = ButtonForeground;
				Style.PressedForeground = ButtonForeground;
				bChanged = true;
			}
		}
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
		// W2 LABEL EXPERIMENT (2026-07-21): the walk's per-label colour handling for UButtonWithText
		// (RefreshTextStyle + the three ApplyThemedLabelColor cases: ribbon-tab / custom-SWS / shared
		// Mobius.Text.Label) is DISABLED. The label STextBlock is constructed with
		// ColorAndOpacity(FSlateColor::UseForeground()) (ButtonWithText::RebuildWidget), so it should
		// follow the button STYLE foreground the block ABOVE still sets (ribbon-tab NormalForeground
		// remap + non-ribbon ButtonText role). Discriminates two theories for the invisible ribbon-tab /
		// tool-panel labels: if they now appear + track active/inactive, the walk was clobbering
		// UseForeground with white (candidate B) and this is the clean W2 label endpoint; if still white,
		// UseForeground isn't reaching the label (candidate A) → explicit per-state label colour needed.
		// KEEP the foreground remaps above (they feed UseForeground). Restore this block only if candidate A.
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
		// Round 11: EXPLICIT combo foregrounds per theme. The authored values are cyan sentinels no
		// TextMap pair matches, so the closed combo's DEFAULT content text (engine STextBlock on the
		// FCoreStyle "NormalText" UseForeground rule) rendered near-black in BOTH themes — invisible
		// on the dark chrome (owner report: Display-tab dropdowns).
		const FSlateColor ComboTextColor(PaletteColor(EMobiusPaletteRole::InputText, bLight));
		if (Style.ComboButtonStyle.ButtonStyle.NormalForeground != ComboTextColor)
		{
			Style.ComboButtonStyle.ButtonStyle.NormalForeground = ComboTextColor;
			Style.ComboButtonStyle.ButtonStyle.HoveredForeground = ComboTextColor;
			Style.ComboButtonStyle.ButtonStyle.PressedForeground = ComboTextColor;
			bChanged = true;
		}
		if (bChanged)
		{
			ComboBox->SetWidgetStyle(Style);
		}
		// The live SComboBox was constructed with .ForegroundColor(<authored per-widget property>) —
		// style foregrounds never reach an already-built widget and UComboBoxString has no
		// post-construction setter (SetForegroundColor ensures !MyComboBox). Set the compound
		// foreground directly so the UseForeground content re-lands per theme (idempotent, toggle-safe).
		if (const TSharedPtr<SWidget> LiveCombo = ComboBox->GetCachedWidget())
		{
			StaticCastSharedPtr<SCompoundWidget>(LiveCombo)->SetForegroundColor(ComboTextColor);
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
		// W2 (2026-07-21): themed-combo-entry generator REMOVED — killed the HandleGenerateThemedComboEntry
		// NewObject<UTextBlock>(this) leak the owner flagged. Combos are off the walk entirely (the
		// crash-fix skip at the top of ApplyToWidget), so this whole branch is unreachable dead code;
		// combo theming is rebuilt churn-free in W1 (UComboBoxString subclass, styled before Slate build).
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
		bool bStyleChanged = false;

		// Q26: numeric/path edit boxes → Font_Inter Mono 14 (input_mono token). Font is
		// theme-independent, but the walk is the only C++ hook that touches every live widget, and
		// FEditableTextBoxStyle.Font is not settable from Python (protected). Convert only boxes not
		// already on Inter, so a deliberate Inter face/size is never re-stomped (idempotent on toggle).
		if (UFont* Inter = GetInterFont())
		{
			if (EditBox->WidgetStyle.TextStyle.Font.FontObject != Inter)
			{
				EditBox->WidgetStyle.TextStyle.Font = FSlateFontInfo(Inter, 11, FName(TEXT("Mono"))); // BW6 density: 14->11
				bStyleChanged = true;
			}
		}

		// Edit boxes carry their surface in FEditableTextBoxStyle, which the Border/Image value walk
		// never reaches — a dark-authored input (e.g. the flow-counter rename field) stayed dark in
		// light mode. Set the input palette roles EXPLICITLY: a generic SurfaceMap remap is not safe
		// here — the input values collide with other rows (0.0243 matched a rail pair), so each theme
		// toggle walked the fill to a different bucket (0.0243 -> 0.791 -> 0.913 drift).
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
			if (Brush->OutlineSettings.Width > 0.0f
				&& !Brush->OutlineSettings.Color.GetSpecifiedColor().Equals(InputBorderColor, 0.004f))
			{
				Brush->OutlineSettings.Color = FSlateColor(InputBorderColor);
				bStyleChanged = true;
			}
		}

		// Text colour: set the InputText palette role EXPLICITLY. Do NOT value-remap these — edit
		// boxes commonly leave ForegroundColor/TextStyle colour on an inheritance rule, and reading
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
				const FString TextAssetName = AssetData.AssetName.ToString();
				if (TextAssetName == TEXT("SWS_FlowRemoveButtonTextStyle"))
				{
					// Danger red — theme-independent, leave the authored colour alone.
				}
				else if (TextAssetName == TEXT("SWS_FlowCounterTextStyle"))
				{
					// In-world counter card is a light surface in BOTH themes (material-brush card),
					// so its text is pinned black. A value-remap here ping-ponged it white via the
					// playbar {white<->black} row and the in-world labels vanished.
					const FSlateColor PinnedBlack(FLinearColor::Black);
					if (TextStyle->ColorAndOpacity != PinnedBlack)
					{
						TextStyle->ColorAndOpacity = PinnedBlack;
						bChanged = true;
					}
				}
				else if (TextAssetName.Contains(TEXT("Button")))
				{
					// Button-label assets: EXPLICIT per-theme colour (idempotent, direction-safe).
					// The generic TextMap remap is order-dependent: a pass run against mixed state
					// (extra ReapplyTheme calls, editor+PIE both mutating the shared asset) walked
					// black -> white through the playbar row and every ButtonWithText label except
					// the red Remove went invisible.
					const FSlateColor LabelColor(PaletteColor(EMobiusPaletteRole::ButtonText, bLight));
					if (TextStyle->ColorAndOpacity != LabelColor)
					{
						TextStyle->ColorAndOpacity = LabelColor;
						bChanged = true;
					}
				}
				else
				{
					bChanged |= RemapSlate(TextStyle->ColorAndOpacity, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				}
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
