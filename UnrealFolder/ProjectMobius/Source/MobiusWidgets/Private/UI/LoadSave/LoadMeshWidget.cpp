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

#include "UI/LoadSave/LoadMeshWidget.h"
#include "Subsystems/NativeFileDialogSubsystem.h"
#include "Core/MobiusWidgetSubsystem.h"

void ULoadMeshWidget::NativeConstruct()
{
	Super::NativeConstruct();

}

void ULoadMeshWidget::OnSelectFileButtonClicked()
{

	//TODO: IMPLEMENT SPECIFIC HTTP REQUESTS!!!
	
	if (UNativeFileDialogSubsystem* FileDialogSubsystem = GetWorld()->GetSubsystem<UNativeFileDialogSubsystem>())
	{
		FOnFileSelectedDelegate OnFileSelectedDelegate;

		// Bind the delegate to DialogClosed before requesting
		OnFileSelectedDelegate.BindDynamic(this, &ULoadMeshWidget::DialogClosed);

		// Bind error delegate to show popup if dialog fails to open
		FileDialogSubsystem->OnDialogError.BindDynamic(this, &ULoadMeshWidget::OnDialogError);

		FileDialogSubsystem->RequestMeshFileDialog(OnFileSelectedDelegate);

	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("NativeFileDialogSubsystem not available"));
	}

	
	// Call the parent method which will set the mouse to being captured in the view port
	Super::OnSelectFileButtonClicked();
}

void ULoadMeshWidget::GetMobiusGameInstanceData()
{
	Super::GetMobiusGameInstanceData();
	
	// Get world
	UWorld* World = GetWorld();
	
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("World is nullptr"));
		return;
	}

	// Update the data file defaults from the custom game instance
	IProjectMobiusInterface::GetMobiusGameInstanceMeshDataFile(World, DataFile);

	// Update the text block with the new data file
	UpdateWidgetFileProperties(DataFile);
	
}

void ULoadMeshWidget::UpdateMobiusGameInstanceData()
{
	Super::UpdateMobiusGameInstanceData();

	// Get world
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("World is nullptr"));
		return;
	}

	IProjectMobiusInterface::UpdateMobiusGameInstanceMeshDataFile(World, DataFile);
}

void ULoadMeshWidget::DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess,
	bool bMeshSuccess)
{
	if (MeshFilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("File dialog canceled."));
		return;
	}

	// check if the file was successfully opened
	if (bMeshSuccess)
	{
		// log file path
		UE_LOG(LogTemp, Warning, TEXT("Selected file path in widget: %s"), *MeshFilePath);
		
		// update the data file properties
		UpdateWidgetFileProperties(MeshFilePath);

		// Update the text block with the new data file
		UpdateFileTextBlockTexts();

		// Update the game instance with the new data file
		UpdateMobiusGameInstanceData();
	}
	else
	{
		if (UMobiusWidgetSubsystem* WidgetSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMobiusWidgetSubsystem>() : nullptr)
		{
			WidgetSubsystem->DisplayErrorWidget(
				FText::FromString("Invalid Mesh File"),
				FText::FromString("Unsupported mesh file type selected."),
				FText::FromString("Supported types: .fbx, .obj, .udatasmith, .ifc, .wkt"),
				FText::FromString("Load Mesh"));
		}
		UE_LOG(LogTemp, Warning, TEXT("The file dialog was canceled or an error occurred"));
	}
}

void ULoadMeshWidget::OnDialogError(const FString& ErrorTitle, const FString& ErrorMessage)
{
	if (UMobiusWidgetSubsystem* WidgetSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMobiusWidgetSubsystem>() : nullptr)
	{
		WidgetSubsystem->DisplayErrorWidget(
			FText::FromString(TEXT("File Dialog Error")),
			FText::FromString(ErrorTitle),
			FText::FromString(ErrorMessage),
			FText::FromString(TEXT("Load Mesh")));
	}
	UE_LOG(LogTemp, Error, TEXT("File dialog error: %s - %s"), *ErrorTitle, *ErrorMessage);
}

