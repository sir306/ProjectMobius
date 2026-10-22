// Copyright (c) 2026 ProjectMobius contributors. Licensed under MIT.
//
// LogWindowPerfTest.cpp
//
// Frame cost of the Mobius log window (SLogWindowWidget) once its backlog is full - the "Mobius gets
// slow after 30+ minutes with the log window open" report. Against a 1000-line backlog it measures:
//   - idle    : a redraw with nothing new (the window's standing per-frame cost)
//   - trickle : one line per frame (the low-FPS watchdog and other steady producers)
//   - burst N : N lines in one logger flush, then the next two frames.
//               UMobiusCustomLoggerSubsystem::FlushToDisk broadcasts every queued line separately,
//               4x a second, so a heatmap load (~3,600 CreateMeshSection lines) lands as one burst.
// Each scenario times AppendLine (what the logger flush pays, synchronously) and
// FSlateApplication::ForceRedrawWindow on the log window alone - prepass, widget tick and paint, which
// is where text layout and list-row generation actually happen. Timing only AppendLine, or only the
// redraw, would hide whichever half a given implementation defers.
//
// Uses only the widget's public API and finds the window by title, so this one file measures any
// implementation of the window, before and after a change. Every scenario also checks the view is
// still pinned to the newest line - a cheap frame that stopped showing the end would not be a win.
//
// FollowsEndUnlessScrolledUp is the behavioural half: a reader who scrolls up (real wheel events routed
// through FSlateApplication, so they count as input) is not yanked back by new lines or a backlog trim,
// and following resumes once they scroll back down to the end.
//
// Needs a real renderer. Under -nullrhi Slate skips widget drawing entirely
// (GSlateSkipWidgetDrawingInHeadlessMode), so the redraw would time nothing; the test fails rather than
// report that. Run:
//   MobiusPerf\RunTests.ps1 -Filter "Mobius.RenderedTiming.LogWindow." -Rendered
// Named "Mobius.RenderedTiming." - deliberately NOT containing "ProjectMobius." or "Mobius.Timing." - so
// neither the default correctness run nor -IncludeTiming picks it up: both run under -nullrhi, where these
// tests can only fail. Rows are appended to
// <ProjectSaved>/MobiusPerfTimings/logwindow.csv; no absolute number is asserted.
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogWindow.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace MobiusLogWindowTiming
{
	const FString LogWindowTitle = TEXT("Mobius Log");
	constexpr int32 BacklogLines = 1000;

	/**
	 * Two real line shapes from MobiusCustomLog.txt, alternated so the mean (~92 chars) sits near the
	 * ~89-char mean of the 1.26 M-line log on the dev machine. The index makes every line distinct.
	 */
	FString MakeLine(int32 Index)
	{
		if (Index % 2 == 0)
		{
			return FString::Printf(TEXT("[2026-10-22 11:23:45.%03d] [Heatmap floor=0] CreateMeshSection section=%d verts=1056 tris=1984 in 0.05 ms"),
				Index % 1000, Index);
		}
		return FString::Printf(TEXT("[2026-10-22 11:23:45.%03d] Low FPS detected: 27.4 FPS (threshold 30.0) line %d"),
			Index % 1000, Index);
	}

	/** The "Mobius Log" window that was not already up before the widget was constructed. */
	TSharedPtr<SWindow> FindNewLogWindow(const TSet<const SWindow*>& Before)
	{
		for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
		{
			if (!Before.Contains(&Window.Get()) && Window->GetTitle().ToString().Equals(LogWindowTitle))
			{
				return Window;
			}
		}
		return nullptr;
	}

	/** Drain the render thread. Through the Slate renderer (FSlateRHIRenderer::FlushCommands is
	 *  FlushRenderingCommands) so this module needs no RenderCore link dependency. */
	void FlushSlateRendering()
	{
		if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
		{
			Renderer->FlushCommands();
		}
	}

	/**
	 * FSlateApplicationBase::UpdateAnyActiveTimersArePending is protected; a real Slate frame calls it
	 * once before drawing, and an active timer only runs in a paint that follows it. ForceRedrawWindow
	 * alone would therefore never run the window's timers. Re-exposed here - not re-implemented - so a
	 * redraw below behaves like a real frame.
	 */
	struct FActiveTimerPump : FSlateApplicationBase
	{
		using FSlateApplicationBase::UpdateAnyActiveTimersArePending;
	};

	void PumpActiveTimers()
	{
		FSlateApplicationBase& App = FSlateApplication::Get();
		(App.*(&FActiveTimerPump::UpdateAnyActiveTimersArePending))();
	}

	/** Game-thread cost of one frame of this window: active-timer pump + prepass + tick + paint.
	 *  Render-thread work is flushed outside the timer so it neither counts nor piles up across
	 *  iterations (a full draw-buffer pool would otherwise make the next draw block inside the timer). */
	double RedrawMs(const TSharedRef<SWindow>& Window)
	{
		FlushSlateRendering();
		const double Start = FPlatformTime::Seconds();
		PumpActiveTimers();
		FSlateApplication::Get().ForceRedrawWindow(Window);
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		FlushSlateRendering();
		return Ms;
	}

	int32 CountTextBlocks(const TSharedRef<SWidget>& Root)
	{
		int32 Count = Root->GetType() == TEXT("STextBlock") ? 1 : 0;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			Count += CountTextBlocks(Children->GetChildAt(Index));
		}
		return Count;
	}

	/** First widget, depth-first, whose type name starts with Prefix ("SListView<...>" is templated). */
	TSharedPtr<SWidget> FindWidgetOfType(const TSharedRef<SWidget>& Root, const TCHAR* Prefix)
	{
		if (Root->GetType().ToString().StartsWith(Prefix))
		{
			return Root;
		}
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SWidget> Found = FindWidgetOfType(Children->GetChildAt(Index), Prefix))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** A painted STextBlock ending with Line. EndsWith, not Equals, so it matches both a single
	 *  whole-backlog text block and a one-row-per-line list. */
	TSharedPtr<SWidget> FindPaintedTextEndingWith(const TSharedRef<SWidget>& Root, const FString& Line)
	{
		if (Root->GetType() == TEXT("STextBlock")
			&& StaticCastSharedRef<STextBlock>(Root)->GetText().ToString().EndsWith(Line)
			&& Root->GetPaintSpaceGeometry().GetLocalSize().Y > 0.0)
		{
			return Root;
		}
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SWidget> Found = FindPaintedTextEndingWith(Children->GetChildAt(Index), Line))
			{
				return Found;
			}
		}
		return nullptr;
	}

	double AbsoluteTop(const FGeometry& Geometry)
	{
		return Geometry.LocalToAbsolute(FVector2D::ZeroVector).Y;
	}

	double AbsoluteBottom(const FGeometry& Geometry)
	{
		return Geometry.LocalToAbsolute(FVector2D(0.0, Geometry.GetLocalSize().Y)).Y;
	}

	/** The scrolling viewport: the list (one row per line) or the scroll box (one whole-backlog text). */
	TSharedPtr<SWidget> FindLogViewport(const TSharedRef<SWindow>& Window)
	{
		const TSharedPtr<SWidget> List = FindWidgetOfType(Window, TEXT("SListView"));
		return List.IsValid() ? List : FindWidgetOfType(Window, TEXT("SScrollBox"));
	}

	/**
	 * Line is painted with its bottom on the viewport's bottom edge: the view is pinned to it, not merely
	 * holding a row for it somewhere below the fold. (Holding-but-not-showing is exactly what a freshly
	 * generated wrapping row does if nothing re-pins once it learns its wrap width.)
	 */
	bool IsLineAtViewportBottom(const TSharedRef<SWindow>& Window, const FString& Line, FString& OutDetail)
	{
		const TSharedPtr<SWidget> Viewport = FindLogViewport(Window);
		if (!Viewport.IsValid())
		{
			OutDetail = TEXT("no list or scroll box in the log window");
			return false;
		}
		const TSharedPtr<SWidget> Text = FindPaintedTextEndingWith(Viewport.ToSharedRef(), Line);
		if (!Text.IsValid())
		{
			OutDetail = TEXT("the line has no painted text block (not generated, or never drawn)");
			return false;
		}
		const double ViewportBottom = AbsoluteBottom(Viewport->GetPaintSpaceGeometry());
		const double TextBottom = AbsoluteBottom(Text->GetPaintSpaceGeometry());
		OutDetail = FString::Printf(TEXT("line bottom %.1f px, viewport bottom %.1f px"), TextBottom, ViewportBottom);
		return FMath::Abs(TextBottom - ViewportBottom) <= 3.0;
	}

	/** Texts of the painted text blocks lying wholly inside the viewport, top to bottom. */
	void CollectVisibleLines(const TSharedRef<SWidget>& Root, double ViewportTop, double ViewportBottom, TArray<FString>& OutLines)
	{
		if (Root->GetType() == TEXT("STextBlock"))
		{
			const FGeometry& Geometry = Root->GetPaintSpaceGeometry();
			if (Geometry.GetLocalSize().Y > 0.0
				&& AbsoluteTop(Geometry) >= ViewportTop - 0.5 && AbsoluteBottom(Geometry) <= ViewportBottom + 0.5)
			{
				OutLines.Add(StaticCastSharedRef<STextBlock>(Root)->GetText().ToString());
			}
			return;
		}
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectVisibleLines(Children->GetChildAt(Index), ViewportTop, ViewportBottom, OutLines);
		}
	}

	/** One wheel notch (or a big flick) routed to Target the way FSlateApplication routes real input -
	 *  inside its input-processing scope, which is what the window keys "the reader scrolled" off. */
	bool RouteWheel(const TSharedRef<SWidget>& Target, float WheelDelta)
	{
		FWidgetPath FoundPath;
		if (!FSlateApplication::Get().FindPathToWidget(Target, FoundPath))
		{
			return false;
		}
		// FindPathToWidget leaves the per-widget virtual pointer positions empty, and pointer routing
		// indexes them, so rebuild the path through the constructor that fills them (unset = no 3D widget).
		TArray<FWidgetAndPointer> WidgetsAndPointers;
		for (int32 Index = 0; Index < FoundPath.Widgets.Num(); ++Index)
		{
			WidgetsAndPointers.Add(FWidgetAndPointer(FoundPath.Widgets[Index]));
		}
		const FWidgetPath Path(WidgetsAndPointers);

		const FVector2D Centre = Target->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5));
		const FPointerEvent Wheel(/*PointerIndex*/ 0, Centre, Centre, TSet<FKey>(), FKey(), WheelDelta, FModifierKeysState());
		return FSlateApplication::Get().RouteMouseWheelOrGestureEvent(Path, Wheel, nullptr).IsEventHandled();
	}

	double Median(TArray<double> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return (Values.Num() % 2 == 1) ? Values[Mid] : 0.5 * (Values[Mid - 1] + Values[Mid]);
	}

	double MaxOf(const TArray<double>& Values)
	{
		double Result = 0.0;
		for (const double Value : Values)
		{
			Result = FMath::Max(Result, Value);
		}
		return Result;
	}

	struct FScenarioResult
	{
		FString Scenario;
		int32 LinesPerIteration = 0;
		int32 Iterations = 0;
		double AppendMedianMs = 0.0;
		double Frame1MedianMs = 0.0;
		double Frame2MedianMs = 0.0;
		double TotalMedianMs = 0.0;
		double TotalMaxMs = 0.0;
	};

	/** Append one row per scenario to the trend CSV (header written on first use). */
	void AppendCsvRow(const FScenarioResult& Result, int32 TextBlocks)
	{
		const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusPerfTimings"));
		const FString CsvPath = FPaths::Combine(Dir, TEXT("logwindow.csv"));
		FString Row;
		if (!IFileManager::Get().FileExists(*CsvPath))
		{
			IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
			Row += TEXT("timestamp_utc,machine,scenario,lines_per_iteration,iterations,append_median_ms,frame1_median_ms,frame2_median_ms,total_median_ms,total_max_ms,text_blocks_in_window\n");
		}
		Row += FString::Printf(TEXT("%s,%s,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n"),
			*FDateTime::UtcNow().ToIso8601(), FPlatformProcess::ComputerName(), *Result.Scenario,
			Result.LinesPerIteration, Result.Iterations, Result.AppendMedianMs, Result.Frame1MedianMs,
			Result.Frame2MedianMs, Result.TotalMedianMs, Result.TotalMaxMs, TextBlocks);
		FFileHelper::SaveStringToFile(Row, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusTimingLogWindowFullBacklogTest,
	"Mobius.RenderedTiming.LogWindow.FullBacklogFrameCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FMobiusTimingLogWindowFullBacklogTest::RunTest(const FString& Parameters)
{
	using namespace MobiusLogWindowTiming;

	if (!FSlateApplication::IsInitialized() || !FApp::CanEverRender())
	{
		AddError(TEXT("Needs Slate and a real renderer: under -nullrhi Slate skips widget drawing, so the ")
			TEXT("redraw half of every scenario would measure nothing. Run with RunTests.ps1 -Rendered."));
		return false;
	}

	TSet<const SWindow*> WindowsBefore;
	for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
	{
		WindowsBefore.Add(&Window.Get());
	}

	TArray<FScenarioResult> Results;
	int32 TextBlocksAtEnd = 0;
	int32 NextLine = 0;

	{
		const TSharedRef<SLogWindowWidget> Widget = SNew(SLogWindowWidget);
		const TSharedPtr<SWindow> Window = FindNewLogWindow(WindowsBefore);
		if (!Window.IsValid())
		{
			AddError(TEXT("Constructing SLogWindowWidget did not open a 'Mobius Log' window."));
			return false;
		}
		const TSharedRef<SWindow> WindowRef = Window.ToSharedRef();

		// The backlog the owner's sessions reach after ~30 minutes: the cap, full.
		TArray<FString> Backlog;
		Backlog.Reserve(BacklogLines);
		for (; NextLine < BacklogLines; ++NextLine)
		{
			Backlog.Add(MakeLine(NextLine));
		}
		Widget->SetLines(Backlog);

		// Warm-up: the first paint establishes the wrap width, the next re-wraps against it, and any
		// scroll-to-end request settles.
		for (int32 Warm = 0; Warm < 3; ++Warm)
		{
			RedrawMs(WindowRef);
		}
		FString SeedDetail;
		if (!IsLineAtViewportBottom(WindowRef, Backlog.Last(), SeedDetail))
		{
			AddError(FString::Printf(TEXT("After seeding and three redraws the newest backlog line is not at the ")
				TEXT("bottom of the view (%s) - the window is not drawing, or not following the end, so nothing ")
				TEXT("below would be a measurement."), *SeedDetail));
			Widget->CloseLogWindow();
			return false;
		}

		// --- idle: nothing new ----------------------------------------------------------------------
		{
			TArray<double> Frames;
			for (int32 Iteration = 0; Iteration < 30; ++Iteration)
			{
				Frames.Add(RedrawMs(WindowRef));
			}
			FScenarioResult& Result = Results.AddDefaulted_GetRef();
			Result.Scenario = TEXT("idle");
			Result.Iterations = Frames.Num();
			Result.Frame1MedianMs = Median(Frames);
			Result.TotalMedianMs = Result.Frame1MedianMs;
			Result.TotalMaxMs = MaxOf(Frames);
		}

		// --- trickle: one line per frame ------------------------------------------------------------
		{
			TArray<double> Appends;
			TArray<double> Frames;
			TArray<double> Totals;
			for (int32 Iteration = 0; Iteration < 120; ++Iteration)
			{
				const FString Line = MakeLine(NextLine++);
				const double Start = FPlatformTime::Seconds();
				Widget->AppendLine(Line);
				const double AppendMs = (FPlatformTime::Seconds() - Start) * 1000.0;
				const double FrameMs = RedrawMs(WindowRef);
				Appends.Add(AppendMs);
				Frames.Add(FrameMs);
				Totals.Add(AppendMs + FrameMs);
			}
			RedrawMs(WindowRef);
			FString Detail;
			TestTrue(TEXT("trickle: the view stays pinned to the newest line"),
				IsLineAtViewportBottom(WindowRef, MakeLine(NextLine - 1), Detail));
			AddInfo(FString::Printf(TEXT("trickle end: %s"), *Detail));

			FScenarioResult& Result = Results.AddDefaulted_GetRef();
			Result.Scenario = TEXT("trickle");
			Result.LinesPerIteration = 1;
			Result.Iterations = Totals.Num();
			Result.AppendMedianMs = Median(Appends);
			Result.Frame1MedianMs = Median(Frames);
			Result.TotalMedianMs = Median(Totals);
			Result.TotalMaxMs = MaxOf(Totals);
		}

		// --- bursts: one logger flush of N lines, then two frames ---------------------------------
		struct FBurst { int32 Lines; int32 Iterations; };
		const FBurst Bursts[] = { { 250, 5 }, { 3600, 3 } };
		for (const FBurst& Burst : Bursts)
		{
			TArray<double> Appends;
			TArray<double> Frame1s;
			TArray<double> Frame2s;
			TArray<double> Totals;
			for (int32 Iteration = 0; Iteration < Burst.Iterations; ++Iteration)
			{
				TArray<FString> Lines;
				Lines.Reserve(Burst.Lines);
				for (int32 Index = 0; Index < Burst.Lines; ++Index)
				{
					Lines.Add(MakeLine(NextLine++));
				}

				const double Start = FPlatformTime::Seconds();
				for (const FString& Line : Lines)
				{
					Widget->AppendLine(Line);
				}
				const double AppendMs = (FPlatformTime::Seconds() - Start) * 1000.0;
				const double Frame1Ms = RedrawMs(WindowRef);
				const double Frame2Ms = RedrawMs(WindowRef);

				FString Detail;
				const bool bPinned = IsLineAtViewportBottom(WindowRef, Lines.Last(), Detail);
				TestTrue(FString::Printf(TEXT("burst %d: the view is pinned to the newest line after two frames (%s)"), Burst.Lines, *Detail),
					bPinned);

				Appends.Add(AppendMs);
				Frame1s.Add(Frame1Ms);
				Frame2s.Add(Frame2Ms);
				Totals.Add(AppendMs + Frame1Ms + Frame2Ms);
			}

			FScenarioResult& Result = Results.AddDefaulted_GetRef();
			Result.Scenario = FString::Printf(TEXT("burst_%d"), Burst.Lines);
			Result.LinesPerIteration = Burst.Lines;
			Result.Iterations = Totals.Num();
			Result.AppendMedianMs = Median(Appends);
			Result.Frame1MedianMs = Median(Frame1s);
			Result.Frame2MedianMs = Median(Frame2s);
			Result.TotalMedianMs = Median(Totals);
			Result.TotalMaxMs = MaxOf(Totals);
		}

		// Fingerprint of the implementation under test: a single whole-backlog text block reads ~3
		// (body + Close label + title); a virtualized list reads roughly its visible row count.
		TextBlocksAtEnd = CountTextBlocks(WindowRef);

		Widget->CloseLogWindow();
	}

	for (const FScenarioResult& Result : Results)
	{
		AddInfo(FString::Printf(TEXT("%-10s lines/iter=%4d iters=%3d | append %8.3f ms | frame1 %8.3f ms | frame2 %8.3f ms | total median %8.3f ms, max %8.3f ms"),
			*Result.Scenario, Result.LinesPerIteration, Result.Iterations, Result.AppendMedianMs,
			Result.Frame1MedianMs, Result.Frame2MedianMs, Result.TotalMedianMs, Result.TotalMaxMs));
		AppendCsvRow(Result, TextBlocksAtEnd);
	}
	AddInfo(FString::Printf(TEXT("text blocks in the log window at the end: %d"), TextBlocksAtEnd));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusLogWindowFollowsEndUnlessScrolledUpTest,
	"Mobius.RenderedTiming.LogWindow.FollowsEndUnlessScrolledUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FMobiusLogWindowFollowsEndUnlessScrolledUpTest::RunTest(const FString& Parameters)
{
	using namespace MobiusLogWindowTiming;

	if (!FSlateApplication::IsInitialized() || !FApp::CanEverRender())
	{
		AddError(TEXT("Needs Slate and a real renderer (geometry only exists once painted). Run with RunTests.ps1 -Rendered."));
		return false;
	}

	TSet<const SWindow*> WindowsBefore;
	for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
	{
		WindowsBefore.Add(&Window.Get());
	}

	{
		const TSharedRef<SLogWindowWidget> Widget = SNew(SLogWindowWidget);
		const TSharedPtr<SWindow> Window = FindNewLogWindow(WindowsBefore);
		if (!Window.IsValid())
		{
			AddError(TEXT("Constructing SLogWindowWidget did not open a 'Mobius Log' window."));
			return false;
		}
		const TSharedRef<SWindow> WindowRef = Window.ToSharedRef();
		const auto Redraw = [&WindowRef](int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				RedrawMs(WindowRef);
			}
		};

		int32 NextLine = 0;
		TArray<FString> Backlog;
		for (; NextLine < BacklogLines; ++NextLine)
		{
			Backlog.Add(MakeLine(NextLine));
		}
		Widget->SetLines(Backlog);
		Redraw(4);

		FString Detail;
		TestTrue(TEXT("opens pinned to the newest line"), IsLineAtViewportBottom(WindowRef, Backlog.Last(), Detail));
		AddInfo(FString::Printf(TEXT("open: %s"), *Detail));

		const TSharedPtr<SWidget> List = FindWidgetOfType(WindowRef, TEXT("SListView"));
		if (!List.IsValid())
		{
			AddError(TEXT("The log window has no SListView - this test drives the virtualized list."));
			Widget->CloseLogWindow();
			return false;
		}

		// --- the reader scrolls up a few notches ----------------------------------------------------
		for (int32 Notch = 0; Notch < 5; ++Notch)
		{
			TestTrue(TEXT("wheel-up is handled by the list"), RouteWheel(List.ToSharedRef(), 1.0f));
			Redraw(2);
		}
		const double ViewportTop = AbsoluteTop(List->GetPaintSpaceGeometry());
		const double ViewportBottom = AbsoluteBottom(List->GetPaintSpaceGeometry());
		TArray<FString> VisibleBefore;
		CollectVisibleLines(List.ToSharedRef(), ViewportTop, ViewportBottom, VisibleBefore);
		TestTrue(TEXT("scrolled up: some lines are on screen"), VisibleBefore.Num() > 0);
		TestFalse(TEXT("scrolled up: the newest line is no longer at the bottom"), IsLineAtViewportBottom(WindowRef, Backlog.Last(), Detail));

		// --- new lines arrive - enough to overflow the cap and trim the front ------------------------
		FString Newest;
		for (int32 Index = 0; Index < 200; ++Index)
		{
			Newest = MakeLine(NextLine++);
			Widget->AppendLine(Newest);
		}
		Redraw(5);
		TestFalse(TEXT("a scrolled-up reader is not yanked to the newest line"), IsLineAtViewportBottom(WindowRef, Newest, Detail));
		TArray<FString> VisibleAfter;
		CollectVisibleLines(List.ToSharedRef(), ViewportTop, ViewportBottom, VisibleAfter);
		if (VisibleBefore.Num() > 0)
		{
			TestTrue(TEXT("the lines being read stay on screen through new lines and a front trim"),
				VisibleAfter.Contains(VisibleBefore[VisibleBefore.Num() / 2]));
		}

		// --- the reader scrolls back down to the end: following resumes -----------------------------
		TestTrue(TEXT("wheel-down is handled by the list"), RouteWheel(List.ToSharedRef(), -1000.0f));
		Redraw(3);
		Newest = MakeLine(NextLine++);
		Widget->AppendLine(Newest);
		Redraw(4);
		TestTrue(TEXT("after scrolling back to the end, new lines are followed again"), IsLineAtViewportBottom(WindowRef, Newest, Detail));
		AddInfo(FString::Printf(TEXT("resumed: %s"), *Detail));

		Widget->CloseLogWindow();
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
