// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SMoveableWindow.h"


SMoveableWindow::SMoveableWindow()
{
}

SMoveableWindow::~SMoveableWindow()
{
}

void SMoveableWindow::Construct(const FArguments& InArgs)
{
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
	WindowArgs.CreateTitleBar(InArgs._CreateTitleBar);
	WindowArgs.SaneWindowPlacement(InArgs._SaneWindowPlacement);
	WindowArgs.LayoutBorder(InArgs._LayoutBorder);
	WindowArgs.UserResizeBorder(InArgs._UserResizeBorder);

	// Forward the content slot
	WindowArgs._Content = InArgs._Content;

	// Call parent construct
	SWindow::Construct(WindowArgs);

	// Bind to the OnWindowMoved delegate to detect when moves complete
	SetOnWindowMoved(FOnWindowMoved::CreateRaw(this, &SMoveableWindow::HandleWindowMoved));
}

void SMoveableWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SWindow::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Get current position and size
	const FVector2D CurrentPosition = GetPositionInScreen();
	const FVector2D CurrentSize = GetSizeInScreen();

	// Initialize cache on first tick
	if (!bHasInitializedCache)
	{
		LastPosition = CurrentPosition;
		LastSize = CurrentSize;
		bHasInitializedCache = true;
		return;
	}

	// Check for position change (moving)
	const bool bPositionChanged = !CurrentPosition.Equals(LastPosition, 0.1f);
	// Check for size change (resizing) - also check IsMorphingSize for programmatic resize
	const bool bSizeChanged = !CurrentSize.Equals(LastSize, 0.1f) || IsMorphingSize();

	// Handle moving state changes
	if (bPositionChanged && !bSizeChanged && !bIsMoving)
	{
		bIsMoving = true;
		UE_LOG(LogTemp, Warning, TEXT("SMoveableWindow: Window move started"));
	}
	else if (!bPositionChanged && bIsMoving)
	{
		bIsMoving = false;
		UE_LOG(LogTemp, Warning, TEXT("SMoveableWindow: Window move ended"));
	}

	// Handle resizing state changes
	if (bSizeChanged && !bResizing)
	{
		bResizing = true;
		UE_LOG(LogTemp, Warning, TEXT("SMoveableWindow: Window resize started"));
	}
	else if (!bSizeChanged && bResizing)
	{
		bResizing = false;
		UE_LOG(LogTemp, Warning, TEXT("SMoveableWindow: Window resize ended"));
	}

	// Update cached values
	LastPosition = CurrentPosition;
	LastSize = CurrentSize;
}

void SMoveableWindow::HandleWindowMoved(const TSharedRef<SWindow>& Window)
{
	// This delegate fires when window move completes
	// Note: Due to OS-level window dragging on Windows, Tick() is blocked during native drag.
	// This delegate helps catch moves that complete between ticks.
	UE_LOG(LogTemp, Warning, TEXT("SMoveableWindow: OnWindowMoved delegate fired (position: %s)"),
		*GetPositionInScreen().ToString());
}
