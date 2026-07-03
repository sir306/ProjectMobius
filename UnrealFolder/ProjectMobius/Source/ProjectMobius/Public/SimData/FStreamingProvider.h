// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "SimData/ISimSampleProvider.h"
#include "SimData/SimDiskCache.h"

class FRunnableThread;

/**
 * Window sizing knobs for FStreamingProvider (perf task A5). Defaults match A4's original fixed
 * constants; the spawn subsystem populates them from the mobius.Streaming.* cvars (ini-settable),
 * scaled up on drives with a seek penalty (HDD) where cold reads cost more.
 */
struct FStreamingProviderConfig
{
	/** Ring capacity: playhead pins + prefetch + scrub slack. */
	int32 WindowSlotCount = 96;

	/** Timesteps prefetched in the playback direction on each playhead notify. */
	int32 PrefetchLookahead = 16;

	/** Always-resident keyframe count target: stride ~= NumTimesteps / this. */
	int32 TargetKeyframeCount = 512;

	/**
	 * Byte cap on the always-resident keyframe set. The count target alone breaks down on short-but-fat
	 * sims (few hundred timesteps x thousands of agents): stride rounds to 1 and the "keyframes" become
	 * the whole dataset resident again — observed 2026-07-03 with a 600-timestep / 176 MB file. The
	 * effective stride is max(count-based, ceil(TotalRecordBytes / this)).
	 */
	int32 KeyframeByteBudgetMB = 64;
};

/**
 * Disk-backed ISimSampleProvider streaming from an A3 ".msc" cache (perf task A4).
 *
 * Bounds resident RAM to a fixed slot window (plus a coarse keyframe stride) instead of the whole
 * dataset, while keeping the game-thread read path wait-free: a single dedicated reader thread owns
 * the file handle and fills slots; the game thread only exchanges requests/completions through
 * SPSC queues. On a cold miss the windowed accessor NEVER blocks — it kicks an async load and serves
 * the last-good / nearest-keyframe block for one tick (cosmetic, self-correcting; sanctioned by
 * Invariant 5). Analysis must not use the windowed accessor at all: ForEachTimestep is a separate,
 * guaranteed-complete sequential disk pass with its own reader.
 *
 * Timestep axis: the AGENT grid (PRD §A·0) — the same `Ts` the movement processor already computes
 * via RecomputeAgentTimeIndex. Out-of-range Ts is a safe no-op: GetSamplesForTimestep returns
 * nullptr (mirroring the resident provider's Find miss) and NotifyPlayhead prefetch skips it.
 *
 * Threading model (the part that keeps this simple):
 *  - ALL slot/window state transitions happen on the game thread (inside the provider calls).
 *  - The reader thread ONLY decodes into a slot's sample array while that slot is Pending (the game
 *    thread never reads a Pending slot) and then reports the slot index through the completion queue.
 *  - The offset table / header are immutable after construction, so both threads read them freely.
 *
 * Pointer lifetime contract (matches how the consumers behave today): a pointer returned by
 * GetSamplesForTimestep is valid for the current frame — slots served this frame and the pinned
 * playhead pair (N, N+1) are never evicted, and consumers do not hold sample pointers across frames
 * (the movement processor's B2 cache stores derived index maps, not sample pointers).
 */
class PROJECTMOBIUS_API FStreamingProvider final : public ISimSampleProvider
{
public:
	/**
	 * Opens + validates the cache (header parse, layout compatibility, source-hash match), loads the
	 * offset table and the keyframe stride synchronously, then starts the reader thread. On ANY
	 * validation failure the provider is left invalid (IsValidAndPopulated() == false) and the caller
	 * is expected to fall back to FFullyResidentProvider.
	 */
	FStreamingProvider(const FString& InCacheFilePath, uint64 ExpectedSourceHash,
	                   const FStreamingProviderConfig& InConfig = FStreamingProviderConfig());
	virtual ~FStreamingProvider() override;

	FStreamingProvider(const FStreamingProvider&) = delete;
	FStreamingProvider& operator=(const FStreamingProvider&) = delete;

	// ISimSampleProvider
	virtual const TArray<FSimMovementSample>* GetSamplesForTimestep(int32 Ts) const override;
	virtual int32 GetNumTimesteps() const override;
	virtual bool IsValidAndPopulated() const override;
	virtual const TArray<FString>& GetModeTable() const override;
	virtual void ForEachTimestep(TFunctionRef<void(int32, const TArray<FSimMovementSample>&)> Fn) const override;
	virtual void NotifyPlayhead(int32 Ts, int32 DirectionHint) override;

	/**
	 * TEST/TOOLING ONLY — request Ts and wait (sleep-poll) until its exact block is resident, then
	 * return it. The runtime read path never blocks; the golden-equality test uses this to compare
	 * exact streamed data instead of the one-tick cosmetic serve.
	 */
	const TArray<FSimMovementSample>* BlockUntilTimestepResident(int32 Ts, double TimeoutSeconds = 5.0);

	/** True when cvar mobius.SimCache.ForceStreaming is 1 (A4 manual flag; default 0 = resident path). */
	static bool IsForceStreamingEnabled();

	/** True when cvar mobius.SimCache.AutoStreaming is 1 (A5 RAM-budget auto-detection, default 1). */
	static bool IsAutoStreamingEnabled();

	/**
	 * A5 residency decision (pure; unit-tested): stream when the estimated resident footprint exceeds
	 * the budget min(BudgetFraction x AvailablePhysicalBytes, CapBytes). Fraction is clamped to a sane
	 * range so a bad ini value cannot produce a zero/absurd budget.
	 */
	static bool ShouldStreamSimData(uint64 EstimatedResidentBytes, uint64 AvailablePhysicalBytes,
	                                float BudgetFraction, uint64 BudgetCapBytes);

	/** Window config from the mobius.Streaming.* cvars, scaled up when the cache drive has a seek penalty. */
	static FStreamingProviderConfig MakeConfigFromCVars(bool bCacheDriveHasSeekPenalty);

	/** Current budget knobs from the mobius.SimCache.* cvars (fraction, cap in bytes). */
	static void GetBudgetCVars(float& OutBudgetFraction, uint64& OutBudgetCapBytes);

	/** Header MaxTime (seconds) — same value the resident path gets from FSimulationFragment. */
	float GetMaxTime() const { return Header.MaxTime; }

	/** Header sampling interval (seconds). */
	float GetTimeBetweenSteps() const { return Header.TimeBetweenSteps; }

private:
	/** Fixed-size sample window slot. State transitions game-thread only; reader fills Samples while Pending. */
	struct FSlot
	{
		enum class EState : uint8 { Free, Pending, Resident };

		EState State = EState::Free;
		int32 Ts = INDEX_NONE;
		uint64 LastUsedTick = 0;      // LRU key (provider-call counter, not wall time)
		uint64 LastServedFrame = 0;   // GFrameCounter when last returned to a consumer — never evict same-frame
		TArray<FSimMovementSample> Samples;
	};

	/** Read request handed to the reader thread. Offsets/counts derive from the immutable offset table. */
	struct FReadRequest
	{
		int32 SlotIndex = INDEX_NONE;
		int32 Ts = INDEX_NONE;
	};

	/** Dedicated reader: owns its FArchive, decodes requested blocks, reports completions. */
	class FReaderWorker final : public FRunnable
	{
	public:
		explicit FReaderWorker(FStreamingProvider& InOwner) : Owner(InOwner) {}
		virtual uint32 Run() override;
		virtual void Stop() override;

	private:
		FStreamingProvider& Owner;
	};

	// --- game-thread helpers (all mutate the mutable window state; see const note below) ---
	void DrainCompletions() const;
	void TouchSlot(int32 SlotIndex) const;
	/** Kick an async load of Ts if it is in range and not already resident/pending. */
	void RequestLoad(int32 Ts) const;
	/** Free slot index, evicting the LRU eligible slot if needed; INDEX_NONE when nothing is evictable. */
	int32 AcquireSlot() const;
	/** Exact resident block for Ts (slot or keyframe), nullptr otherwise. Updates LRU/serve stamps. */
	const TArray<FSimMovementSample>* FindExactResident(int32 Ts) const;

	/** Per-timestep record count from the offset table (0 for an empty block). */
	int32 RecordCountForTs(int32 Ts) const;
	/** Decode Count records of the A3 field layout starting at ByteOffset into Out. Any-thread (own archive pos). */
	static bool DecodeBlock(FArchive& Reader, int64 ByteOffset, int32 Count, TArray<FSimMovementSample>& Out);

	// --- immutable after construction (safe to read from both threads) ---
	FString CacheFilePath;
	FStreamingProviderConfig Config;
	MobiusSimCache::FMscHeader Header;
	TArray<uint64> Offsets;               // (NumTimesteps + 1) absolute byte offsets
	TMap<int32, TArray<FSimMovementSample>> Keyframes; // every KeyframeStride-th ts, loaded at construction
	int32 KeyframeStride = 1;
	bool bValid = false;

	// --- window state; game-thread only. `mutable` because the interface's windowed accessor is const
	// (the resident provider is a pure lookup) while streaming must drain completions / track LRU on read.
	mutable TArray<FSlot> Slots;
	mutable TMap<int32, int32> TsToSlot;  // resident + pending timesteps -> slot index
	mutable uint64 UseTickCounter = 0;
	mutable int32 LastGoodTs = INDEX_NONE; // last exactly-served ts, for the one-tick cosmetic fallback
	int32 PinnedTsA = INDEX_NONE;          // playhead N   (never evicted)
	int32 PinnedTsB = INDEX_NONE;          // playhead N+1 (never evicted)

	// --- cross-thread ---
	mutable TQueue<FReadRequest, EQueueMode::Spsc> RequestQueue;   // game -> reader
	mutable TQueue<int32, EQueueMode::Spsc> CompletionQueue;       // reader -> game (slot index)
	FEvent* WakeEvent = nullptr;
	FThreadSafeBool bStopRequested = false;
	TUniquePtr<FReaderWorker> Worker;
	FRunnableThread* WorkerThread = nullptr;

};
