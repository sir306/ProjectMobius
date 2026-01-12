// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/OldSMoveableWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
#include "Types/WidgetActiveTimerDelegate.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	constexpr float IdleTimerPeriodSeconds = 1.0f;
	constexpr double IdleThresholdSeconds = 1.0;
const FText PositionChangedText = FText::FromString(TEXT("OldSMoveableWindow: Position changed"));
const FText ResizedText = FText::FromString(TEXT("OldSMoveableWindow: Resized"));
const FText IdleText = FText::FromString(TEXT("OldSMoveableWindow: Idle"));
const FText MouseHeldText = FText::FromString(TEXT("OldSMoveableWindow: Mouse held"));

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

FOnMoveableWindowActivityChanged& SOldMoveableWindow::OnActivityChanged()
{
        static FOnMoveableWindowActivityChanged ActivityDelegate;
        return ActivityDelegate;
}

void SOldMoveableWindow::BroadcastActivityIfChanged(bool bIsActive)
{
        if (bHasActivityState && bWasActive == bIsActive)
        {
                return;
        }

        bHasActivityState = true;
        bWasActive = bIsActive;
	OnActivityChanged().Broadcast(bIsActive);
}

void SOldMoveableWindow::UpdateMouseHeldState(bool bIsHeld)
{
	if (bIsMouseHeld == bIsHeld)
	{
		return;
	}

	bIsMouseHeld = bIsHeld;
	if (bIsMouseHeld)
	{
                UE_LOG(LogTemp, Log, TEXT("OldSMoveableWindow: Mouse held"));
		if (OnStatusMessage.IsBound())
		{
			OnStatusMessage.Execute(MouseHeldText);
		}
        }
        else
        {
                UE_LOG(LogTemp, Log, TEXT("OldSMoveableWindow: Mouse released"));
                if (OnStatusMessage.IsBound())
                {
                        OnStatusMessage.Execute(IdleText);
                }
        }
}

void SOldMoveableWindow::SetTitleBarMouseHeld(bool bIsHeld)
{
	UpdateMouseHeldState(bIsHeld);
}

void SOldMoveableWindow::Construct(const FArguments& InArgs)
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
	
	
	FWindowTitleBarArgs Args(SharedThis(this));
	Args.CenterContent = TitleBarContent;
	Args.CenterContentAlignment = GetTitleAlignment();
	Args.CloseButtonToolTipText = CloseButtonToolTipText;

	TSharedRef<SWidget> TitleBarWidget = FSlateApplicationBase::Get().MakeWindowTitleBar(Args, TitleBar);
	SetTitleBar(TitleBar);

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

SetOnWindowMoved(FOnWindowMoved::CreateSP(this, &SOldMoveableWindow::HandleWindowMoved));
        LastMoveTimeSeconds = FPlatformTime::Seconds();
        bIsIdle = true;
        if (OnStatusMessage.IsBound())
        {
                OnStatusMessage.Execute(IdleText);
        }

        if (!bIdleTimerRegistered)
        {
                RegisterActiveTimer(
                        IdleTimerPeriodSeconds,
FWidgetActiveTimerDelegate::CreateSP(this, &SOldMoveableWindow::HandleIdleTimer));
                bIdleTimerRegistered = true;
        }
	
	// Default window DOESN'T TICK so we need to override that behavior after calling the parent construct
	SetCanTick(true);
	
}

void SOldMoveableWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
        SWindow::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

        if (FSlateApplication::IsInitialized())
        {
                const bool bLeftMouseButtonDown = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
                UpdateMouseHeldState(bLeftMouseButtonDown);
        }

        const FVector2D CurrentSize = FVector2D(GetClientSizeInScreen());       
        if (!bHasLastSize || !CurrentSize.Equals(LastUpdatedSize))
        {
		LastUpdatedSize = CurrentSize;
		bHasLastSize = true;
		LastMoveTimeSeconds = FPlatformTime::Seconds();
		bIsIdle = false;
		BroadcastActivityIfChanged(true);
		if (OnStatusMessage.IsBound())
		{
			OnStatusMessage.Execute(bIsMouseHeld ? MouseHeldText : ResizedText);
		}
	}
}

void SOldMoveableWindow::HandleWindowMoved(const TSharedRef<SWindow>& Window)
{
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
		BroadcastActivityIfChanged(true);
		if (OnStatusMessage.IsBound())
		{
			OnStatusMessage.Execute(bIsMouseHeld ? MouseHeldText : PositionChangedText);
		}
	}
}

EActiveTimerReturnType SOldMoveableWindow::HandleIdleTimer(double InCurrentTime, float InDeltaTime)
{
        (void)InCurrentTime;
        (void)InDeltaTime;

	const double NowSeconds = FPlatformTime::Seconds();
	if (!bIsIdle && (NowSeconds - LastMoveTimeSeconds) >= IdleThresholdSeconds)
	{
		bIsIdle = true;
		BroadcastActivityIfChanged(false);
		if (OnStatusMessage.IsBound())
		{
			OnStatusMessage.Execute(bIsMouseHeld ? MouseHeldText : IdleText);
		}
	}

        return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SOldMoveableWindow::MakeWindowTitleBar(const TSharedRef<SWindow>& Window, const TSharedPtr<SWidget>& CenterContent,
        EHorizontalAlignment TitleContentAlignment)
{
	if (TitleBarContent.IsValid())
	{
		return SWindow::MakeWindowTitleBar(Window, TitleBarContent, TitleBarContentAlignment);
	}

	return SWindow::MakeWindowTitleBar(Window, CenterContent, TitleContentAlignment);
}
