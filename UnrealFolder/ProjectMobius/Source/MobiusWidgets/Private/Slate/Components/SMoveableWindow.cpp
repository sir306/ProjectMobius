// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SMoveableWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Widgets/SBoxPanel.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	constexpr float IdleTimerPeriodSeconds = 1.0f;
	constexpr double IdleThresholdSeconds = 1.0;
	const FText PositionChangedText = FText::FromString(TEXT("SMoveableWindow: Position changed"));
	const FText ResizedText = FText::FromString(TEXT("SMoveableWindow: Resized"));
	const FText IdleText = FText::FromString(TEXT("SMoveableWindow: Idle"));
	const FText MouseHeldText = FText::FromString(TEXT("SMoveableWindow: Mouse held"));

	bool TryGetNativeWindowPosition(const TSharedPtr<FGenericWindow>& NativeWindow, FVector2D& OutPosition)
	{
#if PLATFORM_WINDOWS
		if (!NativeWindow.IsValid())
		{
			return false;
		}

		void* WindowHandle = NativeWindow->GetOSWindowHandle();
		if (WindowHandle == nullptr)
		{
			return false;
		}

		RECT WindowRect;
		if (!::GetWindowRect(reinterpret_cast<HWND>(WindowHandle), &WindowRect))
		{
			return false;
		}

		OutPosition = FVector2D(static_cast<float>(WindowRect.left), static_cast<float>(WindowRect.top));
		return true;
#else
		return false;
#endif
	}
}

// FOnMoveableWindowActivityChanged& SMoveableWindow::OnActivityChanged()
// {
// 	static FOnMoveableWindowActivityChanged ActivityDelegate;
// 	return ActivityDelegate;
// }

// int32 SMoveableWindow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
// 	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
// 	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
// {
// 	// the parent SWindow::OnPaint is private, but we can call it in the same manner to ensure normal drawing happens
// 	int32 MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
// 	// 2. Check if the size has changed since the last frame
// 	const FVector2D CurrentSize = AllottedGeometry.GetLocalSize();
//     
// 	if (CurrentSize != LastPaintSize)
// 	{
// 		// Update the cache
// 		LastPaintSize = CurrentSize;
//
// 		// We need to cast 'this' to non-const to call your helper functions
// 		// This is safe because we are only updating logic state, not rendering state
// 		SMoveableWindow* MutableThis = const_cast<SMoveableWindow*>(this);
//         
// 		// 3. Trigger your existing activity logic (Pauses the Sim)
// 		MutableThis->BroadcastActivityIfChanged(true);
//         
// 		// Optional: Update the timestamp so the Idle Timer knows we are busy
// 		MutableThis->LastMoveTimeSeconds = FPlatformTime::Seconds();
// 		MutableThis->bIsIdle = false;
//
// 		// Log or Status update
// 		UE_LOG(LogTemp, Log, TEXT("SMoveableWindow: Resizing via OnPaint (Size: %s)"), *CurrentSize.ToString());
// 	}
//
// 	return MaxLayer;
// }

// void SMoveableWindow::BroadcastActivityIfChanged(bool bIsActive)
// {
// 	if (bHasActivityState && bWasActive == bIsActive)
// 	{
// 		return;
// 	}
//
// 	bHasActivityState = true;
// 	bWasActive = bIsActive;
// 	OnActivityChanged().Broadcast(bIsActive);
// }

// void SMoveableWindow::UpdateMouseHeldState(bool bIsHeld)
// {
// 	if (bIsMouseHeld == bIsHeld)
// 	{
// 		return;
// 	}
//
// 	bIsMouseHeld = bIsHeld;
// 	if (bIsMouseHeld)
// 	{
// 		UE_LOG(LogTemp, Log, TEXT("SMoveableWindow: Mouse held"));
// 		if (OnStatusMessage.IsBound())
// 		{
// 			OnStatusMessage.Execute(MouseHeldText);
// 		}
// 	}
// 	else
// 	{
// 		UE_LOG(LogTemp, Log, TEXT("SMoveableWindow: Mouse released"));
// 		if (OnStatusMessage.IsBound())
// 		{
// 			OnStatusMessage.Execute(IdleText);
// 		}
// 	}
// }

// void SMoveableWindow::SetTitleBarMouseHeld(bool bIsHeld)
// {
// 	UpdateMouseHeldState(bIsHeld);
// }

void SMoveableWindow::Construct(const FArguments& InArgs)
{
	// Store whether we want a title bar before passing to parent
	const bool bWantTitleBar = InArgs._CreateTitleBar;

	// Build SWindow::FArguments from our arguments and construct the parent
	SWindow::FArguments WindowArgs;
	WindowArgs.Type(InArgs._Type);
	WindowArgs.Style(InArgs._Style);
	WindowArgs.Title(InArgs._Title);
	WindowArgs.bDragAnywhere(InArgs._bDragAnywhere);
	WindowArgs.AutoCenter(InArgs._AutoCenter);
	WindowArgs.ScreenPosition(InArgs._ScreenPosition);
	WindowArgs.ClientSize(InArgs._ClientSize);
	WindowArgs.SupportsTransparency(InArgs._SupportsTransparency);
	WindowArgs.InitialOpacity(InArgs._InitialOpacity);
	WindowArgs.IsInitiallyMaximized(InArgs._IsInitiallyMaximized);
	WindowArgs.IsInitiallyMinimized(InArgs._IsInitiallyMinimized);
	WindowArgs.SizingRule(InArgs._SizingRule);
	WindowArgs.IsPopupWindow(InArgs._IsPopupWindow);
	WindowArgs.IsTopmostWindow(InArgs._IsTopmostWindow);
	WindowArgs.FocusWhenFirstShown(InArgs._FocusWhenFirstShown);
	WindowArgs.ActivationPolicy(InArgs._ActivationPolicy);
	WindowArgs.UseOSWindowBorder(InArgs._UseOSWindowBorder);
	WindowArgs.HasCloseButton(InArgs._HasCloseButton);
	WindowArgs.SupportsMaximize(InArgs._SupportsMaximize);
	WindowArgs.SupportsMinimize(InArgs._SupportsMinimize);
	WindowArgs.ShouldPreserveAspectRatio(InArgs._ShouldPreserveAspectRatio);
	// IMPORTANT: Always tell SWindow NOT to create its title bar - we'll create our own
	WindowArgs.CreateTitleBar(false);
	WindowArgs.SaneWindowPlacement(InArgs._SaneWindowPlacement);
	WindowArgs.LayoutBorder(InArgs._LayoutBorder);
	WindowArgs.UserResizeBorder(InArgs._UserResizeBorder);
	// Store alignment for later use
	TitleBarContentAlignment = InArgs._TitleBarContentAlignment;

	// Store status message delegate before parent construct
	OnStatusMessage = InArgs._OnStatusMessage;

	// DON'T forward content to parent - we'll wrap it with our title bar
	// WindowArgs._Content = InArgs._Content;  // Removed

	// Call parent construct FIRST - window must be ready before we can set content
	SWindow::Construct(WindowArgs);

	// Create and set up our custom title bar if requested
	if (bWantTitleBar)
	{
		// Check if caller provided an external title bar
		if (InArgs._TitleBarContent.IsValid())
		{
			TitleBarContent = StaticCastSharedPtr<SWindowTitleBarWidget>(InArgs._TitleBarContent);
		}
		else
		{
			// Create our own SWindowTitleBarWidget
			SAssignNew(TitleBarContent, SWindowTitleBarWidget)
			.OwnerWindow(SharedThis(this))
			.TitleText(InArgs._Title.Get(FText::GetEmpty()))
			.TitleTextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
			.WindowStyle(InArgs._Style)
			.TitleAlignment(TitleBarContentAlignment)
			.ShowAppIcon(false);
		}

		// Wrap the user's content with our title bar on top
		TSharedRef<SVerticalBox> ContentWrapper = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			TitleBarContent.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			InArgs._WindowPanelContent.IsValid() ? InArgs._WindowPanelContent.ToSharedRef() : SNew(SBox)
		];

		
		// Set the wrapped content on the window
		SWindow::SetContent(ContentWrapper);
		
		// Need to get the overlay as that is another area of clicking, resizing etc.
		//WindowOverlay->
		// TODO: The issue now seems to be related to the overlay part of the widget. and how it handles resizing and movement events.
		// it seems to have a second to half second delay before it triggers the game tick pausing where as moving by the custom title bar is instant, but also does not cause the system to hang.
		// In standalone game the simulation carries on like nothing is happening. but when i move the window by the edge it instantly freezes the simulation and causes the delay, which is what we want to avoid.
		// it is also the same with resizing the window by the edges.

		// Register the title bar interface for hit-testing and flash effects
		if (TitleBarContent.IsValid())
		{
			SetTitleBar(TitleBarContent->GetTitleBar());
			UE_LOG(LogTemp, Warning, TEXT("SMoveableWindow: Titlebar is valid"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("SMoveableWindow: Titlebar is INVALID"));
		}

		// Set the title bar size so SWindow knows about it for layout calculations
		TitleBarSize = SWindowDefs::DefaultTitleBarSize;
	}
	else
	{
		// No title bar - just set the content directly
		SWindow::SetContent(InArgs._Content.Widget);
		UE_LOG(LogTemp, Log, TEXT("SMoveableWindow: No title bar requested"));
	}

	// FVector2D CurrentPosition = GetPositionInScreen();
	// FVector2D NativePosition;
	// if (TryGetNativeWindowPosition(GetNativeWindow(), NativePosition))
	// {
	// 	CurrentPosition = NativePosition;
	// }
	// LastUpdatedPosition = CurrentPosition;
	// bHasLastPosition = true;
	// LastUpdatedSize = FVector2D(GetClientSizeInScreen());
	// bHasLastSize = true;

	//SetOnWindowMoved(FOnWindowMoved::CreateSP(this, &SMoveableWindow::HandleWindowMoved));
	// LastMoveTimeSeconds = FPlatformTime::Seconds();
	// bIsIdle = true;
	// if (OnStatusMessage.IsBound())
	// {
	// 	OnStatusMessage.Execute(IdleText);
	// }

	// if (!bIdleTimerRegistered)
	// {
	// 	RegisterActiveTimer(
	// 		IdleTimerPeriodSeconds,
	// 		FWidgetActiveTimerDelegate::CreateSP(this, &SMoveableWindow::HandleIdleTimer));
	// 	bIdleTimerRegistered = true;
	// }

	// Default window DOESN'T TICK so we need to override that behavior after calling the parent construct
	SetCanTick(true);
}

void SMoveableWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SWindow::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// if (FSlateApplication::IsInitialized())
	// {
	// 	const bool bLeftMouseButtonDown = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	// 	UpdateMouseHeldState(bLeftMouseButtonDown);
	// }
	//
	// const FVector2D CurrentSize = FVector2D(GetClientSizeInScreen());       
	// if (!bHasLastSize || !CurrentSize.Equals(LastUpdatedSize))
	// {
	// 	LastUpdatedSize = CurrentSize;
	// 	bHasLastSize = true;
	// 	LastMoveTimeSeconds = FPlatformTime::Seconds();
	// 	bIsIdle = false;
	// 	BroadcastActivityIfChanged(true);
	// 	if (OnStatusMessage.IsBound())
	// 	{
	// 		OnStatusMessage.Execute(bIsMouseHeld ? MouseHeldText : ResizedText);
	// 	}
	// }
}

// void SMoveableWindow::HandleWindowMoved(const TSharedRef<SWindow>& Window)
// {
// 	FVector2D CurrentPosition = Window->GetPositionInScreen();
// 	FVector2D NativePosition;
// 	if (TryGetNativeWindowPosition(Window->GetNativeWindow(), NativePosition))
// 	{
// 		CurrentPosition = NativePosition;
// 	}
//
// 	if (!bHasLastPosition || !CurrentPosition.Equals(LastUpdatedPosition))
// 	{
// 		LastUpdatedPosition = CurrentPosition;
// 		bHasLastPosition = true;
// 		LastMoveTimeSeconds = FPlatformTime::Seconds();
// 		bIsIdle = false;
// 		BroadcastActivityIfChanged(true);
// 		if (OnStatusMessage.IsBound())
// 		{
// 			OnStatusMessage.Execute(bIsMouseHeld ? MouseHeldText : PositionChangedText);
// 		}
// 	}
// }
//
// EActiveTimerReturnType SMoveableWindow::HandleIdleTimer(double InCurrentTime, float InDeltaTime)
// {
// 	(void)InCurrentTime;
// 	(void)InDeltaTime;
//
// 	const double NowSeconds = FPlatformTime::Seconds();
// 	if (!bIsIdle && (NowSeconds - LastMoveTimeSeconds) >= IdleThresholdSeconds)
// 	{
// 		bIsIdle = true;
// 		BroadcastActivityIfChanged(false);
// 		if (OnStatusMessage.IsBound())
// 		{
// 			OnStatusMessage.Execute(bIsMouseHeld ? MouseHeldText : IdleText);
// 		}
// 	}
//
// 	return EActiveTimerReturnType::Continue;
// }
