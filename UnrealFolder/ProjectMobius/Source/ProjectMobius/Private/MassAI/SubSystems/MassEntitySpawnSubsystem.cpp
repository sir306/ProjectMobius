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

#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "MassCommonFragments.h"
#include "MassSpawnerSubsystem.h"
// Fragments
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
// Actors to include
#include "MassAI/Actors/AgentRepresentationActorISM.h"
// Other Subsystems we want to use
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "Subsystems/LoadingSubsystem.h"
// GameInstance
#include "SkeletalMeshAttributes.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "Subsystems/TimeDilationSubSystem.h"
// Niagara
#include "MassExecutionContext.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraDataFrag.h"

class UTimeDilationSubSystem;

UMassEntitySpawnSubsystem::UMassEntitySpawnSubsystem() :
	SpawnedEntityPedestrianHandles(TArray<FMassEntityHandle>()),
	PedestrianTemplateData(FMassEntityTemplateData()),
	AgentDataSubsystem(nullptr)
{
}

void UMassEntitySpawnSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{

	// Add the AgentDataSubsystem to the collection Dependency
	AgentDataSubsystem = Collection.InitializeDependency<UAgentDataSubsystem>();
	
	// Get the entity manager from the MassSubsystem
	//EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().AsShared();

	// If we have other subsystems that we depend on we can initialize them here before super
	Super::Initialize(Collection);

	// Get the Game Instance 
	if(UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld()))
	{
		// Bind the required Game Instance Delegates
		GameInst->OnPedestrianVectorFileUpdated.AddDynamic(this, &UMassEntitySpawnSubsystem::CreatePedestrianTemplateData);
		// log that it has bound
		UE_LOG(LogTemp, Warning, TEXT("Data file Changed Delegate Bound"));

		// Get the Current Data File set on the instance
		//JSONDataFile = GameInst->GetPedestrianDataFilePath();
		//GetJSONDataFile(JSONDataFile);
	}
	// only create the archetype if we are in world
	else if (GetWorld()->IsGameWorld())
	{
		// Create the template data for the pedestrian entity archetype
		//CreatePedestrianTemplateData();
		// Create the pedestrian archetype and relevant data -- we call this here as we only want to create the archetype once
		//TODO: add a bool to check if the archetype has been created and if so, don't create it again
		//CreatePedestrianArchetype();
	}

}

void UMassEntitySpawnSubsystem::Deinitialize()
{
	// If we have delegates we can unbind them here before super
	Super::Deinitialize();
}

void UMassEntitySpawnSubsystem::SpawnMassEntityPedestrians(int32 NumberOfPedestriansToSpawn, FMassArchetypeSharedFragmentValues ArchetypeSharedFragmentValues)
{
	auto PedestrianArchetypeHandle = CreatePedestrianArchetype();
	
	// check shared fragment values are sorted and sort if not
	// -- this has been debugged and is redundant but in place as a safety measure
	if (!ArchetypeSharedFragmentValues.IsSorted())
	{
		ArchetypeSharedFragmentValues.Sort();
	}

	//TODO: We dont want to simulate time till this is done, also we need a better way to build shared fragment and update the archetype on data changes
	EntityManager->BatchCreateEntities(PedestrianArchetypeHandle, ArchetypeSharedFragmentValues, NumberOfPedestriansToSpawn, SpawnedEntityPedestrianHandles);
}

void UMassEntitySpawnSubsystem::SpawnMaxPedestrians(FMassArchetypeSharedFragmentValues ArchetypeSharedFragmentValues)
{
	int32 MaxPedestrians = AgentDataSubsystem->GetMaxAgents();

	if(MaxPedestrians > 0)
	{
		SpawnMassEntityPedestrians(MaxPedestrians, ArchetypeSharedFragmentValues);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Max Pedestrians is less than 0, likely a bad data file."));
	}
	
}

void UMassEntitySpawnSubsystem::DestroySpawnedPedestrians(TConstArrayView<FMassEntityHandle> EntitiesToDestroy)
{
}

FMassArchetypeHandle UMassEntitySpawnSubsystem::CreatePedestrianArchetype()
{
	// TemplateBuildContext
	FMassEntityTemplateBuildContext PedestrianLocationTraitBuildContext = FMassEntityTemplateBuildContext(PedestrianTemplateData);

	// Get this traits template id
	FMassEntityTemplateID DebugEntityLocationTraitID = PedestrianLocationTraitBuildContext.GetTemplateID();
	
	TemplateRegistryInstance.FindOrAddTemplate(DebugEntityLocationTraitID, MoveTemp(PedestrianTemplateData));
	
	auto PedestrianArchetypeHandle = EntityManager->CreateArchetype(PedestrianTemplateData.GetCompositionDescriptor());

	return PedestrianArchetypeHandle;
}

void UMassEntitySpawnSubsystem::CreatePedestrianTemplateData()
{
	// check if the template data and archetype handle are not null
	if (!PedestrianTemplateData.IsEmpty() || !SpawnedEntityPedestrianHandles.IsEmpty())
	{
		// log that the data is already created
		UE_LOG(LogTemp, Warning, TEXT("PedestrianTemplateData Already Created"));

		EntityManager->BatchDestroyEntities(SpawnedEntityPedestrianHandles);
		// Clear the associated data for the entity manager, if we don't then the entity manager will keep the data in memory
		EntityManager->CreateExecutionContext(GetWorld()->GetDeltaSeconds()).ClearExecutionData();
		EntityManager->CreateExecutionContext(GetWorld()->GetDeltaSeconds()).ClearEntityCollection();
		EntityManager->CreateExecutionContext(GetWorld()->GetDeltaSeconds()).FlushDeferred();
		EntityManager->FlushCommands();
		//EntityManager.Reset();
	}

	auto* ExistingActor = Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass()));
	if (ExistingActor)
	{
		ExistingActor->GetNiagaraComponent()->ClearSimCache(true);
		ExistingActor->GetNiagaraComponent()->DeactivateImmediate();
		ExistingActor->GetNiagaraComponent()->DestroyInstanceNotComponent();
	}

	
	// Get time now
	float RealtimeSeconds = UGameplayStatics::GetRealTimeSeconds(GetWorld());

	// Reset the template data
	PedestrianTemplateData = FMassEntityTemplateData();

	// We have to force a garbage collection here to ensure that the old data is cleared from memory before new
	// data is created
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	
	float elapsedTime = UGameplayStatics::GetRealTimeSeconds(GetWorld()) - RealtimeSeconds;
	
	LoadPedestrianData();
	elapsedTime = UGameplayStatics::GetRealTimeSeconds(GetWorld()) - elapsedTime;
	// log time taken
	UE_LOG(LogTemp, Warning, TEXT("Time taken to load data: %f"), elapsedTime);
}

void UMassEntitySpawnSubsystem::LoadPedestrianData()
{
	FString JSONDataFile = "";
	// Get the Game Instance 
	if(UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld()))
	{
		// do we have a file to use from the game instance
		JSONDataFile = GameInst->GetPedestrianDataFilePath();
	}
	
	// Check Agent Data Subsystem is valid
	if (!AgentDataSubsystem)
	{
		// error log
		UE_LOG(LogTemp, Error, TEXT("Agent Data Subsystem is not valid"));
	}

	// get the mobius widget subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();

	if (FJsonDataRunnable* ToKill = AgentDataSubsystem->JsonDataRunnable)
	{
		// 1) Clear the member now so no one else ever re-uses it
		AgentDataSubsystem->JsonDataRunnable = nullptr;

		// 2) Unbind your delegates immediately
		ToKill->OnLoadSimulationDataComplete.RemoveDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);
		ToKill->OnMaxAgentCount.RemoveDynamic(AgentDataSubsystem, &UAgentDataSubsystem::UpdateMaxAgentCount);
		if (auto* LS = GetWorld()->GetSubsystem<ULoadingSubsystem>())
		{
			ToKill->OnLoadSimulationDataProgress.RemoveDynamic(LS, &ULoadingSubsystem::BroadcastNewLoadPercent);
		}
		

		// 3) Schedule deletion of *that* runnable
		AsyncTask(ENamedThreads::GameThread, [ToKill]()
		{
			ToKill->Stop();
			ToKill->Exit();
			delete ToKill;  // safe: destructor will join + free the thread
			UE_LOG(LogTemp, Warning, TEXT("Pedestrian Data Runnable Deleted"));
		});
	}

	// Get the JSON Data File using the FRunnable class to get the data asynchronously
	AgentDataSubsystem->JsonDataRunnable = new FJsonDataRunnable(JSONDataFile);
	AgentDataSubsystem->JsonDataRunnable->OnLoadSimulationDataComplete.AddDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);
	AgentDataSubsystem->JsonDataRunnable->OnMaxAgentCount.AddDynamic(AgentDataSubsystem, &UAgentDataSubsystem::UpdateMaxAgentCount);

	// check if the widget subsystem is valid
	if (LoadingSubsystem)
	{
		// bind current load percent
		AgentDataSubsystem->JsonDataRunnable->OnLoadSimulationDataProgress.AddDynamic(LoadingSubsystem, &ULoadingSubsystem::BroadcastNewLoadPercent);

		// get file name from the json data file
		FString FileName = FPaths::GetCleanFilename(JSONDataFile);

		FString LoadingText = FString::Printf(TEXT("File: %s"), *FileName);
		
		// Set the loading text and title
		LoadingSubsystem->SetLoadingTextAndTitle("Loading Pedestrian Vectors", LoadingText);
	}

	
}

void UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData()
{
	if (AgentDataSubsystem->JsonDataRunnable != nullptr)
	{
		AgentDataSubsystem->JsonDataRunnable->Stop();
	}
	//UE_LOG(LogTemp, Warning, TEXT("Building Pedestrian Movement Fragment Data"));
	PedestrianTemplateData.AddFragment<FEntityInfoFragment>();
	PedestrianTemplateData.AddFragment<FEntityMovementFragment>();
	PedestrianTemplateData.AddFragment<FEntityRenderingFragment>();

	// ensure a new fragment is created as not to get clashes
	auto SimulationFragment = FSimulationFragment();
	
	// create the shared fragments
	SimulationFragment = MoveTemp(AgentDataSubsystem->JsonDataRunnable->AgentMovementInfoData);

	NumOfAgentsPerTimeStep = TArray<int32>();
	NumOfAgentsPerTimeStep.SetNum(SimulationFragment.SimulationData.Num());
	
	// map the number of agents per time step
	for (int32 i = 0; i < SimulationFragment.SimulationData.Num(); i++)
	{
		NumOfAgentsPerTimeStep[i] = SimulationFragment.SimulationData[i].Num();
	}

	// log i 0 for the number of agents per time step
	UE_LOG(LogTemp, Warning, TEXT("Number of Agents Per Time Step: %d"), NumOfAgentsPerTimeStep[0]);

	// Set the json object on the agent data subsystem
	AgentDataSubsystem->JSONObject = MoveTemp(AgentDataSubsystem->JsonDataRunnable->JSONObject);

	float TimeBetweenSteps = MoveTemp(AgentDataSubsystem->JsonDataRunnable->TimeBetweenSteps);

	// Get Time Dilation from the ProjectMobius Game Instance
	UTimeDilationSubSystem* TimeDilationSubSystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

	// update time between steps
	TimeDilationSubSystem->UpdateTimeBetweenData(TimeBetweenSteps);

	// Update the total time for the Time Dilation Subsystem - which also updates the max time steps
	TimeDilationSubSystem->UpdateTotalTime(SimulationFragment.MaxTime);

	auto SharedSimulationFragment = FSharedStruct::Make(SimulationFragment);

	// Add the shared fragment to the build context
	PedestrianTemplateData.AddSharedFragment(SharedSimulationFragment);
	
	// Create the Pedestrian Representation Fragment Data
	BuildPedestrianRepresentationFragmentData();
		
	auto ArchetypeSharedFragmentValues = PedestrianTemplateData.GetSharedFragmentValues();

	// check shared fragment values are sorted and sort if not
	if (!ArchetypeSharedFragmentValues.IsSorted())
	{
		ArchetypeSharedFragmentValues.Sort();
	}

	// Broadcast that the pedestrian data is ready to spawn
	OnPedestrianDataReadyToSpawn.Broadcast();

	AgentDataSubsystem->JsonDataRunnable->Exit();
	
	// At this point data should be ready to spawn
	SpawnMaxPedestrians(ArchetypeSharedFragmentValues);
	
	if (FJsonDataRunnable* ToKill = AgentDataSubsystem->JsonDataRunnable)
	{
		// 1) Clear the member now so no one else ever re-uses it
		AgentDataSubsystem->JsonDataRunnable = nullptr;

		// 2) Unbind your delegates immediately
		ToKill->OnLoadSimulationDataComplete.RemoveDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);
		ToKill->OnMaxAgentCount.RemoveDynamic(AgentDataSubsystem, &UAgentDataSubsystem::UpdateMaxAgentCount);
		if (auto* LS = GetWorld()->GetSubsystem<ULoadingSubsystem>())
		{
			ToKill->OnLoadSimulationDataProgress.RemoveDynamic(LS, &ULoadingSubsystem::BroadcastNewLoadPercent);
		}

		// 3) Schedule deletion of *that* runnable
		AsyncTask(ENamedThreads::GameThread, [ToKill, this]()
		{
			ToKill->Stop();
			ToKill->Exit();

			// log exit done
			UE_LOG(LogTemp, Warning, TEXT("Pedestrian Data Runnable Exit Done"));

			while (!ToKill->bReadyToDelete)
			{
				// wait for the runnable to be ready to delete
				FPlatformProcess::Sleep(0.01f); // sleep for a short time to avoid busy waiting
			}
			delete ToKill;  // safe: destructor will join + free the thread
			// log that the runnable has been deleted
			UE_LOG(LogTemp, Warning, TEXT("Pedestrian Data Runnable Deleted"));			
			
			// Reset the template data
			PedestrianTemplateData = FMassEntityTemplateData();

			// We have to force a garbage collection here to ensure that the old data is cleared from memory before new
			// data is created
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			UE_LOG(LogTemp, Warning, TEXT("Pedestrian Data cleared"));
		});
	}
}

void UMassEntitySpawnSubsystem::BuildPedestrianRepresentationFragmentData()
{
	// Removed the old fragment logic - it is commented out below as the logic may be useful in the future

	// // we only want to add the shared fragment if it doesn't already exist
	// if(!PedestrianTemplateData.HasSharedFragment<FAgentRepresentationFragment>())
	// {
	// 	//TODO: Refactor this code we don't use the ISM component anymore and this code is messy and will be better if cleaned
	// 	
	// 	// Create the agent representation actor
	// 	AAgentRepresentationActorISM* AgentRepresentationActor = NewObject<AAgentRepresentationActorISM>(GetWorld(), TEXT("ActorRepresentationClass"));
	//
	// 	UStaticMesh* MaleAgentMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("StaticMesh'/Game/MakeHuman/Male/SM_MakeHuman.SM_MakeHuman'")));
	// 	
	// 	UStaticMesh* FemaleAgentMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("StaticMesh/Script/Engine.StaticMesh'/Game/MakeHuman/Female/SM_MakeHumanFemale.SM_MakeHumanFemale'"))); 
	// 	
	// 	UMaterial* AgentMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("Material'/Game/MakeHuman/Male/Skeleton/Human_body_003.Human_body_003'")));
	//
	// 	USkeletalMesh* AgentSkeletalMesh = Cast<USkeletalMesh>(StaticLoadObject(USkeletalMesh::StaticClass(), NULL, TEXT("SkeletalMesh'/Game/MakeHuman/Male/Skeleton/MakeHumanMaleLowPoly_Skeleton.MakeHumanMaleLowPoly_Skeleton'")));
	// 	
	// 	// create the shared fragments
	// 	AgentRepresentationFragment = FAgentRepresentationFragment(AgentRepresentationActor, MaleAgentMesh, FemaleAgentMesh, AgentMaterial, AgentSkeletalMesh);
	//
	// 	SharedAgentRepresentationFrag = FSharedStruct::Make(AgentRepresentationFragment);
	//
	// 	// Add the shared fragment to the build context
	// 	PedestrianTemplateData.AddSharedFragment(SharedAgentRepresentationFrag);
	// }

	// check to see if the Niagara stats Fragment is already in the template data
	if (!PedestrianTemplateData.HasSharedFragment<FNiagaraStatsFragment>())
	{
		// create the shared fragment
		auto NiagaraSharedStatsFrag = FSharedStruct::Make(FNiagaraStatsFragment()); // We can add specific data to this later

		// Add the shared fragment to the build context
		PedestrianTemplateData.AddSharedFragment(NiagaraSharedStatsFrag);
	}

	// check to see if the Niagara data Fragment is already in the template data
	if (!PedestrianTemplateData.HasSharedFragment<FAgentNiagaraDataFrag>())
	{
		// create the shared fragment
		auto NiagaraSharedDataFrag = FSharedStruct::Make(FAgentNiagaraDataFrag()); // We can add specific data to this later

		// Add the shared fragment to the build context
		PedestrianTemplateData.AddSharedFragment(NiagaraSharedDataFrag);
	}
	
}
