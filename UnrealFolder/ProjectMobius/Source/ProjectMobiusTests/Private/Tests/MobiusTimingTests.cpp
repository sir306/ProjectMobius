// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// MobiusTimingTests.cpp
//
// PRD 02 task T6 — timing-metric tests. Named under "Mobius.Timing." so the default correctness
// filter ("ProjectMobius.", contains-match) NEVER runs them: timing variance must not redden the
// gate. Run explicitly via MobiusPerf\RunTests.ps1 -IncludeTiming.
//
// Two kinds of output:
//  - RATIO assertions, measured in the same run on the same machine (machine-independent):
//      simdjson >= 2x the pull-parser on a >=64 MB synthetic document (deliberately loose vs the
//      observed ~10-20x, so background load can't flake the suite);
//      A6 fast-reload <= 0.5x the full import on a ~10 MB fixture.
//  - TREND rows appended to <ProjectSaved>/MobiusPerfTimings/timings.csv (absolute seconds +
//    MB/s). RunTests.ps1 merges them into MobiusPerf/TestReports/timings.csv with the git rev.
//    Absolute numbers are never asserted.
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MobiusAgentDataImporter.h"
#include "SimData/SimDiskCache.h"
#include "MobiusTestDataRoots.h"

namespace
{
	FString TimingDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusPerfTimings"));
	}

	/** Append one metric row to the trend CSV (header written on first use). */
	void AppendTimingRow(const FString& Fixture, const FString& Metric, double Seconds, double Megabytes)
	{
		const FString CsvPath = FPaths::Combine(TimingDir(), TEXT("timings.csv"));
		FString Row;
		if (!IFileManager::Get().FileExists(*CsvPath))
		{
			IFileManager::Get().MakeDirectory(*TimingDir(), /*Tree*/ true);
			Row += TEXT("timestamp_utc,machine,fixture,metric,seconds,mb_per_s\n");
		}
		Row += FString::Printf(TEXT("%s,%s,%s,%s,%.6f,%.2f\n"),
			*FDateTime::UtcNow().ToIso8601(),
			FPlatformProcess::ComputerName(),
			*Fixture,
			*Metric,
			Seconds,
			(Seconds > 0.0 && Megabytes > 0.0) ? Megabytes / Seconds : 0.0);
		FFileHelper::SaveStringToFile(Row, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}

	/** Deterministic synthetic agent JSON of roughly (AgentsPerTimestep x NumTimesteps) samples. */
	FString GenerateSyntheticJson(int32 NumTimesteps, int32 AgentsPerTimestep)
	{
		FString Json;
		// ~120 bytes per sample line; reserve generously to avoid re-allocation churn.
		Json.Reserve(140LL * NumTimesteps * AgentsPerTimestep + 4096);
		Json += TEXT("{\n\"metadata\": { \"duration\": ");
		Json += FString::SanitizeFloat(0.1 * NumTimesteps);
		Json += TEXT(", \"sampling_rate\": 0.1, \"max_num_entities\": ");
		Json += FString::FromInt(AgentsPerTimestep);
		Json += TEXT(", \"isSI\": true, \"isDeg\": true },\n\"entities\": [\n");
		for (int32 Agent = 0; Agent < AgentsPerTimestep; ++Agent)
		{
			Json += FString::Printf(TEXT("{ \"id\": %d, \"name\": \"agent_%d\", \"simTimeS\": %d.5, \"max_speed\": 1.5, \"m_plane\": \"floor_0\", \"map\": 0 }%s\n"),
				Agent, Agent, NumTimesteps / 10, Agent + 1 < AgentsPerTimestep ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("],\n\"simulation\": [\n");
		for (int32 Ts = 0; Ts < NumTimesteps; ++Ts)
		{
			Json += TEXT("{ \"samples\": [\n");
			for (int32 Agent = 0; Agent < AgentsPerTimestep; ++Agent)
			{
				// Vary values so nothing is trivially compressible/constant-folded.
				Json += FString::Printf(TEXT("{ \"entity\": %d, \"rotation\": %d.%02d, \"speed\": 1.%03d, \"mode\": \"walk\", \"position\": { \"x\": %d.%03d, \"y\": %d.%03d, \"z\": 0.0 } }%s\n"),
					Agent,
					(Ts + Agent) % 360, Agent % 100,
					Ts % 1000,
					Agent % 500, Ts % 1000,
					(Agent * 7) % 500, (Ts * 3) % 1000,
					Agent + 1 < AgentsPerTimestep ? TEXT(",") : TEXT(""));
			}
			Json += FString::Printf(TEXT("] }%s\n"), Ts + 1 < NumTimesteps ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("]\n}\n");
		return Json;
	}
}

// --- simdjson vs pull-parser ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusTimingJsonParserComparisonTest,
	"Mobius.Timing.JsonParserComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMobiusTimingJsonParserComparisonTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Dir = TimingDir();
	FileManager.MakeDirectory(*Dir, /*Tree*/ true);
	const FString FixturePath = FPaths::Combine(Dir, TEXT("ParserComparison.json"));

	// ~500 agents x 1100 timesteps ~= 70 MB — big enough that per-call overhead vanishes.
	{
		const FString Json = GenerateSyntheticJson(1100, 500);
		TestTrue(TEXT("fixture is >= 64 MB"), Json.Len() >= 64 * 1024 * 1024);
		TestTrue(TEXT("fixture written"), FFileHelper::SaveStringToFile(
			Json, *FixturePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	}
	const double Megabytes = static_cast<double>(FileManager.FileSize(*FixturePath)) / (1024.0 * 1024.0);

	// simdjson runs FIRST (cold file cache) — the bias is against the fast path, so a pass is
	// conservative.
	FMobiusAgentSimulationData SimdData;
	const double SimdStart = FPlatformTime::Seconds();
	const bool bSimdOk = FMobiusAgentDataImporter::ParseJsonWithSimdjson(FixturePath, SimdData);
	const double SimdSeconds = FPlatformTime::Seconds() - SimdStart;
	TestTrue(TEXT("simdjson parse succeeded"), bSimdOk);

	FMobiusAgentSimulationData PullData;
	const double PullStart = FPlatformTime::Seconds();
	const bool bPullOk = FMobiusAgentDataImporter::ParseJsonWithPullParser(FixturePath, PullData);
	const double PullSeconds = FPlatformTime::Seconds() - PullStart;
	TestTrue(TEXT("pull-parser parse succeeded"), bPullOk);

	TestEqual(TEXT("both parsers agree on sample count"), SimdData.Samples.Num(), PullData.Samples.Num());

	AppendTimingRow(TEXT("synthetic_70mb"), TEXT("json_parse_simdjson"), SimdSeconds, Megabytes);
	AppendTimingRow(TEXT("synthetic_70mb"), TEXT("json_parse_pullparser"), PullSeconds, Megabytes);
	AddInfo(FString::Printf(TEXT("simdjson %.2f s (%.0f MB/s) vs pull-parser %.2f s (%.0f MB/s) — ratio %.1fx"),
		SimdSeconds, Megabytes / SimdSeconds, PullSeconds, Megabytes / PullSeconds,
		SimdSeconds > 0.0 ? PullSeconds / SimdSeconds : 0.0));

	// Loose gate: observed ~10-20x; assert only >= 2x so background load can't flake the run.
	TestTrue(FString::Printf(TEXT("simdjson at least 2x pull-parser (%.2f s vs %.2f s)"), SimdSeconds, PullSeconds),
		SimdSeconds * 2.0 <= PullSeconds);

	FileManager.Delete(*FixturePath, false, true);
	return true;
}

// --- fast-reload vs full import --------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusTimingFastReloadComparisonTest,
	"Mobius.Timing.FastReloadComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMobiusTimingFastReloadComparisonTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Dir = TimingDir();
	FileManager.MakeDirectory(*Dir, /*Tree*/ true);
	const FString FixturePath = FPaths::Combine(Dir, TEXT("FastReloadComparison.json"));

	// ~200 agents x 400 timesteps ~= 10 MB — a real (if small) workload for both paths.
	TestTrue(TEXT("fixture written"), FFileHelper::SaveStringToFile(
		GenerateSyntheticJson(400, 200), *FixturePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const double Megabytes = static_cast<double>(FileManager.FileSize(*FixturePath)) / (1024.0 * 1024.0);

	IConsoleVariable* WriteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
	IConsoleVariable* FastReloadCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.FastReload"));
	const int32 SavedWrite = WriteCVar ? WriteCVar->GetInt() : 1;
	const int32 SavedFastReload = FastReloadCVar ? FastReloadCVar->GetInt() : 1;
	if (WriteCVar) { WriteCVar->Set(1, ECVF_SetByCode); }
	if (FastReloadCVar) { FastReloadCVar->Set(1, ECVF_SetByCode); }

	const uint64 SourceHash = MobiusSimCache::ComputeSourceHash(FixturePath);
	const FString CachePath = MobiusSimCache::MakeCacheFilePath(FixturePath, SourceHash);
	FileManager.Delete(*CachePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);

	UAgentDataSubsystem* Owner = NewObject<UAgentDataSubsystem>();

	FProcessAgentSimulationDataRunnable RunnableFull(FixturePath, Owner, /*bAutoStartThread*/ false);
	RunnableFull.Run();
	TestFalse(TEXT("first import took the full path"), RunnableFull.ImportTimings.bUsedFastReload);
	TestTrue(TEXT("cache written by first import"), FileManager.FileExists(*CachePath));

	FProcessAgentSimulationDataRunnable RunnableFast(FixturePath, Owner, /*bAutoStartThread*/ false);
	RunnableFast.Run();
	TestTrue(TEXT("second import fast-reloaded"), RunnableFast.ImportTimings.bUsedFastReload);

	const FMobiusImportTimings& Full = RunnableFull.ImportTimings;
	const FMobiusImportTimings& Fast = RunnableFast.ImportTimings;
	AppendTimingRow(TEXT("synthetic_10mb"), TEXT("import_full_total"), Full.TotalSeconds, Megabytes);
	AppendTimingRow(TEXT("synthetic_10mb"), TEXT("import_full_parse"), Full.ParseSeconds, Megabytes);
	AppendTimingRow(TEXT("synthetic_10mb"), TEXT("import_full_convert"), Full.ConvertSeconds, 0.0);
	AppendTimingRow(TEXT("synthetic_10mb"), TEXT("import_full_brackets"), Full.BracketSeconds, 0.0);
	AppendTimingRow(TEXT("synthetic_10mb"), TEXT("import_full_cachewrite"), Full.CacheWriteSeconds, 0.0);
	AppendTimingRow(TEXT("synthetic_10mb"), TEXT("import_fastreload_total"), Fast.TotalSeconds, Megabytes);
	AddInfo(FString::Printf(TEXT("full import %.2f s (parse %.2f, convert %.2f, brackets %.2f, cache %.2f) vs fast-reload %.2f s"),
		Full.TotalSeconds, Full.ParseSeconds, Full.ConvertSeconds, Full.BracketSeconds, Full.CacheWriteSeconds,
		Fast.TotalSeconds));

	TestTrue(FString::Printf(TEXT("fast-reload at most half the full import (%.2f s vs %.2f s)"), Fast.TotalSeconds, Full.TotalSeconds),
		Fast.TotalSeconds * 2.0 <= Full.TotalSeconds);

	if (WriteCVar) { WriteCVar->Set(SavedWrite, ECVF_SetByCode); }
	if (FastReloadCVar) { FastReloadCVar->Set(SavedFastReload, ECVF_SetByCode); }
	FileManager.Delete(*CachePath, false, true);
	FileManager.Delete(*FixturePath, false, true);
	return true;
}

// --- large real file (local-only trend rows, no assertions) -----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusTimingLargeFileImportTest,
	"Mobius.Timing.LargeFileImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMobiusTimingLargeFileImportTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	// Private fixture, resolved via MobiusTestDataRoots.h rather than a hardcoded drive letter.
	const FString LargeFixture = FPaths::Combine(
		TEXT("TechSchoolTest"), TEXT("TechnicalSchool5000DefaultExits.json"));
	const FString LargeFile = MobiusTestData::FindInternalFixture(LargeFixture);
	if (LargeFile.IsEmpty() || !FileManager.FileExists(*LargeFile))
	{
		AddInfo(MobiusTestData::DescribeMissingFixture(LargeFixture));
		return true;
	}
	const double Megabytes = static_cast<double>(FileManager.FileSize(*LargeFile)) / (1024.0 * 1024.0);

	// Parser-level only (no runnable, no cache interference with the user's real .msc files).
	FMobiusAgentSimulationData SimdData;
	const double SimdStart = FPlatformTime::Seconds();
	const bool bSimdOk = FMobiusAgentDataImporter::ParseJsonWithSimdjson(LargeFile, SimdData);
	const double SimdSeconds = FPlatformTime::Seconds() - SimdStart;
	TestTrue(TEXT("simdjson parses the large file"), bSimdOk);
	AppendTimingRow(TEXT("techschool_5000"), TEXT("json_parse_simdjson"), SimdSeconds, Megabytes);

	FMobiusAgentSimulationData PullData;
	const double PullStart = FPlatformTime::Seconds();
	const bool bPullOk = FMobiusAgentDataImporter::ParseJsonWithPullParser(LargeFile, PullData);
	const double PullSeconds = FPlatformTime::Seconds() - PullStart;
	TestTrue(TEXT("pull-parser parses the large file"), bPullOk);
	AppendTimingRow(TEXT("techschool_5000"), TEXT("json_parse_pullparser"), PullSeconds, Megabytes);

	AddInfo(FString::Printf(TEXT("TechSchool5000 (%.0f MB): simdjson %.2f s (%.0f MB/s), pull-parser %.2f s (%.0f MB/s), ratio %.1fx — trend rows only, nothing asserted"),
		Megabytes, SimdSeconds, Megabytes / SimdSeconds, PullSeconds, Megabytes / PullSeconds,
		SimdSeconds > 0.0 ? PullSeconds / SimdSeconds : 0.0));
	return true;
}

#endif // !UE_BUILD_SHIPPING
