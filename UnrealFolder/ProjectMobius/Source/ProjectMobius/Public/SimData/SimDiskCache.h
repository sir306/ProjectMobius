// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"                                  // FThreadSafeBool (abort flag, passed from the import runnable)
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h" // FSimMovementSample
#include "MobiusAgentDataImporter.h"                             // FMobiusAgentEntityData (v2 metadata block, A6)

/**
 * On-disk post-conversion movement-sample cache (".msc") — perf task A3.
 *
 * Written once at import, AFTER the importer's full post-processing (unit scaling, Y-invert, yaw
 * (-rot-90), computed rotation/speed, and the smoothed movement brackets). Storing the *post-conversion*
 * records — not the raw source — is what guarantees a future streaming provider (A4/A5) returns values
 * bit-identical to the full-RAM path (Invariant 5): streaming decodes these bytes verbatim, it never
 * re-derives rotation/speed from a window (which couldn't reproduce the importer's global per-entity
 * look-ahead).
 *
 * A3 only WRITES the file (reuse-guarded) and is unused for playback — so it is a pure side effect with
 * zero behavioural change. The reader lives with the streaming provider (A4).
 *
 * ---------------------------------------------------------------------------------------------------
 * FILE FORMAT (this comment is the authoritative A4 read contract — keep it in lockstep with the writer)
 *
 * All scalars are written with FArchive's default little-endian byte order (stable across the shipped
 * Win64 + macOS targets, both little-endian). Records are written field-by-field — NEVER as a memcpy of a
 * C++ struct — so there is no compiler padding in the stream and the reader must decode field-by-field too.
 *
 *   [Header]
 *     uint32  Magic            == MobiusSimCache::Magic ('MSC1')
 *     uint32  Version          == MobiusSimCache::Version
 *     uint64  SourceHash       fingerprint of the source file (size + mtime + CRC of head/tail 64 KB)
 *     uint32  NumTimesteps     number of timestep blocks (dense: indices 0..NumTimesteps-1)
 *     uint32  RecordSize       == MobiusSimCache::RecordSize (per-sample byte width; a self-check)
 *     float   MaxTime          FSimulationFragment::MaxTime
 *     float   TimeBetweenSteps sampling interval (s)
 *     uint32  NumModes         then NumModes x FString (FArchive << FString): the ModeIndex intern table
 *
 *   [Offset table]  (NumTimesteps + 1) x uint64
 *     Absolute file byte offsets. off[ts] = first byte of timestep ts's record block;
 *     off[NumTimesteps] = total file size. O(1) seek: Seek(off[ts]); Read(off[ts+1] - off[ts]).
 *     A timestep with no samples has off[ts+1] == off[ts].
 *
 *   [Records]  grouped by timestep (ascending), in resident-array order within each timestep.
 *     Each record is exactly RecordSize bytes, in this exact field order:
 *       int32   EntityID
 *       double  Position.X, Position.Y, Position.Z      (FVector is double under UE5 LWC)
 *       double  Rotation.Pitch, Rotation.Yaw, Rotation.Roll   (FRotator, this component order)
 *       float   Speed
 *       uint8   MovementBracket   (EPedestrianMovementBracket : uint8)
 *       uint8   ModeIndex         (index into the ModeTable above)
 *     => 4 + 24 + 24 + 4 + 1 + 1 = 58 bytes.
 * ---------------------------------------------------------------------------------------------------
 */
namespace MobiusSimCache
{
	/** File magic 'MSC1' (Mobius Sim Cache). Fixed value; only round-trip consistency matters. */
	static constexpr uint32 Magic = 0x3143534Du; // bytes 'M','S','C','1' little-endian

	/** Format version. Bump on ANY layout change so stale caches are rejected by the reuse check.
	 *  v2 (perf task A6): header gains a metadata block (MaxAgents, source format, per-entity info)
	 *  after the ModeTable, making the cache self-sufficient for the import fast-reload path. v1
	 *  caches are rejected by the reuse check and transparently rewritten on the next import. */
	static constexpr uint32 Version = 2u;

	/** Per-sample on-disk record width in bytes. Must equal the field-by-field layout documented above. */
	static constexpr uint32 RecordSize = 4u /*EntityID*/ + 24u /*Pos*/ + 24u /*Rot*/ + 4u /*Speed*/ + 1u /*Bracket*/ + 1u /*ModeIndex*/;
	static_assert(RecordSize == 58u, "SimDiskCache RecordSize must match the documented field layout");

	/**
	 * Parsed .msc header — everything ahead of the offset table (see the format block above).
	 *
	 * v2 METADATA BLOCK (A6), serialized directly after the ModeTable, in this exact order:
	 *   int32   MaxAgents          spawn count (UAgentDataSubsystem::GetMaxAgents source)
	 *   uint8   SourceFormat       EMobiusAgentFileFormat of the imported source
	 *   uint32  NumEntities        then per entity, field-by-field:
	 *     int32 Id / FString Name / float SimTimeS / float MaxSpeed / FString MPlane / int32 Map
	 *     (mirrors FMobiusAgentEntityData — the data BuildPedestrianMovementFragmentData moves into
	 *      UAgentDataSubsystem::CachedEntityData for InitMOP/entity-info lookups)
	 */
	struct FMscHeader
	{
		uint32 Magic = 0;
		uint32 Version = 0;
		uint64 SourceHash = 0;
		uint32 NumTimesteps = 0;
		uint32 RecordSize = 0;
		float MaxTime = 0.f;
		float TimeBetweenSteps = 0.f;
		TArray<FString> ModeTable;
		// --- v2 metadata block (A6) ---
		int32 MaxAgents = 0;
		uint8 SourceFormat = 0;
		TArray<FMobiusAgentEntityData> Entities;
		/** Absolute byte offset of the (NumTimesteps+1) x uint64 offset table (== first byte after the header). */
		int64 OffsetTableStart = 0;

		/** True when magic/version/record width match this build's writer layout. */
		bool IsCompatible() const
		{
			return Magic == MobiusSimCache::Magic
				&& Version == MobiusSimCache::Version
				&& RecordSize == MobiusSimCache::RecordSize;
		}
	};

	/**
	 * Read + parse the header of an existing cache. Seeks to 0; on success the archive is left positioned
	 * at the start of the offset table (== OutHeader.OffsetTableStart). Returns false on short/corrupt
	 * files or an incompatible layout (parses no further than the fixed prefix in that case). Shared
	 * decoder for the reuse check, the A4 streaming provider, and tests.
	 */
	PROJECTMOBIUS_API bool ReadCacheHeader(FArchive& Reader, FMscHeader& OutHeader);

	/** Cache directory: <ProjectSaved>/MobiusSimCache. */
	PROJECTMOBIUS_API FString GetCacheDir();

	/**
	 * True when the volume holding the cache dir reports a seek penalty (spinning disk). Used by the A5
	 * residency decision to enlarge the streaming window/lookahead where cold reads cost more. Windows
	 * only; other platforms return false (treat as SSD). Failure to query also returns false.
	 */
	PROJECTMOBIUS_API bool CacheDriveHasSeekPenalty();

	/**
	 * Fingerprint of the source agent-data file: combines size + modification time + a CRC of the first
	 * and last 64 KB. Cheap (no full-file read) and good enough to detect "same file, unchanged" for the
	 * reuse check; a fingerprint clash would only cause a stale cache to be reused, which A4's golden-frame
	 * equality test would catch (A3 never reads the file back). Returns a value derived from size=-1 / zero
	 * CRCs when the path is missing/unreadable.
	 *
	 * A4 FORWARD-TRAP: this keys ONLY on the source file bytes, not on anything that affects the conversion.
	 * If a future import setting (unit mode, sampling, rotation/speed derivation, etc.) can change the
	 * post-conversion records WITHOUT the source bytes changing, A4 would serve a stale cache and silently
	 * break Invariant 5. Before A4 reads these files, fold the relevant conversion params into the hash (or
	 * bump Version when they change).
	 */
	PROJECTMOBIUS_API uint64 ComputeSourceHash(const FString& SourceFilePath);

	/** Cache file path for a source file + its hash: <CacheDir>/<SourceBaseName>_<Hash16hex>.msc. */
	PROJECTMOBIUS_API FString MakeCacheFilePath(const FString& SourceFilePath, uint64 SourceHash);

	/** True if the import-time write is enabled (cvar mobius.SimCache.WriteOnImport, default 1). */
	PROJECTMOBIUS_API bool IsWriteOnImportEnabled();

	/** True if the import fast-reload from a valid v2 cache is enabled (cvar mobius.SimCache.FastReload,
	 *  default 1; 0 = always parse the source, perf task A6). */
	PROJECTMOBIUS_API bool IsFastReloadEnabled();

	/**
	 * Core writer: serialise SimulationData to OutFilePath in the format above (writing atomically via a
	 * .tmp + rename). Bypasses the cvar and the reuse check — used directly by tests. Honours bShouldStop
	 * (aborts and removes the partial .tmp). Returns true on a complete write.
	 */
	PROJECTMOBIUS_API bool WriteCacheFile(
		const FString& OutFilePath,
		uint64 SourceHash,
		const TMap<int32, TArray<FSimMovementSample>>& SimulationData,
		float MaxTime,
		float TimeBetweenSteps,
		const TArray<FString>& ModeTable,
		int32 MaxAgents,
		uint8 SourceFormat,
		const TArray<FMobiusAgentEntityData>& Entities,
		const FThreadSafeBool& bShouldStop);

	/**
	 * Import-time entry point: cvar-gated, reuse-guarded write. Computes the source hash, and if a valid
	 * matching cache already exists, logs and skips (returns true). Otherwise writes it. Called on the
	 * import worker thread from FProcessAgentSimulationDataRunnable::FinalizeProgress, after the movement
	 * brackets are finalised and BEFORE bIsDataLoaded is set (the data must not be moved out mid-write).
	 * Returns true if a valid cache is present after the call (written or reused).
	 */
	PROJECTMOBIUS_API bool WriteCacheForImport(
		const FString& SourceFilePath,
		const TMap<int32, TArray<FSimMovementSample>>& SimulationData,
		float MaxTime,
		float TimeBetweenSteps,
		const TArray<FString>& ModeTable,
		int32 MaxAgents,
		uint8 SourceFormat,
		const TArray<FMobiusAgentEntityData>& Entities,
		const FThreadSafeBool& bShouldStop);

	/**
	 * Read the (NumTimesteps + 1) offset table. The archive must be positioned at
	 * Header.OffsetTableStart (where ReadCacheHeader leaves it). Validates monotonicity,
	 * record alignment and that the final offset fits the file. Shared by the A4 streaming
	 * provider and the A6 fast-reload path.
	 */
	PROJECTMOBIUS_API bool ReadOffsetTable(FArchive& Reader, const FMscHeader& Header, TArray<uint64>& OutOffsets);

	/**
	 * Decode Count records of the documented field layout starting at ByteOffset into Out
	 * (field-by-field — never a struct memcpy). Any archive position on entry; safe on any thread
	 * with a private archive.
	 */
	PROJECTMOBIUS_API bool DecodeRecords(FArchive& Reader, int64 ByteOffset, int32 Count, TArray<FSimMovementSample>& Out);
}
