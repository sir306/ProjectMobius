// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Traits/DebugEntityLocationTrait.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
// fragments we need
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/PedestrianMovementFragment.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"

void UDebugEntityLocationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Add fragments to the build context
	BuildContext.AddFragment<FEntityInfoFragment>();
	BuildContext.AddFragment<FPedestrianMovementFragment>();

	// Get the entity manager
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	// create the shared fragments
	FSimulationFragment SimulationFragment = FSimulationFragment();

	//Get a hash of a FConstStructView of said fragment and store it
	uint32 SimulationFragmentHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(SimulationFragment));

	//Search the Mass Entity subsystem for an identical struct with the hash. If there are none, make a new one with the set fragment.
	FSharedStruct SharedSimulationFragment =
		EntityManager.GetOrCreateSharedFragment<FSimulationFragment>(SimulationFragment);

	// Add the shared fragment to the build context
	BuildContext.AddSharedFragment(SharedSimulationFragment);

	// Add the tag to the build context
}
