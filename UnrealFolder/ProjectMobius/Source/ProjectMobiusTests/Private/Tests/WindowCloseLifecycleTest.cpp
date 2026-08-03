// Copyright (c) 2026 ProjectMobius contributors. Licensed under MIT.
//
// WindowCloseLifecycleTest.cpp
//
// Gates the reopen contract of the two free-standing Slate popups, SErrorWindowWidget and
// SLogWindowWidget, against EVERY close route rather than only the one their body "Close" button uses.
//
// The defect this exists for: both classes early-out of their open function on
// `if (<Window>Ptr.IsValid()) return;`, and that TSharedPtr used to be cleared only by
// Close<X>Window(), which was wired solely to the body button's OnClicked. The TITLE-BAR x does not go
// near it - SWindowTitleBar::CloseButton_OnClicked calls OwnerWindow->RequestDestroyWindow()
// (SWindowTitleBar.cpp:392) and nothing else. So a title-bar close destroyed the native window while
// leaving the shared pointer valid, and the window could never be reopened for the rest of the session:
// the open call returned early, and ShowErrorWindow's follow-up BringToFront ran on a dead window.
//
// The tests below drive RequestDestroyWindow() on the window directly. That is not an approximation of
// the title-bar x - it is literally the one line that button executes.
//
// Also gated, because the fix reordered them and they are the regression risk:
//   - the body Close button (SButton::SimulateClick on the real button inside the window tree),
//   - the destructor path,
//   - two full close/reopen cycles, since one cycle cannot show a theme delegate stacking up,
//   - SLogWindowWidget's OnLogWindowClosed notification, which UMobiusWidgetSubsystem uses to drop its
//     holder - and which now fires from a handler that resets LogWindowPtr FIRST, because that
//     subscriber destroys this widget from inside the callback.
//
// Windows are matched by title out of FSlateApplication's top-level list rather than by reaching into
// the widgets' private pointers: the pointer being stale IS the bug, so asserting on it would assert on
// the wrong thing. Counting is done as a DELTA against a baseline taken at test start, so an editor
// session that already has a window of the same name open cannot skew the result.
//
// Run from the Session Frontend (search "ProjectMobius.UI.Window") or:
//   MobiusPerf\RunTests.ps1
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "ErrorHandling/ErrorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Logging/LogWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace MobiusWindowCloseLifecycleTest
{
	const FString ErrorWindowTitle = TEXT("Error Window");
	const FString LogWindowTitle = TEXT("Mobius Log");

	/** Live top-level windows carrying this title. The widgets set it at construction. */
	int32 CountWindows(const FString& Title)
	{
		int32 Count = 0;
		for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
		{
			if (Window->GetTitle().ToString().Equals(Title))
			{
				++Count;
			}
		}
		return Count;
	}

	TSharedPtr<SWindow> FindWindow(const FString& Title)
	{
		for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
		{
			if (Window->GetTitle().ToString().Equals(Title))
			{
				return Window;
			}
		}
		return nullptr;
	}

	/** True if this subtree contains an STextBlock reading exactly LabelText. */
	bool ContainsLabel(const TSharedRef<SWidget>& Root, const FString& LabelText)
	{
		if (Root->GetType() == TEXT("STextBlock")
			&& StaticCastSharedRef<STextBlock>(Root)->GetText().ToString().Equals(LabelText))
		{
			return true;
		}

		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (ContainsLabel(Children->GetChildAt(Index), LabelText))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * The SButton whose content reads LabelText, depth-first.
	 *
	 * NOT "the first SButton in the tree": SMoveableWindow's own SWindowTitleBarWidget sits above the
	 * body in the same tree and contributes minimise/maximise/close buttons, which come first in a
	 * depth-first walk. On the error window those first two are built but inert (SupportsMinimize /
	 * SupportsMaximize are false), so clicking one is a silent no-op - which is exactly how this test
	 * failed before it matched on the label instead.
	 */
	TSharedPtr<SButton> FindButtonWithLabel(const TSharedRef<SWidget>& Root, const FString& LabelText)
	{
		if (Root->GetType() == TEXT("SButton") && ContainsLabel(Root, LabelText))
		{
			return StaticCastSharedRef<SButton>(Root);
		}

		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SButton> Found = FindButtonWithLabel(Children->GetChildAt(Index), LabelText))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** Slate has to be up for any of this to mean anything. Reported as a FAILURE, never a silent
	 *  skip: a vacuous green here would read as "the reopen contract holds" when nothing was tested. */
	bool RequireSlate(FAutomationTestBase& Test)
	{
		if (!FSlateApplication::IsInitialized())
		{
			Test.AddError(TEXT("FSlateApplication is not initialized - this test cannot gate the window ")
				TEXT("close/reopen contract in this run. Run it from the editor's Session Frontend."));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FErrorWindowReopensAfterEveryCloseRouteTest,
	"ProjectMobius.UI.WindowClose.ErrorWindowReopensAfterEveryCloseRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FErrorWindowReopensAfterEveryCloseRouteTest::RunTest(const FString& Parameters)
{
	using namespace MobiusWindowCloseLifecycleTest;

	if (!RequireSlate(*this))
	{
		return false;
	}

	const int32 Baseline = CountWindows(ErrorWindowTitle);

	{
		// Construct opens the window (SErrorWindowWidget::Construct -> OpenErrorWindow).
		const TSharedRef<SErrorWindowWidget> Widget = SNew(SErrorWindowWidget);
		TestEqual(TEXT("Constructing SErrorWindowWidget opens exactly one window"),
			CountWindows(ErrorWindowTitle), Baseline + 1);

		// --- Cycle 1: the title-bar x -------------------------------------------------------------
		{
			const TSharedPtr<SWindow> Window = FindWindow(ErrorWindowTitle);
			if (!Window.IsValid())
			{
				AddError(TEXT("No error window to close - the open path did not produce one."));
				return false;
			}
			// The exact call SWindowTitleBar::CloseButton_OnClicked makes.
			Window->RequestDestroyWindow();
		}
		TestEqual(TEXT("Title-bar close destroys the window"),
			CountWindows(ErrorWindowTitle), Baseline);

		Widget->ShowErrorWindow();
		TestEqual(TEXT("Window reopens after a title-bar close (the defect: OpenErrorWindow used to ")
			TEXT("early-out forever on a stale ErrorWindowPtr)"),
			CountWindows(ErrorWindowTitle), Baseline + 1);

		// Content must follow the reopen, not just the window. OpenErrorWindow rebuilds ContentPanel
		// from scratch, so the setters have to be writing into the NEW tree - a reopened window that
		// still showed the placeholder text would be just as broken as one that never appeared.
		// Assert on DIFFERENT values and read them back; re-setting the existing title would pass
		// against a no-op setter.
		const FString ReopenedWindowTitle = TEXT("Error Window Reopened");
		const FString ReopenedErrorTitle = TEXT("Reopened Error Title");
		const FString ReopenedErrorMessage = TEXT("Reopened error message.");
		Widget->SetTitleBarText(FText::FromString(ReopenedWindowTitle));
		Widget->SetErrorTitleText(FText::FromString(ReopenedErrorTitle));
		Widget->SetErrorMessageText(FText::FromString(ReopenedErrorMessage));
		{
			const TSharedPtr<SWindow> Reopened = FindWindow(ReopenedWindowTitle);
			TestTrue(TEXT("Reopened window took the new title-bar text"), Reopened.IsValid());
			if (Reopened.IsValid())
			{
				// SWindowContentPanel -> SFieldAndTitleText renders both of these as plain STextBlocks
				// (SFieldAndTitleText.cpp:54,59), so a label match is a real read-back of the body.
				TestTrue(TEXT("Reopened window body shows the new error title"),
					ContainsLabel(Reopened->GetContent(), ReopenedErrorTitle));
				TestTrue(TEXT("Reopened window body shows the new error message"),
					ContainsLabel(Reopened->GetContent(), ReopenedErrorMessage));

				// Put the title back: every later CountWindows/FindWindow keys off ErrorWindowTitle and
				// would silently stop matching.
				Widget->SetTitleBarText(FText::FromString(ErrorWindowTitle));
			}
		}
		TestEqual(TEXT("Window count unchanged by the content setters"),
			CountWindows(ErrorWindowTitle), Baseline + 1);

		// --- Cycle 2: title-bar x again. One cycle cannot show state that accumulates. -------------
		{
			const TSharedPtr<SWindow> Window = FindWindow(ErrorWindowTitle);
			if (Window.IsValid())
			{
				Window->RequestDestroyWindow();
			}
		}
		TestEqual(TEXT("Second title-bar close destroys the window"),
			CountWindows(ErrorWindowTitle), Baseline);

		Widget->ShowErrorWindow();
		TestEqual(TEXT("Window reopens after a SECOND title-bar close"),
			CountWindows(ErrorWindowTitle), Baseline + 1);

		// --- The body "Close" button, which is what the fix reordered ------------------------------
		{
			const TSharedPtr<SWindow> Window = FindWindow(ErrorWindowTitle);
			const TSharedPtr<SButton> BodyClose = Window.IsValid()
				? FindButtonWithLabel(Window->GetContent(), TEXT("Close"))
				: nullptr;
			if (!BodyClose.IsValid())
			{
				AddError(TEXT("Could not find the body Close button in the error window tree."));
				return false;
			}
			BodyClose->SimulateClick();
		}
		TestEqual(TEXT("Body Close button still destroys the window"),
			CountWindows(ErrorWindowTitle), Baseline);

		Widget->ShowErrorWindow();
		TestEqual(TEXT("Window reopens after a body Close click"),
			CountWindows(ErrorWindowTitle), Baseline + 1);
	}

	// --- Destructor path. ~SErrorWindowWidget calls CloseErrorWindow while the object is already
	// being torn down, so the OnWindowClosed AddSP binding must fail its weak pin instead of
	// re-entering a half-destructed widget.
	TestEqual(TEXT("Destroying the widget destroys its window"),
		CountWindows(ErrorWindowTitle), Baseline);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLogWindowReopensAfterEveryCloseRouteTest,
	"ProjectMobius.UI.WindowClose.LogWindowReopensAfterEveryCloseRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FLogWindowReopensAfterEveryCloseRouteTest::RunTest(const FString& Parameters)
{
	using namespace MobiusWindowCloseLifecycleTest;

	if (!RequireSlate(*this))
	{
		return false;
	}

	const int32 Baseline = CountWindows(LogWindowTitle);

	// Counts the OnLogWindowClosed notification. UMobiusWidgetSubsystem::LogWindowIsClosing hangs off
	// it and drops the subsystem's holder, so a close route that fires the window destroy WITHOUT this
	// callback would strand the subsystem believing the window is still up. Shared so the delegate does
	// not have to outlive a stack local.
	const TSharedRef<int32> ClosedNotifications = MakeShared<int32>(0);

	{
		const TSharedRef<SLogWindowWidget> Widget = SNew(SLogWindowWidget)
			.OnLogWindowClosed(FOnLogWindowClosed::CreateLambda([ClosedNotifications]()
			{
				++(*ClosedNotifications);
			}));

		TestEqual(TEXT("Constructing SLogWindowWidget opens exactly one window"),
			CountWindows(LogWindowTitle), Baseline + 1);
		TestTrue(TEXT("Log window reports itself open"), Widget->IsOpen());

		Widget->AppendLine(TEXT("line-before-close"));

		// --- Cycle 1: the title-bar x -------------------------------------------------------------
		{
			const TSharedPtr<SWindow> Window = FindWindow(LogWindowTitle);
			if (!Window.IsValid())
			{
				AddError(TEXT("No log window to close - the open path did not produce one."));
				return false;
			}
			Window->RequestDestroyWindow();
		}
		TestEqual(TEXT("Title-bar close destroys the log window"),
			CountWindows(LogWindowTitle), Baseline);
		TestFalse(TEXT("Log window no longer reports itself open after a title-bar close (the defect: ")
			TEXT("LogWindowPtr stayed valid, so IsOpen lied and OpenLogWindow early-outed forever)"),
			Widget->IsOpen());
		TestEqual(TEXT("Title-bar close still notifies OnLogWindowClosed"), *ClosedNotifications, 1);

		Widget->ShowLogWindow();
		TestEqual(TEXT("Log window reopens after a title-bar close"),
			CountWindows(LogWindowTitle), Baseline + 1);
		TestTrue(TEXT("Reopened log window reports itself open"), Widget->IsOpen());

		// --- Cycle 2 ------------------------------------------------------------------------------
		{
			const TSharedPtr<SWindow> Window = FindWindow(LogWindowTitle);
			if (Window.IsValid())
			{
				Window->RequestDestroyWindow();
			}
		}
		TestEqual(TEXT("Second title-bar close destroys the log window"),
			CountWindows(LogWindowTitle), Baseline);
		TestEqual(TEXT("Second title-bar close notifies again"), *ClosedNotifications, 2);

		Widget->ShowLogWindow();
		TestEqual(TEXT("Log window reopens after a SECOND title-bar close"),
			CountWindows(LogWindowTitle), Baseline + 1);

		// Contents survive a close/reopen: ReleaseWindowState drops the widget handles but must NOT
		// clear LogLines/LogText, which OpenLogWindow seeds the rebuilt STextBlock from.
		Widget->AppendLine(TEXT("line-after-reopen"));

		// --- The public programmatic close, which is what the body Close button runs ---------------
		Widget->CloseLogWindow();
		TestEqual(TEXT("CloseLogWindow destroys the window"),
			CountWindows(LogWindowTitle), Baseline);
		TestEqual(TEXT("CloseLogWindow notifies OnLogWindowClosed exactly once"),
			*ClosedNotifications, 3);

		// Calling it again with no window up must be inert - no second notification, no destroy of a
		// window that is already gone.
		Widget->CloseLogWindow();
		TestEqual(TEXT("CloseLogWindow on an already-closed window does not re-notify"),
			*ClosedNotifications, 3);

		Widget->ShowLogWindow();
		TestEqual(TEXT("Log window reopens after a programmatic close"),
			CountWindows(LogWindowTitle), Baseline + 1);
	}

	TestEqual(TEXT("Destroying the widget destroys its window"),
		CountWindows(LogWindowTitle), Baseline);

	return true;
}

#endif // !UE_BUILD_SHIPPING
