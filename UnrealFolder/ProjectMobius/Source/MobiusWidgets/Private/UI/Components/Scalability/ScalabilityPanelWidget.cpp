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

#include "UI/Components/Scalability/ScalabilityPanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/PerformanceUtilSubsystem.h"
#include "UI/MobiusPanelWindowHost.h"
#include "UI/Components/ButtonWithText.h"
#include "UI/Components/Scalability/ScalabilityMatrixRowWidget.h"

namespace
{
	/**
	 * The nine per-feature categories the matrix drives, in the order the brief prints them.
	 *
	 * ESc_Resolution is excluded on purpose — UPerformanceUtilSubsystem::ApplyScalabilityLevel is an
	 * explicit no-op for it ("handled separately") and it has its own row above the matrix. ESc_Global is
	 * excluded because the brief removed the global row from this window: the preset lives in Settings, so
	 * the two windows cannot disagree.
	 */
	constexpr EScalabilityCategories GMatrixCategories[] = {
		ESc_GlobalIllumination,
		ESc_PostProcessing,
		ESc_Shadows,
		ESc_AntiAliasing,
		ESc_Reflections,
		ESc_Textures,
		ESc_Effects,
		ESc_Shading,
		ESc_ViewDistance
	};

	/** Smallest resolution the X/Y fields will stage; below this the window is unusable. */
	constexpr int32 GMinimumResolutionAxis = 320;
}

void UScalabilityPanelWidget::NativeConstruct()
{
	// Before Super, matching the pattern UThemeToggleWidget documents: Super themes the tree and then calls
	// ApplyMobiusTheme, so the rows must be subscribed and the controls populated first.
	CollectMatrixRows();

	if (ResetButton)
	{
		ResetButton->OnClicked.AddUniqueDynamic(this, &UScalabilityPanelWidget::HandleResetClicked);
	}
	if (ConfirmButton)
	{
		ConfirmButton->OnClicked.AddUniqueDynamic(this, &UScalabilityPanelWidget::HandleConfirmClicked);
	}
	if (ResolutionXTextBox)
	{
		ResolutionXTextBox->OnTextCommitted.AddUniqueDynamic(this, &UScalabilityPanelWidget::HandleResolutionTextCommitted);
	}
	if (ResolutionYTextBox)
	{
		ResolutionYTextBox->OnTextCommitted.AddUniqueDynamic(this, &UScalabilityPanelWidget::HandleResolutionTextCommitted);
	}
	if (ResolutionPresetComboBox)
	{
		ResolutionPresetComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UScalabilityPanelWidget::HandlePresetSelected);
	}

	PopulatePresetOptions();
	SyncFromAppliedSettings();

	Super::NativeConstruct();
}

void UScalabilityPanelWidget::NativeDestruct()
{
	// The window holds this widget's Slate; leaving it up past the widget's life would paint a dead tree.
	CloseWindow();

	Super::NativeDestruct();
}

void UScalabilityPanelWidget::ShowAsWindow()
{
	if (PanelWindow.IsValid())
	{
		PanelWindow->BringToFront(true);
		// Re-baseline anyway: quality can have been changed from the Settings card's Global Quality segments
		// while this window sat behind it.
		SyncFromAppliedSettings();
		return;
	}

	// Staged state must start from what is actually applied, every open — the widget is not destroyed
	// between openings, so stale staged values from a previous session would otherwise survive.
	SyncFromAppliedSettings();

	const TWeakObjectPtr<UScalabilityPanelWidget> WeakThis(this);
	PanelWindow = MobiusPanelWindow::Open(
		this,
		NSLOCTEXT("MobiusScalability", "CustomDisplayTitle", "Custom Display Settings"),
		[WeakThis]()
		{
			if (UScalabilityPanelWidget* Panel = WeakThis.Get())
			{
				// OWNER RULING 2: the window's X acts as Reset — it restores the last CONFIRMED values
				// rather than leaving staged-but-unapplied edits behind. Nothing was ever pushed to the
				// engine (Confirm is the only apply), so this is a state discard, not a re-apply.
				// A separate "Reset to defaults" button remains explicitly deferred.
				Panel->ResetStagedSettings();
				// Cleared here, not in CloseWindow: this runs on EVERY close route, including the
				// title-bar X and Alt+F4, which never enter CloseWindow at all.
				Panel->PanelWindow.Reset();
			}
		});
}

void UScalabilityPanelWidget::CloseWindow()
{
	MobiusPanelWindow::Close(PanelWindow);
}

bool UScalabilityPanelWidget::IsWindowOpen() const
{
	return PanelWindow.IsValid();
}

void UScalabilityPanelWidget::ApplyMobiusTheme_Implementation()
{
	// Replace the (playbar-material) background with a solid RibbonBg surface the walk couldn't retint.
	if (PanelBackgroundImage)
	{
		FSlateBrush Brush = PanelBackgroundImage->GetBrush();
		// Rounded box with a 1px WindowBorder outline so the panel has a visible themed edge (was a flat
		// Box tinted RibbonBg with no border).
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.SetResourceObject(nullptr);
		Brush.TintColor = FSlateColor(GetThemeColor(EMobiusPaletteRole::RibbonBg));
		Brush.OutlineSettings.Color = FSlateColor(GetThemeColor(EMobiusPaletteRole::WindowBorder));
		Brush.OutlineSettings.Width = 1.0f;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(6.0, 6.0, 6.0, 6.0);
		PanelBackgroundImage->SetBrush(Brush);
	}

	// Recolour the panel title to the current-theme header text colour. Font is authored in the .uasset
	// (owner decision, 2026-08-04) — the theme walk re-tints text but never sets FSlateFontInfo.
	if (PanelTitleText)
	{
		PanelTitleText->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::PanelHeaderText)));
	}
}

void UScalabilityPanelWidget::CollectMatrixRows()
{
	MatrixRows.Reset();

	if (!WidgetTree)
	{
		return;
	}

	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		if (UScalabilityMatrixRowWidget* Row = Cast<UScalabilityMatrixRowWidget>(Widget))
		{
			MatrixRows.Add(Row);
			Row->OnLevelStaged.AddUniqueDynamic(this, &UScalabilityPanelWidget::HandleRowLevelStaged);
		}
	});
}

void UScalabilityPanelWidget::SyncFromAppliedSettings()
{
	StagedLevels.Reset();
	ConfirmedLevels.Reset();

	// Same design-time / game-world guard the scalability widgets use: the UMG designer and thumbnail
	// renderers have no world subsystem collection, and touching one there crashes.
	UWorld* World = IsDesignTime() ? nullptr : GetWorld();
	UPerformanceUtilSubsystem* Performance = (World && World->IsGameWorld())
		? World->GetSubsystem<UPerformanceUtilSubsystem>()
		: nullptr;

	for (const EScalabilityCategories Category : GMatrixCategories)
	{
		const TEnumAsByte<EScalabilitySettings> Applied = Performance
			? TEnumAsByte<EScalabilitySettings>(Performance->GetScalabilityLevel(Category))
			: TEnumAsByte<EScalabilitySettings>(EScalabilitySettings::ESsl_Epic);
		StagedLevels.Add(Category, Applied);
		ConfirmedLevels.Add(Category, Applied);
	}

	StagedResolution = Performance ? Performance->GetCurrentScreenResolution() : FIntPoint::ZeroValue;
	ConfirmedResolution = StagedResolution;

	RefreshMatrixRows();
	RefreshResolutionControls();
}

void UScalabilityPanelWidget::HandleRowLevelStaged(const TEnumAsByte<EScalabilityCategories> Category,
	const TEnumAsByte<EScalabilitySettings> NewLevel)
{
	// Staged only. Nothing is applied until Confirm — that is the whole point of the footer buttons now
	// covering the window rather than just the resolution.
	StagedLevels.Add(Category, NewLevel);
}

void UScalabilityPanelWidget::HandleResetClicked()
{
	ResetStagedSettings();
}

void UScalabilityPanelWidget::HandleConfirmClicked()
{
	ConfirmStagedSettings();
}

void UScalabilityPanelWidget::ResetStagedSettings()
{
	StagedLevels = ConfirmedLevels;
	StagedResolution = ConfirmedResolution;

	RefreshMatrixRows();
	RefreshResolutionControls();
}

void UScalabilityPanelWidget::ConfirmStagedSettings()
{
	UWorld* World = IsDesignTime() ? nullptr : GetWorld();
	UPerformanceUtilSubsystem* Performance = (World && World->IsGameWorld())
		? World->GetSubsystem<UPerformanceUtilSubsystem>()
		: nullptr;
	if (!Performance)
	{
		// A19: no on-screen error for a missing subsystem — the panel simply cannot apply, and the staged
		// values stay staged so a later Confirm still works.
		UE_LOG(LogTemp, Warning, TEXT("UScalabilityPanelWidget::ConfirmStagedSettings: no PerformanceUtilSubsystem, nothing applied."));
		return;
	}

	// Quality first, resolution last: a resolution change re-applies GameUserSettings and is the more
	// disruptive of the two, so it should land on already-correct quality values.
	for (const TPair<TEnumAsByte<EScalabilityCategories>, TEnumAsByte<EScalabilitySettings>>& Staged : StagedLevels)
	{
		const TEnumAsByte<EScalabilitySettings>* Confirmed = ConfirmedLevels.Find(Staged.Key);
		if (Confirmed && *Confirmed == Staged.Value)
		{
			continue;
		}
		Performance->ApplyScalabilityLevel(Staged.Value, Staged.Key);
	}

	if (StagedResolution != ConfirmedResolution
		&& StagedResolution.X >= GMinimumResolutionAxis
		&& StagedResolution.Y >= GMinimumResolutionAxis)
	{
		Performance->UpdateScreenResolutions(StagedResolution);
	}

	ConfirmedLevels = StagedLevels;
	ConfirmedResolution = StagedResolution;

	// The Settings window's Global Quality row derives its active segment from the applied per-feature
	// levels, so it has to be told the batch landed.
	OnSettingsConfirmed.Broadcast();
}

bool UScalabilityPanelWidget::HasPendingChanges() const
{
	if (StagedResolution != ConfirmedResolution)
	{
		return true;
	}

	for (const TPair<TEnumAsByte<EScalabilityCategories>, TEnumAsByte<EScalabilitySettings>>& Staged : StagedLevels)
	{
		const TEnumAsByte<EScalabilitySettings>* Confirmed = ConfirmedLevels.Find(Staged.Key);
		if (!Confirmed || *Confirmed != Staged.Value)
		{
			return true;
		}
	}

	return false;
}

void UScalabilityPanelWidget::HandleResolutionTextCommitted(const FText& Text, const ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnCleared)
	{
		return;
	}

	// Read BOTH boxes rather than trusting the committed one: the two fields are one value, and a partial
	// edit (X typed, Y untouched) must still produce a complete staged resolution.
	const int32 NewX = ResolutionXTextBox ? FCString::Atoi(*ResolutionXTextBox->GetText().ToString()) : 0;
	const int32 NewY = ResolutionYTextBox ? FCString::Atoi(*ResolutionYTextBox->GetText().ToString()) : 0;

	if (NewX < GMinimumResolutionAxis || NewY < GMinimumResolutionAxis)
	{
		// Reject rather than clamp: silently rewriting a half-typed number is worse than putting the last
		// good value back and letting the user try again.
		RefreshResolutionControls();
		return;
	}

	StagedResolution = FIntPoint(NewX, NewY);
	RefreshResolutionControls();
}

void UScalabilityPanelWidget::HandlePresetSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressPresetCallback || !ResolutionPresetComboBox)
	{
		return;
	}

	const int32 Index = ResolutionPresetComboBox->FindOptionIndex(SelectedItem);
	if (PresetResolutions.IsValidIndex(Index))
	{
		StagedResolution = PresetResolutions[Index];
		RefreshResolutionControls();
	}
}

void UScalabilityPanelWidget::PopulatePresetOptions()
{
	PresetResolutions.Reset();

	if (!ResolutionPresetComboBox)
	{
		return;
	}

	UWorld* World = IsDesignTime() ? nullptr : GetWorld();
	if (UPerformanceUtilSubsystem* Performance = (World && World->IsGameWorld())
		? World->GetSubsystem<UPerformanceUtilSubsystem>()
		: nullptr)
	{
		PresetResolutions = Performance->GetSystemScreenResolutions();
	}

	bSuppressPresetCallback = true;
	ResolutionPresetComboBox->ClearOptions();
	for (const FIntPoint& Resolution : PresetResolutions)
	{
		ResolutionPresetComboBox->AddOption(FormatResolution(Resolution));
	}
	bSuppressPresetCallback = false;
}

void UScalabilityPanelWidget::RefreshResolutionControls()
{
	if (ResolutionXTextBox)
	{
		ResolutionXTextBox->SetText(FText::AsNumber(StagedResolution.X, &FNumberFormattingOptions::DefaultNoGrouping()));
	}
	if (ResolutionYTextBox)
	{
		ResolutionYTextBox->SetText(FText::AsNumber(StagedResolution.Y, &FNumberFormattingOptions::DefaultNoGrouping()));
	}

	if (!ResolutionPresetComboBox)
	{
		return;
	}

	// Writing the selection re-enters OnSelectionChanged, which would re-stage the value we are displaying.
	bSuppressPresetCallback = true;
	const int32 Index = PresetResolutions.IndexOfByKey(StagedResolution);
	if (PresetResolutions.IsValidIndex(Index))
	{
		ResolutionPresetComboBox->SetSelectedIndex(Index);
	}
	else
	{
		// A typed resolution that is not in the supported list is legal — clear the selection rather than
		// leaving the combo claiming a preset the fields contradict.
		ResolutionPresetComboBox->ClearSelection();
	}
	bSuppressPresetCallback = false;
}

void UScalabilityPanelWidget::RefreshMatrixRows()
{
	for (const TObjectPtr<UScalabilityMatrixRowWidget>& Row : MatrixRows)
	{
		if (!Row)
		{
			continue;
		}
		if (const TEnumAsByte<EScalabilitySettings>* Level = StagedLevels.Find(Row->ScalabilityCategory))
		{
			Row->SetStagedLevel(*Level);
		}
	}
}

FString UScalabilityPanelWidget::FormatResolution(const FIntPoint& Resolution)
{
	return FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
}
