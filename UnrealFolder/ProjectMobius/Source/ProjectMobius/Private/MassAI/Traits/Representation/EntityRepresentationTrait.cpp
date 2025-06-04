// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Traits/Representation/EntityRepresentationTrait.h"
// must includes
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
// fragments we need
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
// Tags we need
#include "MassAI/Tags/MassAITags.h"
// Actor we need
#include "MassAI/Actors/AgentRepresentationActorISM.h"

void UEntityRepresentationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Get the entity manager
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	// Create the agent representation actor
	AAgentRepresentationActorISM* AgentRepresentationActor = NewObject<AAgentRepresentationActorISM>();

	// create the shared fragments
	FAgentRepresentationFragment AgentRepresentationFrag = FAgentRepresentationFragment(AgentRepresentationActor, AgentMesh, AgentMesh, AgentMaterial, AgentSkeletalMesh);

	// Pass data to the fragment
	//TimeStepFragment

	//Get a hash of a FConstStructView of said fragment and store it
	uint32 SharedAgentRepresentationFragHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(AgentRepresentationFrag));

	//Search the Mass Entity subsystem for an identical struct with the hash. If there are none, make a new one with the set fragment.
	FSharedStruct SharedAgentRepresentationFrag =
		EntityManager.GetOrCreateSharedFragment<FAgentRepresentationFragment>(AgentRepresentationFrag);

	// Add the shared fragment to the build context
	BuildContext.AddSharedFragment(SharedAgentRepresentationFrag);

	// Add the tag to the build context
	BuildContext.AddTag<FMassEntityRepresentationTag>();
}
