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
                                        Window->SetTitleBarMouseHeld(true);
                                }
                                else
                                {
                                        UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseButtonDown owner invalid"));
                                }
                        }

                        return SWindowTitleBar::OnMouseButtonDown(MyGeometry, MouseEvent);
                }

                virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
                {
                        if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
                        {
                                if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
                                {
                                        UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseButtonUp window valid"));
                                        Window->SetTitleBarMouseHeld(false);
                                }
                                else
                                {
                                        UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseButtonUp owner invalid"));
                                }
                        }

                        return SWindowTitleBar::OnMouseButtonUp(MyGeometry, MouseEvent);
                }

                virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
                {
                        if (TSharedPtr<SMoveableWindow> Window = OwnerWindow.Pin())
                        {
                                UE_LOG(LogTemp, Log, TEXT("SMoveableWindowTitleBar::OnMouseCaptureLost window valid"));
                                Window->SetTitleBarMouseHeld(false);
                        }

                        if (!OwnerWindow.IsValid())
                        {
                                UE_LOG(LogTemp, Warning, TEXT("SMoveableWindowTitleBar::OnMouseCaptureLost owner invalid"));
                        }

                        SWindowTitleBar::OnMouseCaptureLost(CaptureLostEvent);
                }

        private:
                TWeakPtr<SMoveableWindow> OwnerWindow;
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

        SAssignNew(TitleTextBlock, STextBlock)
                .Text(InArgs._TitleText)
                .TextStyle(InArgs._TitleTextStyle);

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
