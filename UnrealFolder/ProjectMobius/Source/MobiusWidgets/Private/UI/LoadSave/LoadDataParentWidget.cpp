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

#include "UI/LoadSave/LoadDataParentWidget.h"
#include "Blueprint/WidgetTree.h" // Used to create the widget tree and creating/mapping components
#include "Components/CanvasPanel.h" // UCanvasPanel component
#include "Components/Button.h" // UButton component
#include "Components/TextBlock.h" // UTextBlock component
#include "UI/Theme/MobiusThemePalette.h" // EMobiusPaletteRole - S1 pulls InputText / LabelText
#include "Engine/Font.h" // UFont - S1 pins the path field to the composite Font_Inter so faces resolve
// headers for opening file dialogs
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
// headers for getting the game instance
#include "UI/Components/ButtonWithText.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Engine/GameInstance.h"
#include "GameInstances/ProjectMobiusGameInstance.h"

void ULoadDataParentWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Setup the text blocks and set their default values
	SetupTextBlocks();
	UpdateFileTextBlockTexts();

	// Bind the button click event
	if (SelectFileButton) // only bind if the button is valid
	{
		// Setup the button parameters


		
	}

	// DEBUG Text styling
	if (DataFileTextBlock->IsValidLowLevel())
	{

		DataFileTextBlock->SetVisibility(ESlateVisibility::Visible);
		DataFileTextBlock->SetText(FText::FromString("really long debugging text text"));
	}
}

void ULoadDataParentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind the button click event
	if (SelectFileButton) // only bind if the button is valid
	{
		// Setup the button parameters
		

		// Bind the button click event
		SelectFileButton->OnClicked.AddDynamic(this, &ULoadDataParentWidget::OnSelectFileButtonClicked);
	}

	// Setup the text blocks and set their default values
	SetupTextBlocks();

	// if in world we need to get the game instance and cast to our custom game instance and get the data file defaults
	if (!IsDesignTime())
	{
		// Get world
		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("World is nullptr"));
			return;
		}

		// Update the data file defaults from the custom game instance
		GetMobiusGameInstanceData();

		// Observe the game instance from here on. The pull above covers a path that was already set
		// before this widget existed (a launch-argument preload beats the HUD into existence); this
		// covers one set afterwards, by the preload subsystem, a console command, or anything else
		// that writes the game instance without going through this widget's Browse callback.
		BindGameInstanceFileDelegate();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("IsDesignTime"));
	}
	// This will either update with defaults or the data update from game instance
	UpdateFileTextBlockTexts();
}

void ULoadDataParentWidget::NativeDestruct()
{
	// The game instance outlives this widget, so a subscription left behind would fire into a
	// destructed UObject on the next file change.
	if (!IsDesignTime())
	{
		UnbindGameInstanceFileDelegate();
	}

	Super::NativeDestruct();
}

void ULoadDataParentWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

}

void ULoadDataParentWidget::RefreshFromGameInstance()
{
	// GetMobiusGameInstanceData is the subclass's "read my field out of the game instance" hook and
	// ends in UpdateWidgetFileProperties, so this is exactly the state the Browse callback would have
	// left behind - minus the write back to the game instance, which would be circular here.
	GetMobiusGameInstanceData();
	UpdateFileTextBlockTexts();
}

void ULoadDataParentWidget::OnSelectFileButtonClicked()
{
	// No matter what action is taken, the mouse needs to be captured again so that clicks are registered immediately
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
	// not the problem -> its the way dialog box close is handled, in windows it seems to be setting the capture
	// back to window and contained in it without allowing the application to capture the mouse correctly again??
}

void ULoadDataParentWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	// Value: the path sits on an InputBg surface (the row's UMobiusThemedBorder), so it takes InputText -
	// the same role "Mobius.Text.Field" is retinted to for every other value readout in the app.
	if (DataFileTextBlock)
	{
		DataFileTextBlock->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::InputText)));
	}

	// Label: standard UI copy, so LabelText. Same values as InputText in the palette today, but the two are
	// separate roles and the label is not a value - naming the right one is what keeps that true if they
	// ever diverge.
	if (TextBlock_127)
	{
		TextBlock_127->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::LabelText)));
	}
}

void ULoadDataParentWidget::SetupTextBlocks()
{
	// Ensure all file names and paths have been set
	UpdateWidgetFileProperties(DataFile);

	// Set the DataFileTextBlock and any other setting for DataFileTextBlock
	if (DataFileTextBlock->IsValidLowLevel())
	{
		// S1 (font half): the path field is authored Font_Inter MONO 11 and is the only widget on the files
		// panel that is neither Regular/SemiBold nor size 10 - measured, not assumed:
		//
		//   fhdr_geo_lbl / fhdr_ped_lbl / fhdr_sim_lbl / fhdr_smoke_lbl   SemiBold 10   (section headers)
		//   GeomLabel / TimingLabel / ShowClosedOpeningsLabel / the row's own label   Regular 10
		//   DataFileTextBlock                                             Mono 11      <- the odd one out
		//
		// So it takes Regular 10, which is both the panel's body face and "Mobius.Text.Field", the app-wide
		// value-readout token. Worth recording WHY it was Mono, because it looks deliberate and half is: a
		// file path is the same content class as the numeric/path inputs that StyleEditableTextBoxForTheme
		// gives Font_Inter Mono 11 (UIThemeSubsystem.cpp:1617-1631). That token is right for a
		// UEditableTextBox and wrong here - this is a read-only label sitting among Regular-10 siblings, and
		// the owner's ruling is that it should match its panel rather than the input ramp. Do not "restore"
		// Mono by arguing from that comment.
		//
		// Set from the owner rather than authored per-asset (three .uassets, public repo) or bolted onto the
		// generic UTextBlock pass (there is none, and adding one would restyle every text block in the app).
		// Theme-INDEPENDENT, so it belongs here on the construct path and not in ApplyMobiusTheme.
		if (UFont* Inter = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter")))
		{
			const FSlateFontInfo& Current = DataFileTextBlock->GetFont();
			if (Current.FontObject != Inter
				|| Current.TypefaceFontName != FName(TEXT("Regular"))
				|| Current.Size != 10)
			{
				DataFileTextBlock->SetFont(FSlateFontInfo(Inter, 10, FName(TEXT("Regular"))));
			}
		}

		DataFileTextBlock->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DataFileTextBlock is nullptr"));
	}
}

void ULoadDataParentWidget::UpdateFileTextBlockTexts() const
{
	// Update DataFileTextBlock with the new data file
	if (DataFileTextBlock->IsValidLowLevel())
	{

		DataFileTextBlock->SetText(FText::FromString(DataFile));

		// Update Scrollbar
		UpdateScrollBarPosition();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DataFileTextBlock is nullptr"));
	}
}

void ULoadDataParentWidget::UpdateWidgetFileProperties(FString CompleteDataPath)
{
	// Set the data file to the selected file
	DataFile = CompleteDataPath;
}

void ULoadDataParentWidget::GetMobiusGameInstanceData()
{
}

void ULoadDataParentWidget::UpdateMobiusGameInstanceData()
{
}

void ULoadDataParentWidget::UpdateScrollBarPosition() const
{
	// is the scroll box valid
	if (DataFileScrollBox)
	{
		// scroll to the end
		DataFileScrollBox->ScrollToEnd();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ScrollBox is nullptr"));
	}
}

void ULoadDataParentWidget::DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess,
	bool bMeshSuccess)
{
}
