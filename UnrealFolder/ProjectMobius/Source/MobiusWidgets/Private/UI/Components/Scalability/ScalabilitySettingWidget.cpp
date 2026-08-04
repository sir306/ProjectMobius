// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Components/Scalability/ScalabilitySettingWidget.h"
#include "UI/Components/ButtonWithText.h"

void UScalabilitySettingWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UScalabilitySettingWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UScalabilitySettingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ConfigureButtonStyles();

}

void UScalabilitySettingWidget::InitializeScalabilityLevel()
{
	Super::InitializeScalabilityLevel();
}

void UScalabilitySettingWidget::UpdateScalabilityLevel()
{
	Super::UpdateScalabilityLevel();
}


void UScalabilitySettingWidget::ApplyButtonStyleForActiveSetting()
{
	// check style asset is valid
	if (ScalabilityButtonStyle == nullptr)
	{
		return;
	}
	// A10 (2026-08-04) — swap the style ASSET, not the style STRUCT. This used to build an FButtonStyle by
	// hand (Normal <-> Pressed swapped for the active tier) and push it with UButton::SetStyle. SetStyle
	// assigns WidgetStyle and hands it to Slate WITHOUT calling SynchronizeProperties, so
	// UBaseButton::ApplyMobiusButtonStyle never re-ran and these buttons silently opted out of two things
	// every other Mobius button gets. Measured in PIE before the change (see
	// _CurrentHandoff\THREAD_A10_SWSCleanup_2026-08-04.md):
	//
	//   1. COLOUR came from the asset, not the palette — 0.964 light / 0.052861 dark instead of ButtonBg
	//      0.93869 / 0.06848. Their five siblings in WBP_ScalabilitySettingGlobal (whose BP graph swaps the
	//      ASSET and so does route through ApplyMobiusButtonStyle) painted ButtonBg, so one style asset
	//      rendered two different colours in the same popup. It also meant ApplySharedStyles was the ONLY
	//      thing keeping these 50 buttons themed at all, which blocked retiring it (A10's endgame).
	//   2. PADDING was the asset's authored NormalPadding (8,4,8,4) vs PressedPadding (6,2,6,2) — a 4x4px
	//      shrink on press that StabilisePressedPadding exists to repair. Latent rather than live (every
	//      tier button is in a GridSlot with H_ALIGN_FILL, so the column holds its width from the four
	//      unpressed siblings and the hit rect does not move) but one reparent away from eating clicks.
	//
	// Swapping the asset instead makes this path identical to WBP_ScalabilitySettingGlobal's BP graph, which
	// was already correct, and both fixes fall out of ApplyMobiusButtonStyle rather than being re-implemented
	// here. The active tier now reads as the same accent-ring chip the global panel shows (owner-approved
	// 2026-08-04: the old flat fill was only 5/255 from its neighbours in dark theme, with no ring, so which
	// tier was selected was effectively unreadable).
	USlateWidgetStyleAsset* const InactiveStyle = ScalabilityButtonStyle;
	USlateWidgetStyleAsset* ActiveStyle = ActiveTierButtonStyle.LoadSynchronous();
	if (ActiveStyle == nullptr)
	{
		// Chip asset missing: degrade to the ordinary tier look rather than dropping the style entirely.
		ActiveStyle = InactiveStyle;
	}

	auto DesiredStyleFor = [InactiveStyle, ActiveStyle](const bool bIsActive)
	{
		return bIsActive ? ActiveStyle : InactiveStyle;
	};

	// Unconditional, and deliberately so: the old CheckButtonStyle early-out compared the live Normal brush
	// against the asset's brush, which made correct RE-THEMING depend on that compare happening to mismatch
	// after ApplySharedStyles retinted the asset (documented in the header). A pointer compare would not
	// mismatch on a theme flip, so the chip would never re-land. ApplyMobiusButtonStyle is idempotent —
	// asset snapshot, then padding repair, then the palette re-stamp, all derived from the asset plus the
	// palette — and this runs on selection change and on theme change, never per frame. So re-landing by
	// construction is both cheaper to reason about and strictly more correct than the compare it replaces.
	auto ApplyButtonStyle = [&DesiredStyleFor](UButtonWithText* Button, const bool bIsActive)
	{
		if (Button == nullptr)
		{
			return;
		}
		Button->SlateButtonStyle = DesiredStyleFor(bIsActive);
		Button->ApplyMobiusButtonStyle();
	};


	// Resolve WHICH button is the active tier, then style all five against it. Replaces six near-identical
	// five-line blocks whose only difference was which button got `true`; ESsl_Default resolves to nullptr,
	// which is the same "no tier active" case its block spelled out by hand.
	const UButtonWithText* ActiveButton = nullptr;
	switch (GetScalabilityLevel())
	{
	case ESsl_Low:       ActiveButton = LowSetting_Button.Get();  break;
	case ESsl_Medium:    ActiveButton = MedSetting_Button.Get();  break;
	case ESsl_High:      ActiveButton = HighSetting_Button.Get(); break;
	case ESsl_Epic:      ActiveButton = EpicSetting_Button.Get(); break;
	case ESsl_Cinematic: ActiveButton = CineSetting_Button.Get(); break;
	case ESsl_Default:   break; // no tier is active
	default:             break;
	}

	// A null ActiveButton compares equal to a null tier pointer, but ApplyButtonStyle no-ops on null, so a
	// missing BindWidget cannot make an absent button "active".
	ApplyButtonStyle(LowSetting_Button,  LowSetting_Button.Get()  == ActiveButton);
	ApplyButtonStyle(MedSetting_Button,  MedSetting_Button.Get()  == ActiveButton);
	ApplyButtonStyle(HighSetting_Button, HighSetting_Button.Get() == ActiveButton);
	ApplyButtonStyle(EpicSetting_Button, EpicSetting_Button.Get() == ActiveButton);
	ApplyButtonStyle(CineSetting_Button, CineSetting_Button.Get() == ActiveButton);
}

void UScalabilitySettingWidget::ApplyMobiusTheme_Implementation()
{
	// A6b: the walker used to re-land this chip; now the widget does it on the theme event. Deliberately
	// NOT calling Super — the base implementation is empty by design, and the standard-control pass runs
	// from NativeConstruct / HandleThemeChanged, not from here (see MobiusThemedUserWidget.cpp).
	ApplyButtonStyleForActiveSetting();
}

void UScalabilitySettingWidget::UpdateScalabilityAndButtonStyle(EScalabilitySettings NewSetting)
{
	// Update the Scalability Level
	SetScalabilityLevel(NewSetting);

	// Update the buttons style based on the new Scalability Level
	ApplyButtonStyleForActiveSetting();

	// Update the scalability level in the Performance Util Subsystem
	UpdateScalabilityLevel();
}

void UScalabilitySettingWidget::ConfigureButtonStyles()
{
	// A10 (2026-08-04) OBSERVATION, deliberately NOT fixed here: this guard looks inverted. It bails when
	// the style IS valid, so the body below only runs when ScalabilityButtonStyle is null — at which point
	// every `ButtonStyleDefault = ScalabilityButtonStyle` assignment writes null. In practice the WBPs do
	// set ScalabilityButtonStyle (ApplyButtonStyleForActiveSetting early-returns without it, and the tier
	// buttons demonstrably get styled), so this whole function is currently a no-op.
	//
	// Left alone on purpose: correcting the guard would start applying `bShouldSwitchNormalWithHovered =
	// false`, which changes hover behaviour on 55 buttons — a look change with no owner ruling behind it,
	// and out of scope for A10. Logged for its own pass.
	if (ScalabilityButtonStyle != nullptr)
	{
		return;
	}
	// Apply the ScalabilityButtonStyle to all buttons
	// TODO: Add text style configuration for scalability buttons
	if (LowSetting_Button)
	{
		LowSetting_Button->bShouldSwitchNormalWithHovered = false;
		LowSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		LowSetting_Button->ApplyMobiusButtonStyle();
	}
	if (MedSetting_Button)
	{
		MedSetting_Button->bShouldSwitchNormalWithHovered = false;
		MedSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		MedSetting_Button->ApplyMobiusButtonStyle();
	}
	if (HighSetting_Button)
	{
		HighSetting_Button->bShouldSwitchNormalWithHovered = false;
		HighSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		HighSetting_Button->ApplyMobiusButtonStyle();
	}
	if (EpicSetting_Button)
	{
		EpicSetting_Button->bShouldSwitchNormalWithHovered = false;
		EpicSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		EpicSetting_Button->ApplyMobiusButtonStyle();
	}
	if (CineSetting_Button)
	{
		CineSetting_Button->bShouldSwitchNormalWithHovered = false;
		CineSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		CineSetting_Button->ApplyMobiusButtonStyle();
	}
}
