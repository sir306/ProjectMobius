// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "SimData/SimDiskCache.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/Crc.h"

// Import-time write is not a tick/latency-sensitive path (it runs once on the load worker thread while the
// loading screen is up), so logging here is fine and is the PRD's sanctioned way to confirm write/reuse.
DEFINE_LOG_CATEGORY_STATIC(LogMobiusSimCache, Log, All);

// The byte layout below assumes 8-byte doubles / 4-byte floats; the field-by-field serialisation makes the
// stream padding-free, but the component types must match the documented record for cross-machine reuse.
static_assert(sizeof(double) == 8 && sizeof(float) == 4, "SimDiskCache record layout assumes 8-byte double / 4-byte float");

namespace
{
	/** mobius.SimCache.WriteOnImport — gate the A3 disk-cache write. Default 1 (on). Read on the worker
	 *  thread via GetValueOnAnyThread(). Set 0 to skip the write (e.g. to avoid the one-time disk cost on
	 *  huge datasets); does not affect playback, which never reads the cache yet (A3). */
	static TAutoConsoleVariable<int32> CVarSimCacheWriteOnImport(
		TEXT("mobius.SimCache.WriteOnImport"),
		1,
		TEXT("If 1 (default), write a post-conversion .msc disk cache after each agent-data import (perf task A3, prerequisite for streaming). 0 disables the write."),
		ECVF_Default);

	/** Read up to MaxBytes from Reader starting at SeekPos and CRC32 them. Returns 0 if nothing read. */
	uint32 CrcChunk(FArchive& Reader, int64 SeekPos, int64 MaxBytes)
	{
		if (MaxBytes <= 0)
		{
			return 0;
		}
		Reader.Seek(SeekPos);
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(static_cast<int32>(MaxBytes));
		Reader.Serialize(Buffer.GetData(), MaxBytes);
		return FCrc::MemCrc32(Buffer.GetData(), static_cast<int32>(MaxBytes));
	}

	/** Read just the fixed prefix (Magic..RecordSize) of an existing cache and confirm it matches this hash
	 *  + format. Cheap reuse check — does not validate records. */
	bool IsValidExistingCache(const FString& CacheFilePath, uint64 ExpectedSourceHash)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*CacheFilePath));
		if (!Reader)
		{
			return false;
		}

		// Magic(4) + Version(4) + SourceHash(8) + NumTimesteps(4) + RecordSize(4) = 24 bytes, all ahead of
		// the variable-length ModeTable so they can be read without parsing the rest.
		if (Reader->TotalSize() < 24)
		{
			return false;
		}

		uint32 Magic = 0, Version = 0, NumTimesteps = 0, RecordSize = 0;
		uint64 SourceHash = 0;
		*Reader << Magic;
		*Reader << Version;
		*Reader << SourceHash;
		*Reader << NumTimesteps;
		*Reader << RecordSize;

		return Magic == MobiusSimCache::Magic
			&& Version == MobiusSimCache::Version
			&& SourceHash == ExpectedSourceHash
			&& RecordSize == MobiusSimCache::RecordSize;
	}
}

namespace MobiusSimCache
{
	FString GetCacheDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusSimCache"));
	}

	uint64 ComputeSourceHash(const FString& SourceFilePath)
	{
		IFileManager& FileManager = IFileManager::Get();

		const int64 FileSize = FileManager.FileSize(*SourceFilePath); // -1 if the file does not exist
		const int64 MTimeTicks = FileManager.GetTimeStamp(*SourceFilePath).GetTicks();

		uint32 CrcHead = 0;
		uint32 CrcTail = 0;
		TUniquePtr<FArchive> Reader(FileManager.CreateFileReader(*SourceFilePath));
		if (Reader)
		{
			const int64 Total = Reader->TotalSize();
			const int64 Chunk = FMath::Min<int64>(64 * 1024, Total);
			CrcHead = CrcChunk(*Reader, 0, Chunk);
			if (Total > Chunk)
			{
				CrcTail = CrcChunk(*Reader, Total - Chunk, Chunk);
			}
		}

		// Mix the four components into one 64-bit fingerprint. Odd multipliers (golden-ratio / splitmix
		// style) spread bits so unrelated files with the same size don't collide on the low bits.
		uint64 Hash = (static_cast<uint64>(CrcHead) | (static_cast<uint64>(CrcTail) << 32));
		Hash ^= static_cast<uint64>(FileSize) * 0x9E3779B97F4A7C15ull;
		Hash ^= static_cast<uint64>(MTimeTicks) * 0xC2B2AE3D27D4EB4Full;
		return Hash;
	}

	FString MakeCacheFilePath(const FString& SourceFilePath, uint64 SourceHash)
	{
		const FString BaseName = FPaths::GetBaseFilename(SourceFilePath);
		const FString FileName = FString::Printf(TEXT("%s_%016llx.msc"), *BaseName, SourceHash);
		return FPaths::Combine(GetCacheDir(), FileName);
	}

	bool IsWriteOnImportEnabled()
	{
		return CVarSimCacheWriteOnImport.GetValueOnAnyThread() != 0;
	}

	bool WriteCacheFile(
		const FString& OutFilePath,
		uint64 SourceHash,
		const TMap<int32, TArray<FSimMovementSample>>& SimulationData,
		float MaxTime,
		float TimeBetweenSteps,
		const TArray<FString>& ModeTable,
		const FThreadSafeBool& bShouldStop)
	{
		IFileManager& FileManager = IFileManager::Get();
		const FString CacheDir = GetCacheDir();
		FileManager.MakeDirectory(*CacheDir, /*Tree*/ true);

		// Timesteps are dense (the import loop Adds indices 0..MaxTimestepIndex contiguously), so NumTimesteps
		// == Num() and we can index by ts in [0, NumTimesteps). Find() handles any gap defensively as 0 samples.
		const uint32 NumTimesteps = static_cast<uint32>(SimulationData.Num());

		// Write to a sibling .tmp then atomically rename, so a crash/abort mid-write never leaves a partial
		// file that the reuse check would accept.
		const FString TmpPath = OutFilePath + TEXT(".tmp");
		TUniquePtr<FArchive> Ar(FileManager.CreateFileWriter(*TmpPath));
		if (!Ar)
		{
			UE_LOG(LogMobiusSimCache, Warning, TEXT("[SimCache] Could not open '%s' for writing"), *TmpPath);
			return false;
		}

		// --- Header ---
		uint32 MagicLocal      = Magic;
		uint32 VersionLocal    = Version;
		uint64 SourceHashLocal = SourceHash;
		uint32 NumTimestepsLocal = NumTimesteps;
		uint32 RecordSizeLocal = RecordSize;
		float  MaxTimeLocal    = MaxTime;
		float  TbsLocal        = TimeBetweenSteps;
		*Ar << MagicLocal;
		*Ar << VersionLocal;
		*Ar << SourceHashLocal;
		*Ar << NumTimestepsLocal;
		*Ar << RecordSizeLocal;
		*Ar << MaxTimeLocal;
		*Ar << TbsLocal;

		uint32 NumModes = static_cast<uint32>(ModeTable.Num());
		*Ar << NumModes;
		for (uint32 i = 0; i < NumModes; ++i)
		{
			FString ModeStr = ModeTable[static_cast<int32>(i)]; // non-const copy: FArchive::operator<< takes FString&
			*Ar << ModeStr;
		}

		// --- Offset table (absolute byte offsets) ---
		// Header ends here; the offset table itself is fixed-size, so the records start immediately after it.
		const int64 OffsetTableStart = Ar->Tell();
		const int64 RecordsStart = OffsetTableStart + static_cast<int64>(NumTimesteps + 1) * static_cast<int64>(sizeof(uint64));

		TArray<uint64> Offsets;
		Offsets.SetNumUninitialized(static_cast<int32>(NumTimesteps) + 1);
		{
			int64 Cursor = RecordsStart;
			for (uint32 Ts = 0; Ts < NumTimesteps; ++Ts)
			{
				Offsets[static_cast<int32>(Ts)] = static_cast<uint64>(Cursor);
				const TArray<FSimMovementSample>* Block = SimulationData.Find(static_cast<int32>(Ts));
				const int64 Count = Block ? Block->Num() : 0;
				Cursor += Count * static_cast<int64>(RecordSize);
			}
			Offsets[static_cast<int32>(NumTimesteps)] = static_cast<uint64>(Cursor);
		}
		for (uint32 i = 0; i <= NumTimesteps; ++i)
		{
			uint64 Off = Offsets[static_cast<int32>(i)];
			*Ar << Off;
		}
		checkf(Ar->Tell() == RecordsStart,
			TEXT("[SimCache] offset table size mismatch (Tell=%lld, expected RecordsStart=%lld)"), Ar->Tell(), RecordsStart);

		// --- Records ---
		bool bWroteWidthCheck = false;
		for (uint32 Ts = 0; Ts < NumTimesteps; ++Ts)
		{
			if (bShouldStop)
			{
				Ar->Close();
				Ar.Reset(); // release the OS handle before deleting the temp
				FileManager.Delete(*TmpPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
				UE_LOG(LogMobiusSimCache, Warning, TEXT("[SimCache] Aborted (bShouldStop) while writing '%s'"), *OutFilePath);
				return false;
			}

			const TArray<FSimMovementSample>* Block = SimulationData.Find(static_cast<int32>(Ts));
			if (!Block)
			{
				continue;
			}

			for (const FSimMovementSample& Sample : *Block)
			{
				const int64 RecordStart = Ar->Tell();

				int32  EntityID = Sample.EntityID;
				double PosX = Sample.Position.X, PosY = Sample.Position.Y, PosZ = Sample.Position.Z;
				double RotP = Sample.Rotation.Pitch, RotY = Sample.Rotation.Yaw, RotR = Sample.Rotation.Roll;
				float  Speed = Sample.Speed;
				uint8  Bracket = static_cast<uint8>(Sample.MovementBracket);
				uint8  ModeIndex = Sample.ModeIndex;

				*Ar << EntityID;
				*Ar << PosX; *Ar << PosY; *Ar << PosZ;
				*Ar << RotP; *Ar << RotY; *Ar << RotR;
				*Ar << Speed;
				*Ar << Bracket;
				*Ar << ModeIndex;

				// Tripwire: the field-by-field write must produce exactly RecordSize bytes, or the offset
				// table (and every A4 seek) is wrong. Cheap to check once.
				if (!bWroteWidthCheck)
				{
					checkf((Ar->Tell() - RecordStart) == static_cast<int64>(RecordSize),
						TEXT("[SimCache] serialised record width %lld != RecordSize %u"), Ar->Tell() - RecordStart, RecordSize);
					bWroteWidthCheck = true;
				}
			}
		}

		const int64 FinalSize = Ar->Tell();
		Ar->Close();
		Ar.Reset();

		checkf(FinalSize == static_cast<int64>(Offsets[static_cast<int32>(NumTimesteps)]),
			TEXT("[SimCache] final size %lld != offset-table end %llu"), FinalSize, Offsets[static_cast<int32>(NumTimesteps)]);

		// Atomic publish.
		if (!FileManager.Move(*OutFilePath, *TmpPath, /*Replace*/ true, /*EvenIfReadOnly*/ true))
		{
			FileManager.Delete(*TmpPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
			UE_LOG(LogMobiusSimCache, Warning, TEXT("[SimCache] Could not move temp into place: '%s'"), *OutFilePath);
			return false;
		}

		UE_LOG(LogMobiusSimCache, Log, TEXT("[SimCache] Wrote '%s' (%u timesteps, %lld bytes)"),
			*OutFilePath, NumTimesteps, FinalSize);
		return true;
	}

	bool WriteCacheForImport(
		const FString& SourceFilePath,
		const TMap<int32, TArray<FSimMovementSample>>& SimulationData,
		float MaxTime,
		float TimeBetweenSteps,
		const TArray<FString>& ModeTable,
		const FThreadSafeBool& bShouldStop)
	{
		if (!IsWriteOnImportEnabled())
		{
			return false;
		}

		const uint64 SourceHash = ComputeSourceHash(SourceFilePath);
		const FString CacheFilePath = MakeCacheFilePath(SourceFilePath, SourceHash);

		if (IsValidExistingCache(CacheFilePath, SourceHash))
		{
			UE_LOG(LogMobiusSimCache, Log, TEXT("[SimCache] Reusing existing cache '%s'"), *CacheFilePath);
			return true;
		}

		return WriteCacheFile(CacheFilePath, SourceHash, SimulationData, MaxTime, TimeBetweenSteps, ModeTable, bShouldStop);
	}
}

// ---------------------------------------------------------------------------------------------------
// Console command: mobius.SimCache.Clear — delete every cached .msc (and any leftover .tmp) so a stale
// or unwanted cache can be discarded without hand-deleting files. (PRD A3 "console command to clear cache dir".)
// ---------------------------------------------------------------------------------------------------
static FAutoConsoleCommand GClearSimCacheCmd(
	TEXT("mobius.SimCache.Clear"),
	TEXT("Delete all cached .msc simulation files in <ProjectSaved>/MobiusSimCache."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		IFileManager& FileManager = IFileManager::Get();
		const FString CacheDir = MobiusSimCache::GetCacheDir();
		if (!FileManager.DirectoryExists(*CacheDir))
		{
			UE_LOG(LogMobiusSimCache, Log, TEXT("[SimCache] Clear: no cache dir at '%s'"), *CacheDir);
			return;
		}

		int32 Removed = 0;
		TArray<FString> Files;
		FileManager.FindFiles(Files, *FPaths::Combine(CacheDir, TEXT("*.msc")), /*Files*/ true, /*Dirs*/ false);
		FileManager.FindFiles(Files, *FPaths::Combine(CacheDir, TEXT("*.tmp")), /*Files*/ true, /*Dirs*/ false);
		for (const FString& File : Files)
		{
			if (FileManager.Delete(*FPaths::Combine(CacheDir, File), /*RequireExists*/ false, /*EvenReadOnly*/ true))
			{
				++Removed;
			}
		}
		UE_LOG(LogMobiusSimCache, Log, TEXT("[SimCache] Clear: removed %d file(s) from '%s'"), Removed, *CacheDir);
	}));
