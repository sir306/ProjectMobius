// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/AgentTenabilityTimeline.h"

#include "Algo/BinarySearch.h"
#include "BRiskDataImporter.h"                                  // FBRiskTenabilityRoomTable / FBRiskTenabilitySample (Layer 2)
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h" // FSimMovementSample (full type)

namespace UE::Mobius::Tenability
{
	FRoomVolume MakeRoomVolume(const FBox& Bounds, TArray<FVector2D> FootprintPolygonCm)
	{
		FRoomVolume Volume;
		Volume.Bounds = Bounds;
		Volume.FootprintPolygonCm = MoveTemp(FootprintPolygonCm);

		// A polygon room measures its TRUE plan area (shoelace, made sign-independent since the ring
		// may arrive either winding) so a non-convex corridor cannot out-claim a small room sitting
		// inside its bounding box. A room with no polygon multiplies the box extents in X, Y, Z
		// order — the same sequence and associativity as the FBox::GetVolume() this metric replaced,
		// so rectangle-only scenarios break ties bit-identically to before.
		const FVector Extent = Bounds.IsValid ? (Bounds.Max - Bounds.Min) : FVector::ZeroVector;
		const double PlanAreaCm2 = Volume.FootprintPolygonCm.Num() >= 3
			? FMath::Abs(BRiskCoord::SignedRingArea(Volume.FootprintPolygonCm))
			: Extent.X * Extent.Y;
		Volume.SpecificityVolumeCm3 = PlanAreaCm2 * Extent.Z;

		return Volume;
	}

	namespace
	{
		/**
		 * Is WorldLocation inside this room?
		 *
		 * The bounding box answers the Z slab and rejects the great majority of rooms in a couple
		 * of compares; only a box hit pays for the ring walk. That ordering matters because this
		 * runs once per agent per frame on the MASS path.
		 *
		 * A room with no polygon is its bounding box, so the box result stands alone - which is
		 * what keeps rectangle-only (.smv-without-JSON) scenarios resolving exactly as before.
		 */
		FORCEINLINE bool RoomContains(const FRoomVolume& Room, const FVector& WorldLocation)
		{
			if (!Room.Bounds.IsInsideOrOn(WorldLocation))
			{
				return false;
			}

			return Room.FootprintPolygonCm.Num() < 3
				|| BRiskCoord::IsPointInRing(
					Room.FootprintPolygonCm, FVector2D(WorldLocation.X, WorldLocation.Y));
		}
	}

	int32 ResolveRoomIndexAtLocation(
		const TConstArrayView<FRoomVolume> RoomVolumes,
		const FVector& WorldLocation,
		const int32 PreferredRoomIndex)
	{
		// Rule extracted verbatim from UBRiskEgressSubsystem::FindRoomStateAtLocation
		// (scientific-integrity invariant 4: offline and live resolution are one function).
		//
		// 1. Preferred-room stickiness: if the preferred room is valid and still
		//    contains the location, keep it.
		if (RoomVolumes.IsValidIndex(PreferredRoomIndex)
			&& RoomContains(RoomVolumes[PreferredRoomIndex], WorldLocation))
		{
			return PreferredRoomIndex;
		}

		// 2. When rooms overlap (e.g. a corridor modelled inside a larger B-Risk zone),
		//    the smallest enclosing volume is the most specific spatial match for the agent.
		int32 BestIndex = INDEX_NONE;
		double BestVolume = TNumericLimits<double>::Max();

		for (int32 RoomIndex = 0; RoomIndex < RoomVolumes.Num(); ++RoomIndex)
		{
			if (RoomIndex == PreferredRoomIndex)
			{
				continue;
			}

			const FRoomVolume& Room = RoomVolumes[RoomIndex];
			if (!RoomContains(Room, WorldLocation))
			{
				continue;
			}

			if (Room.SpecificityVolumeCm3 < BestVolume)
			{
				BestVolume = Room.SpecificityVolumeCm3;
				BestIndex = RoomIndex;
			}
		}

		// 3. Outside every room. For a polygon room that now includes the parts of its bounding
		//    box the room does not actually occupy — deliberately, see the header.
		return BestIndex;
	}

	namespace
	{
		/**
		 * THE rule for "which occupancy interval is this agent's DOSE read from at time t": the last
		 * interval whose entry time is <= t, or INDEX_NONE before the first entry / with no intervals at
		 * all. On a room change (Exit_k == Entry_k+1) this picks the entering interval, which is right for
		 * dose - its Prior already banks everything completed through the leaving interval.
		 *
		 * Deliberately NOT used for the at-failure snapshot's instantaneous channels: those belong to the
		 * room that produced the crossing, which at a boundary is the interval being LEFT. See
		 * ComputeFailureData's SlotInterval.
		 */
		int32 FindIntervalIndexAtTime(
			const TConstArrayView<FAgentRoomOccupancyInterval> Intervals, const float TimeSeconds)
		{
			if (Intervals.Num() == 0 || TimeSeconds < Intervals[0].EntryTimeSeconds)
			{
				return INDEX_NONE;
			}
			return Algo::UpperBoundBy(Intervals, TimeSeconds,
				&FAgentRoomOccupancyInterval::EntryTimeSeconds) - 1;
		}
	}

	void FAgentTenabilityTimeline::DoseAt(const float TimeSeconds, const FRoomFEDSampler& Sampler,
		float& OutToxicFED, float& OutThermalFED) const
	{
		OutToxicFED = 0.0f;
		OutThermalFED = 0.0f;
		const int32 Index = FindIntervalIndexAtTime(Intervals, TimeSeconds);
		if (Index == INDEX_NONE)
		{
			return;
		}
		const FAgentRoomOccupancyInterval& Iv = Intervals[Index];
		if (TimeSeconds >= Iv.ExitTimeSeconds)
		{
			// In the gap after Iv (or past the end): dose is the completed total through Iv.
			OutToxicFED = Iv.PriorToxicFED + FMath::Max(Iv.ExitToxicFED - Iv.EntryToxicFED, 0.0f);
			OutThermalFED = Iv.PriorThermalFED + FMath::Max(Iv.ExitThermalFED - Iv.EntryThermalFED, 0.0f);
			return;
		}
		// Inside Iv: partial accrual from the room's cumulative curve at TimeSeconds.
		double Toxic = 0.0, Thermal = 0.0;
		Sampler(Iv.RoomIndex, TimeSeconds, Toxic, Thermal);
		OutToxicFED = Iv.PriorToxicFED + FMath::Max(static_cast<float>(Toxic) - Iv.EntryToxicFED, 0.0f);
		OutThermalFED = Iv.PriorThermalFED + FMath::Max(static_cast<float>(Thermal) - Iv.EntryThermalFED, 0.0f);
	}

	FAgentTimelineSetBuilder::FAgentTimelineSetBuilder(
		TArray<FRoomVolume> InRoomVolumes,
		TArray<int32> InRoomIds,
		TFunction<void(int32, double, double&, double&)> InFEDSampler)
		: RoomVolumes(MoveTemp(InRoomVolumes))
		, RoomIds(MoveTemp(InRoomIds))
		, FEDSampler(MoveTemp(InFEDSampler))
	{
	}

	void FAgentTimelineSetBuilder::AddTimestep(const float TimeSeconds, const TConstArrayView<FSimMovementSample> Samples)
	{
		SeenThisStep.Reset();
		SeenThisStep.Reserve(Samples.Num());

		for (const FSimMovementSample& Sample : Samples)
		{
			const int32 EntityID = Sample.EntityID;
			SeenThisStep.Add(EntityID);

			FOpenInterval* Existing = Open.Find(EntityID);
			const int32 PreferredRoomIndex = Existing ? Existing->RoomIndex : INDEX_NONE;
			const int32 Room = ResolveRoomIndexAtLocation(RoomVolumes, Sample.Position, PreferredRoomIndex);

			if (Existing)
			{
				if (Existing->RoomIndex == Room)
				{
					// Same room (INDEX_NONE stays INDEX_NONE — still outside). Just advance last-seen.
					Existing->LastSeenTimeSeconds = TimeSeconds;
					Existing->LastSeenLocation = Sample.Position;
					Existing->LastSeenRotation = Sample.Rotation;
					continue;
				}

				// Room changed. Close the currently-open interval (if any) at THIS timestep's
				// time — the sample resolution quantizes the room-boundary crossing to the step
				// (integrity invariant 5). Then fall through to (re)open below.
				CloseInterval(EntityID, *Existing, TimeSeconds);
			}

			if (Room == INDEX_NONE)
			{
				// Outside every room (corridor). No interval while outside — dose is flat.
				// Drop the open-interval tracking so a later re-entry opens a fresh interval
				// (late-entrant rule: entry baseline; no dose inherited across the gap).
				Open.Remove(EntityID);
				continue;
			}

			// Open a new interval for this room. Entry FED is the room's cumulative curve
			// value at THIS time; Prior is carried from the just-closed interval (or 0).
			FAgentTenabilityTimeline& Timeline = Result.Timelines.FindOrAdd(EntityID);
			FAgentRoomOccupancyInterval NewInterval;
			NewInterval.RoomIndex = Room;
			NewInterval.RoomId = RoomIds.IsValidIndex(Room) ? RoomIds[Room] : INDEX_NONE;
			NewInterval.EntryTimeSeconds = TimeSeconds;
			NewInterval.ExitTimeSeconds = TimeSeconds; // provisional until closed

			double EntryToxic = 0.0, EntryThermal = 0.0;
			if (FEDSampler)
			{
				FEDSampler(Room, static_cast<double>(TimeSeconds), EntryToxic, EntryThermal);
			}
			NewInterval.EntryToxicFED = static_cast<float>(EntryToxic);
			NewInterval.EntryThermalFED = static_cast<float>(EntryThermal);
			NewInterval.ExitToxicFED = NewInterval.EntryToxicFED;
			NewInterval.ExitThermalFED = NewInterval.EntryThermalFED;

			// Prior = previous interval's Prior + its clamped contribution.
			if (Timeline.Intervals.Num() > 0)
			{
				const FAgentRoomOccupancyInterval& Prev = Timeline.Intervals.Last();
				NewInterval.PriorToxicFED = Prev.PriorToxicFED
					+ FMath::Max(Prev.ExitToxicFED - Prev.EntryToxicFED, 0.0f);
				NewInterval.PriorThermalFED = Prev.PriorThermalFED
					+ FMath::Max(Prev.ExitThermalFED - Prev.EntryThermalFED, 0.0f);
			}

			const int32 IntervalIndex = Timeline.Intervals.Add(NewInterval);

			FOpenInterval NewOpen;
			NewOpen.IntervalIndex = IntervalIndex;
			NewOpen.RoomIndex = Room;
			NewOpen.LastSeenTimeSeconds = TimeSeconds;
			NewOpen.LastSeenLocation = Sample.Position;
			NewOpen.LastSeenRotation = Sample.Rotation;
			Open.Add(EntityID, NewOpen);
		}

		// Agents that were open but ABSENT from this timestep (left the dataset) close at
		// their own last-seen time — not this timestep's time (they were not here to cross).
		for (auto It = Open.CreateIterator(); It; ++It)
		{
			if (SeenThisStep.Contains(It.Key()))
			{
				continue;
			}
			CloseInterval(It.Key(), It.Value(), It.Value().LastSeenTimeSeconds);
			It.RemoveCurrent();
		}
	}

	void FAgentTimelineSetBuilder::CloseInterval(const int32 EntityID, FOpenInterval& OpenInterval, const float CloseTimeSeconds)
	{
		if (OpenInterval.IntervalIndex == INDEX_NONE)
		{
			return;
		}
		FAgentTenabilityTimeline* Timeline = Result.Timelines.Find(EntityID);
		if (!Timeline || !Timeline->Intervals.IsValidIndex(OpenInterval.IntervalIndex))
		{
			return;
		}

		FAgentRoomOccupancyInterval& Iv = Timeline->Intervals[OpenInterval.IntervalIndex];
		Iv.ExitTimeSeconds = CloseTimeSeconds;

		double ExitToxic = 0.0, ExitThermal = 0.0;
		if (FEDSampler)
		{
			FEDSampler(Iv.RoomIndex, static_cast<double>(CloseTimeSeconds), ExitToxic, ExitThermal);
		}
		Iv.ExitToxicFED = static_cast<float>(ExitToxic);
		Iv.ExitThermalFED = static_cast<float>(ExitThermal);

		// Mark this open record consumed so a double close (room change followed by the
		// absent-agent sweep) is a no-op.
		OpenInterval.IntervalIndex = INDEX_NONE;
	}

	FAgentTimelineSet FAgentTimelineSetBuilder::Finish()
	{
		// Close any still-open intervals at each agent's final observed time.
		for (auto It = Open.CreateIterator(); It; ++It)
		{
			CloseInterval(It.Key(), It.Value(), It.Value().LastSeenTimeSeconds);
		}
		Open.Reset();
		return MoveTemp(Result);
	}

	// ===============================================================================================
	// Layer 2 — failure precompute (crossings + pose)
	// ===============================================================================================
	//
	// Every predicate below is a line-by-line mirror of ComputeInstantaneousTenability (formerly
	// UpdateAgentTenability) in AgentEgressTenabilityFragments.h. The mapping (source predicate ->
	// offline predicate):
	//
	//   Visibility   Tenability.CurrentVisibilityM <= Settings.EndpointVisibilityM         (gate: bUseVisibilityCriterion && bHasCalcVisibility)
	//                -> raw VisibilityM(t) <= EndpointVisibilityM                            (gate: bUseVisibilityCriterion && bHasVisibility[Lo]&&[Hi])
	//   ToxicFED     Tenability.AccumulatedToxicFED >= Settings.EndpointToxicFED            (gate: bUseToxicFEDCriterion && bHasCalcFEDSum)
	//                -> dose(t) = Prior + FEDSum(t) - EntryToxicFED >= EndpointToxicFED      (gate: bUseToxicFEDCriterion && bHasFEDSum[Lo]&&[Hi])
	//   ThermalFED   Tenability.AccumulatedThermalFED >= Settings.EndpointThermalFED        (gate: bUseThermalFEDCriterion && bHasCalcFEDRadSum)
	//                -> dose(t) = Prior + FEDRadSum(t) - EntryThermalFED >= EndpointThermalFED (gate: bUseThermalFEDCriterion && bHasFEDRadSum[Lo]&&[Hi])
	//   Temperature  Tenability.CurrentTemperatureC >= Settings.EndpointTemperatureC         (gate: bUseTemperatureCriterion)
	//                with monitor-layer selection: bMonitorInUpperLayer = bHasCalcLayerHeight
	//                  ? MonitorHeightM >= CalcLayerHeightM : bUpperLayer;
	//                  temp = bHasCalcTemperature ? (upper ? CalcUpperTemperatureC : CalcLowerTemperatureC) : raw.
	//                -> tempAtMonitor(t) >= EndpointTemperatureC, layer chosen by MonitorHeightM >= LayerHeightM(t).
	//                   The offline pass has NO live raw fallback and NO per-agent bUpperLayer, so it
	//                   requires bHasLayerHeight[Lo]&&[Hi] to pick a layer AND the selected layer's
	//                   temperature to be present at both endpoints; missing -> criterion skipped for the span.
	//   LayerHeight  Tenability.CurrentLayerHeightM <= Settings.MonitorHeightM              (gate: bUseLayerHeightCriterion && bHasCalcLayerHeight)
	//                -> raw LayerHeightM(t) <= MonitorHeightM                                 (gate: bUseLayerHeightCriterion && bHasLayerHeight[Lo]&&[Hi])
	//
	// Failure priority (mirrors the old UpdateAgentTenability FirstFailureCriterion ?: chain; that
	// chain now lives here, in ECriterionSlot's declaration order, since failure state is precomputed
	// offline rather than latched at runtime):
	//   Visibility > ToxicFED > ThermalFED > Temperature > LayerHeight.
	//
	// All curve arithmetic is done in double; only the stored fields are floats. bHas* gating uses the
	// same both-endpoints AND rule SampleTenabilityTableAtTime uses (a channel is usable across a
	// bracketing pair only when present in BOTH samples) so offline crossings agree with live display.

	namespace
	{
		// Fixed priority order shared by the failure fields and the first-failure tiebreak.
		enum class ECriterionSlot : uint8
		{
			Visibility = 0,
			ToxicFED,
			ThermalFED,
			Temperature,
			LayerHeight,
			Count
		};

		constexpr ETenabilityCriterion SlotToCriterion(const ECriterionSlot Slot)
		{
			switch (Slot)
			{
			case ECriterionSlot::Visibility:  return ETenabilityCriterion::Visibility;
			case ECriterionSlot::ToxicFED:    return ETenabilityCriterion::ToxicFED;
			case ECriterionSlot::ThermalFED:  return ETenabilityCriterion::ThermalFED;
			case ECriterionSlot::Temperature: return ETenabilityCriterion::Temperature;
			default:                          return ETenabilityCriterion::LayerHeight;
			}
		}

		constexpr uint8 SlotToFlag(const ECriterionSlot Slot)
		{
			switch (Slot)
			{
			case ECriterionSlot::Visibility:  return UE::Mobius::TenabilityFailureFlags::Visibility;
			case ECriterionSlot::ToxicFED:    return UE::Mobius::TenabilityFailureFlags::ToxicFED;
			case ECriterionSlot::ThermalFED:  return UE::Mobius::TenabilityFailureFlags::ThermalFED;
			case ECriterionSlot::Temperature: return UE::Mobius::TenabilityFailureFlags::Temperature;
			default:                          return UE::Mobius::TenabilityFailureFlags::LayerHeight;
			}
		}

		// Linear crossing of a value curve against a threshold on the sub-span [Ta, Tb] (Ta <= Tb),
		// where the value is ValA at Ta and ValB at Tb (already interpolated to the clipped endpoints).
		//  - bFailAtOrAbove: predicate is (value >= Threshold); false means (value <= Threshold).
		//  - Returns true and fills OutTime with the first time in [Ta, Tb] the predicate holds.
		//  - Already failing at Ta (the interval/span entry) -> crossing == Ta ("already failed at entry").
		bool SolveCrossing(
			const double Ta, const double ValA,
			const double Tb, const double ValB,
			const double Threshold, const bool bFailAtOrAbove,
			double& OutTime)
		{
			auto Fails = [Threshold, bFailAtOrAbove](const double V)
			{
				return bFailAtOrAbove ? (V >= Threshold) : (V <= Threshold);
			};

			// Already failing at the span entry: earliest failure is Ta itself.
			if (Fails(ValA))
			{
				OutTime = Ta;
				return true;
			}
			// Not failing anywhere on this span.
			if (!Fails(ValB))
			{
				return false;
			}
			// Fails at Tb but not Ta -> the curve crosses the threshold strictly inside (Ta, Tb].
			// Solve the linear interpolant Val(t) == Threshold. Guard a zero-width span (Ta==Tb):
			// ValA didn't fail but ValB does with Ta==Tb is contradictory for a single point, so
			// fall back to Tb.
			const double Span = Tb - Ta;
			if (Span <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				OutTime = Tb;
				return true;
			}
			const double Alpha = (Threshold - ValA) / (ValB - ValA);
			OutTime = Ta + FMath::Clamp(Alpha, 0.0, 1.0) * Span;
			return true;
		}

		/**
		 * The bracketing sample pair for time T, with the SAME semantics
		 * UBRiskEgressSubsystem::SampleTenabilityTableAtTime uses: a single-sample table, or a time at
		 * or before the first sample, clamps to the first sample; a time at or after the last clamps to
		 * the last; otherwise binary-search the pair. Lo == Hi in the clamp regions, which LerpField
		 * reads as a constant (zero-width span -> FieldA), so the same call site covers all three.
		 *
		 * Returns false only for an empty table. Exists so the at-failure snapshot interpolates channels
		 * exactly as the live sampler does - the values are compared against each other by
		 * ProjectMobius.BRisk.Tenability.FailurePrecompute, which drives the LIVE sampler.
		 */
		bool FindSamplePairAtTime(
			const FBRiskTenabilityRoomTable& Table, const double T,
			const FBRiskTenabilitySample*& OutLo, const FBRiskTenabilitySample*& OutHi)
		{
			const int32 NumSamples = Table.Samples.Num();
			if (NumSamples == 0)
			{
				return false;
			}
			if (NumSamples == 1 || T <= Table.Samples[0].SampleTimeSeconds)
			{
				OutLo = &Table.Samples[0];
				OutHi = OutLo;
				return true;
			}
			if (T >= Table.Samples.Last().SampleTimeSeconds)
			{
				OutLo = &Table.Samples.Last();
				OutHi = OutLo;
				return true;
			}

			int32 LowerIndex = 0;
			int32 UpperIndex = NumSamples - 1;
			while (UpperIndex - LowerIndex > 1)
			{
				const int32 MidIndex = (LowerIndex + UpperIndex) / 2;
				if (Table.Samples[MidIndex].SampleTimeSeconds <= T)
				{
					LowerIndex = MidIndex;
				}
				else
				{
					UpperIndex = MidIndex;
				}
			}
			OutLo = &Table.Samples[LowerIndex];
			OutHi = &Table.Samples[UpperIndex];
			return true;
		}

		// Interpolate a raw sample field linearly at time T between two bracketing samples.
		double LerpField(
			const double Ta, const double FieldA,
			const double Tb, const double FieldB,
			const double T)
		{
			const double Span = Tb - Ta;
			if (Span <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				return FieldA;
			}
			const double Alpha = FMath::Clamp((T - Ta) / Span, 0.0, 1.0);
			return FieldA + Alpha * (FieldB - FieldA);
		}
	}

	void ComputeFailureData(
		FAgentTenabilityTimeline& Timeline,
		const TConstArrayView<FBRiskTenabilityRoomTable> Tables,
		const TConstArrayView<int32> RoomIndexToTableIndex,
		const FTenabilityAnalysisSettings& Settings,
		const TFunction<bool(float TimeSeconds, FVector& OutLoc, FRotator& OutRot)>& PoseSampler)
	{
		// Reset Layer-2 fields so a rebuild over the same timeline is idempotent.
		Timeline.FirstFailureTimeSeconds = -1.0f;
		Timeline.FirstFailureCriterion = ETenabilityCriterion::None;
		Timeline.FirstFailureMask = UE::Mobius::TenabilityFailureFlags::None;
		Timeline.VisibilityFailureTimeSeconds = -1.0f;
		Timeline.ToxicFEDFailureTimeSeconds = -1.0f;
		Timeline.ThermalFEDFailureTimeSeconds = -1.0f;
		Timeline.TemperatureFailureTimeSeconds = -1.0f;
		Timeline.LayerHeightFailureTimeSeconds = -1.0f;
		Timeline.FailureLocation = FVector::ZeroVector;
		Timeline.FailureRotation = FRotator::ZeroRotator;
		// Reset here with the rest of Layer 2, not only on the failure path below: a rebuild whose new
		// settings remove the failure entirely must not leave the previous run's snapshot standing.
		Timeline.FailureSnapshot = FTenabilityFailureSnapshot();

		// Earliest crossing time per criterion slot (-1 = never). Filled by walking every interval;
		// intervals are ascending and non-overlapping, so the running minimum is the global first.
		double SlotTime[static_cast<int32>(ECriterionSlot::Count)];
		for (double& T : SlotTime)
		{
			T = -1.0;
		}
		// Which occupancy interval produced each slot's earliest crossing. Recorded rather than
		// re-derived from the time, because a crossing can land exactly ON a room change, where the
		// leaving interval's Exit equals the entering interval's Entry: "the interval containing t*" then
		// resolves to the room the agent walked INTO, whose conditions did not cause the failure. The
		// at-failure snapshot has to read the room that produced the crossing.
		int32 SlotInterval[static_cast<int32>(ECriterionSlot::Count)];
		for (int32& I : SlotInterval)
		{
			I = INDEX_NONE;
		}
		// Set by the interval loop below so RecordCrossing can attribute a crossing without threading the
		// index through EvaluatePair's signature.
		int32 CurrentIntervalIndex = INDEX_NONE;

		const double MonitorHeightM = static_cast<double>(Settings.MonitorHeightM);
		const double EndpointVisibilityM = static_cast<double>(Settings.EndpointVisibilityM);
		const double EndpointToxicFED = static_cast<double>(Settings.EndpointToxicFED);
		const double EndpointThermalFED = static_cast<double>(Settings.EndpointThermalFED);
		const double EndpointTemperatureC = static_cast<double>(Settings.EndpointTemperatureC);

		// Record a crossing for a slot, keeping the earliest - and the interval that produced it.
		auto RecordCrossing = [&SlotTime, &SlotInterval, &CurrentIntervalIndex](
			const ECriterionSlot Slot, const double Time)
		{
			const int32 Idx = static_cast<int32>(Slot);
			if (SlotTime[Idx] < 0.0 || Time < SlotTime[Idx])
			{
				SlotTime[Idx] = Time;
				SlotInterval[Idx] = CurrentIntervalIndex;
			}
		};

		for (int32 IntervalIdx = 0; IntervalIdx < Timeline.Intervals.Num(); ++IntervalIdx)
		{
			const FAgentRoomOccupancyInterval& Interval = Timeline.Intervals[IntervalIdx];
			CurrentIntervalIndex = IntervalIdx;
			if (!RoomIndexToTableIndex.IsValidIndex(Interval.RoomIndex))
			{
				continue;
			}
			const int32 TableIndex = RoomIndexToTableIndex[Interval.RoomIndex];
			if (!Tables.IsValidIndex(TableIndex))
			{
				continue; // no curve for this room -> no crossings from this span (never fabricate)
			}
			const FBRiskTenabilityRoomTable& Table = Tables[TableIndex];
			if (Table.Samples.Num() == 0)
			{
				continue; // empty table -> nothing to solve
			}

			const double Entry = static_cast<double>(Interval.EntryTimeSeconds);
			const double Exit = static_cast<double>(Interval.ExitTimeSeconds);
			const double EntryToxicFED = static_cast<double>(Interval.EntryToxicFED);
			const double EntryThermalFED = static_cast<double>(Interval.EntryThermalFED);
			const double PriorToxic = static_cast<double>(Interval.PriorToxicFED);
			const double PriorThermal = static_cast<double>(Interval.PriorThermalFED);

			// Walk each bracketing sample PAIR [i, i+1] whose time range intersects [Entry, Exit].
			// A single-sample table (or a span before the first / after the last sample) still needs
			// covering: clamp against the ends by treating the degenerate pair [last,last] etc. We do
			// this by iterating pairs and also seeding a synthetic clamp span at the extremes.
			const int32 NumSamples = Table.Samples.Num();

			// Helper that evaluates one bracketing pair (Lo, Hi) over the clipped sub-span [A, B]
			// (A <= B) and records any crossings. Lo/Hi are the raw samples bracketing the sub-span;
			// A/B are clamped into [Entry, Exit] AND into [Lo.t, Hi.t].
			auto EvaluatePair = [&](const FBRiskTenabilitySample& Lo, const FBRiskTenabilitySample& Hi,
				const double A, const double B)
			{
				if (B < A)
				{
					return;
				}
				const double LoT = Lo.SampleTimeSeconds;
				const double HiT = Hi.SampleTimeSeconds;

				// --- Visibility: raw VisibilityM(t) <= EndpointVisibilityM ---
				if (Settings.bUseVisibilityCriterion && Lo.bHasVisibility && Hi.bHasVisibility)
				{
					const double VA = LerpField(LoT, Lo.VisibilityM, HiT, Hi.VisibilityM, A);
					const double VB = LerpField(LoT, Lo.VisibilityM, HiT, Hi.VisibilityM, B);
					double Cross = 0.0;
					if (SolveCrossing(A, VA, B, VB, EndpointVisibilityM, /*bFailAtOrAbove*/false, Cross))
					{
						RecordCrossing(ECriterionSlot::Visibility, Cross);
					}
				}

				// --- Toxic FED: dose(t) = Prior + FEDSum(t) - EntryToxicFED >= EndpointToxicFED ---
				if (Settings.bUseToxicFEDCriterion && Lo.bHasFEDSum && Hi.bHasFEDSum)
				{
					const double RawA = LerpField(LoT, Lo.FEDSum, HiT, Hi.FEDSum, A);
					const double RawB = LerpField(LoT, Lo.FEDSum, HiT, Hi.FEDSum, B);
					const double DoseA = PriorToxic + FMath::Max(RawA - EntryToxicFED, 0.0);
					const double DoseB = PriorToxic + FMath::Max(RawB - EntryToxicFED, 0.0);
					double Cross = 0.0;
					if (SolveCrossing(A, DoseA, B, DoseB, EndpointToxicFED, /*bFailAtOrAbove*/true, Cross))
					{
						RecordCrossing(ECriterionSlot::ToxicFED, Cross);
					}
				}

				// --- Thermal FED: dose(t) = Prior + FEDRadSum(t) - EntryThermalFED >= EndpointThermalFED ---
				if (Settings.bUseThermalFEDCriterion && Lo.bHasFEDRadSum && Hi.bHasFEDRadSum)
				{
					const double RawA = LerpField(LoT, Lo.FEDRadSum, HiT, Hi.FEDRadSum, A);
					const double RawB = LerpField(LoT, Lo.FEDRadSum, HiT, Hi.FEDRadSum, B);
					const double DoseA = PriorThermal + FMath::Max(RawA - EntryThermalFED, 0.0);
					const double DoseB = PriorThermal + FMath::Max(RawB - EntryThermalFED, 0.0);
					double Cross = 0.0;
					if (SolveCrossing(A, DoseA, B, DoseB, EndpointThermalFED, /*bFailAtOrAbove*/true, Cross))
					{
						RecordCrossing(ECriterionSlot::ThermalFED, Cross);
					}
				}

				// --- Temperature: tempAtMonitor(t) >= EndpointTemperatureC, layer via MonitorHeightM ---
				// Mirror of ComputeInstantaneousTenability's layer selection. Offline requires layer height present
				// (to pick a layer) AND the selected layer's temperature present at BOTH endpoints; there
				// is no live raw-sample fallback offline, so an absent channel skips the criterion.
				if (Settings.bUseTemperatureCriterion && Lo.bHasLayerHeight && Hi.bHasLayerHeight)
				{
					const double LayerA = LerpField(LoT, Lo.LayerHeightM, HiT, Hi.LayerHeightM, A);
					const double LayerB = LerpField(LoT, Lo.LayerHeightM, HiT, Hi.LayerHeightM, B);
					const bool bMonitorInUpperA = MonitorHeightM >= LayerA;
					const bool bMonitorInUpperB = MonitorHeightM >= LayerB;

					// Selected-layer temperature availability at each endpoint (both bracketing samples).
					auto TempAvailable = [&](const bool bUpper)
					{
						return bUpper
							? (Lo.bHasUpperTemperature && Hi.bHasUpperTemperature)
							: (Lo.bHasLowerTemperature && Hi.bHasLowerTemperature);
					};
					auto TempAt = [&](const double T, const bool bUpper)
					{
						return bUpper
							? LerpField(LoT, Lo.UpperTemperatureC, HiT, Hi.UpperTemperatureC, T)
							: LerpField(LoT, Lo.LowerTemperatureC, HiT, Hi.LowerTemperatureC, T);
					};

					// The chosen layer can differ between A and B if the interface crosses the monitor
					// height within the span. Only solve when the layer choice is stable across [A,B] and
					// the chosen layer's temperature is present; otherwise skip (no fabricated value).
					if (bMonitorInUpperA == bMonitorInUpperB && TempAvailable(bMonitorInUpperA))
					{
						const double TA = TempAt(A, bMonitorInUpperA);
						const double TB = TempAt(B, bMonitorInUpperB);
						double Cross = 0.0;
						if (SolveCrossing(A, TA, B, TB, EndpointTemperatureC, /*bFailAtOrAbove*/true, Cross))
						{
							RecordCrossing(ECriterionSlot::Temperature, Cross);
						}
					}
				}

				// --- Layer height: raw LayerHeightM(t) <= MonitorHeightM ---
				if (Settings.bUseLayerHeightCriterion && Lo.bHasLayerHeight && Hi.bHasLayerHeight)
				{
					const double LA = LerpField(LoT, Lo.LayerHeightM, HiT, Hi.LayerHeightM, A);
					const double LB = LerpField(LoT, Lo.LayerHeightM, HiT, Hi.LayerHeightM, B);
					double Cross = 0.0;
					if (SolveCrossing(A, LA, B, LB, MonitorHeightM, /*bFailAtOrAbove*/false, Cross))
					{
						RecordCrossing(ECriterionSlot::LayerHeight, Cross);
					}
				}
			};

			if (NumSamples == 1)
			{
				// Degenerate single-sample table: SampleTenabilityTableAtTime clamps to the one sample
				// everywhere, so the value is constant across [Entry, Exit]. Evaluate the constant pair.
				EvaluatePair(Table.Samples[0], Table.Samples[0], Entry, Exit);
			}
			else
			{
				// Span BEFORE the first sample (t < first sample time): the live interpolator clamps to
				// the first sample, so treat it as a constant span. Only when the interval starts before
				// the first recorded time.
				if (Entry < Table.Samples[0].SampleTimeSeconds)
				{
					const double A = Entry;
					const double B = FMath::Min(Exit, Table.Samples[0].SampleTimeSeconds);
					EvaluatePair(Table.Samples[0], Table.Samples[0], A, B);
				}

				for (int32 i = 0; i + 1 < NumSamples; ++i)
				{
					const FBRiskTenabilitySample& Lo = Table.Samples[i];
					const FBRiskTenabilitySample& Hi = Table.Samples[i + 1];
					// Clip this pair's own time range to the occupancy interval.
					const double A = FMath::Max(Entry, Lo.SampleTimeSeconds);
					const double B = FMath::Min(Exit, Hi.SampleTimeSeconds);
					if (B < A)
					{
						continue; // pair does not intersect the occupancy interval
					}
					EvaluatePair(Lo, Hi, A, B);
				}

				// Span AFTER the last sample (t > last sample time): clamp to the last sample.
				const double LastT = Table.Samples.Last().SampleTimeSeconds;
				if (Exit > LastT)
				{
					const double A = FMath::Max(Entry, LastT);
					const double B = Exit;
					EvaluatePair(Table.Samples.Last(), Table.Samples.Last(), A, B);
				}
			}
		}

		// Publish per-criterion failure times.
		Timeline.VisibilityFailureTimeSeconds = SlotTime[static_cast<int32>(ECriterionSlot::Visibility)] < 0.0
			? -1.0f : static_cast<float>(SlotTime[static_cast<int32>(ECriterionSlot::Visibility)]);
		Timeline.ToxicFEDFailureTimeSeconds = SlotTime[static_cast<int32>(ECriterionSlot::ToxicFED)] < 0.0
			? -1.0f : static_cast<float>(SlotTime[static_cast<int32>(ECriterionSlot::ToxicFED)]);
		Timeline.ThermalFEDFailureTimeSeconds = SlotTime[static_cast<int32>(ECriterionSlot::ThermalFED)] < 0.0
			? -1.0f : static_cast<float>(SlotTime[static_cast<int32>(ECriterionSlot::ThermalFED)]);
		Timeline.TemperatureFailureTimeSeconds = SlotTime[static_cast<int32>(ECriterionSlot::Temperature)] < 0.0
			? -1.0f : static_cast<float>(SlotTime[static_cast<int32>(ECriterionSlot::Temperature)]);
		Timeline.LayerHeightFailureTimeSeconds = SlotTime[static_cast<int32>(ECriterionSlot::LayerHeight)] < 0.0
			? -1.0f : static_cast<float>(SlotTime[static_cast<int32>(ECriterionSlot::LayerHeight)]);

		// Overall first failure = smallest crossing time. Ties are broken by the priority order
		// Visibility > ToxicFED > ThermalFED > Temperature > LayerHeight (the slot enum order), matching
		// the old UpdateAgentTenability FirstFailureCriterion ?: chain (now precomputed here instead of
		// latched at runtime). The mask collects EVERY criterion whose crossing time equals the
		// first-failure time (simultaneous failures all recorded).
		double FirstTime = -1.0;
		ECriterionSlot FirstSlot = ECriterionSlot::Count;
		for (int32 Idx = 0; Idx < static_cast<int32>(ECriterionSlot::Count); ++Idx)
		{
			const double T = SlotTime[Idx];
			if (T < 0.0)
			{
				continue;
			}
			// Strictly-earlier wins; on an exact tie the earlier slot (higher priority) is kept because
			// we only replace on a strict decrease and iterate in priority order.
			if (FirstTime < 0.0 || T < FirstTime)
			{
				FirstTime = T;
				FirstSlot = static_cast<ECriterionSlot>(Idx);
			}
		}

		if (FirstTime < 0.0)
		{
			return; // no failure on this timeline
		}

		Timeline.FirstFailureTimeSeconds = static_cast<float>(FirstTime);
		Timeline.FirstFailureCriterion = SlotToCriterion(FirstSlot);

		// Mask = every criterion whose crossing coincides with the first-failure time (simultaneous
		// failures at the same sample time all recorded — mirrors ComputeInstantaneousTenability's
		// current-frame FailureMask, which ORs all bXxxFailed flags true in the failing frame). Because
		// FirstTime is the MINIMUM
		// crossing time, no criterion can cross strictly earlier, so T <= FirstTime + eps selects exactly
		// the criteria that cross AT FirstTime (the small epsilon absorbs float round-off between two
		// same-time linear solves rather than demanding a bitwise-equal time).
		uint8 Mask = UE::Mobius::TenabilityFailureFlags::None;
		for (int32 Idx = 0; Idx < static_cast<int32>(ECriterionSlot::Count); ++Idx)
		{
			const double T = SlotTime[Idx];
			if (T >= 0.0 && T <= FirstTime + UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				Mask |= SlotToFlag(static_cast<ECriterionSlot>(Idx));
			}
		}
		Timeline.FirstFailureMask = Mask;

		// ---------------------------------------------------------------------------------------------
		// At-failure snapshot: the criterion values a post-failure readout shows, evaluated at the
		// crossing time. This is what makes the post-failure display navigation-independent - see
		// FTenabilityFailureSnapshot and ApplyFailureSnapshot.
		// ---------------------------------------------------------------------------------------------
		{
			FTenabilityFailureSnapshot& Snapshot = Timeline.FailureSnapshot;
			const float FailureTime = Timeline.FirstFailureTimeSeconds;

			// Dose: the SAME closed-form query the live frame makes (DoseAt itself, not a re-derivation),
			// asked at the failure time instead of the current time. The sampler mirrors
			// UBRiskEgressSubsystem::SampleTenabilityDoseAtRoomIndex, which copies the interpolated curve
			// value with NO bHas* gate - presence gates the risk below, not the raw dose lookup.
			auto DoseSampler = [&Tables, &RoomIndexToTableIndex](
				const int32 RoomIndex, const double TimeSeconds, double& OutToxic, double& OutThermal)
			{
				if (!RoomIndexToTableIndex.IsValidIndex(RoomIndex))
				{
					return;
				}
				const int32 SamplerTableIndex = RoomIndexToTableIndex[RoomIndex];
				if (!Tables.IsValidIndex(SamplerTableIndex))
				{
					return;
				}
				const FBRiskTenabilitySample* Lo = nullptr;
				const FBRiskTenabilitySample* Hi = nullptr;
				if (!FindSamplePairAtTime(Tables[SamplerTableIndex], TimeSeconds, Lo, Hi))
				{
					return;
				}
				OutToxic = LerpField(
					Lo->SampleTimeSeconds, Lo->FEDSum, Hi->SampleTimeSeconds, Hi->FEDSum, TimeSeconds);
				OutThermal = LerpField(
					Lo->SampleTimeSeconds, Lo->FEDRadSum, Hi->SampleTimeSeconds, Hi->FEDRadSum, TimeSeconds);
			};
			const FRoomFEDSampler DoseSamplerRef(DoseSampler);
			Timeline.DoseAt(
				FailureTime, DoseSamplerRef, Snapshot.AccumulatedToxicFED, Snapshot.AccumulatedThermalFED);

			// Instantaneous channels come from the room that PRODUCED the crossing - the interval recorded
			// alongside the winning slot's time, not "the interval containing FailureTime". The two differ
			// when the failure lands exactly on a room change, and there the recorded one is the honest
			// answer: the agent was stopped by the room it was leaving, so reporting the room it stepped
			// into (routinely a clear one) would describe conditions that did not cause the failure.
			//
			// Dose deliberately does NOT follow this room: it is cumulative, and at a boundary the
			// entering interval's Prior already equals the completed dose through the leaving one, so
			// DoseAt's own interval rule is correct there. Channels are per-room; dose is per-agent.
			const int32 IntervalIndex = SlotInterval[static_cast<int32>(FirstSlot)];
			const FBRiskTenabilitySample* Lo = nullptr;
			const FBRiskTenabilitySample* Hi = nullptr;
			if (Timeline.Intervals.IsValidIndex(IntervalIndex))
			{
				const int32 FailureRoomIndex = Timeline.Intervals[IntervalIndex].RoomIndex;
				if (RoomIndexToTableIndex.IsValidIndex(FailureRoomIndex))
				{
					const int32 FailureTableIndex = RoomIndexToTableIndex[FailureRoomIndex];
					if (Tables.IsValidIndex(FailureTableIndex))
					{
						FindSamplePairAtTime(Tables[FailureTableIndex], FailureTime, Lo, Hi);
					}
				}
			}

			// No curve for the failure instant (the failing span's room has no table): every channel
			// keeps its default and every risk stays zero. That is what a LIVE frame there reads too -
			// the room state's Calc* fields are never filled - so the two still agree, and nothing is
			// fabricated for a room B-Risk did not publish.
			if (Lo && Hi)
			{
				const double LoT = Lo->SampleTimeSeconds;
				const double HiT = Hi->SampleTimeSeconds;
				const double T = static_cast<double>(FailureTime);

				// Raw channels are copied unconditionally, exactly as ResolveTypedRoomState copies them
				// into the room state before ComputeInstantaneousTenability reads them: an absent channel
				// leaves the parsed sample at its struct default, so the interpolation of an absent
				// channel IS the default. Presence gates the RISK, below.
				const double LayerHeightAtFailure = LerpField(LoT, Lo->LayerHeightM, HiT, Hi->LayerHeightM, T);
				Snapshot.VisibilityM = static_cast<float>(LerpField(LoT, Lo->VisibilityM, HiT, Hi->VisibilityM, T));
				Snapshot.LayerHeightM = static_cast<float>(LayerHeightAtFailure);
				Snapshot.HeatReleaseKW = static_cast<float>(LerpField(LoT, Lo->HeatReleaseKW, HiT, Hi->HeatReleaseKW, T));
				Snapshot.RoomFEDSum = static_cast<float>(LerpField(LoT, Lo->FEDSum, HiT, Hi->FEDSum, T));
				Snapshot.RoomFEDRadSum = static_cast<float>(LerpField(LoT, Lo->FEDRadSum, HiT, Hi->FEDRadSum, T));

				// Monitor-layer temperature, same selection as the temperature crossing above: the layer
				// is chosen by the interface height at the failure time, and the chosen layer's
				// temperature must be present at BOTH bracketing samples. There is no live raw-sample
				// fallback offline, so a missing channel leaves the ambient default and zero risk.
				const bool bHasLayerHeight = Lo->bHasLayerHeight && Hi->bHasLayerHeight;
				bool bTemperatureAvailable = false;
				if (bHasLayerHeight)
				{
					const bool bMonitorInUpperLayer = MonitorHeightM >= LayerHeightAtFailure;
					bTemperatureAvailable = bMonitorInUpperLayer
						? (Lo->bHasUpperTemperature && Hi->bHasUpperTemperature)
						: (Lo->bHasLowerTemperature && Hi->bHasLowerTemperature);
					if (bTemperatureAvailable)
					{
						Snapshot.TemperatureC = static_cast<float>(bMonitorInUpperLayer
							? LerpField(LoT, Lo->UpperTemperatureC, HiT, Hi->UpperTemperatureC, T)
							: LerpField(LoT, Lo->LowerTemperatureC, HiT, Hi->LowerTemperatureC, T));
					}
				}

				// Risks: the same Compute*Risk functions ComputeInstantaneousTenability calls, under the
				// same enable + presence gates. A disabled or absent criterion contributes zero, so a
				// zero risk here means the same thing it means on a live frame.
				if (Settings.bUseVisibilityCriterion && Lo->bHasVisibility && Hi->bHasVisibility)
				{
					Snapshot.VisibilityRisk = ComputeVisibilityRisk(
						Snapshot.VisibilityM, Settings.EndpointVisibilityM, Settings.ReferenceVisibilityM);
				}
				if (Settings.bUseToxicFEDCriterion && Lo->bHasFEDSum && Hi->bHasFEDSum)
				{
					Snapshot.ToxicFEDRisk = ComputeFEDRisk(
						Snapshot.AccumulatedToxicFED, Settings.EndpointToxicFED);
				}
				if (Settings.bUseThermalFEDCriterion && Lo->bHasFEDRadSum && Hi->bHasFEDRadSum)
				{
					Snapshot.ThermalFEDRisk = ComputeFEDRisk(
						Snapshot.AccumulatedThermalFED, Settings.EndpointThermalFED);
				}
				if (Settings.bUseTemperatureCriterion && bTemperatureAvailable)
				{
					Snapshot.TemperatureRisk = ComputeTemperatureRisk(
						Snapshot.TemperatureC, Settings.EndpointTemperatureC);
				}
				if (Settings.bUseLayerHeightCriterion && bHasLayerHeight)
				{
					Snapshot.LayerHeightRisk = ComputeLayerHeightRisk(
						Snapshot.LayerHeightM, Settings.MonitorHeightM);
				}
			}

			// Valid because a failure was recorded, whether or not a curve backed every channel.
			Snapshot.bValid = true;
		}

		// Failure pose: sample the agent trajectory at the first-failure time when a sampler is provided.
		// A settings-only rebuild may pass an unset TFunction to skip pose capture (fetched lazily later).
		if (PoseSampler)
		{
			FVector Loc = FVector::ZeroVector;
			FRotator Rot = FRotator::ZeroRotator;
			if (PoseSampler(Timeline.FirstFailureTimeSeconds, Loc, Rot))
			{
				Timeline.FailureLocation = Loc;
				Timeline.FailureRotation = Rot;
			}
		}
	}

	void ApplyFailureSnapshot(
		FAgentEgressTenabilityFragment& Tenability,
		const FAgentTenabilityTimeline& Timeline)
	{
		const FTenabilityFailureSnapshot& Snapshot = Timeline.FailureSnapshot;
		if (!Snapshot.bValid)
		{
			return; // no recorded failure -> nothing to project (never fabricate a failure readout)
		}

		// Accumulated dose and the raw sampled values, AT the failure time.
		Tenability.AccumulatedToxicFED = Snapshot.AccumulatedToxicFED;
		Tenability.AccumulatedThermalFED = Snapshot.AccumulatedThermalFED;
		Tenability.CurrentVisibilityM = Snapshot.VisibilityM;
		Tenability.CurrentTemperatureC = Snapshot.TemperatureC;
		Tenability.CurrentLayerHeightM = Snapshot.LayerHeightM;
		Tenability.CurrentHeatReleaseKW = Snapshot.HeatReleaseKW;
		Tenability.CurrentFEDSum = Snapshot.RoomFEDSum;
		Tenability.CurrentFEDRadSum = Snapshot.RoomFEDRadSum;

		// Per-criterion risks at the failure time.
		Tenability.VisibilityRisk = Snapshot.VisibilityRisk;
		Tenability.ToxicFEDRisk = Snapshot.ToxicFEDRisk;
		Tenability.ThermalFEDRisk = Snapshot.ThermalFEDRisk;
		Tenability.TemperatureRisk = Snapshot.TemperatureRisk;
		Tenability.LayerHeightRisk = Snapshot.LayerHeightRisk;

		// Per-criterion failed flags from the precomputed mask. FirstFailureMask IS the set of criteria
		// failing at the failure instant: it collects every criterion whose crossing coincides with the
		// (minimum) first-failure time, and none can cross earlier. Assigned unconditionally, both true
		// and false: ComputeInstantaneousTenability has no else-branch clearing these, so a flag left
		// alone here would stay stuck from whatever the last live frame wrote.
		const uint8 Mask = Timeline.FirstFailureMask;
		Tenability.FailureMask = Mask;
		Tenability.bVisibilityFailed = (Mask & UE::Mobius::TenabilityFailureFlags::Visibility) != 0;
		Tenability.bToxicFEDFailed = (Mask & UE::Mobius::TenabilityFailureFlags::ToxicFED) != 0;
		Tenability.bThermalFEDFailed = (Mask & UE::Mobius::TenabilityFailureFlags::ThermalFED) != 0;
		Tenability.bTemperatureFailed = (Mask & UE::Mobius::TenabilityFailureFlags::Temperature) != 0;
		Tenability.bLayerHeightFailed = (Mask & UE::Mobius::TenabilityFailureFlags::LayerHeight) != 0;

		// Locked display state: full bar on the cause of failure. Deliberately NOT the max of the risks
		// above - a criterion sitting exactly at its endpoint does not always normalise to 1 (layer
		// height reads 0 at its own crossing, by that risk's definition), and "what stopped this agent"
		// is the claim the bar makes after failure.
		Tenability.DisplayRisk = 1.0f;
		Tenability.CurrentDominantCriterion = Timeline.FirstFailureCriterion;
		Tenability.Health = 0.0f;
	}
}
