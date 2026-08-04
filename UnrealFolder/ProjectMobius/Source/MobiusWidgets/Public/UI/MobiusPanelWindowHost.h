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

#pragma once

#include "CoreMinimal.h"

class SMoveableWindow;
class UUserWidget;

/**
 * Hosts a UMG panel in a themed, draggable SMoveableWindow — the phase-2 home for the Settings and
 * Custom Display Settings cards, which previously lived nested in the viewport widget tree.
 *
 * Why a free function pair rather than a subsystem: nothing in Mobius loads a WBP class from C++ (verified
 * by sweep — there is no TSoftClassPtr/LoadClass of any /Game widget anywhere in Source), so a spawner
 * subsystem would have to hardcode content paths. The widget instances stay Blueprint-created; C++ only
 * takes an existing instance and gives it a window. That keeps the BP rewire to one node per entry point.
 */
namespace MobiusPanelWindow
{
	/**
	 * Opens Content as the body of a themed SMoveableWindow, autosized to the panel.
	 *
	 * @param Content   the panel. If it is currently slotted in another widget tree it is detached first —
	 *                  a UMG widget can only be taken as Slate by one parent.
	 * @param Title     native title-bar text. The cards' own in-tree title bars should be collapsed.
	 * @param OnClosed  runs on EVERY close route (title-bar X, Alt+F4, OS close). Capture weakly.
	 * @return the window, or null if Slate is unavailable / Content is null.
	 */
	MOBIUSWIDGETS_API TSharedPtr<SMoveableWindow> Open(
		UUserWidget* Content,
		const FText& Title,
		TFunction<void()> OnClosed);

	/** Destroys Window and clears the handle. Safe when already closed. Touch NOTHING after this call. */
	MOBIUSWIDGETS_API void Close(TSharedPtr<SMoveableWindow>& Window);
}
