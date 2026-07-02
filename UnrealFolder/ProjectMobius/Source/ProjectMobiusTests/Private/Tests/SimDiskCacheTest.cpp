// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// SimDiskCacheTest.cpp
//
// Round-trips the post-conversion .msc disk cache (perf task A3): write a known in-memory
// TMap<int32, TArray<FSimMovementSample>> via MobiusSimCache::WriteCacheFile, then re-parse the file
// field-by-field and assert every value is bit-identical. This locks down the on-disk record width and
// field order BEFORE the A4 streaming reader depends on them (Invariant 5 bit-identity). The inline
// parser here is the reference decoder / read contract.
//
// Run: UnrealEditor ProjectMobius.uproject -ExecCmds="Automation RunTests ProjectMobius.SimData" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/ThreadSafeBool.h"
#include "SimData/SimDiskCache.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"

namespace
{
	/** Small known dataset shared by the disk-cache tests (timestep 1 is empty on purpose). */
	static TMap<int32, TArray<FSimMovementSample>> MakeKnownSimData()
	{
		auto MakeSample = [](int32 Id, double Px, double Py, double Pz, double Rp, double Ry, double Rr,
		                     float Speed, uint8 Bracket, uint8 ModeIdx)
		{
			FSimMovementSample S;
			S.EntityID = Id;
			S.Position = FVector(Px, Py, Pz);
			S.Rotation = FRotator(Rp, Ry, Rr); // FRotator(Pitch, Yaw, Roll)
			S.Speed = Speed;
			S.MovementBracket = static_cast<EPedestrianMovementBracket>(Bracket);
			S.ModeIndex = ModeIdx;
			return S;
		};

		TMap<int32, TArray<FSimMovementSample>> Data;
		TArray<FSimMovementSample> Ts0;
		Ts0.Add(MakeSample(0,  10.5,  -20.25,  3.125,  45.0,  -90.0,  0.0,  1.5f,  0, 0));
		Ts0.Add(MakeSample(7,  -0.5,    0.0,  100.0,  -12.5,  180.0,  7.75, 2.25f, 1, 0));
		Data.Add(0, MoveTemp(Ts0));
		Data.Add(1, TArray<FSimMovementSample>());
		TArray<FSimMovementSample> Ts2;
		Ts2.Add(MakeSample(0,  11.0,  -19.0,   3.5,    46.0,  -89.0,  0.5,  1.625f, 2, 0));
		Data.Add(2, MoveTemp(Ts2));
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimDiskCacheRoundTripTest,
	"ProjectMobius.SimData.DiskCacheRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimDiskCacheRoundTripTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();

	// Known dataset: timestep 1 is intentionally empty to exercise off[ts+1] == off[ts]. All values are
	// exactly representable so a successful round-trip is exact equality.
	TMap<int32, TArray<FSimMovementSample>> SimulationData = MakeKnownSimData();
	const int32 NumTimesteps = SimulationData.Num();

	const float MaxTime = 0.3f;
	const float TimeBetweenSteps = 0.1f;
	const TArray<FString> ModeTable = { FString(TEXT("")), FString(TEXT("walk")) };

	// --- Source-file fingerprint needs a real file on disk. ---
	const FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusSimCacheTest"));
	FileManager.MakeDirectory(*TempDir, /*Tree*/ true);
	const FString FakeSource = FPaths::Combine(TempDir, TEXT("RoundTripSource.json"));
	FFileHelper::SaveStringToFile(TEXT("{\"fake\":\"agent source for hashing\"}"), *FakeSource);

	const uint64 SourceHash = MobiusSimCache::ComputeSourceHash(FakeSource);
	const FString CacheFilePath = MobiusSimCache::MakeCacheFilePath(FakeSource, SourceHash);
	FileManager.Delete(*CacheFilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true); // deterministic: force a fresh write

	// --- Write (core writer bypasses the cvar + reuse-skip). ---
	FThreadSafeBool bShouldStop(false);
	const bool bWrote = MobiusSimCache::WriteCacheFile(
		CacheFilePath, SourceHash, SimulationData, MaxTime, TimeBetweenSteps, ModeTable, bShouldStop);
	TestTrue(TEXT("WriteCacheFile succeeded"), bWrote);

	// --- Re-parse and verify (reference decoder; must match SimDiskCache.h field order). ---
	if (bWrote)
	{
		TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CacheFilePath) };
		if (Reader)
		{
			uint32 Magic = 0, Version = 0, NumTs = 0, RecordSize = 0, NumModes = 0;
			uint64 ReadHash = 0;
			float ReadMaxTime = 0.f, ReadTbs = 0.f;
			*Reader << Magic;
			*Reader << Version;
			*Reader << ReadHash;
			*Reader << NumTs;
			*Reader << RecordSize;
			*Reader << ReadMaxTime;
			*Reader << ReadTbs;
			*Reader << NumModes;

			TestEqual(TEXT("Magic"), static_cast<int32>(Magic), static_cast<int32>(MobiusSimCache::Magic));
			TestEqual(TEXT("Version"), static_cast<int32>(Version), static_cast<int32>(MobiusSimCache::Version));
			TestTrue(TEXT("SourceHash matches"), ReadHash == SourceHash);
			TestEqual(TEXT("NumTimesteps"), static_cast<int32>(NumTs), NumTimesteps);
			TestEqual(TEXT("RecordSize"), static_cast<int32>(RecordSize), static_cast<int32>(MobiusSimCache::RecordSize));
			TestEqual(TEXT("MaxTime"), ReadMaxTime, MaxTime);
			TestEqual(TEXT("TimeBetweenSteps"), ReadTbs, TimeBetweenSteps);
			TestEqual(TEXT("NumModes"), static_cast<int32>(NumModes), ModeTable.Num());

			// Header sanity gates the heavy record parsing (avoid huge loops on a corrupt NumTs).
			const bool bHeaderOk =
				Magic == MobiusSimCache::Magic &&
				Version == MobiusSimCache::Version &&
				RecordSize == MobiusSimCache::RecordSize &&
				ReadHash == SourceHash &&
				static_cast<int32>(NumTs) == NumTimesteps;

			TArray<FString> ReadModeTable;
			for (uint32 i = 0; i < NumModes && i < 1024u; ++i)
			{
				FString M;
				*Reader << M;
				ReadModeTable.Add(M);
			}
			for (int32 i = 0; i < ModeTable.Num() && i < ReadModeTable.Num(); ++i)
			{
				TestEqual(FString::Printf(TEXT("ModeTable[%d]"), i), ReadModeTable[i], ModeTable[i]);
			}

			if (bHeaderOk)
			{
				// Offset table: NumTs + 1 entries.
				TArray<uint64> Offsets;
				for (uint32 i = 0; i <= NumTs; ++i)
				{
					uint64 Off = 0;
					*Reader << Off;
					Offsets.Add(Off);
				}

				// Per-timestep record blocks: seek by offset, decode each record, compare to source.
				for (uint32 Ts = 0; Ts < NumTs; ++Ts)
				{
					const TArray<FSimMovementSample>* Expected = SimulationData.Find(static_cast<int32>(Ts));
					const int32 ExpectedCount = Expected ? Expected->Num() : 0;
					const int64 BlockBytes = static_cast<int64>(Offsets[Ts + 1] - Offsets[Ts]);
					TestEqual(FString::Printf(TEXT("Ts %u block byte size"), Ts),
						BlockBytes, static_cast<int64>(ExpectedCount) * static_cast<int64>(RecordSize));

					Reader->Seek(static_cast<int64>(Offsets[Ts]));
					for (int32 r = 0; r < ExpectedCount; ++r)
					{
						int32 EntityID = 0;
						double PosX = 0, PosY = 0, PosZ = 0, RotP = 0, RotY = 0, RotR = 0;
						float Speed = 0.f;
						uint8 Bracket = 0, ModeIndex = 0;
						*Reader << EntityID;
						*Reader << PosX; *Reader << PosY; *Reader << PosZ;
						*Reader << RotP; *Reader << RotY; *Reader << RotR;
						*Reader << Speed;
						*Reader << Bracket;
						*Reader << ModeIndex;

						const FSimMovementSample& E = (*Expected)[r];
						const FString Where = FString::Printf(TEXT("Ts %u rec %d"), Ts, r);
						TestEqual(Where + TEXT(" EntityID"), EntityID, E.EntityID);
						TestEqual(Where + TEXT(" Pos.X"), PosX, E.Position.X);
						TestEqual(Where + TEXT(" Pos.Y"), PosY, E.Position.Y);
						TestEqual(Where + TEXT(" Pos.Z"), PosZ, E.Position.Z);
						TestEqual(Where + TEXT(" Rot.Pitch"), RotP, E.Rotation.Pitch);
						TestEqual(Where + TEXT(" Rot.Yaw"), RotY, E.Rotation.Yaw);
						TestEqual(Where + TEXT(" Rot.Roll"), RotR, E.Rotation.Roll);
						TestEqual(Where + TEXT(" Speed"), Speed, E.Speed);
						TestEqual(Where + TEXT(" Bracket"), static_cast<int32>(Bracket), static_cast<int32>(E.MovementBracket));
						TestEqual(Where + TEXT(" ModeIndex"), static_cast<int32>(ModeIndex), static_cast<int32>(E.ModeIndex));
					}
				}

				const int64 EndOffset = Offsets.Num() > 0 ? static_cast<int64>(Offsets.Last()) : 0;
				TestEqual(TEXT("File total size == offset-table end"), Reader->TotalSize(), EndOffset);
			}
		}
		else
		{
			AddError(FString::Printf(TEXT("Could not open written cache for reading: %s"), *CacheFilePath));
		}
	}

	// --- Cleanup ---
	FileManager.Delete(*CacheFilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	FileManager.Delete(*FakeSource, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	FileManager.DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);

	return true;
}

// ---------------------------------------------------------------------------------------------------
// Reuse-on-re-import: WriteCacheForImport must SKIP the rewrite when a valid matching cache already
// exists (A3 pass criterion "reused on re-import"). Proven by corrupting a record byte after the first
// write (header prefix left valid) and asserting a second WriteCacheForImport leaves that byte intact.
// ---------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimDiskCacheReuseTest,
	"ProjectMobius.SimData.DiskCacheReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimDiskCacheReuseTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();

	// Force the import write on regardless of any dev-set cvar, restore afterwards.
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
	const int32 OldCVar = CVar ? CVar->GetInt() : 1;
	if (CVar)
	{
		CVar->Set(1);
	}

	TMap<int32, TArray<FSimMovementSample>> SimulationData = MakeKnownSimData();
	const TArray<FString> ModeTable = { FString() };
	FThreadSafeBool bShouldStop(false);

	const FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusSimCacheTest"));
	FileManager.MakeDirectory(*TempDir, /*Tree*/ true);
	const FString FakeSource = FPaths::Combine(TempDir, TEXT("ReuseSource.json"));
	FFileHelper::SaveStringToFile(TEXT("{\"fake\":\"reuse source\"}"), *FakeSource);

	const uint64 SourceHash = MobiusSimCache::ComputeSourceHash(FakeSource);
	const FString CacheFilePath = MobiusSimCache::MakeCacheFilePath(FakeSource, SourceHash);
	FileManager.Delete(*CacheFilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);

	// First import: writes a fresh cache.
	const bool bWrote1 = MobiusSimCache::WriteCacheForImport(
		FakeSource, SimulationData, 0.3f, 0.1f, ModeTable, bShouldStop);
	TestTrue(TEXT("First WriteCacheForImport wrote/produced a cache"), bWrote1);

	// Corrupt the LAST byte (a record byte; the 24-byte header prefix used by the reuse check stays valid).
	TArray<uint8> Bytes;
	const bool bLoaded = FFileHelper::LoadFileToArray(Bytes, *CacheFilePath);
	TestTrue(TEXT("Loaded written cache bytes"), bLoaded);
	if (bLoaded && Bytes.Num() > 24)
	{
		const int32 LastIdx = Bytes.Num() - 1;
		Bytes[LastIdx] = static_cast<uint8>(Bytes[LastIdx] ^ 0xFF); // sentinel flip
		FFileHelper::SaveArrayToFile(Bytes, *CacheFilePath);

		// Second import of the same source: must detect the valid cache and SKIP the rewrite.
		const bool bWrote2 = MobiusSimCache::WriteCacheForImport(
			FakeSource, SimulationData, 0.3f, 0.1f, ModeTable, bShouldStop);
		TestTrue(TEXT("Second WriteCacheForImport reports cache present"), bWrote2);

		TArray<uint8> Bytes2;
		FFileHelper::LoadFileToArray(Bytes2, *CacheFilePath);
		const bool bSentinelSurvived = Bytes2.Num() == Bytes.Num()
			&& Bytes2.Num() > 0
			&& Bytes2[Bytes2.Num() - 1] == Bytes[LastIdx];
		TestTrue(TEXT("Reuse path skipped the rewrite (sentinel byte intact)"), bSentinelSurvived);
	}

	// Cleanup + restore cvar.
	FileManager.Delete(*CacheFilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	FileManager.Delete(*FakeSource, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	FileManager.DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);
	if (CVar)
	{
		CVar->Set(OldCVar);
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
