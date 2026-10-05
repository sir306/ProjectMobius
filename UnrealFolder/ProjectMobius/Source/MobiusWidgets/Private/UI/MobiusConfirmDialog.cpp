// Copyright (c) 2026 ProjectMobius contributors. MIT License.

#include "UI/MobiusConfirmDialog.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UI/Theme/MobiusThemePalette.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobiusConfirmDialog"

namespace
{
	/** Resolve the theme subsystem from a world context, or null if there is no game instance yet. */
	UUIThemeSubsystem* GetThemeSubsystem(const UObject* WorldContext)
	{
		if (!WorldContext)
		{
			return nullptr;
		}

		const UWorld* World = WorldContext->GetWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;

		return GameInstance ? GameInstance->GetSubsystem<UUIThemeSubsystem>() : nullptr;
	}

	/**
	 * Same construction the legal notice uses. Kept as a local helper rather than shared with it
	 * because that one deliberately hard-codes light (it runs before any theme exists) while this
	 * one follows the live theme.
	 */
	TSharedRef<FButtonStyle> MakeButtonStyle(
		const FLinearColor& Normal, const FLinearColor& Border, const FLinearColor& Foreground)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Normal, 3.0f, Border, 1.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(Normal * 1.20f, 3.0f, Border, 1.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(Normal * 0.72f, 3.0f, Border, 1.0f));
		Style.SetDisabled(FSlateRoundedBoxBrush(
			Normal.CopyWithNewOpacity(0.35f), 3.0f, Border.CopyWithNewOpacity(0.35f), 1.0f));

		// Keep Normal/Pressed padding totals equal. An asymmetric pressed padding shrinks the hit
		// rect mid-press, which fires OnMouseLeave and silently discards the click.
		Style.SetNormalPadding(FMargin(16.0f, 7.0f));
		Style.SetPressedPadding(FMargin(16.0f, 7.0f));
		Style.SetNormalForeground(FSlateColor(Foreground));
		Style.SetHoveredForeground(FSlateColor(Foreground));
		Style.SetPressedForeground(FSlateColor(Foreground));

		return MakeShared<FButtonStyle>(MoveTemp(Style));
	}
}

bool MobiusConfirmDialog::ShowYesNo(
	const UObject* WorldContext,
	const FText& WindowTitle,
	const FText& Heading,
	const FText& Body)
{
	// Slate must exist and we must be on the game thread: AddModalWindow asserts otherwise, and a
	// headless/commandlet run has no Slate at all. Declining is the safe answer in both cases.
	if (!FSlateApplication::IsInitialized() || !IsInGameThread())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Mobius.ConfirmDialog: no Slate or off the game thread - answering No to '%s'."),
			*WindowTitle.ToString());
		return false;
	}

	const UUIThemeSubsystem* ThemeSubsystem = GetThemeSubsystem(WorldContext);
	const bool bLight = ThemeSubsystem && ThemeSubsystem->GetTheme() == EMobiusUITheme::Light;

	using namespace MobiusThemePalette;
	const FLinearColor Background = Color(EMobiusPaletteRole::RibbonBg, bLight);
	const FLinearColor TextColour = Color(EMobiusPaletteRole::LabelText, bLight);
	const FLinearColor Accent     = Color(EMobiusPaletteRole::Accent, bLight);
	const FLinearColor PanelBg    = Color(EMobiusPaletteRole::InputBg, bLight);
	const FLinearColor BorderCol  = Color(EMobiusPaletteRole::WindowBorder, bLight);

	// SLATE_STYLE_ARGUMENT keeps a RAW pointer, so every style below must outlive the widget that
	// points at it. They are held alive by the OnWindowClosed capture at the end of this function,
	// which dies with the window.
	const TSharedRef<FWindowStyle> WindowStyle = MakeShared<FWindowStyle>(
		ThemeSubsystem
			? ThemeSubsystem->GetThemedWindowStyle()
			: FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window"));

	const TSharedRef<FButtonStyle> YesStyle = MakeButtonStyle(Accent, Accent, FLinearColor::White);
	const TSharedRef<FButtonStyle> NoStyle  = MakeButtonStyle(PanelBg, BorderCol, TextColour);

	// Default No. Every exit that is not the Yes button - title-bar close, Slate tearing the parent
	// down - leaves this false, which is the correct reading of "the user did not say yes".
	const TSharedRef<bool> bAccepted = MakeShared<bool>(false);

	// SAssignNew does not assign until the whole expression completes, so click lambdas must
	// dereference this holder at click time rather than capture the window early.
	const TSharedRef<TWeakPtr<SWindow>> WindowRef = MakeShared<TWeakPtr<SWindow>>();
	TSharedPtr<SMoveableWindow> DialogWindow;

	const auto CloseWindow = [WindowRef]()
	{
		if (const TSharedPtr<SWindow> Window = WindowRef->Pin())
		{
			Window->RequestDestroyWindow();
		}
	};

	// SMoveableWindow, not a raw SWindow: only it themes the TITLE BAR. A stock SWindow title bar
	// always paints FCoreStyle grey whatever style the window carries.
	SAssignNew(DialogWindow, SMoveableWindow)
		.Style(&WindowStyle.Get())
		.Title(WindowTitle)
		.ClientSize(FVector2D(520.0f, 210.0f))
		.SizingRule(ESizingRule::FixedSize)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.HasCloseButton(true)
		.CreateTitleBar(true)
		.IsTopmostWindow(true)
		// .WindowPanelContent, NOT the default content slot. When SMoveableWindow builds a title bar
		// it wraps ONLY _WindowPanelContent and never looks at _Content, so using the default slot
		// compiles fine and renders an EMPTY window with a correct title bar on top.
		.WindowPanelContent(
			SNew(SBorder)
			.BorderImage(new FSlateColorBrush(Background))
			.Padding(FMargin(24.0f, 20.0f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Heading)
					.ColorAndOpacity(FSlateColor(TextColour))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					.AutoWrapText(true)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Body)
					.ColorAndOpacity(FSlateColor(TextColour))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.AutoWrapText(true)
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBox)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&NoStyle.Get())
						.OnClicked_Lambda([CloseWindow]()
						{
							CloseWindow();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("No", "No"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&YesStyle.Get())
						.OnClicked_Lambda([bAccepted, CloseWindow]()
						{
							*bAccepted = true;
							CloseWindow();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Yes", "Yes"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						]
					]
				]
			]
		);

	*WindowRef = DialogWindow;

	// Style keep-alive. These captures are LIFETIME, not logic - dropping one leaves Slate reading
	// freed memory during teardown. Do not "tidy" them away.
	DialogWindow->GetOnWindowClosedEvent().AddLambda(
		[WindowStyle, YesStyle, NoStyle](const TSharedRef<SWindow>&) {});

	// Blocking. Returns once the window is destroyed, which every exit path above does.
	FSlateApplication::Get().AddModalWindow(DialogWindow.ToSharedRef(), nullptr, false);

	return *bAccepted;
}

#undef LOCTEXT_NAMESPACE
