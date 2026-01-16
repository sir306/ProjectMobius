#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Framework/Application/SWindowTitleBar.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	class SMoveableWindowTitleBar final : public SWindowTitleBar
	{
	public:
		SLATE_BEGIN_ARGS(SMoveableWindowTitleBar)
				: _Style(&FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window"))
				  , _ShowAppIcon(false)
				  , _OwnerWindow()
				  , _TitleBarContent(SNullWidget::NullWidget)
				  , _TitleAlignment(HAlign_Fill)
			{
			}
			SLATE_STYLE_ARGUMENT(FWindowStyle, Style)
			SLATE_ARGUMENT(bool, ShowAppIcon)
			SLATE_ARGUMENT(TSharedPtr<SMoveableWindow>, OwnerWindow)
			SLATE_ARGUMENT(TSharedRef<SWidget>, TitleBarContent)
			SLATE_ARGUMENT(EHorizontalAlignment, TitleAlignment)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OwnerWindow = InArgs._OwnerWindow;
			if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
			{
				SWindowTitleBar::Construct(
					SWindowTitleBar::FArguments()
					.Style(InArgs._Style)
					.ShowAppIcon(InArgs._ShowAppIcon),
					Window.ToSharedRef(),
					InArgs._TitleBarContent,
					InArgs._TitleAlignment);
			}
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
				{
					// Record where we grabbed the window relative to its origin
					// Absolute Mouse Pos - Window Screen Pos = Offset
					CursorOffset = MouseEvent.GetScreenSpacePosition() - Window->GetPositionInScreen();
				}

				return FReply::Handled().CaptureMouse(SharedThis(this));
			}

			// TODO: Investigate further why calling the parent implementation here seems to cause issues.
			// The way Title bar handles this mousebutton down event seemed to be the cause of my simulation pauses/stutters and hangs.
			// Will need to investigate further later. As our bypass logic seems to work fine for now. and doesn't cause any issues.
			return SWindowTitleBar::OnMouseButtonDown(MyGeometry, MouseEvent);
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (HasMouseCapture() && MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
				{
					FVector2D NewPosition = MouseEvent.GetScreenSpacePosition() - CursorOffset;
            
					Window->MoveWindowTo(NewPosition);
					return FReply::Handled();
                                        
				}
			}
			return SWindowTitleBar::OnMouseMove(MyGeometry, MouseEvent);
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
		{
			return FCursorReply::Unhandled();
		}

		virtual EWindowZone::Type GetWindowZoneOverride() const override
		{
			// Avoid OS title-bar hit testing so we don't enter modal move/resize paths.
			// NOTE: There is still a subtle edge hit region around the title bar to investigate later.
			return EWindowZone::ClientArea;
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				return FReply::Handled().ReleaseMouseCapture();
			}

			return SWindowTitleBar::OnMouseButtonUp(MyGeometry, MouseEvent);
		}

		virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
		{
			return SWindowTitleBar::OnMouseCaptureLost(CaptureLostEvent);
		}

	private:
		TWeakPtr<SMoveableWindow> OwnerWindow;
		FVector2D CursorOffset; // Distance from Top-Left of window to Mouse Cursor
	};
}

SWindowTitleBarWidget::SWindowTitleBarWidget()
{
}

SWindowTitleBarWidget::~SWindowTitleBarWidget()
{
}

void SWindowTitleBarWidget::Construct(const FArguments& InArgs)
{
	WindowStyle = *InArgs._WindowStyle;
	// Override title bar background to red for visibility testing
	WindowStyle.BackgroundBrush.TintColor = FSlateColor(FLinearColor::Red);

	// Create a custom text style with black text color
	FTextBlockStyle BlackTextStyle = *InArgs._TitleTextStyle;
	BlackTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::Blue);

	SAssignNew(TitleTextBlock, STextBlock)
	.Text(InArgs._TitleText)
	.TextStyle(&BlackTextStyle);

	if (!InArgs._OwnerWindow.IsValid())
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];
		return;
	}

	TitleBarWidget = SNew(SMoveableWindowTitleBar)
		.OwnerWindow(InArgs._OwnerWindow)
		.TitleBarContent(TitleTextBlock.ToSharedRef())
		.TitleAlignment(InArgs._TitleAlignment)
		.Style(&WindowStyle)
		.ShowAppIcon(InArgs._ShowAppIcon);

	ChildSlot
	[
		TitleBarWidget.ToSharedRef()
	];
}

void SWindowTitleBarWidget::SetTitleText(const FText& InTitleText)
{
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(InTitleText);
	}
}

TSharedPtr<IWindowTitleBar> SWindowTitleBarWidget::GetTitleBar() const
{
	return TitleBarWidget;
}
