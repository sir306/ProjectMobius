// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SMoveableWindow.h"
#include "GenericPlatform/GenericWindow.h"
#include "Types/WidgetActiveTimerDelegate.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	constexpr float IdleTimerPeriodSeconds = 0.25f;
	constexpr double IdleThresholdSeconds = 0.25;
	const FText PositionChangedText = FText::FromString(TEXT("SMoveableWindow: Position changed"));
	const FText ResizedText = FText::FromString(TEXT("SMoveableWindow: Resized"));
	const FText IdleText = FText::FromString(TEXT("SMoveableWindow: Idle"));

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
	TitleBarContent = InArgs._TitleBarContent;
	TitleBarContentAlignment = InArgs._TitleBarContentAlignment;

	// Forward the content slot
	WindowArgs._Content = InArgs._Content;

	// Call parent construct
	SWindow::Construct(WindowArgs);

	OnStatusMessage = InArgs._OnStatusMessage;
	FVector2D CurrentPosition = GetPositionInScreen();
	FVector2D NativePosition;
	if (TryGetNativeWindowPosition(GetNativeWindow(), NativePosition))
	{
		CurrentPosition = NativePosition;
	}
	LastUpdatedPosition = CurrentPosition;
	bHasLastPosition = true;
	LastUpdatedSize = FVector2D(GetClientSizeInScreen());
	bHasLastSize = true;

	if (OnStatusMessage.IsBound())
	{
		SetOnWindowMoved(FOnWindowMoved::CreateSP(this, &SMoveableWindow::HandleWindowMoved));
		LastMoveTimeSeconds = FPlatformTime::Seconds();
		bIsIdle = true;
		OnStatusMessage.Execute(IdleText);

		if (!bIdleTimerRegistered)
		{
			RegisterActiveTimer(
				IdleTimerPeriodSeconds,
				FWidgetActiveTimerDelegate::CreateSP(this, &SMoveableWindow::HandleIdleTimer));
			bIdleTimerRegistered = true;
		}
	}
	
	// Default window DOESN'T TICK so we need to override that behavior after calling the parent construct
	SetCanTick(true);
	
}

void SMoveableWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SWindow::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!OnStatusMessage.IsBound())
	{
		return;
	}

	const FVector2D CurrentSize = FVector2D(GetClientSizeInScreen());
	if (!bHasLastSize || !CurrentSize.Equals(LastUpdatedSize))
	{
		LastUpdatedSize = CurrentSize;
		bHasLastSize = true;
		LastMoveTimeSeconds = FPlatformTime::Seconds();
		bIsIdle = false;
		OnStatusMessage.Execute(ResizedText);
	}
}

void SMoveableWindow::HandleWindowMoved(const TSharedRef<SWindow>& Window)
{
	if (!OnStatusMessage.IsBound())
	{
		return;
	}

	FVector2D CurrentPosition = Window->GetPositionInScreen();
	FVector2D NativePosition;
	if (TryGetNativeWindowPosition(Window->GetNativeWindow(), NativePosition))
	{
		CurrentPosition = NativePosition;
	}

	if (!bHasLastPosition || !CurrentPosition.Equals(LastUpdatedPosition))
	{
		LastUpdatedPosition = CurrentPosition;
		bHasLastPosition = true;
		LastMoveTimeSeconds = FPlatformTime::Seconds();
		bIsIdle = false;
		OnStatusMessage.Execute(PositionChangedText);
	}
}

EActiveTimerReturnType SMoveableWindow::HandleIdleTimer(double InCurrentTime, float InDeltaTime)
{
	if (!OnStatusMessage.IsBound())
	{
		return EActiveTimerReturnType::Stop;
	}

	(void)InCurrentTime;
	(void)InDeltaTime;

	const double NowSeconds = FPlatformTime::Seconds();
	if (!bIsIdle && (NowSeconds - LastMoveTimeSeconds) >= IdleThresholdSeconds)
	{
		bIsIdle = true;
		OnStatusMessage.Execute(IdleText);
	}

	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SMoveableWindow::MakeWindowTitleBar(const TSharedRef<SWindow>& Window, const TSharedPtr<SWidget>& CenterContent,
	EHorizontalAlignment TitleContentAlignment)
{
	if (TitleBarContent.IsValid())
	{
		return SWindow::MakeWindowTitleBar(Window, TitleBarContent, TitleBarContentAlignment);
	}

	return SWindow::MakeWindowTitleBar(Window, CenterContent, TitleContentAlignment);
}
