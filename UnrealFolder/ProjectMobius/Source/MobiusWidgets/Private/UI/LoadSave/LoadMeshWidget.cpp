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
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Subsystems/NativeFileDialogSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Hdf5SimulationReader.h"
#include "UI/MobiusConfirmDialog.h"

void ULoadMeshWidget::NativeConstruct()
{
	Super::NativeConstruct();

}

void ULoadMeshWidget::OnSelectFileButtonClicked()
{
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
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("File Dialog Error"),
				FText::FromString("Subsystem unavailable"),
				FText::FromString("NativeFileDialogSubsystem not available."),
				FText::FromString("LoadMeshWidget"));
		}
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

		// The mirror of ULoadAgentDataWidget::OfferEmbeddedGeometry: an .h5 picked as GEOMETRY may
		// also hold the trajectories. Ask, do not assume -- see that function for the reasoning.
		OfferEmbeddedAgentData(MeshFilePath);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Invalid Mesh File"),
				FText::FromString("Unsupported mesh file type selected."),
				// .h5 belongs here: the dialog filter offers it and bMeshSuccess accepts it
				// (NativeFileDialogSubsystem), so omitting it made this message contradict the
				// picker the user had just used.
				FText::FromString("Supported types: .fbx, .obj, .udatasmith, .ifc, .wkt, .h5"),
				FText::FromString("Load Mesh"));
		}
		UE_LOG(LogTemp, Warning, TEXT("The file dialog was canceled or an error occurred"));
	}
}

void ULoadMeshWidget::OfferEmbeddedAgentData(const FString& MeshFilePath)
{
	// Only an .h5 can hold both. DetectFormat is heavier than the geometry side's H5Aexists probe
	// (it opens and runs two H5Lexists), which is fine once at file-pick time -- do not reuse it
	// anywhere hotter.
	if (!MeshFilePath.EndsWith(TEXT(".h5"), ESearchCase::IgnoreCase)
		|| FHdf5SimulationReader::DetectFormat(MeshFilePath) == EHdf5FormatType::Unknown)
	{
		return;
	}

	UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld());
	if (!MobiusGameInstance || MobiusGameInstance->GetPedestrianDataFilePath() == MeshFilePath)
	{
		return;
	}

	const FText Body = MobiusGameInstance->GetPedestrianDataFilePath().IsEmpty()
		? FText::FromString(TEXT("Load the agent trajectories from this file as well?"))
		: FText::FromString(TEXT("Load the agent trajectories from this file as well? This "
			"replaces the agent data currently loaded."));

	if (!MobiusConfirmDialog::ShowYesNo(
			this,
			FText::FromString(TEXT("Agent Vectors Detected")),
			FText::FromString(FString::Printf(
				TEXT("'%s' also contains agent trajectory data."),
				*FPaths::GetCleanFilename(MeshFilePath))),
			Body))
	{
		return;
	}

	MobiusGameInstance->SetPedestrianDataFilePath(MeshFilePath);
}

void ULoadMeshWidget::OnDialogError(const FString& ErrorTitle, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("File dialog error: %s - %s"), *ErrorTitle, *ErrorMessage);
}

void ULoadMeshWidget::BindGameInstanceFileDelegate()
{
	// OnMeshFileChanged, not OnMeshScaleChanged: this is the delegate SetSimulationMeshFilePath
	// broadcasts, so it fires for a Browse selection AND for a preload / console-command path push.
	if (UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld()))
	{
		MobiusGameInstance->OnMeshFileChanged.AddUniqueDynamic(this, &ULoadMeshWidget::RefreshFromGameInstance);
	}
}

void ULoadMeshWidget::UnbindGameInstanceFileDelegate()
{
	if (UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld()))
	{
		MobiusGameInstance->OnMeshFileChanged.RemoveDynamic(this, &ULoadMeshWidget::RefreshFromGameInstance);
	}
}

