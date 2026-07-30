// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

#include "BRisk/AgentTenabilityTimeline.h"
#include "BRisk/BRiskEgressSubsystem.h" // UBRiskEgressSubsystem::HashTenabilitySettings (static)
#include "BRiskDataImporter.h"          // FBRiskTenabilityRoomTable / FBRiskTenabilitySample (Layer 2 fixtures)
#include "CoreMinimal.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h" // FTenabilityAnalysisSettings
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "Misc/AutomationTest.h"

namespace
{
	using namespace UE::Mobius::Tenability;

	// Two rooms side by side, sharing the plane x=1000. Equal volume, so on the shared
	// boundary the smaller-volume tie resolves to the first room (index 0 == A) unless
	// preferred-room stickiness keeps the agent in its current room.
	//  A: x in [0,1000],  B: x in [1000,2000];  both y in [0,1000], z in [0,300].
	//
	// Neither carries a footprint polygon, so these are rooms known only as B-Risk's equivalent
	// rectangle — the case that must keep resolving EXACTLY as it did before footprint containment
	// existed. Every assertion in the legacy block below is therefore a regression gate.
	TArray<FRoomVolume> MakeRoomVolumes()
	{
		TArray<FRoomVolume> Volumes;
		Volumes.Add(MakeRoomVolume(                                                   // A (index 0)
			FBox(FVector(0.0, 0.0, 0.0), FVector(1000.0, 1000.0, 300.0)), {}));
		Volumes.Add(MakeRoomVolume(                                                   // B (index 1)
			FBox(FVector(1000.0, 0.0, 0.0), FVector(2000.0, 1000.0, 300.0)), {}));
		return Volumes;
	}

	TArray<int32> MakeRoomIds()
	{
		return TArray<int32>({1, 2}); // RoomId parallel to bounds: A->1, B->2.
	}

	// Synthetic linear cumulative FED curves, per plan:
	//   toxic:   FEDSum_A(t) = 0.001*t,  FEDSum_B(t) = 0.002*t
	//   thermal: half of toxic.
	// RoomIndex 0 == A, 1 == B.
	TFunction<void(int32, double, double&, double&)> MakeFEDSampler()
	{
		return [](int32 RoomIndex, double TimeSeconds, double& OutToxic, double& OutThermal)
		{
			const double Slope = (RoomIndex == 1) ? 0.002 : 0.001;
			OutToxic = Slope * TimeSeconds;
			OutThermal = 0.5 * OutToxic;
		};
	}

	FSimMovementSample MakeSample(int32 EntityID, double X)
	{
		FSimMovementSample S;
		S.EntityID = EntityID;
		S.Position = FVector(X, 500.0, 150.0); // y/z well inside both rooms.
		S.Rotation = FRotator::ZeroRotator;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentTenabilityTimelineCoreTest,
	"ProjectMobius.BRisk.Tenability.TimelineCore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAgentTenabilityTimelineCoreTest::RunTest(const FString&)
{
	const TArray<FRoomVolume> Volumes = MakeRoomVolumes();

	// --- ResolveRoomIndexAtLocation: the shared room-resolution rule ---------
	{
		// Inside A -> A.
		TestEqual(TEXT("inside A -> 0"),
			ResolveRoomIndexAtLocation(Volumes, FVector(500, 500, 150), INDEX_NONE), 0);
		// Inside B -> B.
		TestEqual(TEXT("inside B -> 1"),
			ResolveRoomIndexAtLocation(Volumes, FVector(1500, 500, 150), INDEX_NONE), 1);
		// Outside all rooms -> INDEX_NONE.
		TestEqual(TEXT("outside -> INDEX_NONE"),
			ResolveRoomIndexAtLocation(Volumes, FVector(5000, 500, 150), INDEX_NONE), INDEX_NONE);
		// Shared boundary x=1000 with NO preferred room: both boxes contain it and have
		// equal volume, so the first (index 0 == A) wins the smallest-volume tie. This is
		// exactly the extracted live rule (FindRoomStateAtLocation: strict `<` on volume,
		// iteration order breaks ties toward the earlier room).
		TestEqual(TEXT("boundary x=1000, no preferred -> A (tie to first)"),
			ResolveRoomIndexAtLocation(Volumes, FVector(1000, 500, 150), INDEX_NONE), 0);
		// Preferred-room stickiness: on the shared boundary the current room is kept.
		TestEqual(TEXT("boundary x=1000, preferred B -> stays B"),
			ResolveRoomIndexAtLocation(Volumes, FVector(1000, 500, 150), 1), 1);
		TestEqual(TEXT("boundary x=1000, preferred A -> stays A"),
			ResolveRoomIndexAtLocation(Volumes, FVector(1000, 500, 150), 0), 0);
		// Preferred index that no longer contains the location falls back to the search.
		TestEqual(TEXT("preferred stale (A) but inside B -> B"),
			ResolveRoomIndexAtLocation(Volumes, FVector(1500, 500, 150), 0), 1);
		// An out-of-range preferred index is ignored (treated as no preference).
		TestEqual(TEXT("preferred out-of-range -> normal search"),
			ResolveRoomIndexAtLocation(Volumes, FVector(500, 500, 150), 99), 0);
		// A rectangle-only room's tie-break metric must equal the FBox::GetVolume() it replaced,
		// exactly — this is what makes every assertion above a like-for-like regression gate.
		// Compared EXACTLY, not within a tolerance: the claim is that the multiplication is the same
		// sequence of operations, so any difference at all would mean the tie-break could diverge.
		TestTrue(TEXT("rectangle specificity == FBox::GetVolume() exactly"),
			Volumes[0].SpecificityVolumeCm3 == Volumes[0].Bounds.GetVolume());
	}

	// --- Footprint containment: the polygon fix -------------------------------
	// An L-shaped corridor and a small room sitting in the L's notch. This is the 12 RoomTest
	// shape in miniature: the corridor's BOUNDING BOX covers the whole 1000x1000 square while
	// the corridor itself occupies only the bottom and left arms, so before footprint containment
	// the notch resolved into the corridor and picked up the corridor's FED.
	//
	//   y=1000  +-----------+          Corridor ring (counter-clockwise), 200-wide arms:
	//           |C|  notch  |            (0,0) (1000,0) (1000,200) (200,200) (200,1000) (0,1000)
	//           |C|  room   |          Notch room: x in [200,1000], y in [200,1000].
	//   y=200   |C+---------|          Corridor plan area = 1000*200 + 200*800 = 360000
	//           |CCCCCCCCCCC|          Notch plan area                        = 640000
	//   y=0     +-----------+          -> the corridor is the SMALLER room; it would win the
	//          x=0        x=1000          tie-break anywhere both claimed a point.
	{
		const TArray<FVector2D> CorridorRing = {
			FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0), FVector2D(1000.0, 200.0),
			FVector2D(200.0, 200.0), FVector2D(200.0, 1000.0), FVector2D(0.0, 1000.0)
		};
		const FBox Slab(FVector(0.0, 0.0, 0.0), FVector(1000.0, 1000.0, 300.0));

		TArray<FRoomVolume> LShape;
		LShape.Add(MakeRoomVolume(Slab, CorridorRing));                              // corridor (0)
		LShape.Add(MakeRoomVolume(                                                   // notch room (1)
			FBox(FVector(200.0, 200.0, 0.0), FVector(1000.0, 1000.0, 300.0)), {}));

		// The corridor's bbox is the full square, so bbox-only resolution claimed everything.
		TestTrue(TEXT("corridor bbox still covers the notch (the bug's precondition)"),
			LShape[0].Bounds.IsInsideOrOn(FVector(600, 600, 150)));
		// And it is the smaller of the two by plan area, so it would also win the tie-break.
		TestTrue(TEXT("corridor is the smaller room by specificity"),
			LShape[0].SpecificityVolumeCm3 < LShape[1].SpecificityVolumeCm3);

		// THE FIX: a point in the notch resolves to the notch room, not the enclosing corridor.
		TestEqual(TEXT("notch -> notch room, not the corridor"),
			ResolveRoomIndexAtLocation(LShape, FVector(600, 600, 150), INDEX_NONE), 1);
		// Both corridor arms still resolve to the corridor.
		TestEqual(TEXT("bottom arm -> corridor"),
			ResolveRoomIndexAtLocation(LShape, FVector(600, 100, 150), INDEX_NONE), 0);
		TestEqual(TEXT("left arm -> corridor"),
			ResolveRoomIndexAtLocation(LShape, FVector(100, 600, 150), INDEX_NONE), 0);
		// Stickiness must not smuggle the old behaviour back in: an agent whose preferred room is
		// the corridor, standing in the notch, is NOT kept in the corridor — the polygon rejects
		// it and the search re-resolves. This is the case the live MASS path actually hits, since
		// an agent walks out of the corridor into the notch carrying its previous room index.
		TestEqual(TEXT("preferred corridor but standing in the notch -> notch room"),
			ResolveRoomIndexAtLocation(LShape, FVector(600, 600, 150), 0), 1);

		// Unmodelled space inside a footprint's bbox resolves to NO room, deliberately: B-Risk
		// modelled no zone there, so there is no reading to attribute. Same corridor with nothing
		// in the notch.
		TArray<FRoomVolume> CorridorOnly;
		CorridorOnly.Add(MakeRoomVolume(Slab, CorridorRing));
		TestEqual(TEXT("notch with no room modelled there -> INDEX_NONE (no fabricated dose)"),
			ResolveRoomIndexAtLocation(CorridorOnly, FVector(600, 600, 150), INDEX_NONE), INDEX_NONE);
		// Including for an agent that just walked out of the corridor.
		TestEqual(TEXT("preferred corridor, walked into unmodelled notch -> INDEX_NONE"),
			ResolveRoomIndexAtLocation(CorridorOnly, FVector(600, 600, 150), 0), INDEX_NONE);
		// The Z slab still gates: above the ceiling is outside even inside the ring.
		TestEqual(TEXT("above the ceiling over a corridor arm -> INDEX_NONE"),
			ResolveRoomIndexAtLocation(CorridorOnly, FVector(600, 100, 400), INDEX_NONE), INDEX_NONE);

		// A wall shared by two abutting polygon rooms belongs to exactly one of them — never both
		// (as FBox::IsInsideOrOn would) and never neither, so an agent in a doorway always lands
		// in a room. Two unit squares meeting at x=1000.
		const TArray<FVector2D> LeftSquare = {
			FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0), FVector2D(1000.0, 1000.0), FVector2D(0.0, 1000.0)
		};
		const TArray<FVector2D> RightSquare = {
			FVector2D(1000.0, 0.0), FVector2D(2000.0, 0.0), FVector2D(2000.0, 1000.0), FVector2D(1000.0, 1000.0)
		};
		TArray<FRoomVolume> Abutting;
		Abutting.Add(MakeRoomVolume(FBox(FVector(0.0, 0.0, 0.0), FVector(1000.0, 1000.0, 300.0)), LeftSquare));
		Abutting.Add(MakeRoomVolume(FBox(FVector(1000.0, 0.0, 0.0), FVector(2000.0, 1000.0, 300.0)), RightSquare));
		TestEqual(TEXT("point on a shared polygon wall resolves to exactly one room"),
			ResolveRoomIndexAtLocation(Abutting, FVector(1000, 500, 150), INDEX_NONE), 1);
	}

	// --- BRiskCoord::IsPointInRing is the predicate BOTH paths use -----------
	// Asserted directly so the smoke mask and agent->room attribution can never drift apart on
	// where a wall is: RasteriseFootprintMask calls this same function per texel.
	{
		const TArray<FVector2D> Square = {
			FVector2D(0.0, 0.0), FVector2D(100.0, 0.0), FVector2D(100.0, 100.0), FVector2D(0.0, 100.0)
		};
		TestTrue(TEXT("IsPointInRing: interior"),
			BRiskCoord::IsPointInRing(Square, FVector2D(50.0, 50.0)));
		TestFalse(TEXT("IsPointInRing: exterior"),
			BRiskCoord::IsPointInRing(Square, FVector2D(150.0, 50.0)));
		// Winding-independent: the ring may arrive either way round.
		TArray<FVector2D> Reversed = Square;
		Algo::Reverse(Reversed);
		TestTrue(TEXT("IsPointInRing: winding does not matter"),
			BRiskCoord::IsPointInRing(Reversed, FVector2D(50.0, 50.0)));
		// Degenerate input is not a ring — callers reading "no polygon" as "no constraint" must
		// check the vertex count themselves rather than rely on this returning true.
		TestFalse(TEXT("IsPointInRing: fewer than 3 vertices is not a ring"),
			BRiskCoord::IsPointInRing(TArray<FVector2D>({FVector2D(0.0, 0.0), FVector2D(100.0, 0.0)}),
				FVector2D(50.0, 0.0)));
	}

	// --- Build the interval list for agent 1 ---------------------------------
	// A for t in [0,50], B for t in [50,100], outside for t in (100,120],
	// re-enters A for t in [120,150]. Samples every 1 s. Agent 2 present t in [0,30] only.
	FAgentTimelineSetBuilder Builder(MakeRoomVolumes(), MakeRoomIds(), MakeFEDSampler());
	for (int32 T = 0; T <= 150; ++T)
	{
		TArray<FSimMovementSample> Samples;

		// Transitions land on the exact boundary times: the FIRST sample in a new region
		// is what closes the previous interval and opens the next, so B's first sample at
		// t=50 makes A's exit == 50 (interval {A,0,50}); the first outside sample at t=100
		// makes B's exit == 100; re-entry to A at t=120 opens {A,120,...}.
		double X1;
		if (T < 50)         { X1 = 500.0; }   // in A  (t in [0,49])
		else if (T < 100)   { X1 = 1500.0; }  // in B  (t in [50,99])
		else if (T < 120)   { X1 = 5000.0; }  // outside (t in [100,119])
		else                { X1 = 500.0; }   // re-enters A (t in [120,150])
		Samples.Add(MakeSample(1, X1));

		if (T <= 30)
		{
			Samples.Add(MakeSample(2, 500.0)); // agent 2 in A, then leaves the dataset.
		}

		Builder.AddTimestep(static_cast<float>(T), Samples);
	}
	FAgentTimelineSet Set = Builder.Finish();

	const FAgentTenabilityTimeline* TL1 = Set.Timelines.Find(1);
	TestNotNull(TEXT("agent 1 timeline exists"), TL1);
	if (!TL1)
	{
		return false;
	}

	// --- Interval list exact: [{A,0,50},{B,50,100},{A,120,150}] --------------
	TestEqual(TEXT("agent 1 has 3 intervals"), TL1->Intervals.Num(), 3);
	if (TL1->Intervals.Num() == 3)
	{
		const FAgentRoomOccupancyInterval& I0 = TL1->Intervals[0];
		const FAgentRoomOccupancyInterval& I1 = TL1->Intervals[1];
		const FAgentRoomOccupancyInterval& I2 = TL1->Intervals[2];

		TestEqual(TEXT("I0 room A"), I0.RoomIndex, 0);
		TestEqual(TEXT("I0 roomId 1"), I0.RoomId, 1);
		TestEqual(TEXT("I0 entry 0"), I0.EntryTimeSeconds, 0.0f, 1e-4f);
		TestEqual(TEXT("I0 exit 50"), I0.ExitTimeSeconds, 50.0f, 1e-4f);

		TestEqual(TEXT("I1 room B"), I1.RoomIndex, 1);
		TestEqual(TEXT("I1 roomId 2"), I1.RoomId, 2);
		TestEqual(TEXT("I1 entry 50"), I1.EntryTimeSeconds, 50.0f, 1e-4f);
		TestEqual(TEXT("I1 exit 100"), I1.ExitTimeSeconds, 100.0f, 1e-4f);

		TestEqual(TEXT("I2 room A"), I2.RoomIndex, 0);
		TestEqual(TEXT("I2 entry 120"), I2.EntryTimeSeconds, 120.0f, 1e-4f);
		TestEqual(TEXT("I2 exit 150"), I2.ExitTimeSeconds, 150.0f, 1e-4f);

		// Prefix sums (toxic): 0 / 0.05 / 0.15.
		//   I0: Prior 0,   Entry 0.001*0=0,     Exit 0.001*50=0.05  -> contributes 0.05.
		//   I1: Prior 0.05, Entry 0.002*50=0.10, Exit 0.002*100=0.20 -> contributes 0.10.
		//   I2: Prior 0.15.
		TestEqual(TEXT("I0 prior toxic 0"), I0.PriorToxicFED, 0.0f, 1e-4f);
		TestEqual(TEXT("I1 prior toxic 0.05"), I1.PriorToxicFED, 0.05f, 1e-4f);
		TestEqual(TEXT("I2 prior toxic 0.15"), I2.PriorToxicFED, 0.15f, 1e-4f);
		// Thermal is half throughout.
		TestEqual(TEXT("I1 prior thermal 0.025"), I1.PriorThermalFED, 0.025f, 1e-4f);
		TestEqual(TEXT("I2 prior thermal 0.075"), I2.PriorThermalFED, 0.075f, 1e-4f);
	}

	// --- DoseAt golden values ------------------------------------------------
	// SamplerLambda must outlive every FRoomFEDSampler (a TFunctionRef, which does NOT
	// own its callable) built from it, so it is a named local here, not a temporary.
	auto SamplerLambda = [](int32 RoomIndex, double TimeSeconds, double& OutToxic, double& OutThermal)
	{
		const double Slope = (RoomIndex == 1) ? 0.002 : 0.001;
		OutToxic = Slope * TimeSeconds;
		OutThermal = 0.5 * OutToxic;
	};

	auto DoseToxic = [&](float T)
	{
		float Toxic = -1.0f, Thermal = -1.0f;
		FRoomFEDSampler SamplerRef(SamplerLambda);
		TL1->DoseAt(T, SamplerRef, Toxic, Thermal);
		return Toxic;
	};

	TestEqual(TEXT("DoseAt(25) == 0.025"), DoseToxic(25.0f), 0.025f, 1e-4f);
	TestEqual(TEXT("DoseAt(50) == 0.05"), DoseToxic(50.0f), 0.05f, 1e-4f);
	TestEqual(TEXT("DoseAt(75) == 0.10"), DoseToxic(75.0f), 0.10f, 1e-4f);
	TestEqual(TEXT("DoseAt(110) == 0.15 (gap -> flat)"), DoseToxic(110.0f), 0.15f, 1e-4f);
	TestEqual(TEXT("DoseAt(135) == 0.165"), DoseToxic(135.0f), 0.165f, 1e-4f);
	TestEqual(TEXT("DoseAt(1e6) == 0.18 (past end -> final)"), DoseToxic(1e6f), 0.18f, 1e-4f);
	TestEqual(TEXT("DoseAt(-5) == 0"), DoseToxic(-5.0f), 0.0f, 1e-4f);

	// --- Navigation independence by construction -----------------------------
	// Query the same times in shuffled order; each time must yield a BITWISE-identical
	// value regardless of the order it was queried in. This is the test that was
	// impossible under the old forward-integrating state machine.
	{
		const TArray<float> ShuffledTimes({75.0f, 25.0f, 135.0f, 25.0f, 110.0f, 75.0f});
		TMap<float, float> FirstSeen;
		FRoomFEDSampler SamplerRef(SamplerLambda);
		for (float T : ShuffledTimes)
		{
			float Toxic = 0.0f, Thermal = 0.0f;
			TL1->DoseAt(T, SamplerRef, Toxic, Thermal);
			if (const float* Prior = FirstSeen.Find(T))
			{
				// Bitwise identity — exact equality, not tolerance.
				TestTrue(FString::Printf(TEXT("shuffled DoseAt(%.0f) bitwise-repeatable"), T),
					*Prior == Toxic);
			}
			else
			{
				FirstSeen.Add(T, Toxic);
			}
		}
	}

	// --- Absent-agent close: agent 2 present t in [0,30] only ----------------
	{
		const FAgentTenabilityTimeline* TL2 = Set.Timelines.Find(2);
		TestNotNull(TEXT("agent 2 timeline exists"), TL2);
		if (TL2)
		{
			TestEqual(TEXT("agent 2 has 1 interval"), TL2->Intervals.Num(), 1);
			if (TL2->Intervals.Num() == 1)
			{
				TestEqual(TEXT("agent 2 room A"), TL2->Intervals[0].RoomIndex, 0);
				TestEqual(TEXT("agent 2 entry 0"), TL2->Intervals[0].EntryTimeSeconds, 0.0f, 1e-4f);
				// Left the dataset after t=30 -> interval closes at its last-seen time 30.
				TestEqual(TEXT("agent 2 closes at 30"), TL2->Intervals[0].ExitTimeSeconds, 30.0f, 1e-4f);
			}
		}
	}

	return true;
}

// ===================================================================================================
// Task 2: invalidation / key logic. Pure key + settings-hash semantics — the threaded build path is
// proven end-to-end in Task 6. Everything here is decidable without a UWorld or a live subsystem, so
// it locks the decision table the health processor's per-frame poll depends on.
// ===================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentTenabilityTimelineInvalidationTest,
	"ProjectMobius.BRisk.Tenability.TimelineInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAgentTenabilityTimelineInvalidationTest::RunTest(const FString&)
{
	// --- FAgentTimelineKey equality / LayerOneEquals semantics -----------------------------------
	// Layer 1 (intervals) depends on (agent gen, scenario gen) only; Layer 2 (failures) adds the
	// settings hash. So a settings-only change must leave LayerOneEquals TRUE but == FALSE.
	{
		const FAgentTimelineKey Base{ /*Agent*/1, /*Scenario*/1, /*Settings*/100 };

		// Identical -> equal both ways.
		{
			const FAgentTimelineKey Same{ 1, 1, 100 };
			TestTrue(TEXT("identical == true"), Base == Same);
			TestFalse(TEXT("identical != false"), Base != Same);
			TestTrue(TEXT("identical LayerOneEquals true"), Base.LayerOneEquals(Same));
		}
		// Settings-only change: Layer 1 identical, Layer 2 differs.
		{
			const FAgentTimelineKey SettingsChanged{ 1, 1, 999 };
			TestFalse(TEXT("settings change -> == false"), Base == SettingsChanged);
			TestTrue(TEXT("settings change -> != true"), Base != SettingsChanged);
			TestTrue(TEXT("settings change -> LayerOneEquals STILL true"),
				Base.LayerOneEquals(SettingsChanged));
		}
		// Agent-generation change: both differ.
		{
			const FAgentTimelineKey AgentChanged{ 2, 1, 100 };
			TestFalse(TEXT("agent gen change -> == false"), Base == AgentChanged);
			TestFalse(TEXT("agent gen change -> LayerOneEquals false"),
				Base.LayerOneEquals(AgentChanged));
		}
		// Scenario-generation change: both differ.
		{
			const FAgentTimelineKey ScenarioChanged{ 1, 2, 100 };
			TestFalse(TEXT("scenario gen change -> == false"), Base == ScenarioChanged);
			TestFalse(TEXT("scenario gen change -> LayerOneEquals false"),
				Base.LayerOneEquals(ScenarioChanged));
		}
		// Sentinel (all zero) is distinct from any real key (real agent gens start at 1).
		{
			const FAgentTimelineKey Sentinel;
			TestFalse(TEXT("sentinel != real key"), Sentinel == Base);
			TestTrue(TEXT("two sentinels equal"), Sentinel == FAgentTimelineKey());
		}
	}

	// --- Key-mismatch decision table (mirrors the invalidation matrix rows) -----------------------
	// The health processor holds a BuiltKey and derives a Current key each frame; it must rebuild
	// whenever they differ. "Current" here stands in for MakeCurrentTimelineKey()'s output.
	{
		const FAgentTimelineKey Built{ 1, 1, 100 };

		// Agent file changed -> agent gen bumped -> rebuild (Layer 1 too: LayerOneEquals false).
		{
			const FAgentTimelineKey Current{ 2, 1, 100 };
			TestFalse(TEXT("agent-file change -> not current -> rebuild"), Built == Current);
			TestFalse(TEXT("agent-file change -> Layer 1 stale"), Built.LayerOneEquals(Current));
		}
		// B-Risk file changed -> scenario gen bumped -> rebuild (Layer 1 stale).
		{
			const FAgentTimelineKey Current{ 1, 2, 100 };
			TestFalse(TEXT("brisk-file change -> not current -> rebuild"), Built == Current);
			TestFalse(TEXT("brisk-file change -> Layer 1 stale"), Built.LayerOneEquals(Current));
		}
		// Settings changed -> Layer-2-only rebuild allowed; either way the FULL key is stale so the
		// processor treats it as not-current and rebuilds (a full rebuild is an acceptable superset of
		// the Layer-2-only optimisation for this milestone).
		{
			const FAgentTimelineKey Current{ 1, 1, 200 };
			TestFalse(TEXT("settings change -> not current -> rebuild"), Built == Current);
			TestTrue(TEXT("settings change -> Layer 1 still valid (Layer-2-only rebuild permitted)"),
				Built.LayerOneEquals(Current));
		}
		// Identical -> current -> no rebuild.
		{
			const FAgentTimelineKey Current{ 1, 1, 100 };
			TestTrue(TEXT("identical -> current -> no rebuild"), Built == Current);
		}
	}

	// --- HashTenabilitySettings: field-by-field, deterministic, sensitive -------------------------
	// Determinism (same input -> same hash) and sensitivity (each meaningful field changes the hash)
	// are what make SettingsHash a sound Layer-2 invalidation signal. Bitwise-equal settings must
	// hash equal so a settings rebuild is requested ONLY on a genuine change.
	{
		const FTenabilityAnalysisSettings A;                 // defaults
		FTenabilityAnalysisSettings B;                       // identical copy
		const uint32 HashA = UBRiskEgressSubsystem::HashTenabilitySettings(A);
		TestEqual(TEXT("hash deterministic (identical settings)"),
			UBRiskEgressSubsystem::HashTenabilitySettings(B), HashA);

		// Each float endpoint change must move the hash.
		auto ExpectHashChange = [&](const TCHAR* Label, TFunctionRef<void(FTenabilityAnalysisSettings&)> Mutate)
		{
			FTenabilityAnalysisSettings M;
			Mutate(M);
			TestNotEqual(Label, UBRiskEgressSubsystem::HashTenabilitySettings(M), HashA);
		};
		ExpectHashChange(TEXT("MonitorHeightM change -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.MonitorHeightM += 0.5f; });
		ExpectHashChange(TEXT("EndpointVisibilityM change -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.EndpointVisibilityM += 1.0f; });
		ExpectHashChange(TEXT("ReferenceVisibilityM change -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.ReferenceVisibilityM += 1.0f; });
		ExpectHashChange(TEXT("EndpointToxicFED change -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.EndpointToxicFED += 0.1f; });
		ExpectHashChange(TEXT("EndpointThermalFED change -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.EndpointThermalFED += 0.1f; });
		ExpectHashChange(TEXT("EndpointTemperatureC change -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.EndpointTemperatureC += 1.0f; });

		// Each criterion-enable toggle must move the hash (packed-flags fold).
		ExpectHashChange(TEXT("bUseVisibilityCriterion toggle -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.bUseVisibilityCriterion = !S.bUseVisibilityCriterion; });
		ExpectHashChange(TEXT("bUseToxicFEDCriterion toggle -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.bUseToxicFEDCriterion = !S.bUseToxicFEDCriterion; });
		ExpectHashChange(TEXT("bUseThermalFEDCriterion toggle -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.bUseThermalFEDCriterion = !S.bUseThermalFEDCriterion; });
		ExpectHashChange(TEXT("bUseTemperatureCriterion toggle -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.bUseTemperatureCriterion = !S.bUseTemperatureCriterion; });
		ExpectHashChange(TEXT("bUseLayerHeightCriterion toggle -> hash change"),
			[](FTenabilityAnalysisSettings& S){ S.bUseLayerHeightCriterion = !S.bUseLayerHeightCriterion; });
	}

	return true;
}

// ===================================================================================================
// Task 3: Layer-2 failure precompute (crossings + pose). Golden values are the REAL basemodel curve
// numbers embedded in BRiskTenabilityTest.cpp::MakeOutputXml (room 1). The predicates + priority mirror
// ComputeInstantaneousTenability (formerly UpdateAgentTenability) in AgentEgressTenabilityFragments.h
// exactly.
//
// This suite deliberately enables ONLY the visibility + toxic-FED criteria (thermal / temperature /
// layer-height OFF). Thermal FED would otherwise fail at t=150 (FEDRadSum reaches 1.0 there), which
// would mask the two crossings the plan pins (vis 46.586 s, toxic 531.43 s) and change the vis-off
// first-failure to thermal. Isolating vis + toxic keeps the golden arithmetic legible and matches the
// plan's Task-3 golden list, which cites only those two crossings.
// ===================================================================================================

namespace
{
	// One <time> row of the basemodel golden curve as an FBRiskTenabilitySample. All channels present
	// (the fixture's output1.xml carries every tag), matching MakeOutputXml's bHas* = true rows.
	FBRiskTenabilitySample MakeGoldenSample(
		const double T, const double HRR, const double Layer, const double UpT, const double LowT,
		const double FEDSum, const double Vis, const double FEDRad)
	{
		FBRiskTenabilitySample S;
		S.SampleTimeSeconds = T;
		S.HeatReleaseKW = HRR;
		S.LayerHeightM = Layer;
		S.UpperTemperatureC = UpT;
		S.LowerTemperatureC = LowT;
		S.FEDSum = FEDSum;
		S.VisibilityM = Vis;
		S.FEDRadSum = FEDRad;
		S.bHasHeatRelease = true;
		S.bHasLayerHeight = true;
		S.bHasUpperTemperature = true;
		S.bHasLowerTemperature = true;
		S.bHasVisibility = true;
		S.bHasFEDSum = true;
		S.bHasFEDRadSum = true;
		return S;
	}

	// Room 1 tenability table: the exact rows from BRiskTenabilityTest.cpp::MakeOutputXml.
	FBRiskTenabilityRoomTable MakeGoldenRoomTable()
	{
		FBRiskTenabilityRoomTable Table;
		Table.RoomId = 1;
		Table.Samples.Add(MakeGoldenSample(0,   0.0,    2.600, 24.00, 24.00, 0.000, 20.000, 0.000));
		Table.Samples.Add(MakeGoldenSample(30,  169.2,  2.399, 53.27, 24.25, 0.000, 20.000, 0.000));
		Table.Samples.Add(MakeGoldenSample(60,  676.8,  1.905, 95.95, 26.20, 0.001, 1.917,  0.010));
		Table.Samples.Add(MakeGoldenSample(90,  1391.2, 1.470, 155.19, 31.91, 0.008, 1.183, 0.155));
		Table.Samples.Add(MakeGoldenSample(120, 1391.2, 1.328, 180.99, 0.0,   0.021, 0.933, 0.551));
		Table.Samples.Add(MakeGoldenSample(150, 1391.2, 1.325, 187.71, 0.0,   0.038, 0.833, 1.000));
		Table.Samples.Add(MakeGoldenSample(510, 1391.2, 1.346, 218.23, 0.0,   0.285, 0.808, 1.000));
		Table.Samples.Add(MakeGoldenSample(540, 1391.2, 1.348, 219.60, 0.0,   0.306, 0.811, 1.000));
		Table.Samples.Add(MakeGoldenSample(600, 1391.2, 1.350, 222.13, 0.0,   0.347, 0.816, 1.000));
		return Table;
	}

	// Linear-interp a raw FEDSum from the golden table (for the late-entrant entry-FED baseline).
	double GoldenFEDSumAt(const FBRiskTenabilityRoomTable& Table, const double T)
	{
		const FBRiskTenabilitySample Sample = UBRiskEgressSubsystem::SampleTenabilityTableAtTime(Table, T);
		return Sample.FEDSum;
	}

	// Vis + toxic-FED only (see suite header for why thermal/temp/layer are off).
	FTenabilityAnalysisSettings MakeVisToxicSettings()
	{
		FTenabilityAnalysisSettings Settings; // defaults: vis 10, toxic 0.3, thermal 1.0
		Settings.bUseVisibilityCriterion = true;
		Settings.bUseToxicFEDCriterion = true;
		Settings.bUseThermalFEDCriterion = false;
		Settings.bUseTemperatureCriterion = false;
		Settings.bUseLayerHeightCriterion = false;
		return Settings;
	}

	// A timeline for a single agent occupying room 0 (RoomIndex 0 -> table 0) over [Entry, Exit],
	// with the given entry FED baseline. Prior = 0 (no earlier interval).
	FAgentTenabilityTimeline MakeSingleRoomTimeline(
		const float Entry, const float Exit, const double EntryToxicFED, const double EntryThermalFED)
	{
		FAgentTenabilityTimeline Timeline;
		FAgentRoomOccupancyInterval Interval;
		Interval.RoomIndex = 0;
		Interval.RoomId = 1;
		Interval.EntryTimeSeconds = Entry;
		Interval.ExitTimeSeconds = Exit;
		Interval.EntryToxicFED = static_cast<float>(EntryToxicFED);
		Interval.EntryThermalFED = static_cast<float>(EntryThermalFED);
		Interval.ExitToxicFED = Interval.EntryToxicFED;   // unused by ComputeFailureData
		Interval.ExitThermalFED = Interval.EntryThermalFED;
		Interval.PriorToxicFED = 0.0f;
		Interval.PriorThermalFED = 0.0f;
		Timeline.Intervals.Add(Interval);
		return Timeline;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentTenabilityFailurePrecomputeTest,
	"ProjectMobius.BRisk.Tenability.FailurePrecompute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAgentTenabilityFailurePrecomputeTest::RunTest(const FString&)
{
	const FBRiskTenabilityRoomTable RoomTable = MakeGoldenRoomTable();
	const TArray<FBRiskTenabilityRoomTable> Tables({ RoomTable });
	const TArray<int32> RoomIndexToTableIndex({ 0 }); // RoomIndex 0 -> table 0
	const FTenabilityAnalysisSettings Settings = MakeVisToxicSettings();

	// The LIVE dose path, verbatim: UBRiskEgressSubsystem::SampleTenabilityDoseAtRoomIndex is
	// SampleTenabilityTableAtTime plus a field copy. Used to cross-check the snapshot's dose against the
	// interpolator the running game uses, so an offline/live divergence in clamping or interpolation
	// fails here rather than showing up as a wrong number on screen.
	auto LiveRoomSampler = [&RoomTable](
		const int32 /*RoomIndex*/, const double TimeSeconds, double& OutToxic, double& OutThermal)
	{
		const FBRiskTenabilitySample Live = UBRiskEgressSubsystem::SampleTenabilityTableAtTime(RoomTable, TimeSeconds);
		OutToxic = Live.FEDSum;
		OutThermal = Live.FEDRadSum;
	};

	// Every field ApplyFailureSnapshot claims to own, compared in ONE place: a field added to the
	// snapshot but forgotten in the projection has to fail something, and this is what fails.
	const auto TestProjectionsEqual = [this](
		const TCHAR* What,
		const FAgentEgressTenabilityFragment& A,
		const FAgentEgressTenabilityFragment& B)
	{
		const auto Same = [this, What](const TCHAR* Field, const float X, const float Y)
		{
			TestEqual(FString::Printf(TEXT("%s: %s"), What, Field), X, Y, 0.0f); // EXACT: same source data
		};
		Same(TEXT("AccumulatedToxicFED"), A.AccumulatedToxicFED, B.AccumulatedToxicFED);
		Same(TEXT("AccumulatedThermalFED"), A.AccumulatedThermalFED, B.AccumulatedThermalFED);
		Same(TEXT("CurrentVisibilityM"), A.CurrentVisibilityM, B.CurrentVisibilityM);
		Same(TEXT("CurrentTemperatureC"), A.CurrentTemperatureC, B.CurrentTemperatureC);
		Same(TEXT("CurrentLayerHeightM"), A.CurrentLayerHeightM, B.CurrentLayerHeightM);
		Same(TEXT("CurrentHeatReleaseKW"), A.CurrentHeatReleaseKW, B.CurrentHeatReleaseKW);
		Same(TEXT("CurrentFEDSum"), A.CurrentFEDSum, B.CurrentFEDSum);
		Same(TEXT("CurrentFEDRadSum"), A.CurrentFEDRadSum, B.CurrentFEDRadSum);
		Same(TEXT("VisibilityRisk"), A.VisibilityRisk, B.VisibilityRisk);
		Same(TEXT("ToxicFEDRisk"), A.ToxicFEDRisk, B.ToxicFEDRisk);
		Same(TEXT("ThermalFEDRisk"), A.ThermalFEDRisk, B.ThermalFEDRisk);
		Same(TEXT("TemperatureRisk"), A.TemperatureRisk, B.TemperatureRisk);
		Same(TEXT("LayerHeightRisk"), A.LayerHeightRisk, B.LayerHeightRisk);
		Same(TEXT("DisplayRisk"), A.DisplayRisk, B.DisplayRisk);
		Same(TEXT("Health"), A.Health, B.Health);
		TestEqual(FString::Printf(TEXT("%s: FailureMask"), What),
			static_cast<int32>(A.FailureMask), static_cast<int32>(B.FailureMask));
		TestEqual(FString::Printf(TEXT("%s: CurrentDominantCriterion"), What),
			static_cast<int32>(A.CurrentDominantCriterion), static_cast<int32>(B.CurrentDominantCriterion));
		TestTrue(FString::Printf(TEXT("%s: bVisibilityFailed"), What), A.bVisibilityFailed == B.bVisibilityFailed);
		TestTrue(FString::Printf(TEXT("%s: bToxicFEDFailed"), What), A.bToxicFEDFailed == B.bToxicFEDFailed);
		TestTrue(FString::Printf(TEXT("%s: bThermalFEDFailed"), What), A.bThermalFEDFailed == B.bThermalFEDFailed);
		TestTrue(FString::Printf(TEXT("%s: bTemperatureFailed"), What), A.bTemperatureFailed == B.bTemperatureFailed);
		TestTrue(FString::Printf(TEXT("%s: bLayerHeightFailed"), What), A.bLayerHeightFailed == B.bLayerHeightFailed);
	};

	// A pose sampler that returns a fixed marker pose, to prove pose capture runs at the crossing time.
	const FVector MarkerLoc(11.0, 22.0, 33.0);
	const FRotator MarkerRot(5.0, 6.0, 7.0);
	float CapturedTime = -1.0f;
	TFunction<bool(float, FVector&, FRotator&)> PoseSampler =
		[&](float TimeSeconds, FVector& OutLoc, FRotator& OutRot) -> bool
	{
		CapturedTime = TimeSeconds;
		OutLoc = MarkerLoc;
		OutRot = MarkerRot;
		return true;
	};

	// --- Agent occupies room 1 for t in [0, 600], entered at t=0 (entry FED 0) ---------------------
	{
		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(0.0f, 600.0f, /*toxic*/0.0, /*thermal*/0.0);
		ComputeFailureData(Timeline, Tables, RoomIndexToTableIndex, Settings, PoseSampler);

		// Visibility crossing between t=30 (20.0 m) and t=60 (1.917 m), endpoint 10:
		//   t* = 30 + 30*(20 - 10)/(20 - 1.917) = 30 + 300/18.083 = 46.5884 s.
		TestEqual(TEXT("visibility crossing ~46.586 s"),
			Timeline.VisibilityFailureTimeSeconds, 46.586f, 1e-2f);

		// Toxic FED crossing between t=510 (0.285) and t=540 (0.306), endpoint 0.3, entry FED 0 -> dose = curve:
		//   t* = 510 + 30*(0.3 - 0.285)/(0.306 - 0.285) = 510 + 30*0.015/0.021 = 531.4286 s.
		TestEqual(TEXT("toxic FED crossing ~531.43 s"),
			Timeline.ToxicFEDFailureTimeSeconds, 531.43f, 1e-2f);

		// First failure = min(vis 46.588, toxic 531.43) = Visibility, priority respected.
		TestEqual(TEXT("first failure time == visibility crossing"),
			Timeline.FirstFailureTimeSeconds, 46.586f, 1e-2f);
		TestEqual(TEXT("first failure criterion == Visibility"),
			static_cast<int32>(Timeline.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::Visibility));
		// Mask at the first-failure time = visibility only (toxic has not crossed by 46.6 s).
		TestEqual(TEXT("first failure mask == Visibility flag"),
			static_cast<int32>(Timeline.FirstFailureMask),
			static_cast<int32>(UE::Mobius::TenabilityFailureFlags::Visibility));

		// Pose captured at the crossing time (not the interval bounds).
		TestEqual(TEXT("pose sampled at first-failure time"), CapturedTime, 46.586f, 1e-2f);
		TestEqual(TEXT("failure location captured"), Timeline.FailureLocation, MarkerLoc);
		TestTrue(TEXT("failure rotation captured"), Timeline.FailureRotation.Equals(MarkerRot, 1e-3f));

		// --- The AT-FAILURE SNAPSHOT: every criterion value evaluated at the crossing ----------------
		// Golden arithmetic at t* = 46.586 s, alpha = (46.586 - 30)/30 = 0.55287 across the t=30..60 pair:
		//   visibility  20.000 -> 1.917 : the crossing time is DEFINED by visibility == 10, so 10.000
		//   layer       2.399  -> 1.905 : 2.399 + 0.55287*(-0.494)     = 2.1259 m
		//   lower temp  24.25  -> 26.20 : 24.25 + 0.55287*(1.95)       = 25.328 C  (monitor 2.0 m is
		//                                 BELOW the 2.1259 m interface -> LOWER layer selected)
		//   HRR         169.2  -> 676.8 : 169.2 + 0.55287*(507.6)      = 449.85 kW
		//   FEDSum      0.000  -> 0.001 : 0.55287*0.001                = 5.529e-4  (entry FED 0 -> dose)
		const FTenabilityFailureSnapshot& Snapshot = Timeline.FailureSnapshot;
		TestTrue(TEXT("snapshot valid when a failure was recorded"), Snapshot.bValid);
		// Not merely "10-ish": the visibility crossing is the solution of visibility == endpoint, so a
		// snapshot taken at the right instant reads the endpoint back. Off-by-one-sample or
		// evaluated-at-the-interval-bound would read 20.0 or 1.917 instead.
		TestEqual(TEXT("snapshot visibility == the endpoint it crossed (10 m)"),
			Snapshot.VisibilityM, Settings.EndpointVisibilityM, 1e-2f);
		TestEqual(TEXT("snapshot layer height at the crossing ~2.126 m"), Snapshot.LayerHeightM, 2.1259f, 1e-3f);
		TestEqual(TEXT("snapshot monitor-layer temperature is the LOWER layer ~25.33 C"),
			Snapshot.TemperatureC, 25.328f, 1e-2f);
		TestEqual(TEXT("snapshot heat release at the crossing ~449.85 kW"),
			Snapshot.HeatReleaseKW, 449.85f, 1e-1f);
		TestEqual(TEXT("snapshot room FEDSum at the crossing ~5.529e-4"),
			Snapshot.RoomFEDSum, 5.529e-4f, 1e-6f);

		// Dose: the snapshot must equal what the LIVE closed-form query returns at the failure time,
		// through the interpolator the running game uses. This is the offline/live agreement check.
		{
			float LiveToxic = 0.0f;
			float LiveThermal = 0.0f;
			FRoomFEDSampler LiveSamplerRef(LiveRoomSampler);
			Timeline.DoseAt(Timeline.FirstFailureTimeSeconds, LiveSamplerRef, LiveToxic, LiveThermal);
			TestEqual(TEXT("snapshot toxic dose == live DoseAt(first-failure time)"),
				Snapshot.AccumulatedToxicFED, LiveToxic, 1e-6f);
			TestEqual(TEXT("snapshot thermal dose == live DoseAt(first-failure time)"),
				Snapshot.AccumulatedThermalFED, LiveThermal, 1e-6f);
		}

		// Risks come from the same Compute*Risk functions the live frame calls, so they are derivable
		// from the snapshot's own values - which is the property that lets the bar read identically.
		TestEqual(TEXT("snapshot visibility risk saturates at the endpoint (1.0)"),
			Snapshot.VisibilityRisk, 1.0f, 1e-3f);
		TestEqual(TEXT("snapshot toxic risk == ComputeFEDRisk(snapshot dose)"),
			Snapshot.ToxicFEDRisk, ComputeFEDRisk(Snapshot.AccumulatedToxicFED, Settings.EndpointToxicFED), 1e-6f);
		// Thermal / temperature / layer-height criteria are OFF in this suite: zero risk, but the raw
		// values above are still captured, exactly as ComputeInstantaneousTenability writes them
		// regardless of which criteria are enabled.
		TestEqual(TEXT("disabled thermal criterion contributes no snapshot risk"), Snapshot.ThermalFEDRisk, 0.0f);
		TestEqual(TEXT("disabled temperature criterion contributes no snapshot risk"), Snapshot.TemperatureRisk, 0.0f);
		TestEqual(TEXT("disabled layer-height criterion contributes no snapshot risk"), Snapshot.LayerHeightRisk, 0.0f);

		// --- NAVIGATION INDEPENDENCE: scrubbed straight past the failure == played through it --------
		// The whole point of precomputing the snapshot. "Played through" is modelled by a fragment that
		// already carries live values from earlier frames (any pre- or post-failure frame leaves SOME
		// values behind); "scrubbed" is a fragment that has never been written. If the projection is
		// total, the two are bit-identical afterwards - and if it is not, whatever it fails to overwrite
		// is exactly the field that would differ on screen between the two navigations. The earlier
		// skip-and-hold guard fails this by construction: it wrote nothing at all, so the two fragments
		// kept their different histories.
		//
		// SCOPE: this pins the PROJECTION, not the processor wiring. The health processor needs a MASS
		// execution context and two subsystems, so a call site that stopped calling ApplyFailureSnapshot
		// would still read green here. Mobius.InGame.TenabilityScrubReplay covers the wiring, in a live
		// world, by asserting the at-failure dose after a direct scrub past the failure.
		{
			FAgentEgressTenabilityFragment PlayedThrough;
			// Deliberately wrong in every field the projection owns, and wrong in a DIRECTION a reader
			// would believe (a clear room, a nearly-lethal dose) rather than obviously-garbage values.
			PlayedThrough.CurrentVisibilityM = 19.5f;
			PlayedThrough.CurrentTemperatureC = 210.0f;
			PlayedThrough.CurrentLayerHeightM = 1.35f;
			PlayedThrough.CurrentHeatReleaseKW = 1391.2f;
			PlayedThrough.CurrentFEDSum = 0.347f;
			PlayedThrough.CurrentFEDRadSum = 1.0f;
			PlayedThrough.AccumulatedToxicFED = 0.99f;
			PlayedThrough.AccumulatedThermalFED = 0.42f;
			PlayedThrough.VisibilityRisk = 0.7f;
			PlayedThrough.ToxicFEDRisk = 0.7f;
			PlayedThrough.ThermalFEDRisk = 0.7f;
			PlayedThrough.TemperatureRisk = 0.7f;
			PlayedThrough.LayerHeightRisk = 0.7f;
			PlayedThrough.DisplayRisk = 0.7f;
			PlayedThrough.Health = 0.3f;
			PlayedThrough.CurrentDominantCriterion = ETenabilityCriterion::ThermalFED;
			PlayedThrough.bThermalFEDFailed = true;
			PlayedThrough.bLayerHeightFailed = true;
			PlayedThrough.FailureMask = UE::Mobius::TenabilityFailureFlags::ThermalFED
				| UE::Mobius::TenabilityFailureFlags::LayerHeight;

			FAgentEgressTenabilityFragment Scrubbed; // straight from t=0 to a post-failure time
			ApplyFailureSnapshot(PlayedThrough, Timeline);
			ApplyFailureSnapshot(Scrubbed, Timeline);

			TestProjectionsEqual(TEXT("scrubbed past failure == played through"), Scrubbed, PlayedThrough);

			// And the shared answer is the AT-FAILURE state, not either history: pin it against the
			// snapshot so "equal" cannot be satisfied by both being wrong in the same way.
			TestEqual(TEXT("projected visibility is the at-failure value"),
				Scrubbed.CurrentVisibilityM, Snapshot.VisibilityM);
			TestEqual(TEXT("projected toxic dose is the at-failure dose"),
				Scrubbed.AccumulatedToxicFED, Snapshot.AccumulatedToxicFED);
			TestEqual(TEXT("projected temperature is the at-failure value"),
				Scrubbed.CurrentTemperatureC, Snapshot.TemperatureC);
			TestEqual(TEXT("projected layer height is the at-failure value"),
				Scrubbed.CurrentLayerHeightM, Snapshot.LayerHeightM);
			TestEqual(TEXT("projected visibility risk is the at-failure risk"),
				Scrubbed.VisibilityRisk, Snapshot.VisibilityRisk);
			// Sanity: the poisoned "played through" values really were different, so the equality above
			// is a claim about the overwrite and not a tautology.
			TestTrue(TEXT("the seeded pre-projection values differed from the snapshot"),
				!FMath::IsNearlyEqual(0.99f, Snapshot.AccumulatedToxicFED, 1e-3f)
				&& !FMath::IsNearlyEqual(19.5f, Snapshot.VisibilityM, 1e-3f));
			// Failed flags + mask come from the precomputed first-failure mask (visibility only here), so
			// the ThermalFED/LayerHeight flags seeded above must be GONE, not merely equal on both sides.
			TestTrue(TEXT("projected visibility-failed flag set from the mask"), Scrubbed.bVisibilityFailed);
			TestFalse(TEXT("seeded thermal-failed flag cleared by the projection"), PlayedThrough.bThermalFEDFailed);
			TestFalse(TEXT("seeded layer-height-failed flag cleared by the projection"), PlayedThrough.bLayerHeightFailed);
			TestEqual(TEXT("projected mask == precomputed first-failure mask"),
				static_cast<int32>(PlayedThrough.FailureMask), static_cast<int32>(Timeline.FirstFailureMask));
			// Locked display state (the pre-existing contract, now owned by the projection).
			TestEqual(TEXT("projected display risk locked to 1.0"), PlayedThrough.DisplayRisk, 1.0f);
			TestEqual(TEXT("projected Health locked to 0.0"), PlayedThrough.Health, 0.0f);
			TestEqual(TEXT("projected dominant criterion == first-failure criterion"),
				static_cast<int32>(PlayedThrough.CurrentDominantCriterion),
				static_cast<int32>(Timeline.FirstFailureCriterion));
		}
	}

	// --- Late entrant: enters at t=520, entry FED = FEDSum(520) = 0.292 ------------------------------
	{
		const double EntryFED = GoldenFEDSumAt(RoomTable, 520.0);
		TestEqual(TEXT("late-entrant entry FED ~0.292"), static_cast<float>(EntryFED), 0.292f, 1e-3f);

		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(520.0f, 600.0f, EntryFED, /*thermal*/0.0);
		ComputeFailureData(Timeline, Tables, RoomIndexToTableIndex, Settings, /*PoseSampler*/{});

		// Dose = FEDSum(t) - 0.292. At t=540: 0.306-0.292 = 0.014; at t=600: 0.347-0.292 = 0.055.
		// Neither reaches 0.3, so NO toxic failure by 600.
		TestEqual(TEXT("late entrant: no toxic failure by 600"),
			Timeline.ToxicFEDFailureTimeSeconds, -1.0f, 1e-4f);

		// Visibility at t=520 is ~0.809 m (<= 10) -> fails instantly at the entry time.
		TestEqual(TEXT("late entrant: visibility fails at entry 520"),
			Timeline.VisibilityFailureTimeSeconds, 520.0f, 1e-2f);
		TestEqual(TEXT("late entrant: first failure == Visibility"),
			static_cast<int32>(Timeline.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::Visibility));
		TestEqual(TEXT("late entrant: first failure time 520"),
			Timeline.FirstFailureTimeSeconds, 520.0f, 1e-2f);
	}

	// --- Settings toggle: visibility criterion disabled -> first failure becomes ToxicFED ------------
	{
		FTenabilityAnalysisSettings VisOff = Settings;
		VisOff.bUseVisibilityCriterion = false;

		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(0.0f, 600.0f, /*toxic*/0.0, /*thermal*/0.0);
		ComputeFailureData(Timeline, Tables, RoomIndexToTableIndex, VisOff, /*PoseSampler*/{});

		TestEqual(TEXT("vis-off: visibility never fails"),
			Timeline.VisibilityFailureTimeSeconds, -1.0f, 1e-4f);
		TestEqual(TEXT("vis-off: toxic crossing still 531.43"),
			Timeline.ToxicFEDFailureTimeSeconds, 531.43f, 1e-2f);
		TestEqual(TEXT("vis-off: first failure == ToxicFED"),
			static_cast<int32>(Timeline.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::ToxicFED));
		TestEqual(TEXT("vis-off: first failure time == toxic crossing"),
			Timeline.FirstFailureTimeSeconds, 531.43f, 1e-2f);

		// Snapshot on a DOSE failure. Same argument as the visibility case: the toxic crossing is the
		// solution of dose == endpoint, so the at-failure dose reads the endpoint (0.3) back, and its
		// risk saturates. A snapshot taken anywhere else on the curve cannot produce this number.
		const FTenabilityFailureSnapshot& DoseSnapshot = Timeline.FailureSnapshot;
		TestEqual(TEXT("vis-off: snapshot toxic dose == the endpoint it crossed (0.3)"),
			DoseSnapshot.AccumulatedToxicFED, VisOff.EndpointToxicFED, 1e-4f);
		TestEqual(TEXT("vis-off: snapshot toxic risk saturates at 1.0"), DoseSnapshot.ToxicFEDRisk, 1.0f, 1e-4f);
		// Visibility is captured as a raw value but contributes no risk, because the criterion is
		// disabled - the same split the live frame produces. Across the t=510..540 pair at t* = 531.4286
		// (alpha 0.71429): 0.808 + 0.71429*(0.811 - 0.808) = 0.8101 m.
		TestEqual(TEXT("vis-off: snapshot still captures the raw visibility ~0.810 m"),
			DoseSnapshot.VisibilityM, 0.8101f, 1e-3f);
		TestEqual(TEXT("vis-off: disabled visibility criterion contributes no snapshot risk"),
			DoseSnapshot.VisibilityRisk, 0.0f);
	}

	// --- No failure -> no snapshot, and the projection is inert ---------------------------------------
	// A settings rebuild that removes the failure must not leave the previous run's snapshot standing,
	// and the projection must refuse to invent a failure readout when asked out of sequence.
	{
		FTenabilityAnalysisSettings NothingOn = Settings;
		NothingOn.bUseVisibilityCriterion = false;
		NothingOn.bUseToxicFEDCriterion = false;

		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(0.0f, 600.0f, /*toxic*/0.0, /*thermal*/0.0);
		ComputeFailureData(Timeline, Tables, RoomIndexToTableIndex, Settings, /*PoseSampler*/{});
		TestTrue(TEXT("snapshot filled on the first (failing) pass"), Timeline.FailureSnapshot.bValid);

		// Rebuild the SAME timeline with every criterion off: no failure, so no snapshot.
		ComputeFailureData(Timeline, Tables, RoomIndexToTableIndex, NothingOn, /*PoseSampler*/{});
		TestEqual(TEXT("no-failure rebuild: first failure cleared"), Timeline.FirstFailureTimeSeconds, -1.0f, 1e-4f);
		TestFalse(TEXT("no-failure rebuild: stale snapshot cleared"), Timeline.FailureSnapshot.bValid);
		TestEqual(TEXT("no-failure rebuild: snapshot dose reset"),
			Timeline.FailureSnapshot.AccumulatedToxicFED, 0.0f);

		// Out-of-sequence projection against a no-failure timeline: a no-op, not a fabricated failure.
		FAgentEgressTenabilityFragment Untouched;
		ApplyFailureSnapshot(Untouched, Timeline);
		TestEqual(TEXT("no-failure projection leaves DisplayRisk alone"), Untouched.DisplayRisk, 0.0f);
		TestEqual(TEXT("no-failure projection leaves Health alone"), Untouched.Health, 1.0f);
		TestEqual(TEXT("no-failure projection claims no failed criterion"),
			static_cast<int32>(Untouched.FailureMask), 0);
	}

	// --- Empty table / missing room mapping: no failure, no crash (never fabricate) ------------------
	{
		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(0.0f, 600.0f, 0.0, 0.0);
		const TArray<FBRiskTenabilityRoomTable> Empty;
		ComputeFailureData(Timeline, Empty, RoomIndexToTableIndex, Settings, /*PoseSampler*/{});
		TestEqual(TEXT("no table -> no visibility failure"), Timeline.VisibilityFailureTimeSeconds, -1.0f, 1e-4f);
		TestEqual(TEXT("no table -> no toxic failure"), Timeline.ToxicFEDFailureTimeSeconds, -1.0f, 1e-4f);
		TestEqual(TEXT("no table -> first failure none"),
			static_cast<int32>(Timeline.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::None));

		// RoomIndex out of the mapping's range -> criterion skipped, no crash.
		FAgentTenabilityTimeline BadIndex = MakeSingleRoomTimeline(0.0f, 600.0f, 0.0, 0.0);
		BadIndex.Intervals[0].RoomIndex = 7; // no entry in RoomIndexToTableIndex
		ComputeFailureData(BadIndex, Tables, RoomIndexToTableIndex, Settings, /*PoseSampler*/{});
		TestEqual(TEXT("bad room index -> first failure none"),
			static_cast<int32>(BadIndex.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::None));
	}

	// --- Missing channel (bHasVisibility false) -> visibility criterion skipped for the span ---------
	{
		FBRiskTenabilityRoomTable NoVisTable = MakeGoldenRoomTable();
		for (FBRiskTenabilitySample& S : NoVisTable.Samples)
		{
			S.bHasVisibility = false; // channel absent -> visibility must be skipped, never fabricated
		}
		const TArray<FBRiskTenabilityRoomTable> NoVisTables({ NoVisTable });

		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(0.0f, 600.0f, 0.0, 0.0);
		ComputeFailureData(Timeline, NoVisTables, RoomIndexToTableIndex, Settings, /*PoseSampler*/{});
		TestEqual(TEXT("no-vis channel -> visibility skipped"),
			Timeline.VisibilityFailureTimeSeconds, -1.0f, 1e-4f);
		// Toxic still fails (its channel is present), so the first failure falls through to ToxicFED.
		TestEqual(TEXT("no-vis channel -> first failure == ToxicFED"),
			static_cast<int32>(Timeline.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::ToxicFED));
	}

	// --- Crossing exactly at a sample time: temperature endpoint tuned to a sample value -------------
	// Upper temp at t=90 is 155.19 C (t<90 monitor is in the upper layer: layer height ~1.47 m >= 2 m? no).
	// Instead pin an endpoint that the LAYER-HEIGHT curve hits exactly at a sample to exercise the
	// "already at threshold at a sample" branch of SolveCrossing without layer-selection ambiguity.
	{
		FTenabilityAnalysisSettings LayerSettings;
		LayerSettings.bUseVisibilityCriterion = false;
		LayerSettings.bUseToxicFEDCriterion = false;
		LayerSettings.bUseThermalFEDCriterion = false;
		LayerSettings.bUseTemperatureCriterion = false;
		LayerSettings.bUseLayerHeightCriterion = true;
		LayerSettings.MonitorHeightM = 1.905f; // == LayerHeightM at the t=60 sample exactly

		FAgentTenabilityTimeline Timeline = MakeSingleRoomTimeline(0.0f, 600.0f, 0.0, 0.0);
		ComputeFailureData(Timeline, Tables, RoomIndexToTableIndex, LayerSettings, /*PoseSampler*/{});
		// LayerHeight <= MonitorHeight: layer starts 2.6 (>1.905), falls to 1.905 at t=60 (crossing at 60),
		// stays below afterwards. First layer-height crossing is t=60 (sample-aligned).
		TestEqual(TEXT("layer-height crossing at sample t=60"),
			Timeline.LayerHeightFailureTimeSeconds, 60.0f, 1e-2f);
		TestEqual(TEXT("layer-off-others: first failure == LayerHeight"),
			static_cast<int32>(Timeline.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::LayerHeight));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentTenabilityDoseOnlyTest,
	"ProjectMobius.BRisk.Tenability.DoseOutsideRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * An agent that walked out of smoke keeps the dose it accrued.
 *
 * Dose is a property of the AGENT, not of the room it is standing in, so leaving a modelled room
 * must not discard it. The previous behaviour zeroed every risk in the no-room branch, which
 * under-reported a dose the timeline had already computed and made the bar jump back up on re-entry.
 *
 * Also pins the display-gate semantics: zero risk WITH data (measured and clear) has to be
 * distinguishable from zero risk WITHOUT data (nothing measured here), because every other field
 * reads the same for both. That gate is FAgentEgressTenabilityFragment::bHasTenabilityData, and the
 * final blocks assert all four of its paths through UE::Mobius::Tenability::ShouldDisplayTenability
 * and ResetTenabilityToNoData - the two pure functions the owning MASS processor routes through.
 */
bool FAgentTenabilityDoseOnlyTest::RunTest(const FString&)
{
	using namespace UE::Mobius::Tenability;

	FTenabilityAnalysisSettings Settings; // endpoints: toxic 0.3, thermal 1.0

	// --- Dose survives leaving the room ---------------------------------------
	{
		FAgentEgressTenabilityFragment Health;
		// Half the toxic endpoint, no thermal dose.
		ComputeDoseOnlyTenability(Health, Settings, 0.15f, 0.0f);

		TestEqual(TEXT("accumulated toxic dose is carried, not discarded"),
			Health.AccumulatedToxicFED, 0.15f);
		// ComputeFEDRisk = dose / endpoint = 0.15 / 0.3.
		TestEqual(TEXT("toxic risk is dose/endpoint"), Health.ToxicFEDRisk, 0.5f);
		TestEqual(TEXT("DisplayRisk is the surviving dose risk, NOT zero"), Health.DisplayRisk, 0.5f);
		TestEqual(TEXT("dominant criterion names the dose that survived"),
			static_cast<int32>(Health.CurrentDominantCriterion),
			static_cast<int32>(ETenabilityCriterion::ToxicFED));
		TestEqual(TEXT("Health mirrors DisplayRisk"), Health.Health, 0.5f);

		// Instantaneous criteria describe a room the agent is no longer in: they must report no risk
		// rather than hold the last room's values, which would assert something untrue about where
		// the agent is now standing.
		TestEqual(TEXT("visibility risk is not carried out of the room"), Health.VisibilityRisk, 0.0f);
		TestEqual(TEXT("temperature risk is not carried out of the room"), Health.TemperatureRisk, 0.0f);
		TestEqual(TEXT("layer-height risk is not carried out of the room"), Health.LayerHeightRisk, 0.0f);
		TestFalse(TEXT("no instantaneous failure is claimed without a room"), Health.bVisibilityFailed);
		// A zero bar beside a non-empty mask reads as a bug, so the mask carries only what is real.
		TestEqual(TEXT("failure mask carries no instantaneous criteria"),
			static_cast<int32>(Health.FailureMask & UE::Mobius::TenabilityFailureFlags::Visibility), 0);
	}

	// --- Stale instantaneous values are cleared, not left standing -----------
	{
		FAgentEgressTenabilityFragment Health;
		// Simulate the fragment still holding the smoke-filled room it just left.
		Health.CurrentVisibilityM = 1.5f;
		Health.CurrentTemperatureC = 180.0f;
		Health.VisibilityRisk = 0.9f;
		Health.TemperatureRisk = 0.8f;
		Health.bVisibilityFailed = true;
		Health.FailureMask = UE::Mobius::TenabilityFailureFlags::Visibility;

		ComputeDoseOnlyTenability(Health, Settings, 0.0f, 0.0f);

		TestEqual(TEXT("stale visibility is reset to the clear-air default"), Health.CurrentVisibilityM, 20.0f);
		TestEqual(TEXT("stale temperature is reset to ambient"), Health.CurrentTemperatureC, 24.0f);
		TestEqual(TEXT("stale visibility risk is cleared"), Health.VisibilityRisk, 0.0f);
		TestFalse(TEXT("stale visibility failure is cleared"), Health.bVisibilityFailed);
		TestEqual(TEXT("stale failure mask is cleared"), static_cast<int32>(Health.FailureMask), 0);
		// No dose and no room -> genuinely nothing to show.
		TestEqual(TEXT("no dose, no room -> zero risk"), Health.DisplayRisk, 0.0f);
		TestEqual(TEXT("no dose, no room -> no dominant criterion"),
			static_cast<int32>(Health.CurrentDominantCriterion),
			static_cast<int32>(ETenabilityCriterion::None));
	}

	// --- Dose failure is still detected outside a room -----------------------
	{
		FAgentEgressTenabilityFragment Health;
		ComputeDoseOnlyTenability(Health, Settings, 0.30f, 0.0f); // exactly at the endpoint

		TestTrue(TEXT("toxic FED failure is flagged at the endpoint, outside a room"),
			Health.bToxicFEDFailed);
		TestEqual(TEXT("risk saturates at 1 when the endpoint is reached"), Health.DisplayRisk, 1.0f);
		TestEqual(TEXT("failure mask carries the toxic FED bit"),
			static_cast<int32>(Health.FailureMask & UE::Mobius::TenabilityFailureFlags::ToxicFED),
			static_cast<int32>(UE::Mobius::TenabilityFailureFlags::ToxicFED));
	}

	// --- Thermal dominates when it is the larger risk ------------------------
	{
		FAgentEgressTenabilityFragment Health;
		// toxic 0.03/0.3 = 0.1 risk; thermal 0.5/1.0 = 0.5 risk.
		ComputeDoseOnlyTenability(Health, Settings, 0.03f, 0.5f);

		TestEqual(TEXT("DisplayRisk is the MAX of the two dose risks, never the sum"),
			Health.DisplayRisk, 0.5f);
		TestEqual(TEXT("dominant criterion is the larger dose risk"),
			static_cast<int32>(Health.CurrentDominantCriterion),
			static_cast<int32>(ETenabilityCriterion::ThermalFED));
	}

	// --- Disabled criteria contribute nothing -------------------------------
	{
		FTenabilityAnalysisSettings Off;
		Off.bUseToxicFEDCriterion = false;
		Off.bUseThermalFEDCriterion = false;

		FAgentEgressTenabilityFragment Health;
		ComputeDoseOnlyTenability(Health, Off, 0.9f, 0.9f);

		TestEqual(TEXT("disabled toxic criterion reports no risk"), Health.ToxicFEDRisk, 0.0f);
		TestEqual(TEXT("disabled thermal criterion reports no risk"), Health.ThermalFEDRisk, 0.0f);
		TestFalse(TEXT("disabled criterion never claims failure"), Health.bToxicFEDFailed);
		TestEqual(TEXT("no enabled criteria -> no risk to display"), Health.DisplayRisk, 0.0f);
	}

	// --- Navigation independence: pure in dose, monotone, and no latch -------
	// The display must be a function of sim time alone (invariant 1). DoseAt is non-decreasing in
	// time by construction (Prior + max(curve - EntryFED, 0)), so the dose risks are already
	// "worst so far" WITHOUT a running maximum. This asserts the property that makes that safe:
	// the function keeps no state, so replaying the same dose gives the same answer, and a dose
	// that went DOWN (only reachable by scrubbing backwards) reports the lower value rather than
	// sticking at the high-water mark.
	{
		FAgentEgressTenabilityFragment Forward;
		ComputeDoseOnlyTenability(Forward, Settings, 0.06f, 0.0f);
		ComputeDoseOnlyTenability(Forward, Settings, 0.24f, 0.0f); // played forward to t

		FAgentEgressTenabilityFragment Jumped;
		ComputeDoseOnlyTenability(Jumped, Settings, 0.24f, 0.0f);  // jumped straight to t

		TestEqual(TEXT("same dose reads the same however playback reached it"),
			Forward.DisplayRisk, Jumped.DisplayRisk);

		FAgentEgressTenabilityFragment Rewound;
		ComputeDoseOnlyTenability(Rewound, Settings, 0.24f, 0.0f);
		ComputeDoseOnlyTenability(Rewound, Settings, 0.06f, 0.0f); // scrubbed back before the dose
		TestEqual(TEXT("scrubbing back reports the earlier dose, not a latched maximum"),
			Rewound.DisplayRisk, 0.2f);
	}

	// --- The display gate: bHasTenabilityData ---------------------------------
	// The bar encodes  value = ShownCriterion + Clamp(DisplayRisk, 0, 0.999)  and has no spare value
	// meaning "no data": criterion None with risk 0 is EXACTLY what a measured-and-clear agent
	// produces. This flag is therefore the only thing separating "measured here, and clear" from
	// "nothing was measured here". If it regresses, an agent standing in space B-Risk never simulated
	// draws a confident CLEAR bar - a false safe reading, and a scientific-integrity failure rather
	// than a cosmetic one. The four blocks below pin the four paths the field's docs promise.
	//
	// The decision lives in ShouldDisplayTenability precisely so it can be asserted: the processor
	// that owns the flag needs a MASS execution context and two subsystems, and both of its call
	// sites route through that function.
	//
	// SCOPE: what is pinned here is the DECISION, not the wiring. These blocks call the helpers
	// directly, so a call site passing the wrong argument - or removed outright - would still read
	// green. Covering that needs the processor running in a live world.

	// --- Path 1: in a modelled room this frame -> measured, so show it even at zero risk ------
	{
		TestTrue(TEXT("in-room sample with no dose has data (measured AND clear)"),
			ShouldDisplayTenability(
				/*bHasRoomSampleThisFrame*/ true, 0.0f, 0.0f, /*bFailedByNow*/ false));

		// The case this flag exists to separate from the one above: identical in every other field.
		TestFalse(TEXT("no room, no dose, not failed -> NO data (bar hides, never reads clear)"),
			ShouldDisplayTenability(
				/*bHasRoomSampleThisFrame*/ false, 0.0f, 0.0f, /*bFailedByNow*/ false));
	}

	// --- Path 2: outside every room, but carrying dose accrued in rooms it has left -----------
	// Dose belongs to the AGENT, not to the room it happens to be standing in, so an agent that
	// walked out of smoke was measured and must keep showing what it accrued.
	{
		TestTrue(TEXT("outside every room, carried toxic dose is still data"),
			ShouldDisplayTenability(false, 0.15f, 0.0f, false));
		TestTrue(TEXT("outside every room, carried thermal dose is still data"),
			ShouldDisplayTenability(false, 0.0f, 0.15f, false));

		// Both sides of the `> 0.0f` dose test. Exactly zero is the no-data case (an agent that has
		// never been anywhere modelled reads exactly this); the smallest positive dose is data.
		TestFalse(TEXT("dose of exactly zero is not data"),
			ShouldDisplayTenability(false, 0.0f, 0.0f, false));
		TestTrue(TEXT("smallest positive toxic dose is data"),
			ShouldDisplayTenability(false, UE_SMALL_NUMBER, 0.0f, false));
		TestTrue(TEXT("smallest positive thermal dose is data"),
			ShouldDisplayTenability(false, 0.0f, UE_SMALL_NUMBER, false));
		// Defensive: DoseAt cannot return a negative dose, but the gate must not invert if it ever does.
		TestFalse(TEXT("negative dose is not a measurement"),
			ShouldDisplayTenability(false, -0.5f, -0.5f, false));
	}

	// --- Path 3: failed by now -> ALWAYS data, wherever the agent is standing -----------------
	// The bar is locked to the cause of failure, which is what a reviewer needs at the end of a
	// scenario. Hiding it because the failure happened outside a modelled room would be worse than
	// the ambiguity this flag removes.
	{
		TestTrue(TEXT("failed, outside every room, with no dose, still has data"),
			ShouldDisplayTenability(
				/*bHasRoomSampleThisFrame*/ false, 0.0f, 0.0f, /*bFailedByNow*/ true));
		TestTrue(TEXT("failed inside a room has data"),
			ShouldDisplayTenability(true, 0.4f, 0.0f, true));
	}

	// --- Path 4: the stale-timeline rebuild window -> no data for EVERY entity ----------------
	// While the built timeline set does not match the current (agent file, B-Risk file, settings)
	// triple, nothing computed from the mismatched triple may be displayed (scientific-integrity
	// invariant 2) and there is no partial or interpolated fallback.
	{
		FAgentEgressTenabilityFragment Health;

		// Poison every field the reset owns with a value computed against the PREVIOUS triple.
		Health.bHasTenabilityData = true;
		Health.DisplayRisk = 0.8f;
		Health.VisibilityRisk = 0.8f;
		Health.ToxicFEDRisk = 0.7f;
		Health.ThermalFEDRisk = 0.6f;
		Health.TemperatureRisk = 0.5f;
		Health.LayerHeightRisk = 0.4f;
		Health.CurrentDominantCriterion = ETenabilityCriterion::Visibility;
		Health.Health = 0.2f;
		Health.bVisibilityFailed = true;
		Health.bToxicFEDFailed = true;
		Health.bThermalFEDFailed = true;
		Health.bTemperatureFailed = true;
		Health.bLayerHeightFailed = true;
		Health.FailureMask = static_cast<uint8>(
			UE::Mobius::TenabilityFailureFlags::Visibility | UE::Mobius::TenabilityFailureFlags::ToxicFED);
		Health.bTenabilityFailed = true;
		Health.bIsDead = true;
		Health.FirstFailureTimeSeconds = 90.0f;
		Health.FirstFailureCriterion = ETenabilityCriterion::Visibility;
		Health.DeathTimeSeconds = 90.0f;
		Health.DeathLocation = FVector(100.0, 200.0, 300.0);
		Health.DeathRotation = FRotator(0.0, 45.0, 0.0);
		Health.TimelineIntervalCount = 3;

		// Fields the reset deliberately does NOT own - it is a subset, not a pristine wipe.
		Health.AccumulatedToxicFED = 0.2f;
		Health.AccumulatedThermalFED = 0.1f;
		Health.CurrentVisibilityM = 1.5f;
		Health.CurrentTemperatureC = 180.0f;
		Health.ToxicFEDFailureTimeSeconds = 120.0f;
		Health.CombinedHazardDose = 0.3f;
		Health.InstantaneousHazard = 0.8f;

		ResetTenabilityToNoData(Health);

		TestFalse(TEXT("rebuild window: the display gate is cleared, not left stale true"),
			Health.bHasTenabilityData);
		TestEqual(TEXT("rebuild window: display risk cleared"), Health.DisplayRisk, 0.0f);
		TestEqual(TEXT("rebuild window: visibility risk cleared"), Health.VisibilityRisk, 0.0f);
		TestEqual(TEXT("rebuild window: toxic risk cleared"), Health.ToxicFEDRisk, 0.0f);
		TestEqual(TEXT("rebuild window: thermal risk cleared"), Health.ThermalFEDRisk, 0.0f);
		TestEqual(TEXT("rebuild window: temperature risk cleared"), Health.TemperatureRisk, 0.0f);
		TestEqual(TEXT("rebuild window: layer-height risk cleared"), Health.LayerHeightRisk, 0.0f);
		TestEqual(TEXT("rebuild window: no dominant criterion"),
			static_cast<int32>(Health.CurrentDominantCriterion),
			static_cast<int32>(ETenabilityCriterion::None));
		TestEqual(TEXT("rebuild window: Health mirrors zero risk"), Health.Health, 1.0f);
		TestFalse(TEXT("rebuild window: no visibility failure claimed"), Health.bVisibilityFailed);
		TestFalse(TEXT("rebuild window: no toxic failure claimed"), Health.bToxicFEDFailed);
		TestFalse(TEXT("rebuild window: no thermal failure claimed"), Health.bThermalFEDFailed);
		TestFalse(TEXT("rebuild window: no temperature failure claimed"), Health.bTemperatureFailed);
		TestFalse(TEXT("rebuild window: no layer-height failure claimed"), Health.bLayerHeightFailed);
		TestEqual(TEXT("rebuild window: failure mask cleared"), static_cast<int32>(Health.FailureMask), 0);
		TestFalse(TEXT("rebuild window: not reported as failed"), Health.bTenabilityFailed);
		TestFalse(TEXT("rebuild window: not reported as dead"), Health.bIsDead);
		TestEqual(TEXT("rebuild window: first failure time cleared"),
			Health.FirstFailureTimeSeconds, -1.0f);
		TestEqual(TEXT("rebuild window: first failure criterion cleared"),
			static_cast<int32>(Health.FirstFailureCriterion),
			static_cast<int32>(ETenabilityCriterion::None));
		// The movement processor's failure-pose freeze keys off DeathTimeSeconds ALONE, so a stale
		// death pose surviving the rebuild window would snap a live agent frozen.
		TestEqual(TEXT("rebuild window: death time cleared so no agent is frozen mid-rebuild"),
			Health.DeathTimeSeconds, -1.0f);
		TestTrue(TEXT("rebuild window: death location cleared"),
			Health.DeathLocation == FVector::ZeroVector);
		TestTrue(TEXT("rebuild window: death rotation cleared"),
			Health.DeathRotation == FRotator::ZeroRotator);
		TestEqual(TEXT("rebuild window: interval-count diagnostic cleared"),
			Health.TimelineIntervalCount, 0);

		// The reset is a SUBSET on purpose. Dose and the per-criterion crossing times are re-derived
		// wholesale from the rebuilt timeline on the first good frame, and the Current* readouts are
		// debug-only and already gated for display by bHasTenabilityData, which IS cleared above.
		// Asserting they survive is what stops the extraction quietly widening into a full wipe.
		TestEqual(TEXT("rebuild window: toxic dose is NOT part of the reset"),
			Health.AccumulatedToxicFED, 0.2f);
		TestEqual(TEXT("rebuild window: thermal dose is NOT part of the reset"),
			Health.AccumulatedThermalFED, 0.1f);
		TestEqual(TEXT("rebuild window: per-criterion crossing times are NOT part of the reset"),
			Health.ToxicFEDFailureTimeSeconds, 120.0f);
		TestEqual(TEXT("rebuild window: Current* debug readouts are NOT part of the reset"),
			Health.CurrentVisibilityM, 1.5f);
		TestEqual(TEXT("rebuild window: legacy hazard fields are NOT part of the reset"),
			Health.InstantaneousHazard, 0.8f);
	}

	// --- Ownership: setting the gate is the PROCESSOR's job, not this function's --------------
	// ComputeDoseOnlyTenability must leave the flag exactly as it found it. Deciding it in here would
	// leave the in-room path (which never calls this) unaccounted for, and reading it before the dose
	// call would hide the bar for one frame on the way out of every room.
	{
		FAgentEgressTenabilityFragment Held;
		Held.bHasTenabilityData = true;
		ComputeDoseOnlyTenability(Held, Settings, 0.15f, 0.0f);
		TestTrue(TEXT("dose-only leaves an already-true gate alone"), Held.bHasTenabilityData);

		FAgentEgressTenabilityFragment Untouched;
		ComputeDoseOnlyTenability(Untouched, Settings, 0.15f, 0.0f);
		TestFalse(TEXT("dose-only does not raise the gate itself, even with a surviving dose"),
			Untouched.bHasTenabilityData);
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
