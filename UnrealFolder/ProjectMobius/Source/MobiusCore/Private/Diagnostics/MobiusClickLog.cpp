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

#include "Diagnostics/MobiusClickLog.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"

DEFINE_LOG_CATEGORY(LogMobiusClick);

namespace
{
	void HandleLogClicksChanged(IConsoleVariable* Variable);

	TAutoConsoleVariable<int32> CVarMobiusLogClicks(
		TEXT("Mobius.LogClicks"),
		0,
		TEXT("Trace the click path for the unresponsive-button investigation (0 = off).\n")
		TEXT("Logs to LogMobiusClick: SLATE (pre-input mouse-down + widget chain), BUTTON (press/release/\n")
		TEXT("click/rebuild per UBaseButton) and TRACE (world line trace). Lines share a click id.\n")
		TEXT("Toggling this hooks/unhooks the Slate listener live — no PIE restart needed."),
		FConsoleVariableDelegate::CreateStatic(&HandleLogClicksChanged));

	/** Game-thread only: bumped by the Slate listener, read by every other source. */
	int32 GClickId = 0;

	FDelegateHandle GSlateListenerHandle;

	/** How many widgets of the hit path to name, leaf first. Enough to see button + its container. */
	constexpr int32 GMaxPathEntries = 5;

	void HandleSlateMouseButtonDown(const FPointerEvent& MouseEvent)
	{
		if (!MobiusClickLog::IsEnabled())
		{
			return;
		}

		// This listener runs BEFORE Slate routes the event, so the id is allocated here and every later
		// line (button, world trace) reports against the same click.
		++GClickId;

		FSlateApplication& SlateApp = FSlateApplication::Get();
		const FVector2D ScreenPos(MouseEvent.GetScreenSpacePosition());

		FString Chain;
		bool bHitSButton = false;
		FWidgetPath HitPath = SlateApp.LocateWindowUnderMouse(ScreenPos, SlateApp.GetInteractiveTopLevelWindows());
		if (HitPath.IsValid())
		{
			const int32 NumWidgets = HitPath.Widgets.Num();
			for (int32 Index = NumWidgets - 1; Index >= 0 && (NumWidgets - Index) <= GMaxPathEntries; --Index)
			{
				const TSharedRef<SWidget>& PathWidget = HitPath.Widgets[Index].Widget;
				const FString TypeName = PathWidget->GetTypeAsString();
				if (TypeName.Contains(TEXT("SButton")))
				{
					bHitSButton = true;
				}
				Chain += Chain.IsEmpty() ? TypeName : FString(TEXT(" < ")) + TypeName;
			}
		}

		UE_LOG(LogMobiusClick, Log, TEXT("[#%d] SLATE   %s down at (%.0f,%.0f)  hitSButton=%s  path= %s"),
			GClickId, *MouseEvent.GetEffectingButton().ToString(), ScreenPos.X, ScreenPos.Y,
			bHitSButton ? TEXT("YES") : TEXT("no"),
			Chain.IsEmpty() ? TEXT("<nothing interactive under cursor>") : *Chain);
	}

	void HandleLogClicksChanged(IConsoleVariable* Variable)
	{
		if (Variable && Variable->GetInt() != 0)
		{
			MobiusClickLog::RegisterSlateListener();
		}
		else
		{
			MobiusClickLog::UnregisterSlateListener();
		}
	}
}

bool MobiusClickLog::IsEnabled()
{
	return CVarMobiusLogClicks.GetValueOnAnyThread() != 0;
}

int32 MobiusClickLog::GetClickId()
{
	return GClickId;
}

void MobiusClickLog::Log(const TCHAR* Source, const FString& Message)
{
	if (!IsEnabled())
	{
		return;
	}

	UE_LOG(LogMobiusClick, Log, TEXT("[#%d] %-7s %s"), GClickId, Source, *Message);
}

void MobiusClickLog::RegisterSlateListener()
{
	// Dormant unless asked for: with `Mobius.LogClicks 0` (the default) nothing is hooked at all, so the
	// diagnostics cost literally nothing per mouse-down. The cvar's OnChanged sink hooks it the moment
	// the flag is raised, so re-enabling it is a console command, not a rebuild.
	if (!IsEnabled() || !FSlateApplication::IsInitialized() || GSlateListenerHandle.IsValid())
	{
		return;
	}

	GSlateListenerHandle = FSlateApplication::Get().OnApplicationMousePreInputButtonDownListener()
		.AddStatic(&HandleSlateMouseButtonDown);
}

void MobiusClickLog::UnregisterSlateListener()
{
	if (!GSlateListenerHandle.IsValid())
	{
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationMousePreInputButtonDownListener().Remove(GSlateListenerHandle);
	}
	GSlateListenerHandle.Reset();
}
