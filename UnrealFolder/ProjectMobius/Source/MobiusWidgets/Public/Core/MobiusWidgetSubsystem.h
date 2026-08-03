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

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogWindow.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "MobiusWidgetSubsystem.generated.h"

class UImprovedLoadingNotifyWidget;
class UErrorWindowWidget;
class USimulationPlayBar;
class SLogWindowWidget;
class SErrorWindowWidget;
enum class EMobiusLogWindowCommand : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLogWindowClosedBP);

/**
 *
 */
UCLASS()
class MOBIUSWIDGETS_API UMobiusWidgetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
#pragma region METHODS
public:
	UMobiusWidgetSubsystem();

	/**
	 * Initialize the subsystem and bind loading delegates.
	 * @param Collection Subsystem collection for dependencies.
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

        /**
         * Deinitialize the subsystem and unbind delegates.
         */
        virtual void Deinitialize() override;

        /** Tick -- fires every frame. */
        virtual void Tick(float DeltaTime) override;

        virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMobiusWidgetSubsystem, STATGROUP_Tickables); }

        /**
         * Register this subsystem to receive moveable window activity changes.
         */
        void RegisterMoveableWindowActivity();

        /**
         * Unregister this subsystem from moveable window activity changes.
         */
        void UnregisterMoveableWindowActivity();

	/**
	 * Simple method to add the Error Widget to the Subsystem
	 * - this ensures that the Error Widget is always available to the project
	 *
	 * @param NewErrorWidget - New Error Widget to add to the Subsystem - by design there is only ever one error widget
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void AddErrorWidget(UErrorWindowWidget* NewErrorWidget);

	/**
	 * Method to get the Error Widget
	 *
	 * @return Error Widget
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	UErrorWindowWidget* GetErrorWidget() const;

	/**
	 * Display Error Widget with the new title and message
	 *
	 * @param TitleBarText - Text displayed in the window title bar
	 * @param ErrorTitle - Title of the Error
	 * @param ErrorMessage - Message of the Error
	 * @param ErrorLocation - Optional location text for where the error occurred
	 * @param Severity - Message severity; drives the window's emphasis cue (Error/Fatal = red,
	 *        Warning = amber, Info = accent). Defaults to Error to preserve existing call sites.
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget", meta = (AdvancedDisplay = "ErrorLocation,Severity"))
	void DisplayErrorWidget(const FText& TitleBarText, const FText& ErrorTitle, const FText& ErrorMessage,
		const FText& ErrorLocation = FText::GetEmpty(),
		EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error);

	/**
	 * Update the title bar text for the error window.
	 * @param TitleBarText Text displayed in the window title bar.
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void SetErrorTitleBarText(const FText& TitleBarText);

	/**
	 * Update the error title text.
	 * @param ErrorTitle Error title to display.
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void SetErrorTitleText(const FText& ErrorTitle);

	/**
	 * Update the error message text.
	 * @param ErrorMessage Error message to display.
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void SetErrorMessageText(const FText& ErrorMessage);

	/**
	 * Update the optional error location text.
	 * @param ErrorLocation Optional location text for where the error occurred.
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void SetErrorLocationText(const FText& ErrorLocation);

	/**
	 * Add the loading widget to the subsystem
	 *
	 * @param NewLoadingWidget - New Loading Widget to add to the Subsystem - by design there is only ever one loading widget
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget")
	void AddLoadingWidget(UImprovedLoadingNotifyWidget* NewLoadingWidget);

	/**
	 * Get the Loading Widget
	 *
	 * @return Loading Widget
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget")
	UImprovedLoadingNotifyWidget* GetLoadingWidget() const;

	/**
	 * Update Load percent value used for binding with external delegates
	 *
	 * @param[float] NewLoadPercent - New Load Percent Value
	 */
	UFUNCTION()
	void UpdateLoadPercent(float NewLoadPercent);

	/**
	 * Update the loading text and title for the loading widget
	 *
	 * @param[bool] bIsLoadingBar - update the correct type of loading widget
	 * @param[FString] NewLoadingText - New Loading Title
	 */
	UFUNCTION()
	void SetLoadingText(bool bIsLoadingBar, FString NewLoadingText);

	/**
	 * Update the loading widget when the duration is unknown.
	 *
	 * @param bIsLoading True if loading is active.
	 * @param NewLoadingText New loading text to display.
	 */
	UFUNCTION()
	void UpdateLoadingInfiniteWidget(bool bIsLoading, FString NewLoadingText);

	/**
	 * Drop transient per-simulation widget references (ErrorWidget,
	 * LoadingNotifyWidget) so they don't root the prior simulation's widget
	 * tree across a file switch. The subsystem itself is a world subsystem
	 * and survives the switch; these UPROPERTYs would otherwise stay alive
	 * with all the MIDs / shader maps they reference.
	 */
	void ResetForFileSwitch();

private:
        /**
         * Handle move/resize activity updates from moveable windows.
         * @param bIsActive True when moving/resizing, false when idle.
         */
        void HandleMoveableWindowActivityChanged(bool bIsActive);

        /**
         * Apply move/resize activity updates on the game thread.
         * @param bIsActive True when moving/resizing, false when idle.
         */
        void ApplyMoveableWindowActivity(bool bIsActive);

        /** Track new simulation play bars for move/resize handling. */
	void HandleSimulationPlayBarConstructed(USimulationPlayBar* PlayBar);

        /** Stop tracking a simulation play bar. */
        void HandleSimulationPlayBarDestructed(USimulationPlayBar* PlayBar);

        /** Relay error payloads from the feedback subsystem. */
        void HandleErrorReported(const FMobiusErrorMessage& Message);

        /** Relay log lines into the log window. */
        void HandleLogLine(const FString& Line);

        /** Handle log window commands from the feedback subsystem. */
        void HandleLogWindowCommand(EMobiusLogWindowCommand Command);

        /** Open the log window if enabled. */
        void OpenLogWindow();

        /** Close and destroy the log window. */
        void CloseLogWindow();

        /** Enable or disable the log window. */
        void SetLogWindowEnabled(bool bEnabled);

        /** Flush queued errors once the error widget exists. */
        void FlushDeferredErrors();

        /**
         * Internal method to get a center position of the screen for the specified widget
	 *
	 * @param Widget - Widget to get the center position for
	 * @return Center Position of the Widget
	 */
	FVector2D GetCenterPosition(UUserWidget* Widget);

	/**
	 * Internal method to get a center position of the screen for the specified widget
	 *
	 * @param WidgetPanel - Widget panel to get the center position for
	 * @return Center Position of the Widget
	 */
	FVector2D GetCenterPosForWidgetPanel(class UPanelWidget* WidgetPanel);

	void LogWindowIsClosing();



#pragma endregion METHODS

#pragma region PROPERTIES
public:
	// Error Widget
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Error Widget")
	UErrorWindowWidget* ErrorWidget;

	// Loading Notify Widget
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoadingNotifyWidget")
        UImprovedLoadingNotifyWidget* LoadingNotifyWidget;

	// Log Window Closed Delegate
	FOnLogWindowClosed OnLogWindowClosedNative;

	UPROPERTY(BlueprintAssignable, Category = "Logging")
	FOnLogWindowClosedBP OnLogWindowClosedBP;
#pragma endregion PROPERTIES

private:
        FDelegateHandle MoveableWindowActivityHandle;
        int32 MoveableWindowActivityRefCount = 0;
        bool bHasPendingMoveableWindowActivity = false;
        bool bPendingMoveableWindowActivity = false;
        FDelegateHandle SimulationPlayBarConstructedHandle;
        FDelegateHandle SimulationPlayBarDestructedHandle;
        TArray<TWeakObjectPtr<USimulationPlayBar>> SimulationPlayBars;

        TWeakObjectPtr<UMobiusUserFeedbackSubsystem> FeedbackSubsystem;
        FDelegateHandle FeedbackErrorHandle;
        FDelegateHandle FeedbackLogLineHandle;
        FDelegateHandle FeedbackLogWindowHandle;
        TArray<FMobiusErrorMessage> DeferredErrors;
        TSharedPtr<SLogWindowWidget> LogWindowWidget;
        bool bLogWindowEnabled = true;

        /**
         * True only while CloseLogWindow() is driving the close — i.e. teardown, or the logger being
         * switched off. Lets LogWindowIsClosing tell a PROGRAMMATIC close from a USER one (title-bar x or
         * the window's Close button), which is the only distinction the close path cannot infer for
         * itself: Deinitialize() closes the window through that same function.
         */
        bool bClosingLogWindowProgrammatically = false;

        /**
         * A19, owner ruling 2026-08-03: "if old error window is open then it should display a new error
         * widget". There is only ONE UErrorWindowWidget (a UMG widget placed in the HUD, registered via
         * AddErrorWidget), so a second error used to silently OVERWRITE the first one's text in the same
         * window and the first message was lost. Errors that arrive while a window is already up now get
         * their own free-standing SErrorWindowWidget, held here.
         *
         * This array is the SOLE owner of those widgets — ~SErrorWindowWidget calls CloseErrorWindow(), so
         * dropping an entry destroys its native window. Pruned on entry to DisplayErrorWidget and cleared
         * in Deinitialize (PIE stop must not leave native windows behind).
         */
        TArray<TSharedPtr<SErrorWindowWidget>> ExtraErrorWindows;

        /**
         * Ceiling on the extra windows above. The feedback subsystem already suppresses DUPLICATE prompts
         * (2 s cooldown on a content hash) but nothing caps a burst of DISTINCT errors, and its pending
         * queue is 32 deep — without a cap, one bad load could paper the screen. Past the cap the newest
         * error reuses the primary window, i.e. the pre-A19 behaviour, and says so in the log.
         */
        static constexpr int32 MaxExtraErrorWindows = 4;

        /** Drop entries whose window the user has already closed. Cheap; called before each new error. */
        void PruneClosedErrorWindows();

        /** Close and forget every extra error window, unregistering each one's activity refcount. */
        void CloseAllExtraErrorWindows();
};
