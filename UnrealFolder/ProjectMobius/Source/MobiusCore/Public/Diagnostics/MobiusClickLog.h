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

MOBIUSCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogMobiusClick, Log, All);

/**
 * Click-path tracing for the "buttons unresponsive / need multiple clicks" investigation.
 *
 * OFF by default — `Mobius.LogClicks 1` turns it on, so the disabled cost is one bool compare and this
 * never violates the project's no-logging-in-hot-paths rule. Every line carries the SAME monotonic click
 * id, so the three sources align without needing timestamps:
 *
 *   SLATE   FSlateApplication's pre-input listener. Fires for EVERY mouse-down BEFORE routing, so a
 *           click that shows up here and nowhere else was either consumed or landed on nothing
 *           interactive. Also names the widget chain under the cursor.
 *   BUTTON  UBaseButton press / release / click, plus Slate rebuilds. `PRESSED` with no `CLICKED` is
 *           the smoking gun: the release landed elsewhere, or the SButton was rebuilt mid-press and
 *           dropped mouse capture (which is what a theme reapply during a press would do).
 *   TRACE   UMobiusControllerSubsystem's world line trace. A TRACE line sharing a click id with a
 *           BUTTON line means the click was handled TWICE (UI *and* world) — double-handling, not
 *           consumption.
 *
 * Lives in MobiusCore (the lowest module involved) so MobiusWidgets and the trace code can both use it
 * without a new dependency edge.
 */
namespace MobiusClickLog
{
	/** True when `Mobius.LogClicks` is non-zero. Call this before building any log string. */
	MOBIUSCORE_API bool IsEnabled();

	/** Current click id — bumped by the Slate listener on each mouse-button-down. */
	MOBIUSCORE_API int32 GetClickId();

	/** Emit one `[#id] SOURCE  message` line. No-op when disabled. */
	MOBIUSCORE_API void Log(const TCHAR* Source, const FString& Message);

	/** Hook/unhook the FSlateApplication pre-input listener. Owner: UMobiusWidgetSubsystem. */
	MOBIUSCORE_API void RegisterSlateListener();
	MOBIUSCORE_API void UnregisterSlateListener();
}
