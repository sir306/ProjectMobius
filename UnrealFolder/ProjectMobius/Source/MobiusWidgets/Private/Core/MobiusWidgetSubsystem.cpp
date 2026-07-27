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

#include "Core/MobiusWidgetSubsystem.h"

#include "Diagnostics/MobiusClickLog.h"
#include "UI/ImprovedLoadingNotifyWidget.h"
#include "UI/LoadingNotifyWidget.h"
#include "ErrorHandling/ErrorWindowWidget.h"
#include "Logging/LogWindow.h"
#include "Components/PanelWidget.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widgets/Simulation/SimulationPlayBar.h"

namespace
{
	/**
	 * The loading sub-text is a fixed-width line inside a 400x152 popup: WBP_LoadingBar's LoadingText
	 * wraps at 300px in Inter Regular 10 (~54 chars a line) and has room for two lines before it collides
	 * with the percent readout and the bar. Long simulation / geometry file names blew past that, which is
	 * what made the text look squished. Middle-elide past the budget — the head and the extension carry
	 * the information. ASCII "..." on purpose: a narrow-string ellipsis renders as tofu (MEMORY
	 * reference-ue-nonascii-literal-mojibake).
	 */
	constexpr int32 GMaxLoadingTextChars = 100;

	FString ElideLoadingTextMiddle(const FString& InText)
	{
		if (InText.Len() <= GMaxLoadingTextChars)
		{
			return InText;
		}

		const int32 Keep = GMaxLoadingTextChars - 3; // room for the ellipsis
		const int32 HeadLen = (Keep + 1) / 2;
		return InText.Left(HeadLen) + TEXT("...") + InText.Right(Keep - HeadLen);
	}
}

UMobiusWidgetSubsystem::UMobiusWidgetSubsystem(): ErrorWidget(nullptr), LoadingNotifyWidget(nullptr)
{
}

void UMobiusWidgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Click-path diagnostics: a NO-OP unless `Mobius.LogClicks` is already non-zero — nothing is hooked
	// while the flag is 0 (the default), and the cvar's OnChanged sink hooks it live if it is raised later.
	MobiusClickLog::RegisterSlateListener();

	// add the loading subsystem dependency to the collection and then bind our methods to its delegates
	if (ULoadingSubsystem* LoadingSubsystem = Collection.InitializeDependency<ULoadingSubsystem>())
	{
		// ensure that it is not already bound to the delegates
		// OnLoadingPercentChanged
		LoadingSubsystem->OnLoadingPercentChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadPercent);
		// OnLoadingTextChanged
		LoadingSubsystem->OnLoadingTextChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::SetLoadingText);
		// OnLoadingUnknownDurationChanged
		LoadingSubsystem->OnLoadingUnknownDurationChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget);
		
		
		// bind the loading percent changed delegate
		LoadingSubsystem->OnLoadingPercentChanged.AddDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadPercent);
		// bind the loading text changed delegate
		LoadingSubsystem->OnLoadingTextChanged.AddDynamic(this, &UMobiusWidgetSubsystem::SetLoadingText);
		// OnLoadingUnknownDurationChanged
		LoadingSubsystem->OnLoadingUnknownDurationChanged.AddDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget);
	}

	if (SimulationPlayBarConstructedHandle.IsValid())
	{
		USimulationPlayBar::OnPlayBarConstructed().Remove(SimulationPlayBarConstructedHandle);
		SimulationPlayBarConstructedHandle.Reset();
	}
	if (SimulationPlayBarDestructedHandle.IsValid())
	{
		USimulationPlayBar::OnPlayBarDestructed().Remove(SimulationPlayBarDestructedHandle);
		SimulationPlayBarDestructedHandle.Reset();
	}
	SimulationPlayBars.Reset();
	SimulationPlayBarConstructedHandle = USimulationPlayBar::OnPlayBarConstructed().AddUObject(this, &UMobiusWidgetSubsystem::HandleSimulationPlayBarConstructed);
	SimulationPlayBarDestructedHandle = USimulationPlayBar::OnPlayBarDestructed().AddUObject(this, &UMobiusWidgetSubsystem::HandleSimulationPlayBarDestructed);

	Super::Initialize(Collection);

	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = GameInstance->GetSubsystem<UMobiusUserFeedbackSubsystem>())
		{
			FeedbackSubsystem = Feedback;
			FeedbackErrorHandle = Feedback->OnErrorReported().AddUObject(this, &UMobiusWidgetSubsystem::HandleErrorReported);
			FeedbackLogLineHandle = Feedback->OnLogLine().AddUObject(this, &UMobiusWidgetSubsystem::HandleLogLine);
			FeedbackLogWindowHandle = Feedback->OnLogWindowCommand().AddUObject(this, &UMobiusWidgetSubsystem::HandleLogWindowCommand);

			DeferredErrors.Append(Feedback->DrainPendingErrors());
			FlushDeferredErrors();

			bLogWindowEnabled = Feedback->IsLogWindowEnabled();
			if (bLogWindowEnabled && Feedback->IsLogWindowOpen())
			{
				OpenLogWindow();
			}
		}
	}

}

void UMobiusWidgetSubsystem::Deinitialize()
{
	MobiusClickLog::UnregisterSlateListener();

        MoveableWindowActivityRefCount = 0;
	if (MoveableWindowActivityHandle.IsValid())
	{
		//SMoveableWindow::OnActivityChanged().Remove(MoveableWindowActivityHandle);
		MoveableWindowActivityHandle.Reset();
	}

	if (SimulationPlayBarConstructedHandle.IsValid())
	{
		USimulationPlayBar::OnPlayBarConstructed().Remove(SimulationPlayBarConstructedHandle);
		SimulationPlayBarConstructedHandle.Reset();
	}
	if (SimulationPlayBarDestructedHandle.IsValid())
	{
		USimulationPlayBar::OnPlayBarDestructed().Remove(SimulationPlayBarDestructedHandle);
		SimulationPlayBarDestructedHandle.Reset();
	}
	SimulationPlayBars.Reset();

	if (FeedbackSubsystem.IsValid())
	{
		if (FeedbackErrorHandle.IsValid())
		{
			FeedbackSubsystem->OnErrorReported().Remove(FeedbackErrorHandle);
			FeedbackErrorHandle.Reset();
		}
		if (FeedbackLogLineHandle.IsValid())
		{
			FeedbackSubsystem->OnLogLine().Remove(FeedbackLogLineHandle);
			FeedbackLogLineHandle.Reset();
		}
		if (FeedbackLogWindowHandle.IsValid())
		{
			FeedbackSubsystem->OnLogWindowCommand().Remove(FeedbackLogWindowHandle);
			FeedbackLogWindowHandle.Reset();
		}
	}
	FeedbackSubsystem.Reset();
	DeferredErrors.Reset();
	CloseLogWindow();

	// Unbind all bound delegates from the loading subsystem
	if (ULoadingSubsystem* LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>())
	{
		// OnLoadingPercentChanged
		LoadingSubsystem->OnLoadingPercentChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadPercent);

		// OnLoadingTextChanged
		LoadingSubsystem->OnLoadingTextChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::SetLoadingText);

		// OnLoadingUnknownDurationChanged
		LoadingSubsystem->OnLoadingUnknownDurationChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget);
	}
	
	OnLogWindowClosedBP.Clear();
	OnLogWindowClosedNative.Unbind();

        Super::Deinitialize();
}

void UMobiusWidgetSubsystem::ResetForFileSwitch()
{
	// ErrorWidget and LoadingNotifyWidget are persistent infrastructure widgets
	// the subsystem owns for the lifetime of the world. They are bound to live
	// LoadingSubsystem delegates and the next file load broadcasts to them
	// immediately, so nulling them mid-switch crashes the next SetLoadingText.
	// They are NOT the per-simulation MID/shader-map root we suspected.
	// This hook is intentionally a no-op for now and kept as the call site for
	// future per-switch widget resets (e.g. floor-stats panels) without having
	// to re-thread the subsystem reset through MassEntitySpawnSubsystem.
}

void UMobiusWidgetSubsystem::Tick(float DeltaTime)
{
        Super::Tick(DeltaTime);

        if (!bHasPendingMoveableWindowActivity)
        {
                return;
        }
        UE_LOG(LogTemp,Warning, TEXT("Ticking"));

        bHasPendingMoveableWindowActivity = false;
        ApplyMoveableWindowActivity(bPendingMoveableWindowActivity);
}

void UMobiusWidgetSubsystem::RegisterMoveableWindowActivity()
{
        ++MoveableWindowActivityRefCount;
        if (MoveableWindowActivityRefCount == 1)
        {
                if (MoveableWindowActivityHandle.IsValid())
                {
                        //SMoveableWindow::OnActivityChanged().Remove(MoveableWindowActivityHandle);
                        MoveableWindowActivityHandle.Reset();
                }
                //MoveableWindowActivityHandle = SMoveableWindow::OnActivityChanged().AddUObject(this, &UMobiusWidgetSubsystem::HandleMoveableWindowActivityChanged);
        }
}

void UMobiusWidgetSubsystem::UnregisterMoveableWindowActivity()
{
        if (MoveableWindowActivityRefCount <= 0)
        {
                MoveableWindowActivityRefCount = 0;
                return;
        }

        --MoveableWindowActivityRefCount;
        if (MoveableWindowActivityRefCount == 0 && MoveableWindowActivityHandle.IsValid())
        {
                //SMoveableWindow::OnActivityChanged().Remove(MoveableWindowActivityHandle);
                MoveableWindowActivityHandle.Reset();
        }
}

void UMobiusWidgetSubsystem::HandleMoveableWindowActivityChanged(bool bIsActive)
{
	bPendingMoveableWindowActivity = bIsActive;
	bHasPendingMoveableWindowActivity = true;
	UE_LOG(LogTemp, Warning, TEXT("Queued moveable window activity change: %s"), bIsActive ? TEXT("active") : TEXT("idle"));
}

void UMobiusWidgetSubsystem::ApplyMoveableWindowActivity(bool bIsActive)
{
	UE_LOG(LogTemp, Log, TEXT("Moveable window %s"), bIsActive ? TEXT("moving/resizing") : TEXT("idle"));
	for (int32 Index = SimulationPlayBars.Num() - 1; Index >= 0; --Index)
	{
		USimulationPlayBar* PlayBar = SimulationPlayBars[Index].Get();
		if (!PlayBar)
		{
			SimulationPlayBars.RemoveAt(Index);
			continue;
		}

		PlayBar->HandleMoveableWindowActivityChanged(bIsActive);
	}
}

void UMobiusWidgetSubsystem::HandleSimulationPlayBarConstructed(USimulationPlayBar* PlayBar)
{
	if (!PlayBar || PlayBar->GetWorld() != GetWorld())
	{
		return;
	}

	SimulationPlayBars.RemoveAll([](const TWeakObjectPtr<USimulationPlayBar>& PlayBarPtr)
	{
		return !PlayBarPtr.IsValid();
	});
	SimulationPlayBars.AddUnique(PlayBar);
}

void UMobiusWidgetSubsystem::HandleSimulationPlayBarDestructed(USimulationPlayBar* PlayBar)
{
	SimulationPlayBars.RemoveAll([PlayBar](const TWeakObjectPtr<USimulationPlayBar>& PlayBarPtr)
	{
		return !PlayBarPtr.IsValid() || PlayBarPtr.Get() == PlayBar;
	});
}

void UMobiusWidgetSubsystem::HandleErrorReported(const FMobiusErrorMessage& Message)
{
	if (ErrorWidget)
	{
		DisplayErrorWidget(Message.TitleBarText, Message.ErrorTitle, Message.ErrorMessage, Message.ErrorLocation, Message.Severity);
	}
	else
	{
		DeferredErrors.Add(Message);
	}
}

void UMobiusWidgetSubsystem::HandleLogLine(const FString& Line)
{
	if (LogWindowWidget.IsValid() && LogWindowWidget->IsOpen())
	{
		LogWindowWidget->AppendLine(Line);
	}
}

void UMobiusWidgetSubsystem::HandleLogWindowCommand(EMobiusLogWindowCommand Command)
{
	switch (Command)
	{
	case EMobiusLogWindowCommand::Open:
		OpenLogWindow();
		break;
	case EMobiusLogWindowCommand::Close:
		CloseLogWindow();
		break;
	case EMobiusLogWindowCommand::Enable:
		SetLogWindowEnabled(true);
		break;
	case EMobiusLogWindowCommand::Disable:
		SetLogWindowEnabled(false);
		break;
	default:
		break;
	}
}

void UMobiusWidgetSubsystem::OpenLogWindow()
{
	if (!bLogWindowEnabled)
	{
		return;
	}

	if (!LogWindowWidget.IsValid())
	{
		OnLogWindowClosedNative = FOnLogWindowClosed::CreateUObject(this, &UMobiusWidgetSubsystem::LogWindowIsClosing);
		
		LogWindowWidget = SNew(SLogWindowWidget)
			.OnLogWindowClosed(OnLogWindowClosedNative);
		
	}

	if (FeedbackSubsystem.IsValid())
	{
		LogWindowWidget->SetLines(FeedbackSubsystem->GetCachedLogLines());
	}

	LogWindowWidget->ShowLogWindow();
	RegisterMoveableWindowActivity();
}

void UMobiusWidgetSubsystem::CloseLogWindow()
{
	if (LogWindowWidget.IsValid())
	{
		LogWindowWidget->CloseLogWindow();
		LogWindowWidget.Reset();
		UnregisterMoveableWindowActivity();
		OnLogWindowClosedNative.Unbind();
	}
}

void UMobiusWidgetSubsystem::SetLogWindowEnabled(bool bEnabled)
{
	bLogWindowEnabled = bEnabled;
	if (!bLogWindowEnabled)
	{
		CloseLogWindow();
	}
}

void UMobiusWidgetSubsystem::FlushDeferredErrors()
{
	if (!ErrorWidget || DeferredErrors.Num() == 0)
	{
		return;
	}

	for (const FMobiusErrorMessage& Message : DeferredErrors)
	{
		DisplayErrorWidget(Message.TitleBarText, Message.ErrorTitle, Message.ErrorMessage, Message.ErrorLocation, Message.Severity);
	}
	DeferredErrors.Reset();
}

void UMobiusWidgetSubsystem::AddErrorWidget(UErrorWindowWidget* NewErrorWidget)
{
	// Set the error widget
	ErrorWidget = NewErrorWidget;
	FlushDeferredErrors();
}

UErrorWindowWidget* UMobiusWidgetSubsystem::GetErrorWidget() const
{
	return ErrorWidget;
}

void UMobiusWidgetSubsystem::DisplayErrorWidget(const FText& TitleBarText, const FText& ErrorTitle,
	const FText& ErrorMessage, const FText& ErrorLocation, EMobiusErrorSeverity Severity)
{
	if(ErrorWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Error Widget is null, cannot display error"));
		return;
	}
	// We show the error window first, that way it triggers the rebuild and gives it access to the slate and subsystems
	// it needs to function correctly
	ErrorWidget->ShowErrorWindow();
	// Severity drives the emphasis cue; set after ShowErrorWindow so the Slate exists (the accent lambda
	// reads it before first paint on this same synchronous call).
	ErrorWidget->SetErrorSeverity(Severity);
	// Set the variables and display the window
	ErrorWidget->SetTitleBarText(TitleBarText);
	ErrorWidget->SetErrorTitleText(ErrorTitle);
	ErrorWidget->SetErrorMessageText(ErrorMessage);
	ErrorWidget->SetErrorLocationText(ErrorLocation);
}

void UMobiusWidgetSubsystem::SetErrorTitleBarText(const FText& TitleBarText)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetTitleBarText(TitleBarText);
	}
}

void UMobiusWidgetSubsystem::SetErrorTitleText(const FText& ErrorTitle)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetErrorTitleText(ErrorTitle);
	}
}

void UMobiusWidgetSubsystem::SetErrorMessageText(const FText& ErrorMessage)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetErrorMessageText(ErrorMessage);
	}
}

void UMobiusWidgetSubsystem::SetErrorLocationText(const FText& ErrorLocation)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetErrorLocationText(ErrorLocation);
	}
}

void UMobiusWidgetSubsystem::AddLoadingWidget(UImprovedLoadingNotifyWidget* NewLoadingWidget)
{
	// set the loading widget
	LoadingNotifyWidget = NewLoadingWidget;

	// Set the position of the loading widget to the center of the screen
	//UWidgetLayoutLibrary::SlotAsCanvasSlot(LoadingNotifyWidget)->SetPosition(GetCenterPosForWidgetPanel(LoadingNotifyWidget->LoadingNotifyOverlay));

}

UImprovedLoadingNotifyWidget* UMobiusWidgetSubsystem::GetLoadingWidget() const
{
	return LoadingNotifyWidget;
}

void UMobiusWidgetSubsystem::UpdateLoadPercent(float NewLoadPercent)
{
	// check if the loading widget is null
	if(LoadingNotifyWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Loading Widget is null, cannot update load percent"));
		return;
	}
	
	// Update the load percent
	LoadingNotifyWidget->UpdateLoadPercent(NewLoadPercent);

	// log the new load percent
	//UE_LOG(LogTemp, Warning, TEXT("New Load Percent: %f"), NewLoadPercent);
}

void UMobiusWidgetSubsystem::SetLoadingText(bool bIsLoadingBar, FString NewLoadingText) 
{
	// check if the loading widget is null
	if(LoadingNotifyWidget == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("Loading Widget is null, cannot update load percent"));
        return;
    }
	FText LoadingText = FText::FromString(ElideLoadingTextMiddle(NewLoadingText));
	LoadingNotifyWidget->SetLoadingText(bIsLoadingBar, LoadingText);
}

void UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget(bool bIsLoading, FString NewLoadingText)
{
	FText LoadingText = FText::FromString(ElideLoadingTextMiddle(NewLoadingText));
	LoadingNotifyWidget->SetLoadingText(false, LoadingText);
	LoadingNotifyWidget->SetIsLoadingGeometry(bIsLoading);
}

FVector2D UMobiusWidgetSubsystem::GetCenterPosition(UUserWidget* Widget)
{
	// Get widget size
	FVector2D WidgetSize = Widget->GetDesiredSize();

	// Get Viewport Size
	FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());

	// Calculate the center position for this widget
	FVector2D WidgetPosition = FVector2D(ViewportSize.X / 2 - WidgetSize.X / 2, ViewportSize.Y / 2 - WidgetSize.Y / 2);

	return WidgetPosition;
}

FVector2D UMobiusWidgetSubsystem::GetCenterPosForWidgetPanel(UPanelWidget* WidgetPanel)
{
	// Get widget size
	FVector2D WidgetSize = WidgetPanel->GetDesiredSize();

	// Get Viewport Size
	FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());

	// Calculate the center position for this widget
	FVector2D WidgetPosition = FVector2D(ViewportSize.X / 2 - WidgetSize.X / 2, ViewportSize.Y / 2 - WidgetSize.Y / 2);

	return WidgetPosition;
}

void UMobiusWidgetSubsystem::LogWindowIsClosing()
{
	if (OnLogWindowClosedBP.IsBound())
	{
		OnLogWindowClosedBP.Broadcast();
	}
	OnLogWindowClosedNative.Unbind();
	
	if (LogWindowWidget.IsValid())
	{
		LogWindowWidget.Reset();
		UnregisterMoveableWindowActivity();
	}
}
