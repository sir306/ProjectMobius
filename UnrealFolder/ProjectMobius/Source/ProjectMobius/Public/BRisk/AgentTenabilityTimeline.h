// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h" // ETenabilityCriterion, FTenabilityAnalysisSettings

// Forward declarations keep this header light and free of circular includes: the
// interval/dose core only needs the *names* here (TConstArrayView<FSimMovementSample>
// stores a pointer + count; ComputeFailureData in Task 3 needs the full table type,
// and its .cpp includes the concrete headers). AgentTenabilityTimeline.cpp includes
// SimulationFragment.h; the future Task-3 code includes BRiskDataImporter.h.
struct FSimMovementSample;
struct FBRiskTenabilityRoomTable;

/**
 * Precomputed, navigation-independent per-agent B-Risk tenability.
 *
 * The timeline core is a PURE data layer: no UObject access, no game-thread
 * requirement, safe to build on a worker thread. It replaces the old runtime
 * forward-integration (mutable baselines, banked dose, one-way failure latches)
 * with a closed-form dose query over a precomputed room-occupancy interval list,
 * so tenability at time t is the same value regardless of how playback reached t
 * (play / skip / rewind / replay). See the implementation plan
 * `_ClaudeHandoff/plans/2026-07-07-brisk-tenability-timeline-v2.md`, Task 1.
 */
namespace UE::Mobius::Tenability
{
	/**
	 * Room cumulative-FED sampler: fills FEDSum/FEDRadSum for a room at a time
	 * (clamped to the recorded range). Backed by
	 * UBRiskEgressSubsystem::SampleTenabilityTableAtTime in production; synthetic
	 * in tests. RoomIndex indexes the bounds/ids arrays the timeline was built from.
	 */
	using FRoomFEDSampler = TFunctionRef<void(int32 RoomIndex, double TimeSeconds, double& OutFEDSum, double& OutFEDRadSum)>;

	/**
	 * Where one B-Risk room is, for the purpose of deciding whether a point is inside it.
	 *
	 * Bounds is the axis-aligned slab including floor-to-ceiling Z. FootprintPolygonCm is the true
	 * plan ring in the UE XY plane (cm) when Zones-data.json supplied one, and EMPTY when the room
	 * is only known as B-Risk's equivalent rectangle - in which case Bounds *is* the room and
	 * containment is the box test alone.
	 *
	 * Built by UBRiskEgressSubsystem::RebuildRoomCache from BRiskCoord::MakeRoomFootprint, in the
	 * same loop that fills the parallel room-state array, and copied wholesale as a thread-safe
	 * snapshot for the offline builder. Self-contained (FBox + FVector2D are both CoreMinimal) so
	 * this header stays free of the importer plugin include.
	 */
	struct FRoomVolume
	{
		FBox Bounds = FBox(ForceInit);
		TArray<FVector2D> FootprintPolygonCm;

		/**
		 * Precomputed specificity metric for the smallest-enclosing tie-break: plan area x Z
		 * extent. For a polygon room that is the ring's true area, NOT its bounding box - a
		 * non-convex corridor must not out-claim a small room sitting inside its bbox.
		 *
		 * For a room with no polygon this is exactly (X extent * Y extent) * Z extent, evaluated
		 * in that order, so it is bit-identical to the FBox::GetVolume() this replaced and
		 * rectangle-only scenarios break ties exactly as before.
		 */
		double SpecificityVolumeCm3 = 0.0;
	};

	/**
	 * THE only way to build an FRoomVolume — it is what derives SpecificityVolumeCm3, so callers
	 * cannot disagree about the tie-break metric. Pass an empty polygon for a room known only as
	 * B-Risk's equivalent rectangle. Defined in the .cpp because the shoelace area lives in the
	 * importer header, which this one stays free of.
	 */
	PROJECTMOBIUS_API FRoomVolume MakeRoomVolume(const FBox& Bounds, TArray<FVector2D> FootprintPolygonCm);

	/**
	 * Shared room resolution — THE single rule for "which B-Risk room contains
	 * this location". Extracted verbatim from
	 * UBRiskEgressSubsystem::FindRoomStateAtLocation so the offline builder and the
	 * live sampler resolve the room identically (scientific-integrity invariant 4).
	 *
	 * Rule:
	 *   1. If PreferredRoomIndex is a valid index AND that room contains WorldLocation
	 *      -> return PreferredRoomIndex (preferred-room stickiness).
	 *   2. Otherwise, among all OTHER rooms containing WorldLocation, return the one with the
	 *      smallest SpecificityVolumeCm3 (most specific match). Ties go to the lower index.
	 *   3. Otherwise -> INDEX_NONE (outside every room).
	 *
	 * CONTAINMENT is the Z slab and XY bounding box (FBox::IsInsideOrOn, boundary-inclusive) AND,
	 * when the room has a footprint polygon, BRiskCoord::IsPointInRing on the XY position. The box
	 * is kept as the cheap reject ahead of the ring walk because this runs per agent per frame.
	 *
	 * There is deliberately NO fall back to the bounding box when no polygon claims the point. A
	 * non-convex room's bbox covers space B-Risk never modelled - the notch of an L-shaped corridor
	 * is up to several times the room's real area - and attributing an agent there to the enclosing
	 * room is what this rule exists to stop. An agent in unmodelled space resolves to INDEX_NONE
	 * and accrues no dose, exactly as one standing outside the building already does; fabricating a
	 * room's readings for a location that room does not occupy would violate
	 * scientific-integrity invariant 3.
	 */
	PROJECTMOBIUS_API int32 ResolveRoomIndexAtLocation(
		TConstArrayView<FRoomVolume> RoomVolumes,
		const FVector& WorldLocation,
		int32 PreferredRoomIndex);

	/** One contiguous span an agent spent inside a single room, entry-to-exit. */
	struct FAgentRoomOccupancyInterval
	{
		int32 RoomIndex = INDEX_NONE;
		int32 RoomId = INDEX_NONE;
		float EntryTimeSeconds = 0.0f;
		float ExitTimeSeconds = 0.0f;
		// Room cumulative FED at entry/exit (raw curve values — settings-independent).
		float EntryToxicFED = 0.0f;
		float ExitToxicFED = 0.0f;
		float EntryThermalFED = 0.0f;
		float ExitThermalFED = 0.0f;
		// Dose banked by all COMPLETED intervals before this one (prefix sums for O(log k) queries).
		float PriorToxicFED = 0.0f;
		float PriorThermalFED = 0.0f;
	};

	/** Precomputed tenability for one agent: Layer-1 intervals + Layer-2 failure fields. */
	struct FAgentTenabilityTimeline
	{
		TArray<FAgentRoomOccupancyInterval> Intervals; // ascending, non-overlapping
		// Layer 2 (filled by Task 3):
		float FirstFailureTimeSeconds = -1.0f;
		ETenabilityCriterion FirstFailureCriterion = ETenabilityCriterion::None;
		uint8 FirstFailureMask = 0;
		float VisibilityFailureTimeSeconds = -1.0f;
		float ToxicFEDFailureTimeSeconds = -1.0f;
		float ThermalFEDFailureTimeSeconds = -1.0f;
		float TemperatureFailureTimeSeconds = -1.0f;
		float LayerHeightFailureTimeSeconds = -1.0f;
		FVector FailureLocation = FVector::ZeroVector;
		FRotator FailureRotation = FRotator::ZeroRotator;

		/** Closed-form dose at any time — THE navigation-independent query.
		 *  Exported (the struct is plain data, but this one out-of-line method is
		 *  called from the tests module across the DLL boundary). */
		PROJECTMOBIUS_API void DoseAt(float TimeSeconds, const FRoomFEDSampler& Sampler,
			float& OutToxicFED, float& OutThermalFED) const;
	};

	/**
	 * Identity of the (agent file, B-Risk file, settings) triple a timeline was
	 * built against. Layer 1 (intervals) depends only on the agent + scenario
	 * generations; Layer 2 (failures) additionally depends on the settings hash.
	 */
	struct FAgentTimelineKey
	{
		uint32 AgentDataGeneration = 0;
		uint64 ScenarioGeneration = 0;
		uint32 SettingsHash = 0; // Layer-2 component; Layer 1 ignores it
		bool LayerOneEquals(const FAgentTimelineKey& O) const
			{ return AgentDataGeneration == O.AgentDataGeneration && ScenarioGeneration == O.ScenarioGeneration; }
		bool operator==(const FAgentTimelineKey& O) const
			{ return LayerOneEquals(O) && SettingsHash == O.SettingsHash; }
		bool operator!=(const FAgentTimelineKey& O) const { return !(*this == O); }
	};

	/** All agents' timelines built against one key. */
	struct FAgentTimelineSet
	{
		TMap<int32 /*EntityID*/, FAgentTenabilityTimeline> Timelines;
		FAgentTimelineKey BuiltKey;
	};

	/**
	 * Layer 2 — settings-dependent failure precompute.
	 *
	 * Fills the failure fields on a timeline from its Layer-1 intervals, the room
	 * cumulative-tenability curves and the analysis settings. PURE (no UObject
	 * access): callable from the build job on a worker thread (initial build) or the
	 * game thread (a cheap settings-only rebuild).
	 *
	 * Per-criterion first-crossing times are found by walking, within each occupancy
	 * interval, the room table's samples clipped to [Entry, Exit] and solving the
	 * predicate's crossing linearly between the bracketing samples. FED predicates
	 * are solved against the AGENT dose curve (Prior + Sample - EntryFED), which is
	 * itself piecewise-linear between B-Risk samples; the instantaneous predicates
	 * (visibility / temperature / layer height) are solved against the raw room curve.
	 *
	 * The criterion predicates, per-field bHas* gating and failure priority order
	 * MIRROR ComputeInstantaneousTenability (formerly UpdateAgentTenability) in
	 * AgentEgressTenabilityFragments.h exactly. A
	 * channel absent at the bracketing samples means that criterion is SKIPPED for
	 * that span (never fabricated — scientific-integrity invariant 3). Because the
	 * offline pass has only the room table (no live per-agent raw sample), the
	 * temperature criterion is additionally skipped whenever the bracketing samples
	 * lack an upper/lower layer temperature — there is no raw fallback offline.
	 *
	 * @param Timeline              In/out: intervals are read; failure fields are filled.
	 * @param Tables                All room tenability tables (indexed by RoomIndexToTableIndex).
	 * @param RoomIndexToTableIndex Maps a timeline RoomIndex to an index into Tables
	 *                              (INDEX_NONE when no table exists for that room).
	 * @param Settings              Endpoints + which criteria are enabled.
	 * @param PoseSampler           Resolves the agent's trajectory pose at a time for
	 *                              FailureLocation/Rotation; pass an unset TFunction to
	 *                              skip pose capture (settings-only rebuilds fetch poses
	 *                              lazily afterwards on the game thread).
	 */
	PROJECTMOBIUS_API void ComputeFailureData(
		FAgentTenabilityTimeline& Timeline,
		TConstArrayView<FBRiskTenabilityRoomTable> Tables,
		TConstArrayView<int32> RoomIndexToTableIndex,
		const FTenabilityAnalysisSettings& Settings,
		const TFunction<bool(float TimeSeconds, FVector& OutLoc, FRotator& OutRot)>& PoseSampler);

	/**
	 * Incremental Layer-1 builder. Feed timesteps in ascending order; Finish()
	 * closes open intervals at each agent's last-seen sample time. Pure — no
	 * UObject access, safe off the game thread.
	 */
	class PROJECTMOBIUS_API FAgentTimelineSetBuilder
	{
	public:
		FAgentTimelineSetBuilder(
			TArray<FRoomVolume> InRoomVolumes,    // copied — thread-safe snapshot
			TArray<int32> InRoomIds,              // parallel to volumes
			TFunction<void(int32, double, double&, double&)> InFEDSampler);

		void AddTimestep(float TimeSeconds, TConstArrayView<FSimMovementSample> Samples);
		FAgentTimelineSet Finish();

	private:
		struct FOpenInterval
		{
			int32 IntervalIndex = INDEX_NONE; // into that agent's Intervals
			int32 RoomIndex = INDEX_NONE;
			float LastSeenTimeSeconds = 0.0f;
			FVector LastSeenLocation = FVector::ZeroVector;
			FRotator LastSeenRotation = FRotator::ZeroRotator;
		};
		void CloseInterval(int32 EntityID, FOpenInterval& OpenInterval, float CloseTimeSeconds);

		TArray<FRoomVolume> RoomVolumes;
		TArray<int32> RoomIds;
		TFunction<void(int32, double, double&, double&)> FEDSampler;
		FAgentTimelineSet Result;
		TMap<int32, FOpenInterval> Open;
		// Agents present in the current AddTimestep call — used to detect agents that
		// left the dataset (absent this step) so their open interval closes at LastSeen.
		TSet<int32> SeenThisStep;
	};
}
