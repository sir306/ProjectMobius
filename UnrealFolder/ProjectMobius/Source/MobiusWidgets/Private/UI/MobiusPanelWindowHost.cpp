// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "UI/MobiusPanelWindowHost.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UI/Theme/UIThemeSubsystem.h"

namespace MobiusPanelWindow
{
	// =============================================================================================
	// WORLD-TEARDOWN CLOSE (2026-08-05)
	//
	// Without this, a panel window OUTLIVES PIE. Measured: stop PIE with either card open and the
	// window stays on the desktop, same HWND, resized to 103x100 — the "collapsed content has zero
	// desired size and an Autosized window sizes to desired" signature. It went away only minutes
	// later, on a garbage-collection pass.
	//
	// The cause is a genuine ownership CYCLE, not a missing call. Both panel classes close from
	// NativeDestruct, but `.WindowPanelContent(Content->TakeWidget())` gives the window a shared ref
	// to the widget's SObjectWidget, which keeps the UUserWidget alive. So the window holds the
	// widget, the widget's destructor is what would close the window, and neither happens until GC
	// breaks the cycle. Adding another NativeDestruct call cannot fix that; the close has to come
	// from OUTSIDE the cycle.
	//
	// FWorldDelegates::OnWorldBeginTearDown rather than FEditorDelegates::EndPIE: this is a Runtime
	// module, EndPIE needs WITH_EDITOR, and the same cycle exists in a packaged build on level travel
	// and shutdown. The runtime delegate fixes both with one hook.
	//
	// The registry holds WEAK handles only, so it never extends any lifetime; it exists purely to
	// know what to close. Entries are removed by the closed-event lambda in Open(), which runs on
	// EVERY close route (title-bar X, Alt+F4, CloseWindow, and this teardown path).
	// =============================================================================================
	namespace
	{
		struct FHostedPanel
		{
			TWeakPtr<SMoveableWindow> Window;
			TWeakObjectPtr<UUserWidget> Content;
		};

		TArray<FHostedPanel> GHostedPanels;
		FDelegateHandle GWorldTearDownHandle;

		void CloseHostedPanelsForWorld(UWorld* World)
		{
			if (GHostedPanels.IsEmpty())
			{
				return;
			}

			// Iterate a COPY: RequestDestroyWindow is synchronous and runs the closed-event lambda inline,
			// which unregisters the entry and therefore mutates GHostedPanels mid-loop.
			TArray<FHostedPanel> Snapshot = GHostedPanels;
			for (const FHostedPanel& Panel : Snapshot)
			{
				const TSharedPtr<SMoveableWindow> Window = Panel.Window.Pin();
				if (!Window.IsValid())
				{
					continue;
				}

				// Skip ONLY a panel that provably belongs to a different, still-live world. Everything else
				// gets closed, deliberately erring that way: a panel whose content was already collected, or
				// whose GetWorld() has gone null because teardown is under way, can only ever paint a dead
				// tree — and an orphaned always-on-top window is a worse outcome than closing one early.
				const UUserWidget* Content = Panel.Content.Get();
				const UWorld* ContentWorld = Content ? Content->GetWorld() : nullptr;
				if (Content && ContentWorld && World && ContentWorld != World)
				{
					continue;
				}

				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef());
				}
			}

			GHostedPanels.RemoveAll([](const FHostedPanel& Panel) { return !Panel.Window.IsValid(); });
		}

		void RegisterHostedPanel(const TSharedPtr<SMoveableWindow>& Window, UUserWidget* Content)
		{
			// Hooked once and deliberately never removed. Unhooking when the registry empties would mean
			// removing a delegate from inside its own broadcast (the teardown path empties it), and this
			// matches how the module already parks process-lifetime statics such as its FAutoConsoleCommands.
			// Caveat: a C++ hot reload would leave this dangling — irrelevant in practice here, because
			// building with the editor open is already forbidden in this project (it hot-reloads and trips
			// the MASS duplicate-shared-fragment assert).
			if (!GWorldTearDownHandle.IsValid())
			{
				GWorldTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddStatic(&CloseHostedPanelsForWorld);
			}

			GHostedPanels.Add(FHostedPanel{ Window, Content });
		}

		/** WindowKey is an IDENTITY key only and is never dereferenced — the window may already be gone. */
		void UnregisterHostedPanel(const SMoveableWindow* WindowKey)
		{
			GHostedPanels.RemoveAll([WindowKey](const FHostedPanel& Panel)
			{
				return !Panel.Window.IsValid() || Panel.Window.Pin().Get() == WindowKey;
			});
		}
	}

	TSharedPtr<SMoveableWindow> Open(UUserWidget* Content, const FText& Title, TFunction<void()> OnClosed)
	{
		if (!Content || !FSlateApplication::IsInitialized())
		{
			return nullptr;
		}

		// A UMG widget can be taken as Slate by ONE parent. These cards ship as designer children of the
		// settings tree (the Custom card sits in RetainerBox_CustomHidden), so detach before taking the
		// widget or the window and the old parent fight over the same SObjectWidget. This is the runtime
		// RemoveFromParent, which is a normal UMG operation — NOT the editor-time WidgetTree mutation that
		// must go through UWidgetTree::RemoveWidget.
		if (Content->GetParent() != nullptr)
		{
			Content->RemoveFromParent();
		}

		// MUST be forced visible here, not left to the caller or to Blueprint.
		// Both cards ship COLLAPSED — they were nested panels that the host graph revealed with a
		// Set Visibility(Visible) node on open. A collapsed widget has a ZERO desired size, and an
		// Autosized window sizes to its content's desired size, so hosting one collapsed produces a
		// correctly-themed title bar over an empty 103x100 grey box (measured, PIE, 2026-08-04).
		// SelfHitTestInvisible rather than Visible: the root is a layout container, and its children still
		// receive clicks — which is also what restores the Custom card's buttons.
		Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		// And ENABLED, for the same reason. The host graph's closed state ran
		// Set Visibility(Collapsed) + Set Is Enabled(false) as a pair, and its open path re-enabled the card;
		// a window host must not inherit that. A disabled UUserWidget still lays out and paints, but it
		// paints through the DISABLED style set and drops all input — which reads as "the panel is greyed
		// out and nothing clicks" rather than as an error. Visibility alone was not enough (measured, PIE).
		Content->SetIsEnabled(true);

		FWindowStyle WindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
		if (const UGameInstance* GameInstance = Content->GetGameInstance())
		{
			// Themed chrome for the CURRENT theme; SWindowTitleBarWidget then polls the palette on paint, so
			// the bar follows a live theme toggle without this being re-run.
			if (const UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				WindowStyle = Theme->GetThemedWindowStyle();
			}
		}
		// SLATE_STYLE_ARGUMENT stores a RAW pointer, so the style must outlive the window — hence a shared
		// holder captured by the closed-event lambda below rather than a stack local.
		const TSharedRef<FWindowStyle> StyleHolder = MakeShared<FWindowStyle>(WindowStyle);

		TSharedPtr<SMoveableWindow> Window;
		SAssignNew(Window, SMoveableWindow)
			.Style(&StyleHolder.Get())
			.Title(Title)
			// Autosized, not UserSized: the brief specifies fixed widths (420 / 640) with height to content,
			// and the panels carry their own width in a SizeBox. ClientSize is deliberately not passed —
			// Autosized ignores it.
			.SizingRule(ESizingRule::Autosized)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.HasCloseButton(true)
			.CreateTitleBar(true)
			.IsTopmostWindow(true)
			// .WindowPanelContent, NOT the default slot: when SMoveableWindow builds a title bar it wraps
			// only _WindowPanelContent and never looks at _Content, so the default slot renders an empty
			// window with a correct title bar (measured in LegalNoticeDialog, see its comment).
			.WindowPanelContent(Content->TakeWidget());

		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
		Window->BringToFront(true);

		// Track it so world teardown can close it — see the registry note at the top of this file. Weak
		// handles only; this does not keep either the window or the panel alive.
		RegisterHostedPanel(Window, Content);

		// Converge every close route on the caller's callback. StyleHolder is captured only to keep the
		// FWindowStyle alive for the window's lifetime (see the raw-pointer note above). WindowKey is an
		// identity key for deregistration and is never dereferenced.
		const SMoveableWindow* const WindowKey = Window.Get();
		Window->GetOnWindowClosedEvent().AddLambda(
			[OnClosed = MoveTemp(OnClosed), StyleHolder, WindowKey](const TSharedRef<SWindow>&)
			{
				// Deregister FIRST: OnClosed calls back into the owning widget, and leaving a stale entry
				// in the registry would let the teardown pass try to destroy an already-destroyed window.
				UnregisterHostedPanel(WindowKey);

				if (OnClosed)
				{
					OnClosed();
				}
			});

		return Window;
	}

	void Close(TSharedPtr<SMoveableWindow>& Window)
	{
		// Clear the caller's handle BEFORE destroying: RequestDestroyWindow is synchronous and runs the
		// closed-event lambda inline, which calls back into the owner. If the member were still set, that
		// callback would see an "open" window and could re-enter this function. Same ordering rule
		// SLogWindowWidget::CloseLogWindow documents.
		const TSharedPtr<SMoveableWindow> WindowToDestroy = Window;
		Window.Reset();

		if (WindowToDestroy.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().RequestDestroyWindow(WindowToDestroy.ToSharedRef());
		}
	}
}
