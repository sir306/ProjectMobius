// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/AgentTenabilityTimeline.h"

#include "Algo/BinarySearch.h"
#include "BRiskDataImporter.h"                                  // FBRiskTenabilityRoomTable / FBRiskTenabilitySample (Layer 2)
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h" // FSimMovementSample (full type)

namespace UE::Mobius::Tenability
{
	int32 ResolveRoomIndexAtLocation(
		const TConstArrayView<FBox> RoomWorldBounds,
		const FVector& WorldLocation,
		const int32 PreferredRoomIndex)
	{
		// Rule extracted verbatim from UBRiskEgressSubsystem::FindRoomStateAtLocation
		// (scientific-integrity invariant 4: offline and live resolution are one function).
		//
		// 1. Preferred-room stickiness: if the preferred room is valid and still
		//    contains the location, keep it.
		if (RoomWorldBounds.IsValidIndex(PreferredRoomIndex)
			&& RoomWorldBounds[PreferredRoomIndex].IsInsideOrOn(WorldLocation))
		{
			return PreferredRoomIndex;
		}

		// 2. When rooms overlap (e.g. a corridor modelled inside a larger B-Risk zone),
		//    the smallest enclosing volume is the most specific spatial match for the agent.
		int32 BestIndex = INDEX_NONE;
		double BestVolume = TNumericLimits<double>::Max();

		for (int32 RoomIndex = 0; RoomIndex < RoomWorldBounds.Num(); ++RoomIndex)
		{
			if (RoomIndex == PreferredRoomIndex)
			{
				continue;
			}

			const FBox& Bounds = RoomWorldBounds[RoomIndex];
			if (!Bounds.IsInsideOrOn(WorldLocation))
			{
				continue;
			}

			const double Volume = Bounds.GetVolume();
			if (Volume < BestVolume)
			{
				BestVolume = Volume;
				BestIndex = RoomIndex;
			}
		}

		// 3. Outside every room.
		return BestIndex;
	}

	void FAgentTenabilityTimeline::DoseAt(const float TimeSeconds, const FRoomFEDSampler& Sampler,
		float& OutToxicFED, float& OutThermalFED) const
	{
		OutToxicFED = 0.0f;
		OutThermalFED = 0.0f;
		if (Intervals.Num() == 0 || TimeSeconds < Intervals[0].EntryTimeSeconds)
		{
			return;
		}
		// Last interval whose entry time is <= TimeSeconds.
		int32 Index = Algo::UpperBoundBy(Intervals, TimeSeconds,
			&FAgentRoomOccupancyInterval::EntryTimeSeconds) - 1;
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
		TArray<FBox> InRoomWorldBounds,
		TArray<int32> InRoomIds,
		TFunction<void(int32, double, double&, double&)> InFEDSampler)
		: RoomWorldBounds(MoveTemp(InRoomWorldBounds))
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
			const int32 Room = ResolveRoomIndexAtLocation(RoomWorldBounds, Sample.Position, PreferredRoomIndex);

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

		// Earliest crossing time per criterion slot (-1 = never). Filled by walking every interval;
		// intervals are ascending and non-overlapping, so the running minimum is the global first.
		double SlotTime[static_cast<int32>(ECriterionSlot::Count)];
		for (double& T : SlotTime)
		{
			T = -1.0;
		}

		const double MonitorHeightM = static_cast<double>(Settings.MonitorHeightM);
		const double EndpointVisibilityM = static_cast<double>(Settings.EndpointVisibilityM);
		const double EndpointToxicFED = static_cast<double>(Settings.EndpointToxicFED);
		const double EndpointThermalFED = static_cast<double>(Settings.EndpointThermalFED);
		const double EndpointTemperatureC = static_cast<double>(Settings.EndpointTemperatureC);

		// Record a crossing for a slot, keeping the earliest.
		auto RecordCrossing = [&SlotTime](const ECriterionSlot Slot, const double Time)
		{
			const int32 Idx = static_cast<int32>(Slot);
			if (SlotTime[Idx] < 0.0 || Time < SlotTime[Idx])
			{
				SlotTime[Idx] = Time;
			}
		};

		for (const FAgentRoomOccupancyInterval& Interval : Timeline.Intervals)
		{
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
}
