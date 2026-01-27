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
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "Subsystems/TimeDilationSubSystem.h"
// Niagara
#include "MassExecutionContext.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/CapsuleComponent.h"
#include "MassAI/Fragments/EntityTags/PedestrianCollisionTags.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraDataFrag.h"
#include "MassAI/SubSystems/PedestrianSignalSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"

class UTimeDilationSubSystem;

UMassEntitySpawnSubsystem::UMassEntitySpawnSubsystem() :
        SpawnedEntityPedestrianHandles(TArray<FMassEntityHandle>()),
        PedestrianTemplateData(FMassEntityTemplateData()),
        AgentDataSubsystem(nullptr),
        bHasResetFlowCounters(false)
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

void UMassEntitySpawnSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	// Ensure rep actor exists BEFORE Mass creates entities
	if (UWorld* World = GetWorld())
	{
		if (!UGameplayStatics::GetActorOfClass(World, ANiagaraAgentRepActor::StaticClass()))
		{
			const FTransform T = FTransform::Identity;
			ANiagaraAgentRepActor* Rep =
				World->SpawnActorDeferred<ANiagaraAgentRepActor>(ANiagaraAgentRepActor::StaticClass(), T);
			UGameplayStatics::FinishSpawningActor(Rep, T);
		}
	}
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

        if (!bHasResetFlowCounters)
        {
                if (auto StatSubsystem = GetWorld()->GetSubsystem<UStatisticSubsystem>())
                {
                        StatSubsystem->ResetFlowCounters();
                        bHasResetFlowCounters = true;
                }
        }

	//TODO: We dont want to simulate time till this is done, also we need a better way to build shared fragment and update the archetype on data changes
	EntityManager->BatchCreateEntities(PedestrianArchetypeHandle, ArchetypeSharedFragmentValues, NumberOfPedestriansToSpawn, SpawnedEntityPedestrianHandles);

        // Cleanup any existing runnable to avoid memory leaks
        AgentDataRunnableCleanup(AgentDataSubsystem->JsonDataRunnable);
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
	if (EntitiesToDestroy.IsEmpty())
	{
		return;
	}

	EntityManager->BatchDestroyEntities(EntitiesToDestroy);
	EntityManager->CreateExecutionContext(GetWorld()->GetDeltaSeconds()).FlushDeferred();
	EntityManager->FlushCommands();
}

void UMassEntitySpawnSubsystem::DestroyAllSpawnedPedestrians()
{
        // check if the template data and archetype handle are not null
        if (!PedestrianTemplateData.IsEmpty() || !SpawnedEntityPedestrianHandles.IsEmpty())
        {
                // log that the data is already created
                UE_LOG(LogTemp, Warning, TEXT("PedestrianTemplateData Already Created"));

                EntityManager->BatchDestroyEntities(SpawnedEntityPedestrianHandles);
                // Clear the associated data for the entity manager, if we don't then the entity manager will keep the data in memory
                FMassExecutionContext ExecutionContext = EntityManager->CreateExecutionContext(GetWorld()->GetDeltaSeconds());
                ExecutionContext.ClearExecutionData();
                ExecutionContext.ClearEntityCollection();
                ExecutionContext.FlushDeferred();
                EntityManager->FlushCommands();
                //EntityManager.Reset();
        }
}

void UMassEntitySpawnSubsystem::ClearNiagaraSim()
{
	auto* ExistingActor = Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass()));
	if (ExistingActor)
	{
		ExistingActor->GetNiagaraComponent()->ClearSimCache(true);
		ExistingActor->GetNiagaraComponent()->DeactivateImmediate();
		ExistingActor->GetNiagaraComponent()->DestroyInstanceNotComponent();
	}
}

void UMassEntitySpawnSubsystem::AgentDataRunnableCleanup(TUniquePtr<FProcessSimulationDataRunnable>& ToKill)
{
        if (!ToKill) return;

	// 1) Stop on calling thread
	ToKill->Stop();

	// 2) Join/Exit on calling thread (don’t bounce to GT). Ensure the runnable sets a “finished” flag.
	ToKill->Exit();

	// 3) Now it’s safe to unbind dynamic delegates on the subsystem (they’re not being used by the worker anymore)
	if (auto* LS = GetWorld()->GetSubsystem<ULoadingSubsystem>()) {
		AgentDataSubsystem->OnLoadSimulationDataProgress.RemoveDynamic(LS, &ULoadingSubsystem::BroadcastNewLoadPercent);
	}
	AgentDataSubsystem->OnLoadSimulationDataComplete.RemoveDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);
	//AgentDataSubsystem->OnMaxAgentCount.RemoveDynamic(AgentDataSubsystem, &UAgentDataSubsystem::UpdateMaxAgentCount);

        // 4) Delete
        ToKill.Reset();
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
	// get the mobius widget subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();
	
	// check if the widget subsystem is valid
	if (LoadingSubsystem)
	{

		FString LoadingText = FString::Printf(TEXT("Clearing Old Data..."));
		
		// Set the loading text and title
		LoadingSubsystem->SetLoadingText(true, LoadingText);
	}
	
        // Cleanup any existing runnable to avoid memory leaks
        AgentDataRunnableCleanup(AgentDataSubsystem->JsonDataRunnable);

        bHasResetFlowCounters = false;
	
	// as our capsule objects are bound to the world and the world is never destroyed, we need to ensure that the
	// capsule components are cleared and marked for destruction so that we don't have memory leaks
	for (auto& EntityHandle : SpawnedEntityPedestrianHandles)
	{
		FEntityCollisionFragment Fragment = EntityManager->GetFragmentDataChecked<FEntityCollisionFragment>(EntityHandle);
		if (Fragment.Capsule.IsValid())
		{
			Fragment.Capsule->DestroyComponent();
		}
	}
	
	// Destroy any existing spawned pedestrians and clear the Niagara simulation
	DestroyAllSpawnedPedestrians();
	ClearNiagaraSim();

	// Empty out the handles array
	SpawnedEntityPedestrianHandles.Empty();
	
	// We have to force a garbage collection here to ensure that the old data is cleared from memory before new
	// data is created
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	// Reset the template data
	PedestrianTemplateData = FMassEntityTemplateData();
	
	LoadPedestrianData();
}

void UMassEntitySpawnSubsystem::LoadPedestrianData()
{
	// get the mobius widget subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();
	
	// check if the widget subsystem is valid
	if (LoadingSubsystem)
	{

		FString LoadingText = FString::Printf(TEXT("Fetching Pedestrian Data File..."));
		
		// Set the loading text and title
		LoadingSubsystem->SetLoadingText(true, LoadingText);
	}
	
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
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Spawn Error"),
				FText::FromString("Agent data subsystem missing"),
				FText::FromString("Agent Data Subsystem is not valid."),
				FText::FromString("MassEntitySpawnSubsystem"));
		}
		// error log
		UE_LOG(LogTemp, Error, TEXT("Agent Data Subsystem is not valid"));
		return;
	}

	// Cleanup any existing runnable to avoid memory leaks
	AgentDataRunnableCleanup(AgentDataSubsystem->JsonDataRunnable);

	// Get the JSON Data File using the FRunnable class to get the data asynchronously
        AgentDataSubsystem->JsonDataRunnable = MakeUnique<FProcessSimulationDataRunnable>(JSONDataFile, AgentDataSubsystem);
	AgentDataSubsystem->OnLoadSimulationDataComplete.AddDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);
	//AgentDataSubsystem->OnMaxAgentCount.AddDynamic(AgentDataSubsystem, &UAgentDataSubsystem::UpdateMaxAgentCount);

	// check if the widget subsystem is valid
	if (LoadingSubsystem)
	{
		// bind current load percent
		AgentDataSubsystem->OnLoadSimulationDataProgress.AddDynamic(LoadingSubsystem, &ULoadingSubsystem::BroadcastNewLoadPercent);

		// get file name from the json data file
		FString FileName = FPaths::GetCleanFilename(JSONDataFile);

		FString LoadingText = FString::Printf(TEXT("Loading File: %s"), *FileName);
		
		// Set the loading text and title
		LoadingSubsystem->SetLoadingText(true, LoadingText);
	}

	
}

void UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData()
{
	UE_LOG(LogTemp, Warning, TEXT("Building Pedestrian Movement Fragment Data"));
	
	// get the mobius widget subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();
	
	// check if the widget subsystem is valid
	if (LoadingSubsystem)
	{

		FString LoadingText = FString::Printf(TEXT("Building Pedestrian Movement AI Data..."));
		
		// Set the loading text and title
		LoadingSubsystem->SetLoadingText(true, LoadingText);
		LoadingSubsystem->BroadcastNewLoadPercent(0.0f);
	}

	FSimulationFragment SimulationFragment;
	TSharedPtr<FJsonObject, ESPMode::ThreadSafe> JSONObjectLocal;
	float TimeBetweenStepsLocal = 0.f;

	if (AgentDataSubsystem->JsonDataRunnable)
	{
		SimulationFragment   = MoveTemp(AgentDataSubsystem->JsonDataRunnable->AgentMovementInfoData);
		JSONObjectLocal      = MoveTemp(AgentDataSubsystem->JsonDataRunnable->JSONObject);
		TimeBetweenStepsLocal = AgentDataSubsystem->JsonDataRunnable->TimeBetweenSteps;
	}

	//UE_LOG(LogTemp, Warning, TEXT("Building Pedestrian Movement Fragment Data"));
	PedestrianTemplateData.AddFragment<FEntityInfoFragment>();
	PedestrianTemplateData.AddFragment<FEntityMovementFragment>();
	PedestrianTemplateData.AddFragment<FEntityRenderingFragment>();
	PedestrianTemplateData.AddFragment<FEntityCollisionFragment>();

	// Add the tag to prevent collision updates
	PedestrianTemplateData.AddTag<FPedestrianCollisionsDisabled>();

	NumOfAgentsPerTimeStep = AgentDataSubsystem->JsonDataRunnable->NumOfAgentsPerTimeStep;

	if (NumOfAgentsPerTimeStep.IsValidIndex(0))
	{
		// log i 0 for the number of agents per time step
		UE_LOG(LogTemp, Warning, TEXT("Number of Agents Per Time Step: %d"), NumOfAgentsPerTimeStep[0]);
	}

	// Set the json object on the agent data subsystem
	AgentDataSubsystem->JSONObject = JSONObjectLocal;

	// Get Time Dilation from the ProjectMobius Game Instance
	UTimeDilationSubSystem* TimeDilationSubSystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

	// update time between steps
	TimeDilationSubSystem->UpdateTimeBetweenData(TimeBetweenStepsLocal);

	// Update the total time for the Time Dilation Subsystem - which also updates the max time steps
	TimeDilationSubSystem->UpdateTotalTime(SimulationFragment.MaxTime);

        auto SharedSimulationFragmentData = FSharedStruct::Make(SimulationFragment);

        SharedSimulationFragment = SharedSimulationFragmentData;

	// Add the shared fragment to the build context
        PedestrianTemplateData.AddSharedFragment(SharedSimulationFragmentData);
	
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
	
	// At this point data should be ready to spawn
	SpawnMaxPedestrians(ArchetypeSharedFragmentValues);
}

const FSimulationFragment* UMassEntitySpawnSubsystem::GetSimulationFragment() const
{
        return SharedSimulationFragment.GetPtr<FSimulationFragment>();
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
