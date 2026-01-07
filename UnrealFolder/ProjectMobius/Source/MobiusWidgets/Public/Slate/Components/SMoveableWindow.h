// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWindow.h"

/**
 * A window widget that extends SWindow with move/resize state tracking.
 * Use bIsMoving and bResizing to detect when the window is being moved or resized.
 * These can later be used to fire off delegates for custom behavior.
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
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	SMoveableWindow();
	virtual ~SMoveableWindow();

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	//~ SWidget overrides
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** True when the window is actively being moved */
	bool bIsMoving = false;

	/** True when the window is actively being resized */
	bool bResizing = false;

private:
	/** Called when the window finishes moving (bound to OnWindowMoved delegate) */
	void HandleWindowMoved(const TSharedRef<SWindow>& Window);

	/** Cached position from last tick for move detection */
	FVector2D LastPosition = FVector2D::ZeroVector;

	/** Cached size from last tick for resize detection */
	FVector2D LastSize = FVector2D::ZeroVector;

	/** Whether we've initialized the cached position/size */
	bool bHasInitializedCache = false;
};
