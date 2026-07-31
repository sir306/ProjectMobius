// Copyright (c) 2026 ProjectMobius contributors. MIT License.

#include "UI/LegalNoticeDialog.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UI/Theme/MobiusThemePalette.h"
#include "UserConfig/UserProjectSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobiusLegalNotice"

namespace
{
	struct FLegalNoticePalette
	{
		FLinearColor Background;
		FLinearColor Panel;
		FLinearColor Border;
		FLinearColor TitlebarBackground;
		FLinearColor TitlebarText;
		FLinearColor Text;
		FLinearColor Accent;
		FLinearColor ButtonText;
		FLinearColor DangerText;
		FLinearColor Divider;
		FLinearColor ScrollTrack;
		FLinearColor ScrollThumb;
	};

	FLegalNoticePalette GetPalette(const bool bUseLightTheme)
	{
		using namespace MobiusThemePalette;
		return {
			Color(EMobiusPaletteRole::RibbonBg, bUseLightTheme),
			Color(EMobiusPaletteRole::InputBg, bUseLightTheme),
			Color(EMobiusPaletteRole::WindowBorder, bUseLightTheme),
			Color(EMobiusPaletteRole::TitlebarBg, bUseLightTheme),
			Color(EMobiusPaletteRole::TitlebarText, bUseLightTheme),
			Color(EMobiusPaletteRole::LabelText, bUseLightTheme),
			Color(EMobiusPaletteRole::Accent, bUseLightTheme),
			Color(EMobiusPaletteRole::CheckboxCheckmark, bUseLightTheme),
			Color(EMobiusPaletteRole::DangerText, bUseLightTheme),
			Color(EMobiusPaletteRole::PanelDivider, bUseLightTheme),
			Color(EMobiusPaletteRole::SliderTrack, bUseLightTheme),
			Color(EMobiusPaletteRole::SliderThumb, bUseLightTheme)
		};
	}

	/**
	 * THEME ENFORCEMENT (2026-07-31). Everything below exists because a Slate widget with no explicit style
	 * silently falls back to FCoreStyle, and FCoreStyle is DARK. On this light-locked dialog that produced
	 * dark chrome and near-invisible controls on a light surface — the same failure the hard-coded
	 * GetPalette(true) call was added to prevent, just in the places a palette colour was never applied.
	 *
	 * This dialog is NATIVE Slate (SWindow / SBorder / STextBlock), not a UUserWidget, so it is invisible to
	 * BOTH theme drivers: the deleted value walk only ever saw UWidgets inside a UWidgetTree, and owner-pull
	 * only reaches UMobiusThemedUserWidget subclasses. Nothing will ever theme it for you. Every colour it
	 * renders has to be set here, explicitly, or it is a FCoreStyle default.
	 *
	 * It is also built ONCE per process, before any theme toggle can exist (FMobiusWidgetsModule's
	 * PostLoadMapWithWorld hook, packaged-only via the GIsEditor guard), so a static light palette is correct
	 * rather than a shortcut — there is no live theme to follow yet.
	 */
	TSharedRef<FWindowStyle> MakeWindowStyle(const FLegalNoticePalette& Palette)
	{
		// CreateTitleBar(true) makes SWindow draw its own title bar from this style. Without it the bar takes
		// FCoreStyle's dark brushes and its light title text, i.e. a dark strip with washed-out text sitting on
		// a light dialog. Palette.TitlebarBackground / TitlebarText were computed for this and were previously
		// never read. Same field set UUIThemeSubsystem::GetThemedWindowStyle writes; that function cannot be
		// reused here because it is a subsystem member and no GameInstance exists this early.
		FWindowStyle Style = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");

		// Frame + body, as FLAT COLOUR BRUSHES not tints. Tinting these leaves a 3px DARK GRADIENT RING outside
		// the border, because FCoreStyle's frame brushes are dark textures and a TintColor multiply can only
		// darken. Measured at mid-height before the fix: x=0..2 #323334 / #0E0F10 / #494B4E, then x=3..5 #B0B0B0.
		// Same correction is applied in UUIThemeSubsystem::GetThemedWindowStyle, which is where every other
		// Mobius popup gets its chrome. These windows are square-cornered, so no corner geometry is lost.
		Style.BorderBrush = FSlateColorBrush(Palette.Border);
		Style.OutlineBrush = FSlateColorBrush(Palette.Border);
		Style.BackgroundBrush = FSlateColorBrush(Palette.Background);

		// TITLE BAR — deliberately NOT set here, and this is not an oversight in either direction.
		//
		// The window is an SMoveableWindow, so its bar is Mobius's own SWindowTitleBarWidget, and that widget
		// OVERWRITES ActiveTitleBrush / InactiveTitleBrush / FlashTitleBrush with NoBrush on purpose
		// (SWindowTitleBarWidget.cpp:196-198) and paints an SColorBlock behind the bar whose colour is polled
		// LIVE from the theme subsystem (TitlebarBg), with the title text colour likewise polled live
		// (TitlebarText). That is what lets window chrome follow a runtime theme toggle instead of being frozen
		// at the style the window was created with. Setting the brushes here would be silently discarded.
		//
		// For this dialog the live value is the right one anyway: it shows once, on first packaged launch, where
		// the theme is the product default — light. The subsystem resolves at that point (the notice is opened
		// from PostLoadMapWithWorld, so a GameInstance exists), and the dark fallbacks baked into those lambdas
		// only apply if it cannot be found at all.
		//
		// TitleTextStyle still supplies the title FONT, so the shadow clear below DOES land — FCoreStyle's title
		// text carries a drop shadow sized for a dark bar, which reads as a smear on a light one. The colour is
		// left alone precisely because the live lambda outranks it.
		Style.TitleTextStyle.ShadowOffset = FVector2f::ZeroVector;
		Style.TitleTextStyle.ShadowColorAndOpacity = FLinearColor::Transparent;
		return MakeShared<FWindowStyle>(MoveTemp(Style));
	}

	TSharedRef<FTextBlockStyle> MakeLinkTextStyle(const FLegalNoticePalette& Palette)
	{
		// The four licence links carried no style at all, so they rendered in FCoreStyle's hyperlink colour —
		// tuned for dark chrome and close to unreadable on Palette.Background (0.9131). Accent is the palette's
		// interactive-text role and matches the "Custom scalability settings..." link in the main UI.
		FTextBlockStyle Style = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
		Style.ColorAndOpacity = FSlateColor(Palette.Accent);
		return MakeShared<FTextBlockStyle>(MoveTemp(Style));
	}

	TSharedRef<FButtonStyle> MakeLinkUnderlineStyle(const FLegalNoticePalette& Palette)
	{
		// SHyperlink draws its underline through a BUTTON style, so the underline is a separate colour from the
		// text and has to be tinted to match or the link reads as text with a stray dark rule under it.
		FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FHyperlinkStyle>("Hyperlink").UnderlineStyle;
		Style.Normal.TintColor = FSlateColor(Palette.Accent);
		Style.Hovered.TintColor = FSlateColor(Palette.Accent);
		Style.Pressed.TintColor = FSlateColor(Palette.Accent);
		Style.SetNormalForeground(Palette.Accent);
		Style.SetHoveredForeground(Palette.Accent);
		Style.SetPressedForeground(Palette.Accent);
		return MakeShared<FButtonStyle>(MoveTemp(Style));
	}

	TSharedRef<FScrollBarStyle> MakeScrollBarStyle(const FLegalNoticePalette& Palette)
	{
		// The notices are long enough to scroll on a default window size, so the bar is always visible. Unstyled
		// it is FCoreStyle's dark thumb on a dark track, inside a Palette.Panel (white) box.
		FScrollBarStyle Style = FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
		const FSlateRoundedBoxBrush Track(Palette.ScrollTrack, 2.0f);
		const FSlateRoundedBoxBrush Thumb(Palette.ScrollThumb, 2.0f);
		Style.SetVerticalBackgroundImage(Track);
		Style.SetHorizontalBackgroundImage(Track);
		Style.SetNormalThumbImage(Thumb);
		Style.SetHoveredThumbImage(Thumb);
		Style.SetDraggedThumbImage(Thumb);
		return MakeShared<FScrollBarStyle>(MoveTemp(Style));
	}

	TSharedRef<FButtonStyle> MakeButtonStyle(const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Pressed, const FLinearColor& Border, const FLinearColor& Foreground)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Normal, 3.0f, Border, 1.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(Hovered, 3.0f, Border, 1.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(Pressed, 3.0f, Border, 1.0f));
		Style.SetDisabled(FSlateRoundedBoxBrush(Normal.CopyWithNewOpacity(0.35f), 3.0f, Border.CopyWithNewOpacity(0.35f), 1.0f));
		Style.SetNormalForeground(Foreground);
		Style.SetHoveredForeground(Foreground);
		Style.SetPressedForeground(Foreground);
		Style.SetDisabledForeground(Foreground.CopyWithNewOpacity(0.55f));
		return MakeShared<FButtonStyle>(MoveTemp(Style));
	}

	TSharedRef<FCheckBoxStyle> MakeCheckBoxStyle(const FLegalNoticePalette& Palette)
	{
		FCheckBoxStyle Style;
		FSlateRoundedBoxBrush Unchecked(Palette.Panel, 3.0f, Palette.Border, 1.0f);
		FSlateRoundedBoxBrush Checked(Palette.Accent, 3.0f, FLinearColor::White, 1.0f);
		Unchecked.ImageSize = FVector2D(18.0f, 18.0f);
		Checked.ImageSize = FVector2D(18.0f, 18.0f);
		Style.SetUncheckedImage(Unchecked);
		Style.SetUncheckedHoveredImage(Unchecked);
		Style.SetUncheckedPressedImage(Unchecked);
		Style.SetCheckedImage(Checked);
		Style.SetCheckedHoveredImage(Checked);
		Style.SetCheckedPressedImage(Checked);
		return MakeShared<FCheckBoxStyle>(MoveTemp(Style));
	}

	void OpenUrl(const TCHAR* Url)
	{
		FPlatformProcess::LaunchURL(Url, nullptr, nullptr);
	}

	void OpenLocalDocument(const TCHAR* FileName)
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*FPaths::Combine(FPaths::ProjectDir(), TEXT("BuildDocs"), FileName));
	}

	// CloseNoticeOrQuit was deleted along with the hand-rolled close button. Its preview-dismiss / real-quit
	// split now lives in the SetRequestDestroyWindowOverride lambda, which is where the title bar's close
	// button lands — one close policy instead of two that had to be kept in agreement.
}

namespace
{
void ShowLegalNotice(UUserProjectSettings& UserSettings, const bool bForce)
{
	if ((!bForce && UserSettings.HasAcceptedCurrentLegalNotice()) || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// This is the very first UI a new user sees. The product default is light, so do not inherit
	// editor or OS chrome colours here; that was the source of the unreadable dark-on-dark preview.
	const FLegalNoticePalette Palette = GetPalette(/* bUseLightTheme = */ true);
	const TSharedRef<bool> bAcknowledgedTerms = MakeShared<bool>(false);
	const TSharedRef<bool> bAcknowledgedDisclaimer = MakeShared<bool>(false);
	const TSharedRef<bool> bPermittedToClose = MakeShared<bool>(false);
	const TSharedRef<FButtonStyle> PrimaryButtonStyle = MakeButtonStyle(
		Palette.Accent, Palette.Accent * 1.20f, Palette.Accent * 0.72f, Palette.Accent, FLinearColor::White);
	// DangerButtonStyle is gone with the hand-rolled quit button; the title bar supplies its own close-button
	// styling. Palette.DangerText is consequently unused here — kept in the struct as the correct token if a
	// destructive control is ever added back to this dialog's body.
	const TSharedRef<FCheckBoxStyle> CheckBoxStyle = MakeCheckBoxStyle(Palette);
	// SLATE_STYLE_ARGUMENT stores a raw pointer, so every one of these must outlive the widget that points at
	// it. They are kept alive by being captured in the OnWindowClosed lambda below, which dies with the window
	// — the same keep-alive trick the button/checkbox styles already use via their OnClicked captures.
	const TSharedRef<FWindowStyle> WindowStyle = MakeWindowStyle(Palette);
	const TSharedRef<FTextBlockStyle> LinkTextStyle = MakeLinkTextStyle(Palette);
	const TSharedRef<FButtonStyle> LinkUnderlineStyle = MakeLinkUnderlineStyle(Palette);
	const TSharedRef<FScrollBarStyle> ScrollBarStyle = MakeScrollBarStyle(Palette);
	// SAssignNew does not assign NoticeWindow until the whole Slate expression is complete. Lambdas inside
	// that expression must therefore dereference this holder at click time, not capture NoticeWindow early.
	const TSharedRef<TWeakPtr<SWindow>> WindowRef = MakeShared<TWeakPtr<SWindow>>();
	TSharedPtr<SMoveableWindow> NoticeWindow;

	// SMoveableWindow, NOT a raw SWindow — this is what makes the TITLE BAR themeable, and it is the same
	// window class the ImPlot overlay, the log window and the error window already use, so this dialog now
	// wears the same chrome as the rest of the application.
	//
	// Why a raw SWindow could not do it: FSlateApplication::MakeWindowTitleBar builds SWindowTitleBar with
	// SNew(SWindowTitleBar, Window, ...).Visibility(...).CloseButtonToolTipText(...) and NEVER forwards a
	// .Style(), while SWindowTitleBar::FArguments defaults _Style to FCoreStyle's "Window"
	// (SWindowTitleBar.h:99, .cpp:10) — so a stock title bar always paints FCoreStyle grey whatever style the
	// window carries. Measured: on a raw SWindow, tinting the title brushes and then REPLACING them with flat
	// FSlateColorBrush gave pixel-identical captures (#B0B0B0 both times, which was the window BORDER showing
	// through). SMoveableWindow closes exactly that gap: it forces CreateTitleBar(false) on the engine path and
	// builds its own SWindowTitleBarWidget with .WindowStyle(WindowStyle) and
	// .TitleTextStyle(&WindowStyle->TitleTextStyle) (SMoveableWindow.cpp:428, :469-482), and its
	// SMoveableWindowTitleBar passes .Style(InArgs._Style) into SWindowTitleBar::Construct.
	//
	// It also supplies its own title-bar dragging and resize overlay instead of entering the OS modal
	// move/resize path, which is what the previous "native chrome" comment here was protecting — window
	// movement and the close affordance both survive the switch.
	SAssignNew(NoticeWindow, SMoveableWindow)
		.Style(&WindowStyle.Get())
		.Title(LOCTEXT("WindowTitle", "Project Möbius - Terms and Licences"))
		.ClientSize(FVector2D(820.0f, 680.0f))
		.SizingRule(ESizingRule::UserSized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		// The title bar's own close button IS the quit control — standard position and metrics, no hand-rolled
		// glyph. Nothing binds to it directly: it routes through SWindow::RequestDestroyWindow(), which the
		// destroy override below intercepts, so the quit/dismiss policy stays in one place.
		.HasCloseButton(true)
		.CloseButtonToolTipText(LOCTEXT("QuitTooltip", "Quit application"))
		.CreateTitleBar(true)
		.IsTopmostWindow(true)
		// .WindowPanelContent, NOT the default [ ... ] slot. When SMoveableWindow builds a title bar it wraps
		// ONLY _WindowPanelContent under it and never looks at _Content (SMoveableWindow.cpp:496), so passing
		// the body through the default slot compiled fine and rendered an EMPTY grey window with a correct
		// title bar on top. Measured, not theorised.
		.WindowPanelContent(
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				// Padding 0, NOT 1: the WINDOW draws the 1px frame now (SMoveableWindow's LayoutBorder, painted
				// with the style's BorderBrush). A 1u ring here as well made the visible border 2px on top of
				// the window's own band. Kept as a zero-padding border rather than deleted so the Slate nesting
				// below is untouched — the inner border covers it completely, so it contributes no pixels.
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Palette.Border)
				.Padding(0.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(Palette.Background)
					.Padding(28.0f)
					[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Heading", "Before you use Project Möbius"))
						.ColorAndOpacity(Palette.Text)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 16.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Intro", "Please review these notices. The application cannot be used until they are acknowledged."))
						.ColorAndOpacity(Palette.Text)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(Palette.Border)
						.Padding(1.0f)
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(Palette.Panel)
							.Padding(16.0f)
							[
								SNew(SScrollBox)
								.ScrollBarStyle(&ScrollBarStyle.Get())
								+ SScrollBox::Slot()
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(STextBlock).Text(LOCTEXT("EpicHeading", "1. Unreal Engine and Epic content"))
										.ColorAndOpacity(Palette.Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 10.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("EpicBody", "This application uses Unreal Engine. Unreal Engine and any Epic-provided content are governed by Epic's applicable terms. The packaged application excludes Twinmotion desktop-export content; it is not licensed for interactive distribution in this product."))
										.ColorAndOpacity(Palette.Text).AutoWrapText(true)
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SHorizontalBox)
										// VAlign_Top + NO bottom padding. An SHorizontalBox slot defaults to VAlign_Fill, so the
									// hyperlink in the sibling UNPADDED slot stretched to the row height this slot's
									// 10u bottom padding created — and SHyperlink draws its underline as a BOTTOM
									// BORDER brush on the button, so the dotted rule landed ~10u below the glyphs.
									// That is the "dotted underline way below the text". Equal vertical padding on
									// both slots means neither stretches, so both underlines hug their text. The 10u
									// gap is not re-added: the following slot already carries its own top padding.
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 18.0f, 0.0f)
										[
											SNew(SHyperlink).Text(LOCTEXT("UnrealEulaLink", "Read the full Unreal Engine EULA"))
											.TextStyle(&LinkTextStyle.Get()).UnderlineStyle(&LinkUnderlineStyle.Get())
											.OnNavigate_Lambda([] { OpenUrl(TEXT("https://www.unrealengine.com/en-US/eula/unreal")); })
										]
										+ SHorizontalBox::Slot().AutoWidth()
										[
											SNew(SHyperlink).Text(LOCTEXT("EpicContentLink", "Read the full Epic Content Licence"))
											.TextStyle(&LinkTextStyle.Get()).UnderlineStyle(&LinkUnderlineStyle.Get())
											.OnNavigate_Lambda([] { OpenUrl(TEXT("https://www.unrealengine.com/en-US/eula/content")); })
										]
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 12.0f)
									[
										// Unstyled this takes FCoreStyle's dark rule, which on a white Panel reads as a hard black line.
										SNew(SSeparator).ColorAndOpacity(FSlateColor(Palette.Divider))
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(STextBlock).Text(LOCTEXT("LicenceHeading", "2. Project Möbius and third-party licences"))
										.ColorAndOpacity(Palette.Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 10.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("LicenceBody", "Project Möbius source is made available under the MIT License. This packaged application also contains third-party components which remain subject to their own licence terms. The complete third-party notices are supplied with this application."))
										.ColorAndOpacity(Palette.Text).AutoWrapText(true)
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SHorizontalBox)
										// VAlign_Top + NO bottom padding. An SHorizontalBox slot defaults to VAlign_Fill, so the
									// hyperlink in the sibling UNPADDED slot stretched to the row height this slot's
									// 10u bottom padding created — and SHyperlink draws its underline as a BOTTOM
									// BORDER brush on the button, so the dotted rule landed ~10u below the glyphs.
									// That is the "dotted underline way below the text". Equal vertical padding on
									// both slots means neither stretches, so both underlines hug their text. The 10u
									// gap is not re-added: the following slot already carries its own top padding.
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 18.0f, 0.0f)
										[
											SNew(SHyperlink).Text(LOCTEXT("ThirdPartyLink", "Open included third-party licences"))
											.TextStyle(&LinkTextStyle.Get()).UnderlineStyle(&LinkUnderlineStyle.Get())
											.OnNavigate_Lambda([] { OpenLocalDocument(TEXT("THIRD-PARTY-LICENSES.md")); })
										]
										+ SHorizontalBox::Slot().AutoWidth()
										[
											SNew(SHyperlink).Text(LOCTEXT("MitLink", "Read the MIT licence"))
											.TextStyle(&LinkTextStyle.Get()).UnderlineStyle(&LinkUnderlineStyle.Get())
											.OnNavigate_Lambda([] { OpenUrl(TEXT("https://opensource.org/license/mit")); })
										]
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("Disclaimer", "TO THE FULLEST EXTENT PERMITTED BY LAW, PROJECT MÖBIUS, UNREAL ENGINE, EPIC CONTENT, AND OTHER LICENSED COMPONENTS ARE PROVIDED \"AS IS\" WITHOUT REPRESENTATIONS, WARRANTIES, CONDITIONS, OR LIABILITY OF ANY KIND. Nothing here limits rights that cannot lawfully be excluded."))
										.ColorAndOpacity(Palette.Text).AutoWrapText(true)
									]
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 4.0f)
					[
						SNew(SCheckBox)
						.Style(&CheckBoxStyle.Get())
						.OnCheckStateChanged_Lambda([bAcknowledgedTerms, CheckBoxStyle](ECheckBoxState State)
						{
							*bAcknowledgedTerms = State == ECheckBoxState::Checked;
							UE_LOG(LogTemp, Display, TEXT("Mobius.LegalNotice: first acknowledgement changed."));
						})
						[
							SNew(STextBlock).Text(LOCTEXT("AcknowledgeTerms", "I have read and acknowledge the software, third-party, Unreal Engine and Epic content notices."))
							.ColorAndOpacity(Palette.Text).AutoWrapText(true)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 16.0f)
					[
						SNew(SCheckBox)
						.Style(&CheckBoxStyle.Get())
						.OnCheckStateChanged_Lambda([bAcknowledgedDisclaimer, CheckBoxStyle](ECheckBoxState State)
						{
							*bAcknowledgedDisclaimer = State == ECheckBoxState::Checked;
							UE_LOG(LogTemp, Display, TEXT("Mobius.LegalNotice: second acknowledgement changed."));
						})
						[
							SNew(STextBlock).Text(LOCTEXT("AcknowledgeDisclaimer", "I acknowledge the application is provided as-is and I accept these conditions before continuing."))
							.ColorAndOpacity(Palette.Text).AutoWrapText(true)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DiagnosticsPrivacy", "Diagnostics and troubleshooting logs are generated and stored locally on this device. Project Möbius does not automatically upload, transmit, or share log content. You choose whether to provide logs when seeking support."))
						.ColorAndOpacity(Palette.Text.CopyWithNewOpacity(0.70f))
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SSpacer)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(&PrimaryButtonStyle.Get())
							.IsEnabled_Lambda([bAcknowledgedTerms, bAcknowledgedDisclaimer]() { return *bAcknowledgedTerms && *bAcknowledgedDisclaimer; })
							.OnClicked_Lambda([&UserSettings, bForce, bPermittedToClose, PrimaryButtonStyle, WindowRef]()
							{
								UE_LOG(LogTemp, Display, TEXT("Mobius.LegalNotice: agree control clicked (Preview=%s)."), bForce ? TEXT("true") : TEXT("false"));
								if (!bForce)
								{
									UserSettings.AcceptCurrentLegalNotice();
								}
								*bPermittedToClose = true;
								if (const TSharedPtr<SWindow> Window = WindowRef->Pin())
								{
									// Acceptance is the only permitted non-terminating close path.
									Window->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride());
									Window->RequestDestroyWindow();
								}
								return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(LOCTEXT("AgreeButton", "I agree - continue to Project Möbius"))
							.ColorAndOpacity(Palette.ButtonText).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						]
					]
					]
				]
			]
		]
		// The quit affordance is the TITLE BAR's own close button (HasCloseButton + CloseButtonToolTipText
		// above), not a widget in this overlay. It replaced a hand-rolled 24x24 SButton floated in the
		// top-right of the content: that one sat inside the dialog body rather than in the chrome, did not
		// match the platform's close-button metrics, and its glyph was drawn in DangerText ON a DangerText
		// fill, so there was no visible X at all. Nothing is bound to the button directly — SWindowTitleBar
		// routes it through SWindow::RequestDestroyWindow(), which is what the destroy override below
		// intercepts, so the quit / dismiss policy stays in exactly one place.
		);

	*WindowRef = NoticeWindow;

	// SINGLE close policy. Every dismissal route now arrives here — the title bar's close button, an OS close
	// request, and any other Slate caller trying to dismiss the mandatory modal — because they all go through
	// SWindow::RequestDestroyWindow(). The acceptance path bypasses it by clearing the override first.
	NoticeWindow->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride::CreateLambda([bForce, bPermittedToClose](const TSharedRef<SWindow>& Window)
	{
		// Preview must stay dismissible: it runs in the EDITOR, so quitting would take the editor with it, and
		// it deliberately records no consent. Clear the override BEFORE re-requesting or this lambda re-enters
		// itself forever.
		if (bForce || *bPermittedToClose)
		{
			UE_LOG(LogTemp, Display, TEXT("Mobius.LegalNotice: close control dismissed the notice (Preview=%s)."),
				bForce ? TEXT("true") : TEXT("false"));
			*bPermittedToClose = true;
			Window->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride());
			Window->RequestDestroyWindow();
			return;
		}

		// Real first-launch path: declining the notice quits the application. The window is intentionally NOT
		// destroyed here — refusing to consent is refusing to run.
		FPlatformMisc::RequestExit(false);
	}));

	// Covers direct/immediate destruction paths that bypass RequestDestroyWindow().
	// The four style captures here are LIFETIME, not logic — SLATE_STYLE_ARGUMENT keeps a raw pointer, and
	// this delegate is destroyed with the window, so it is the natural owner. Do not "tidy" them out of the
	// capture list: dropping one leaves SWindow / SScrollBox / SHyperlink reading freed memory.
	NoticeWindow->GetOnWindowClosedEvent().AddLambda([bForce, bPermittedToClose, WindowStyle, LinkTextStyle, LinkUnderlineStyle, ScrollBarStyle](const TSharedRef<SWindow>&)
	{
		if (!bForce && !*bPermittedToClose)
		{
			FPlatformMisc::RequestExit(false);
		}
	});

	// Some platforms do not visibly flash a modal child when its parent is clicked. Request the
	// platform attention cue explicitly whenever focus leaves this mandatory runtime window.
	NoticeWindow->GetOnWindowDeactivatedEvent().AddLambda([WindowRef]()
	{
		if (const TSharedPtr<SWindow> Window = WindowRef->Pin())
		{
			Window->FlashWindow();
		}
	});

	FSlateApplication::Get().AddModalWindow(NoticeWindow.ToSharedRef(), nullptr, false);
}
}

void MobiusLegalNotice::ShowIfRequired(UUserProjectSettings& UserSettings)
{
	ShowLegalNotice(UserSettings, false);
}

void MobiusLegalNotice::ShowPreview(UUserProjectSettings& UserSettings)
{
	ShowLegalNotice(UserSettings, true);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand GMobiusLegalNoticePreviewCommand(
	TEXT("Mobius.LegalNotice.Preview"),
	TEXT("Opens the first-launch legal notice for visual review, ignoring prior acceptance."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (UUserProjectSettings* UserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
		{
			MobiusLegalNotice::ShowPreview(*UserSettings);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Mobius.LegalNotice.Preview: Project Mobius user settings are unavailable."));
		}
	}));
#endif

#undef LOCTEXT_NAMESPACE
