// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"                                  // FThreadSafeBool (abort flag, passed from the import runnable)
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h" // FSimMovementSample

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
	/** File magic 'MSC1' (Mobius Sim Cache v1). Fixed value; only round-trip consistency matters. */
	static constexpr uint32 Magic = 0x3143534Du; // bytes 'M','S','C','1' little-endian

	/** Format version. Bump on ANY layout change so stale caches are rejected by the reuse check. */
	static constexpr uint32 Version = 1u;

	/** Per-sample on-disk record width in bytes. Must equal the field-by-field layout documented above. */
	static constexpr uint32 RecordSize = 4u /*EntityID*/ + 24u /*Pos*/ + 24u /*Rot*/ + 4u /*Speed*/ + 1u /*Bracket*/ + 1u /*ModeIndex*/;
	static_assert(RecordSize == 58u, "SimDiskCache RecordSize must match the documented field layout");

	/** Cache directory: <ProjectSaved>/MobiusSimCache. */
	PROJECTMOBIUS_API FString GetCacheDir();

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
		const FThreadSafeBool& bShouldStop);
}
