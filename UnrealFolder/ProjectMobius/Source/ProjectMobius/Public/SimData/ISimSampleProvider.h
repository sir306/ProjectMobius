// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h" // FSimMovementSample

/**
 * Backend abstraction for per-timestep movement-sample storage (perf task A1).
 *
 * Decouples the playback/analysis consumers (PedestrianMovementProcessor, PedestrianInitializeMOP,
 * FloorStatsWidget) from the concrete storage, so a future disk-backed / streaming provider (A3-A5) can be
 * swapped in without touching them. For A1 the only implementation is FFullyResidentProvider, a thin view
 * over the in-RAM TMap the fragment already holds.
 *
 * Lives in ProjectMobius (not MobiusCore as the PRD originally sketched): it returns FSimMovementSample,
 * which is defined in ProjectMobius, and MobiusCore is the lower module (cannot see ProjectMobius types
 * without a circular dependency). Relocating the data layer to MobiusCore is a separate FSimMovementSample
 * type-move task.
 */
class PROJECTMOBIUS_API ISimSampleProvider
{
public:
	virtual ~ISimSampleProvider() = default;

	/**
	 * Samples present at the given timestep, or nullptr if that timestep is absent.
	 * Windowed accessor — fine for the render/playback path. MUST be bitwise-identical to the legacy
	 * SimulationData->Find(Ts), INCLUDING returning nullptr for an absent timestep: the movement
	 * processor's per-timestep map cache (task B2) keys its "samples the same" / out-of-bounds guard off
	 * exactly that nullptr.
	 */
	virtual const TArray<FSimMovementSample>* GetSamplesForTimestep(int32 Ts) const = 0;

	/** Number of stored timesteps. */
	virtual int32 GetNumTimesteps() const = 0;

	/** True once populated with usable data (mirrors the old SimulationData.IsValid() && Num()>0 check). */
	virtual bool IsValidAndPopulated() const = 0;

	/**
	 * Guaranteed-complete, ascending pass over every stored timestep. All-timestep analysis MUST use this
	 * rather than the windowed GetSamplesForTimestep (Invariant 5): a future streaming provider may only
	 * have a window resident for the windowed accessor, but this is contractually a full pass.
	 */
	virtual void ForEachTimestep(TFunctionRef<void(int32 /*Timestep*/, const TArray<FSimMovementSample>& /*Samples*/)> Fn) const = 0;

	/**
	 * Intern table for FSimMovementSample::ModeIndex (perf task A2): ModeTable[sample.ModeIndex] is that
	 * sample's mode string, with index 0 == "" (unset). Today every sample's ModeIndex is 0 because the
	 * importer drops the source "mode" attribute at the FSimMovementSample conversion, so this is effectively
	 * { "" }. It is the documented home for re-interning that attribute later (intern at the conversion, store
	 * the index on the sample) WITHOUT re-bloating the per-sample struct with a 16-byte FString. Returned by
	 * reference; valid for the provider's lifetime.
	 */
	virtual const TArray<FString>& GetModeTable() const = 0;

	/**
	 * Playhead hint for streaming providers to prefetch. The resident provider ignores it.
	 * DirectionHint: -1 rewind, 0 scrub/paused, +1 forward.
	 */
	virtual void NotifyPlayhead(int32 /*Ts*/, int32 /*DirectionHint*/) {}

	/**
	 * True when GetSamplesForTimestep(Ts) would return the EXACT block for Ts rather than a
	 * cosmetic stand-in (a streaming cold miss serves last-good/keyframe data from a DIFFERENT
	 * timestep). Time-integrating analysis (tenability FED banking) must hold state on frames
	 * where this is false — stand-in positions fabricate room changes. Must be side-effect free:
	 * never triggers a load. The resident provider is always exact.
	 */
	virtual bool HasExactSamplesForTimestep(int32 /*Ts*/) const { return true; }
};

/**
 * Fully-resident provider (A1): a thin view over the in-RAM TMap that FSimulationFragment already owns.
 * Shares ownership of the backing TMap via TSharedPtr, so the data is freed on file switch exactly as
 * before (when both the fragment's SimulationData and this provider are released). Adds no copy and no
 * behavioural change — GetSamplesForTimestep is a straight TMap::Find.
 */
class PROJECTMOBIUS_API FFullyResidentProvider final : public ISimSampleProvider
{
public:
	// InModeTable defaults to a single "" so ModeIndex 0 always resolves; a future importer that preserves the
	// source "mode" attribute would pass the interned table here (perf task A2).
	explicit FFullyResidentProvider(TSharedPtr<TMap<int32, TArray<FSimMovementSample>>> InSamples,
	                                TArray<FString> InModeTable = { FString() })
		: Samples(MoveTemp(InSamples))
		, ModeTable(MoveTemp(InModeTable))
	{
	}

	virtual const TArray<FSimMovementSample>* GetSamplesForTimestep(int32 Ts) const override
	{
		return Samples.IsValid() ? Samples->Find(Ts) : nullptr;
	}

	virtual int32 GetNumTimesteps() const override
	{
		return Samples.IsValid() ? Samples->Num() : 0;
	}

	virtual bool IsValidAndPopulated() const override
	{
		return Samples.IsValid() && Samples->Num() > 0;
	}

	virtual const TArray<FString>& GetModeTable() const override
	{
		return ModeTable;
	}

	virtual void ForEachTimestep(TFunctionRef<void(int32, const TArray<FSimMovementSample>&)> Fn) const override
	{
		if (!Samples.IsValid())
		{
			return;
		}
		// Sort keys so the pass is deterministic/ascending even if timesteps were inserted out of order
		// (mirrors FloorStatsWidget's previous GetKeys/Sort/Find behaviour).
		TArray<int32> Keys;
		Samples->GetKeys(Keys);
		Keys.Sort();
		for (int32 Ts : Keys)
		{
			if (const TArray<FSimMovementSample>* TimestepSamples = Samples->Find(Ts))
			{
				Fn(Ts, *TimestepSamples);
			}
		}
	}

private:
	/** Shared with FSimulationFragment::SimulationData — same allocation, same lifetime. */
	TSharedPtr<TMap<int32, TArray<FSimMovementSample>>> Samples;

	/** Mode intern table for FSimMovementSample::ModeIndex (A2). Defaults to { "" }; see GetModeTable. */
	TArray<FString> ModeTable;
};
