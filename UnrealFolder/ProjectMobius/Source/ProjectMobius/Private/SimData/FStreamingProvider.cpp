// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "SimData/FStreamingProvider.h"

#include "CoreGlobals.h" // GFrameCounter (same-frame eviction guard)
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// One-time construction/teardown diagnostics only — never logs on the per-frame read path (Invariant 7).
DEFINE_LOG_CATEGORY_STATIC(LogMobiusStreaming, Log, All);

/** mobius.SimCache.ForceStreaming — A4's manual switch: serve agent samples from the .msc disk cache
 *  (FStreamingProvider) instead of the fully-resident provider. Default 0: the legacy resident path,
 *  bit-identical. A5 replaces this flag with RAM-budget auto-detection. */
static TAutoConsoleVariable<int32> CVarSimCacheForceStreaming(
	TEXT("mobius.SimCache.ForceStreaming"),
	0,
	TEXT("If 1, the spawn subsystem serves agent movement samples from the .msc disk cache (streaming, perf task A4) instead of holding them fully resident. Default 0 (resident)."),
	ECVF_Default);

bool FStreamingProvider::IsForceStreamingEnabled()
{
	return CVarSimCacheForceStreaming.GetValueOnGameThread() != 0;
}

/** mobius.SimCache.AutoStreaming — A5's shipped switch: when the estimated resident footprint of a
 *  freshly-imported dataset exceeds the RAM budget below, serve it from the .msc cache and free the
 *  resident copy. Default 1; typical datasets stay comfortably under the budget and keep the resident
 *  (bit-identical legacy) path. */
static TAutoConsoleVariable<int32> CVarSimCacheAutoStreaming(
	TEXT("mobius.SimCache.AutoStreaming"),
	1,
	TEXT("If 1 (default), agent datasets whose estimated resident RAM exceeds the budget (mobius.SimCache.BudgetFraction / BudgetCapGB) are served from the .msc disk cache instead of held fully resident (perf task A5)."),
	ECVF_Default);

/** Budget = min(BudgetFraction x available physical RAM, BudgetCapGB). Both ini-settable. */
static TAutoConsoleVariable<float> CVarSimCacheBudgetFraction(
	TEXT("mobius.SimCache.BudgetFraction"),
	0.65f,
	TEXT("Fraction of currently-available physical RAM the resident agent dataset may occupy before auto-streaming kicks in (perf task A5). Clamped to [0.05, 0.95]. Default 0.65."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarSimCacheBudgetCapGB(
	TEXT("mobius.SimCache.BudgetCapGB"),
	8.0f,
	TEXT("Hard upper cap (GB) on the resident agent-dataset budget regardless of how much RAM is free (perf task A5). Default 8."),
	ECVF_Default);

/** Streaming window knobs (defaults match the A4 constants). Scaled by HddScale on seek-penalty drives. */
static TAutoConsoleVariable<int32> CVarStreamingWindowSlots(
	TEXT("mobius.Streaming.WindowSlots"),
	96,
	TEXT("FStreamingProvider slot-ring capacity (timestep blocks resident at once). Default 96."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarStreamingLookahead(
	TEXT("mobius.Streaming.Lookahead"),
	16,
	TEXT("FStreamingProvider prefetch lookahead (timesteps) in the playback direction. Default 16."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarStreamingKeyframeTarget(
	TEXT("mobius.Streaming.KeyframeTarget"),
	512,
	TEXT("FStreamingProvider always-resident keyframe count target (stride ~= NumTimesteps / this). Default 512."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarStreamingKeyframeBudgetMB(
	TEXT("mobius.Streaming.KeyframeBudgetMB"),
	64,
	TEXT("Byte cap (MB) on FStreamingProvider's always-resident keyframe set; the stride grows to respect it. Prevents short-but-fat sims from re-residenting the whole dataset as keyframes. Default 64."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStreamingHddScale(
	TEXT("mobius.Streaming.HddScale"),
	2.0f,
	TEXT("Multiplier applied to WindowSlots and Lookahead when the cache drive reports a seek penalty (HDD) — cold reads cost more there. Default 2.0."),
	ECVF_Default);

bool FStreamingProvider::IsAutoStreamingEnabled()
{
	return CVarSimCacheAutoStreaming.GetValueOnGameThread() != 0;
}

bool FStreamingProvider::ShouldStreamSimData(uint64 EstimatedResidentBytes, uint64 AvailablePhysicalBytes,
                                             float BudgetFraction, uint64 BudgetCapBytes)
{
	// Clamp so a bad ini value can neither zero the budget (everything streams) nor disable the cap.
	const float ClampedFraction = FMath::Clamp(BudgetFraction, 0.05f, 0.95f);
	const uint64 FractionBudget = static_cast<uint64>(ClampedFraction * static_cast<double>(AvailablePhysicalBytes));
	const uint64 Budget = FMath::Min(FractionBudget, BudgetCapBytes);
	return EstimatedResidentBytes > Budget;
}

FStreamingProviderConfig FStreamingProvider::MakeConfigFromCVars(bool bCacheDriveHasSeekPenalty)
{
	FStreamingProviderConfig OutConfig;
	OutConfig.WindowSlotCount = CVarStreamingWindowSlots.GetValueOnGameThread();
	OutConfig.PrefetchLookahead = CVarStreamingLookahead.GetValueOnGameThread();
	OutConfig.TargetKeyframeCount = CVarStreamingKeyframeTarget.GetValueOnGameThread();
	OutConfig.KeyframeByteBudgetMB = CVarStreamingKeyframeBudgetMB.GetValueOnGameThread();
	if (bCacheDriveHasSeekPenalty)
	{
		const float Scale = FMath::Clamp(CVarStreamingHddScale.GetValueOnGameThread(), 1.0f, 8.0f);
		OutConfig.WindowSlotCount = FMath::CeilToInt32(OutConfig.WindowSlotCount * Scale);
		OutConfig.PrefetchLookahead = FMath::CeilToInt32(OutConfig.PrefetchLookahead * Scale);
	}
	return OutConfig; // final range clamps happen in the provider constructor
}

void FStreamingProvider::GetBudgetCVars(float& OutBudgetFraction, uint64& OutBudgetCapBytes)
{
	OutBudgetFraction = CVarSimCacheBudgetFraction.GetValueOnGameThread();
	// Floor guards against a zeroed ini (which would stream everything); kept low (50 MB) so modest
	// caps stay usable for testing the auto path against mid-size datasets.
	const float CapGB = FMath::Max(0.05f, CVarSimCacheBudgetCapGB.GetValueOnGameThread());
	OutBudgetCapBytes = static_cast<uint64>(static_cast<double>(CapGB) * 1024.0 * 1024.0 * 1024.0);
}

// ---------------------------------------------------------------------------------------------------
// Construction / teardown
// ---------------------------------------------------------------------------------------------------

FStreamingProvider::FStreamingProvider(const FString& InCacheFilePath, uint64 ExpectedSourceHash,
                                       const FStreamingProviderConfig& InConfig)
	: CacheFilePath(InCacheFilePath)
	, Config(InConfig)
{
	// Sanitize the knobs: a bad ini/cvar value must degrade to something workable, never to a stuck
	// window (lookahead capped to half the ring so prefetch alone can't exhaust the evictable slots).
	Config.WindowSlotCount = FMath::Clamp(Config.WindowSlotCount, 8, 4096);
	Config.PrefetchLookahead = FMath::Clamp(Config.PrefetchLookahead, 0, Config.WindowSlotCount / 2);
	Config.TargetKeyframeCount = FMath::Clamp(Config.TargetKeyframeCount, 16, 8192);
	Config.KeyframeByteBudgetMB = FMath::Clamp(Config.KeyframeByteBudgetMB, 8, 2048);

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*CacheFilePath));
	if (!Reader)
	{
		UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Could not open cache '%s'"), *CacheFilePath);
		return;
	}

	if (!MobiusSimCache::ReadCacheHeader(*Reader, Header) || Header.SourceHash != ExpectedSourceHash
		|| Header.NumTimesteps == 0)
	{
		UE_LOG(LogMobiusStreaming, Warning,
			TEXT("[Streaming] Cache '%s' rejected (incompatible layout, hash mismatch, or empty)"), *CacheFilePath);
		return;
	}

	// Offset table: (NumTimesteps + 1) absolute offsets; monotonic, record-aligned, in-file (validated
	// by the shared reader).
	if (!MobiusSimCache::ReadOffsetTable(*Reader, Header, Offsets))
	{
		UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Cache '%s' rejected (truncated/corrupt offset table)"), *CacheFilePath);
		return;
	}

	// Always-resident coarse keyframes: every KeyframeStride-th timestep, loaded synchronously here
	// (one-time cost at load, keeps the cross-thread surface to just the slot queues). Ts 0 is always
	// a keyframe, so the spawn-time read (PedestrianInitializeMOP at t=0) is exact from frame one.
	// Stride is the LARGER of the count target and the byte budget: the count target alone collapses to
	// stride 1 on short-but-fat sims and quietly re-residents the whole dataset (see config comment).
	const uint64 TotalRecordBytes = Offsets.Last() - Offsets[0];
	const uint64 KeyframeByteBudget = static_cast<uint64>(Config.KeyframeByteBudgetMB) * 1024ull * 1024ull;
	const int32 CountStride = static_cast<int32>(Header.NumTimesteps) / Config.TargetKeyframeCount;
	const int32 ByteStride = static_cast<int32>((TotalRecordBytes + KeyframeByteBudget - 1) / KeyframeByteBudget);
	KeyframeStride = FMath::Max3(1, CountStride, ByteStride);
	for (int32 Ts = 0; Ts < static_cast<int32>(Header.NumTimesteps); Ts += KeyframeStride)
	{
		TArray<FSimMovementSample> Block;
		if (!DecodeBlock(*Reader, static_cast<int64>(Offsets[Ts]), RecordCountForTs(Ts), Block))
		{
			UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Cache '%s' rejected (keyframe decode failed at ts %d)"), *CacheFilePath, Ts);
			Keyframes.Empty();
			return;
		}
		Keyframes.Add(Ts, MoveTemp(Block));
	}

	// Fixed slot pool — sized once BEFORE the reader thread starts so element addresses stay stable
	// (the reader writes into Slots[i].Samples by index).
	Slots.SetNum(Config.WindowSlotCount);

	WakeEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset*/ false);
	Worker = MakeUnique<FReaderWorker>(*this);
	WorkerThread = FRunnableThread::Create(Worker.Get(), TEXT("MobiusSimStreamReader"), 0, TPri_Normal);
	if (!WorkerThread)
	{
		UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Could not start reader thread for '%s'"), *CacheFilePath);
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		WakeEvent = nullptr;
		Worker.Reset();
		return;
	}

	bValid = true;
	UE_LOG(LogMobiusStreaming, Log, TEXT("[Streaming] Serving '%s' (%u timesteps, keyframe stride %d, window %d slots)"),
		*CacheFilePath, Header.NumTimesteps, KeyframeStride, Config.WindowSlotCount);
}

FStreamingProvider::~FStreamingProvider()
{
	if (WorkerThread)
	{
		bStopRequested = true;
		WakeEvent->Trigger();
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}
	if (WakeEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		WakeEvent = nullptr;
	}
}

// ---------------------------------------------------------------------------------------------------
// Reader thread
// ---------------------------------------------------------------------------------------------------

uint32 FStreamingProvider::FReaderWorker::Run()
{
	// The reader owns its own archive/file handle for the provider's lifetime.
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Owner.CacheFilePath));
	if (!Reader)
	{
		UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Reader thread could not open '%s'"), *Owner.CacheFilePath);
		return 1;
	}

	while (!Owner.bStopRequested)
	{
		FReadRequest Request;
		while (Owner.RequestQueue.Dequeue(Request))
		{
			if (Owner.bStopRequested)
			{
				break;
			}
			// Safe by construction: the slot is Pending (game thread will not touch Samples), the slot
			// array is fixed-size, and the offset table is immutable.
			FSlot& Slot = Owner.Slots[Request.SlotIndex];
			DecodeBlock(*Reader, static_cast<int64>(Owner.Offsets[Request.Ts]),
				Owner.RecordCountForTs(Request.Ts), Slot.Samples);
			Owner.CompletionQueue.Enqueue(Request.SlotIndex);
		}
		if (!Owner.bStopRequested)
		{
			// Auto-reset event: a Trigger that raced the drain leaves it signalled, so no lost wakeups.
			Owner.WakeEvent->Wait();
		}
	}
	return 0;
}

void FStreamingProvider::FReaderWorker::Stop()
{
	Owner.bStopRequested = true;
	if (Owner.WakeEvent)
	{
		Owner.WakeEvent->Trigger();
	}
}

// ---------------------------------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------------------------------

bool FStreamingProvider::DecodeBlock(FArchive& Reader, int64 ByteOffset, int32 Count, TArray<FSimMovementSample>& Out)
{
	// Shared decoder (SimDiskCache) — one implementation of the record layout for the provider, the
	// A6 fast-reload path, and tests.
	return MobiusSimCache::DecodeRecords(Reader, ByteOffset, Count, Out);
}

int32 FStreamingProvider::RecordCountForTs(int32 Ts) const
{
	return static_cast<int32>((Offsets[Ts + 1] - Offsets[Ts]) / Header.RecordSize);
}

// ---------------------------------------------------------------------------------------------------
// Game-thread window bookkeeping
// ---------------------------------------------------------------------------------------------------

void FStreamingProvider::DrainCompletions() const
{
	int32 SlotIndex = INDEX_NONE;
	while (CompletionQueue.Dequeue(SlotIndex))
	{
		FSlot& Slot = Slots[SlotIndex];
		if (Slot.State == FSlot::EState::Pending)
		{
			Slot.State = FSlot::EState::Resident;
		}
	}
}

void FStreamingProvider::TouchSlot(int32 SlotIndex) const
{
	FSlot& Slot = Slots[SlotIndex];
	Slot.LastUsedTick = ++UseTickCounter;
	Slot.LastServedFrame = GFrameCounter;
}

int32 FStreamingProvider::AcquireSlot() const
{
	int32 BestEvict = INDEX_NONE;
	uint64 BestEvictTick = MAX_uint64;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FSlot& Slot = Slots[i];
		if (Slot.State == FSlot::EState::Free)
		{
			return i;
		}
		// Eviction candidates: resident, not the pinned playhead pair, not served this frame (a pointer
		// returned this frame must stay valid for the rest of the frame — see the class contract).
		if (Slot.State == FSlot::EState::Resident
			&& Slot.Ts != PinnedTsA && Slot.Ts != PinnedTsB
			&& Slot.LastServedFrame != GFrameCounter
			&& Slot.LastUsedTick < BestEvictTick)
		{
			BestEvict = i;
			BestEvictTick = Slot.LastUsedTick;
		}
	}
	if (BestEvict != INDEX_NONE)
	{
		TsToSlot.Remove(Slots[BestEvict].Ts);
		Slots[BestEvict].State = FSlot::EState::Free;
		Slots[BestEvict].Ts = INDEX_NONE;
	}
	return BestEvict;
}

void FStreamingProvider::RequestLoad(int32 Ts) const
{
	if (Ts < 0 || Ts >= static_cast<int32>(Header.NumTimesteps)
		|| TsToSlot.Contains(Ts) || Keyframes.Contains(Ts))
	{
		return;
	}
	const int32 SlotIndex = AcquireSlot();
	if (SlotIndex == INDEX_NONE)
	{
		return; // window saturated with unevictable slots — retried naturally on a later call
	}
	FSlot& Slot = Slots[SlotIndex];
	Slot.State = FSlot::EState::Pending;
	Slot.Ts = Ts;
	Slot.LastUsedTick = ++UseTickCounter;
	TsToSlot.Add(Ts, SlotIndex);
	RequestQueue.Enqueue(FReadRequest{ SlotIndex, Ts });
	WakeEvent->Trigger();
}

const TArray<FSimMovementSample>* FStreamingProvider::FindExactResident(int32 Ts) const
{
	if (const int32* SlotIndex = TsToSlot.Find(Ts))
	{
		FSlot& Slot = Slots[*SlotIndex];
		if (Slot.State == FSlot::EState::Resident)
		{
			TouchSlot(*SlotIndex);
			return &Slot.Samples;
		}
		return nullptr; // still pending
	}
	return Keyframes.Find(Ts);
}

// ---------------------------------------------------------------------------------------------------
// ISimSampleProvider
// ---------------------------------------------------------------------------------------------------

const TArray<FSimMovementSample>* FStreamingProvider::GetSamplesForTimestep(int32 Ts) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingProvider_GetSamples);
	if (!bValid)
	{
		return nullptr;
	}
	DrainCompletions();

	// Out-of-range mirrors the resident provider's TMap::Find miss (and the B2 cache's bounds guard):
	// nullptr, no request. Includes agent-grid steps past the agent range when B-Risk owns a longer clock.
	if (Ts < 0 || Ts >= static_cast<int32>(Header.NumTimesteps))
	{
		return nullptr;
	}

	if (const TArray<FSimMovementSample>* Exact = FindExactResident(Ts))
	{
		LastGoodTs = Ts;
		return Exact;
	}

	// Cold miss: kick the async load and serve a stand-in for this one tick — cosmetic-only, sanctioned
	// by Invariant 5 (analysis never uses this accessor; it uses ForEachTimestep). Never blocks.
	RequestLoad(Ts);

	if (LastGoodTs != INDEX_NONE)
	{
		if (const TArray<FSimMovementSample>* LastGood = FindExactResident(LastGoodTs))
		{
			return LastGood;
		}
	}

	// Nearest keyframe (rounded to the stride, clamped in range).
	const int32 NearestKf = FMath::Clamp(
		FMath::RoundToInt(static_cast<float>(Ts) / KeyframeStride) * KeyframeStride,
		0, (static_cast<int32>(Header.NumTimesteps) - 1) / KeyframeStride * KeyframeStride);
	return Keyframes.Find(NearestKf);
}

bool FStreamingProvider::HasExactSamplesForTimestep(const int32 Ts) const
{
	if (!bValid || Ts < 0 || Ts >= static_cast<int32>(Header.NumTimesteps))
	{
		return false;
	}
	DrainCompletions();
	// Exact residency only — deliberately NO RequestLoad here (side-effect-free contract); the
	// paired GetSamplesForTimestep call (if any) is what kicks the load.
	return FindExactResident(Ts) != nullptr;
}

int32 FStreamingProvider::GetNumTimesteps() const
{
	return bValid ? static_cast<int32>(Header.NumTimesteps) : 0;
}

bool FStreamingProvider::IsValidAndPopulated() const
{
	return bValid && Header.NumTimesteps > 0;
}

const TArray<FString>& FStreamingProvider::GetModeTable() const
{
	return Header.ModeTable;
}

void FStreamingProvider::ForEachTimestep(TFunctionRef<void(int32, const TArray<FSimMovementSample>&)> Fn) const
{
	// Guaranteed-complete sequential pass with its OWN reader (Invariant 5: analysis never depends on
	// the window). Synchronous by contract — same as the resident provider's full-map iteration, just
	// from disk. The .msc is dense (A3 writes every index 0..NumTimesteps-1, empty blocks included), so
	// this visits exactly the timesteps the resident TMap holds, ascending.
	if (!bValid)
	{
		return;
	}
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*CacheFilePath));
	if (!Reader)
	{
		UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] ForEachTimestep could not open '%s'"), *CacheFilePath);
		return;
	}
	TArray<FSimMovementSample> Block;
	for (int32 Ts = 0; Ts < static_cast<int32>(Header.NumTimesteps); ++Ts)
	{
		if (!DecodeBlock(*Reader, static_cast<int64>(Offsets[Ts]), RecordCountForTs(Ts), Block))
		{
			UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] ForEachTimestep decode failed at ts %d in '%s'"), Ts, *CacheFilePath);
			return;
		}
		Fn(Ts, Block);
	}
}

void FStreamingProvider::NotifyPlayhead(int32 Ts, int32 DirectionHint)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingProvider_NotifyPlayhead);
	if (!bValid)
	{
		return;
	}
	DrainCompletions();

	// Pin the playhead pair (N, N+1) — the movement processor interpolates between exactly these two.
	PinnedTsA = Ts;
	PinnedTsB = Ts + 1;
	RequestLoad(PinnedTsA);
	RequestLoad(PinnedTsB);

	// Direction-aware prefetch on the agent grid. RequestLoad no-ops per-step when out of range,
	// already resident/pending, or a keyframe.
	if (DirectionHint > 0)
	{
		for (int32 Ahead = 2; Ahead <= Config.PrefetchLookahead; ++Ahead)
		{
			RequestLoad(Ts + Ahead);
		}
	}
	else if (DirectionHint < 0)
	{
		for (int32 Behind = 1; Behind <= Config.PrefetchLookahead; ++Behind)
		{
			RequestLoad(Ts - Behind);
		}
	}
	else
	{
		for (int32 Delta = 1; Delta <= Config.PrefetchLookahead / 2; ++Delta)
		{
			RequestLoad(Ts + Delta);
			RequestLoad(Ts - Delta);
		}
	}
}

const TArray<FSimMovementSample>* FStreamingProvider::BlockUntilTimestepResident(int32 Ts, double TimeoutSeconds)
{
	// TEST/TOOLING ONLY — the runtime read path never blocks. Sleep-polls the async pipeline so the
	// golden-equality test exercises the real reader thread instead of the cosmetic fallback. Note the
	// same-frame eviction guard: more than WindowSlotCount distinct timesteps requested inside one
	// frame cannot all be resident at once — keep test datasets under the window size (or use
	// ForEachTimestep for arbitrarily large sweeps).
	if (!bValid || Ts < 0 || Ts >= static_cast<int32>(Header.NumTimesteps))
	{
		return nullptr;
	}
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	while (FPlatformTime::Seconds() < Deadline)
	{
		DrainCompletions();
		if (const TArray<FSimMovementSample>* Exact = FindExactResident(Ts))
		{
			LastGoodTs = Ts;
			return Exact;
		}
		RequestLoad(Ts);
		FPlatformProcess::Sleep(0.001f);
	}
	return nullptr;
}
