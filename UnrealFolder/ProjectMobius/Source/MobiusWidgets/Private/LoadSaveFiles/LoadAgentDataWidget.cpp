// Fill out your copyright notice in the Description page of Project Settings.


#include "LoadSaveFiles/LoadAgentDataWidget.h"
#include "LoadSaveFiles/QtFileOpenSubsystem.h"
#include "MassAI/Subsystems/TimeDilationSubSystem.h"

void ULoadAgentDataWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void ULoadAgentDataWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void ULoadAgentDataWidget::OnSelectFileButtonClicked()
{

	if (UQtFileOpenSubsystem* QtFileOpenSubsystem = GetWorld()->GetSubsystem<UQtFileOpenSubsystem>())
	{
		if (QtFileOpenSubsystem->IsQtAppRunning() == false)
		{
			QtFileOpenSubsystem->LaunchQtDialogApp();
		}
		FOnFileSelectedDelegate OnFileSelectedDelegate;
		
		// Bind the delegate to DialogClosed before requesting
		OnFileSelectedDelegate.BindDynamic(this, &ULoadAgentDataWidget::DialogClosed);

		QtFileOpenSubsystem->RequestFileDialogFromQt(
		OnFileSelectedDelegate, TEXT("openAgentFile"));

	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("QtFileOpenSubsystem not available"));
	}
	
	// Calling super here ensures the game view port has focus
	Super::OnSelectFileButtonClicked();
}

void ULoadAgentDataWidget::GetMobiusGameInstanceData()
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
	IProjectMobiusInterface::GetMobiusGameInstancePedestrianData(World, DataFile);

	// Update the text block with the new data file
	UpdateWidgetFileProperties(DataFile);
}

void ULoadAgentDataWidget::UpdateMobiusGameInstanceData()
{
	Super::UpdateMobiusGameInstanceData();
	
	// Get world
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("World is nullptr"));
		return;
	}

	IProjectMobiusInterface::UpdateMobiusGameInstancePedestrianData(World, DataFile);
}

void ULoadAgentDataWidget::DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess,
	bool bMeshSuccess)
{
	// check if the file was successfully opened
	if (bAgentSuccess)
	{
		
		// log file path
		UE_LOG(LogTemp, Warning, TEXT("Selected file path in widget: %s"), *AgentFilePath);
		
		// update the data file properties
		UpdateWidgetFileProperties(AgentFilePath);

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

