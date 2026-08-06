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

#include "UI/LoadSave/LoadBRiskDataWidget.h"
#include "Subsystems/NativeFileDialogSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "GameInstances/ProjectMobiusGameInstance.h"

void ULoadBRiskDataWidget::OnSelectFileButtonClicked()
{
	if (UNativeFileDialogSubsystem* FileDialogSubsystem = GetWorld()->GetSubsystem<UNativeFileDialogSubsystem>())
	{
		FOnBRiskFileSelectedDelegate OnFileSelectedDelegate;

		// Bind the B-Risk-specific (2-param) callback before requesting.
		OnFileSelectedDelegate.BindDynamic(this, &ULoadBRiskDataWidget::OnBRiskFileDialogClosed);

		// Bind error delegate to show a popup if the dialog fails to open.
		FileDialogSubsystem->OnDialogError.BindDynamic(this, &ULoadBRiskDataWidget::OnDialogError);

		FileDialogSubsystem->RequestBRiskFileDialog(OnFileSelectedDelegate);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("File Dialog Error"),
				FText::FromString("Subsystem unavailable"),
				FText::FromString("NativeFileDialogSubsystem not available."),
				FText::FromString("LoadBRiskDataWidget"));
		}
		UE_LOG(LogTemp, Error, TEXT("NativeFileDialogSubsystem not available"));
	}

	// Calling super here ensures the game view port regains focus.
	Super::OnSelectFileButtonClicked();
}

void ULoadBRiskDataWidget::GetMobiusGameInstanceData()
{
	Super::GetMobiusGameInstanceData();

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("World is nullptr"));
		return;
	}

	if (const UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance()))
	{
		// Mirror the current B-Risk path into the widget's display.
		UpdateWidgetFileProperties(GameInstance->GetBRiskSmvFilePath());
	}
}

void ULoadBRiskDataWidget::UpdateMobiusGameInstanceData()
{
	Super::UpdateMobiusGameInstanceData();

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("World is nullptr"));
		return;
	}

	if (UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance()))
	{
		// Broadcasts OnBRiskFileChanged -> UBRiskDataSubsystem::OnSmvFileChanged -> LoadScenarioFromSmv.
		GameInstance->SetBRiskSmvFilePath(DataFile);
	}
}

void ULoadBRiskDataWidget::OnBRiskFileDialogClosed(const FString& SmvFilePath, bool bSuccess)
{
	if (SmvFilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("B-Risk file dialog canceled."));
		return;
	}

	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("Selected B-Risk file path in widget: %s"), *SmvFilePath);

		// Store + display the path, then push it to the game instance (which triggers the load).
		UpdateWidgetFileProperties(SmvFilePath);
		UpdateFileTextBlockTexts();
		UpdateMobiusGameInstanceData();
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Invalid B-Risk Data File"),
				FText::FromString("Unsupported B-Risk file type selected."),
				FText::FromString("Supported type: .smv"),
				FText::FromString("Load B-Risk Data"));
		}
		UE_LOG(LogTemp, Warning, TEXT("The B-Risk file dialog was canceled or an error occurred"));
	}
}

void ULoadBRiskDataWidget::OnDialogError(const FString& ErrorTitle, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("File dialog error: %s - %s"), *ErrorTitle, *ErrorMessage);
}

void ULoadBRiskDataWidget::BindGameInstanceFileDelegate()
{
	// Same delegate UBRiskDataSubsystem::OnSmvFileChanged listens on, so the field and the load are
	// driven by one signal.
	if (UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld()))
	{
		MobiusGameInstance->OnBRiskFileChanged.AddUniqueDynamic(this, &ULoadBRiskDataWidget::RefreshFromGameInstance);
	}
}

void ULoadBRiskDataWidget::UnbindGameInstanceFileDelegate()
{
	if (UProjectMobiusGameInstance* MobiusGameInstance = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld()))
	{
		MobiusGameInstance->OnBRiskFileChanged.RemoveDynamic(this, &ULoadBRiskDataWidget::RefreshFromGameInstance);
	}
}
