// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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

#include "MassAI/Traits/TimeDilationTrait.h"
// must includes
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
// fragments we need
#include "MassAI/Fragments/SimulationTimestepFragment.h"
// Tags we need
#include "MassAI/Tags/MassAITags.h"

void UTimeDilationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Get the entity manager
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	// create the shared fragments
	FSimulationTimeStepFragment SimulationTimeStepFragment = FSimulationTimeStepFragment();
	
	// Pass data to the fragment
	//TimeStepFragment

	//Get a hash of a FConstStructView of said fragment and store it
	uint32 SimulationTimeStepFragmentHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(SimulationTimeStepFragment));

	//Search the Mass Entity subsystem for an identical struct with the hash. If there are none, make a new one with the set fragment.
	FSharedStruct SharedSimulationTimeStepFragment =
		EntityManager.GetOrCreateSharedFragment<FSimulationTimeStepFragment>(SimulationTimeStepFragment);
	
	// Add the shared fragment to the build context
	BuildContext.AddSharedFragment(SharedSimulationTimeStepFragment);

	// Add the tag to the build context
	BuildContext.AddTag<FMassAITimeStepTag>();
	
}
