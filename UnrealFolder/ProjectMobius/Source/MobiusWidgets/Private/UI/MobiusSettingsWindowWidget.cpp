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

#include "UI/MobiusSettingsWindowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/SlateBrush.h"
#include "UI/MobiusPanelWindowHost.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "UI/Components/ButtonWithText.h"
#include "UI/Components/Scalability/GlobalQualitySegmentWidget.h"
#include "UI/Components/Scalability/ScalabilityPanelWidget.h"
#include "UserConfig/UserProjectSettings.h"

namespace
{
	/** The logger's file name, single-sourced from MobiusCustomLoggerSubsystem::Initialize. */
	const FString GMobiusLogFileName = TEXT("MobiusCustomLog.txt");

	UUserProjectSettings* GetMobiusUserSettings()
	{
		return GEngine ? Cast<UUserProjectSettings>(GEngine->GetGameUserSettings()) : nullptr;
	}

	UMobiusCustomLoggerSubsystem* GetMobiusLogger()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	}
}

void UMobiusSettingsWindowWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// The brief's footer line says "MobiusLog.txt"; the logger actually appends to MobiusCustomLog.txt.
	// Set it here so the UI can never claim a file the logger does not write.
	if (LogFileNoteText)
	{
		LogFileNoteText->SetText(FText::Format(
			NSLOCTEXT("MobiusSettings", "LogFileNote", "Every session appends to the same file: {0}"),
			FText::FromString(GMobiusLogFileName)));
	}
}

void UMobiusSettingsWindowWidget::ResolvePanelReferences()
{
	if (!WidgetTree || (GlobalQualityWidget && CustomSettingsPanel))
	{
		return;
	}

	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		if (!GlobalQualityWidget)
		{
			if (UGlobalQualitySegmentWidget* Segment = Cast<UGlobalQualitySegmentWidget>(Widget))
			{
				GlobalQualityWidget = Segment;
			}
		}
		if (!CustomSettingsPanel)
		{
			if (UScalabilityPanelWidget* Panel = Cast<UScalabilityPanelWidget>(Widget))
			{
				CustomSettingsPanel = Panel;
			}
		}
	});
}

void UMobiusSettingsWindowWidget::NativeConstruct()
{
	// The two child panels are named for their assets, so BindWidget leaves them null — resolve by class
	// before anything below reads them.
	ResolvePanelReferences();

	// Bind before Super, matching the rest of the themed widgets: Super sweeps the tree and then calls
	// ApplyMobiusTheme, and the check states written by RefreshSettingStates should already be in place.
	if (SessionLoggingCheckBox)
	{
		SessionLoggingCheckBox->OnCheckStateChanged.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleSessionLoggingToggled);
	}
	if (SessionLogWindowCheckBox)
	{
		SessionLogWindowCheckBox->OnCheckStateChanged.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleSessionLogWindowToggled);
	}
	if (StartupLoggingCheckBox)
	{
		StartupLoggingCheckBox->OnCheckStateChanged.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleStartupLoggingToggled);
	}
	if (StartupLogWindowCheckBox)
	{
		StartupLogWindowCheckBox->OnCheckStateChanged.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleStartupLogWindowToggled);
	}
	if (OpenCustomSettingsButton)
	{
		OpenCustomSettingsButton->OnClicked.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleOpenCustomSettingsClicked);
	}
	if (GlobalQualityWidget)
	{
		GlobalQualityWidget->OnCustomQualityRequested.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleCustomQualityRequested);
	}
	if (CustomSettingsPanel)
	{
		CustomSettingsPanel->OnSettingsConfirmed.AddUniqueDynamic(
			this, &UMobiusSettingsWindowWidget::HandleCustomSettingsConfirmed);
		// Starts hidden — it is a separate window in the brief, opened from the Custom segment or the
		// footer row. Collapsed rather than Hidden so it takes no space in the sibling layout.
		CustomSettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	RefreshSettingStates();

	Super::NativeConstruct();
}

void UMobiusSettingsWindowWidget::NativeDestruct()
{
	// Close the child window FIRST: the Custom card was detached from this tree when it opened, so it does
	// not get torn down by this widget's destruction and would otherwise outlive its owner.
	if (CustomSettingsPanel)
	{
		CustomSettingsPanel->CloseWindow();
	}
	CloseWindow();

	Super::NativeDestruct();
}

void UMobiusSettingsWindowWidget::ShowAsWindow()
{
	if (PanelWindow.IsValid())
	{
		PanelWindow->BringToFront(true);
		RefreshSettingStates();
		return;
	}

	// Logging flags and the pedestrian toggle are read from live subsystem/config state, so refresh on every
	// open rather than trusting whatever the last session left in the checkboxes.
	RefreshSettingStates();

	const TWeakObjectPtr<UMobiusSettingsWindowWidget> WeakThis(this);
	PanelWindow = MobiusPanelWindow::Open(
		this,
		NSLOCTEXT("MobiusSettings", "SettingsTitle", "Settings"),
		[WeakThis]()
		{
			if (UMobiusSettingsWindowWidget* Settings = WeakThis.Get())
			{
				// Cleared here rather than in CloseWindow so the title-bar X and Alt+F4 — which never enter
				// CloseWindow — leave the handle in the same state.
				Settings->PanelWindow.Reset();
			}
		});
}

void UMobiusSettingsWindowWidget::CloseWindow()
{
	MobiusPanelWindow::Close(PanelWindow);
}

bool UMobiusSettingsWindowWidget::IsWindowOpen() const
{
	return PanelWindow.IsValid();
}

void UMobiusSettingsWindowWidget::ToggleWindow()
{
	if (IsWindowOpen())
	{
		CloseWindow();
	}
	else
	{
		ShowAsWindow();
	}
}

void UMobiusSettingsWindowWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	if (PanelBackgroundImage)
	{
		FSlateBrush Brush = PanelBackgroundImage->GetBrush();
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.SetResourceObject(nullptr);
		Brush.TintColor = FSlateColor(GetThemeColor(EMobiusPaletteRole::RibbonBg));
		Brush.OutlineSettings.Color = FSlateColor(GetThemeColor(EMobiusPaletteRole::WindowBorder));
		Brush.OutlineSettings.Width = 1.0f;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(6.0, 6.0, 6.0, 6.0);
		PanelBackgroundImage->SetBrush(Brush);
	}

	if (TitleBarImage)
	{
		// Flat, and only the TOP corners rounded, so the strip sits inside the card's 6px radius without
		// a lighter sliver showing through at the corners.
		FSlateBrush Brush = TitleBarImage->GetBrush();
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.SetResourceObject(nullptr);
		Brush.TintColor = FSlateColor(GetThemeColor(EMobiusPaletteRole::PanelHeaderBg));
		Brush.OutlineSettings.Color = FSlateColor(GetThemeColor(EMobiusPaletteRole::PanelHeaderBorder));
		Brush.OutlineSettings.Width = 1.0f;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(6.0, 6.0, 0.0, 0.0);
		TitleBarImage->SetBrush(Brush);
	}

	if (PanelTitleText)
	{
		PanelTitleText->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::PanelHeaderText)));
	}

	// Group labels: SublabelText per owner ruling. The brief's blue-grey #3d6b8e has no palette role and
	// none was added — do not "restore" a literal here.
	//
	// FONT is NOT set here, deliberately (owner decision, 2026-08-04): fonts for these panels are authored
	// into the .uasset like the rest of the app's text, and StyleTextBlockForTheme (UIThemeSubsystem.cpp:1299)
	// re-tints per theme. Do not reintroduce ramp SetFont calls here without changing that decision.
	const FSlateColor GroupColour(GetThemeColor(EMobiusPaletteRole::SublabelText));
	for (UTextBlock* Label : {
		GroupLabel_GlobalQuality.Get(), GroupLabel_UITheme.Get(),
		GroupLabel_Pedestrian.Get(), GroupLabel_Logging.Get()})
	{
		if (Label)
		{
			Label->SetColorAndOpacity(GroupColour);
		}
	}

	const FSlateColor MicroColour(GetThemeColor(EMobiusPaletteRole::MicroText));
	for (UTextBlock* Head : {ColumnHead_ThisSession.Get(), ColumnHead_AtStartup.Get()})
	{
		if (Head)
		{
			Head->SetColorAndOpacity(MicroColour);
		}
	}

	const FSlateColor HintColour(GetThemeColor(EMobiusPaletteRole::HintText));
	for (UTextBlock* Hint : {PedestrianHelperText.Get(), LogFileNoteText.Get()})
	{
		if (Hint)
		{
			Hint->SetColorAndOpacity(HintColour);
		}
	}
}

void UMobiusSettingsWindowWidget::RefreshSettingStates()
{
	const UUserProjectSettings* UserSettings = GetMobiusUserSettings();
	const UMobiusCustomLoggerSubsystem* Logger = GetMobiusLogger();

	// SetIsChecked fires OnCheckStateChanged, which would write straight back into the settings we are
	// only reading here.
	bSuppressLoggingCallbacks = true;

	if (SessionLoggingCheckBox && Logger)
	{
		SessionLoggingCheckBox->SetIsChecked(Logger->IsLoggingEnabled());
	}
	if (SessionLogWindowCheckBox && UserSettings)
	{
		// Live window state, not a stored flag — the log window can also be closed from its own titlebar.
		SessionLogWindowCheckBox->SetIsChecked(UserSettings->IsMobiusLogWindowVisible());
	}
	if (StartupLoggingCheckBox && UserSettings)
	{
		StartupLoggingCheckBox->SetIsChecked(UserSettings->GetEnableMobiusLoggerAtStartup());
	}
	if (StartupLogWindowCheckBox && UserSettings)
	{
		StartupLogWindowCheckBox->SetIsChecked(UserSettings->GetDisplayMobiusLogWindowAtStartup());
	}

	bSuppressLoggingCallbacks = false;

	if (GlobalQualityWidget)
	{
		GlobalQualityWidget->RefreshActiveSegment();
	}
}

void UMobiusSettingsWindowWidget::SetCustomPanelVisible(const bool bVisible)
{
	if (!CustomSettingsPanel)
	{
		return;
	}

	// Phase 2: the Custom card is its own window, not a nested child, so "visible" means "has a window".
	// ShowAsWindow re-syncs staged state itself and detaches the card from this tree on first open, which is
	// also what fixes its dead buttons — a RetainerBox renders its child to a render target and the cards'
	// controls were not receiving clicks through it.
	if (bVisible)
	{
		CustomSettingsPanel->ShowAsWindow();
	}
	else
	{
		CustomSettingsPanel->CloseWindow();
	}
}

void UMobiusSettingsWindowWidget::HandleSessionLoggingToggled(const bool bIsChecked)
{
	if (bSuppressLoggingCallbacks)
	{
		return;
	}

	// Straight to the subsystem. UUserProjectSettings::EnableMobiusLogger would ALSO set the startup flag,
	// which is the other column's setting.
	if (UMobiusCustomLoggerSubsystem* Logger = GetMobiusLogger())
	{
		Logger->SetLoggingEnabled(bIsChecked);
	}
}

void UMobiusSettingsWindowWidget::HandleSessionLogWindowToggled(const bool bIsChecked)
{
	if (bSuppressLoggingCallbacks)
	{
		return;
	}

	if (UUserProjectSettings* UserSettings = GetMobiusUserSettings())
	{
		UserSettings->ShowMobiusLogWindow(bIsChecked);
	}
}

void UMobiusSettingsWindowWidget::HandleStartupLoggingToggled(const bool bIsChecked)
{
	if (bSuppressLoggingCallbacks)
	{
		return;
	}

	if (UUserProjectSettings* UserSettings = GetMobiusUserSettings())
	{
		UserSettings->SetEnableMobiusLoggerAtStartup(bIsChecked);
		// Config property — persist now rather than at shutdown, so a crash cannot lose the preference.
		UserSettings->SaveMobiusSettings();
	}
}

void UMobiusSettingsWindowWidget::HandleStartupLogWindowToggled(const bool bIsChecked)
{
	if (bSuppressLoggingCallbacks)
	{
		return;
	}

	if (UUserProjectSettings* UserSettings = GetMobiusUserSettings())
	{
		UserSettings->SetDisplayMobiusLogWindowAtStartup(bIsChecked);
		UserSettings->SaveMobiusSettings();
	}
}

void UMobiusSettingsWindowWidget::HandleOpenCustomSettingsClicked()
{
	SetCustomPanelVisible(true);
}

void UMobiusSettingsWindowWidget::HandleCustomQualityRequested()
{
	SetCustomPanelVisible(true);
}

void UMobiusSettingsWindowWidget::HandleCustomSettingsConfirmed()
{
	// A confirmed batch can have changed any of the nine per-feature levels, and the Global Quality row
	// shows the LOWEST of them (owner ruling), so it has to re-derive.
	if (GlobalQualityWidget)
	{
		GlobalQualityWidget->RefreshActiveSegment();
	}
}
