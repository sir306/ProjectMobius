// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryCaptureRecorder.h
//
// Records the whole trajectory-heatmap data path for one bounded capture window, so a run can be
// diffed against the source movement data to find where samples are lost.
//
// The path has four stages and this records all four, because a gap can be introduced at any of them:
//
//   1. COLLECT  UAgentHeatmapProcessor::ProcessChunk    - per entity per frame, incl. entities skipped
//                                                         for !bRenderAgent / bReadyToDestroy, and
//                                                         positions that did not change (no segment)
//   2. FLUSH    UAgentHeatmapProcessor::ApplyHeatmapUpdates - which batch went out, when, how big
//   3. FILTER   UHeatmapSubsystem::UpdateHeatmapsWithTrajectorySegments - the End-only Z-band test
//   4. RASTER   AHeatmapPixelTextureVisualizer::UpdateHeatmapWithTrajectorySegments - world -> texel
//
// Stages 3 and 4 are recorded only for the target floor, which bounds their volume. Stage 1 records
// every entity, because "the processor never even offered this agent" is itself a finding.
//
// THREADING: every hook site runs on the game thread - the heatmap processor sets
// bRequiresGameThreadExecution, ProcessChunk is a plain ForEachEntityChunk (the ParallelFor variant is
// commented out), and the trajectory flush is deliberately synchronous. So no locking here. A check()
// in Arm() pins that assumption.
//
// COST WHEN DISARMED: one predictable branch on a bool at each hook site. Nothing is logged via
// UE_LOG in the tick path; rows accumulate in preallocated arrays and are written once, to CSV, at the
// end of the window while the simulation is paused.
//
#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class AHeatmapPixelTextureVisualizer;
class UTimeDilationSubSystem;
class UWorld;

class MOBIUSCORE_API FTrajectoryCaptureRecorder
{
public:
	static FTrajectoryCaptureRecorder& Get();

	/**
	 * Hot check at every hook site. Deliberately a plain bool, not an atomic: all hook sites are
	 * game-thread-only (see the threading note above), so an atomic would add cost for no correctness.
	 */
	FORCEINLINE bool IsArmed() const { return bArmed; }

	/** True only for the floor being captured, so stage 3/4 hooks can bail immediately. */
	FORCEINLINE bool IsTargetFloor(int32 FloorID) const { return bArmed && FloorID == TargetFloorID; }

	/**
	 * Reset the playhead to zero, clear the accumulation and start capturing.
	 * @return an empty string on success, otherwise the reason it could not start.
	 */
	FString Arm(UWorld* World, int32 InFloorID, float InDurationSeconds, int32 InMaxRowsPerStream);

	/** Frame heartbeat, driven from the heatmap processor. Ends the window and writes the CSVs. */
	void Tick(float SimTime);

	/** Stop early and write whatever has been captured so far. */
	void Abort();

	// --- hooks ------------------------------------------------------------------------------------
	void RecordCollect(float SimTime, int32 EntityID, const FVector& Location,
	                   bool bRenderAgent, bool bReadyToDestroy, bool bHasPrevious,
	                   bool bPositionChanged, bool bSegmentEmitted);

	void RecordFlush(float SimTime, int32 SegmentCount, bool bTrajectoryActive);

	void RecordFilter(float SimTime, int32 FloorID, float OriginZ, float MaxAddHeight,
	                  const FVector& Start, const FVector& End, bool bKept);

	void RecordRaster(float SimTime, int32 FloorID, const FVector& Start, const FVector& End,
	                  FIntPoint StartTexel, FIntPoint EndTexel, bool bDrawn);

private:
	FTrajectoryCaptureRecorder() = default;

	void Finish(const TCHAR* Reason);
	void WriteAllStreams(const FString& Directory);
	bool WriteTextureDump(const FString& Path, int32& OutTouched, int32& OutPeak) const;
	/** @param RelativeDir path under Saved/, which is the form SaveDynamicTextureToPNG expects. */
	void WriteTexturePngs(const FString& Directory, const FString& RelativeDir, int32 PeakByte) const;

	// Rows are packed and positions kept as float32: at this world scale (<= ~10 km) that is sub-10-um
	// precision, far finer than anything being compared, and it roughly halves the resident footprint.
	struct FCollectRow
	{
		float SimTime;
		int32 Frame;
		int32 EntityID;
		FVector3f Location;
		uint8 Flags; // 1 render, 2 readyToDestroy, 4 hasPrev, 8 posChanged, 16 emitted
	};

	struct FFlushRow
	{
		float SimTime;
		int32 Frame;
		int32 Sequence;
		int32 SegmentCount;
		uint8 bTrajectoryActive;
	};

	struct FSegmentRow
	{
		float SimTime;
		int32 Frame;
		FVector3f Start;
		FVector3f End;
		int32 StartX, StartY, EndX, EndY; // texels; -1 on the filter stream, which runs before mapping
		uint8 bAccepted;
	};

	bool bArmed = false;
	int32 TargetFloorID = 0;
	float TargetOriginZ = 0.0f;
	float DurationSeconds = 30.0f;
	float StartSimTime = 0.0f;
	int32 MaxRowsPerStream = 4'000'000;
	int32 FrameCounter = 0;
	int32 FlushSequence = 0;

	// Non-silent truncation: any stream that hits the cap reports how many rows it lost.
	int32 DroppedCollect = 0;
	int32 DroppedFilter = 0;
	int32 DroppedRaster = 0;

	TArray<FCollectRow> CollectRows;
	TArray<FFlushRow> FlushRows;
	TArray<FSegmentRow> FilterRows;
	TArray<FSegmentRow> RasterRows;

	TWeakObjectPtr<AHeatmapPixelTextureVisualizer> TargetHeatmap;
	TWeakObjectPtr<UTimeDilationSubSystem> TimeDilation;
};

#endif // !UE_BUILD_SHIPPING
