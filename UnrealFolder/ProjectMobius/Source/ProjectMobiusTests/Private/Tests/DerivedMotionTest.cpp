// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// DerivedMotionTest.cpp
//
// Guards FProcessAgentSimulationDataRunnable::CalculateRotationFromMovement() and
// CalculateSpeedFromMovement(), which derive heading and speed for sources that carry positions only
// (every Juelich file, and Mobius-HDF5 written positions-only). Both were rewritten from a per-entity
// scan, O(entities x total samples), to one O(total samples) pass; no derived value may move by a bit.
//
// The oracle is the old scan itself, copied below from AgentDataSubsystem.cpp at 7dc1b050. Only how it
// reaches the map, MaxAgents and TimeBetweenSteps changed; logging, progress and cancellation were
// dropped. Every sample's Rotation and Speed get a unique sentinel first, so a write the old scan never
// made - or one it made that the new pass skips - shows up as a mismatch, not as an equal value.
//
//   ProjectMobius.SimData.DerivedMotionParity - real Juelich export + a hand-built edge-case map
//   Mobius.Timing.DerivedMotionScaling        - old vs new wall-clock at two sizes. Set
//                                               MOBIUS_DERIVED_MOTION_FILE to a positions-only file
//                                               to time a real import as well.
//
// Run: MobiusPerf\RunTests.ps1 -Filter "ProjectMobius.SimData.DerivedMotion+Mobius.Timing.DerivedMotion"
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"

namespace DerivedMotionOracle
{
	using FSimMap = TMap<int32, TArray<FSimMovementSample>>;

	// FProcessAgentSimulationDataRunnable::CalculateRotationFromMovement() as of 7dc1b050.
	void CalculateRotationFromMovement(FSimMap& SimulationData, const int32 MaxAgents)
	{
		for (int32 EntityIdx = 0; EntityIdx < MaxAgents; ++EntityIdx)
		{
			TArray<TPair<int32, FVector>> EntityPositions;

			for (auto& Pair : SimulationData)
			{
				for (const FSimMovementSample& Sample : Pair.Value)
				{
					if (Sample.EntityID == EntityIdx)
					{
						EntityPositions.Add(TPair<int32, FVector>(Pair.Key, Sample.Position));
						break;
					}
				}
			}

			if (EntityPositions.Num() < 2) continue;

			EntityPositions.Sort([](const TPair<int32, FVector>& A, const TPair<int32, FVector>& B)
			{
				return A.Key < B.Key;
			});

			FRotator LastValidRotation = FRotator::ZeroRotator;

			for (int32 i = 0; i < EntityPositions.Num(); ++i)
			{
				FRotator NewRotation = LastValidRotation;

				if (i < EntityPositions.Num() - 1)
				{
					FVector Delta = EntityPositions[i + 1].Value - EntityPositions[i].Value;

					if (!Delta.IsNearlyZero(0.1f))
					{
						float YawRad = FMath::Atan2(Delta.Y, Delta.X);
						float YawDeg = FMath::RadiansToDegrees(YawRad);

						NewRotation = FRotator(0.0f, YawDeg - 90.0f, 0.0f);
						LastValidRotation = NewRotation;
					}
				}

				int32 Timestep = EntityPositions[i].Key;
				for (FSimMovementSample& Sample : SimulationData[Timestep])
				{
					if (Sample.EntityID == EntityIdx)
					{
						Sample.Rotation = NewRotation;
						break;
					}
				}
			}
		}
	}

	// FProcessAgentSimulationDataRunnable::CalculateSpeedFromMovement() as of 7dc1b050.
	void CalculateSpeedFromMovement(FSimMap& SimulationData, const int32 MaxAgents, const float TimeBetweenSteps)
	{
		for (int32 EntityIdx = 0; EntityIdx < MaxAgents; ++EntityIdx)
		{
			TArray<TPair<int32, FVector>> EntityPositions;

			for (auto& Pair : SimulationData)
			{
				for (const FSimMovementSample& Sample : Pair.Value)
				{
					if (Sample.EntityID == EntityIdx)
					{
						EntityPositions.Add(TPair<int32, FVector>(Pair.Key, Sample.Position));
						break;
					}
				}
			}

			if (EntityPositions.Num() < 2) continue;

			EntityPositions.Sort([](const TPair<int32, FVector>& A, const TPair<int32, FVector>& B)
			{
				return A.Key < B.Key;
			});

			float LastValidSpeed = 0.0f;

			for (int32 i = 0; i < EntityPositions.Num(); ++i)
			{
				float Speed = LastValidSpeed;

				if (i < EntityPositions.Num() - 1)
				{
					FVector Delta = EntityPositions[i + 1].Value - EntityPositions[i].Value;
					float Distance = Delta.Size();

					Speed = Distance / (TimeBetweenSteps * 100.0f);
					LastValidSpeed = Speed;
				}

				int32 Timestep = EntityPositions[i].Key;
				for (FSimMovementSample& Sample : SimulationData[Timestep])
				{
					if (Sample.EntityID == EntityIdx)
					{
						Sample.Speed = Speed;
						break;
					}
				}
			}
		}
	}
}

// Named, not anonymous: helper names like DoubleBits/MakeSample already exist in other test files'
// anonymous namespaces, which would collide if this module is ever unity-built.
namespace DerivedMotionTestUtil
{
	using DerivedMotionOracle::FSimMap;

	const TCHAR* JuelichFixtureRelative = TEXT("MobiusDataImporter/Hdf5TestData/JuelichTestCases/trajectories_hdf5/030_c_56_h0.h5");

	uint64 DoubleBits(const double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	uint32 FloatBits(const float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	bool RotatorsBitEqual(const FRotator& A, const FRotator& B)
	{
		return DoubleBits(A.Pitch) == DoubleBits(B.Pitch) && DoubleBits(A.Yaw) == DoubleBits(B.Yaw) && DoubleBits(A.Roll) == DoubleBits(B.Roll);
	}

	bool VectorsBitEqual(const FVector& A, const FVector& B)
	{
		return DoubleBits(A.X) == DoubleBits(B.X) && DoubleBits(A.Y) == DoubleBits(B.Y) && DoubleBits(A.Z) == DoubleBits(B.Z);
	}

	/** A Speed no derivation can produce (negative) and that is unique per sample. */
	float SentinelSpeed(const int32 SampleOrdinal)
	{
		return -1.0f - static_cast<float>(SampleOrdinal);
	}

	/** Overwrite every Rotation/Speed with a per-sample sentinel, so "not written" is observable. */
	void StampSentinels(FSimMap& Map)
	{
		int32 SampleOrdinal = 0;
		for (TPair<int32, TArray<FSimMovementSample>>& Pair : Map)
		{
			for (FSimMovementSample& Sample : Pair.Value)
			{
				Sample.Rotation = FRotator(1000.0 + SampleOrdinal, 2000.0 + SampleOrdinal, 3000.0 + SampleOrdinal);
				Sample.Speed = SentinelSpeed(SampleOrdinal);
				++SampleOrdinal;
			}
		}
	}

	/** Runs the shipping derivation on Map through a bare runnable; Run() is never called. */
	void DeriveWithRunnable(const FString& AnyExistingFile, UAgentDataSubsystem* Owner, const TSharedPtr<FSimMap>& Map,
	                        const int32 MaxAgents, const float TimeBetweenSteps)
	{
		// The constructor only checks the file exists; nothing reads it unless Run() is called.
		FProcessAgentSimulationDataRunnable Runnable(AnyExistingFile, Owner, /*bAutoStartThread*/ false);
		Runnable.MaxAgents = MaxAgents;
		Runnable.TimeBetweenSteps = TimeBetweenSteps;
		Runnable.AgentSimulationData.Metadata.bHasRotationData = false;
		Runnable.AgentSimulationData.Metadata.bHasSpeedData = false;
		Runnable.AgentMovementInfoData.SimulationData = Map;
		Runnable.CalculateRotationFromMovement();
		Runnable.CalculateSpeedFromMovement();
	}

	void DeriveWithOracle(FSimMap& Map, const int32 MaxAgents, const float TimeBetweenSteps)
	{
		DerivedMotionOracle::CalculateRotationFromMovement(Map, MaxAgents);
		DerivedMotionOracle::CalculateSpeedFromMovement(Map, MaxAgents, TimeBetweenSteps);
	}

	struct FParity
	{
		int32 Samples = 0;
		/** Samples the oracle wrote (Speed no longer its sentinel). */
		int32 Derived = 0;
		int32 Mismatches = 0;
		FString FirstMismatch;
	};

	/** Bit-for-bit comparison of two derivations of the same sentinel-stamped source map. */
	FParity CompareDerivations(const FSimMap& Oracle, const FSimMap& Candidate)
	{
		FParity Result;
		auto NoteMismatch = [&Result](const FString& What)
		{
			if (Result.Mismatches++ == 0)
			{
				Result.FirstMismatch = What;
			}
		};

		if (Oracle.Num() != Candidate.Num())
		{
			NoteMismatch(FString::Printf(TEXT("timestep count %d vs %d"), Oracle.Num(), Candidate.Num()));
			return Result;
		}

		int32 SampleOrdinal = 0;
		for (const TPair<int32, TArray<FSimMovementSample>>& Pair : Oracle)
		{
			const TArray<FSimMovementSample>* CandidateSamples = Candidate.Find(Pair.Key);
			if (!CandidateSamples || CandidateSamples->Num() != Pair.Value.Num())
			{
				NoteMismatch(FString::Printf(TEXT("timestep %d missing or resized"), Pair.Key));
				SampleOrdinal += Pair.Value.Num();
				continue;
			}

			for (int32 Index = 0; Index < Pair.Value.Num(); ++Index, ++SampleOrdinal)
			{
				const FSimMovementSample& A = Pair.Value[Index];
				const FSimMovementSample& B = (*CandidateSamples)[Index];
				++Result.Samples;
				if (FloatBits(A.Speed) != FloatBits(SentinelSpeed(SampleOrdinal)))
				{
					++Result.Derived;
				}

				if (A.EntityID != B.EntityID || !VectorsBitEqual(A.Position, B.Position)
				    || !RotatorsBitEqual(A.Rotation, B.Rotation) || FloatBits(A.Speed) != FloatBits(B.Speed))
				{
					NoteMismatch(FString::Printf(TEXT("timestep %d sample %d entity %d: oracle yaw %.9g speed %.9g, candidate yaw %.9g speed %.9g"),
						Pair.Key, Index, A.EntityID, A.Rotation.Yaw, A.Speed, B.Rotation.Yaw, B.Speed));
				}
			}
		}
		return Result;
	}

	/** Oracle vs shipping derivation on copies of Source (which is sentinel-stamped first). */
	FParity RunParity(FSimMap Source, const int32 MaxAgents, const float TimeBetweenSteps,
	                  const FString& AnyExistingFile, UAgentDataSubsystem* Owner,
	                  double* OutOracleSeconds = nullptr, double* OutCandidateSeconds = nullptr)
	{
		StampSentinels(Source);
		FSimMap OracleMap = Source;
		TSharedPtr<FSimMap> CandidateMap = MakeShared<FSimMap>(MoveTemp(Source));

		const double OracleStart = FPlatformTime::Seconds();
		DeriveWithOracle(OracleMap, MaxAgents, TimeBetweenSteps);
		const double OracleSeconds = FPlatformTime::Seconds() - OracleStart;

		const double CandidateStart = FPlatformTime::Seconds();
		DeriveWithRunnable(AnyExistingFile, Owner, CandidateMap, MaxAgents, TimeBetweenSteps);
		const double CandidateSeconds = FPlatformTime::Seconds() - CandidateStart;

		if (OutOracleSeconds) { *OutOracleSeconds = OracleSeconds; }
		if (OutCandidateSeconds) { *OutCandidateSeconds = CandidateSeconds; }
		return CompareDerivations(OracleMap, *CandidateMap);
	}

	/** FNV-1a over every sample's derived fields in timestep order - comparable across builds. */
	uint64 DigestDerivedValues(const FSimMap& Map)
	{
		uint64 Hash = 0xcbf29ce484222325ull;
		auto Mix = [&Hash](const uint64 Value)
		{
			for (int32 Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= (Value >> (Byte * 8)) & 0xffull;
				Hash *= 0x100000001b3ull;
			}
		};

		TArray<int32> Timesteps;
		Map.GetKeys(Timesteps);
		Timesteps.Sort();
		for (const int32 Timestep : Timesteps)
		{
			for (const FSimMovementSample& Sample : Map.FindChecked(Timestep))
			{
				Mix(static_cast<uint64>(Timestep));
				Mix(static_cast<uint64>(static_cast<uint32>(Sample.EntityID)));
				Mix(DoubleBits(Sample.Rotation.Pitch));
				Mix(DoubleBits(Sample.Rotation.Yaw));
				Mix(DoubleBits(Sample.Rotation.Roll));
				Mix(FloatBits(Sample.Speed));
			}
		}
		return Hash;
	}

	FSimMovementSample MakeDerivedMotionSample(const int32 EntityId, const double X, const double Y)
	{
		FSimMovementSample Sample;
		Sample.EntityID = EntityId;
		Sample.Position = FVector(X, Y, 0.0);
		return Sample;
	}

	/** Pins both .msc cache cvars off, so an import always parses and derives, and never writes a cache. */
	struct FScopedSimCacheOff
	{
		IConsoleVariable* WriteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
		IConsoleVariable* FastReloadCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.FastReload"));
		int32 SavedWrite = WriteCVar ? WriteCVar->GetInt() : 1;
		int32 SavedFastReload = FastReloadCVar ? FastReloadCVar->GetInt() : 1;

		FScopedSimCacheOff()
		{
			if (WriteCVar) { WriteCVar->Set(0, ECVF_SetByCode); }
			if (FastReloadCVar) { FastReloadCVar->Set(0, ECVF_SetByCode); }
		}

		~FScopedSimCacheOff()
		{
			if (WriteCVar) { WriteCVar->Set(SavedWrite, ECVF_SetByCode); }
			if (FastReloadCVar) { FastReloadCVar->Set(SavedFastReload, ECVF_SetByCode); }
		}
	};

	/**
	 * Positions-only map shaped like an evacuation: agents enter and leave at staggered times, so
	 * timestep arrays vary in length and each agent sits at a different index in each one.
	 */
	FSimMap MakeSyntheticPositionsOnly(const int32 NumAgents, const int32 NumTimesteps)
	{
		FSimMap Map;
		for (int32 Timestep = 0; Timestep < NumTimesteps; ++Timestep)
		{
			TArray<FSimMovementSample>& Samples = Map.Add(Timestep);
			for (int32 Agent = 0; Agent < NumAgents; ++Agent)
			{
				const int32 Enter = (Agent * 37) % (NumTimesteps / 3);
				const int32 Leave = Enter + NumTimesteps / 2 + (Agent * 53) % (NumTimesteps / 2);
				if (Timestep < Enter || Timestep >= Leave)
				{
					continue;
				}
				const double Heading = Agent * 0.7;
				const double Walked = 12.0 * (Timestep - Enter);
				Samples.Add(MakeDerivedMotionSample(Agent, 50.0 * Agent + Walked * FMath::Cos(Heading), Walked * FMath::Sin(Heading)));
			}
		}
		return Map;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDerivedMotionParityTest,
	"ProjectMobius.SimData.DerivedMotionParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDerivedMotionParityTest::RunTest(const FString& Parameters)
{
	using namespace DerivedMotionTestUtil;

	const FString JuelichPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectPluginsDir(), JuelichFixtureRelative));
	if (!TestTrue(FString::Printf(TEXT("Juelich fixture present (%s)"), *JuelichPath), FPaths::FileExists(JuelichPath)))
	{
		return false;
	}

	UAgentDataSubsystem* Owner = NewObject<UAgentDataSubsystem>();

	// ---------- 1. Real Juelich export through the real import ----------
	{
		FScopedSimCacheOff CacheOff;
		FProcessAgentSimulationDataRunnable Import(JuelichPath, Owner, /*bAutoStartThread*/ false);
		Import.Run();

		// A cache hit would skip derivation altogether and make every comparison below trivially true.
		TestFalse(TEXT("import parsed the source, not a cache"), Import.ImportTimings.bUsedFastReload);
		TestEqual(TEXT("file format"), static_cast<int32>(Import.AgentFileFormat), static_cast<int32>(EMobiusAgentFileFormat::JuelichHdf5));
		TestTrue(TEXT("import ran the derivation"), Import.ImportTimings.DeriveSeconds > 0.0);
		TestTrue(TEXT("Juelich has agents"), Import.MaxAgents > 0);

		const FSimMap& Imported = *Import.AgentMovementInfoData.SimulationData;
		AddInfo(FString::Printf(TEXT("Juelich 030_c_56_h0: %d agents, %d timesteps, TimeBetweenSteps %.6f s, derive %.4f s; derived-value digest 0x%016llx"),
			Import.MaxAgents, Imported.Num(), Import.TimeBetweenSteps, Import.ImportTimings.DeriveSeconds, DigestDerivedValues(Imported)));

		const FParity Parity = RunParity(Imported, Import.MaxAgents, Import.TimeBetweenSteps, JuelichPath, Owner);
		TestTrue(TEXT("Juelich: oracle derived something"), Parity.Derived > 0);
		TestEqual(FString::Printf(TEXT("Juelich: bit-identical to the old scan (%d samples, %d derived; first mismatch: %s)"),
			Parity.Samples, Parity.Derived, *Parity.FirstMismatch), Parity.Mismatches, 0);
	}

	// ---------- 2. Edge cases a clean export never hits ----------
	{
		const int32 MaxAgents = 6;
		const float TimeBetweenSteps = 0.1f;

		// Keys inserted out of order: both derivations must walk timesteps by key, not by insertion.
		FSimMap Map;
		Map.Add(3, TArray<FSimMovementSample>{
			MakeDerivedMotionSample(3, 400.0, 0.0),
			MakeDerivedMotionSample(0, 30.0, 30.0),
			MakeDerivedMotionSample(1, 100.0, 0.0),       // first move of a stationary-from-start agent: +X, yaw -90
			MakeDerivedMotionSample(6, 1.0, 1.0),         // id == MaxAgents: never derived
		});
		Map.Add(0, TArray<FSimMovementSample>{
			MakeDerivedMotionSample(0, 0.0, 0.0),
			MakeDerivedMotionSample(1, 0.0, 0.0),
			MakeDerivedMotionSample(3, 0.0, 0.0),
			MakeDerivedMotionSample(4, 0.0, 0.0),
			MakeDerivedMotionSample(-1, 5.0, 5.0),        // negative id: never derived
			MakeDerivedMotionSample(7, 2.0, 2.0),         // id > MaxAgents: never derived
		});
		Map.Add(4, TArray<FSimMovementSample>());   // empty timestep
		Map.Add(2, TArray<FSimMovementSample>{
			MakeDerivedMotionSample(2, 9.0, 9.0),         // the only sample of entity 2: never derived
			MakeDerivedMotionSample(0, 20.0, 20.0),
			MakeDerivedMotionSample(1, 0.0, 0.0),
		});
		Map.Add(1, TArray<FSimMovementSample>{
			MakeDerivedMotionSample(1, 0.0, 0.0),
			MakeDerivedMotionSample(3, 100.0, 0.0),
			MakeDerivedMotionSample(0, 10.0, 10.0),
			MakeDerivedMotionSample(3, -500.0, -500.0),   // second sample of entity 3 in one timestep: only the first counts
			MakeDerivedMotionSample(6, 3.0, 3.0),
		});
		Map.Add(5, TArray<FSimMovementSample>{
			MakeDerivedMotionSample(1, 100.0, 0.05),      // sub-0.1 cm step along +Y: heading holds at -90, not 0
			MakeDerivedMotionSample(4, 0.0, 300.0),       // entity 4 skipped timesteps 1-4
			MakeDerivedMotionSample(0, 60.0, 30.0),       // last move +X: yaw -90, carried onto this final frame
		});

		const FParity Parity = RunParity(Map, MaxAgents, TimeBetweenSteps, JuelichPath, Owner);
		TestEqual(FString::Printf(TEXT("edge cases: bit-identical to the old scan (%d samples, %d derived; first mismatch: %s)"),
			Parity.Samples, Parity.Derived, *Parity.FirstMismatch), Parity.Mismatches, 0);
		// 0: 5 samples, 1: 5, 3: 3 (duplicate excluded), 4: 2. Entities 2, 5 and the out-of-range ids: none.
		TestEqual(TEXT("edge cases: exactly the expected samples were derived"), Parity.Derived, 15);

		// The parity check above proves "same as before"; these pin down what "before" was.
		TSharedPtr<FSimMap> Derived = MakeShared<FSimMap>(Map);
		StampSentinels(*Derived);
		DeriveWithRunnable(JuelichPath, Owner, Derived, MaxAgents, TimeBetweenSteps);
		const FSimMovementSample& StationaryStart = (*Derived)[0][1];
		TestTrue(TEXT("stationary-from-start agent holds yaw 0 (not the importer's -90)"), RotatorsBitEqual(StationaryStart.Rotation, FRotator::ZeroRotator));
		TestEqual(TEXT("stationary-from-start agent speed 0"), StationaryStart.Speed, 0.0f);
		const FSimMovementSample& Duplicate = (*Derived)[1][3];
		TestTrue(TEXT("duplicate sample left untouched"), Duplicate.Speed < 0.0f);
		TestTrue(TEXT("single-sample entity left untouched"), (*Derived)[2][0].Speed < 0.0f);
		TestTrue(TEXT("negative id left untouched"), (*Derived)[0][4].Speed < 0.0f);
		TestTrue(TEXT("id > MaxAgents left untouched"), (*Derived)[0][5].Speed < 0.0f);
		TestTrue(TEXT("id == MaxAgents left untouched"), (*Derived)[3][3].Speed < 0.0f && (*Derived)[1][4].Speed < 0.0f);
		TestEqual(TEXT("look-ahead speed = |next - this| cm / (dt * 100)"), (*Derived)[0][0].Speed,
			static_cast<float>(FVector(10.0, 10.0, 0.0).Size()) / (TimeBetweenSteps * 100.0f));
		TestEqual(TEXT("last frame carries the previous heading"), (*Derived)[5][2].Rotation.Yaw, -90.0);
		TestTrue(TEXT("last frame carries the previous heading (bitwise)"), RotatorsBitEqual((*Derived)[5][2].Rotation, (*Derived)[3][1].Rotation));
		TestEqual(TEXT("sub-threshold step holds the heading"), (*Derived)[3][2].Rotation.Yaw, -90.0);
		TestTrue(TEXT("sub-threshold step holds the heading (bitwise)"), RotatorsBitEqual((*Derived)[3][2].Rotation, (*Derived)[2][2].Rotation));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDerivedMotionScalingTimingTest,
	"Mobius.Timing.DerivedMotionScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDerivedMotionScalingTimingTest::RunTest(const FString& Parameters)
{
	using namespace DerivedMotionTestUtil;

	const FString JuelichPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectPluginsDir(), JuelichFixtureRelative));
	UAgentDataSubsystem* Owner = NewObject<UAgentDataSubsystem>();
	const float TimeBetweenSteps = 0.1f;

	// Doubling the agents doubles the samples: the old scan should grow ~4x, the new pass ~2x.
	const int32 NumTimesteps = 1000;
	const int32 AgentCounts[] = { 500, 1000 };
	double OracleSeconds[2] = {};
	double CandidateSeconds[2] = {};
	for (int32 Size = 0; Size < 2; ++Size)
	{
		const FSimMap Map = MakeSyntheticPositionsOnly(AgentCounts[Size], NumTimesteps);
		const FParity Parity = RunParity(Map, AgentCounts[Size], TimeBetweenSteps, JuelichPath, Owner, &OracleSeconds[Size], &CandidateSeconds[Size]);
		TestEqual(FString::Printf(TEXT("%d agents: bit-identical (first mismatch: %s)"), AgentCounts[Size], *Parity.FirstMismatch), Parity.Mismatches, 0);
		AddInfo(FString::Printf(TEXT("%d agents x %d timesteps (%d samples): old scan %.3f s, new pass %.4f s, %.0fx"),
			AgentCounts[Size], NumTimesteps, Parity.Samples, OracleSeconds[Size], CandidateSeconds[Size],
			CandidateSeconds[Size] > 0.0 ? OracleSeconds[Size] / CandidateSeconds[Size] : 0.0));
	}
	AddInfo(FString::Printf(TEXT("2x agents -> old scan %.1fx slower, new pass %.1fx slower"),
		OracleSeconds[0] > 0.0 ? OracleSeconds[1] / OracleSeconds[0] : 0.0,
		CandidateSeconds[0] > 0.0 ? CandidateSeconds[1] / CandidateSeconds[0] : 0.0));

	// Loose gate, as in MobiusTimingTests: observed is orders of magnitude, so background load can't flake it.
	TestTrue(FString::Printf(TEXT("new pass at least 10x the old scan at %d agents (%.4f s vs %.3f s)"), AgentCounts[1], CandidateSeconds[1], OracleSeconds[1]),
		CandidateSeconds[1] * 10.0 <= OracleSeconds[1]);

	// Optional: a real positions-only file, through the real import first.
	const FString FilePath = FPlatformMisc::GetEnvironmentVariable(TEXT("MOBIUS_DERIVED_MOTION_FILE"));
	if (FilePath.IsEmpty())
	{
		AddInfo(TEXT("MOBIUS_DERIVED_MOTION_FILE not set - real-file timing skipped"));
		return true;
	}
	if (!TestTrue(FString::Printf(TEXT("MOBIUS_DERIVED_MOTION_FILE exists (%s)"), *FilePath), FPaths::FileExists(FilePath)))
	{
		return false;
	}

	FScopedSimCacheOff CacheOff;
	FProcessAgentSimulationDataRunnable Import(FilePath, Owner, /*bAutoStartThread*/ false);
	Import.Run();
	TestFalse(TEXT("file import parsed the source, not a cache"), Import.ImportTimings.bUsedFastReload);
	TestFalse(TEXT("file is positions-only (no rotation)"), Import.AgentSimulationData.Metadata.bHasRotationData);
	TestFalse(TEXT("file is positions-only (no speed)"), Import.AgentSimulationData.Metadata.bHasSpeedData);

	const FMobiusImportTimings& Timings = Import.ImportTimings;
	const FSimMap& Imported = *Import.AgentMovementInfoData.SimulationData;
	double FileOracleSeconds = 0.0;
	double FileCandidateSeconds = 0.0;
	const FParity Parity = RunParity(Imported, Import.MaxAgents, Import.TimeBetweenSteps, FilePath, Owner, &FileOracleSeconds, &FileCandidateSeconds);
	TestEqual(FString::Printf(TEXT("file: bit-identical (first mismatch: %s)"), *Parity.FirstMismatch), Parity.Mismatches, 0);
	AddInfo(FString::Printf(TEXT("%s: %d agents, %d timesteps, %d samples. Import %.2f s (parse %.2f, convert %.2f, derive %.3f, brackets %.2f). Derivation on the imported map: old scan %.2f s, new pass %.3f s, %.0fx"),
		*FPaths::GetCleanFilename(FilePath), Import.MaxAgents, Imported.Num(), Parity.Samples,
		Timings.TotalSeconds, Timings.ParseSeconds, Timings.ConvertSeconds, Timings.DeriveSeconds, Timings.BracketSeconds,
		FileOracleSeconds, FileCandidateSeconds, FileCandidateSeconds > 0.0 ? FileOracleSeconds / FileCandidateSeconds : 0.0));
	return true;
}

#endif // !UE_BUILD_SHIPPING
