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
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "SimData/ISimSampleProvider.h" // A1: FFullyResidentProvider built into the shared fragment
#include "SimData/FStreamingProvider.h" // A4: optional .msc-backed streaming provider (flag-gated)
#include "SimData/SimDiskCache.h"       // A4: cache path/hash lookup for the loaded source file
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentRepresentationFragment.h"
// Actors to include
#include "MassAI/Actors/AgentRepresentationActorISM.h"
// Other Subsystems we want to use
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"
// GameInstance
#include "SkeletalMeshAttributes.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Async/Async.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "BRisk/BRiskDataSubsystem.h"
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
#include "Subsystems/HeatmapSubsystem.h" // RequestTrajectoryTrackingReset on file switch
#include "HAL/IConsoleManager.h"

class UTimeDilationSubSystem;

#if !UE_BUILD_SHIPPING
namespace
{
	TAutoConsoleVariable<int32> CVarMobiusAgentDataFileSwitchDiagnostics(
		TEXT("mobius.AgentData.FileSwitchDiagnostics"),
		0,
		TEXT("Enable expensive agent-data file switch diagnostics: extra GC, allocator trim, memreport, and memory polling."),
		ECVF_Default);

	bool ShouldRunAgentDataFileSwitchDiagnostics()
	{
		return CVarMobiusAgentDataFileSwitchDiagnostics.GetValueOnGameThread() != 0;
	}
}
#endif

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
	if (UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld()))
	{
		GameInst->OnPedestrianVectorFileUpdated.RemoveDynamic(this, &UMassEntitySpawnSubsystem::CreatePedestrianTemplateData);
	}
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
	// The runnable data was moved out in BuildPedestrianMovementFragmentData. Keep
	// the completed runnable object until the next file switch so we do not join
	// its thread from the spawn/completion path.
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
                SpawnedEntityPedestrianHandles.Empty();
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

void UMassEntitySpawnSubsystem::AgentDataRunnableCleanup(TUniquePtr<FProcessAgentSimulationDataRunnable>& ToKill)
{
        if (!ToKill) return;

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapCleanupStart = FMobiusMemSnapshot::Take(TEXT("RunnableCleanup_Start"));
	SnapCleanupStart.LogAbsolute();
#endif

	// 1) Remove delegates first — no stale broadcast can reach us even if thread finishes during stop/join
	if (auto* LS = GetWorld()->GetSubsystem<ULoadingSubsystem>()) {
		AgentDataSubsystem->OnLoadSimulationDataProgress.RemoveDynamic(LS, &ULoadingSubsystem::BroadcastNewLoadPercent);
	}
	AgentDataSubsystem->OnLoadSimulationDataComplete.RemoveDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);
	//AgentDataSubsystem->OnMaxAgentCount.RemoveDynamic(AgentDataSubsystem, &UAgentDataSubsystem::UpdateMaxAgentCount);

	// 2) Signal the thread to stop
	ToKill->Stop();

	// 3) Join via destructor — WaitForCompletion() is called inside ~FProcessAgentSimulationDataRunnable,
	//    then UE calls Exit() once cleanly after Run() returns. Do NOT call Exit() manually here;
	//    that races with the background thread still accessing AgentDataArray / AgentSimulationData.
	ToKill.Reset();

	// 4) Clear any stale completion flag now the thread is fully joined
	if (AgentDataSubsystem)
	{
		AgentDataSubsystem->bIsDataLoaded = false;
	}

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
#if !UE_BUILD_SHIPPING
	const bool bRunFileSwitchDiagnostics = ShouldRunAgentDataFileSwitchDiagnostics();
#endif
	const bool bHadExistingFileState =
		!PedestrianTemplateData.IsEmpty() ||
		!SpawnedEntityPedestrianHandles.IsEmpty() ||
		SharedSimulationFragment.GetPtr<FSimulationFragment>() != nullptr ||
		(AgentDataSubsystem && AgentDataSubsystem->AgentDataRunnable);

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
        AgentDataRunnableCleanup(AgentDataSubsystem->AgentDataRunnable);

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


	// reset data in stats subsystem in prep for new data
	if (UWorld* World = GetWorld())
	{
		if (UStatisticSubsystem* StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->ResetForFileSwitch();
		}
		if (UHeatmapSubsystem* HeatmapSub = World->GetSubsystem<UHeatmapSubsystem>())
		{
			// The new dataset reuses entity IDs. Without this the heatmap processor joins each recycled
			// ID to the position its predecessor last held and draws a streak across the floor.
			HeatmapSub->RequestTrajectoryTrackingReset();
		}
	}

#if !UE_BUILD_SHIPPING
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterSubsystemReset"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

#if !UE_BUILD_SHIPPING
	if (bRunFileSwitchDiagnostics)
	{
		// Optional early GC diagnostic. Normal switching does one GC after all
		// strong simulation/template references are released below.
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
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
		if (Frag->SimulationData.IsValid())
		{
			// Empty the pointed-to map first because this TSharedPtr may have
			// copies in Mass fragments that outlive this subsystem reference.
			Frag->SimulationData->Empty();
			Frag->SimulationData.Reset();
		}
		// A1: release the provider too. It holds a copy of the same TSharedPtr as SimulationData, so it
		// must be reset for the backing allocation's control block to go away on file switch.
		Frag->Provider.Reset();
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

	// Force GC once after template + SimulationData references are both released.
	// Skip it on the initial load path where there was no previous file state to
	// tear down.
	if (bHadExistingFileState)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

#if !UE_BUILD_SHIPPING
	if (bHadExistingFileState)
	{
		FMobiusMemSnapshot S = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterGC"));
		S.LogDelta(SnapPrev);
		SnapPrev = S;
	}
#endif

#if !UE_BUILD_SHIPPING
	if (bRunFileSwitchDiagnostics)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		// Hint the allocator to return freed pages to the OS. Kept opt-in
		// because it can cause later page-fault hitches.
		FMemory::Trim();

	FMobiusMemSnapshot SnapAfterTrim = FMobiusMemSnapshot::Take(TEXT("FileSwitch_AfterMemTrim"));
	SnapAfterTrim.LogDelta(SnapPrev);
	// Cumulative drop vs the start of the switch, for quick scanning.
	SnapAfterTrim.LogDelta(SnapSwitchStart);

	// Diagnostic probe A: MemReport -full. Writes Saved/Profiling/MemReports/
	// <timestamp>.memreport + .memreportgpu with the full allocator / UObject / RHI
	// breakdown. One file per switch — diff them to see what's retained. This is
	// opt-in diagnostic; enable only while investigating retention.
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
	AgentDataRunnableCleanup(AgentDataSubsystem->AgentDataRunnable);

	// Bind delegate BEFORE creating the runnable so we never miss a completion if the thread is very fast
	AgentDataSubsystem->OnLoadSimulationDataComplete.AddDynamic(this, &UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData);

	// Get the JSON Data File using the FRunnable class to get the data asynchronously
#if !UE_BUILD_SHIPPING
	const double RunnableCreateStart = FPlatformTime::Seconds();
#endif
	AgentDataSubsystem->AgentDataRunnable = MakeUnique<FProcessAgentSimulationDataRunnable>(JSONDataFile, AgentDataSubsystem);
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Display, TEXT("Agent data runnable creation took %.3f ms"), (FPlatformTime::Seconds() - RunnableCreateStart) * 1000.0);
#endif
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

	if (AgentDataSubsystem->AgentDataRunnable)
	{
		SimulationFragment    = MoveTemp(AgentDataSubsystem->AgentDataRunnable->AgentMovementInfoData);
		TimeBetweenStepsLocal = AgentDataSubsystem->AgentDataRunnable->TimeBetweenSteps;
		NumOfAgentsPerTimeStep = AgentDataSubsystem->AgentDataRunnable->NumOfAgentsPerTimeStep;

		// Cache entity metadata before the runnable is torn down.
		// PedestrianInitializeMOP fires after SpawnMaxPedestrians destroys the runnable,
		// so SetEntityInfoByIndex / SetEntityRenderingByIndex must read from here instead.
		AgentDataSubsystem->CachedEntityData = MoveTemp(AgentDataSubsystem->AgentDataRunnable->AgentSimulationData.Entities);
		AgentDataSubsystem->CachedEntityData.Shrink();
	}

	// Persist the agent grid (interval + total) before SimulationFragment is moved into the shared
	// struct below. These outlive the runnable and act as the reliable "agent data present" signal
	// for the timeline coordinator, plus the source of the movement processor's agent-native index.
	AgentTimeBetweenSteps = TimeBetweenStepsLocal;
	AgentTotalTime = SimulationFragment.MaxTime;

	//UE_LOG(LogTemp, Warning, TEXT("Building Pedestrian Movement Fragment Data"));
	PedestrianTemplateData.AddFragment<FEntityInfoFragment>();
	PedestrianTemplateData.AddFragment<FEntityMovementFragment>();
	PedestrianTemplateData.AddFragment<FEntityRenderingFragment>();
	PedestrianTemplateData.AddFragment<FEntityCollisionFragment>();
	PedestrianTemplateData.AddFragment<FAgentBRiskExposureFragment>();
	PedestrianTemplateData.AddFragment<FAgentEgressTenabilityFragment>();

	// Add the tag to prevent collision updates
	PedestrianTemplateData.AddTag<FPedestrianCollisionsDisabled>();

	if (NumOfAgentsPerTimeStep.IsValidIndex(0))
	{
		// log i 0 for the number of agents per time step
		UE_LOG(LogTemp, Warning, TEXT("Number of Agents Per Time Step: %d"), NumOfAgentsPerTimeStep[0]);
	}

	// Configure the shared playback clock. Route through the B-Risk timeline coordinator so the
	// active source (agent vs B-Risk) owns the clock: with B-Risk timing enabled the clock keeps
	// the B-Risk duration while agent data still loads on its own grid (req: agent loaded after a
	// B-Risk file). The coordinator reads this subsystem's just-cached AgentTotalTime/interval.
	// Fall back to a direct agent-clock config when the B-Risk subsystem is unavailable so the
	// pure-agent workflow never regresses.
	if (UBRiskDataSubsystem* BRiskSubsystem = GetWorld()->GetSubsystem<UBRiskDataSubsystem>())
	{
		BRiskSubsystem->ApplyActiveTimeline(/*bResetToStart=*/true);
	}
	else if (UTimeDilationSubSystem* TimeDilationSubSystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>())
	{
		TimeDilationSubSystem->UpdateTimeBetweenData(TimeBetweenStepsLocal);
		TimeDilationSubSystem->UpdateTotalTime(SimulationFragment.MaxTime);
	}

	// B2: stamp the monotonic build generation (composite cache key for the persistent movement processor).
	// Bumped here once per rebuild and never reset, so a file switch back to t=0 still invalidates the
	// processor's sample-index maps. Placed after the SimulationFragment move above and before the MoveTemp
	// into the shared struct below so it travels into the shared fragment (covers the null-runnable branch too).
	SimulationFragment.DataGeneration = ++SimDataGenerationCounter;

	// A1: wrap the resident TMap in an FFullyResidentProvider so consumers (FloorStatsWidget, and a future
	// streaming provider) read through the ISimSampleProvider interface instead of touching SimulationData
	// directly. Shares the same TSharedPtr (no copy); built before the MoveTemp so it travels into the shared
	// fragment. ModeTable defaults to { "" } (perf task A2 — importer drops the source per-sample mode attribute).
	SimulationFragment.Provider = MakeShared<FFullyResidentProvider>(SimulationFragment.SimulationData);

	// A4/A5: serve from the .msc disk cache instead of the resident TMap when either the manual force
	// flag is on (A4, mobius.SimCache.ForceStreaming) or the auto residency decision finds the dataset
	// over the RAM budget (A5, mobius.SimCache.AutoStreaming + BudgetFraction/BudgetCapGB). On streaming
	// success the resident copy is FREED here: at this point it has a single owner (the runnable's
	// fragment was MoveTemp'd into this local above, and the streaming provider holds no reference to
	// it), so the Reset() returns the multi-GB block to the allocator before entities spawn. Consumers
	// have been provider-only since A1 (InitMOP copies its spawn block from the provider; all-timestep
	// analysis uses ForEachTimestep — a disk pass). Every validation failure falls back to the resident
	// provider built above. NOTE: the transient import peak is unchanged (the data must exist once to be
	// converted and cached); this bounds the STEADY-STATE playback footprint.
	if (SimulationFragment.SimulationData.IsValid())
	{
		const bool bForceStreaming = FStreamingProvider::IsForceStreamingEnabled();
		bool bWantStreaming = bForceStreaming;

		if (!bWantStreaming && FStreamingProvider::IsAutoStreamingEnabled())
		{
			// Inline sample footprint only (64 B x samples; container overhead is small beside it).
			int64 TotalSamples = 0;
			for (const TPair<int32, TArray<FSimMovementSample>>& Pair : *SimulationFragment.SimulationData)
			{
				TotalSamples += Pair.Value.Num();
			}
			const uint64 EstimatedResidentBytes = static_cast<uint64>(TotalSamples) * sizeof(FSimMovementSample);

			float BudgetFraction = 0.f;
			uint64 BudgetCapBytes = 0;
			FStreamingProvider::GetBudgetCVars(BudgetFraction, BudgetCapBytes);
			const uint64 AvailablePhysical = FPlatformMemory::GetStats().AvailablePhysical;
			bWantStreaming = FStreamingProvider::ShouldStreamSimData(
				EstimatedResidentBytes, AvailablePhysical, BudgetFraction, BudgetCapBytes);
			if (bWantStreaming)
			{
				UE_LOG(LogTemp, Log, TEXT("Agent dataset estimated at %llu MB resident exceeds the RAM budget — auto-streaming (perf task A5)"),
					EstimatedResidentBytes / (1024ull * 1024ull));
			}
		}

		if (bWantStreaming)
		{
			const FString SourcePath = AgentDataSubsystem ? AgentDataSubsystem->GetLoadedSimulationDataFilePath() : FString();
			bool bStreamingActive = false;
			if (!SourcePath.IsEmpty())
			{
				const uint64 SourceHash = MobiusSimCache::ComputeSourceHash(SourcePath);
				const FString CacheFilePath = MobiusSimCache::MakeCacheFilePath(SourcePath, SourceHash);
				const FStreamingProviderConfig StreamConfig =
					FStreamingProvider::MakeConfigFromCVars(MobiusSimCache::CacheDriveHasSeekPenalty());
				TSharedPtr<FStreamingProvider> StreamingProvider =
					MakeShared<FStreamingProvider>(CacheFilePath, SourceHash, StreamConfig);
				// Timestep-count parity with the just-imported data is the last stale-cache guard (the
				// hash keys only source bytes — see the A4 forward-trap note in SimDiskCache.h).
				if (StreamingProvider->IsValidAndPopulated()
					&& StreamingProvider->GetNumTimesteps() == SimulationFragment.SimulationData->Num())
				{
					SimulationFragment.Provider = StreamingProvider;
#if !UE_BUILD_SHIPPING
					FMobiusMemSnapshot SnapDropBefore = FMobiusMemSnapshot::Take(TEXT("BuildFrag_StreamingDropResident_Before"));
#endif
					// Single owner (see block comment) — this is the A5 RAM win. The refcount log is the
					// tiebreaker between "freed but the allocator retains the pages" (count 1) and "a
					// hidden holder keeps it alive" (count > 1).
					UE_LOG(LogTemp, Log, TEXT("Dropping resident sample map (shared refs at drop: %d)"),
						SimulationFragment.SimulationData.GetSharedReferenceCount());
					SimulationFragment.SimulationData.Reset();
					// Hand the freed pages back to the OS: the samples are 64-byte-bin allocations the
					// binned allocator otherwise pools indefinitely, so without this the working set
					// never falls and the drop is invisible (the documented "baseline stays elevated"
					// behaviour from the 2026-04 memory investigation). One-time cost at load.
					FMemory::Trim();
#if !UE_BUILD_SHIPPING
					FMobiusMemSnapshot::Take(TEXT("BuildFrag_StreamingDropResident_After")).LogDelta(SnapDropBefore);
#endif
					bStreamingActive = true;
				}
			}
			if (!bStreamingActive)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Streaming requested (%s) but no usable .msc for '%s' — keeping the resident provider"),
					bForceStreaming ? TEXT("ForceStreaming=1") : TEXT("auto RAM budget"), *SourcePath);
			}
		}
	}

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
