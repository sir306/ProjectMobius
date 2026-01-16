// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SMoveableWindow.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"

namespace
{
	// Resize overlay slots sit under the main content but still receive hit tests.
	constexpr int32 ResizeOverlayZOrder = -2;

	// Resize overlay helpers.
	bool IsLeftResizeZone(EWindowZone::Type Zone)
	{
		return Zone == EWindowZone::LeftBorder || Zone == EWindowZone::TopLeftBorder || Zone == EWindowZone::BottomLeftBorder;
	}

	bool IsRightResizeZone(EWindowZone::Type Zone)
	{
		return Zone == EWindowZone::RightBorder || Zone == EWindowZone::TopRightBorder || Zone == EWindowZone::BottomRightBorder;
	}

	bool IsTopResizeZone(EWindowZone::Type Zone)
	{
		return Zone == EWindowZone::TopBorder || Zone == EWindowZone::TopLeftBorder || Zone == EWindowZone::TopRightBorder;
	}

	bool IsBottomResizeZone(EWindowZone::Type Zone)
	{
		return Zone == EWindowZone::BottomBorder || Zone == EWindowZone::BottomLeftBorder || Zone == EWindowZone::BottomRightBorder;
	}

	EMouseCursor::Type GetResizeCursor(EWindowZone::Type Zone)
	{
		switch (Zone)
		{
		case EWindowZone::TopLeftBorder:
		case EWindowZone::BottomRightBorder:
			return EMouseCursor::ResizeSouthEast;
		case EWindowZone::TopRightBorder:
		case EWindowZone::BottomLeftBorder:
			return EMouseCursor::ResizeSouthWest;
		case EWindowZone::TopBorder:
		case EWindowZone::BottomBorder:
			return EMouseCursor::ResizeUpDown;
		case EWindowZone::LeftBorder:
		case EWindowZone::RightBorder:
			return EMouseCursor::ResizeLeftRight;
		default:
			return EMouseCursor::Default;
		}
	}

	/** Computes a resized window rect based on a drag delta while respecting the window size limits. */
	FSlateRect ComputeResizedRect(const FSlateRect& StartRect, const FVector2D& StartMouse, const FVector2D& CurrentMouse,
		EWindowZone::Type Zone, const FWindowSizeLimits& SizeLimits)
	{
		FSlateRect NewRect = StartRect;
		const FVector2D Delta = CurrentMouse - StartMouse;

		if (IsLeftResizeZone(Zone))
		{
			NewRect.Left += Delta.X;
		}
		if (IsRightResizeZone(Zone))
		{
			NewRect.Right += Delta.X;
		}
		if (IsTopResizeZone(Zone))
		{
			NewRect.Top += Delta.Y;
		}
		if (IsBottomResizeZone(Zone))
		{
			NewRect.Bottom += Delta.Y;
		}

		const float MinWidth = FMath::Max(1.0f, SizeLimits.GetMinWidth().Get(1.0f));
		const float MinHeight = FMath::Max(1.0f, SizeLimits.GetMinHeight().Get(1.0f));
		const float MaxWidth = SizeLimits.GetMaxWidth().Get(TNumericLimits<float>::Max());
		const float MaxHeight = SizeLimits.GetMaxHeight().Get(TNumericLimits<float>::Max());

		const float CurrentWidth = NewRect.Right - NewRect.Left;
		const float CurrentHeight = NewRect.Bottom - NewRect.Top;

		const float ClampedWidth = FMath::Clamp(CurrentWidth, MinWidth, MaxWidth);
		if (ClampedWidth != CurrentWidth)
		{
			if (IsLeftResizeZone(Zone) && !IsRightResizeZone(Zone))
			{
				NewRect.Left = NewRect.Right - ClampedWidth;
			}
			else
			{
				NewRect.Right = NewRect.Left + ClampedWidth;
			}
		}

		const float ClampedHeight = FMath::Clamp(CurrentHeight, MinHeight, MaxHeight);
		if (ClampedHeight != CurrentHeight)
		{
			if (IsTopResizeZone(Zone) && !IsBottomResizeZone(Zone))
			{
				NewRect.Top = NewRect.Bottom - ClampedHeight;
			}
			else
			{
				NewRect.Bottom = NewRect.Top + ClampedHeight;
			}
		}

		return NewRect;
	}

	// Lightweight hit target that resizes the window when dragged.
	class SMoveableWindowResizeHandle final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMoveableWindowResizeHandle)
			: _OwnerWindow()
			, _ResizeZone(EWindowZone::Unspecified)
		{
		}
			SLATE_ARGUMENT(TWeakPtr<SMoveableWindow>, OwnerWindow)
			SLATE_ARGUMENT(EWindowZone::Type, ResizeZone)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OwnerWindow = InArgs._OwnerWindow;
			ResizeZone = InArgs._ResizeZone;
			SetCanTick(false);

			ChildSlot
			[
				SNullWidget::NullWidget
			];
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
		{
			return FCursorReply::Cursor(GetResizeCursor(ResizeZone));
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}

			TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin();
			if (!Window.IsValid())
			{
				return FReply::Unhandled();
			}

			TSharedPtr<FGenericWindow> NativeWindow = Window->GetNativeWindow();
			if (NativeWindow.IsValid() && NativeWindow->IsMaximized())
			{
				return FReply::Unhandled();
			}

			bIsResizing = true;
			StartRect = Window->GetRectInScreen();
			StartMousePosition = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (!bIsResizing || !HasMouseCapture() || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Unhandled();
			}

			TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin();
			if (!Window.IsValid())
			{
				return FReply::Unhandled();
			}

			const FVector2D CurrentMousePosition = MouseEvent.GetScreenSpacePosition();
			const FWindowSizeLimits SizeLimits = Window->GetSizeLimits();
			const FSlateRect NewRect = ComputeResizedRect(StartRect, StartMousePosition, CurrentMousePosition, ResizeZone, SizeLimits);

			Window->ReshapeWindow(NewRect);
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsResizing)
			{
				bIsResizing = false;
				return FReply::Handled().ReleaseMouseCapture();
			}

			return FReply::Unhandled();
		}

		virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
		{
			bIsResizing = false;
			SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
		}

	private:
		TWeakPtr<SMoveableWindow> OwnerWindow;
		EWindowZone::Type ResizeZone = EWindowZone::Unspecified;
		bool bIsResizing = false;
		FSlateRect StartRect;
		FVector2D StartMousePosition = FVector2D::ZeroVector;
	};

	// Overlay that places resize handles along the window edges and corners.
	class SMoveableWindowResizeOverlay final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMoveableWindowResizeOverlay)
			: _OwnerWindow()
			, _ResizeBorder(FMargin(0.0f))
		{
		}
			SLATE_ARGUMENT(TWeakPtr<SMoveableWindow>, OwnerWindow)
			SLATE_ARGUMENT(FMargin, ResizeBorder)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OwnerWindow = InArgs._OwnerWindow;
			SetCanTick(false);
			SetVisibility(EVisibility::SelfHitTestInvisible);

			const FMargin Border = InArgs._ResizeBorder;
			const float Left = FMath::Max(0.0f, Border.Left);
			const float Right = FMath::Max(0.0f, Border.Right);
			const float Top = FMath::Max(0.0f, Border.Top);
			const float Bottom = FMath::Max(0.0f, Border.Bottom);

			TSharedRef<SOverlay> ResizeOverlay = SNew(SOverlay);

			if (Top > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.HeightOverride(Top)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::TopBorder)
					]
				];
			}

			if (Bottom > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.HeightOverride(Bottom)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::BottomBorder)
					]
				];
			}

			if (Left > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.WidthOverride(Left)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::LeftBorder)
					]
				];
			}

			if (Right > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.WidthOverride(Right)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::RightBorder)
					]
				];
			}

			if (Top > 0.0f && Left > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.WidthOverride(Left)
					.HeightOverride(Top)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::TopLeftBorder)
					]
				];
			}

			if (Top > 0.0f && Right > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.WidthOverride(Right)
					.HeightOverride(Top)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::TopRightBorder)
					]
				];
			}

			if (Bottom > 0.0f && Left > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.WidthOverride(Left)
					.HeightOverride(Bottom)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::BottomLeftBorder)
					]
				];
			}

			if (Bottom > 0.0f && Right > 0.0f)
			{
				ResizeOverlay->AddSlot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.WidthOverride(Right)
					.HeightOverride(Bottom)
					[
						SNew(SMoveableWindowResizeHandle)
						.OwnerWindow(OwnerWindow)
						.ResizeZone(EWindowZone::BottomRightBorder)
					]
				];
			}

			ChildSlot
			[
				ResizeOverlay
			];
		}

	private:
		TWeakPtr<SMoveableWindow> OwnerWindow;
	};
}


// SMoveableWindow construction and layout wiring.
void SMoveableWindow::Construct(const FArguments& InArgs)
{
	// Store whether we want a title bar before passing to parent
	const bool bWantTitleBar = InArgs._CreateTitleBar;
	const bool bUseCustomResizeOverlay = InArgs._SizingRule == ESizingRule::UserSized && !InArgs._UseOSWindowBorder;

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
	// Use fixed sizing when custom resize handles are active to avoid OS modal sizing.
	WindowArgs.SizingRule(bUseCustomResizeOverlay ? ESizingRule::FixedSize : InArgs._SizingRule);
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

	// We'll set the content after we wrap it with the custom title bar.

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
                        const FWindowStyle* WindowStyle = InArgs._Style
                                ? InArgs._Style
                                : &FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
                        const TAttribute<FText> TitleText = TAttribute<FText>::Create(
                                TAttribute<FText>::FGetter::CreateLambda([this]()
                                {
                                        return GetTitle();
                                }));
                        SAssignNew(TitleBarContent, SWindowTitleBarWidget)
                        .OwnerWindow(SharedThis(this))
                        .TitleText(TitleText)
                        .TitleTextStyle(&WindowStyle->TitleTextStyle)
                        .WindowStyle(WindowStyle)
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

		// Register the title bar interface for hit-testing and flash effects
		if (TitleBarContent.IsValid())
		{
			SetTitleBar(TitleBarContent->GetTitleBar());
		}

		// Set the title bar size so SWindow knows about it for layout calculations
		TitleBarSize = SWindowDefs::DefaultTitleBarSize;
	}
	else
	{
		// No title bar - just set the content directly
		SWindow::SetContent(InArgs._Content.Widget);
	}

	if (bUseCustomResizeOverlay && WindowOverlay.IsValid())
	{
		WindowOverlay->AddSlot(ResizeOverlayZOrder)
		[
			SNew(SMoveableWindowResizeOverlay)
			.OwnerWindow(SharedThis(this))
			.ResizeBorder(UserResizeBorder)
		];
	}
}


// Input overrides to prevent OS modal move/resize paths.
FReply SMoveableWindow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		TOptional<EMouseCursor::Type> CurrentCursor = this->GetCursor();
		if (!CurrentCursor.IsSet())
		{
			return FReply::Unhandled();
		}
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SMoveableWindow::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

FReply SMoveableWindow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

void SMoveableWindow::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SWindow::OnMouseCaptureLost(CaptureLostEvent);
}
