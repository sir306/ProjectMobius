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
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentRepresentationFragment.h"
// Actors to include
#include "MassAI/Actors/AgentRepresentationActorISM.h"
// Other Subsystems we want to use
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"
#include "Core/MobiusWidgetSubsystem.h"
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
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentNiagaraDataFrag.h"
#include "MassAI/SubSystems/PedestrianSignalSubsystem.h"
#include "Util/MemoryTraceHelper.h"
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
#if !UE_BUILD_SHIPPING
                        FMobiusMemSnapshot SnapFlowBefore = FMobiusMemSnapshot::Take(TEXT("FlowReset_Before"));
                        SnapFlowBefore.LogAbsolute();
#endif
                        StatSubsystem->ResetFlowCounters();
                        bHasResetFlowCounters = true;
#if !UE_BUILD_SHIPPING
                        FMobiusMemSnapshot::Take(TEXT("FlowReset_After")).LogDelta(SnapFlowBefore);
#endif
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
#if !UE_BUILD_SHIPPING
		FMobiusMemSnapshot NiaPrev = FMobiusMemSnapshot::Take(TEXT("Niagara_BeforeClearSimCache"));
#endif
		ExistingActor->GetNiagaraComponent()->ClearSimCache(true);
#if !UE_BUILD_SHIPPING
		{
			FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("Niagara_AfterClearSimCache"));
			S.LogDelta(NiaPrev);
			NiaPrev = S;
		}
#endif
		ExistingActor->GetNiagaraComponent()->DeactivateImmediate();
#if !UE_BUILD_SHIPPING
		{
			FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("Niagara_AfterDeactivate"));
			S.LogDelta(NiaPrev);
			NiaPrev = S;
		}
#endif
		ExistingActor->GetNiagaraComponent()->DestroyInstanceNotComponent();
#if !UE_BUILD_SHIPPING
		{
			FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("Niagara_AfterDestroyInstance"));
			S.LogDelta(NiaPrev);
		}
#endif
	}
}

void UMassEntitySpawnSubsystem::AgentDataRunnableCleanup(TUniquePtr<FProcessSimulationDataRunnable>& ToKill)
{
        if (!ToKill) return;

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapCleanupStart = FMobiusMemSnapshot::Take(TEXT("RunnableCleanup_Start"));
	SnapCleanupStart.LogAbsolute();
#endif

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

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("RunnableCleanup_AfterReset")).LogDelta(SnapCleanupStart);
#endif
}

FMassArchetypeHandle UMassEntitySpawnSubsystem::CreatePedestrianArchetype()
{
	// TemplateBuildContext
	FMassEntityTemplateBuildContext PedestrianLocationTraitBuildContext = FMassEntityTemplateBuildContext(PedestrianTemplateData);

	// Get this traits template id
	FMassEntityTemplateID DebugEntityLocationTraitID = PedestrianLocationTraitBuildContext.GetTemplateID();

	// Remember the ID so CreatePedestrianTemplateData can call DestroyTemplate on file switch
	RegisteredPedestrianTemplateID = DebugEntityLocationTraitID;

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

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapSwitchStart = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterRunnableCleanup"));
	SnapSwitchStart.LogAbsolute();
	FMobiusMemSnapshot SnapPrev = SnapSwitchStart;
#endif

	// Drop per-file caches (CachedEntityData + subsystem TQueues) BEFORE we destroy
	// entities and the template. Those caches are never reached by GC and were
	// observed to hold prior-file residue across switches.
	if (AgentDataSubsystem)
	{
		AgentDataSubsystem->ClearPerFileState();
	}

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterClearPerFileState"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

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

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterCapsuleDestroy"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Destroy any existing spawned pedestrians and clear the Niagara simulation
	DestroyAllSpawnedPedestrians();

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterDestroyAllSpawned"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	ClearNiagaraSim();

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterClearNiagara"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Drop per-simulation state held by world subsystems before the GC pass.
	// These outlive an individual file load (subsystems are world-scoped and
	// PIE world only ends on stop), so without an explicit reset they keep
	// the prior simulation's agent data + widget tree (MIDs, shader maps)
	// rooted across switches.
	if (UWorld* World = GetWorld())
	{
		if (UStatisticSubsystem* StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->ResetForFileSwitch();
		}
		if (UMobiusWidgetSubsystem* WidgetSub = World->GetSubsystem<UMobiusWidgetSubsystem>())
		{
			WidgetSub->ResetForFileSwitch();
		}
	}

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterSubsystemReset"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Empty out the handles array
	SpawnedEntityPedestrianHandles.Empty();

	// We have to force a garbage collection here to ensure that the old data is cleared from memory before new
	// data is created
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterFirstGC"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Destroy old template entry in the registry — FindOrAddTemplate never replaces existing
	// entries (UE5 returns the old one and silently discards new data), so we must explicitly
	// remove it. This drops the registry's FSharedStruct ref to the old FSimulationFragment.
	// Safe because all entities using this template have already been destroyed above.
	TemplateRegistryInstance.DestroyTemplate(RegisteredPedestrianTemplateID);
	RegisteredPedestrianTemplateID.Invalidate();

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterDestroyTemplate"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Reset the template data
	PedestrianTemplateData = FMassEntityTemplateData();

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterTemplateDataReset"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Drop our ref to the old simulation fragment so it can be freed now that the
	// TemplateRegistryInstance has also released its ref.
	// We aggressively clear the data map first because Mass AI may still hold
	// references to the fragment in lingering chunks, which would otherwise
	// prevent the ~900MB of TMap/TArray memory from being reclaimed.
	if (FSimulationFragment* Frag = SharedSimulationFragment.GetPtr<FSimulationFragment>())
	{
		// Reset the TSharedPtr — frees the 4 GB TMap independently of the Mass archetype
		// that permanently holds the FSimulationFragment struct.
		Frag->SimulationData.Reset();
	}

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterSimDataReset"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	SharedSimulationFragment = FSharedStruct();

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterSharedStructCleared"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Second GC pass now that template + SimulationData are both released.
	// The first pass (above) ran before DestroyTemplate, so the archetype still held refs then.
	// This pass lets the allocator reclaim pages sooner after the 4 GB drop.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterSecondGC"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

	// Hint the allocator to return freed pages to the OS. App-level frees without
	// Trim often keep pages in the process's working set, masking whether the
	// earlier steps actually released memory.
	FMemory::Trim();

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapAfterTrim = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterMemTrim"));
	SnapAfterTrim.LogDelta(SnapPrev);
	// Cumulative drop vs the start of the switch, for quick scanning.
	SnapAfterTrim.LogDelta(SnapSwitchStart);

	// Diagnostic probe A: MemReport -full. Writes Saved/Profiling/MemReports/
	// <timestamp>.memreport + .memreportgpu with the full allocator / UObject / RHI
	// breakdown. One file per switch — diff them to see what's retained. This is
	// temporary instrumentation; revert before committing to main.
	if (GEngine && GetWorld())
	{
		GEngine->Exec(GetWorld(), TEXT("MemReport -full"));
	}

	// Diagnostic probe B: poll memory every 500ms for 5s after the switch kicks
	// off. Tells us whether Phys drops on its own (allocator is lazy) vs. stays
	// flat (genuine retention). The next file's async load will start inflating
	// Phys partway through the poll window — read the first 1-2 samples for the
	// steady-state cleanup signal.
	if (UWorld* World = GetWorld())
	{
		struct FPollState
		{
			FMobiusMemSnapshot Baseline;
			FMobiusMemSnapshot Prev;
			int32              Count = 0;
			FTimerHandle       Handle;
		};
		TSharedRef<FPollState> State = MakeShared<FPollState>();
		State->Baseline = FMobiusMemSnapshot::Take(TEXT("FileSwitch_PollBaseline"));
		State->Prev     = State->Baseline;

		TWeakObjectPtr<UMassEntitySpawnSubsystem> WeakThis(this);
		World->GetTimerManager().SetTimer(State->Handle, FTimerDelegate::CreateLambda(
			[State, WeakThis]()
			{
				if (!WeakThis.IsValid()) { return; }
				++State->Count;
				FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(
					FString::Printf(TEXT("FileSwitch_Poll[%d]"), State->Count));
				S.LogDelta(State->Baseline);   // vs. cleanup-end baseline
				S.LogDelta(State->Prev);       // vs. previous 500ms sample
				State->Prev = S;

				if (State->Count >= 10)
				{
					if (UMassEntitySpawnSubsystem* Self = WeakThis.Get())
					{
						if (UWorld* W = Self->GetWorld())
						{
							W->GetTimerManager().ClearTimer(State->Handle);
						}
					}
				}
			}),
			0.5f, /*bLoop=*/true);
	}
#endif

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

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapBuildStart = FMobiusMemSnapshot::Take(TEXT("BuildFrag_Start"));
	SnapBuildStart.LogAbsolute();
#endif

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
	float TimeBetweenStepsLocal = 0.f;
	ESimulationFileType LoadedFileType = ESimulationFileType::ESFT_Unknown;

	if (AgentDataSubsystem->JsonDataRunnable)
	{
		SimulationFragment    = MoveTemp(AgentDataSubsystem->JsonDataRunnable->AgentMovementInfoData);
		TimeBetweenStepsLocal = AgentDataSubsystem->JsonDataRunnable->TimeBetweenSteps;
		LoadedFileType        = AgentDataSubsystem->JsonDataRunnable->SimulationFileType;
		NumOfAgentsPerTimeStep = AgentDataSubsystem->JsonDataRunnable->NumOfAgentsPerTimeStep;

		// Cache entity metadata before the runnable is torn down.
		// PedestrianInitializeMOP fires after SpawnMaxPedestrians destroys the runnable,
		// so SetEntityInfoByIndex / SetEntityRenderingByIndex must read from here instead.
		AgentDataSubsystem->CachedEntityData = MoveTemp(AgentDataSubsystem->JsonDataRunnable->Hdf5Data.Entities);
		AgentDataSubsystem->CachedEntityData.Shrink();
	}

	//UE_LOG(LogTemp, Warning, TEXT("Building Pedestrian Movement Fragment Data"));
	PedestrianTemplateData.AddFragment<FEntityInfoFragment>();
	PedestrianTemplateData.AddFragment<FEntityMovementFragment>();
	PedestrianTemplateData.AddFragment<FEntityRenderingFragment>();
	PedestrianTemplateData.AddFragment<FEntityCollisionFragment>();

	// Add the tag to prevent collision updates
	PedestrianTemplateData.AddTag<FPedestrianCollisionsDisabled>();

	if (NumOfAgentsPerTimeStep.IsValidIndex(0))
	{
		// log i 0 for the number of agents per time step
		UE_LOG(LogTemp, Warning, TEXT("Number of Agents Per Time Step: %d"), NumOfAgentsPerTimeStep[0]);
	}

	// Get Time Dilation from the ProjectMobius Game Instance
	UTimeDilationSubSystem* TimeDilationSubSystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

	// update time between steps
	TimeDilationSubSystem->UpdateTimeBetweenData(TimeBetweenStepsLocal);

	// Update the total time for the Time Dilation Subsystem - which also updates the max time steps
	TimeDilationSubSystem->UpdateTotalTime(SimulationFragment.MaxTime);

        auto SharedSimulationFragmentData = FSharedStruct::Make(MoveTemp(SimulationFragment));

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
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("BuildFrag_PreSpawn")).LogDelta(SnapBuildStart);
#endif
	SpawnMaxPedestrians(ArchetypeSharedFragmentValues);
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("BuildFrag_AfterSpawn")).LogDelta(SnapBuildStart);
#endif
}

const FSimulationFragment* UMassEntitySpawnSubsystem::GetSimulationFragment() const
{
        return SharedSimulationFragment.GetPtr<FSimulationFragment>();
}

void UMassEntitySpawnSubsystem::BuildPedestrianRepresentationFragmentData()
{
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
