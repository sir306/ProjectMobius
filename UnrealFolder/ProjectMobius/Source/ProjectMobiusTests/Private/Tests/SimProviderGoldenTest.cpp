// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// SimProviderGoldenTest.cpp
//
// Golden-frame equality for perf task A4 (Invariant 5): FStreamingProvider serving a .msc written by
// the A3 writer must return, for every timestep, EXACTLY what FFullyResidentProvider returns over the
// same in-memory data — bitwise for doubles, exact for ints/enums. Covers the windowed accessor (via
// the test-only blocking wait, so the real reader thread is exercised instead of the cosmetic
// fallback), the guaranteed-complete ForEachTimestep pass, metadata (NumTimesteps / ModeTable), and
// the out-of-range nullptr contract.
//
// Run: UnrealEditor ProjectMobius.uproject -ExecCmds="Automation RunTests ProjectMobius.SimData" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/ThreadSafeBool.h"
#include "SimData/FStreamingProvider.h"
#include "SimData/ISimSampleProvider.h"
#include "SimData/SimDiskCache.h"

namespace
{
	/**
	 * Deterministic mixed-value dataset. Sized WELL UNDER FStreamingProvider's slot window (96) so the
	 * per-timestep blocking reads can all be resident within the test's single frame (the provider's
	 * same-frame eviction guard). Timestep 7 is stored EMPTY (resident TMap holds a zero-length array)
	 * to prove streaming reproduces empty-but-present. Values are arbitrary bit patterns — the
	 * comparison is bitwise, not tolerance-based.
	 */
	constexpr int32 GoldenNumTimesteps = 48;

	TMap<int32, TArray<FSimMovementSample>> MakeGoldenSimData()
	{
		TMap<int32, TArray<FSimMovementSample>> Data;
		for (int32 Ts = 0; Ts < GoldenNumTimesteps; ++Ts)
		{
			TArray<FSimMovementSample> Block;
			const int32 Count = (Ts == 7) ? 0 : 1 + (Ts % 5);
			for (int32 i = 0; i < Count; ++i)
			{
				FSimMovementSample S;
				S.EntityID = Ts * 100 + i;
				// Non-representable decimals on purpose: bitwise round-trip must still be exact.
				S.Position = FVector(Ts * 1.1, i * -2.3, 0.7 + Ts * 0.001);
				S.Rotation = FRotator(i * 3.3, Ts * -7.7, 0.1 * i);
				S.Speed = 0.31f * Ts + 0.017f * i;
				S.MovementBracket = static_cast<EPedestrianMovementBracket>(i % 3);
				S.ModeIndex = static_cast<uint8>(i % 2);
				Block.Add(S);
			}
			Data.Add(Ts, MoveTemp(Block));
		}
		return Data;
	}

	/** Bitwise per-field sample equality (doubles compared as bit patterns, never with tolerance). */
	bool SamplesBitIdentical(const FSimMovementSample& A, const FSimMovementSample& B)
	{
		auto BitEq = [](double X, double Y) { return FMemory::Memcmp(&X, &Y, sizeof(double)) == 0; };
		return A.EntityID == B.EntityID
			&& BitEq(A.Position.X, B.Position.X) && BitEq(A.Position.Y, B.Position.Y) && BitEq(A.Position.Z, B.Position.Z)
			&& BitEq(A.Rotation.Pitch, B.Rotation.Pitch) && BitEq(A.Rotation.Yaw, B.Rotation.Yaw) && BitEq(A.Rotation.Roll, B.Rotation.Roll)
			&& FMemory::Memcmp(&A.Speed, &B.Speed, sizeof(float)) == 0
			&& A.MovementBracket == B.MovementBracket
			&& A.ModeIndex == B.ModeIndex;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimProviderGoldenTest,
	"ProjectMobius.SimData.GoldenFrameEquality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimProviderGoldenTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();

	// --- Arrange: known data -> resident provider + a .msc written by the A3 writer. ---
	TSharedPtr<TMap<int32, TArray<FSimMovementSample>>> SimulationData =
		MakeShared<TMap<int32, TArray<FSimMovementSample>>>(MakeGoldenSimData());

	const float MaxTime = 4.8f;
	const float TimeBetweenSteps = 0.1f;
	// A real (multi-entry) table so streaming proves it decodes more than the default { "" }.
	const TArray<FString> ModeTable = { FString(TEXT("")), FString(TEXT("walk")) };

	const FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusSimCacheTest"));
	FileManager.MakeDirectory(*TempDir, /*Tree*/ true);
	const FString FakeSource = FPaths::Combine(TempDir, TEXT("GoldenSource.json"));
	FFileHelper::SaveStringToFile(TEXT("{\"fake\":\"golden source\"}"), *FakeSource);

	const uint64 SourceHash = MobiusSimCache::ComputeSourceHash(FakeSource);
	const FString CacheFilePath = MobiusSimCache::MakeCacheFilePath(FakeSource, SourceHash);
	FileManager.Delete(*CacheFilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);

	FThreadSafeBool bShouldStop(false);
	const bool bWrote = MobiusSimCache::WriteCacheFile(
		CacheFilePath, SourceHash, *SimulationData, MaxTime, TimeBetweenSteps, ModeTable,
		/*MaxAgents*/ 5, /*SourceFormat*/ 1, TArray<FMobiusAgentEntityData>(), bShouldStop);
	TestTrue(TEXT("WriteCacheFile succeeded"), bWrote);

	if (bWrote)
	{
		FFullyResidentProvider Resident(SimulationData, ModeTable);
		FStreamingProvider Streaming(CacheFilePath, SourceHash);

		TestTrue(TEXT("Streaming provider valid"), Streaming.IsValidAndPopulated());
		if (Streaming.IsValidAndPopulated())
		{
			// --- Metadata parity. ---
			TestEqual(TEXT("NumTimesteps"), Streaming.GetNumTimesteps(), Resident.GetNumTimesteps());
			TestEqual(TEXT("ModeTable size"), Streaming.GetModeTable().Num(), ModeTable.Num());
			for (int32 i = 0; i < ModeTable.Num() && i < Streaming.GetModeTable().Num(); ++i)
			{
				TestEqual(FString::Printf(TEXT("ModeTable[%d]"), i), Streaming.GetModeTable()[i], ModeTable[i]);
			}
			TestEqual(TEXT("Header MaxTime"), Streaming.GetMaxTime(), MaxTime);
			TestEqual(TEXT("Header TimeBetweenSteps"), Streaming.GetTimeBetweenSteps(), TimeBetweenSteps);

			// --- Windowed accessor: exact block per timestep (real async path via the blocking wait). ---
			for (int32 Ts = 0; Ts < GoldenNumTimesteps; ++Ts)
			{
				const TArray<FSimMovementSample>* ResidentBlock = Resident.GetSamplesForTimestep(Ts);
				const TArray<FSimMovementSample>* StreamedBlock = Streaming.BlockUntilTimestepResident(Ts);
				const FString Where = FString::Printf(TEXT("Ts %d"), Ts);

				if (!TestNotNull(Where + TEXT(" resident block"), ResidentBlock)) { continue; }
				if (!TestNotNull(Where + TEXT(" streamed block (async load completed)"), StreamedBlock)) { continue; }

				TestEqual(Where + TEXT(" sample count"), StreamedBlock->Num(), ResidentBlock->Num());
				if (StreamedBlock->Num() == ResidentBlock->Num())
				{
					for (int32 i = 0; i < ResidentBlock->Num(); ++i)
					{
						if (!SamplesBitIdentical((*StreamedBlock)[i], (*ResidentBlock)[i]))
						{
							AddError(FString::Printf(TEXT("%s sample %d differs (bitwise)"), *Where, i));
							break;
						}
					}
				}
			}

			// The empty-but-present timestep must be non-null and empty on BOTH sides.
			{
				const TArray<FSimMovementSample>* ResidentEmpty = Resident.GetSamplesForTimestep(7);
				const TArray<FSimMovementSample>* StreamedEmpty = Streaming.BlockUntilTimestepResident(7);
				TestTrue(TEXT("Ts 7 resident empty-but-present"), ResidentEmpty && ResidentEmpty->Num() == 0);
				TestTrue(TEXT("Ts 7 streamed empty-but-present"), StreamedEmpty && StreamedEmpty->Num() == 0);
			}

			// --- Out-of-range parity: nullptr, and playhead notification is a safe no-op (A·0: an
			// agent-grid step past the agent range when B-Risk owns a longer clock). ---
			TestTrue(TEXT("Ts -1 nullptr (both)"),
				Resident.GetSamplesForTimestep(-1) == nullptr && Streaming.GetSamplesForTimestep(-1) == nullptr);
			TestTrue(TEXT("Ts NumTimesteps nullptr (both)"),
				Resident.GetSamplesForTimestep(GoldenNumTimesteps) == nullptr
				&& Streaming.GetSamplesForTimestep(GoldenNumTimesteps) == nullptr);
			Streaming.NotifyPlayhead(GoldenNumTimesteps + 100, /*DirectionHint*/ 1); // must not crash or request

			// --- ForEachTimestep: guaranteed-complete pass parity (the analysis path, Invariant 5). ---
			int32 ResidentVisits = 0, StreamedVisits = 0;
			bool bForEachIdentical = true;
			TMap<int32, TArray<FSimMovementSample>> ResidentPass;
			Resident.ForEachTimestep([&](int32 Ts, const TArray<FSimMovementSample>& Samples)
			{
				++ResidentVisits;
				ResidentPass.Add(Ts, Samples);
			});
			Streaming.ForEachTimestep([&](int32 Ts, const TArray<FSimMovementSample>& Samples)
			{
				++StreamedVisits;
				const TArray<FSimMovementSample>* Expected = ResidentPass.Find(Ts);
				if (!Expected || Expected->Num() != Samples.Num())
				{
					bForEachIdentical = false;
					return;
				}
				for (int32 i = 0; i < Samples.Num(); ++i)
				{
					if (!SamplesBitIdentical(Samples[i], (*Expected)[i]))
					{
						bForEachIdentical = false;
						return;
					}
				}
			});
			TestEqual(TEXT("ForEachTimestep visit count"), StreamedVisits, ResidentVisits);
			TestTrue(TEXT("ForEachTimestep blocks bit-identical"), bForEachIdentical);
		}
	}

	// --- A5 config plumbing: a custom (small) window must still be valid and serve exact data. The
	// window (16) stays >= the dataset's distinct blocked reads here (2), so the same-frame eviction
	// guard cannot starve it. ---
	if (bWrote)
	{
		FStreamingProviderConfig SmallConfig;
		SmallConfig.WindowSlotCount = 16;
		SmallConfig.PrefetchLookahead = 4;
		SmallConfig.TargetKeyframeCount = 64;
		FStreamingProvider SmallWindow(CacheFilePath, SourceHash, SmallConfig);
		TestTrue(TEXT("Custom-config provider valid"), SmallWindow.IsValidAndPopulated());
		if (SmallWindow.IsValidAndPopulated())
		{
			TestEqual(TEXT("Custom-config NumTimesteps"), SmallWindow.GetNumTimesteps(), GoldenNumTimesteps);
			const TArray<FSimMovementSample>* Block = SmallWindow.BlockUntilTimestepResident(3);
			TestTrue(TEXT("Custom-config serves ts 3 exactly"), Block && Block->Num() == 4); // 1 + (3 % 5)
			SmallWindow.NotifyPlayhead(3, 1);
			const TArray<FSimMovementSample>* Next = SmallWindow.BlockUntilTimestepResident(4);
			TestTrue(TEXT("Custom-config serves ts 4 exactly"), Next && Next->Num() == 5); // 1 + (4 % 5)
		}
	}

	// --- Cleanup. ---
	FileManager.Delete(*CacheFilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	FileManager.Delete(*FakeSource, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	FileManager.DeleteDirectory(*TempDir, /*RequireExists*/ false, /*Tree*/ true);

	return true;
}

// ---------------------------------------------------------------------------------------------------
// A5 residency decision — pure budget predicate (mirrors the B2 predicate-test pattern).
// Budget = min(clamped-fraction x available physical, cap); stream when the estimate EXCEEDS it.
// ---------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStreamingBudgetDecisionTest,
	"ProjectMobius.SimData.StreamingBudgetDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStreamingBudgetDecisionTest::RunTest(const FString& Parameters)
{
	using Provider = FStreamingProvider;
	constexpr uint64 GB = 1024ull * 1024ull * 1024ull;

	// Comfortably under budget -> resident (the everyday case).
	TestFalse(TEXT("1 GB estimate, 16 GB free, cap 8 -> resident"),
		Provider::ShouldStreamSimData(1 * GB, 16 * GB, 0.65f, 8 * GB));

	// Fraction binds before the cap: 0.65 x 4 GB = 2.6 GB budget < 3 GB estimate.
	TestTrue(TEXT("3 GB estimate, 4 GB free (fraction-bound) -> stream"),
		Provider::ShouldStreamSimData(3 * GB, 4 * GB, 0.65f, 8 * GB));

	// Cap binds on a big machine: 0.65 x 64 GB = 41.6 GB, but cap 8 GB < 10 GB estimate.
	TestTrue(TEXT("10 GB estimate, 64 GB free (cap-bound) -> stream"),
		Provider::ShouldStreamSimData(10 * GB, 64 * GB, 0.65f, 8 * GB));

	// Exactly AT budget stays resident (strictly-greater threshold).
	TestFalse(TEXT("Estimate == cap budget -> resident"),
		Provider::ShouldStreamSimData(8 * GB, 64 * GB, 0.65f, 8 * GB));

	// Low-fraction clamp (0.05 floor): a near-zero ini value must not zero the budget and stream
	// everything — 0.05 x 100 GB = 5 GB budget > 1 GB estimate.
	TestFalse(TEXT("Near-zero fraction clamps to 0.05 -> small dataset stays resident"),
		Provider::ShouldStreamSimData(1 * GB, 100 * GB, 0.0001f, 8 * GB));

	// High-fraction clamp (0.95 ceiling): an absurd ini value must not disable the fraction —
	// 0.95 x 4 GB = 3.8 GB budget < 3.9 GB estimate (unclamped 5.0 would keep it resident).
	TestTrue(TEXT("Absurd fraction clamps to 0.95 -> oversized dataset still streams"),
		Provider::ShouldStreamSimData(static_cast<uint64>(3.9 * GB), 4 * GB, 5.0f, 8 * GB));

	return true;
}

#endif // !UE_BUILD_SHIPPING
