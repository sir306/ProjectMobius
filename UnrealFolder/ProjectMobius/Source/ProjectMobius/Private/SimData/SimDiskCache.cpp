// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "SimData/SimDiskCache.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/Crc.h"

#if PLATFORM_WINDOWS
// Raw volume handle + IOCTL_STORAGE_QUERY_PROPERTY for the seek-penalty query (CacheDriveHasSeekPenalty).
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winioctl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

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

	/** mobius.SimCache.FastReload — A6: when a valid v2 cache exists for the selected source file, the
	 *  import runnable decodes it directly and skips the JSON/HDF5 parse entirely (measured 22 s JSON
	 *  parse -> ~1-2 s binary decode). Read on the import worker thread. 0 = always full import. */
	static TAutoConsoleVariable<int32> CVarSimCacheFastReload(
		TEXT("mobius.SimCache.FastReload"),
		1,
		TEXT("If 1 (default), re-opening an agent file with a valid .msc cache skips the source parse and loads from the cache (perf task A6). 0 forces the full import."),
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

	/** Parse the header of an existing cache and confirm it matches this hash + format. Cheap reuse
	 *  check — does not validate records. */
	bool IsValidExistingCache(const FString& CacheFilePath, uint64 ExpectedSourceHash)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*CacheFilePath));
		if (!Reader)
		{
			return false;
		}

		MobiusSimCache::FMscHeader Header;
		return MobiusSimCache::ReadCacheHeader(*Reader, Header)
			&& Header.SourceHash == ExpectedSourceHash;
	}
}

namespace MobiusSimCache
{
	bool ReadCacheHeader(FArchive& Reader, FMscHeader& OutHeader)
	{
		// Fixed prefix Magic..RecordSize = 24 bytes, then MaxTime(4) + TimeBetweenSteps(4) + NumModes(4).
		if (Reader.TotalSize() < 36)
		{
			return false;
		}

		Reader.Seek(0);
		Reader << OutHeader.Magic;
		Reader << OutHeader.Version;
		Reader << OutHeader.SourceHash;
		Reader << OutHeader.NumTimesteps;
		Reader << OutHeader.RecordSize;

		// Stop at the fixed prefix on a foreign/stale layout — the rest cannot be decoded safely.
		if (!OutHeader.IsCompatible())
		{
			return false;
		}

		Reader << OutHeader.MaxTime;
		Reader << OutHeader.TimeBetweenSteps;

		uint32 NumModes = 0;
		Reader << NumModes;
		// Sanity bound: the table is a small intern list (today effectively { "" }); a huge count means a
		// corrupt file, and looping on it would read garbage FStrings.
		if (NumModes > 4096u)
		{
			return false;
		}
		OutHeader.ModeTable.Reset();
		OutHeader.ModeTable.Reserve(static_cast<int32>(NumModes));
		for (uint32 i = 0; i < NumModes; ++i)
		{
			FString Mode;
			Reader << Mode;
			OutHeader.ModeTable.Add(MoveTemp(Mode));
		}

		// --- v2 metadata block (A6): MaxAgents, source format, per-entity info. Field order is the
		// header-doc contract; keep in lockstep with the writer below. ---
		Reader << OutHeader.MaxAgents;
		Reader << OutHeader.SourceFormat;
		uint32 NumEntities = 0;
		Reader << NumEntities;
		// Sanity bound: agents number in the thousands; a huge count means corruption.
		if (NumEntities > 10'000'000u)
		{
			return false;
		}
		OutHeader.Entities.Reset();
		OutHeader.Entities.Reserve(static_cast<int32>(NumEntities));
		for (uint32 i = 0; i < NumEntities; ++i)
		{
			FMobiusAgentEntityData& Entity = OutHeader.Entities.AddDefaulted_GetRef();
			Reader << Entity.Id;
			Reader << Entity.Name;
			Reader << Entity.SimTimeS;
			Reader << Entity.MaxSpeed;
			Reader << Entity.MPlane;
			Reader << Entity.Map;
		}

		if (Reader.IsError())
		{
			return false;
		}

		OutHeader.OffsetTableStart = Reader.Tell();

		// The offset table ((NumTimesteps+1) x uint64) must actually fit in the file.
		const int64 OffsetTableBytes = (static_cast<int64>(OutHeader.NumTimesteps) + 1) * static_cast<int64>(sizeof(uint64));
		return OutHeader.OffsetTableStart + OffsetTableBytes <= Reader.TotalSize();
	}

	FString GetCacheDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusSimCache"));
	}

	bool CacheDriveHasSeekPenalty()
	{
#if PLATFORM_WINDOWS
		// Volume root of the cache dir (e.g. "E:"), queried via the seek-penalty storage property.
		const FString FullPath = FPaths::ConvertRelativePathToFull(GetCacheDir());
		if (FullPath.Len() < 2 || FullPath[1] != TEXT(':'))
		{
			return false; // UNC/odd path — assume no penalty
		}
		const FString VolumePath = FString::Printf(TEXT("\\\\.\\%c:"), FullPath[0]);

		const HANDLE Volume = CreateFileW(*VolumePath, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (Volume == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		STORAGE_PROPERTY_QUERY Query = {};
		Query.PropertyId = StorageDeviceSeekPenaltyProperty;
		Query.QueryType = PropertyStandardQuery;
		DEVICE_SEEK_PENALTY_DESCRIPTOR Descriptor = {};
		DWORD BytesReturned = 0;
		const bool bOk = DeviceIoControl(Volume, IOCTL_STORAGE_QUERY_PROPERTY,
			&Query, sizeof(Query), &Descriptor, sizeof(Descriptor), &BytesReturned, nullptr) != 0;
		CloseHandle(Volume);

		return bOk && Descriptor.IncursSeekPenalty;
#else
		return false;
#endif
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

	bool IsFastReloadEnabled()
	{
		return CVarSimCacheFastReload.GetValueOnAnyThread() != 0;
	}

	bool WriteCacheFile(
		const FString& OutFilePath,
		uint64 SourceHash,
		const TMap<int32, TArray<FSimMovementSample>>& SimulationData,
		float MaxTime,
		float TimeBetweenSteps,
		const TArray<FString>& ModeTable,
		int32 MaxAgents,
		uint8 SourceFormat,
		const TArray<FMobiusAgentEntityData>& Entities,
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

		// --- v2 metadata block (A6). Field order per the FMscHeader doc; non-const locals because
		// FArchive::operator<< takes references. ---
		int32 MaxAgentsLocal = MaxAgents;
		uint8 SourceFormatLocal = SourceFormat;
		uint32 NumEntities = static_cast<uint32>(Entities.Num());
		*Ar << MaxAgentsLocal;
		*Ar << SourceFormatLocal;
		*Ar << NumEntities;
		for (const FMobiusAgentEntityData& Entity : Entities)
		{
			int32 Id = Entity.Id;
			FString Name = Entity.Name;
			float SimTimeS = Entity.SimTimeS;
			float MaxSpeed = Entity.MaxSpeed;
			FString MPlane = Entity.MPlane;
			int32 Map = Entity.Map;
			*Ar << Id;
			*Ar << Name;
			*Ar << SimTimeS;
			*Ar << MaxSpeed;
			*Ar << MPlane;
			*Ar << Map;
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
		int32 MaxAgents,
		uint8 SourceFormat,
		const TArray<FMobiusAgentEntityData>& Entities,
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

		return WriteCacheFile(CacheFilePath, SourceHash, SimulationData, MaxTime, TimeBetweenSteps, ModeTable,
			MaxAgents, SourceFormat, Entities, bShouldStop);
	}

	bool ReadOffsetTable(FArchive& Reader, const FMscHeader& Header, TArray<uint64>& OutOffsets)
	{
		const int32 NumOffsets = static_cast<int32>(Header.NumTimesteps) + 1;
		OutOffsets.SetNumUninitialized(NumOffsets);
		for (int32 i = 0; i < NumOffsets; ++i)
		{
			Reader << OutOffsets[i];
		}
		if (Reader.IsError() || static_cast<int64>(OutOffsets.Last()) > Reader.TotalSize())
		{
			return false;
		}
		for (int32 i = 0; i + 1 < NumOffsets; ++i)
		{
			const uint64 BlockBytes = OutOffsets[i + 1] - OutOffsets[i];
			if (OutOffsets[i + 1] < OutOffsets[i] || (BlockBytes % Header.RecordSize) != 0)
			{
				return false;
			}
		}
		return true;
	}

	bool DecodeRecords(FArchive& Reader, int64 ByteOffset, int32 Count, TArray<FSimMovementSample>& Out)
	{
		// Field-by-field decode in the exact writer order (format block in SimDiskCache.h) — never a
		// struct memcpy; the stream is padding-free and FSimMovementSample is not.
		Out.Reset(Count);
		Reader.Seek(ByteOffset);
		for (int32 i = 0; i < Count; ++i)
		{
			int32 EntityID = 0;
			double PosX = 0, PosY = 0, PosZ = 0, RotPitch = 0, RotYaw = 0, RotRoll = 0;
			float Speed = 0.f;
			uint8 Bracket = 0, ModeIndex = 0;
			Reader << EntityID;
			Reader << PosX; Reader << PosY; Reader << PosZ;
			Reader << RotPitch; Reader << RotYaw; Reader << RotRoll;
			Reader << Speed;
			Reader << Bracket;
			Reader << ModeIndex;

			FSimMovementSample& Sample = Out.AddDefaulted_GetRef();
			Sample.EntityID = EntityID;
			Sample.Position = FVector(PosX, PosY, PosZ);
			Sample.Rotation = FRotator(RotPitch, RotYaw, RotRoll);
			Sample.Speed = Speed;
			Sample.MovementBracket = static_cast<EPedestrianMovementBracket>(Bracket);
			Sample.ModeIndex = ModeIndex;
		}
		return !Reader.IsError();
	}

	// ONE definition of "what the cache is on disk", shared by the size readout and the clear operation.
	// They must stay in lockstep: if the size counted a file Clear does not delete, the panel would show a
	// non-zero cache that the button cannot clear, and the user would press it repeatedly for no effect.
	// Names are prefixed rather than bare (FindCacheFiles, CacheFileGlobs) because internal-linkage symbols
	// in a named namespace still collide across a unity-build blob.
	static const TCHAR* const SimCacheFileGlobs[] = { TEXT("*.msc"), TEXT("*.tmp") };

	/** Full paths of every cache artefact in GetCacheDir(). Empty when the directory does not exist. */
	static void FindSimCacheFiles(TArray<FString>& OutFullPaths)
	{
		IFileManager& FileManager = IFileManager::Get();
		const FString CacheDir = GetCacheDir();
		OutFullPaths.Reset();

		if (!FileManager.DirectoryExists(*CacheDir))
		{
			return;
		}

		for (const TCHAR* Glob : SimCacheFileGlobs)
		{
			TArray<FString> Names; // FindFiles returns leaf names, not paths — combine before use.
			FileManager.FindFiles(Names, *FPaths::Combine(CacheDir, Glob), /*Files*/ true, /*Dirs*/ false);
			for (const FString& Name : Names)
			{
				OutFullPaths.Add(FPaths::Combine(CacheDir, Name));
			}
		}
	}

	int64 GetCacheSizeOnDisk(int32* OutFileCount)
	{
		IFileManager& FileManager = IFileManager::Get();

		TArray<FString> Files;
		FindSimCacheFiles(Files);

		int64 TotalBytes = 0;
		int32 Count = 0;
		for (const FString& File : Files)
		{
			// FileSize returns -1 for anything that disappeared between the listing and the stat — an
			// import worker completing its .tmp -> .msc rename mid-walk is the realistic case. Skipping
			// keeps the total honest; accumulating -1 would silently understate the cache.
			const int64 Size = FileManager.FileSize(*File);
			if (Size > 0)
			{
				TotalBytes += Size;
				++Count;
			}
		}

		if (OutFileCount != nullptr)
		{
			*OutFileCount = Count;
		}
		return TotalBytes;
	}

	int32 ClearCache()
	{
		IFileManager& FileManager = IFileManager::Get();

		TArray<FString> Files;
		FindSimCacheFiles(Files);
		if (Files.Num() == 0)
		{
			UE_LOG(LogMobiusSimCache, Log, TEXT("[SimCache] Clear: nothing to remove in '%s'"), *GetCacheDir());
			return 0;
		}

		int32 Removed = 0;
		for (const FString& File : Files)
		{
			if (FileManager.Delete(*File, /*RequireExists*/ false, /*EvenReadOnly*/ true))
			{
				++Removed;
			}
		}
		UE_LOG(LogMobiusSimCache, Log, TEXT("[SimCache] Clear: removed %d file(s) from '%s'"), Removed, *GetCacheDir());
		return Removed;
	}
}

// ---------------------------------------------------------------------------------------------------
// Console command: mobius.SimCache.Clear — delete every cached .msc (and any leftover .tmp) so a stale
// or unwanted cache can be discarded without hand-deleting files. (PRD A3 "console command to clear cache dir".)
// The body lives in MobiusSimCache::ClearCache so this command and the S14 settings-panel button share one
// implementation; before S14 this logic existed only here and was unreachable from the UI.
// ---------------------------------------------------------------------------------------------------
static FAutoConsoleCommand GClearSimCacheCmd(
	TEXT("mobius.SimCache.Clear"),
	TEXT("Delete all cached .msc simulation files in <ProjectSaved>/MobiusSimCache."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		MobiusSimCache::ClearCache();
	}));
