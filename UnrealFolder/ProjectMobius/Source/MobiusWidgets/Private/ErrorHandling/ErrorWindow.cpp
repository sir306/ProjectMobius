// Fill out your copyright notice in the Description page of Project Settings.


#include "ErrorHandling/ErrorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowContentPanel.h"
#include "Slate/Components/MobiusWindowButtonStyle.h"
#include "Style/MobiusStyle.h"
#include "Styling/CoreStyle.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/UserInterfaceSettings.h"   // P17: ApplicationScale, the one UI-scale this window can read
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// The error window is Slate chrome (not UMG), so it cannot ride the palette walk. Mirror the
	// SWindowTitleBarWidget idiom: resolve the theme subsystem from any live game world and poll it
	// per-paint via a colour lambda, so the body surface follows a live theme toggle.
	UUIThemeSubsystem* FindThemeSubsystemForErrorWindow()
	{
		// Cached for the same reason as SWindowTitleBarWidget's copy: the callers are per-paint colour
		// lambdas. The weak pointer self-clears with the GameInstance, so the walk re-runs only then.
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

	FLinearColor PollErrorWindowColor(EMobiusPaletteRole Role, const FLinearColor& Fallback)
	{
		if (const UUIThemeSubsystem* Theme = FindThemeSubsystemForErrorWindow())
		{
			return Theme->GetPaletteColor(Role);
		}
		return Fallback;
	}

	/** Shorthand for the common case: fall back to the role's LIGHT value when there is no subsystem yet. */
	FLinearColor PollErrorWindowColor(EMobiusPaletteRole Role)
	{
		return PollErrorWindowColor(Role, MobiusThemePalette::Color(Role, /*bLight=*/true));
	}

	/**
	 * A19(d): window chrome for the error window. Themed like every other Mobius window, with the A18
	 * danger × layered on top. Falls back to the legacy red-title style only when there is no game
	 * instance yet — GetThemedWindowStyle lives on a GameInstanceSubsystem, so it can genuinely be absent.
	 */
	FWindowStyle ResolveErrorWindowChrome()
	{
		UUIThemeSubsystem* Theme = FindThemeSubsystemForErrorWindow();
		// A19: the fallback was "Mobius.Window.Error", the legacy red-title style. That style is now deleted
		// (pixel-verified inert — SWindowTitleBarWidget replaces the title brushes with NoBrush and paints
		// its own polled SColorBlock), so fall back to FCoreStyle's plain window instead.
		FWindowStyle Chrome = Theme
			? Theme->GetThemedWindowStyle()
			: FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");

		// A18: the title-bar × reads as the destructive affordance it is.
		MobiusWindowButtonStyle::ApplyDangerCloseGlyph(Chrome, Theme);
		return Chrome;
	}
}


SErrorWindowWidget::SErrorWindowWidget()
{
}

SErrorWindowWidget::~SErrorWindowWidget()
{
	CloseErrorWindow();
}

void SErrorWindowWidget::Construct(const FArguments& InArgs)
{
	OpenErrorWindow();
}

int32 SErrorWindowWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                                bParentEnabled);
}

void SErrorWindowWidget::SetTitleBarText(const FText& TitleText)
{
	if (ErrorWindowPtr.IsValid())
	{
		ErrorWindowPtr->SetTitle(TitleText);
	}
}

void SErrorWindowWidget::SetErrorTitleText(const FText& TitleText)
{
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetTitleText(TitleText);
	}
}

void SErrorWindowWidget::SetErrorMessageText(const FText& MessageText)
{
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetMessageText(MessageText);
	}
}

void SErrorWindowWidget::SetErrorLocationText(const FText& LocationText)        
{
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetLocationText(LocationText);
	}
}

void SErrorWindowWidget::ShowErrorWindow()
{
	OpenErrorWindow();

	if (ErrorWindowPtr.IsValid())
	{
		ErrorWindowPtr->BringToFront(true);
	}
}

void SErrorWindowWidget::OffsetWindowPosition(const FVector2D& Delta)
{
	if (ErrorWindowPtr.IsValid())
	{
		// GetPositionInScreen returns FDeprecateVector2DResult and MoveWindowTo takes
		// FDeprecateVector2DParameter; go through FVector2D explicitly so the arithmetic is unambiguous.
		const FVector2D Current(ErrorWindowPtr->GetPositionInScreen());
		ErrorWindowPtr->MoveWindowTo(Current + Delta);
	}
}

void SErrorWindowWidget::OpenErrorWindow()
{
        if (ErrorWindowPtr.IsValid() || !FSlateApplication::IsInitialized())
        {
                return;
        }

        // Theme + typography come from the shared style set. We copy the ramp styles into members and
        // bump the font size LOCALLY: this native SMoveableWindow gets no UMG UI-scale/DPI multiplier,
        // so the shared 12/10 sizes render tiny here. Members (not stack-locals) because the content
        // panel keeps the style pointers past this scope. Colour is left untouched (the shared ramp is
        // retinted per theme in ApplySharedStyles at open time), so the polled colour is preserved.
        // A19(d): the window CHROME now comes from the same source as every other Mobius window
        // (GetThemedWindowStyle) instead of the legacy "Mobius.Window.Error" red-title style, so the frame,
        // title bar and title text follow the theme rather than a baked red. The old style is kept as the
        // fallback for the pre-game-instance case only, where there is no subsystem to ask.
        ErrorWindowStyle = ResolveErrorWindowChrome();
        TitleTextStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Title");
        MessageTextStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Body");
        LocationTextStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Source");
        // P17 (owner-reported 2026-08-13: "warning/error popup text is too small to read").
        //
        // MEASURED CAUSE, and it is not just "the numbers are low": this window is the only surface in
        // the app that ignores the user's own UI-scale preference. UUserProjectSettings::UIScaleFactor
        // (0.5-2.0, an accessibility control) is pushed by ApplyUIScaleFactorToSlate into
        // UUserInterfaceSettings::ApplicationScale - which scales UMG/viewport widgets. This is a native
        // SMoveableWindow, so it never saw it, and a user who had already turned the whole app up to 1.5
        // still got 13pt here. That is why the fix reads the scale rather than only raising literals:
        // raising literals alone would leave the same class of complaint at every non-default scale.
        //
        // Read via GetDefault<UUserInterfaceSettings>() rather than by finding the settings object: this
        // window can open BEFORE there is a game instance (see ResolveErrorWindowChrome's fallback), and
        // that path has nothing to ask. ApplicationScale is where the factor has already been pushed, so
        // it is the single source of truth for "what scale is the rest of the app at" and it is always
        // readable. Pre-push it is the class default 1.0, i.e. the base sizes below.
        //
        // Bases are also raised (16/13/12 -> 18/15/13) so the DEFAULT 1.0 case is legible, which is the
        // case the owner reported. Body copy auto-wraps (SWindowContentPanel -> FieldAutoWrapText(true))
        // and the window is UserSized, so a larger face re-wraps rather than clipping.
        const float UiScale = FMath::Clamp(GetDefault<UUserInterfaceSettings>()->ApplicationScale, 0.5f, 2.0f);
        TitleTextStyle.Font.Size = FMath::RoundToFloat(18.0f * UiScale);
        MessageTextStyle.Font.Size = FMath::RoundToFloat(15.0f * UiScale);
        LocationTextStyle.Font.Size = FMath::RoundToFloat(13.0f * UiScale);

        // Themed Close button (member: SButton caches raw pointers into the style's brushes).
        CloseButtonStyle = MobiusWindowButtonStyle::MakeWindowButtonStyle(FindThemeSubsystemForErrorWindow());

        const FMargin WindowPadding = FMobiusStyle::Get().GetMargin("Mobius.Padding.Window");

        // Body surface: a flat WhiteBrush tinted per-theme via a poll lambda (RibbonBg = panel surface) so
        // the dialog follows the theme instead of the fixed engine-dark ToolPanel.GroupBorder. Body TEXT is
        // themed through the shared ramp (retinted per theme in ApplySharedStyles) at open time.
        TSharedRef<SWidget> BodyPanel = SNew(SBorder)
                .Padding(FMargin(0.0f))
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([]()
                {
                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::RibbonBg, FLinearColor(0.03955f, 0.03955f, 0.03955f)));
                })
                [
                        SNew(SVerticalBox)
                        // Top accent — the ONLY live emphasis cue (the old red title chrome is dead:
                        // SWindowTitleBarWidget forces title brushes to NoBrush and polls TitlebarText for
                        // the title colour). Driven by severity so a Warning-level "Performance Notice" is
                        // NOT painted error-red: Error/Fatal = red, Warning = amber, Info = accent (blue).
                        //
                        // A19(a): all three were hard-coded literals. They are now palette roles, polled
                        // per paint like the rest of this window, so the rule follows a live theme toggle
                        // and there is exactly ONE red in the app. Measured on the body surface (RibbonBg):
                        // DangerText 5.2:1 light / 4.2:1 dark, WarningText 4.8:1 / 5.5:1, Accent 5.2:1 /
                        // 4.0:1. The literal amber this replaces measured 1.96:1 in light — below even the
                        // 3:1 UI-component bar, which is why WarningText had to be added to the palette.
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                                SNew(SColorBlock)
                                .Color_Lambda([Severity = CurrentSeverity]() -> FLinearColor
                                {
                                        switch (*Severity)
                                        {
                                        case EMobiusErrorSeverity::Warning:
                                                return PollErrorWindowColor(EMobiusPaletteRole::WarningText);
                                        case EMobiusErrorSeverity::Info:
                                                return PollErrorWindowColor(EMobiusPaletteRole::Accent);
                                        case EMobiusErrorSeverity::Error:
                                        case EMobiusErrorSeverity::Fatal:
                                        default:
                                                return PollErrorWindowColor(EMobiusPaletteRole::DangerText);
                                        }
                                })
                                .Size(FVector2D(1.0f, 3.0f))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        .Padding(WindowPadding)
                        [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .VAlign(VAlign_Fill)
                                [
                                        SAssignNew(ContentPanel, SWindowContentPanel)
                                        .TitleText(FText::FromString("Error Window (Test)"))
                                        .MessageText(FText::FromString("This is a test popup for error handling."))
                                        .LocationText(FText::GetEmpty())
                                        .TitleTextStyle(&TitleTextStyle)
                                        .MessageTextStyle(&MessageTextStyle)
                                        .LocationTextStyle(&LocationTextStyle)
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .HAlign(HAlign_Right)
                                .Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
                                [
                                        SAssignNew(CloseButton, SButton)
                                        .ButtonStyle(&CloseButtonStyle)
                                        .OnClicked(this, &SErrorWindowWidget::HandleCloseClicked)
                                        [
                                                // Explicit label so the text colour follows a live theme toggle
                                                // (same poll idiom as the body). Background brushes are stamped
                                                // for the current theme at construct.
                                                SNew(STextBlock)
                                                .Text(FText::FromString("Close"))
                                                .Justification(ETextJustify::Center)
                                                .ColorAndOpacity_Lambda([]()
                                                {
                                                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::ButtonText, MobiusThemePalette::Color(EMobiusPaletteRole::ButtonText, /*bLight=*/true)));
                                                })
                                        ]
                                ]
                        ]
                ];

        // #11b: the title bar reads the same as the body (TitlebarBg == RibbonBg by palette design). Wrap
        // the body in a 1px WindowBorder-coloured frame so the two surfaces read as separate. WhiteBrush
        // (stable engine pointer, like the body) + FMargin(1) gives the 1px edge; colour polled live.
        TSharedRef<SWidget> WindowPanel = SNew(SBorder)
                .Padding(FMargin(1.0f))
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([]()
                {
                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::WindowBorder, MobiusThemePalette::Color(EMobiusPaletteRole::WindowBorder, /*bLight=*/true)));
                })
                [
                        BodyPanel
                ];

        // Live re-theme for the Close button, event-driven off OnThemeChangedNative. The bind normally
        // succeeds right here, since the window only opens once there is a game instance to report an error
        // from. If it does not, fall back to a 2 Hz bootstrap timer that stops the moment it binds.
        //
        // The timer has to be registered on CloseButton, not on this controller: this widget is never
        // slotted under any window, so it is never painted, and SWidget::Paint is what pumps active timers.
        // CloseButton lives inside WindowPanel, which is painted.
        if (!TryBindThemeChanged() && CloseButton.IsValid())
        {
                CloseButton->RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SErrorWindowWidget::EnsureThemeBinding));
        }

        SAssignNew(ErrorWindowPtr, SMoveableWindow)
                .Title(FText::FromString("Error Window"))
                .Style(&ErrorWindowStyle)
                .SupportsMaximize(false)
                .SupportsMinimize(false)
                .IsTopmostWindow(true)
                .SizingRule(ESizingRule::UserSized)
                // P17: the box has to grow with the type or the larger face just re-wraps into the same
                // 520px and eats the vertical slack. Base widened 520 -> 560 (the owner's ~1:1 capture
                // already wrapped the two body paragraphs to five lines at 520, with ~130px unused BELOW
                // the rule - so width, not height, was the binding constraint), then scaled by the same
                // UiScale as the fonts so the two cannot drift apart. Still UserSized: the user can
                // always resize, this only sets where it opens.
                .ClientSize(FVector2D(560.0f, 300.0f) * UiScale)
                .AutoCenter(EAutoCenter::PreferredWorkArea)
                .HasCloseButton(true)
                .WindowPanelContent(WindowPanel);

        // Converge EVERY close route on one teardown (see HandleWindowClosed). AddSP rather than a raw
        // binding or a lambda capturing `this`: the destructor's CloseErrorWindow() destroys the window
        // while this widget is already being torn down, and the weak pin makes the callback a no-op then
        // instead of re-entering a half-destructed object. A fresh SMoveableWindow is built per open, so
        // the binding dies with the window and there is no handle to track.
        ErrorWindowPtr->GetOnWindowClosedEvent().AddSP(this, &SErrorWindowWidget::HandleWindowClosed);

        FSlateApplication::Get().AddWindow(ErrorWindowPtr.ToSharedRef());

        SetErrorLocationText(FText::GetEmpty());
}

void SErrorWindowWidget::CloseErrorWindow()
{
	// Clear the member BEFORE asking for the destroy, not after. FSlateApplication::RequestDestroyWindow
	// runs DestroyWindowsImmediately() inline (SlateApplication.cpp:2351), so NotifyWindowBeingDestroyed —
	// and therefore HandleWindowClosed below — fires on THIS call stack, before the old
	// `ErrorWindowPtr.Reset()` after the call would have run. SWindow::RequestDestroyWindow's "not
	// destroyed immediately... queue for destruction on next Tick" comment is stale; on the game thread it
	// is synchronous. Handing the destroy a local keeps the window alive across the call.
	const TSharedPtr<SMoveableWindow> WindowToDestroy = ErrorWindowPtr;
	ReleaseWindowState();

	if (WindowToDestroy.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RequestDestroyWindow(WindowToDestroy.ToSharedRef());
	}
}

void SErrorWindowWidget::ReleaseWindowState()
{
	// Unconditional: the binding outlives the window pointer, and OpenErrorWindow re-binds on reopen.
	UnbindThemeChanged();

	// ContentPanel and CloseButton live inside the destroyed window's tree and are rebuilt by
	// OpenErrorWindow, so holding them past the close would only keep dead widgets alive and let the
	// SetError*Text setters silently write into a tree nobody paints.
	ErrorWindowPtr.Reset();
	ContentPanel.Reset();
	CloseButton.Reset();

	// Back to the out-of-range sentinel so a REOPEN re-pushes unconditionally. Without this, reopening in
	// the same theme early-outs of ApplyThemeToWindow and the freshly rebuilt ContentPanel/CloseButton
	// would rely entirely on construct having got it right.
	LastAppliedTheme = static_cast<EMobiusUITheme>(0xFF);
}

void SErrorWindowWidget::HandleWindowClosed(const TSharedRef<SWindow>& InWindow)
{
	// Teardown ONLY — deliberately NOT CloseErrorWindow(). By the time this fires the window is already
	// inside FSlateApplication::PrivateDestroyWindow and has been popped from WindowDestroyQueue
	// (SlateApplication.cpp:3114), so a second RequestDestroyWindow would re-queue it and the nested
	// DestroyWindowsImmediately would re-run NotifyWindowBeingDestroyed, Renderer->OnWindowDestroyed and
	// NativeWindow->Destroy on the same window.
	//
	// This is the same "single close policy" LegalNoticeDialog enforces, but through the event rather than
	// SetRequestDestroyWindowOverride: that override exists there to VETO/redirect a close (dismiss vs
	// quit), which this window has no need to do, and it only sees callers that go through
	// SWindow::RequestDestroyWindow — OnWindowClosed also catches a direct PrivateDestroyWindow (Slate
	// tearing down a parent window).
	ReleaseWindowState();
}

void SErrorWindowWidget::SetSeverity(EMobiusErrorSeverity InSeverity)
{
	*CurrentSeverity = InSeverity;
	// The accent bar reads the shared severity via a bound colour attribute; invalidate paint so a
	// reused window (shown again for a different-severity error without being rebuilt) repaints it.
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SErrorWindowWidget::HandleCloseClicked()
{
	CloseErrorWindow();
	return FReply::Handled();
}

void SErrorWindowWidget::ApplyThemeToWindow()
{
	// Only rebuild when the theme actually changed (sentinel forces the first apply). Everything touched
	// here is SNAPSHOTTED at open, so without this pass a theme toggle with the window up repaints only
	// the parts that poll (body surface, frame, Close label) and leaves the rest at the open-time theme.
	const UUIThemeSubsystem* ThemeSubsystem = FindThemeSubsystemForErrorWindow();
	if (!ThemeSubsystem)
	{
		return;
	}

	const EMobiusUITheme CurrentThemeValue = ThemeSubsystem->GetTheme();
	if (CurrentThemeValue == LastAppliedTheme)
	{
		return;
	}
	LastAppliedTheme = CurrentThemeValue;

	// 1. Close button. Built at construct and therefore frozen at the construct-time theme; a reused
	//    window shown in another theme would otherwise keep the wrong button background (the LABEL
	//    already polls ButtonText live, so only the brushes are at risk).
	CloseButtonStyle = MobiusWindowButtonStyle::MakeWindowButtonStyle(ThemeSubsystem);
	if (CloseButton.IsValid())
	{
		CloseButton->SetButtonStyle(&CloseButtonStyle);
	}

	// 2. A19: the three body text colours. These are the half of the window that did NOT follow a live
	//    toggle. OpenErrorWindow copies the shared ramp into members and locally bumps the font sizes
	//    (this native window gets no UMG DPI multiplier), and ApplySharedStyles retints the SHARED style
	//    objects — which these copies no longer track. Worse, STextBlock stores its FTextBlockStyle by
	//    value, so even re-stamping the members would not reach the widgets.
	//
	//    So push COLOUR straight through, using the same roles ApplySharedStyles uses for the same keys
	//    (Title/Body -> LabelText, Source -> SublabelText) so the two writers can never disagree. Colour
	//    only, deliberately: a whole-style re-apply would undo SFieldAndTitleText's shrink-to-fit font
	//    size and its Mono typeface. Keep the members in step too, so the next open starts correct.
	const FSlateColor LabelColor(ThemeSubsystem->GetPaletteColor(EMobiusPaletteRole::LabelText));
	const FSlateColor SourceColor(ThemeSubsystem->GetPaletteColor(EMobiusPaletteRole::SublabelText));
	TitleTextStyle.ColorAndOpacity = LabelColor;
	MessageTextStyle.ColorAndOpacity = LabelColor;
	LocationTextStyle.ColorAndOpacity = SourceColor;
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetTextColors(LabelColor, LabelColor, SourceColor);
	}

	// 3. A19(d): window chrome. SWindow keeps a pointer to the style we handed it, so re-stamping the
	//    member in place is the intended route — but SWindowTitleBarWidget may well have copied brushes
	//    into its own children at construct (STextBlock above does exactly that), in which case the title
	//    bar will only pick this up on the NEXT open. Treat "the title bar follows a live toggle" as
	//    UNVERIFIED until the pixel gate says otherwise; the reopen path is correct either way.
	ErrorWindowStyle = ResolveErrorWindowChrome();
	if (ErrorWindowPtr.IsValid())
	{
		ErrorWindowPtr->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

bool SErrorWindowWidget::TryBindThemeChanged()
{
	if (ThemeChangedHandle.IsValid())
	{
		return true;
	}

	UUIThemeSubsystem* ThemeSubsystem = FindThemeSubsystemForErrorWindow();
	if (!ThemeSubsystem)
	{
		return false;
	}

	BoundThemeSubsystem = ThemeSubsystem;
	ThemeChangedHandle = ThemeSubsystem->OnThemeChangedNative.AddSP(this, &SErrorWindowWidget::ApplyThemeToWindow);

	// The event only fires on a later toggle, so stamp the current theme now.
	ApplyThemeToWindow();
	return true;
}

EActiveTimerReturnType SErrorWindowWidget::EnsureThemeBinding(double InCurrentTime, float InDeltaTime)
{
	return TryBindThemeChanged() ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
}

void SErrorWindowWidget::UnbindThemeChanged()
{
	// Removing by handle needs no AsShared(), so this is safe from the destructor. An SP binding would also
	// expire on its own, but a reopened window re-binds and duplicates would stack up.
	if (UUIThemeSubsystem* ThemeSubsystem = BoundThemeSubsystem.Get())
	{
		ThemeSubsystem->OnThemeChangedNative.Remove(ThemeChangedHandle);
	}
	BoundThemeSubsystem.Reset();
	ThemeChangedHandle.Reset();
}
