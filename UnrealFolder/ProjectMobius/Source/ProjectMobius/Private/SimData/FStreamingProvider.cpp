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

// ---------------------------------------------------------------------------------------------------
// Construction / teardown
// ---------------------------------------------------------------------------------------------------

FStreamingProvider::FStreamingProvider(const FString& InCacheFilePath, uint64 ExpectedSourceHash)
	: CacheFilePath(InCacheFilePath)
{
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

	// Offset table: (NumTimesteps + 1) absolute offsets; must be monotonic, record-aligned, and in-file.
	const int32 NumOffsets = static_cast<int32>(Header.NumTimesteps) + 1;
	Offsets.SetNumUninitialized(NumOffsets);
	for (int32 i = 0; i < NumOffsets; ++i)
	{
		*Reader << Offsets[i];
	}
	if (Reader->IsError() || static_cast<int64>(Offsets.Last()) > Reader->TotalSize())
	{
		UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Cache '%s' rejected (truncated offset table/records)"), *CacheFilePath);
		return;
	}
	for (int32 i = 0; i + 1 < NumOffsets; ++i)
	{
		const uint64 BlockBytes = Offsets[i + 1] - Offsets[i];
		if (Offsets[i + 1] < Offsets[i] || (BlockBytes % Header.RecordSize) != 0)
		{
			UE_LOG(LogMobiusStreaming, Warning, TEXT("[Streaming] Cache '%s' rejected (corrupt offset table)"), *CacheFilePath);
			return;
		}
	}

	// Always-resident coarse keyframes: every KeyframeStride-th timestep, loaded synchronously here
	// (one-time cost at load, keeps the cross-thread surface to just the slot queues). Ts 0 is always
	// a keyframe, so the spawn-time read (PedestrianInitializeMOP at t=0) is exact from frame one.
	KeyframeStride = FMath::Max(1, static_cast<int32>(Header.NumTimesteps) / TargetKeyframeCount);
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
	Slots.SetNum(WindowSlotCount);

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
		*CacheFilePath, Header.NumTimesteps, KeyframeStride, WindowSlotCount);
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
	// Field-by-field decode in the exact writer order (SimDiskCache.h format block) — never a struct
	// memcpy; the stream is padding-free and FSimMovementSample is not.
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
		for (int32 Ahead = 2; Ahead <= PrefetchLookahead; ++Ahead)
		{
			RequestLoad(Ts + Ahead);
		}
	}
	else if (DirectionHint < 0)
	{
		for (int32 Behind = 1; Behind <= PrefetchLookahead; ++Behind)
		{
			RequestLoad(Ts - Behind);
		}
	}
	else
	{
		for (int32 Delta = 1; Delta <= PrefetchLookahead / 2; ++Delta)
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
