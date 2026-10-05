// Copyright (c) 2026 ProjectMobius contributors. MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * A themed modal yes/no question.
 *
 * Exists because Mobius has no confirm primitive: UMobiusUserFeedbackSubsystem::ReportError is
 * error-shaped and one-button, and the only other runtime modal is the legal notice, which is
 * bespoke and mandatory. FMessageDialog was deliberately NOT used -- it is unthemed OS chrome and
 * has no precedent anywhere in this codebase.
 *
 * ⚠️ BLOCKING. This uses FSlateApplication::AddModalWindow, whose blocking form runs a nested pump
 * that services Slate ONLY -- the engine tick does not run while it is open. Call it from a user
 * action on the game thread (a file-pick callback), never from a loader thread, a Tick, or any
 * automated path. Automation and the startup preload subsystem cannot answer a prompt, which is
 * why the callers live at the file-pick widgets rather than in the game-instance setters.
 */
namespace MobiusConfirmDialog
{
	/**
	 * Show a modal question and block until the user answers.
	 *
	 * @param WorldContext  Used to reach UUIThemeSubsystem for the CURRENT theme's chrome. If it is
	 *                      unavailable the dialog still opens, on Core's default window style.
	 * @param WindowTitle   Title-bar text.
	 * @param Heading       Short bold line -- what was detected.
	 * @param Body          The question, and any consequence worth stating.
	 * @return true if the user chose Yes. **Defaults to false** for every other outcome, including
	 *         the title-bar close and a window destroyed from underneath -- declining an optional
	 *         extra load is the safe reading of "went away".
	 */
	MOBIUSWIDGETS_API bool ShowYesNo(
		const UObject* WorldContext,
		const FText& WindowTitle,
		const FText& Heading,
		const FText& Body);
}
