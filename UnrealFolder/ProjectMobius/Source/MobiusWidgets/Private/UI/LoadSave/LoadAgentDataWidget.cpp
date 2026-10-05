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

#include "UI/LoadSave/LoadAgentDataWidget.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Subsystems/NativeFileDialogSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Hdf5SimulationReader.h"
#include "UI/MobiusConfirmDialog.h"
//#include "MassAI/Subsystems/TimeDilationSubSystem.h"

void ULoadAgentDataWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void ULoadAgentDataWidget::OnSelectFileButtonClicked()
{

	if (UNativeFileDialogSubsystem* FileDialogSubsystem = GetWorld()->GetSubsystem<UNativeFileDialogSubsystem>())
	{
		FOnFileSelectedDelegate OnFileSelectedDelegate;

		// Bind the delegate to DialogClosed before requesting
		OnFileSelectedDelegate.BindDynamic(this, &ULoadAgentDataWidget::DialogClosed);

		// Bind error delegate to show popup if dialog fails to open
		FileDialogSubsystem->OnDialogError.BindDynamic(this, &ULoadAgentDataWidget::OnDialogError);

		FileDialogSubsystem->RequestAgentFileDialog(OnFileSelectedDelegate);

	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("File Dialog Error"),
				FText::FromString("Subsystem unavailable"),
				FText::FromString("NativeFileDialogSubsystem not available."),
				FText::FromString("LoadAgentDataWidget"));
		}
		UE_LOG(LogTemp, Error, TEXT("NativeFileDialogSubsystem not available"));
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
	if (AgentFilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("File dialog canceled."));
		return;
	}

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

		// A Juelich .h5 can carry the scene it was simulated in as a root `wkt_geometry`
		// attribute, and the geometry loader already knows how to read one
		// (FAssimpMeshLoaderRunnable::LoadWKTFile). ASK rather than load it automatically: the
		// user may have deliberately paired these trajectories with different geometry, and
		// silently replacing their scene is worse than not offering.
		//
		// Deliberately AFTER the agent load above, so the thing that was actually asked for is
		// never held up by the secondary question.
		OfferEmbeddedGeometry(AgentFilePath);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Invalid Agent Data File"),
				FText::FromString("Unsupported agent data file type selected."),
				FText::FromString("Supported types: .json, .h5"),
				FText::FromString("Load Agent Data"));
		}
		UE_LOG(LogTemp, Warning, TEXT("The file dialog was canceled or an error occurred"));
	}
}

void ULoadAgentDataWidget::OfferEmbeddedGeometry(const FString& AgentFilePath)
{
	if (!FHdf5SimulationReader::HasWktGeometry(AgentFilePath))
	{
		return;
	}

	UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld());
	if (!MobiusGameInstance)
	{
		return;
	}

	// Already the loaded geometry -- nothing to offer, and asking would be noise.
	if (MobiusGameInstance->GetSimulationMeshFilePath() == AgentFilePath)
	{
		return;
	}

	const FText Body = MobiusGameInstance->GetSimulationMeshFilePath().IsEmpty()
		? FText::FromString(TEXT("Load the geometry from this file as well?"))
		: FText::FromString(TEXT("Load the geometry from this file as well? This replaces the "
			"geometry currently loaded."));

	if (!MobiusConfirmDialog::ShowYesNo(
			this,
			FText::FromString(TEXT("Geometry Detected")),
			FText::FromString(FString::Printf(
				TEXT("'%s' also contains geometry."), *FPaths::GetCleanFilename(AgentFilePath))),
			Body))
	{
		return;
	}

	MobiusGameInstance->SetSimulationMeshFilePath(AgentFilePath);
}

void ULoadAgentDataWidget::OnDialogError(const FString& ErrorTitle, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("File dialog error: %s - %s"), *ErrorTitle, *ErrorMessage);
}

void ULoadAgentDataWidget::BindGameInstanceFileDelegate()
{
	// The UPDATED delegate, not CHANGED: SetPedestrianDataFilePath broadcasts both, but
	// OnPedestrianVectorFileChanged carries an FString parameter and RefreshFromGameInstance takes
	// none - and the path is read back out of the game instance anyway, so the parameterless signal
	// is the correct one to bind.
	if (UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld()))
	{
		MobiusGameInstance->OnPedestrianVectorFileUpdated.AddUniqueDynamic(this, &ULoadAgentDataWidget::RefreshFromGameInstance);
	}
}

void ULoadAgentDataWidget::UnbindGameInstanceFileDelegate()
{
	if (UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld()))
	{
		MobiusGameInstance->OnPedestrianVectorFileUpdated.RemoveDynamic(this, &ULoadAgentDataWidget::RefreshFromGameInstance);
	}
}
