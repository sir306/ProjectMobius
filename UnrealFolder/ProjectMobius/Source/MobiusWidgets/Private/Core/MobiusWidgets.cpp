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

#include "Core/MobiusWidgets.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Style/MobiusStyle.h"
#include "UI/LegalNoticeDialog.h"
#include "UserConfig/UserProjectSettings.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FMobiusWidgetsModule"

void FMobiusWidgetsModule::StartupModule()
{
    // Register before any widget constructs (module is Runtime/Default-phase) so native widgets can
    // rely on FMobiusStyle::Get() as their style fallback.
    FMobiusStyle::Initialize();

    // MobiusCore deliberately owns only the accepted-version setting. The Widgets module owns the
    // actual presentation and opens it once a packaged game world (and therefore Slate viewport) exists.
    LegalNoticePostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FMobiusWidgetsModule::HandlePostLoadMap);
}

void FMobiusWidgetsModule::ShutdownModule()
{
    if (LegalNoticePostLoadMapHandle.IsValid())
    {
        FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(LegalNoticePostLoadMapHandle);
        LegalNoticePostLoadMapHandle.Reset();
    }

    FMobiusStyle::Shutdown();
}

void FMobiusWidgetsModule::HandlePostLoadMap(UWorld* LoadedWorld)
{
    if (GIsEditor || FApp::IsUnattended() || !LoadedWorld || !LoadedWorld->IsGameWorld())
    {
        return;
    }

    // PostLoadMap occurs before all first-frame viewport work has completed. Defer one tick so the
    // application has a stable native window to own the modal; the user still cannot interact with
    // normal application UI before it appears.
    LoadedWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([]()
    {
        if (UUserProjectSettings* UserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
        {
            MobiusLegalNotice::ShowIfRequired(*UserSettings);
        }
    }));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMobiusWidgetsModule, MobiusWidgets)
