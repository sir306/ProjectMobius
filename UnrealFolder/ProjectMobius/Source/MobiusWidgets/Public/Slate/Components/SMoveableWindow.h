// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Widgets/SWindow.h"

class SWindowTitleBarWidget;

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
	#if PLATFORM_MAC
		  // Mac handles window movement/resizing on separate thread (won't block game thread)
		  // Use native OS window chrome to avoid overlapping with macOS window controls
		  , _UseOSWindowBorder(true)
#else
		  // Windows/other platforms use custom title bar and resize overlay
		  // to avoid OS modal sizing that blocks the game thread
		  , _UseOSWindowBorder(false)
#endif
			  , _HasCloseButton(true)
			  , _SupportsMaximize(true)
			  , _SupportsMinimize(true)
			  , _ShouldPreserveAspectRatio(false)
#if PLATFORM_MAC
			  , _CreateTitleBar(false)
#else
			  , _CreateTitleBar(true)
#endif
			  , _SaneWindowPlacement(true)
			  , _LayoutBorder(FMargin(5, 5, 5, 5))
			  , _UserResizeBorder(FMargin(5, 5, 5, 5))
			  , _TitleBarContent()
			  , _TitleBarContentAlignment(HAlign_Fill)
			  , _WindowPanelContent()
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
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

private:
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	//SLATECORE_API virtual FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const override;
	
	TSharedPtr<SWindowTitleBarWidget> TitleBarContent;
	EHorizontalAlignment TitleBarContentAlignment = HAlign_Fill;
};
