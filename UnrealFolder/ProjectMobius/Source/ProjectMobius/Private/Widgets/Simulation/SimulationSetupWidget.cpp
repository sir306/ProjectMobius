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

#include "Widgets/Simulation/SimulationSetupWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Components/CheckBox.h"
#include "SimData/SimDiskCache.h"
#include "UserConfig/UserProjectSettings.h"
#include "Engine/Engine.h"

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

void USimulationSetupWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

}

void USimulationSetupWidget::NativeConstruct()
{
	// Call the parent implementation
	Super::NativeConstruct();

	// Setup the widget components
	SetupWidgetComponents();
}

void USimulationSetupWidget::SetupWidgetComponents()
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
				this, &USimulationSetupWidget::HandleTimeDilationTextCommitted);
		}
	}

	// Setup the update time dilation button
	if (UpdateTimeDilationButton != nullptr)
	{
		UpdateTimeDilationButton->SetVisibility(ESlateVisibility::Visible);

		// Bind the button click event if not in design time
		if (!IsDesignTime())
		{
			UpdateTimeDilationButton->OnClicked.AddUniqueDynamic(this, &USimulationSetupWidget::UpdateTimeDilation);
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
			TimeDilationPreset1xButton->OnClicked.AddUniqueDynamic(this, &USimulationSetupWidget::HandlePreset1xClicked);
		}
		if (TimeDilationPreset2xButton != nullptr)
		{
			TimeDilationPreset2xButton->OnClicked.AddUniqueDynamic(this, &USimulationSetupWidget::HandlePreset2xClicked);
		}
		if (TimeDilationPreset5xButton != nullptr)
		{
			TimeDilationPreset5xButton->OnClicked.AddUniqueDynamic(this, &USimulationSetupWidget::HandlePreset5xClicked);
		}
		if (TimeDilationPreset10xButton != nullptr)
		{
			TimeDilationPreset10xButton->OnClicked.AddUniqueDynamic(this, &USimulationSetupWidget::HandlePreset10xClicked);
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
				this, &USimulationSetupWidget::HandleCacheOnImportChanged);
		}
		if (ReuseCacheOnReopenCheckBox != nullptr)
		{
			ReuseCacheOnReopenCheckBox->OnCheckStateChanged.AddUniqueDynamic(
				this, &USimulationSetupWidget::HandleReuseCacheOnReopenChanged);
		}
		if (ClearCacheButton != nullptr)
		{
			ClearCacheButton->OnClicked.AddUniqueDynamic(this, &USimulationSetupWidget::HandleClearCacheClicked);
		}
	}
}

void USimulationSetupWidget::UpdateCurrentTimeDilationScaleText()
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

void USimulationSetupWidget::UpdateTimeDilation()
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
}

void USimulationSetupWidget::UpdateTimeDilationScale(float TimeDilationScale)
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

void USimulationSetupWidget::ApplyTimeDilationPreset(float PresetScale)
{
	// Clamped on the same range as typed input. The presets are all well inside it today; the clamp is here so
	// a future preset added in a widget asset cannot bypass the guard that typed input goes through.
	const float ClampedScale = FMath::Clamp(PresetScale, GMinTimeDilationScale, GMaxTimeDilationScale);

	UpdateTimeDilationScale(ClampedScale);

	// Move the numeric box onto the preset, so the two controls always report the same live speed.
	SyncTimeDilationTextBoxToCurrent();
	UpdateCurrentTimeDilationScaleText();
}

void USimulationSetupWidget::SyncTimeDilationTextBoxToCurrent()
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
void USimulationSetupWidget::HandlePreset1xClicked()  { ApplyTimeDilationPreset(1.0f); }
void USimulationSetupWidget::HandlePreset2xClicked()  { ApplyTimeDilationPreset(2.0f); }
void USimulationSetupWidget::HandlePreset5xClicked()  { ApplyTimeDilationPreset(5.0f); }
void USimulationSetupWidget::HandlePreset10xClicked() { ApplyTimeDilationPreset(10.0f); }

void USimulationSetupWidget::HandleTimeDilationTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// Enter commits. Every other commit reason (focus moved away, cleared) is treated as ABANDONING the
	// edit, and the box is snapped back to the value actually in effect — otherwise a half-typed number
	// would sit in the box looking like the live speed. With no Apply button, this box and the "current
	// speed" readout are the only feedback the user gets, so they must never disagree with the simulation.
	if (CommitMethod == ETextCommit::OnEnter)
	{
		UpdateTimeDilation();
	}
	else
	{
		SyncTimeDilationTextBoxToCurrent();
	}
}

void USimulationSetupWidget::HandleCacheOnImportChanged(bool bIsChecked)
{
	if (UUserProjectSettings* Settings = GetMobiusUserSettingsForSimSetup())
	{
		// The setter pushes the console variable and persists; nothing else to do here.
		Settings->SetCacheSimulationsOnImport(bIsChecked);
	}
}

void USimulationSetupWidget::HandleReuseCacheOnReopenChanged(bool bIsChecked)
{
	if (UUserProjectSettings* Settings = GetMobiusUserSettingsForSimSetup())
	{
		Settings->SetReuseSimulationCacheOnReopen(bIsChecked);
	}
}

void USimulationSetupWidget::HandleClearCacheClicked()
{
	// No confirmation prompt, deliberately: the .msc files are a derived cache, fully regenerable by
	// re-importing the source, and clearing costs only the next import's parse time. This is NOT the same
	// affordance as S13's "clear loaded data", which discards state the user cannot get back by waiting —
	// that one does need a confirm.
	const int32 Removed = MobiusSimCache::ClearCache();

	RefreshCacheSizeText();

	UE_LOG(LogTemp, Log, TEXT("[SimCache] cleared %d file(s) from the settings panel"), Removed);
}

void USimulationSetupWidget::RefreshCacheSizeText()
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

void USimulationSetupWidget::SyncCacheCheckBoxesToSettings()
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
