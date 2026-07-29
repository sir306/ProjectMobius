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

#endif // !UE_BUILD_SHIPPING
