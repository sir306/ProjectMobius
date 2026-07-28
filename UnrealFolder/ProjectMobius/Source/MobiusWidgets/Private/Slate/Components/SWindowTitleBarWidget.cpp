#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SWindowTitleBar.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleDefaults.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// D8/Q3: the SWindow chrome is Slate, not UMG, so it can't ride the palette walker. Resolve the theme
	// subsystem from any live game world and poll it per-paint (matches the codebase's per-frame ImGui
	// StyleColors / combo OnGenerate idiom) so the title bar follows a live theme toggle.
	UUIThemeSubsystem* FindMobiusThemeSubsystem()
	{
		// Cached: the callers are per-paint colour lambdas, so the world-context walk below would otherwise
		// run several times a frame for the life of the window. A weak pointer self-clears when the
		// GameInstance goes (PIE stop, level travel), so the walk re-runs exactly when it has to.
		// Game thread only, which is where Slate paints.
		static TWeakObjectPtr<UUIThemeSubsystem> CachedTheme;
		if (UUIThemeSubsystem* Cached = CachedTheme.Get())
		{
			return Cached;
		}

		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UGameInstance* GameInstance = World->GetGameInstance())
				{
					if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
					{
						CachedTheme = Theme;
						return Theme;
					}
				}
			}
		}
		return nullptr;
	}

	FLinearColor MobiusThemeColor(EMobiusPaletteRole Role, const FLinearColor& Fallback)
	{
		if (const UUIThemeSubsystem* Theme = FindMobiusThemeSubsystem())
		{
			return Theme->GetPaletteColor(Role);
		}
		return Fallback;
	}

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
        WindowStyle = InArgs._WindowStyle
                ? *InArgs._WindowStyle
                : FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
        TitleTextStyle = InArgs._TitleTextStyle
                ? *InArgs._TitleTextStyle
                : FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window").TitleTextStyle;

        // D8/Q3: suppress the engine title-bar background brushes so our theme-polled SColorBlock behind
        // the bar is what shows — that way the title chrome follows a live theme toggle instead of staying
        // stuck on the (dark) style the window was created with.
        WindowStyle.ActiveTitleBrush = *FStyleDefaults::GetNoBrush();
        WindowStyle.InactiveTitleBrush = *FStyleDefaults::GetNoBrush();
        WindowStyle.FlashTitleBrush = *FStyleDefaults::GetNoBrush();

        SAssignNew(TitleTextBlock, STextBlock)
        .Text(InArgs._TitleText)
        .TextStyle(&TitleTextStyle)
        .ColorAndOpacity_Lambda([]()
        {
                return FSlateColor(MobiusThemeColor(EMobiusPaletteRole::TitlebarText, FLinearColor(0.55201f, 0.55201f, 0.55201f)));
        });

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
                SNew(SOverlay)
                + SOverlay::Slot()
                [
                        SNew(SColorBlock)
                        .Visibility(EVisibility::HitTestInvisible) // never intercept title-bar drag
                        .Color_Lambda([]()
                        {
                                return MobiusThemeColor(EMobiusPaletteRole::TitlebarBg, FLinearColor(0.03955f, 0.03955f, 0.03955f));
                        })
                ]
                + SOverlay::Slot()
                [
                        TitleBarWidget.ToSharedRef()
                ]
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
