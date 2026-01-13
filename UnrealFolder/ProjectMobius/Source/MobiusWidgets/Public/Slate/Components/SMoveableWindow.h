// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Widgets/SWindow.h"

class SWindowTitleBarWidget;

/** Emits status text for the owning window. */
DECLARE_DELEGATE_OneParam(FOnMoveableWindowStatusMessage, const FText&);
/** Emits move/resize activity changes for any moveable window. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMoveableWindowActivityChanged, bool);

/**
 * A window widget that extends SWindow.
 * This class forwards all SWindow arguments for convenient usage.
 */
class MOBIUSWIDGETS_API SMoveableWindow : public SWindow
{
public:
	// Forward all SWindow arguments so SMoveableWindow can be used exactly like SWindow
	SLATE_BEGIN_ARGS(SMoveableWindow)
			: _Type(EWindowType::Normal)
			  , _Style(&FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window"))
			  , _Title()
			  , _bDragAnywhere(false)
			  , _AutoCenter(EAutoCenter::PreferredWorkArea)
			  , _ScreenPosition(FVector2D::ZeroVector)
			  , _ClientSize(FVector2D::ZeroVector)
			  , _SupportsTransparency(EWindowTransparency::None)
			  , _InitialOpacity(1.0f)
			  , _IsInitiallyMaximized(false)
			  , _IsInitiallyMinimized(false)
			  , _SizingRule(ESizingRule::UserSized)
			  , _IsPopupWindow(false)
			  , _IsTopmostWindow(false)
			  , _FocusWhenFirstShown(true)
			  , _ActivationPolicy(EWindowActivationPolicy::Always)
			  , _UseOSWindowBorder(false)
			  , _HasCloseButton(true)
			  , _SupportsMaximize(true)
			  , _SupportsMinimize(true)
			  , _ShouldPreserveAspectRatio(false)
			  , _CreateTitleBar(true)
			  , _SaneWindowPlacement(true)
			  , _LayoutBorder(FMargin(5, 5, 5, 5))
			  , _UserResizeBorder(FMargin(5, 5, 5, 5))
			  , _TitleBarContent()
			  , _TitleBarContentAlignment(HAlign_Fill)
			  , _WindowPanelContent()
			  , _OnStatusMessage()
		{}
		SLATE_ARGUMENT(EWindowType, Type)
		SLATE_STYLE_ARGUMENT(FWindowStyle, Style)
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ARGUMENT(bool, bDragAnywhere)
		SLATE_ARGUMENT(EAutoCenter, AutoCenter)
		SLATE_ARGUMENT(FVector2D, ScreenPosition)
		SLATE_ARGUMENT(FVector2D, ClientSize)
		SLATE_ARGUMENT(EWindowTransparency, SupportsTransparency)
		SLATE_ARGUMENT(float, InitialOpacity)
		SLATE_ARGUMENT(bool, IsInitiallyMaximized)
		SLATE_ARGUMENT(bool, IsInitiallyMinimized)
		SLATE_ARGUMENT(ESizingRule, SizingRule)
		SLATE_ARGUMENT(bool, IsPopupWindow)
		SLATE_ARGUMENT(bool, IsTopmostWindow)
		SLATE_ARGUMENT(bool, FocusWhenFirstShown)
		SLATE_ARGUMENT(EWindowActivationPolicy, ActivationPolicy)
		SLATE_ARGUMENT(bool, UseOSWindowBorder)
		SLATE_ARGUMENT(bool, HasCloseButton)
		SLATE_ARGUMENT(bool, SupportsMaximize)
		SLATE_ARGUMENT(bool, SupportsMinimize)
		SLATE_ARGUMENT(bool, ShouldPreserveAspectRatio)
		SLATE_ARGUMENT(bool, CreateTitleBar)
		SLATE_ARGUMENT(bool, SaneWindowPlacement)
		SLATE_ARGUMENT(FMargin, LayoutBorder)
		SLATE_ARGUMENT(FMargin, UserResizeBorder)
		/** Optional widget to display in the title bar's center slot. */
		SLATE_ARGUMENT(TSharedPtr<SWidget>, TitleBarContent)
		/** Alignment for the custom title bar content. */
		SLATE_ARGUMENT(EHorizontalAlignment, TitleBarContentAlignment)
		
		SLATE_ARGUMENT(TSharedPtr<SWidget>, WindowPanelContent)
		/** Status message emitter for this window. */
		SLATE_EVENT(FOnMoveableWindowStatusMessage, OnStatusMessage)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Update title bar mouse held state. */
	void SetTitleBarMouseHeld(bool bIsHeld);
	/** Global activity signal fired when a window starts or stops moving/resizing. */
	static FOnMoveableWindowActivityChanged& OnActivityChanged();
	
	// Override OnPaint to catch resize events even when Tick is blocked
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	
private:
	
	void HandleWindowMoved(const TSharedRef<SWindow>& Window);
	EActiveTimerReturnType HandleIdleTimer(double InCurrentTime, float InDeltaTime);
	void BroadcastActivityIfChanged(bool bIsActive);
	void UpdateMouseHeldState(bool bIsHeld);

	FOnMoveableWindowStatusMessage OnStatusMessage;
	TSharedPtr<SWindowTitleBarWidget> TitleBarContent;
	EHorizontalAlignment TitleBarContentAlignment = HAlign_Fill;

	FVector2D LastUpdatedPosition = FVector2D::ZeroVector;
	FVector2D LastUpdatedSize = FVector2D::ZeroVector;
	double LastMoveTimeSeconds = 0.0;
	bool bHasLastPosition = false;
	bool bHasLastSize = false;
	bool bIsIdle = false;
	bool bIdleTimerRegistered = false;
	bool bHasActivityState = false;
	bool bWasActive = false;
	bool bIsMouseHeld = false;
	
	// Mutable so we can update it inside the const OnPaint function
	mutable FVector2D LastPaintSize;
};
