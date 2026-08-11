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
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Components/CheckBox.h"
#include "SimData/SimDiskCache.h"
#include "UserConfig/UserProjectSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
// B-Risk load-time toggles, merged in 2026-08-11.
#include "BRisk/BRiskDataSubsystem.h"

namespace
{
	/** The persisted user settings object, or null when there is no engine (commandlets, some test hosts).
	 *  Suffixed for unity-build safety, as with the constants below. */
	UUserProjectSettings* GetMobiusUserSettingsForSimSetup()
	{
		return GEngine != nullptr ? Cast<UUserProjectSettings>(GEngine->GetGameUserSettings()) : nullptr;
	}

	/** UI guard rails for the typed playback-speed multiplier, where 1.0 == realtime (the value
	 *  UTimeDilationSubSystem itself defaults to). So this range is 1/10th speed up to 100x.
	 *
	 *  These are not a physics limit — IProjectMobiusInterface separately refuses anything <= 0 — but a
	 *  typed 0 (which is what FCString::Atof returns for any non-numeric text) or a pasted 1e9 should never
	 *  reach the simulation at all. Prefixed names because internal-linkage symbols still collide across a
	 *  unity-build blob. */
	constexpr float GMinTimeDilationScale = 0.1f;
	constexpr float GMaxTimeDilationScale = 100.0f;
}

void USimulationSettingsWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

}

void USimulationSettingsWidget::NativeConstruct()
{
	// Call the parent implementation
	Super::NativeConstruct();

	// Setup the widget components
	SetupWidgetComponents();
}

void USimulationSettingsWidget::SetupWidgetComponents()
{
	// Null-check rather than IsValidLowLevel(). These are BindWidget properties, so each is NULL whenever the
	// host asset has no widget of that name — and `Ptr->IsValidLowLevel()` calls a method THROUGH the pointer,
	// dereferencing it first. The old form crashed in exactly the case it was written to guard against.
	if (CurrentTimeDilationScaleText != nullptr)
	{
		CurrentTimeDilationScaleText->SetVisibility(ESlateVisibility::Visible);

		// Update the current time dilation scale text
		UpdateCurrentTimeDilationScaleText();
	}

	// Setup the time dilation editable text
	if (TimeDilationScaleEditableTextBox != nullptr)
	{
		TimeDilationScaleEditableTextBox->SetVisibility(ESlateVisibility::Visible);

		// Open on the value actually in effect, not whatever text the asset was authored with.
		SyncTimeDilationTextBoxToCurrent();

		if (!IsDesignTime())
		{
			TimeDilationScaleEditableTextBox->OnTextCommitted.AddUniqueDynamic(
				this, &USimulationSettingsWidget::HandleTimeDilationTextCommitted);
		}
	}

	// Setup the update time dilation button
	if (UpdateTimeDilationButton != nullptr)
	{
		UpdateTimeDilationButton->SetVisibility(ESlateVisibility::Visible);

		// Bind the button click event if not in design time
		if (!IsDesignTime())
		{
			UpdateTimeDilationButton->OnClicked.AddUniqueDynamic(this, &USimulationSettingsWidget::UpdateTimeDilation);
		}
	}

	// Playback-speed presets (S11). Optional binds, so a host exposing only some of them stays valid; each is
	// wired only when present.
	//
	// AddUniqueDynamic throughout, including on the two bindings above that previously used AddDynamic:
	// NativeConstruct runs again every time the widget is re-added to the viewport, and a plain AddDynamic
	// would stack a second identical binding each time, making one click apply the change twice.
	if (!IsDesignTime())
	{
		if (TimeDilationPreset1xButton != nullptr)
		{
			TimeDilationPreset1xButton->OnClicked.AddUniqueDynamic(this, &USimulationSettingsWidget::HandlePreset1xClicked);
		}
		if (TimeDilationPreset2xButton != nullptr)
		{
			TimeDilationPreset2xButton->OnClicked.AddUniqueDynamic(this, &USimulationSettingsWidget::HandlePreset2xClicked);
		}
		if (TimeDilationPreset5xButton != nullptr)
		{
			TimeDilationPreset5xButton->OnClicked.AddUniqueDynamic(this, &USimulationSettingsWidget::HandlePreset5xClicked);
		}
		if (TimeDilationPreset10xButton != nullptr)
		{
			TimeDilationPreset10xButton->OnClicked.AddUniqueDynamic(this, &USimulationSettingsWidget::HandlePreset10xClicked);
		}
	}

	// Sim-cache controls (S14). Shown from the persisted settings first, then wired — the order matters:
	// SetIsChecked does NOT broadcast OnCheckStateChanged, so syncing cannot re-enter the handler and
	// re-save, but doing it in the other order would still flash the authored state for one frame.
	SyncCacheCheckBoxesToSettings();
	RefreshCacheSizeText();

	if (!IsDesignTime())
	{
		if (CacheOnImportCheckBox != nullptr)
		{
			CacheOnImportCheckBox->OnCheckStateChanged.AddUniqueDynamic(
				this, &USimulationSettingsWidget::HandleCacheOnImportChanged);
		}
		if (ReuseCacheOnReopenCheckBox != nullptr)
		{
			ReuseCacheOnReopenCheckBox->OnCheckStateChanged.AddUniqueDynamic(
				this, &USimulationSettingsWidget::HandleReuseCacheOnReopenChanged);
		}
		if (ClearCacheButton != nullptr)
		{
			ClearCacheButton->OnClicked.AddUniqueDynamic(this, &USimulationSettingsWidget::HandleClearCacheClicked);
		}
	}

	// Seed the preset highlight from the speed already in effect, so the panel opens showing which preset is
	// live rather than lighting up only after the first Apply.
	RefreshPresetHighlight();

	// B-Risk load-time toggles (merged in 2026-08-11). Seed each from the subsystem so the panel opens on the
	// real flag state rather than whatever the asset was authored with, then wire the change events.
	//
	// AddUniqueDynamic, not AddDynamic as the old class used: NativeConstruct runs again on every re-add to
	// the viewport, so plain AddDynamic stacked a second identical binding each time and one click applied
	// the change twice. Same defect already fixed on the time-dilation binds above.
	if (!IsDesignTime())
	{
		UBRiskDataSubsystem* BRiskSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UBRiskDataSubsystem>() : nullptr;

		if (UseBRiskTimingCheckBox != nullptr)
		{
			UseBRiskTimingCheckBox->OnCheckStateChanged.AddUniqueDynamic(
				this, &USimulationSettingsWidget::OnUseBRiskTimingChanged);
			if (BRiskSubsystem)
			{
				UseBRiskTimingCheckBox->SetIsChecked(BRiskSubsystem->GetConfigureSharedPlaybackOnLoad());
			}
		}
		if (LoadRoomGeometryCheckBox != nullptr)
		{
			LoadRoomGeometryCheckBox->OnCheckStateChanged.AddUniqueDynamic(
				this, &USimulationSettingsWidget::OnLoadRoomGeometryChanged);
			if (BRiskSubsystem)
			{
				LoadRoomGeometryCheckBox->SetIsChecked(BRiskSubsystem->GetAutoGenerateRoomGeometryOnLoad());
			}
		}
		if (ShowClosedOpeningsCheckBox != nullptr)
		{
			ShowClosedOpeningsCheckBox->OnCheckStateChanged.AddUniqueDynamic(
				this, &USimulationSettingsWidget::OnShowClosedOpeningsChanged);
			if (BRiskSubsystem)
			{
				ShowClosedOpeningsCheckBox->SetIsChecked(BRiskSubsystem->GetShowClosedOpeningPanels());
			}
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

void USimulationSettingsWidget::OnShowClosedOpeningsChanged(bool bIsChecked)
{
	if (UWorld* World = GetWorld())
	{
		if (UBRiskDataSubsystem* BRiskSubsystem = World->GetSubsystem<UBRiskDataSubsystem>())
		{
			// Live and cheap: only flips visibility on panels that already exist, so unlike the room
			// geometry toggle there is nothing to rebuild and no confirmation to ask for.
			BRiskSubsystem->SetShowClosedOpeningPanels(bIsChecked);
		}
	}
}

void USimulationSettingsWidget::UpdateCurrentTimeDilationScaleText()
{
	// Guarded here as well as at the call site: this is also reached from UpdateTimeDilation and the presets,
	// where the readout widget may be absent even though the numeric box is present.
	if (CurrentTimeDilationScaleText == nullptr)
	{
		return;
	}

	// Get the current time dilation scale
	UWorld* World = GetWorld();
	if (World)
	{
		float CurrentTimeDilationScale = IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(World);
		CurrentTimeDilationScaleText->SetText(FText::FromString(FString::SanitizeFloat(CurrentTimeDilationScale)));
	}
}

void USimulationSettingsWidget::UpdateTimeDilation()
{
	if (TimeDilationScaleEditableTextBox == nullptr)
	{
		return;
	}

	const FString TypedText = TimeDilationScaleEditableTextBox->GetText().ToString().TrimStartAndEnd();

	// Validate before converting, instead of letting FCString::Atof answer for us: Atof("abc") is 0.0, and a
	// factor of 0 is a frozen simulation. IProjectMobiusInterface does refuse values <= 0, so the old path
	// ended in an error toast while the box kept the offending text — no clue that the value never applied.
	if (TypedText.IsEmpty() || !TypedText.IsNumeric())
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Simulation Setup"),
				FText::FromString("Playback speed must be a number"),
				FText::Format(FText::FromString("'{0}' is not a valid playback speed, so the speed was left unchanged."),
					FText::FromString(TypedText)),
				FText::FromString("SimulationSetupWidget"),
				EMobiusErrorSeverity::Warning,
				true);
		}

		// Revert, so the box can never sit there displaying a value that is not in effect.
		SyncTimeDilationTextBoxToCurrent();
		return;
	}

	const float TypedScale = FCString::Atof(*TypedText);
	const float ClampedScale = FMath::Clamp(TypedScale, GMinTimeDilationScale, GMaxTimeDilationScale);

	// Update the time dilation scale
	UpdateTimeDilationScale(ClampedScale);

	// Write back what was actually applied — which differs from what was typed whenever the clamp bit.
	SyncTimeDilationTextBoxToCurrent();

	// Update the current time dilation scale text
	UpdateCurrentTimeDilationScaleText();

	// APPLY is the only place the highlight moves, because it is the only place the live speed changes.
	RefreshPresetHighlight();
}

void USimulationSettingsWidget::UpdateTimeDilationScale(float TimeDilationScale)
{
	UWorld* World = GetWorld();

	if (World)
	{
		IProjectMobiusInterface::UpdateMobiusGameInstanceSimulationTimeDilatationFactor(World, TimeDilationScale);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Simulation Setup Error"),
				FText::FromString("World context unavailable"),
				FText::FromString("Unable to update time dilation without a valid world."),
				FText::FromString("SimulationSetupWidget"));
		}
		UE_LOG(LogTemp, Error, TEXT("World is null"));
	}
}

void USimulationSettingsWidget::StageTimeDilationPreset(float PresetScale)
{
	// Clamped on the same range as typed input. The presets are all well inside it today; the clamp is here so
	// a future preset added in a widget asset cannot bypass the guard that typed input goes through.
	const float ClampedScale = FMath::Clamp(PresetScale, GMinTimeDilationScale, GMaxTimeDilationScale);

	// STAGE ONLY — deliberately does NOT call UpdateTimeDilationScale (owner, 2026-08-11). A preset is an
	// input like typing a number: it moves the pending value, and Apply is what reaches the simulation. So
	// neither the live speed nor the "Current speed" readout nor the highlight moves here; the numeric box
	// IS the pending value, which is why there is no separate member to hold it and nothing can drift out of
	// sync with what the user can see.
	if (TimeDilationScaleEditableTextBox != nullptr)
	{
		TimeDilationScaleEditableTextBox->SetText(FText::FromString(FString::SanitizeFloat(ClampedScale)));
	}

	// Repaint so the staged preset shows its PENDING fill immediately. This is the only feedback a click gives
	// before Apply, so it has to happen here and not wait for the commit.
	RefreshPresetHighlight();
}

float USimulationSettingsWidget::GetLiveTimeDilationScale()
{
	UWorld* World = GetWorld();
	return World ? IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(World) : 1.0f;
}

void USimulationSettingsWidget::RefreshPresetHighlight()
{
	if (IsDesignTime())
	{
		return;
	}

	const float LiveScale = GetLiveTimeDilationScale();

	// The PENDING value is whatever the numeric box currently holds — staging a preset writes it there, so the
	// box is the single source of truth for "what Apply would commit" and nothing can drift from what is on
	// screen. Falls back to the live value when the box is empty or mid-typed garbage.
	float PendingScale = LiveScale;
	if (TimeDilationScaleEditableTextBox != nullptr)
	{
		const FString PendingText = TimeDilationScaleEditableTextBox->GetText().ToString().TrimStartAndEnd();
		if (!PendingText.IsEmpty() && PendingText.IsNumeric())
		{
			PendingScale = FCString::Atof(*PendingText);
		}
	}

	// Float compare with a tolerance, not ==: the factor makes a round trip through the game instance and
	// through SanitizeFloat/Atof in the numeric box, so an applied "5" can come back as 4.9999998.
	auto Near = [](const float A, const float B) { return FMath::IsNearlyEqual(A, B, 0.001f); };

	// Three states, because Apply-gating created a gap the two-state version could not show (owner,
	// 2026-08-11): with the commit deferred, a clicked preset looked identical to an unclicked one right up
	// until Apply, so the user had no confirmation their click registered. ACTIVE keeps the Accent fill;
	// PENDING now takes ButtonHoverBg — the same surface hovering gives, so it reads as "armed" without
	// competing with the accent that means "running". A preset that is both simply stays Accent.
	auto StateOf = [&](const float PresetScale)
	{
		if (Near(LiveScale, PresetScale))    { return EPresetVisualState::Active; }
		if (Near(PendingScale, PresetScale)) { return EPresetVisualState::Pending; }
		return EPresetVisualState::Idle;
	};

	StylePresetButton(TimeDilationPreset1xButton,  StateOf(1.0f));
	StylePresetButton(TimeDilationPreset2xButton,  StateOf(2.0f));
	StylePresetButton(TimeDilationPreset5xButton,  StateOf(5.0f));
	StylePresetButton(TimeDilationPreset10xButton, StateOf(10.0f));
}

void USimulationSettingsWidget::StylePresetButton(UButton* Button, const EPresetVisualState State) const
{
	const bool bActive  = (State == EPresetVisualState::Active);
	const bool bPending = (State == EPresetVisualState::Pending);
	if (!Button)
	{
		return;
	}

	// Same treatment as UGlobalQualitySegmentWidget's active segment, which is the control the owner asked
	// this to match: Accent fill + white label when active, ordinary button colours otherwise.
	const FLinearColor Accent   = GetThemeColor(EMobiusPaletteRole::Accent);
	const FLinearColor HoverBg  = GetThemeColor(EMobiusPaletteRole::ButtonHoverBg);
	const FLinearColor RestFill = bActive ? Accent : (bPending ? HoverBg : GetThemeColor(EMobiusPaletteRole::ButtonBg));

	const FLinearColor Fill   = RestFill;
	const FLinearColor Hover  = bActive ? Accent : HoverBg;
	const FLinearColor Press  = bActive ? Accent : GetThemeColor(EMobiusPaletteRole::ButtonPressedBg);
	// Pending keeps the ordinary label colour: it is armed, not running, and a white label would read as
	// active against a fill that is only a hover grey.
	const FSlateColor  Label(bActive ? FLinearColor::White : GetThemeColor(EMobiusPaletteRole::ButtonText));

	FButtonStyle Style = Button->GetStyle();

	// Tint only, and never touch a brush carrying a ResourceObject — the same rule
	// UBaseButton::RefreshThemedButtonStyle follows, so an art-backed preset button keeps its art.
	auto Tint = [](FSlateBrush& Brush, const FLinearColor& Colour)
	{
		if (Brush.GetResourceObject() == nullptr)
		{
			Brush.TintColor = FSlateColor(Colour);
		}
	};
	Tint(Style.Normal,   Fill);
	Tint(Style.Hovered,  Hover);
	Tint(Style.Pressed,  Press);
	Tint(Style.Disabled, Fill);

	Style.NormalForeground   = Label;
	Style.HoveredForeground  = Label;
	Style.PressedForeground  = Label;
	Style.DisabledForeground = Label;
	Button->SetStyle(Style);

	// Belt and braces: a label authored with its OWN colour ignores the foregrounds above, and the asset's
	// preset buttons were not inspectable when this was written (editor closed). Recolouring any direct
	// TextBlock content covers that shape too, and is a no-op when the label inherits the foreground.
	for (UWidget* Child : Button->GetAllChildren())
	{
		if (UTextBlock* LabelText = Cast<UTextBlock>(Child))
		{
			LabelText->SetColorAndOpacity(Label);
		}
	}
}

void USimulationSettingsWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	// Accent and ButtonText are palette values, so the highlight has to be re-landed per theme.
	RefreshPresetHighlight();
}

void USimulationSettingsWidget::SyncTimeDilationTextBoxToCurrent()
{
	if (TimeDilationScaleEditableTextBox == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Read back from the game instance rather than echoing the requested value: this is what makes the box
	// honest when the interface rejected the write (it refuses <= 0) or when the clamp altered it.
	const float LiveScale = IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(World);
	TimeDilationScaleEditableTextBox->SetText(FText::FromString(FString::SanitizeFloat(LiveScale)));
}

// One forwarder per preset — see the header for why OnClicked cannot carry the multiplier itself.
void USimulationSettingsWidget::HandlePreset1xClicked()  { StageTimeDilationPreset(1.0f); }
void USimulationSettingsWidget::HandlePreset2xClicked()  { StageTimeDilationPreset(2.0f); }
void USimulationSettingsWidget::HandlePreset5xClicked()  { StageTimeDilationPreset(5.0f); }
void USimulationSettingsWidget::HandlePreset10xClicked() { StageTimeDilationPreset(10.0f); }

void USimulationSettingsWidget::HandleTimeDilationTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// Enter STAGES; Apply commits (owner, 2026-08-11 — reverses the earlier "Apply is redundant" ruling).
	// Enter used to call UpdateTimeDilation() and reach the simulation directly, which is exactly the
	// "any change instantly moves current speed too" behaviour being removed. It now normalises the typed
	// text through the same clamp the presets use and leaves it pending, so the box and the presets are one
	// input and Apply is the single commit point.
	//
	// Every other commit reason (focus moved away, cleared) still ABANDONS the edit and snaps the box back
	// to the live value — a half-typed number must never be left sitting there looking authoritative.
	if (CommitMethod == ETextCommit::OnEnter)
	{
		const FString TypedText = Text.ToString().TrimStartAndEnd();
		if (!TypedText.IsEmpty() && TypedText.IsNumeric())
		{
			StageTimeDilationPreset(FCString::Atof(*TypedText));
		}
		// Invalid text is left exactly as typed rather than reverted: Apply is what validates and reports,
		// and silently rewriting the box mid-typing would fight the user.
	}
	else
	{
		SyncTimeDilationTextBoxToCurrent();
	}
}

void USimulationSettingsWidget::HandleCacheOnImportChanged(bool bIsChecked)
{
	if (UUserProjectSettings* Settings = GetMobiusUserSettingsForSimSetup())
	{
		// The setter pushes the console variable and persists; nothing else to do here.
		Settings->SetCacheSimulationsOnImport(bIsChecked);
	}
}

void USimulationSettingsWidget::HandleReuseCacheOnReopenChanged(bool bIsChecked)
{
	if (UUserProjectSettings* Settings = GetMobiusUserSettingsForSimSetup())
	{
		Settings->SetReuseSimulationCacheOnReopen(bIsChecked);
	}
}

void USimulationSettingsWidget::HandleClearCacheClicked()
{
	// No confirmation prompt, deliberately: the .msc files are a derived cache, fully regenerable by
	// re-importing the source, and clearing costs only the next import's parse time. This is NOT the same
	// affordance as S13's "clear loaded data", which discards state the user cannot get back by waiting —
	// that one does need a confirm.
	const int32 Removed = MobiusSimCache::ClearCache();

	RefreshCacheSizeText();

	UE_LOG(LogTemp, Log, TEXT("[SimCache] cleared %d file(s) from the settings panel"), Removed);
}

void USimulationSettingsWidget::RefreshCacheSizeText()
{
	if (CacheSizeText == nullptr)
	{
		return;
	}

	int32 FileCount = 0;
	const int64 SizeBytes = MobiusSimCache::GetCacheSizeOnDisk(&FileCount);

	if (FileCount == 0)
	{
		CacheSizeText->SetText(FText::FromString(TEXT("Cache is empty")));
		return;
	}

	// AsMemory chooses the unit, so a 900 KB cache reads as "900 KB" rather than "0.0 GB".
	CacheSizeText->SetText(FText::Format(
		FText::FromString(TEXT("{0} in {1} file(s)")),
		FText::AsMemory(SizeBytes),
		FText::AsNumber(FileCount)));
}

void USimulationSettingsWidget::SyncCacheCheckBoxesToSettings()
{
	const UUserProjectSettings* Settings = GetMobiusUserSettingsForSimSetup();
	if (Settings == nullptr)
	{
		return;
	}

	// SetIsChecked does not broadcast OnCheckStateChanged, so this cannot re-enter the handlers and trigger
	// a redundant save. Relied upon rather than guarded with a re-entrancy flag.
	if (CacheOnImportCheckBox != nullptr)
	{
		CacheOnImportCheckBox->SetIsChecked(Settings->GetCacheSimulationsOnImport());
	}
	if (ReuseCacheOnReopenCheckBox != nullptr)
	{
		ReuseCacheOnReopenCheckBox->SetIsChecked(Settings->GetReuseSimulationCacheOnReopen());
	}
}
