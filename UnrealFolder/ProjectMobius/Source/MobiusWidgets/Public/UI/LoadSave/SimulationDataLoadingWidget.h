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
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "SimulationDataLoadingWidget.generated.h"

/**
 * Parent for `WBP_LoadDataFiles` — the FILE-LOADING panel (agent vectors, geometry, B-Risk inputs).
 *
 * Created 2026-08-11 by the simulation-widget unification, to fix a naming inversion the owner called out as
 * *"bad coding practice"*: `WBP_LoadDataFiles` used to parent to `USimulationSettingsWidget`, so the class
 * named for the SETTINGS was the one behind the LOADING asset, while a second class in another module owned
 * the actual settings. Settings now all live in `USimulationSettingsWidget`; this class is the loading
 * panel's own home.
 *
 * Intentionally behaviour-free. The individual file rows are their own widgets (`ULoadAgentDataWidget`,
 * `ULoadMeshWidget`, `ULoadBRiskDataWidget`) and own their logic; this is the themed container they sit in.
 * Deriving `UMobiusThemedUserWidget` is the whole point — it gives the panel the standard-control theming
 * pass on construct and on every `OnThemeChanged`, which is what the previous arrangement could not do from
 * `ProjectMobius` without a module cycle.
 *
 * `WBP_LoadDataFiles` is reparented to this class EXPLICITLY in the editor, deliberately NOT through a
 * `[CoreRedirects]` entry: the asset is being re-homed to a different class, which is not the same thing as
 * a class being renamed, and a redirect would also have collided with the one that carries
 * `SimulationSetupWidget` onto `USimulationSettingsWidget`.
 */
UCLASS()
class MOBIUSWIDGETS_API USimulationDataLoadingWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()
};
