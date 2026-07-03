// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// SimCacheCorrectnessTest.cpp
//
// PRD 02 task T3 — cache correctness beyond the A3/A6 round-trip tests. Where
// SimDiskCacheTest re-parses the file with a test-local REFERENCE decoder (locking the byte
// layout), these tests exercise the PRODUCTION read path (ReadCacheHeader / ReadOffsetTable /
// DecodeRecords) and the import-time write wrapper (WriteCacheForImport):
//   - source-change invalidation (hash keys on bytes + mtime; changed source = new cache file)
//   - corruption fallback (bad magic, v1 stamp, garbage offset table, truncation -> clean false)
//   - decode golden equality (production decoders reproduce the written map bit-exactly)
//   - cvar gates (mobius.SimCache.WriteOnImport / FastReload)
//
// Run: MobiusPerf\RunTests.ps1 -Filter "ProjectMobius.SimData."
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "SimData/SimDiskCache.h"

namespace
{
	FString CorrectnessTestDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusSimCacheCorrectnessTest"));
	}

	/** Small known dataset (timestep 1 empty on purpose, values exactly representable). */
	TMap<int32, TArray<FSimMovementSample>> MakeKnownSimData()
	{
		auto MakeSample = [](int32 Id, double Px, double Py, double Pz, double Rp, double Ry, double Rr,
		                     float Speed, uint8 Bracket, uint8 ModeIdx)
		{
			FSimMovementSample S;
			S.EntityID = Id;
			S.Position = FVector(Px, Py, Pz);
			S.Rotation = FRotator(Rp, Ry, Rr);
			S.Speed = Speed;
			S.MovementBracket = static_cast<EPedestrianMovementBracket>(Bracket);
			S.ModeIndex = ModeIdx;
			return S;
		};

		TMap<int32, TArray<FSimMovementSample>> Data;
		TArray<FSimMovementSample> Ts0;
		Ts0.Add(MakeSample(0, 10.5, -20.25, 3.125, 45.0, -90.0, 0.0, 1.5f, 0, 0));
		Ts0.Add(MakeSample(7, -0.5, 0.0, 100.0, -12.5, 180.0, 7.75, 2.25f, 1, 1));
		Data.Add(0, MoveTemp(Ts0));
		Data.Add(1, TArray<FSimMovementSample>());
		TArray<FSimMovementSample> Ts2;
		Ts2.Add(MakeSample(0, 11.0, -19.0, 3.5, 46.0, -89.0, 0.5, 1.625f, 2, 0));
		Data.Add(2, MoveTemp(Ts2));
		return Data;
	}

	TArray<FMobiusAgentEntityData> MakeKnownEntities()
	{
		TArray<FMobiusAgentEntityData> Entities;
		FMobiusAgentEntityData& E0 = Entities.AddDefaulted_GetRef();
		E0.Id = 0; E0.Name = TEXT("agent_zero"); E0.SimTimeS = 0.3f; E0.MaxSpeed = 1.5f; E0.MPlane = TEXT("floor_0"); E0.Map = 2;
		FMobiusAgentEntityData& E7 = Entities.AddDefaulted_GetRef();
		E7.Id = 7; E7.Name = TEXT("agent_seven"); E7.SimTimeS = 0.2f; E7.MaxSpeed = 2.25f; E7.MPlane = TEXT(""); E7.Map = 0;
		return Entities;
	}

	uint64 DoubleBits(double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	uint32 FloatBits(float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	bool SamplesBitEqual(const FSimMovementSample& A, const FSimMovementSample& B)
	{
		return A.EntityID == B.EntityID
			&& DoubleBits(A.Position.X) == DoubleBits(B.Position.X)
			&& DoubleBits(A.Position.Y) == DoubleBits(B.Position.Y)
			&& DoubleBits(A.Position.Z) == DoubleBits(B.Position.Z)
			&& DoubleBits(A.Rotation.Pitch) == DoubleBits(B.Rotation.Pitch)
			&& DoubleBits(A.Rotation.Yaw) == DoubleBits(B.Rotation.Yaw)
			&& DoubleBits(A.Rotation.Roll) == DoubleBits(B.Rotation.Roll)
			&& FloatBits(A.Speed) == FloatBits(B.Speed)
			&& A.MovementBracket == B.MovementBracket
			&& A.ModeIndex == B.ModeIndex;
	}

	/** Writes a fresh source file + a valid v2 cache for it; returns both paths. */
	bool WriteValidSourceAndCache(
		IFileManager& FileManager,
		const FString& SourceName,
		FString& OutSourcePath,
		FString& OutCachePath)
	{
		const FString Dir = CorrectnessTestDir();
		FileManager.MakeDirectory(*Dir, /*Tree*/ true);
		OutSourcePath = FPaths::Combine(Dir, SourceName);
		if (!FFileHelper::SaveStringToFile(TEXT("{\"fake\":\"cache correctness source\"}"), *OutSourcePath))
		{
			return false;
		}

		const uint64 Hash = MobiusSimCache::ComputeSourceHash(OutSourcePath);
		OutCachePath = MobiusSimCache::MakeCacheFilePath(OutSourcePath, Hash);
		FileManager.Delete(*OutCachePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);

		const TMap<int32, TArray<FSimMovementSample>> Data = MakeKnownSimData();
		const TArray<FString> ModeTable = { FString(TEXT("")), FString(TEXT("walk")) };
		FThreadSafeBool bShouldStop(false);
		return MobiusSimCache::WriteCacheFile(
			OutCachePath, Hash, Data, /*MaxTime*/ 0.3f, /*TimeBetweenSteps*/ 0.1f, ModeTable,
			/*MaxAgents*/ 2, /*SourceFormat*/ 1, MakeKnownEntities(), bShouldStop);
	}

	/** Patch Count bytes at Offset in an existing file (read-modify-write; small test files). */
	bool PatchFileBytes(IFileManager& FileManager, const FString& Path, int64 Offset, const TArray<uint8>& Patch)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Offset + Patch.Num() > Bytes.Num())
		{
			return false;
		}
		FMemory::Memcpy(Bytes.GetData() + Offset, Patch.GetData(), Patch.Num());
		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}

	bool TruncateFile(IFileManager& FileManager, const FString& Path, int64 NewSize)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || NewSize >= Bytes.Num())
		{
			return false;
		}
		Bytes.SetNum(static_cast<int32>(NewSize));
		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}
}

// --- Source-change invalidation -------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCacheSourceInvalidationTest,
	"ProjectMobius.SimData.CacheSourceInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimCacheSourceInvalidationTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Dir = CorrectnessTestDir();
	FileManager.MakeDirectory(*Dir, /*Tree*/ true);
	const FString SourcePath = FPaths::Combine(Dir, TEXT("InvalidationSource.json"));

	IConsoleVariable* WriteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
	const int32 SavedWrite = WriteCVar ? WriteCVar->GetInt() : 1;
	if (WriteCVar) { WriteCVar->Set(1, ECVF_SetByCode); }

	const TMap<int32, TArray<FSimMovementSample>> Data = MakeKnownSimData();
	const TArray<FString> ModeTable = { FString(TEXT("")), FString(TEXT("walk")) };
	const TArray<FMobiusAgentEntityData> Entities = MakeKnownEntities();
	FThreadSafeBool bShouldStop(false);

	// Initial import-time write.
	TestTrue(TEXT("write source v1"), FFileHelper::SaveStringToFile(TEXT("{\"agents\":1}"), *SourcePath));
	const uint64 Hash1 = MobiusSimCache::ComputeSourceHash(SourcePath);
	const FString Cache1 = MobiusSimCache::MakeCacheFilePath(SourcePath, Hash1);
	FileManager.Delete(*Cache1, false, true);
	TestTrue(TEXT("WriteCacheForImport writes for new source"),
		MobiusSimCache::WriteCacheForImport(SourcePath, Data, 0.3f, 0.1f, ModeTable, 2, 1, Entities, bShouldStop));
	TestTrue(TEXT("cache file exists"), FileManager.FileExists(*Cache1));

	// Changed source bytes -> different hash -> different cache path -> fresh write.
	TestTrue(TEXT("write source v2"), FFileHelper::SaveStringToFile(TEXT("{\"agents\":2}"), *SourcePath));
	const uint64 Hash2 = MobiusSimCache::ComputeSourceHash(SourcePath);
	TestTrue(TEXT("changed bytes change the hash"), Hash1 != Hash2);
	const FString Cache2 = MobiusSimCache::MakeCacheFilePath(SourcePath, Hash2);
	TestTrue(TEXT("changed hash changes the cache path"), Cache1 != Cache2);
	FileManager.Delete(*Cache2, false, true);
	TestTrue(TEXT("WriteCacheForImport writes for changed source"),
		MobiusSimCache::WriteCacheForImport(SourcePath, Data, 0.3f, 0.1f, ModeTable, 2, 1, Entities, bShouldStop));
	TestTrue(TEXT("new cache file exists"), FileManager.FileExists(*Cache2));
	TestTrue(TEXT("old cache file still present (stale caches are not deleted)"), FileManager.FileExists(*Cache1));

	// Reuse: same source again -> WriteCacheForImport reports success without needing a fresh
	// write (the reuse-skip path); the cache file's timestamp must be unchanged.
	const FDateTime StampBefore = FileManager.GetTimeStamp(*Cache2);
	TestTrue(TEXT("WriteCacheForImport reuses a valid cache"),
		MobiusSimCache::WriteCacheForImport(SourcePath, Data, 0.3f, 0.1f, ModeTable, 2, 1, Entities, bShouldStop));
	TestTrue(TEXT("reuse does not rewrite the file"), FileManager.GetTimeStamp(*Cache2) == StampBefore);

	// mtime-only change with identical bytes: the fingerprint folds in the modification time, so
	// the hash changes (documented behaviour - a false MISS is safe, it just recreates the cache).
	FileManager.SetTimeStamp(*SourcePath, FDateTime::Now() + FTimespan::FromMinutes(5));
	const uint64 Hash3 = MobiusSimCache::ComputeSourceHash(SourcePath);
	TestTrue(TEXT("mtime-only touch changes the hash (documented: safe false-miss)"), Hash3 != Hash2);

	if (WriteCVar) { WriteCVar->Set(SavedWrite, ECVF_SetByCode); }
	return true;
}

// --- Corruption fallback ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCacheCorruptionFallbackTest,
	"ProjectMobius.SimData.CacheCorruptionFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimCacheCorruptionFallbackTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();

	// Case 1: flipped magic -> ReadCacheHeader rejects.
	{
		FString SourcePath, CachePath;
		TestTrue(TEXT("magic: baseline cache written"), WriteValidSourceAndCache(FileManager, TEXT("CorruptMagic.json"), SourcePath, CachePath));
		TestTrue(TEXT("magic: patched"), PatchFileBytes(FileManager, CachePath, 0, { 0xDE, 0xAD }));
		TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
		MobiusSimCache::FMscHeader Header;
		TestTrue(TEXT("magic: reader opened"), Reader.IsValid());
		if (Reader)
		{
			TestFalse(TEXT("magic: corrupt header rejected"), MobiusSimCache::ReadCacheHeader(*Reader, Header));
		}
	}

	// Case 2: v1 version stamp -> rejected, and WriteCacheForImport self-heals with a v2 rewrite.
	{
		FString SourcePath, CachePath;
		TestTrue(TEXT("v1: baseline cache written"), WriteValidSourceAndCache(FileManager, TEXT("CorruptVersion.json"), SourcePath, CachePath));
		const uint32 OldVersion = 1u;
		TArray<uint8> VersionPatch;
		VersionPatch.SetNumUninitialized(sizeof(OldVersion));
		FMemory::Memcpy(VersionPatch.GetData(), &OldVersion, sizeof(OldVersion));
		TestTrue(TEXT("v1: patched"), PatchFileBytes(FileManager, CachePath, /*Version field offset*/ 4, VersionPatch));

		{
			TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
			MobiusSimCache::FMscHeader Header;
			TestTrue(TEXT("v1: reader opened"), Reader.IsValid());
			if (Reader)
			{
				TestFalse(TEXT("v1: stale version rejected"), MobiusSimCache::ReadCacheHeader(*Reader, Header));
			}
		}

		IConsoleVariable* WriteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
		const int32 SavedWrite = WriteCVar ? WriteCVar->GetInt() : 1;
		if (WriteCVar) { WriteCVar->Set(1, ECVF_SetByCode); }
		FThreadSafeBool bShouldStop(false);
		const TArray<FString> ModeTable = { FString(TEXT("")), FString(TEXT("walk")) };
		TestTrue(TEXT("v1: WriteCacheForImport rewrites a stale cache"),
			MobiusSimCache::WriteCacheForImport(SourcePath, MakeKnownSimData(), 0.3f, 0.1f, ModeTable, 2, 1, MakeKnownEntities(), bShouldStop));
		if (WriteCVar) { WriteCVar->Set(SavedWrite, ECVF_SetByCode); }

		TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
		MobiusSimCache::FMscHeader Header;
		TestTrue(TEXT("v1: reader re-opened"), Reader.IsValid());
		if (Reader)
		{
			TestTrue(TEXT("v1: rewritten cache parses"), MobiusSimCache::ReadCacheHeader(*Reader, Header));
			TestEqual(TEXT("v1: rewritten as current version"), static_cast<int32>(Header.Version), static_cast<int32>(MobiusSimCache::Version));
		}
	}

	// Case 3: garbage in the offset table -> ReadOffsetTable rejects (monotonicity/fit checks).
	{
		FString SourcePath, CachePath;
		TestTrue(TEXT("offsets: baseline cache written"), WriteValidSourceAndCache(FileManager, TEXT("CorruptOffsets.json"), SourcePath, CachePath));
		int64 OffsetTableStart = 0;
		{
			TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
			MobiusSimCache::FMscHeader Header;
			TestTrue(TEXT("offsets: valid header before corruption"), Reader && MobiusSimCache::ReadCacheHeader(*Reader, Header));
			OffsetTableStart = Header.OffsetTableStart;
		}
		TestTrue(TEXT("offsets: patched"), PatchFileBytes(FileManager, CachePath, OffsetTableStart + 8,
			{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }));

		TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
		MobiusSimCache::FMscHeader Header;
		TestTrue(TEXT("offsets: header still parses"), Reader && MobiusSimCache::ReadCacheHeader(*Reader, Header));
		if (Reader)
		{
			TArray<uint64> Offsets;
			TestFalse(TEXT("offsets: corrupt table rejected"), MobiusSimCache::ReadOffsetTable(*Reader, Header, Offsets));
		}
	}

	// Case 4: truncation inside the offset table -> clean failure, no crash.
	{
		FString SourcePath, CachePath;
		TestTrue(TEXT("truncated: baseline cache written"), WriteValidSourceAndCache(FileManager, TEXT("CorruptTruncated.json"), SourcePath, CachePath));
		int64 OffsetTableStart = 0;
		{
			TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
			MobiusSimCache::FMscHeader Header;
			TestTrue(TEXT("truncated: valid header before corruption"), Reader && MobiusSimCache::ReadCacheHeader(*Reader, Header));
			OffsetTableStart = Header.OffsetTableStart;
		}
		TestTrue(TEXT("truncated: cut mid-offset-table"), TruncateFile(FileManager, CachePath, OffsetTableStart + 4));

		TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
		MobiusSimCache::FMscHeader Header;
		if (Reader && MobiusSimCache::ReadCacheHeader(*Reader, Header))
		{
			TArray<uint64> Offsets;
			TestFalse(TEXT("truncated: offset table read fails cleanly"), MobiusSimCache::ReadOffsetTable(*Reader, Header, Offsets));
		}
		// A header failure is also an acceptable clean rejection; the assertion is "no crash,
		// no success" either way.
	}

	return true;
}

// --- Production-decoder golden equality --------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCacheDecodeGoldenEqualityTest,
	"ProjectMobius.SimData.CacheDecodeGoldenEquality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimCacheDecodeGoldenEqualityTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	FString SourcePath, CachePath;
	TestTrue(TEXT("baseline cache written"), WriteValidSourceAndCache(FileManager, TEXT("GoldenDecode.json"), SourcePath, CachePath));

	const TMap<int32, TArray<FSimMovementSample>> Expected = MakeKnownSimData();
	const TArray<FMobiusAgentEntityData> ExpectedEntities = MakeKnownEntities();

	TUniquePtr<FArchive> Reader{ FileManager.CreateFileReader(*CachePath) };
	TestTrue(TEXT("reader opened"), Reader.IsValid());
	if (!Reader)
	{
		return false;
	}

	MobiusSimCache::FMscHeader Header;
	TestTrue(TEXT("production ReadCacheHeader"), MobiusSimCache::ReadCacheHeader(*Reader, Header));
	TestEqual(TEXT("NumTimesteps"), static_cast<int32>(Header.NumTimesteps), Expected.Num());
	TestTrue(TEXT("MaxTime bits"), FloatBits(Header.MaxTime) == FloatBits(0.3f));
	TestTrue(TEXT("TimeBetweenSteps bits"), FloatBits(Header.TimeBetweenSteps) == FloatBits(0.1f));
	TestEqual(TEXT("MaxAgents"), Header.MaxAgents, 2);
	TestEqual(TEXT("SourceFormat"), static_cast<int32>(Header.SourceFormat), 1);
	TestEqual(TEXT("ModeTable size"), Header.ModeTable.Num(), 2);
	TestEqual(TEXT("entity count"), Header.Entities.Num(), ExpectedEntities.Num());
	for (int32 i = 0; i < Header.Entities.Num() && i < ExpectedEntities.Num(); ++i)
	{
		const FMobiusAgentEntityData& Read = Header.Entities[i];
		const FMobiusAgentEntityData& Want = ExpectedEntities[i];
		const FString Where = FString::Printf(TEXT("entity[%d]"), i);
		TestEqual(Where + TEXT(".Id"), Read.Id, Want.Id);
		TestTrue(Where + TEXT(".Name"), Read.Name.Equals(Want.Name, ESearchCase::CaseSensitive));
		TestTrue(Where + TEXT(".SimTimeS bits"), FloatBits(Read.SimTimeS) == FloatBits(Want.SimTimeS));
		TestTrue(Where + TEXT(".MaxSpeed bits"), FloatBits(Read.MaxSpeed) == FloatBits(Want.MaxSpeed));
		TestTrue(Where + TEXT(".MPlane"), Read.MPlane.Equals(Want.MPlane, ESearchCase::CaseSensitive));
		TestEqual(Where + TEXT(".Map"), Read.Map, Want.Map);
	}

	TArray<uint64> Offsets;
	TestTrue(TEXT("production ReadOffsetTable"), MobiusSimCache::ReadOffsetTable(*Reader, Header, Offsets));
	TestEqual(TEXT("offset count"), Offsets.Num(), static_cast<int32>(Header.NumTimesteps) + 1);

	for (uint32 Ts = 0; Ts < Header.NumTimesteps && Ts + 1 < static_cast<uint32>(Offsets.Num()); ++Ts)
	{
		const int32 Count = static_cast<int32>((Offsets[Ts + 1] - Offsets[Ts]) / MobiusSimCache::RecordSize);
		TArray<FSimMovementSample> Decoded;
		TestTrue(FString::Printf(TEXT("production DecodeRecords ts %u"), Ts),
			MobiusSimCache::DecodeRecords(*Reader, static_cast<int64>(Offsets[Ts]), Count, Decoded));

		const TArray<FSimMovementSample>* Want = Expected.Find(static_cast<int32>(Ts));
		TestEqual(FString::Printf(TEXT("ts %u sample count"), Ts), Decoded.Num(), Want ? Want->Num() : 0);
		if (Want && Decoded.Num() == Want->Num())
		{
			for (int32 i = 0; i < Decoded.Num(); ++i)
			{
				if (!SamplesBitEqual(Decoded[i], (*Want)[i]))
				{
					AddError(FString::Printf(TEXT("ts %u sample %d differs from source (bitwise)"), Ts, i));
				}
			}
		}
	}

	return true;
}

// --- Cvar gates --------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCacheCvarGatesTest,
	"ProjectMobius.SimData.CacheCvarGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimCacheCvarGatesTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Dir = CorrectnessTestDir();
	FileManager.MakeDirectory(*Dir, /*Tree*/ true);
	const FString SourcePath = FPaths::Combine(Dir, TEXT("CvarGateSource.json"));
	TestTrue(TEXT("write source"), FFileHelper::SaveStringToFile(TEXT("{\"gate\":true}"), *SourcePath));

	const uint64 Hash = MobiusSimCache::ComputeSourceHash(SourcePath);
	const FString CachePath = MobiusSimCache::MakeCacheFilePath(SourcePath, Hash);
	FileManager.Delete(*CachePath, false, true);

	IConsoleVariable* WriteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
	IConsoleVariable* FastReloadCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.FastReload"));
	TestNotNull(TEXT("WriteOnImport cvar registered"), WriteCVar);
	TestNotNull(TEXT("FastReload cvar registered"), FastReloadCVar);
	const int32 SavedWrite = WriteCVar ? WriteCVar->GetInt() : 1;
	const int32 SavedFastReload = FastReloadCVar ? FastReloadCVar->GetInt() : 1;

	const TArray<FString> ModeTable = { FString(TEXT("")) };
	FThreadSafeBool bShouldStop(false);

	if (WriteCVar)
	{
		WriteCVar->Set(0, ECVF_SetByCode);
		TestFalse(TEXT("WriteOnImport=0 suppresses the cache write"),
			MobiusSimCache::WriteCacheForImport(SourcePath, MakeKnownSimData(), 0.3f, 0.1f, ModeTable, 2, 1, MakeKnownEntities(), bShouldStop));
		TestFalse(TEXT("WriteOnImport=0 leaves no file"), FileManager.FileExists(*CachePath));
		TestFalse(TEXT("IsWriteOnImportEnabled follows the cvar"), MobiusSimCache::IsWriteOnImportEnabled());
		WriteCVar->Set(SavedWrite, ECVF_SetByCode);
	}

	if (FastReloadCVar)
	{
		FastReloadCVar->Set(0, ECVF_SetByCode);
		TestFalse(TEXT("FastReload=0 disables fast reload"), MobiusSimCache::IsFastReloadEnabled());
		FastReloadCVar->Set(1, ECVF_SetByCode);
		TestTrue(TEXT("FastReload=1 enables fast reload"), MobiusSimCache::IsFastReloadEnabled());
		FastReloadCVar->Set(SavedFastReload, ECVF_SetByCode);
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
