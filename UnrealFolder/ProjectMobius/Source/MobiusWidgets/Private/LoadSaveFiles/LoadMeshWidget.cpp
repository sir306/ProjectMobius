// Fill out your copyright notice in the Description page of Project Settings.


#include "LoadSaveFiles/LoadMeshWidget.h"
#include "LoadSaveFiles/QtFileOpenSubsystem.h"

void ULoadMeshWidget::NativeConstruct()
{
	Super::NativeConstruct();

}

void ULoadMeshWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void ULoadMeshWidget::OnSelectFileButtonClicked()
{

	//TODO: IMPLEMENT SPECIFIC HTTP REQUESTS!!!
	
	if (UQtFileOpenSubsystem* QtFileOpenSubsystem = GetWorld()->GetSubsystem<UQtFileOpenSubsystem>())
	{
		if (QtFileOpenSubsystem->IsQtAppRunning() == false)
		{
			QtFileOpenSubsystem->LaunchQtDialogApp();
		}
		FOnFileSelectedDelegate OnFileSelectedDelegate;
		
		// Bind the delegate to DialogClosed before requesting
		OnFileSelectedDelegate.BindDynamic(this, &ULoadMeshWidget::DialogClosed);

		QtFileOpenSubsystem->RequestFileDialogFromQt(
			OnFileSelectedDelegate);

	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("QtFileOpenSubsystem not available"));
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
		UE_LOG(LogTemp, Warning, TEXT("The file dialog was canceled or an error occurred"));
	}
}

