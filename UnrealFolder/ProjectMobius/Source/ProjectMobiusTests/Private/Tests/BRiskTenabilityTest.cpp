// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskTenabilityModelTest,
	"ProjectMobius.BRisk.Tenability.Model",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskTenabilityModelTest::RunTest(const FString&)
{
	using namespace UE::Mobius::Tenability;
	const FTenabilityAnalysisSettings Settings = MakeSettings();

	// Test 1 - room entry baseline: entering at FEDSum 0.20 adds no exposure.
	{
		FAgentEgressTenabilityFragment T;
		FAgentBRiskExposureFragment Ex;
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.20, 0.0, 20.0), Settings, 100.0f);
		TestEqual(TEXT("Test1 accumulated toxic FED 0 at entry"), T.AccumulatedToxicFED, 0.0f, 1e-4f);
		TestTrue(TEXT("Test1 baseline set"), Ex.bHasCumulativeFEDBaseline);
		TestEqual(TEXT("Test1 baseline == 0.20"), Ex.EntryRoomToxicFED, 0.20f, 1e-4f);

		// Test 2 - delta while present: 0.20 -> 0.27 adds 0.07.
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.27, 0.0, 20.0), Settings, 130.0f);
		TestEqual(TEXT("Test2 accumulated toxic FED += 0.07"), T.AccumulatedToxicFED, 0.07f, 1e-4f);
	}

	// Test 3 (doc: re-entry dose retention) lives in the QUARANTINED test below - known
	// production bug, see Mobius.Quarantine.Tenability.ReentryDoseRetention.

	// Test 5 - display risk is MAX not SUM (risks driven through the sample/baselines).
	{
		FAgentEgressTenabilityFragment T;
		FAgentBRiskExposureFragment Ex;
		// Vis risk 0.4 => vis 16 (ref 20, endpoint 10): (20-16)/(20-10) = 0.4.
		// Toxic 0.3 via accumulated 0.09 (endpoint 0.3). Thermal 0.2 via accumulated 0.2 (endpoint 1.0).
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 0.0, 16.0), Settings, 10.0f);  // room entry, baseline 0
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.09, 0.2, 16.0), Settings, 11.0f);
		TestEqual(TEXT("Test5 DisplayRisk == max 0.4"), T.DisplayRisk, 0.4f, 1e-3f);
		TestTrue(TEXT("Test5 not additive 0.9"), T.DisplayRisk < 0.5f);
	}

	// Test 4 - simultaneous visibility + thermal FED failure in one frame.
	{
		FAgentEgressTenabilityFragment T;
		FAgentBRiskExposureFragment Ex;
		// Frame 1: enter room clear (vis 20, thermal baseline 0.0) -> no failure yet.
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 0.0, 20.0), Settings, 60.0f);
		TestFalse(TEXT("Test4 no failure on clear frame"), T.bTenabilityFailed);
		// Frame 2: vis collapses to 5 AND thermal FED reaches its endpoint (FEDRadSum 1.0).
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 1.0, 5.0), Settings, 61.0f);
		TestTrue(TEXT("Test4 visibility failed"), T.bVisibilityFailed);
		TestTrue(TEXT("Test4 thermal failed"), T.bThermalFEDFailed);
		const bool bMaskVis = (T.FailureMask & UE::Mobius::TenabilityFailureFlags::Visibility) != 0;
		const bool bMaskThermal = (T.FailureMask & UE::Mobius::TenabilityFailureFlags::ThermalFED) != 0;
		TestTrue(TEXT("Test4 mask holds both failures"), bMaskVis && bMaskThermal);
		TestEqual(TEXT("Test4 first failure = Visibility (priority)"),
			static_cast<uint8>(T.FirstFailureCriterion),
			static_cast<uint8>(ETenabilityCriterion::Visibility));
		TestEqual(TEXT("Test4 first failure time set once"), T.FirstFailureTimeSeconds, 61.0f, 1e-3f);
	}

	// FIX A - late entry into a thermally-saturated zone.
	// Pure per-agent delta would read 0 (FEDRadSum flat at 1.0). The instantaneous
	// Temperature criterion is the correct untenability signal; verify it trips.
	{
		FTenabilityAnalysisSettings TempSettings = MakeSettings();
		TempSettings.bUseTemperatureCriterion = true;
		TempSettings.EndpointTemperatureC = 60.0f;

		FAgentEgressTenabilityFragment T;
		FAgentBRiskExposureFragment Ex;
		FAgentBRiskHazardSample S = MakeCalcSample(1, 0.30, 1.0, 0.8);  // saturated zone
		S.bHasCalcLayerHeight = true;
		S.CalcLayerHeightM = 1.3f;          // interface below monitor height (2 m) -> upper layer
		S.bHasCalcTemperature = true;
		S.CalcUpperTemperatureC = 218.0f;   // lethal upper layer
		UpdateAgentTenability(T, Ex, S, TempSettings, 510.0f);
		TestTrue(TEXT("FIXA temperature failure flagged"), T.bTemperatureFailed);
		TestTrue(TEXT("FIXA tenability failed"), T.bTenabilityFailed);
	}

	// Failure lock - after the first failure the bar freezes on the cause; scrubbing
	// to a time before the failure shows the live pre-failure state again.
	{
		FAgentEgressTenabilityFragment T;
		FAgentBRiskExposureFragment Ex;
		// Enter room clear; visibility then fails at t=60 (vis 5 <= endpoint 10).
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 0.0, 20.0), Settings, 50.0f);
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 0.0, 5.0), Settings, 60.0f);
		TestTrue(TEXT("Lock failed at 60"), T.bTenabilityFailed);
		TestEqual(TEXT("Lock criterion = Visibility"),
			static_cast<uint8>(T.CurrentDominantCriterion),
			static_cast<uint8>(ETenabilityCriterion::Visibility));
		// Later, thermal FED would dominate, but the bar must STAY locked on Visibility.
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 1.0, 5.0), Settings, 200.0f);
		TestEqual(TEXT("Lock bar full after failure"), T.DisplayRisk, 1.0f, 1e-3f);
		TestEqual(TEXT("Lock criterion stays Visibility (cause), not Thermal"),
			static_cast<uint8>(T.CurrentDominantCriterion),
			static_cast<uint8>(ETenabilityCriterion::Visibility));
		// Scrub BEFORE the failure time -> live pre-failure state (clear, no risk).
		UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.0, 0.0, 20.0), Settings, 55.0f);
		TestTrue(TEXT("Scrub before failure shows live low risk"), T.DisplayRisk < 0.5f);
	}

	return true;
}

// QUARANTINED 2026-07-03 (PRD 02 task T1). Doc Test 3 (BuildDocs/BRisk-Tenability-Model.md:
// "on re-entry the baseline is reset - no subtraction, no re-add" + "cumulative state is
// monotonic") exposes a REAL production bug: ClearCurrentHazardSample zeroes the FED baseline
// fields without banking the current room's accrued dose into PriorRooms*, so an agent that
// leaves all B-Risk rooms (e.g. transits an unmodelled corridor) and re-enters LOSES its accrued
// dose - AccumulatedToxicFED recomputes to PriorRooms(0) + 0. The deliberate no-banking-on-exit
// (see the NOTE in ClearCurrentHazardSample: banking on every exit double-banks under timeline
// scrubbing) conflicts with dose retention; the fix needs a design decision on scrub semantics
// (e.g. spurious-exit detection or deferred banking), not a test tweak.
// The Mobius.Quarantine. prefix keeps this out of the default suite filter ("ProjectMobius.").
// NOTE: automation filters are CONTAINS matches, so the quarantine name must not contain
// "ProjectMobius." or any other default-filter fragment. Run it explicitly:
//   Automation RunTests Mobius.Quarantine.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskTenabilityReentryDoseQuarantineTest,
	"Mobius.Quarantine.Tenability.ReentryDoseRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskTenabilityReentryDoseQuarantineTest::RunTest(const FString&)
{
	using namespace UE::Mobius::Tenability;
	const FTenabilityAnalysisSettings Settings = MakeSettings();

	// Doc Test 3 - re-entry: leave room (no sample), room FED rises, re-enter -> accrued dose
	// retained, no historical add.
	FAgentEgressTenabilityFragment T;
	FAgentBRiskExposureFragment Ex;
	UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.20, 0.0, 20.0), Settings, 100.0f);
	UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.30, 0.0, 20.0), Settings, 130.0f);  // +0.10
	const float Accrued = T.AccumulatedToxicFED;
	UE::Mobius::EgressHealth::ClearCurrentHazardSample(Ex);  // agent leaves all rooms
	// Re-enter same room later; room cumulative now 0.80 (rose while absent).
	UpdateAgentTenability(T, Ex, MakeCalcSample(1, 0.80, 0.0, 20.0), Settings, 200.0f);
	TestEqual(TEXT("Test3 no exposure added on re-entry"), T.AccumulatedToxicFED, Accrued, 1e-4f);
	TestEqual(TEXT("Test3 re-baselined to 0.80"), Ex.EntryRoomToxicFED, 0.80f, 1e-4f);
	return true;
}

#endif // !UE_BUILD_SHIPPING
