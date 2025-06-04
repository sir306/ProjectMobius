// Fill out your copyright notice in the Description page of Project Settings.


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
