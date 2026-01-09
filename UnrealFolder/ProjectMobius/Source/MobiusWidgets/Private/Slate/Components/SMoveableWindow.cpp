// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SMoveableWindow.h"


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

        OnStatusMessage = InArgs._OnStatusMessage;
        if (OnStatusMessage.IsBound())
        {
                OnStatusMessage.Execute(FText::FromString(TEXT("SMoveableWindow::Construct")));
        }
	
	// Default window DOESN'T TICK so we need to override that behavior after calling the parent construct
	SetCanTick(true);
	
}

void SMoveableWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
        SWindow::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

        if (OnStatusMessage.IsBound())
        {
        	if (Morpher.bIsActive)
        	{
        		const FText StatusText = FText::FromString(TEXT("SMoveableWindow::Tick (morphing)"));
        		OnStatusMessage.Execute(StatusText);
        		
        	}
        	else if (Morpher.bIsAnimatingWindowSize)
        	{
        		const FText StatusText = FText::FromString(TEXT("SMoveableWindow::Tick (bIsAnimatingWindowSize)"));
        		OnStatusMessage.Execute(StatusText);
        	}
        	else
        	{
        		const FText StatusText = IsMorphing()
						? FText::FromString(TEXT("SMoveableWindow::Tick (morphing)"))
						: FText::FromString(TEXT("SMoveableWindow::Tick (idle)"));
        		OnStatusMessage.Execute(StatusText);
        	}
              if (LastUpdatedPosition != GetPositionInScreen())
              {
              	const FText StatusText = FText::FromString(TEXT("SMoveableWindow::Tick (Has moved)"));
			  	LastUpdatedPosition = GetPositionInScreen();
              	OnStatusMessage.Execute(StatusText);
              }  
        }
}
