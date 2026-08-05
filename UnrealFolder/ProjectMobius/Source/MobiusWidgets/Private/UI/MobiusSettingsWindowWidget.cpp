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
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
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
	/**
	 * Card width. The brief said 420 and the .uasset pins MaxDesiredWidth to 600, but 600 clipped "Medium"
	 * to "Mediu:" and could not fit "Cinematic" once the fifth segment stopped being a link. Measured
	 * against the widest row, which is the five-segment bar, not the two logging columns.
	 */
	constexpr float GSettingsCardWidth = 700.0f;

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

	// Before Super, like the binds above: Super sweeps the tree and then calls ApplyMobiusTheme, so any
	// widget this moves or creates has to be in place first or it paints unthemed until a theme toggle.
	RestructureSettingsLayout();
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

	// The link owns its own colours (bFollowThemePalette is off), so it needs re-landing per theme.
	StyleCustomDisplayLink();

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

void UMobiusSettingsWindowWidget::NestCheckBoxLabel(UCheckBox* CheckBox)
{
	if (!CheckBox || CheckBox->GetContent() != nullptr)
	{
		return; // no checkbox, or its label was already nested on a previous construct
	}

	UPanelWidget* Row = CheckBox->GetParent();
	if (!Row)
	{
		return;
	}

	// The label is the checkbox's TextBlock sibling. Take the FIRST one only: a row is authored as
	// [checkbox][label], and grabbing more would swallow a helper caption that belongs to the column.
	UTextBlock* Label = nullptr;
	for (UWidget* Sibling : Row->GetAllChildren())
	{
		if (Sibling == CheckBox)
		{
			continue;
		}
		if (UTextBlock* AsText = Cast<UTextBlock>(Sibling))
		{
			Label = AsText;
			break;
		}
	}
	if (!Label)
	{
		return;
	}

	Label->RemoveFromParent();
	CheckBox->SetContent(Label);
}

void UMobiusSettingsWindowWidget::RestructureSettingsLayout()
{
	// ---- 1. the four logging toggles: label becomes part of the checkbox ----
	NestCheckBoxLabel(SessionLoggingCheckBox);
	NestCheckBoxLabel(SessionLogWindowCheckBox);
	NestCheckBoxLabel(StartupLoggingCheckBox);
	NestCheckBoxLabel(StartupLogWindowCheckBox);

	// ---- 2. lift the width cap that clipped the segment labels ----
	// Walk up from the quality control to the first SizeBox: that is the one sizing the card, and the
	// window is Autosized so its width follows. MaxDesiredWidth is the cap that mattered (600); Min moves
	// with it so the card does not shrink below the new width when content is narrow.
	// EVERY SizeBox on the way up, not just the first: the asset has two (one wrapping the card, one
	// wrapping the body) and only one of them carried the MaxDesiredWidth 600 that did the clipping.
	// Widening all of them is safe because each is an ANCESTOR of the segment bar, so each already has to
	// be at least as wide as it.
	if (GlobalQualityWidget)
	{
		UWidget* Ancestor = GlobalQualityWidget;
		for (int32 Depth = 0; Depth < 10 && Ancestor; ++Depth)
		{
			Ancestor = Ancestor->GetParent();
			if (USizeBox* CardSizer = Cast<USizeBox>(Ancestor))
			{
				CardSizer->SetMinDesiredWidth(GSettingsCardWidth);
				CardSizer->SetMaxDesiredWidth(GSettingsCardWidth);
				// WidthOverride wins over both when set, and the asset pins one of these to 420.
				CardSizer->SetWidthOverride(GSettingsCardWidth);
			}
		}
	}

	// ---- 3. move the Custom Display link under the Global Quality bar and make it look clickable ----
	if (!OpenCustomSettingsButton || !GlobalQualityWidget)
	{
		return;
	}
	UPanelWidget* QualityParent = GlobalQualityWidget->GetParent();
	if (!QualityParent)
	{
		return;
	}

	// Only move it if it is not already there — NativeConstruct can run more than once per widget.
	if (OpenCustomSettingsButton->GetParent() != QualityParent)
	{
		const int32 QualityIndex = QualityParent->GetChildIndex(GlobalQualityWidget);
		OpenCustomSettingsButton->RemoveFromParent();
		QualityParent->AddChild(OpenCustomSettingsButton);
		if (QualityIndex != INDEX_NONE)
		{
			// Directly BELOW the bar, hence +1. AddChild appended it, so shift it back up into place.
			QualityParent->ShiftChild(QualityIndex + 1, OpenCustomSettingsButton);
		}
	}

	// The button owns its own colours from here on: without this, UBaseButton re-stamps flat
	// ButtonBg/ButtonText on construct and on every theme change, which is what made it read as a caption.
	OpenCustomSettingsButton->bFollowThemePalette = false;
	StyleCustomDisplayLink();
	// Deliberately NOT rewriting the label: the ellipsis already in the .uasset is correct, and a narrow
	// non-ASCII literal here would ship as tofu (the source-encoding trap).
}

void UMobiusSettingsWindowWidget::StyleCustomDisplayLink()
{
	if (!OpenCustomSettingsButton)
	{
		return;
	}

	// A link, not a caption: Accent label with a hairline Accent outline so it has a visible hit area.
	// Fill stays the card colour so it does not read as a solid button competing with the segment bar.
	const FLinearColor Accent = GetThemeColor(EMobiusPaletteRole::Accent);
	const FLinearColor Fill = GetThemeColor(EMobiusPaletteRole::RibbonBg);
	const FLinearColor Hover = GetThemeColor(EMobiusPaletteRole::ButtonHoverBg);

	auto MakeBrush = [](const FLinearColor& InFill, const FLinearColor& Outline)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.SetResourceObject(nullptr);
		Brush.TintColor = FSlateColor(InFill);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(3.0, 3.0, 3.0, 3.0);
		Brush.OutlineSettings.Width = 1.0f;
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		return Brush;
	};

	FButtonStyle Style = OpenCustomSettingsButton->GetStyle();
	Style.SetNormal(MakeBrush(Fill, Accent));
	Style.SetHovered(MakeBrush(Hover, Accent));
	Style.SetPressed(MakeBrush(Hover, Accent));
	Style.SetDisabled(MakeBrush(Fill, Accent));
	const FSlateColor AccentText(Accent);
	Style.NormalForeground = AccentText;
	Style.HoveredForeground = AccentText;
	Style.PressedForeground = AccentText;
	Style.DisabledForeground = AccentText;
	// Equal totals: a smaller pressed padding shrinks the hit rect mid-press and Slate discards the click.
	Style.NormalPadding = FMargin(10.0f, 4.0f);
	Style.PressedPadding = FMargin(10.0f, 5.0f, 10.0f, 3.0f);
	OpenCustomSettingsButton->SetStyle(Style);

	// SetStyle bypasses SynchronizeProperties, so re-land the label colour on the STextBlock itself —
	// the style foregrounds above only reach text that resolves through UseForeground.
	OpenCustomSettingsButton->ApplyThemedLabelColor(Accent);
}

void UMobiusSettingsWindowWidget::HandleOpenCustomSettingsClicked()
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
