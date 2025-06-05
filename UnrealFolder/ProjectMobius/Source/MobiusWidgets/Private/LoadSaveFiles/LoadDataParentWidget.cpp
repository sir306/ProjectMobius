// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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

#include "LoadSaveFiles/LoadDataParentWidget.h"
#include "Blueprint/WidgetTree.h" // Used to create the widget tree and creating/mapping components
#include "Components/CanvasPanel.h" // UCanvasPanel component
#include "Components/Button.h" // UButton component
#include "Components/TextBlock.h" // UTextBlock component
//#include "Components/RichTextBlock.h" // URichTextBlock component TODO
// headers for opening file dialogs
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
// headers for getting the game instance
#include "Components/ButtonWithText.h"
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
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("IsDesignTime"));
	}
	// This will either update with defaults or the data update from game instance
	UpdateFileTextBlockTexts();
}


void ULoadDataParentWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void ULoadDataParentWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	
}

void ULoadDataParentWidget::OnSelectFileButtonClicked()
{
	// No matter what action is taken, the mouse needs to be captured again so that clicks are registered immediately
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
	// not the problem -> its the way dialog box close is handled, in windows it seems to be setting the capture
	// back to window and contained in it without allowing the application to capture the mouse correctly again??
}

void ULoadDataParentWidget::SetupTextBlocks()
{
	// Ensure all file names and paths have been set
	UpdateWidgetFileProperties(DataFile);

	// Set the DataFileTextBlock and any other setting for DataFileTextBlock
	if (DataFileTextBlock->IsValidLowLevel())
	{

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
