// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

#include "BRisk/AgentTenabilityTimeline.h"
#include "BRiskDataImporter.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	FString MakeTenabilityTestDir()
	{
		const FString TestDir = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Automation"),
			TEXT("BRiskTenability"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*TestDir, true);
		return TestDir;
	}

	bool WriteText(const FString& Path, const FString& Contents)
	{
		return FFileHelper::SaveStringToFile(Contents, *Path);
	}

	/** Minimal single-room .smv referencing a zone CSV (companions are siblings). */
	FString MakeSmv()
	{
		return TEXT("ROOM   1\n")
			TEXT(" 2.4000E+001 5.5000E+000 2.6000E+000\n")
			TEXT(" 0.0000E+000 0.0000E+000 0.0000E+000\n")
			TEXT("LABEL\n 0 0 0\nroom\n")
			TEXT("ZONE\n")
			TEXT("basemodel_testBox_zone.csv\n");
	}

	FString MakeZoneCsv()
	{
		return TEXT("s,C,C,m,Pa,1 / m,1 / m,kW,\n")
			TEXT("Time,ULT_1,LLT_1,HGT_1,PRS_1,ULOD_1,LLOD_1,HRR_1,\n")
			TEXT("0,24,24,2.6,0,0.1,0.1,0,\n")
			TEXT("600,222,200,1.35,0,0.1,0.1,1391,\n");
	}

	/** One <time> block for output1.xml. */
	FString MakeTimeBlock(
		double T, double HRR, double Layer, double UpT, double LowT,
		double FEDSum, double Vis, double FEDRad)
	{
		return FString::Printf(
			TEXT("      <time value=\"%g\" units=\"sec\">\n")
			TEXT("        <HeatRelease value=\"%g\" units=\"kW\" />\n")
			TEXT("        <layerheight value=\"%g\" units=\"m\" />\n")
			TEXT("        <uppertemp value=\"%g\" units=\"C\" />\n")
			TEXT("        <lowertemp value=\"%g\" units=\"C\" />\n")
			TEXT("        <FEDSum value=\"%g\" units=\"-\" />\n")
			TEXT("        <Visibility value=\"%g\" units=\"m\" />\n")
			TEXT("        <FEDRadSum value=\"%g\" units=\"-\" />\n")
			TEXT("      </time>\n"),
			T, HRR, Layer, UpT, LowT, FEDSum, Vis, FEDRad);
	}

	/** output1.xml with the real basemodel golden values at key sample times. */
	FString MakeOutputXml()
	{
		FString Xml =
			TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
			TEXT("<output>\n  <run id=\"input1.xml\">\n    <room id=\"1\">\n");
		Xml += MakeTimeBlock(0,    0.0,    2.600, 24.00, 24.00, 0.000, 20.000, 0.000);
		Xml += MakeTimeBlock(30,   169.2,  2.399, 53.27, 24.25, 0.000, 20.000, 0.000);
		Xml += MakeTimeBlock(60,   676.8,  1.905, 95.95, 26.20, 0.001, 1.917,  0.010);
		Xml += MakeTimeBlock(90,   1391.2, 1.470, 155.19, 31.91, 0.008, 1.183, 0.155);
		Xml += MakeTimeBlock(120,  1391.2, 1.328, 180.99, 0.0,   0.021, 0.933, 0.551);
		Xml += MakeTimeBlock(150,  1391.2, 1.325, 187.71, 0.0,   0.038, 0.833, 1.000);
		Xml += MakeTimeBlock(510,  1391.2, 1.346, 218.23, 0.0,   0.285, 0.808, 1.000);
		Xml += MakeTimeBlock(540,  1391.2, 1.348, 219.60, 0.0,   0.306, 0.811, 1.000);
		Xml += MakeTimeBlock(600,  1391.2, 1.350, 222.13, 0.0,   0.347, 0.816, 1.000);
		Xml += TEXT("    </room>\n  </run>\n</output>\n");
		return Xml;
	}

	FString MakeInputXml()
	{
		return TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
			TEXT("<bri>\n  <tenability>\n")
			TEXT("    <monitor_height>2</monitor_height>\n")
			TEXT("    <endpoint_radiation>0.3</endpoint_radiation>\n")
			TEXT("    <endpoint_temp>1146</endpoint_temp>\n")
			TEXT("    <endpoint_visibility>10</endpoint_visibility>\n")
			TEXT("    <endpoint_FED>0.3</endpoint_FED>\n")
			TEXT("  </tenability>\n</bri>\n");
	}

	const FBRiskTenabilityRoomTable* FindRoom(const FBRiskScenarioData& Data, int32 RoomId)
	{
		return Data.TenabilityTables.FindByPredicate(
			[RoomId](const FBRiskTenabilityRoomTable& T) { return T.RoomId == RoomId; });
	}

	double SampleFED(const FBRiskTenabilityRoomTable& T, double Time)
	{
		// Nearest-or-exact lookup for the embedded sample times.
		for (const FBRiskTenabilitySample& S : T.Samples)
		{
			if (FMath::IsNearlyEqual(S.SampleTimeSeconds, Time, 0.01))
			{
				return S.FEDSum;
			}
		}
		return -1.0;
	}

	/** Build a Track-A hazard sample for one room at given calculated values. */
	FAgentBRiskHazardSample MakeCalcSample(
		int32 RoomId, double FEDSum, double FEDRad, double Vis)
	{
		FAgentBRiskHazardSample S;
		S.RoomId = RoomId;
		S.RoomIndex = RoomId;
		S.CalcFEDSum = static_cast<float>(FEDSum);
		S.CalcFEDRadSum = static_cast<float>(FEDRad);
		S.CalcVisibilityM = static_cast<float>(Vis);
		S.bHasCalcFEDSum = true;
		S.bHasCalcFEDRadSum = true;
		S.bHasCalcVisibility = true;
		return S;
	}

	FTenabilityAnalysisSettings MakeSettings()
	{
		FTenabilityAnalysisSettings Settings;  // defaults: vis 10, toxic 0.3, thermal 1.0
		Settings.bUseTemperatureCriterion = false;
		Settings.bUseLayerHeightCriterion = false;
		return Settings;
	}
}

// --- Parser: B-Risk output1.xml golden curve ---------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskTenabilityParserGoldenTest,
	"ProjectMobius.BRisk.Tenability.ParserGoldenCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskTenabilityParserGoldenTest::RunTest(const FString&)
{
	const FString Dir = MakeTenabilityTestDir();
	const FString SmvPath = FPaths::Combine(Dir, TEXT("basemodel_testBox.smv"));
	TestTrue(TEXT("write smv"), WriteText(SmvPath, MakeSmv()));
	TestTrue(TEXT("write zone"), WriteText(FPaths::Combine(Dir, TEXT("basemodel_testBox_zone.csv")), MakeZoneCsv()));
	TestTrue(TEXT("write output"), WriteText(FPaths::Combine(Dir, TEXT("output1.xml")), MakeOutputXml()));
	TestTrue(TEXT("write input"), WriteText(FPaths::Combine(Dir, TEXT("input1.xml")), MakeInputXml()));

	FBRiskScenarioData Data;
	FString Error;
	TestTrue(TEXT("import succeeds"), FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error));

	const FBRiskTenabilityRoomTable* Room = FindRoom(Data, 1);
	TestNotNull(TEXT("room 1 tenability table parsed"), Room);
	if (!Room)
	{
		return false;
	}
	TestEqual(TEXT("9 output samples"), Room->Samples.Num(), 9);

	// Visibility 20 m @30s, ~1.917 m @60s (exact document claim).
	for (const FBRiskTenabilitySample& S : Room->Samples)
	{
		if (FMath::IsNearlyEqual(S.SampleTimeSeconds, 30.0, 0.01))
		{
			TestEqual(TEXT("Vis@30s == 20"), S.VisibilityM, 20.0, 0.001);
		}
		if (FMath::IsNearlyEqual(S.SampleTimeSeconds, 60.0, 0.01))
		{
			TestEqual(TEXT("Vis@60s ~ 1.917"), S.VisibilityM, 1.917, 0.001);
			TestTrue(TEXT("Vis present flag"), S.bHasVisibility);
		}
		if (FMath::IsNearlyEqual(S.SampleTimeSeconds, 150.0, 0.01))
		{
			TestEqual(TEXT("FEDRadSum@150s == 1.0"), S.FEDRadSum, 1.0, 0.001);
		}
	}

	// FEDSum crosses 0.3 between 510 s (0.285) and 540 s (0.306).
	const double Fed510 = SampleFED(*Room, 510.0);
	const double Fed540 = SampleFED(*Room, 540.0);
	TestTrue(TEXT("FEDSum 0.3 crossing brackets 510-540"), Fed510 < 0.3 && Fed540 >= 0.3);

	// Endpoints from input1.xml.
	const FBRiskTenabilityEndpoints& E = Data.TenabilityEndpoints;
	TestTrue(TEXT("monitor_height parsed"), E.bHasMonitorHeight);
	TestEqual(TEXT("monitor_height == 2"), E.MonitorHeightM, 2.0, 0.001);
	TestEqual(TEXT("endpoint_visibility == 10"), E.EndpointVisibilityM, 10.0, 0.001);
	TestEqual(TEXT("endpoint_FED == 0.3"), E.EndpointFED, 0.3, 0.001);
	TestEqual(TEXT("endpoint_radiation == 0.3"), E.EndpointRadiation, 0.3, 0.001);
	// endpoint_temp is captured raw, NOT mapped to Celsius.
	TestTrue(TEXT("endpoint_temp present"), E.bHasEndpointTemp);
	TestEqual(TEXT("endpoint_temp raw == 1146"), E.EndpointTempRaw, 1146.0, 0.001);

	return true;
}

// --- Algorithm: doc Tests 1-5 + FIX A -----------------------------------------
//
// MIGRATION NOTE (Task 4, tenability-timeline v2): UpdateAgentTenability's runtime FED
// banking (Track B: LastExposureRoomId/EntryRoomToxicFED/PriorRoomsToxicFED/etc. on
// FAgentBRiskExposureFragment) and its one-way failure latch (bTenabilityFailed lock,
// StampFailure) are DELETED — dose is now a closed-form query over a precomputed
// per-agent FAgentTenabilityTimeline (FAgentTimelineSetBuilder + DoseAt), and failure
// state is precomputed (ComputeFailureData) and PROJECTED by the caller, not derived by
// this function. UpdateAgentTenability -> ComputeInstantaneousTenability(Tenability,
// Sample, Settings, CurrentSimTime, InToxicDose, InThermalDose): dose is now an INPUT.
// The dose-banking assertions below (Tests 1-3, scrub safety) are re-expressed against
// FAgentTimelineSetBuilder-shaped intervals + DoseAt, keeping the SAME golden numbers.
// The failure-latch assertions (Test 4's FirstFailureCriterion/Time, FIX A's
// bTenabilityFailed, the failure-lock sub-test) are re-expressed as PROJECTION checks
// in FBRiskTenabilityFailureProjectionTest below, replicating the health processor's
// projection arithmetic (bFailedByNow = TL.FirstFailureTimeSeconds >= 0 && t >= that)
// since the processor itself needs a live UWorld to execute.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskTenabilityModelTest,
	"ProjectMobius.BRisk.Tenability.Model",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskTenabilityModelTest::RunTest(const FString&)
{
	using namespace UE::Mobius::Tenability;
	const FTenabilityAnalysisSettings Settings = MakeSettings();

	// Test 5 - display risk is MAX not SUM. Dose is now supplied directly as an input
	// (no baseline bookkeeping needed): toxic dose 0.09 (endpoint 0.3) and thermal dose
	// 0.2 (endpoint 1.0) are passed straight through.
	{
		FAgentEgressTenabilityFragment T;
		// Vis risk 0.4 => vis 16 (ref 20, endpoint 10): (20-16)/(20-10) = 0.4.
		// Toxic risk 0.09/0.3 = 0.3. Thermal risk 0.2/1.0 = 0.2. Max = 0.4 (visibility).
		ComputeInstantaneousTenability(T, MakeCalcSample(1, 0.09, 0.2, 16.0), Settings, 11.0f, 0.09f, 0.2f);
		TestEqual(TEXT("Test5 DisplayRisk == max 0.4"), T.DisplayRisk, 0.4f, 1e-3f);
		TestTrue(TEXT("Test5 not additive 0.9"), T.DisplayRisk < 0.5f);
	}

	// Test 4 - simultaneous visibility + thermal FED CURRENT-FRAME failure indicators.
	// (FirstFailureCriterion/FirstFailureTimeSeconds/bTenabilityFailed are no longer set
	// by ComputeInstantaneousTenability -- they are PROJECTED by the caller from the
	// timeline; see FBRiskTenabilityFailureProjectionTest for that half of doc Test 4.)
	{
		FAgentEgressTenabilityFragment T;
		// Frame 1: clear (vis 20, thermal dose 0.0) -> no current-frame failure.
		ComputeInstantaneousTenability(T, MakeCalcSample(1, 0.0, 0.0, 20.0), Settings, 60.0f, 0.0f, 0.0f);
		TestFalse(TEXT("Test4 no visibility failure on clear frame"), T.bVisibilityFailed);
		TestFalse(TEXT("Test4 no thermal failure on clear frame"), T.bThermalFEDFailed);
		// Frame 2: vis collapses to 5 AND thermal FED dose reaches its endpoint (1.0).
		ComputeInstantaneousTenability(T, MakeCalcSample(1, 0.0, 1.0, 5.0), Settings, 61.0f, 0.0f, 1.0f);
		TestTrue(TEXT("Test4 visibility failed"), T.bVisibilityFailed);
		TestTrue(TEXT("Test4 thermal failed"), T.bThermalFEDFailed);
		const bool bMaskVis = (T.FailureMask & UE::Mobius::TenabilityFailureFlags::Visibility) != 0;
		const bool bMaskThermal = (T.FailureMask & UE::Mobius::TenabilityFailureFlags::ThermalFED) != 0;
		TestTrue(TEXT("Test4 mask holds both failures"), bMaskVis && bMaskThermal);
	}

	// FIX A - late entry into a thermally-saturated zone. Dose is now a direct input
	// (0.30, already past the room's earlier saturation -- no banking needed to express
	// "late entrant"). The instantaneous Temperature criterion is the correct
	// untenability signal for an already-lethal zone; verify it trips (current-frame).
	{
		FTenabilityAnalysisSettings TempSettings = MakeSettings();
		TempSettings.bUseTemperatureCriterion = true;
		TempSettings.EndpointTemperatureC = 60.0f;

		FAgentEgressTenabilityFragment T;
		FAgentBRiskHazardSample S = MakeCalcSample(1, 0.30, 1.0, 0.8);  // saturated zone
		S.bHasCalcLayerHeight = true;
		S.CalcLayerHeightM = 1.3f;          // interface below monitor height (2 m) -> upper layer
		S.bHasCalcTemperature = true;
		S.CalcUpperTemperatureC = 218.0f;   // lethal upper layer
		ComputeInstantaneousTenability(T, S, TempSettings, 510.0f, 0.30f, 1.0f);
		TestTrue(TEXT("FIXA temperature failure flagged"), T.bTemperatureFailed);
		const bool bMaskTemp = (T.FailureMask & UE::Mobius::TenabilityFailureFlags::Temperature) != 0;
		TestTrue(TEXT("FIXA temperature in failure mask"), bMaskTemp);
	}

	return true;
}

// --- Projection: failure-lock display (doc Test 4's latch half + old "Failure lock" sub-test) ---
// The processor projects failure state from a precomputed timeline:
//   bFailedByNow = TL.FirstFailureTimeSeconds >= 0 && t + eps >= TL.FirstFailureTimeSeconds;
//   if (bFailedByNow) { DisplayRisk = 1; CurrentDominantCriterion = TL.FirstFailureCriterion; Health = 0; }
// This test replicates that arithmetic directly (the processor itself needs a live UWorld to
// execute), against a hand-built timeline with FirstFailureTimeSeconds = 60 / Visibility -- the
// same failure time and criterion the old runtime latch reached in doc Test 4 / the old
// "Failure lock" sub-test. Same golden expectations (locked DisplayRisk 1.0, criterion Visibility,
// live pre-failure state before the failure time), expressed against the new API.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskTenabilityFailureProjectionTest,
	"ProjectMobius.BRisk.Tenability.FailureLockProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskTenabilityFailureProjectionTest::RunTest(const FString&)
{
	using namespace UE::Mobius::Tenability;
	const FTenabilityAnalysisSettings Settings = MakeSettings();

	FAgentTenabilityTimeline Timeline;
	Timeline.FirstFailureTimeSeconds = 60.0f;
	Timeline.FirstFailureCriterion = ETenabilityCriterion::Visibility;
	Timeline.FirstFailureMask = UE::Mobius::TenabilityFailureFlags::Visibility;

	// Replicates AgentEgressHealthCalculationProcessor::Execute's projection block exactly.
	auto ProjectFailure = [](FAgentEgressTenabilityFragment& T, const FAgentTenabilityTimeline& TL, float CurrentSimTime)
	{
		const bool bFailedByNow = TL.FirstFailureTimeSeconds >= 0.0f
			&& CurrentSimTime + UE_SMALL_NUMBER >= TL.FirstFailureTimeSeconds;
		T.bTenabilityFailed = bFailedByNow;
		T.FirstFailureTimeSeconds = TL.FirstFailureTimeSeconds;
		T.FirstFailureCriterion = TL.FirstFailureCriterion;
		T.FailureMask = bFailedByNow ? TL.FirstFailureMask : T.FailureMask;
		if (bFailedByNow)
		{
			T.DisplayRisk = 1.0f;
			T.CurrentDominantCriterion = T.FirstFailureCriterion;
			T.Health = 0.0f;
		}
		return bFailedByNow;
	};

	// At t=55 (BEFORE the failure time): live pre-failure state -- vis clear (20 m), no dose.
	// ComputeInstantaneousTenability computes the live risk; the projection must NOT override it.
	{
		FAgentEgressTenabilityFragment T;
		ComputeInstantaneousTenability(T, MakeCalcSample(1, 0.0, 0.0, 20.0), Settings, 55.0f, 0.0f, 0.0f);
		const bool bFailedByNow = ProjectFailure(T, Timeline, 55.0f);
		TestFalse(TEXT("t=55 not failed yet (before failure time)"), bFailedByNow);
		TestTrue(TEXT("t=55 live low risk shown"), T.DisplayRisk < 0.5f);
	}

	// At t=200 (AFTER the failure time): bar freezes on the cause -- full bar, criterion
	// locked to Visibility -- regardless of what the live (thermal-saturated) frame would show.
	{
		FAgentEgressTenabilityFragment T;
		// Live frame at t=200 would show thermal FED dose at its endpoint (1.0) and vis 5 m;
		// the projection must override DisplayRisk/CurrentDominantCriterion to the locked cause.
		ComputeInstantaneousTenability(T, MakeCalcSample(1, 0.0, 1.0, 5.0), Settings, 200.0f, 0.0f, 1.0f);
		const bool bFailedByNow = ProjectFailure(T, Timeline, 200.0f);
		TestTrue(TEXT("t=200 failed (after failure time)"), bFailedByNow);
		TestEqual(TEXT("Lock bar full after failure"), T.DisplayRisk, 1.0f, 1e-3f);
		TestEqual(TEXT("Lock criterion stays Visibility (cause), not Thermal"),
			static_cast<uint8>(T.CurrentDominantCriterion),
			static_cast<uint8>(ETenabilityCriterion::Visibility));
		TestEqual(TEXT("Health zero when failed"), T.Health, 0.0f, 1e-4f);
	}

	// Exactly AT the failure time: bFailedByNow must be true (>= comparison, UE_SMALL_NUMBER epsilon).
	{
		FAgentEgressTenabilityFragment T;
		ComputeInstantaneousTenability(T, MakeCalcSample(1, 0.0, 0.0, 5.0), Settings, 60.0f, 0.0f, 0.0f);
		const bool bFailedByNow = ProjectFailure(T, Timeline, 60.0f);
		TestTrue(TEXT("t=60 (== failure time) is failed"), bFailedByNow);
	}

	return true;
}

// --- Algorithm: doc Test 3 - re-entry dose retention + navigation-independence -----------
// Leaving every B-Risk room (e.g. transiting an unmodelled corridor) closes the agent's
// occupancy interval; a later re-entry opens a NEW interval whose Prior is the closed
// interval's prefix-summed contribution (FAgentTimelineSetBuilder's entry-baseline rule --
// see AgentTenabilityTimelineTest.cpp's TimelineCore suite for the builder itself). Dose at
// any time is then a closed-form DoseAt query, so replaying/rewinding the same span can never
// double- or under-count (the old runtime "spurious exit on rewind" bug class is structurally
// impossible here -- there is no exit-time-direction check to get wrong because there is no
// runtime banking at all). This test re-expresses doc Test 3's exact numbers as a hand-built
// two-interval timeline (mirroring what FAgentTimelineSetBuilder would produce for entry at
// t=100 (room FED 0.20), forward exit at t=131 (room FED 0.30), re-entry at t=200 (room FED
// 0.80), still-present at t=230 (room FED 0.85)) and proves DoseAt reproduces the SAME goldens,
// plus a shuffled-order query proving navigation independence on this exact data.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskTenabilityReentryDoseRetentionTest,
	"ProjectMobius.BRisk.Tenability.ReentryDoseRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskTenabilityReentryDoseRetentionTest::RunTest(const FString&)
{
	using namespace UE::Mobius::Tenability;

	// Synthetic room FED curve hitting exactly the doc Test 3 checkpoints:
	//   t=100 -> 0.20 (entry), t=130..131 -> 0.30 (dose-check / forward-exit sample),
	//   t=200 -> 0.80 (re-entry), t=230 -> 0.85 (still-present sample).
	// DoseAt only ever queries the sampler at an exact requested time or (for interval
	// construction) an entry/exit time, so a small checkpoint table is exact and sufficient.
	auto RoomCurve = [](int32 /*RoomIndex*/, double TimeSeconds, double& OutToxic, double& OutThermal)
	{
		OutThermal = 0.0;
		if (TimeSeconds <= 100.0)      { OutToxic = 0.20; }
		else if (TimeSeconds <= 131.0) { OutToxic = 0.30; } // room curve through the genuine exit sample
		else if (TimeSeconds < 200.0)  { OutToxic = 0.30; } // flat in the corridor gap (no room data)
		else if (TimeSeconds < 230.0)  { OutToxic = 0.80; } // room curve at/just after re-entry
		else                            { OutToxic = 0.85; }
	};

	// Interval 0: room A, [100, 131], entry FED 0.20, exit FED sampled at 131 -> 0.30.
	// Interval 1 (re-entry): room A, [200, 999 (still open/far future)], entry FED 0.80,
	// Prior = Interval0.Prior(0) + max(Interval0.Exit - Interval0.Entry, 0) = 0.10.
	FAgentTenabilityTimeline Timeline;
	{
		FAgentRoomOccupancyInterval I0;
		I0.RoomIndex = 0;
		I0.RoomId = 1;
		I0.EntryTimeSeconds = 100.0f;
		I0.ExitTimeSeconds = 131.0f;
		I0.EntryToxicFED = 0.20f;
		I0.ExitToxicFED = 0.30f;   // room curve at the genuine forward exit time (131)
		I0.PriorToxicFED = 0.0f;
		Timeline.Intervals.Add(I0);

		FAgentRoomOccupancyInterval I1;
		I1.RoomIndex = 0;
		I1.RoomId = 1;
		I1.EntryTimeSeconds = 200.0f;
		I1.ExitTimeSeconds = 999.0f; // still occupied at every query time below
		I1.EntryToxicFED = 0.80f;
		I1.ExitToxicFED = 0.85f;    // unused while queries stay inside [200, 999)
		I1.PriorToxicFED = I0.PriorToxicFED + FMath::Max(I0.ExitToxicFED - I0.EntryToxicFED, 0.0f); // 0.10
		Timeline.Intervals.Add(I1);
	}

	auto DoseToxicAt = [&](float T)
	{
		float Toxic = -1.0f, Thermal = -1.0f;
		FRoomFEDSampler SamplerRef(RoomCurve);
		Timeline.DoseAt(T, SamplerRef, Toxic, Thermal);
		return Toxic;
	};

	// Doc Test 3 goldens, reproduced via DoseAt:
	//   dose@130 (inside I0) = Prior(0) + max(Sampler(130)-Entry(0.20),0) = 0.30-0.20 = 0.10.
	const float Accrued = DoseToxicAt(130.0f);
	TestEqual(TEXT("Test3 accrued 0.10 before exit"), Accrued, 0.10f, 1e-4f);
	//   dose@131 (== exit time -> flat completed total) = Prior(0) + max(0.30-0.20,0) = 0.10.
	TestEqual(TEXT("Test3 exit banks accrued dose"), DoseToxicAt(131.0f), 0.10f, 1e-4f);
	//   dose@200 (inside I1, at entry) = Prior(0.10) + max(Sampler(200)-Entry(0.80),0) = 0.10+0 = 0.10.
	TestEqual(TEXT("Test3 no exposure added on re-entry"), DoseToxicAt(200.0f), Accrued, 1e-4f);
	//   dose@230 (inside I1) = Prior(0.10) + max(Sampler(230)-Entry(0.80),0) = 0.10 + (0.85-0.80) = 0.15.
	TestEqual(TEXT("Test3 delta accrues after re-entry"), DoseToxicAt(230.0f), 0.15f, 1e-4f);

	// Navigation independence on this exact data: shuffled-order queries reproduce bitwise-identical
	// values per time (the old "spurious exit on rewind" bug class -- this is the proof it cannot
	// recur, since DoseAt has no notion of query order or direction at all).
	{
		const TArray<float> ShuffledTimes({230.0f, 130.0f, 200.0f, 130.0f, 131.0f, 230.0f});
		TMap<float, float> FirstSeen;
		FRoomFEDSampler SamplerRef(RoomCurve);
		for (float T : ShuffledTimes)
		{
			float Toxic = 0.0f, Thermal = 0.0f;
			Timeline.DoseAt(T, SamplerRef, Toxic, Thermal);
			if (const float* Prior = FirstSeen.Find(T))
			{
				TestTrue(FString::Printf(TEXT("shuffled DoseAt(%.0f) bitwise-repeatable"), T), *Prior == Toxic);
			}
			else
			{
				FirstSeen.Add(T, Toxic);
			}
		}
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
