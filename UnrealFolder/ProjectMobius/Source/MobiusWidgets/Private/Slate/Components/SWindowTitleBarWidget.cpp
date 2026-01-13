#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SWindowTitleBar.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DECLARE_DELEGATE(FOnTitleBarClicked);

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
			SLATE_EVENT(FOnTitleBarClicked, OnClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OwnerWindow = InArgs._OwnerWindow;
			OnClicked = InArgs._OnClicked;
			UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::Construct Owner valid=%d"), OwnerWindow.IsValid());
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
					UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseButtonDown window valid"));
					//Window->SetTitleBarMouseHeld(true);
					
					// Record where we grabbed the window relative to its origin
					// Absolute Mouse Pos - Window Screen Pos = Offset
					CursorOffset = MouseEvent.GetScreenSpacePosition() - Window->GetPositionInScreen();
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseButtonDown owner invalid"));
				}

				if (OnClicked.IsBound())
				{
					OnClicked.Execute();
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
					UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseMove window valid"));
					FVector2D NewPosition = MouseEvent.GetScreenSpacePosition() - CursorOffset;
            
					Window->MoveWindowTo(NewPosition);
					UE_LOG(LogTemp, Log, TEXT("Should have moved"));
					return FReply::Handled();
                                        
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseMove owner invalid"));
				}
			}
			UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseMove doesn't has capture is: %s. left button down is: %s"), HasMouseCapture() ? TEXT("true") : TEXT("false"), MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ? TEXT("true") : TEXT("false"));
			return SWindowTitleBar::OnMouseMove(MyGeometry, MouseEvent);
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
				{
					UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseButtonUp window valid"));
					//Window->SetTitleBarMouseHeld(false);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseButtonUp owner invalid"));
				}
				return FReply::Handled().ReleaseMouseCapture();
			}

			return SWindowTitleBar::OnMouseButtonUp(MyGeometry, MouseEvent);
		}

		virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
		{
			if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
			{
				UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseCaptureLost window valid"));
				//Window->SetTitleBarMouseHeld(false);
			}

			if (!OwnerWindow.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseCaptureLost owner invalid"));
			}

			return SWindowTitleBar::OnMouseCaptureLost(CaptureLostEvent);
		}

	private:
		TWeakPtr<SMoveableWindow> OwnerWindow;
		FOnTitleBarClicked OnClicked;
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

	// Store the original title text for restoration after click
	OriginalTitleText = InArgs._TitleText.Get(FText::GetEmpty());

	// Create a custom text style with black text color
	FTextBlockStyle BlackTextStyle = *InArgs._TitleTextStyle;
	BlackTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::Blue);

	SAssignNew(TitleTextBlock, STextBlock)
	.Text(InArgs._TitleText)
	.TextStyle(&BlackTextStyle);

	UE_LOG(LogTemp, Warning, TEXT("SWindowTitleBarWidget::Construct OwnerWindowValid=%d"), InArgs._OwnerWindow.IsValid());

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
		.ShowAppIcon(InArgs._ShowAppIcon)
		.OnClicked(FOnTitleBarClicked::CreateSP(this, &SWindowTitleBarWidget::HandleTitleBarClicked));

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

void SWindowTitleBarWidget::HandleTitleBarClicked()
{
	if (bShowingClickText)
	{
		return;
	}

	bShowingClickText = true;

	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(FText::FromString(TEXT("CLICKED!")));
	}

	// Schedule restoration of the original title after 0.5 seconds using active timer
	RegisterActiveTimer(
		0.5f,
		FWidgetActiveTimerDelegate::CreateLambda([this](double, float) -> EActiveTimerReturnType
		{
			RestoreOriginalTitle();
			return EActiveTimerReturnType::Stop;
		}));
}

void SWindowTitleBarWidget::RestoreOriginalTitle()
{
	bShowingClickText = false;

	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(OriginalTitleText);
	}
}
