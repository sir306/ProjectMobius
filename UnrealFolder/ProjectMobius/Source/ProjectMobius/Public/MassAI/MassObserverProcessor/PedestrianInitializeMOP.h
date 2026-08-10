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
#include "MassObserverProcessor.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "PedestrianInitializeMOP.generated.h"

/**
 * 
 */
UCLASS(config=Game)
class PROJECTMOBIUS_API UPedestrianInitializeMOP : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UPedestrianInitializeMOP();

	/**
	 * Breathing (monitoring) height used for wheelchair occupants, in cm.
	 *
	 * Kept as its OWN value rather than folded into the age-band switch because a wheelchair user is
	 * seated — but it DEFAULTS to the same 160 cm the standing-adult path already uses, because the
	 * modelling convention is a single breathing height for all agents. That default is what makes
	 * this safe to land: it ships moving ZERO published FED / visibility / temperature numbers.
	 *
	 * Exposed so a study that wants a seated height (~120 cm) can set one without a code change:
	 *   [/Script/ProjectMobius.PedestrianInitializeMOP]
	 *   WheelchairBreathingHeightCm=120.0
	 *
	 * ⚠️ Changing it MOVES tenability results for wheelchair agents. It selects whether the upper or
	 * lower layer is sampled at each step, so it is an analysis input, not a cosmetic tweak.
	 *
	 * The Child (115) / Elderly (145) / adult (160) literals below are deliberately NOT config-backed
	 * in this pass — making them so would change existing published behaviour for anyone editing the
	 * ini, which is a separate decision.
	 */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Mobius|Tenability")
	float WheelchairBreathingHeightCm = 160.0f;

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash



private:

	void InitializeEntityInfoAgent(int32 InEntityID, FEntityInfoFragment& EntityInfoToAssign);

	void InitializeEntityInfoAgent(FEntityInfoFragment& EntityInfoToAssign, int32 InEntityID, FString InEntityName, FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane, int32 InEntityMap);

	//void SetEntitiesPedestrianMovement(FPedestrianMovementFragment& PedestrianMovementToAssign, FSimMovementSample InSharedMovementData);

	UPROPERTY()
	FMassEntityQuery EntityQuery;
};
