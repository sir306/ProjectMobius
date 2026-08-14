// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryHeatmapCalibrationTest.cpp
//
// COMPLETE REWRITE (trajectory heatmap rebuild, T5). The previous version of this file asserted
// properties of the OLD implementation: seed byte 25, +1 per stamp, HitsPerPassInterior = 2R+1, disc-rim
// falloff, byte edges [24.5, 46.5, 71.5, 110.5, 175.5]. Every one of those was a property of a uint8
// accumulation buffer that no longer exists. This file asserts properties of the PHYSICS instead, against
// FTrajectoryField (Visualization/Public/TrajectoryField.h), which is deliberately plain C++ so it is
// constructible and assertable here with no world, no RHI, no dataset.
//
// PROVENANCE. Every expected number below is transcribed from A1's hand-derived oracle:
//   _CurrentHandoff/trajectoryFix/tasks/oracle/vectors_dda.md + vectors_dda.csv
//   _CurrentHandoff/trajectoryFix/tasks/oracle/vectors_semantics.md
//   _CurrentHandoff/trajectoryFix/tasks/oracle/vectors_kernel.md
// A1 derived these from physics and the spec alone, never from FTrajectoryField.cpp -- and this file's
// author (A5) has likewise never read FTrajectoryField.cpp. If a number here and the implementation ever
// disagree, THIS FILE IS RIGHT and the implementation has a bug; do not "fix" a failing assertion by
// tightening it to match observed behaviour.
//
// RATIFIED CONVENTIONS IN FORCE (from A0, overriding places the oracle prose offered alternatives):
//   - Boundary ownership: LOWER-index cell owns a grid line (matches TrajectoryField.h's own
//     AxisIndexFromCoord doc). Only the CSV's T-DDA-4b rows are used; T-DDA-4a rows are never asserted.
//   - Teleport rejection (T-DDA-7b, canonical): books to the REJECTED bucket (GetRejectedPersonMetres/
//     Seconds/TeleportCount). vectors_dda.md's prose calls this "Dropped" -- that is a labelling
//     difference in the oracle text, not a numeric one; TrajectoryField.h's four-bucket audit identity
//     is explicit that a validity-gate refusal is Rejected, never folded into Dropped, so this file
//     follows the header's bucket name over the oracle's prose label. The NUMBERS (40.0 m, 0.016 s) are
//     unchanged either way.
//
// TOLERANCES. Every per-case tolerance below is transcribed from the real
// _CurrentHandoff/trajectoryFix/tasks/oracle/TOLERANCES.md §10 "Master table" (written by a fresh,
// implementation-firewalled oracle, A1b, after the original A1 was killed before finishing it -- an
// earlier revision of this file used PROVISIONAL placeholders while that file did not exist; it now does
// and this revision uses its real numbers throughout). §11 "Corrections" is applied too: T-CONV-3 is
// tightened to 1e-5 (not 1e-4) and gets an added per-cell BORDER assertion (the sum alone cannot tell
// renormalise from clamp -- both preserve Σ Presentation = 1.0); T-WIDTH-2(b)'s ratio uses 1e-5, not the
// 1e-6 vectors_kernel.md §3 originally stated (not robust against the float32 normaliser-sum error); the
// vectors_kernel.md §4 clamp-fold numbers (which implied Σ=1.366) are corrected to Σ=1.0 and are not
// encoded here since nothing in this file asserts the wrong (clamp) behaviour, only the correct
// (renormalise) one. "Form E" cases (§1.5's exactness taxonomy) assert bitwise/exact with NO epsilon:
// T-DDA-5a/5b, T-DDA-6 (these operands only), T-SEM-1 assertion 1, T-SEM-3, T-WIDTH-2(a), T-CLR-1,
// T-DET-1. T-DDA-8 needs an absolute floor (2.43e-7 m) alongside its relative tolerance because its
// slivers are ~2.5e-5 relative and a pure-relative check misfires on them.
//
// Run: UnrealEditor ProjectMobius.uproject -ExecCmds="Automation RunTests ProjectMobius.Heatmap.Trajectory" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "TrajectoryField.h"
// For the two public static mesh-sizing helpers that T-OFFSET-2 asserts against. Header only -- this
// test never spawns the actor, so it stays a Tier A case with no world.
#include "Actors/HeatmapPixelTextureVisualizer.h"
// For UDynamicPixelRenderingTexture::BandColourForRedValue -- the static the T-BAND cases below drive to
// assert which COLOUR a crossing count actually renders. The header pulls OpenCV, which is why
// HeatmapLOSBands.h deliberately does not; that is fine here, and TrajectoryHeatmapRealDatasetTest.cpp
// already includes it from this same module. Still no world and no RHI, so these stay Tier A.
#include "DynamicPixelRenderingTexture.h"
#if WITH_EDITOR
// The blur gate inspects the material graph, which is editor-only data.
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
// SamplerSource is declared on the SAMPLE expression, not on the texture base (which carries only Texture
// and SamplerType) - UE 5.5: Engine/Public/Materials/MaterialExpressionTextureSample.h:57.
#include "Materials/MaterialExpressionTextureSample.h"
#endif

namespace TrajectoryOracle
{
	// --- Tolerance policy, from TOLERANCES.md §10 "Master table" (ID -> form/REL/ABS/verdict) ----------
	// Every constant is named here, once, and every call site below references one of these -- none are
	// inlined. "Form E" = exact/bitwise, no epsilon at all (use 0.0 or a direct == / memcmp instead of these).
	constexpr double RelTol1e6 = 1.0e-6;          // T-CONV-4/ACC, T-DDA-1/2/3/4, T-SEM-1(3,4), T-WIDTH-1, T-RES-2
	constexpr double RelTol1e5 = 1.0e-5;          // T-CONV-3 (tightened), T-SEM-2 linearity, T-WIDTH-2(b), T-RES-1
	constexpr double RelTol1e3Contract = 1.0e-3;  // T-CONV-1/2 "contract" level (the 1e-6 "flag" level is RelTol1e6)
	constexpr double RelTol1e4Order = 1.0e-4;     // T-SAT-1 (D + order, strict-monotone-safe at k <= 1e3)

	// Absolute floors, expressed as "coefficient x magnitude" per the master table's own recipe.
	constexpr double AbsCoeff2e7 = 2.0e-7;        // "achievable" per-value floor used across most D/C forms
	constexpr double AbsCoeff4e7 = 4.0e-7;        // T-CONV-4's balance check specifically
	constexpr double Dda8AbsFloorMetres = 2.43e-7;  // T-DDA-8: slivers are ~2.5e-5 rel, need this ABS floor too
	constexpr double Dda8AbsFloorSeconds = 1.2e-7;
	constexpr double Sem2LinearityAbsSeconds = 2.0e-6; // T-SEM-2 linearity (100x0.1s == one 10s), NOT exactness

	static bool NearlyEqualAbs(double A, double B, double AbsTol)
	{
		return FMath::Abs(A - B) <= AbsTol;
	}

	static bool NearlyEqualRel(double A, double B, double RelTol)
	{
		const double Scale = FMath::Max(FMath::Abs(A), FMath::Abs(B));
		return FMath::Abs(A - B) <= RelTol * FMath::Max(Scale, 1.0e-12);
	}

	/** REL-or-ABS-floor hybrid: passes if within RelTol relative OR within the absolute floor. Needed
	 * wherever a small sliver value's relative error looks large but its absolute error is still tiny
	 * (T-DDA-8's master-table row is the case this exists for). */
	static bool NearlyEqualHybrid(double A, double B, double RelTol, double AbsFloor)
	{
		return NearlyEqualRel(A, B, RelTol) || NearlyEqualAbs(A, B, AbsFloor);
	}

	/** "2e-7 x ΣL"-style recipes from the master table: tolerance scales with the expected magnitude. */
	static double Coeff(double Coefficient, double Magnitude)
	{
		return Coefficient * FMath::Max(FMath::Abs(Magnitude), 1.0e-6);
	}

	static FTrajectoryFieldConfig MakeConfig(float CmPerTexel, float DisplayPathWidthCm = 20.0f)
	{
		FTrajectoryFieldConfig Config;
		Config.WorldCmPerTexel = CmPerTexel;
		Config.DisplayPathWidthCm = DisplayPathWidthCm;
		return Config;
	}

	static FTrajectoryField MakeField(double ExtentXCm, double ExtentYCm, float CmPerTexel,
		float DisplayPathWidthCm = 20.0f, double OriginXCm = 0.0, double OriginYCm = 0.0)
	{
		FTrajectoryField Field;
		Field.Initialise(FVector2D(ExtentXCm, ExtentYCm), FVector2D(OriginXCm, OriginYCm),
			MakeConfig(CmPerTexel, DisplayPathWidthCm));
		return Field;
	}

	struct FExpectedCell
	{
		int32 X = 0;
		int32 Y = 0;
		double PersonMetres = 0.0;
		double PersonSeconds = 0.0;
	};

	/**
	 * Checks each listed cell's canonical PersonMetres/PersonSeconds against expectation, as a hybrid of
	 * relative tolerance and an absolute floor (TOLERANCES.md's own recipe: most rows are a REL figure
	 * PLUS an ABS floor derived from the case's own magnitude, because a pure relative check is
	 * meaningless against an expected value of exactly zero and misfires on the small slivers T-DDA-8 is
	 * built around). Defaults are the master table's single most common row (1e-6 rel); callers pass the
	 * per-case AbsFloor* explicitly where the table gives a case-specific number.
	 */
	static void CheckCells(FAutomationTestBase& Test, const FTrajectoryField& Field, const TCHAR* CaseId,
		const TArray<FExpectedCell>& Expected, double RelTol = RelTol1e6,
		double AbsFloorMetres = 1.0e-7, double AbsFloorSeconds = 1.0e-7)
	{
		const TArray<float>& Metres = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
		const TArray<float>& Seconds = Field.GetCanonical(ETrajectoryMapMode::RouteExposure);
		const FIntPoint Dims = Field.GetGridDims();
		for (const FExpectedCell& E : Expected)
		{
			const int32 Index = E.Y * Dims.X + E.X;
			if (!Test.TestTrue(FString::Printf(TEXT("%s: cell (%d,%d) index in range"), CaseId, E.X, E.Y),
					Metres.IsValidIndex(Index)))
			{
				continue;
			}
			Test.TestTrue(
				FString::Printf(TEXT("%s: cell (%d,%d) PersonMetres %.10f ~= %.10f"), CaseId, E.X, E.Y,
					(double)Metres[Index], E.PersonMetres),
				NearlyEqualHybrid((double)Metres[Index], E.PersonMetres, RelTol, AbsFloorMetres));
			Test.TestTrue(
				FString::Printf(TEXT("%s: cell (%d,%d) PersonSeconds %.10f ~= %.10f"), CaseId, E.X, E.Y,
					(double)Seconds[Index], E.PersonSeconds),
				NearlyEqualHybrid((double)Seconds[Index], E.PersonSeconds, RelTol, AbsFloorSeconds));
		}
	}

	/**
	 * Form E (TOLERANCES.md §1.5): exact, no epsilon — but exact **in the storage type**.
	 *
	 * A0 fix: the cells are float32, so the tightest honest expectation is `float32(oracle value)`, not the
	 * double literal. 0.05 is not representable in binary32; the nearest float32 is 0.05000000074505806.
	 * Comparing the stored float against the double 0.05 with zero tolerance can therefore NEVER pass, and
	 * that is a defect in the assertion rather than in the field — it is exactly what made T-DDA-6 fail on
	 * the first Tier A run while the deposited values were in fact perfect. Rounding the expectation to
	 * float32 first is what makes the word "bitwise" mean something here. Dyadic expectations (0.5, 10.0,
	 * 0.25) are unaffected, because rounding them to float32 is the identity.
	 */
	static void CheckCellsExact(FAutomationTestBase& Test, const FTrajectoryField& Field, const TCHAR* CaseId,
		const TArray<FExpectedCell>& Expected)
	{
		TArray<FExpectedCell> AsStored = Expected;
		for (FExpectedCell& E : AsStored)
		{
			E.PersonMetres = static_cast<double>(static_cast<float>(E.PersonMetres));
			E.PersonSeconds = static_cast<double>(static_cast<float>(E.PersonSeconds));
		}
		CheckCells(Test, Field, CaseId, AsStored, /*RelTol*/ 0.0, /*AbsFloorMetres*/ 0.0, /*AbsFloorSeconds*/ 0.0);
	}

	static double SumArray(const TArray<float>& Values)
	{
		double Total = 0.0;
		for (const float V : Values)
		{
			Total += (double)V;
		}
		return Total;
	}

	/**
	 * Sum over the float32 CELLS — the only conservation reduction that actually tests the deposition.
	 *
	 * A0/A8: every "Sum PersonMetres" assertion in this file used to read GetTotalPersonMetres(), which is
	 * the implementation's OWN double counter, booked in DepositSegment from the clip result BEFORE the DDA
	 * walk runs. Nothing reconciled it against the arrays, so those assertions passed unchanged with both
	 * of DepositCell's `+=` lines deleted — i.e. with no mass deposited into any cell at all. That is the
	 * defect class this whole run exists to prevent: a green test that cannot fail.
	 *
	 * TOLERANCES.md §1.3 also requires exactly this form: reduce in double while iterating the stored
	 * float32 cells. Where a test still checks the counter, it must ALSO check counter == cell sum.
	 */
	static double SumCanonical(const FTrajectoryField& Field, ETrajectoryMapMode Mode)
	{
		return SumArray(Field.GetCanonical(Mode));
	}
}

// =====================================================================================================
// CONSERVATION
// =====================================================================================================

// T-CONV-1 / T-CONV-2 -- five mixed-angle segments (S1..S5, vectors_dda.md §2). Two identities over one
// segment set: Σ PersonMetres == Σ L, and Σ PersonSeconds == Σ Δt. All five speeds are well under the
// 20 m/s plausibility cap, so nothing here is expected to be rejected.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleConv12Test,
	"ProjectMobius.Heatmap.Trajectory.Conservation.T_CONV_1_2_MixedAngleSum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleConv12Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);

	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);      // S1
	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 25), 0.5f);     // S2
	Field.DepositSegment(FVector2D(5, 15), FVector2D(5, 55), 0.8f);     // S3
	Field.DepositSegment(FVector2D(1, 2), FVector2D(121, 22), 0.6f);    // S4 (== T-DDA-8)
	Field.DepositSegment(FVector2D(38, 31), FVector2D(12, 57), 0.55f);  // S5

	// T-CONV-1/2 row gives TWO REL levels: 1e-3 "contract" (loose, what a consumer may rely on) and 1e-6
	// "flag" (tight, catches a real regression -- "achievable at 3e-7; stated bound 3.4e3x loose"). Both
	// are asserted: the flag level is the one that actually matters, the contract level documents the
	// looser number a downstream consumer is allowed to assume.
	// A0/A8 fix: these reduce over the CELLS, not over the field's own counters. Summing
	// GetTotalPersonMetres() tested nothing about deposition - see SumCanonical's note.
	const double CellMetres = SumCanonical(Field, ETrajectoryMapMode::RouteUsage);
	const double CellSeconds = SumCanonical(Field, ETrajectoryMapMode::RouteExposure);

	TestTrue(TEXT("T-CONV-1: Sum PersonMetres CELLS == Sum L (contract level, REL 1e-3)"),
		NearlyEqualRel(CellMetres, 2.4670907448, RelTol1e3Contract));
	TestTrue(TEXT("T-CONV-1: Sum PersonMetres CELLS == Sum L (flag level, REL 1e-6)"),
		NearlyEqualHybrid(CellMetres, 2.4670907448, RelTol1e6, Coeff(AbsCoeff2e7, 2.4670907448)));
	TestTrue(TEXT("T-CONV-2: Sum PersonSeconds CELLS == Sum Δt (flag level, REL 1e-6)"),
		NearlyEqualHybrid(CellSeconds, 2.85, RelTol1e6, Coeff(AbsCoeff2e7, 2.85)));

	// The counters are still worth asserting - but as a SEPARATE claim, paired against the arrays, so a
	// divergence between "what the field says it deposited" and "what is actually in the cells" fails
	// loudly instead of being the only thing measured.
	TestTrue(TEXT("counter GetTotalPersonMetres agrees with the cell sum"),
		NearlyEqualHybrid(Field.GetTotalPersonMetres(), CellMetres, RelTol1e6, Coeff(AbsCoeff2e7, 2.4670907448)));
	TestTrue(TEXT("counter GetTotalPersonSeconds agrees with the cell sum"),
		NearlyEqualHybrid(Field.GetTotalPersonSeconds(), CellSeconds, RelTol1e6, Coeff(AbsCoeff2e7, 2.85)));

	TestEqual(TEXT("nothing rejected (all speeds < 20 m/s)"), Field.GetRejectedSegmentCount(), 0);
	TestTrue(TEXT("nothing dropped (grid comfortably contains all five)"),
		NearlyEqualAbs(Field.GetDroppedPersonMetres(), 0.0, 1.0e-7));
	return true;
}

// T-CONV-3 -- the widening-kernel conservation guard: Σ Presentation == Σ Canonical, REL 1e-5 (tightened
// from the original 1e-4). TOLERANCES.md §11.1(2) is explicit that the SUM ALONE cannot tell renormalise
// from clamp -- both preserve Σ Presentation = 1.0 -- so a per-cell BORDER assertion is required too;
// without it this test would pass a build that brightens every wall line (clamp-to-edge). Two sub-cases:
// an ISOLATED INTERIOR cell (the sum-only guard, uncomplicated by any border effect) and mass in the
// grid's own corner cell (the per-cell renormalisation check the sum cannot substitute for).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleConv3Test,
	"ProjectMobius.Heatmap.Trajectory.Conservation.T_CONV_3_PresentationEqualsCanonical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleConv3Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	// Interior sub-case: T-DDA-3's exact segment translated by +300/+300 cm. Translation preserves
	// length/frac/mass exactly (pure geometry, not derived from the implementation) while moving it to
	// cell (30,30), 30 cells clear of every edge -- comfortably outside even the R=2.5-texel kernel
	// footprint used elsewhere, so no border renormalisation can confound this half of the check.
	{
		FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
		Field.DepositSegment(FVector2D(302, 303), FVector2D(307, 308), 0.1f);

		const double Canonical = SumCanonical(Field, ETrajectoryMapMode::RouteUsage); // == 0.0707106781187 (T-DDA-3's L)
		TestTrue(TEXT("sanity: deposit landed with T-DDA-3's L"),
			NearlyEqualHybrid(Canonical, 0.0707106781187, RelTol1e6, Coeff(AbsCoeff2e7, 0.0707106781187)));

		const double Presentation = SumArray(Field.GetPresentation(ETrajectoryMapMode::RouteUsage));
		TestTrue(TEXT("T-CONV-3 interior: Sum Presentation == Sum Canonical"),
			NearlyEqualRel(Presentation, Canonical, RelTol1e5));
	}

	// Border sub-case (the check the sum alone cannot substitute for): mass in the grid's own corner cell
	// (0,0), R = 1.0 texel. The renormalised per-cell weights (vectors_kernel.md §4, corrected numbers per
	// TOLERANCES.md §11.1(1): the clamp-fold alternative sums to 1.0 too, not 1.366 as originally written).
	{
		FTrajectoryField Field = MakeField(100.0, 100.0, 10.0f, 20.0f);
		Field.DepositSegment(FVector2D(2, 3), FVector2D(7, 8), 0.1f); // T-DDA-3, lands in corner cell (0,0)

		const double M = SumCanonical(Field, ETrajectoryMapMode::RouteUsage);
		const TArray<float>& Presentation = Field.GetPresentation(ETrajectoryMapMode::RouteUsage);
		const FIntPoint Dims = Field.GetGridDims();
		auto At = [&](int32 X, int32 Y) { return (double)Presentation[Y * Dims.X + X]; };

		TestTrue(TEXT("T-CONV-3 border: (0,0) renormalised weight 0.502005603 (not the clamp-fold 0.634076361)"),
			NearlyEqualRel(At(0, 0) / M, 0.502005603, RelTol1e5));
		TestTrue(TEXT("T-CONV-3 border: (1,0) renormalised weight 0.229221520 (not the clamp-fold 0.170422528)"),
			NearlyEqualRel(At(1, 0) / M, 0.229221520, RelTol1e5));
		TestTrue(TEXT("T-CONV-3 border: (0,1) renormalised weight 0.229221520"),
			NearlyEqualRel(At(0, 1) / M, 0.229221520, RelTol1e5));
		TestTrue(TEXT("T-CONV-3 border: (1,1) renormalised weight 0.039551358"),
			NearlyEqualRel(At(1, 1) / M, 0.039551358, RelTol1e5));
		TestTrue(TEXT("T-CONV-3 border: Sum Presentation == Sum Canonical even at the border"),
			NearlyEqualRel(SumArray(Presentation), M, RelTol1e5));
	}
	return true;
}

// T-CONV-4 -- a segment 60% inside a 100x100 cm (10x10 cell) grid, 40% outside. Grid-AABB clipping is a
// DIFFERENT mechanism from the per-floor End-validity filter (FILE_MAP §3): the retained interval is
// deposited, the complement goes to Dropped, never Rejected.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleConv4Test,
	"ProjectMobius.Heatmap.Trajectory.Conservation.T_CONV_4_GridClipSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleConv4Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(100.0, 100.0, 10.0f);
	Field.DepositSegment(FVector2D(40, 5), FVector2D(140, 5), 1.0f);

	// T-CONV-4 row: C+D, REL 1e-6, ABS 2e-7*L(cells) -- CheckCells' defaults already match this exactly.
	CheckCells(*this, Field, TEXT("T-CONV-4"),
		{
			{4, 0, 0.1, 0.1}, {5, 0, 0.1, 0.1}, {6, 0, 0.1, 0.1},
			{7, 0, 0.1, 0.1}, {8, 0, 0.1, 0.1}, {9, 0, 0.1, 0.1},
		});

	const double BalanceAbsTol = Coeff(AbsCoeff4e7, 1.0); // T-CONV-4's balance row: ABS 4e-7*L specifically

	// A0/A8 fix: "deposited" is measured from the CELLS. Read off the counters instead, the balance check
	// below degenerates into the tautology  L*Retained + L*(1-Retained) == L , which is true for ANY value
	// of Retained and cannot fail - including for a build that deposits nothing at all.
	const double DepositedMetres = SumCanonical(Field, ETrajectoryMapMode::RouteUsage);
	const double DepositedSeconds = SumCanonical(Field, ETrajectoryMapMode::RouteExposure);

	TestTrue(TEXT("deposited == 0.600 m (cells)"),
		NearlyEqualHybrid(DepositedMetres, 0.6, RelTol1e6, Coeff(AbsCoeff2e7, 0.6)));
	TestTrue(TEXT("deposited == 0.600 s (cells)"),
		NearlyEqualHybrid(DepositedSeconds, 0.6, RelTol1e6, Coeff(AbsCoeff2e7, 0.6)));
	TestTrue(TEXT("dropped == 0.400 m"),
		NearlyEqualHybrid(Field.GetDroppedPersonMetres(), 0.4, RelTol1e6, Coeff(AbsCoeff2e7, 0.4)));
	TestTrue(TEXT("dropped == 0.400 s"),
		NearlyEqualHybrid(Field.GetDroppedPersonSeconds(), 0.4, RelTol1e6, Coeff(AbsCoeff2e7, 0.4)));
	TestTrue(TEXT("cells deposited + dropped == offered L (1.0 m)"),
		NearlyEqualAbs(DepositedMetres + Field.GetDroppedPersonMetres(), 1.0, BalanceAbsTol));
	TestTrue(TEXT("cells deposited + dropped == offered Δt (1.0 s)"),
		NearlyEqualAbs(DepositedSeconds + Field.GetDroppedPersonSeconds(), 1.0, BalanceAbsTol));
	TestEqual(TEXT("this is clipping, not rejection"), Field.GetRejectedSegmentCount(), 0);
	return true;
}

// T-CONV-ACC -- the strongest content in the conservation group (vectors_dda.md §2). Three cells receive
// mass from MORE THAN ONE segment; a walker that overwrites instead of accumulating passes every
// single-segment case in this file and fails only here.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleConvAccTest,
	"ProjectMobius.Heatmap.Trajectory.Conservation.T_CONV_ACC_OverlappingSegments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleConvAccTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);

	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);      // S1
	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 25), 0.5f);     // S2
	Field.DepositSegment(FVector2D(5, 15), FVector2D(5, 55), 0.8f);     // S3
	Field.DepositSegment(FVector2D(1, 2), FVector2D(121, 22), 0.6f);    // S4
	Field.DepositSegment(FVector2D(38, 31), FVector2D(12, 57), 0.55f);  // S5

	// T-CONV-ACC row: C, REL 1e-6, ABS 2e-7*Σ L_contributing (k_max = 3 segments into one cell) -- the
	// CheckCells default already matches the REL figure; the fixed 1e-7 abs floor is a safe superset of
	// 2e-7 times these cells' own (sub-1) magnitudes.
	CheckCells(*this, Field, TEXT("T-CONV-ACC"),
		{
			{0, 0, 0.211952116073, 0.27},
			{1, 0, 0.201379375505, 0.25},
			{2, 0, 0.151379375505, 0.15},
		});
	return true;
}

// =====================================================================================================
// TRAVERSAL (DDA)
// =====================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda1Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_1_AxisAligned3Cells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);

	// T-DDA-1 row: C, REL 1e-6, ABS 4e-8 m / 8e-8 s (== 2e-7 x total L / total Δt; dyadic fracs).
	CheckCells(*this, Field, TEXT("T-DDA-1"), { {0, 0, 0.05, 0.1}, {1, 0, 0.1, 0.2}, {2, 0, 0.05, 0.1} },
		RelTol1e6, Coeff(AbsCoeff2e7, 0.20), Coeff(AbsCoeff2e7, 0.4));
	TestTrue(TEXT("Sum PersonMetres == L"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteUsage), 0.20, RelTol1e6, Coeff(AbsCoeff2e7, 0.20)));
	TestTrue(TEXT("Sum PersonSeconds == Δt"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteExposure), 0.4, RelTol1e6, Coeff(AbsCoeff2e7, 0.4)));
	return true;
}

// T-DDA-2 -- 45° diagonal through exact lattice corners. D-2: the walker takes a single diagonal step at
// the tie, so the two off-diagonal cells (1,0) and (0,1) receive NOTHING, not a zero-frac visit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda2Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_2_DiagonalCornerStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda2Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 25), 0.5f);

	// T-DDA-2 row: C + set, REL 1e-6, ABS 5.7e-8 m / 1e-7 s; the visited-cell SET is exact.
	CheckCells(*this, Field, TEXT("T-DDA-2"),
		{
			{0, 0, 0.0707106781187, 0.125},
			{1, 1, 0.141421356237, 0.25},
			{2, 2, 0.0707106781187, 0.125},
		},
		RelTol1e6, 5.7e-8, 1.0e-7);
	// The off-diagonal cells the corner argument (D-2) says must never be touched at all -- "visited set
	// exact" per the master table, so this is CheckCellsExact, not a tolerance-bearing comparison.
	CheckCellsExact(*this, Field, TEXT("T-DDA-2 off-diagonal untouched"), { {1, 0, 0.0, 0.0}, {0, 1, 0.0, 0.0} });
	TestTrue(TEXT("Sum PersonMetres == L"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteUsage), 0.28284271247, RelTol1e6, Coeff(AbsCoeff2e7, 0.28284271247)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda3Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_3_SingleCellSegment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda3Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(2, 3), FVector2D(7, 8), 0.1f);

	// T-DDA-3 row: C, REL 1e-6, ABS 1.4e-8 m; neighbours EXACT 0.
	CheckCells(*this, Field, TEXT("T-DDA-3"), { {0, 0, 0.0707106781187, 0.1} }, RelTol1e6, 1.4e-8, 1.0e-7);
	CheckCellsExact(*this, Field, TEXT("T-DDA-3 neighbours untouched"),
		{ {1, 0, 0.0, 0.0}, {0, 1, 0.0, 0.0}, {1, 1, 0.0, 0.0} });
	return true;
}

// T-DDA-4 -- segment running exactly ALONG the y=10 boundary. A0-ratified convention: LOWER-index row
// (row 0, the "4b" CSV rows) owns it. The invariant that holds regardless of which rule is ratified --
// mass in exactly ONE row, never split 50/50, never doubled into both -- is asserted explicitly too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda4Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_4_BoundaryOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda4Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(5, 10), FVector2D(25, 10), 0.4f);

	// T-DDA-4a/4b row: C+index, REL 1e-6, ABS as T-DDA-1; row index EXACT.
	// Canonical (A0-ratified, lower-index-owns): row 0, the CSV's T-DDA-4b rows.
	CheckCells(*this, Field, TEXT("T-DDA-4 (lower row owns)"),
		{ {0, 0, 0.05, 0.1}, {1, 0, 0.1, 0.2}, {2, 0, 0.05, 0.1} },
		RelTol1e6, Coeff(AbsCoeff2e7, 0.20), Coeff(AbsCoeff2e7, 0.4));
	// The rule-independent invariant: row 1 (the discarded "4a" rows) must receive NOTHING. "Row index
	// exact" per the master table -- CheckCellsExact, not a tolerance-bearing comparison.
	CheckCellsExact(*this, Field, TEXT("T-DDA-4 upper row untouched"),
		{ {0, 1, 0.0, 0.0}, {1, 1, 0.0, 0.0}, {2, 1, 0.0, 0.0} });
	TestTrue(TEXT("Sum PersonMetres == L (not doubled across rows)"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteUsage), 0.20, RelTol1e6, Coeff(AbsCoeff2e7, 0.20)));
	return true;
}

// T-DDA-5 -- degenerate stationary agent. THIS IS THE AC8 GUARD (A0 standing fact #5): 10 s -> 10
// person-seconds in one cell, 0 person-metres, EXACTLY. Tier A / EditorContext, no dataset, no gating --
// this assertion always executes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda5Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_5_StationaryAndThreshold_AC8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda5Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	// T-DDA-5a/5b row: FORM E, ABS 0 -- PersonSeconds bitwise 10.0, PersonMetres exact 0. No epsilon.
	//
	// A0 RULING A0-22, applied here: the oracle's construction is a SINGLE segment of Δt = 10 s, and that
	// is REJECTED by the Δt plausibility gate (cap 5.0 s) -- correctly. DeltaSeconds is per-frame sim time,
	// so a 10 s segment claims a ten-second frame, which cannot happen at any playback speed. A1 never
	// modelled a Δt cap, so its vector was written in ignorance of one. AC8's "stationary for 10 s" is an
	// AGGREGATE dwell, which through the real producer arrives as ~300 frames; the gate must not be widened
	// to admit a physically impossible frame, because that is what stops a forward timeline skip from
	// dumping its whole skipped duration into one cell.
	// So the dwell is deposited as 2 x 5.0 s, which is inside the gate (it rejects Δt > 5.0, so 5.0 passes)
	// and keeps the assertion genuinely bitwise: 5.0 and 10.0 are both dyadic, so 5.0 + 5.0 == 10.0 exactly
	// in float32 with no accumulation error. The realistic many-small-segments form is covered separately by
	// the T-SEM-2 linearity assertion (100 x 0.1 s, REL 1e-5 per TOLERANCES.md §3).
	// 5a: exact stationarity, L == 0.
	{
		FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
		Field.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 5.0f);
		Field.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 5.0f);
		CheckCellsExact(*this, Field, TEXT("T-DDA-5a (AC8)"), { {3, 4, 0.0, 10.0} });
		TestEqual(TEXT("5a: stationary count"), Field.GetStationarySegmentCount(), 2);
		// NOTE: TestEqual<double> defaults its Tolerance to UE_KINDA_SMALL_NUMBER, NOT zero -- Form E
		// demands genuine exactness, so the tolerance is passed explicitly as 0.0 here and everywhere
		// else in this file a "TestEqual" is meant to be bitwise.
		TestEqual(TEXT("5a: Sum PersonSeconds == Δt bitwise (no negligible-seconds bucket)"),
			SumCanonical(Field, ETrajectoryMapMode::RouteExposure), 10.0, 0.0);
		TestEqual(TEXT("5a: Negligible metres exact 0 (L was already exactly 0)"),
			Field.GetNegligiblePersonMetres(), 0.0, 0.0);
	}
	// 5b: L = 5e-5 m, at the threshold (<= 1e-4 m degenerate branch). Same result as 5a, but this time
	// the sub-threshold length must be booked to Negligible, not silently dropped or deposited. The
	// Negligible-bucket VALUE itself is not one of T-DDA-5's Form-E named quantities in the master table
	// (only PersonMetres/PersonSeconds are), so it keeps its own tight-but-not-zero floor: the 0.005 cm
	// input difference is exactly representable in double, but converting to metres and comparing against
	// a float32-derived accumulator is not guaranteed bit-exact.
	{
		FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
		// Same 2 x 5.0 s split as 5a (ruling A0-22), so the sub-threshold length is offered TWICE.
		Field.DepositSegment(FVector2D(37, 42), FVector2D(37.005, 42), 5.0f);
		Field.DepositSegment(FVector2D(37, 42), FVector2D(37.005, 42), 5.0f);
		CheckCellsExact(*this, Field, TEXT("T-DDA-5b (AC8)"), { {3, 4, 0.0, 10.0} });
		TestEqual(TEXT("5b: stationary count"), Field.GetStationarySegmentCount(), 2);
		TestEqual(TEXT("5b: Sum PersonSeconds == Δt bitwise"), SumCanonical(Field, ETrajectoryMapMode::RouteExposure), 10.0, 0.0);
		TestTrue(TEXT("5b: Negligible metres == 2 x 5e-5 m (sub-threshold L booked once per offered segment)"),
			NearlyEqualAbs(Field.GetNegligiblePersonMetres(), 1.0e-4, 1.0e-9));
	}
	return true;
}

// T-DDA-6 -- reverse of T-DDA-1. Per-cell values must be identical; only the visit order (unobservable
// from the canonical arrays) differs. These operands are dyadic (5/20, 15/20), so this case is expected
// to be bit-identical, not merely close -- see TOLERANCES.md's discussion (missing; see BLOCKER note).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda6Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_6_ReversalIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda6Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(25, 5), FVector2D(5, 5), 0.4f);

	// T-DDA-6 row: FORM E for these operands (5/20, 15/20 are dyadic); general reversal would be
	// REL 1e-6 / ABS 4e-7*L instead.
	CheckCellsExact(*this, Field, TEXT("T-DDA-6"), { {0, 0, 0.05, 0.1}, {1, 0, 0.1, 0.2}, {2, 0, 0.05, 0.1} });

	// A0: the above only checks the reversed walk against the oracle's numbers. What T-DDA-6 actually
	// asserts is REVERSAL IDENTITY, so compare the two directions against EACH OTHER bitwise. Without
	// this, a build whose forward and reverse walks both drifted the same way would pass, and the visit
	// order (the thing reversal changes) is unobservable from the canonical arrays any other way.
	FTrajectoryField Forward = MakeField(1000.0, 1000.0, 10.0f);
	Forward.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);
	const TArray<float>& RevMetres = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& FwdMetres = Forward.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& RevSeconds = Field.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& FwdSeconds = Forward.GetCanonical(ETrajectoryMapMode::RouteExposure);
	TestTrue(TEXT("T-DDA-6: reversed PersonMetres array is bitwise identical to the forward walk"),
		RevMetres.Num() == FwdMetres.Num()
			&& FMemory::Memcmp(RevMetres.GetData(), FwdMetres.GetData(), RevMetres.Num() * sizeof(float)) == 0);
	TestTrue(TEXT("T-DDA-6: reversed PersonSeconds array is bitwise identical to the forward walk"),
		RevSeconds.Num() == FwdSeconds.Num()
			&& FMemory::Memcmp(RevSeconds.GetData(), FwdSeconds.GetData(), RevSeconds.Num() * sizeof(float)) == 0);
	return true;
}

// T-DDA-7 -- teleport, 40 m in 0.016 s (2500 m/s, 125x the 20 m/s cap). CANONICAL is 7b: nothing is
// deposited anywhere; the whole segment books to REJECTED (not Dropped -- see file header note on C-2).
// A missing plausibility test paints a 4000-cell streak; this asserts that streak is entirely absent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda7Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_7_TeleportRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda7Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(5000.0, 3000.0, 10.0f);
	Field.DepositSegment(FVector2D(100, 100), FVector2D(4100, 100), 0.016f);

	// T-DDA-7a/7b row: FORM E + C, REL 1e-6 (seconds only), ABS 0 -- metres exact 0 everywhere;
	// "DroppedMetres exact 40.0" in the master table's own naming, which is this file's RejectedPersonMetres
	// per the bucket-naming note above (numbers unchanged, label follows the header not the oracle prose).
	TestEqual(TEXT("teleport rejection fired exactly once"), Field.GetRejectedTeleportCount(), 1);
	TestEqual(TEXT("rejected segment count"), Field.GetRejectedSegmentCount(), 1);
	TestEqual(TEXT("Rejected metres exact 40.0 m"), Field.GetRejectedPersonMetres(), 40.0, 0.0);
	TestTrue(TEXT("Rejected seconds == 0.016 s (REL 1e-6)"),
		NearlyEqualRel(Field.GetRejectedPersonSeconds(), 0.016, RelTol1e6));
	TestEqual(TEXT("nothing deposited (Total metres exact 0)"), SumCanonical(Field, ETrajectoryMapMode::RouteUsage), 0.0, 0.0);
	TestEqual(TEXT("nothing deposited (Total seconds exact 0)"), SumCanonical(Field, ETrajectoryMapMode::RouteExposure), 0.0, 0.0);
	TestEqual(TEXT("Rejected, not Dropped (Dropped stays exact 0)"), Field.GetDroppedPersonMetres(), 0.0, 0.0);

	// No cell along the false-corridor path -- either candidate row, the whole x-span -- receives mass.
	const TArray<float>& Metres = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const FIntPoint Dims = Field.GetGridDims();
	bool bCorridorClean = true;
	for (int32 X = 10; X <= 410; X += 20)
	{
		for (int32 Y = 9; Y <= 10; ++Y)
		{
			const int32 Index = Y * Dims.X + X;
			if (Metres.IsValidIndex(Index) && Metres[Index] != 0.0f)
			{
				bCorridorClean = false;
			}
		}
	}
	TestTrue(TEXT("no false corridor painted between the endpoints"), bCorridorClean);
	return true;
}

// T-DDA-8 -- long shallow-angle segment (drift detector). 15 cells, three of them 1/120-frac slivers 12x
// smaller than the interior frac -- exactly where a naive walker double-steps. All 15 rows asserted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDda8Test,
	"ProjectMobius.Heatmap.Trajectory.DDA.T_DDA_8_ShallowAngleSlivers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDda8Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(1, 2), FVector2D(121, 22), 0.6f);

	// T-DDA-8 row: C, REL 1e-6, ABS 2.43e-7 m / 1.2e-7 s -- the slivers are ~2.5e-5 relative, so the ABS
	// floor (not just REL 1e-6) is load-bearing here; CheckCells' hybrid check applies both.
	CheckCells(*this, Field, TEXT("T-DDA-8"),
		{
			{0, 0, 0.0912414379545, 0.045}, {1, 0, 0.101379375505, 0.05}, {2, 0, 0.101379375505, 0.05},
			{3, 0, 0.101379375505, 0.05}, {4, 0, 0.0912414379545, 0.045},
			{4, 1, 0.0101379375505, 0.005}, {5, 1, 0.101379375505, 0.05}, {6, 1, 0.101379375505, 0.05},
			{7, 1, 0.101379375505, 0.05}, {8, 1, 0.101379375505, 0.05}, {9, 1, 0.101379375505, 0.05},
			{10, 1, 0.0912414379545, 0.045},
			{10, 2, 0.0101379375505, 0.005}, {11, 2, 0.101379375505, 0.05}, {12, 2, 0.0101379375505, 0.005},
		},
		RelTol1e6, Dda8AbsFloorMetres, Dda8AbsFloorSeconds);

	TestTrue(TEXT("Sum PersonMetres == L (the three slivers must not be dropped)"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteUsage), 1.21655250606, RelTol1e6, Coeff(AbsCoeff2e7, 1.21655250606)));
	TestTrue(TEXT("Sum PersonSeconds == Δt"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteExposure), 0.6, RelTol1e6, Coeff(AbsCoeff2e7, 0.6)));
	return true;
}

// =====================================================================================================
// SEMANTICS
// =====================================================================================================

// T-SEM-1 -- identical 4 m geometry at 0.8 and 1.6 m/s. Usage is speed-blind (ratio 1.0 exactly);
// Exposure halves as speed doubles (ratio 2.0 exactly).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleSem1Test,
	"ProjectMobius.Heatmap.Trajectory.Semantics.T_SEM_1_UsageVsExposureSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleSem1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Slow = MakeField(1000.0, 1000.0, 10.0f); // 0.8 m/s -> Δt = 5.0 s
	FTrajectoryField Fast = MakeField(1000.0, 1000.0, 10.0f); // 1.6 m/s -> Δt = 2.5 s
	Slow.DepositSegment(FVector2D(5, 5), FVector2D(405, 5), 5.0f);
	Fast.DepositSegment(FVector2D(5, 5), FVector2D(405, 5), 2.5f);

	// T-SEM-1(3,4) row: C/D, REL 1e-6, ABS 2e-7*Δt.
	CheckCells(*this, Slow, TEXT("T-SEM-1 slow"),
		{ {0, 0, 0.05, 0.0625}, {1, 0, 0.1, 0.125}, {20, 0, 0.1, 0.125}, {40, 0, 0.05, 0.0625} },
		RelTol1e6, Coeff(AbsCoeff2e7, 4.0), Coeff(AbsCoeff2e7, 5.0));
	CheckCells(*this, Fast, TEXT("T-SEM-1 fast"),
		{ {0, 0, 0.05, 0.03125}, {1, 0, 0.1, 0.0625}, {20, 0, 0.1, 0.0625}, {40, 0, 0.05, 0.03125} },
		RelTol1e6, Coeff(AbsCoeff2e7, 4.0), Coeff(AbsCoeff2e7, 2.5));

	const TArray<float>& SlowMetres = Slow.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& FastMetres = Fast.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& SlowSeconds = Slow.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& FastSeconds = Fast.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const int32 InteriorIdx = 20; // row 0, column 20 -> index 20 in a 100-wide grid
	// T-SEM-1(1): FORM E -- Usage arrays bitwise across speed.
	TestEqual(TEXT("Usage ratio slow/fast == 1.0 exactly (speed-blind, bitwise)"),
		(double)SlowMetres[InteriorIdx] / (double)FastMetres[InteriorIdx], 1.0, 0.0);
	// T-SEM-1(2): E via D -- ratio 2.0 is bitwise here BECAUSE 5.0/2.5 is dyadic (not true of an arbitrary
	// speed pair, which would fall back to REL 1e-6 / ABS 2e-7*Δt).
	TestEqual(TEXT("Exposure ratio slow/fast == 2.0 exactly (dyadic operands, bitwise)"),
		(double)SlowSeconds[InteriorIdx] / (double)FastSeconds[InteriorIdx], 2.0, 0.0);
	TestTrue(TEXT("Sum PersonMetres == 4.0 m at BOTH speeds"),
		NearlyEqualHybrid(SumCanonical(Slow, ETrajectoryMapMode::RouteUsage), 4.0, RelTol1e6, Coeff(AbsCoeff2e7, 4.0))
			&& NearlyEqualHybrid(SumCanonical(Fast, ETrajectoryMapMode::RouteUsage), 4.0, RelTol1e6, Coeff(AbsCoeff2e7, 4.0)));
	TestTrue(TEXT("Sum PersonSeconds == Δt at each speed (5.0 / 2.5)"),
		NearlyEqualHybrid(SumCanonical(Slow, ETrajectoryMapMode::RouteExposure), 5.0, RelTol1e6, Coeff(AbsCoeff2e7, 5.0))
			&& NearlyEqualHybrid(SumCanonical(Fast, ETrajectoryMapMode::RouteExposure), 2.5, RelTol1e6, Coeff(AbsCoeff2e7, 2.5)));
	return true;
}

// T-SEM-2 -- stationary agent, 10 s. Second half of the AC8 guard: calibrated against a walking agent
// (T-DDA-3's numbers, reused because they are literally "PersonSeconds = 0.1 in a 10 cm cell" already),
// and against 100 consecutive 0.1 s stationary segments (linearity / subdivision invariance).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleSem2Test,
	"ProjectMobius.Heatmap.Trajectory.Semantics.T_SEM_2_StationaryDwell_AC8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleSem2Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	// T-SEM-2 row: FORM E+C, REL 1e-6 (Exposure), ABS 0 -- seconds bitwise 10.0, metres exact 0; the 100x
	// ratio is FORM D (REL 1e-6), since dividing by CellArea introduces float rounding the raw
	// PersonSeconds deposit does not have.
	FTrajectoryField Standing = MakeField(1000.0, 1000.0, 10.0f);
	// 2 x 5.0 s, not 1 x 10.0 s: ruling A0-22 (see T-DDA-5) -- a single 10 s segment is rejected by the Δt
	// plausibility gate, and rightly so. Both operands are dyadic, so the 10.0 s total stays bitwise exact.
	Standing.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 5.0f);
	Standing.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 5.0f);
	CheckCellsExact(*this, Standing, TEXT("T-SEM-2 stand"), { {3, 4, 0.0, 10.0} });
	const double StandingExposure = SumCanonical(Standing, ETrajectoryMapMode::RouteExposure) / Standing.GetCellAreaSquareMetres();
	TestTrue(TEXT("standing Exposure == 1000.0 person*s/m^2"),
		NearlyEqualRel(StandingExposure, 1000.0, RelTol1e6));

	FTrajectoryField Walking = MakeField(1000.0, 1000.0, 10.0f);
	Walking.DepositSegment(FVector2D(2, 3), FVector2D(7, 8), 0.1f); // T-DDA-3
	const double WalkingExposure = SumCanonical(Walking, ETrajectoryMapMode::RouteExposure) / Walking.GetCellAreaSquareMetres();
	TestTrue(TEXT("walking-through Exposure == 10.0 person*s/m^2 (T-DDA-3's dwell)"),
		NearlyEqualRel(WalkingExposure, 10.0, RelTol1e6));
	TestTrue(TEXT("standing reads exactly 100x hotter than walking through (the AC8 signature)"),
		NearlyEqualRel(StandingExposure / WalkingExposure, 100.0, RelTol1e6));

	// Linearity row: FORM D, REL 1e-5, ABS 2e-6 s -- NOT the single-shot exactness above. 100 consecutive
	// 0.1 s stationary segments in the same cell must total the same 10.0 s, but summing float32 many
	// times over is not bit-exact the way one clean 10.0 s deposit is.
	FTrajectoryField Chopped = MakeField(1000.0, 1000.0, 10.0f);
	for (int32 i = 0; i < 100; ++i)
	{
		Chopped.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 0.1f);
	}
	const int32 ChoppedIndex = 4 * Chopped.GetGridDims().X + 3;
	TestEqual(TEXT("100x0.1s: PersonMetres still exact 0 (summing zeros has no rounding issue)"),
		(double)Chopped.GetCanonical(ETrajectoryMapMode::RouteUsage)[ChoppedIndex], 0.0, 0.0);
	TestTrue(TEXT("100x0.1s: PersonSeconds == 10.0 (REL 1e-5, ABS 2e-6 s -- accumulation, not exactness)"),
		NearlyEqualHybrid((double)Chopped.GetCanonical(ETrajectoryMapMode::RouteExposure)[ChoppedIndex], 10.0,
			RelTol1e5, Sem2LinearityAbsSeconds));
	TestEqual(TEXT("100 stationary segments all counted"), Chopped.GetStationarySegmentCount(), 100);
	return true;
}

// T-SEM-3 -- mode-selection invariant (vectors_semantics.md §1, "T-WIDTH-2's sibling"). Switching mode
// mid-playback must give the same Exposure array as a run made entirely in Exposure mode: both canonical
// arrays are accumulated unconditionally on every deposit, independent of the currently-selected mode.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleSem3Test,
	"ProjectMobius.Heatmap.Trajectory.Semantics.T_SEM_3_ModeSwitchInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleSem3Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	auto DepositFive = [](FTrajectoryField& Field)
	{
		Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);
		Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 25), 0.5f);
		Field.DepositSegment(FVector2D(5, 15), FVector2D(5, 55), 0.8f);
		Field.DepositSegment(FVector2D(1, 2), FVector2D(121, 22), 0.6f);
		Field.DepositSegment(FVector2D(38, 31), FVector2D(12, 57), 0.55f);
	};

	FTrajectoryField SwitchedMidway = MakeField(1000.0, 1000.0, 10.0f); // starts in RouteUsage (default)
	DepositFive(SwitchedMidway);
	SwitchedMidway.SetPresentationMode(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& SwitchedExposure = SwitchedMidway.GetPresentation(); // forces the rebuild

	FTrajectoryField AlwaysExposure = MakeField(1000.0, 1000.0, 10.0f);
	AlwaysExposure.SetPresentationMode(ETrajectoryMapMode::RouteExposure);
	DepositFive(AlwaysExposure);
	const TArray<float>& RunExposure = AlwaysExposure.GetPresentation();

	// NOTE: TOLERANCES.md's T-SEM-3 row ("FORM E ... canonical arrays bitwise across mode switch") covers
	// the CANONICAL comparison below, which is exact. The PRESENTATION comparison here is an additional
	// check this file adds beyond that row -- the header itself documents the rebuild as equal to the
	// incremental path only "up to float rounding", so it is intentionally NOT held to Form E; RelTol1e5
	// (the same figure used for other kernel-rebuild comparisons) with a small absolute floor is used.
	TestEqual(TEXT("presentation array sizes match"), SwitchedExposure.Num(), RunExposure.Num());
	bool bAllMatch = SwitchedExposure.Num() == RunExposure.Num();
	for (int32 i = 0; bAllMatch && i < SwitchedExposure.Num(); ++i)
	{
		if (!NearlyEqualHybrid((double)SwitchedExposure[i], (double)RunExposure[i], RelTol1e5, 1.0e-6))
		{
			bAllMatch = false;
			AddError(FString::Printf(TEXT("presentation cell %d differs after a mid-playback mode switch"), i));
		}
	}
	TestTrue(TEXT("Exposure after a mid-playback switch == a run made entirely in Exposure mode"), bAllMatch);

	// Canonical accumulation itself must be byte-for-byte independent of mode: same deposits, same order.
	const TArray<float>& CanonA = SwitchedMidway.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& CanonB = AlwaysExposure.GetCanonical(ETrajectoryMapMode::RouteExposure);
	bool bCanonMatch = CanonA.Num() == CanonB.Num();
	for (int32 i = 0; bCanonMatch && i < CanonA.Num(); ++i)
	{
		bCanonMatch = (CanonA[i] == CanonB[i]);
	}
	TestTrue(TEXT("canonical PersonSeconds is identical regardless of presentation mode"), bCanonMatch);
	return true;
}

// =====================================================================================================
// RESOLUTION AND WIDTH
// =====================================================================================================

// T-WIDTH-1 -- one 2 m path rasterised at 5 / 10 / 25 cm/texel. Mass is a property of the trajectory:
// Σ PersonMetres and Σ PersonSeconds stay 2.00 m / 2.00 s at all three resolutions. Peak density scales
// as 1/WorldCmPerTexel (20.0 / 10.0 / 4.0 person/m).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleWidth1Test,
	"ProjectMobius.Heatmap.Trajectory.Resolution.T_WIDTH_1_MassAcrossResolutions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleWidth1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	FTrajectoryField F5 = MakeField(1000.0, 1000.0, 5.0f);
	FTrajectoryField F10 = MakeField(1000.0, 1000.0, 10.0f);
	FTrajectoryField F25 = MakeField(1000.0, 1000.0, 25.0f);
	F5.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);
	F10.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);
	F25.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);

	// T-WIDTH-1 row: D+C, REL 1e-6, ABS 4e-7 m (== 2e-7 x the case's total 2.0 m path length).
	constexpr double Width1AbsFloor = 4.0e-7;
	CheckCells(*this, F5, TEXT("T-WIDTH-1a"), { {0, 0, 0.025, 0.025}, {1, 0, 0.05, 0.05}, {40, 0, 0.025, 0.025} },
		RelTol1e6, Width1AbsFloor, Width1AbsFloor);
	CheckCells(*this, F10, TEXT("T-WIDTH-1b"), { {0, 0, 0.075, 0.075}, {1, 0, 0.1, 0.1}, {20, 0, 0.025, 0.025} },
		RelTol1e6, Width1AbsFloor, Width1AbsFloor);
	CheckCells(*this, F25, TEXT("T-WIDTH-1c"), { {0, 0, 0.225, 0.225}, {1, 0, 0.25, 0.25}, {8, 0, 0.025, 0.025} },
		RelTol1e6, Width1AbsFloor, Width1AbsFloor);

	for (FTrajectoryField* F : { &F5, &F10, &F25 })
	{
		TestTrue(TEXT("Sum PersonMetres == 2.00 m at every resolution"),
			NearlyEqualHybrid(SumCanonical(*F, ETrajectoryMapMode::RouteUsage), 2.0, RelTol1e6, Width1AbsFloor));
		TestTrue(TEXT("Sum PersonSeconds == 2.00 s at every resolution"),
			NearlyEqualHybrid(SumCanonical(*F, ETrajectoryMapMode::RouteExposure), 2.0, RelTol1e6, Width1AbsFloor));
	}

	// Peak density (interior cell PersonMetres / CellArea): 20.0 / 10.0 / 4.0 person/m. Closed form
	// verified across all three: peak_density_person_per_m * WorldCmPerTexel == 100 exactly (used again,
	// unmodified, by the D2b floor-size test below).
	const double Peak5 = 0.05 / F5.GetCellAreaSquareMetres();
	const double Peak10 = 0.1 / F10.GetCellAreaSquareMetres();
	const double Peak25 = 0.25 / F25.GetCellAreaSquareMetres();
	TestTrue(TEXT("peak density @5cm == 20.0 person/m"), NearlyEqualRel(Peak5, 20.0, RelTol1e6));
	TestTrue(TEXT("peak density @10cm == 10.0 person/m"), NearlyEqualRel(Peak10, 10.0, RelTol1e6));
	TestTrue(TEXT("peak density @25cm == 4.0 person/m"), NearlyEqualRel(Peak25, 4.0, RelTol1e6));
	TestTrue(TEXT("closed form: peak_density * WorldCmPerTexel == 100 at all three"),
		NearlyEqualRel(Peak5 * 5.0, 100.0, RelTol1e6) && NearlyEqualRel(Peak10 * 10.0, 100.0, RelTol1e6)
			&& NearlyEqualRel(Peak25 * 25.0, 100.0, RelTol1e6));
	return true;
}

// T-WIDTH-1 / T-RES-2 companion -- D2b's floor-size construction (TOLERANCES.md §11.1(4) resolves the
// TEST_PLAN vs vectors_dda.md conflict; §11.2 gives the numbers). At WorldCmPerTexel target 10 and
// MaxGridDim 2048 (the FTrajectoryFieldConfig default): 20/73/100 m floors all fit under the grid-dim
// cap, so EffectiveCmPerTexel stays exactly 10.0 and peak canonical density is IDENTICAL across them
// (same closed form as above: peak_density * WorldCmPerTexel == 100). A 250 m floor does NOT fit
// (25000/10 = 2500 > 2048), so D2b raises cm/texel to 25000/2048 = 12.20703125 (binary-exact), and the
// peak density on that floor is smaller by exactly 10/12.20703125.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleWidth1FloorSizeTest,
	"ProjectMobius.Heatmap.Trajectory.Resolution.T_WIDTH_1_D2b_FloorSizeEffectiveCmPerTexel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleWidth1FloorSizeTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	auto MakeFloor = [](double ExtentCm)
	{
		FTrajectoryField Field;
		FTrajectoryFieldConfig Config;
		Config.WorldCmPerTexel = 10.0f; // requested; D2b may raise it
		Field.Initialise(FVector2D(ExtentCm, ExtentCm), FVector2D(0.0, 0.0), Config);
		return Field;
	};

	FTrajectoryField Floor20m = MakeFloor(2000.0);
	FTrajectoryField Floor73m = MakeFloor(7300.0);
	FTrajectoryField Floor100m = MakeFloor(10000.0);
	FTrajectoryField Floor250m = MakeFloor(25000.0);

	TestEqual(TEXT("20 m floor: EffectiveCmPerTexel == 10.0 (D2b did not fire)"),
		Floor20m.GetEffectiveCmPerTexel(), 10.0f, 0.0f);
	TestEqual(TEXT("73 m floor: EffectiveCmPerTexel == 10.0"), Floor73m.GetEffectiveCmPerTexel(), 10.0f, 0.0f);
	TestEqual(TEXT("100 m floor: EffectiveCmPerTexel == 10.0"), Floor100m.GetEffectiveCmPerTexel(), 10.0f, 0.0f);
	TestTrue(TEXT("20 m floor: GridDims == 200x200"),
		Floor20m.GetGridDims().X == 200 && Floor20m.GetGridDims().Y == 200);
	TestTrue(TEXT("73 m floor: GridDims == 730x730"),
		Floor73m.GetGridDims().X == 730 && Floor73m.GetGridDims().Y == 730);
	TestTrue(TEXT("100 m floor: GridDims == 1000x1000"),
		Floor100m.GetGridDims().X == 1000 && Floor100m.GetGridDims().Y == 1000);

	// 250 m floor: D2b fires (25000/10 = 2500 > MaxGridDim 2048), raising cm/texel to 25000/2048.
	TestTrue(TEXT("250 m floor: EffectiveCmPerTexel == 12.20703125 exactly (D2b fired)"),
		NearlyEqualAbs((double)Floor250m.GetEffectiveCmPerTexel(), 12.20703125, 0.0));
	TestTrue(TEXT("250 m floor: GridDims == 2048x2048 (clamped to MaxGridDim)"),
		Floor250m.GetGridDims().X == 2048 && Floor250m.GetGridDims().Y == 2048);
	// This also closes T-RES-2 (TOLERANCES.md §11.2: "needs MaxGridDim and the D2b clamp rule to state
	// the expected WorldCmPerTexel and GridDim" -- both are asserted above for the 250 m / 25000 cm case).

	// Deposit the SAME axis-aligned 2 m path (T-WIDTH-1's own segment) into the three unclamped floors and
	// the clamped one; peak density must match across 20/73/100 m (same cm/texel), and scale down by
	// EXACTLY 10/12.20703125 on the 250 m floor -- both directly from the peak_density*W==100 closed form
	// verified in T_WIDTH_1_MassAcrossResolutions.
	Floor20m.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);
	Floor73m.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);
	Floor100m.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);
	Floor250m.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);

	auto InteriorDensity = [](FTrajectoryField& Field)
	{
		// Interior cell (1,0): well clear of the first/last partial cells in all four grids.
		const int32 Index = 0 * Field.GetGridDims().X + 1;
		return (double)Field.GetCanonical(ETrajectoryMapMode::RouteUsage)[Index] / Field.GetCellAreaSquareMetres();
	};
	const double Density20 = InteriorDensity(Floor20m);
	const double Density73 = InteriorDensity(Floor73m);
	const double Density100 = InteriorDensity(Floor100m);
	const double Density250 = InteriorDensity(Floor250m);

	TestTrue(TEXT("peak canonical density equal on 20/73/100 m floors (same EffectiveCmPerTexel)"),
		NearlyEqualRel(Density20, Density73, RelTol1e6) && NearlyEqualRel(Density73, Density100, RelTol1e6));
	TestTrue(TEXT("250 m floor density is smaller by exactly 10 / 12.20703125 (D2b's coarser cm/texel)"),
		NearlyEqualRel(Density250 / Density100, 10.0 / 12.20703125, RelTol1e6));
	return true;
}

// T-WIDTH-2 -- the machine-checkable form of "display width must not alter the metric". Canonical arrays
// must be BIT-IDENTICAL across a DisplayPathWidthCm change (20 -> 50 cm), and the presentation peak must
// differ by EXACTLY 6.25x (R^2 ratio, both centre cells fully inside their disc). Also folds in the
// sub-texel identity case (vectors_kernel.md §2): when DisplayPathWidthCm <= WorldCmPerTexel the kernel
// collapses to identity and Presentation == Canonical bitwise.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleWidth2Test,
	"ProjectMobius.Heatmap.Trajectory.Resolution.T_WIDTH_2_DisplayWidthNeverTouchesCanonical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleWidth2Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	// T-DDA-3's segment translated to an interior cell (30,30) -- see T-CONV-3 for why this is legitimate.
	FTrajectoryField R1 = MakeField(1000.0, 1000.0, 10.0f, /*DisplayPathWidthCm*/ 20.0f);
	FTrajectoryField R2_5 = MakeField(1000.0, 1000.0, 10.0f, /*DisplayPathWidthCm*/ 50.0f);
	R1.DepositSegment(FVector2D(302, 303), FVector2D(307, 308), 0.1f);
	R2_5.DepositSegment(FVector2D(302, 303), FVector2D(307, 308), 0.1f);

	// T-WIDTH-2(a) row: FORM E, bitwise -- memcmp, per TOLERANCES.md's explicit instruction.
	const TArray<float>& MetresR1 = R1.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& MetresR2 = R2_5.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& SecondsR1 = R1.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& SecondsR2 = R2_5.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const bool bCanonicalBitIdentical = MetresR1.Num() == MetresR2.Num() && SecondsR1.Num() == SecondsR2.Num()
		&& FMemory::Memcmp(MetresR1.GetData(), MetresR2.GetData(), MetresR1.Num() * sizeof(float)) == 0
		&& FMemory::Memcmp(SecondsR1.GetData(), SecondsR2.GetData(), SecondsR1.Num() * sizeof(float)) == 0;
	TestTrue(TEXT("(a) canonical arrays are bit-identical across a display-width change (memcmp)"),
		bCanonicalBitIdentical);

	// T-WIDTH-2(b) row: FORM D, REL 1e-5 (NOT the 1e-6 vectors_kernel.md §3 originally stated -- not
	// robust against the float32 normaliser-sum error, TOLERANCES.md §11.1(3)).
	const FIntPoint Dims = R1.GetGridDims();
	const int32 CentreIdx = 30 * Dims.X + 30;
	const TArray<float>& PresR1 = R1.GetPresentation();
	const TArray<float>& PresR2 = R2_5.GetPresentation();
	TestTrue(TEXT("(b) presentation peak ratio (R=1.0 / R=2.5) == 6.25 (REL 1e-5)"),
		NearlyEqualRel((double)PresR1[CentreIdx] / (double)PresR2[CentreIdx], 6.25, RelTol1e5));

	// Sub-texel radius: 25 cm/texel, 20 cm display width -> R = 0.4 texel < 0.5.
	//
	// ⚠️ REWRITTEN 2026-08-10 (D-D), and the old claim was retired rather than relaxed. It asserted
	// "Presentation == Canonical bitwise" for ANY deposit at R < 0.5, which held only because the splat
	// was forced onto the cell CENTRE. That forcing was the defect: it quantised the drawn stroke to the
	// lattice and put it up to half a cell from the agent. With the deposit now placed sub-cell, a 20 cm
	// stroke whose centre sits 0.32 of a 25 cm cell off-centre — which is where this test's own segment
	// lands — genuinely overlaps the neighbour, and refusing to draw that would be the bug.
	//
	// So the claim splits into the part that survives and the part that replaces it.
	FTrajectoryField SubTexel = MakeField(1000.0, 1000.0, 25.0f, /*DisplayPathWidthCm*/ 20.0f);

	// (i) SURVIVES — a CELL-CENTRED deposit at R < 0.5 is still the identity. Midpoint (312.5, 312.5) is
	// exactly the centre of cell (12,12) at 25 cm/texel, so the phase is (0,0) and the disc fits inside
	// one cell. This is the original property, stated at the only position where it was ever true.
	SubTexel.DepositSegment(FVector2D(310, 310), FVector2D(315, 315), 0.1f);
	{
		const TArray<float>& Canon = SubTexel.GetCanonical(ETrajectoryMapMode::RouteUsage);
		const TArray<float>& Pres = SubTexel.GetPresentation();
		bool bIdentity = Canon.Num() == Pres.Num();
		for (int32 i = 0; bIdentity && i < Canon.Num(); ++i)
		{
			bIdentity = (Canon[i] == Pres[i]);
		}
		TestTrue(TEXT("sub-texel radius, CELL-CENTRED deposit: Presentation == Canonical bitwise"), bIdentity);
	}

	// (ii) REPLACES — for an OFF-CENTRE deposit the stroke may spill, but only into immediate neighbours,
	// and not one person-metre may be created or lost doing it. Those are the two things that actually
	// matter, and bitwise identity was never the way to state either.
	FTrajectoryField OffCentre = MakeField(1000.0, 1000.0, 25.0f, /*DisplayPathWidthCm*/ 20.0f);
	OffCentre.DepositSegment(FVector2D(302, 303), FVector2D(307, 308), 0.1f);
	{
		const TArray<float>& Canon = OffCentre.GetCanonical(ETrajectoryMapMode::RouteUsage);
		const TArray<float>& Pres = OffCentre.GetPresentation();
		const FIntPoint Dims2 = OffCentre.GetGridDims();

		double CanonSum = 0.0;
		double PresSum = 0.0;
		for (int32 i = 0; i < Canon.Num(); ++i)
		{
			CanonSum += static_cast<double>(Canon[i]);
			PresSum += static_cast<double>(Pres[i]);
		}
		TestTrue(TEXT("sub-texel radius, off-centre: the splat conserves mass (REL 1e-5)"),
			NearlyEqualRel(PresSum, CanonSum, RelTol1e5));

		// Confinement: every lit cell must be within one cell of a cell the DDA actually crossed. At
		// R < 0.5 the disc cannot reach further however it is phased, so a hit here means the phase table
		// is addressing the wrong neighbourhood — which a mass check alone would not notice.
		bool bConfined = true;
		for (int32 Idx = 0; Idx < Pres.Num() && bConfined; ++Idx)
		{
			if (Pres[Idx] == 0.0f)
			{
				continue;
			}
			const int32 PX = Idx % Dims2.X;
			const int32 PY = Idx / Dims2.X;
			bool bNearCrossed = false;
			for (int32 DY = -1; DY <= 1 && !bNearCrossed; ++DY)
			{
				for (int32 DX = -1; DX <= 1 && !bNearCrossed; ++DX)
				{
					const int32 QX = PX + DX;
					const int32 QY = PY + DY;
					if (QX >= 0 && QX < Dims2.X && QY >= 0 && QY < Dims2.Y
						&& Canon[QY * Dims2.X + QX] != 0.0f)
					{
						bNearCrossed = true;
					}
				}
			}
			bConfined = bNearCrossed;
		}
		TestTrue(TEXT("sub-texel radius, off-centre: every lit cell adjoins a crossed cell"), bConfined);
	}
	return true;
}

// T-RES-1 -- rasterise at 5 cm then aggregate 2x2 == rasterise at 10 cm directly (the half-open-interval
// partition proof, vectors_dda.md §4). NOT expected to be bit-exact (different boundary-t rounding), so
// the fine-vs-direct cross-check uses REL 1e-5 (TOLERANCES.md: "not exact; densities average, never sum").
//
// T-RES-2 (once a BLOCKER: no expected value derivable without MaxGridDim + the D2b clamp rule) is now
// closed by TOLERANCES.md §11.2 + A0's ruling -- see T_WIDTH_1_D2b_FloorSizeEffectiveCmPerTexel above,
// which asserts the 25000 cm / 2048-grid-dim / 12.20703125 cm-per-texel case that IS T-RES-2's content.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleRes1Test,
	"ProjectMobius.Heatmap.Trajectory.Resolution.T_RES_1_FineAggregateEqualsDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleRes1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Fine = MakeField(1000.0, 1000.0, 5.0f);
	FTrajectoryField Direct = MakeField(1000.0, 1000.0, 10.0f);
	Fine.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);
	Direct.DepositSegment(FVector2D(2.5, 2.5), FVector2D(202.5, 2.5), 2.0f);

	// T-RES-1 row: FORM D, REL 1e-5, ABS 2e-7*L.
	CheckCells(*this, Fine, TEXT("T-RES-1fine"), { {0, 0, 0.025, 0.025}, {1, 0, 0.05, 0.05}, {40, 0, 0.025, 0.025} },
		RelTol1e5, Coeff(AbsCoeff2e7, 2.0), Coeff(AbsCoeff2e7, 2.0));
	CheckCells(*this, Direct, TEXT("T-RES-1direct"),
		{ {0, 0, 0.075, 0.075}, {1, 0, 0.1, 0.1}, {20, 0, 0.025, 0.025} },
		RelTol1e5, Coeff(AbsCoeff2e7, 2.0), Coeff(AbsCoeff2e7, 2.0));

	// The analytic proof itself: coarse cell i == fine cell 2i + fine cell 2i+1, cell for cell. NOT exact
	// (the two paths round different boundary-t values), hence REL 1e-5, not the CheckCells rows above.
	const TArray<float>& FineMetres = Fine.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& FineSeconds = Fine.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& DirectMetres = Direct.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& DirectSeconds = Direct.GetCanonical(ETrajectoryMapMode::RouteExposure);
	for (const int32 CoarseI : { 0, 1, 10, 19, 20 })
	{
		const double AggMetres = (double)FineMetres[2 * CoarseI] + (double)FineMetres[2 * CoarseI + 1];
		const double AggSeconds = (double)FineSeconds[2 * CoarseI] + (double)FineSeconds[2 * CoarseI + 1];
		TestTrue(FString::Printf(TEXT("coarse cell %d: aggregated fine metres == direct"), CoarseI),
			NearlyEqualRel(AggMetres, (double)DirectMetres[CoarseI], RelTol1e5));
		TestTrue(FString::Printf(TEXT("coarse cell %d: aggregated fine seconds == direct"), CoarseI),
			NearlyEqualRel(AggSeconds, (double)DirectSeconds[CoarseI], RelTol1e5));
	}
	return true;
}

// =====================================================================================================
// KERNEL (vectors_kernel.md -- authoritative per A0 standing facts)
// =====================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleKernelWeightsTest,
	"ProjectMobius.Heatmap.Trajectory.Kernel.WeightsMatchAnalyticDisc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleKernelWeightsTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	// R = DisplayPathWidthCm/2 / WorldCmPerTexel = 10/10 = 1.0 texel -> exact 3x3 stencil.
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f, 20.0f);

	const TArray<FIntPoint>& Offsets = Field.GetKernelOffsets();
	const TArray<float>& Weights = Field.GetKernelWeights();
	TestEqual(TEXT("R=1.0 texel kernel has exactly 9 taps"), Offsets.Num(), 9);
	TestEqual(TEXT("offsets/weights arrays are the same length"), Offsets.Num(), Weights.Num());

	double Sum = 0.0;
	bool bAllMatched = true;
	for (int32 i = 0; i < Offsets.Num() && i < Weights.Num(); ++i)
	{
		const int32 AbsDx = FMath::Abs(Offsets[i].X);
		const int32 AbsDy = FMath::Abs(Offsets[i].Y);
		double Expected = 0.0;
		if (AbsDx == 0 && AbsDy == 0) { Expected = 0.318309886184; }
		else if ((AbsDx == 1 && AbsDy == 0) || (AbsDx == 0 && AbsDy == 1)) { Expected = 0.145343947430; }
		else if (AbsDx == 1 && AbsDy == 1) { Expected = 0.025078581024; }
		else
		{
			bAllMatched = false;
			AddError(FString::Printf(TEXT("unexpected kernel tap offset (%d,%d) outside the 3x3 R=1.0 stencil"),
				Offsets[i].X, Offsets[i].Y));
			continue;
		}
		if (!NearlyEqualRel((double)Weights[i], Expected, RelTol1e6))
		{
			bAllMatched = false;
			AddError(FString::Printf(TEXT("kernel tap (%d,%d) weight %.9f != expected %.9f"),
				Offsets[i].X, Offsets[i].Y, (double)Weights[i], Expected));
		}
		Sum += (double)Weights[i];
	}
	TestTrue(TEXT("every tap matches the analytic disc-coverage weight"), bAllMatched);
	TestTrue(TEXT("kernel weights sum to exactly 1.0"), NearlyEqualRel(Sum, 1.0, RelTol1e6));
	return true;
}

// Border renormalisation (D-4/K-4, vectors_kernel.md §4). Mass in the grid's own corner cell (0,0): the
// 5 out-of-bounds taps must be redistributed onto the 4 in-bounds ones (never clamped-to-edge, never
// dropped), so Σ Presentation == Σ Canonical even at the border.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleKernelBorderTest,
	"ProjectMobius.Heatmap.Trajectory.Kernel.BorderTapsRenormalise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleKernelBorderTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(100.0, 100.0, 10.0f, 20.0f);
	Field.DepositSegment(FVector2D(2, 3), FVector2D(7, 8), 0.1f); // T-DDA-3, lands in corner cell (0,0)

	// A0/A8: cell sum, not the field's counter - the border-renormalisation claim is that the
	// PRESENTATION redistributes exactly the mass the CELLS hold.
	const double M = SumCanonical(Field, ETrajectoryMapMode::RouteUsage); // == 0.0707106781187
	const TArray<float>& Presentation = Field.GetPresentation(ETrajectoryMapMode::RouteUsage);
	const FIntPoint Dims = Field.GetGridDims();
	auto At = [&](int32 X, int32 Y) { return (double)Presentation[Y * Dims.X + X]; };

	TestTrue(TEXT("(0,0) renormalised weight 0.502005603"), NearlyEqualRel(At(0, 0) / M, 0.502005603, RelTol1e5));
	TestTrue(TEXT("(1,0) renormalised weight 0.229221520"), NearlyEqualRel(At(1, 0) / M, 0.229221520, RelTol1e5));
	TestTrue(TEXT("(0,1) renormalised weight 0.229221520"), NearlyEqualRel(At(0, 1) / M, 0.229221520, RelTol1e5));
	TestTrue(TEXT("(1,1) renormalised weight 0.039551358"), NearlyEqualRel(At(1, 1) / M, 0.039551358, RelTol1e5));

	const double PresentationSum = SumArray(Presentation);
	TestTrue(TEXT("Sum Presentation == Sum Canonical even at the border (1e-5 relative, per vectors_kernel.md §4)"),
		NearlyEqualRel(PresentationSum, M, RelTol1e5));
	return true;
}

// =====================================================================================================
// RANGE / LIFECYCLE. No oracle vector exists for this group -- these assert the header's own documented
// API contract (Clear's effect, EncodeToDisplay's auto-exposure ceiling, run-to-run determinism) rather
// than a physics-derived number, since T5_TEST_REWRITE.md and the oracle files never define T-SAT-1,
// T-CLR-1 or T-DET-1's expected VALUES -- only their names. No number here was obtained by running the
// implementation and copying its output; every assertion is either an exact repeat-arithmetic sum or a
// documented invariant from TrajectoryField.h's own comments.
// =====================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleSat1Test,
	"ProjectMobius.Heatmap.Trajectory.Lifecycle.T_SAT_1_NoClampAutoExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleSat1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	// T-SAT-1 row: D + order, REL 1e-4 at k <= 1e3, ABS 2e-7*k*L; strict-monotone is additionally safe out
	// to k < 2^24, but the VALUE check (not just monotonicity) is only meaningful up to k = 1e3, so this
	// stays at exactly that boundary rather than the far-past-saturation k = 2000 an earlier revision used.
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);

	constexpr int32 Repeats = 1000; // far past the old uint8 path's ~77-pass saturation point
	constexpr double PerDepositL = 0.0707106781187; // T-DDA-3, single cell (0,0)
	double PreviousTotal = 0.0;
	bool bStrictlyMonotone = true;
	for (int32 i = 0; i < Repeats; ++i)
	{
		Field.DepositSegment(FVector2D(2, 3), FVector2D(7, 8), 0.1f);
		// A0/A8 fix: monotonicity must be observed on the CELL, not on the field's double counter. The
		// counter rises no matter what the cell does, so the previous form passed even for a build that
		// re-clamped PersonMetres[] to a uint8 range - which is the single thing T-SAT-1 exists to forbid.
		// All 1000 deposits land in cell (0,0) (T-DDA-3's geometry), so cell 0 IS the accumulator here.
		const double CurrentTotal = (double)Field.GetCanonical(ETrajectoryMapMode::RouteUsage)[0];
		if (i > 0 && !(CurrentTotal > PreviousTotal))
		{
			bStrictlyMonotone = false;
		}
		PreviousTotal = CurrentTotal;
	}
	TestTrue(TEXT("strictly monotone: every deposit increases the CELL value, never clamps"), bStrictlyMonotone);

	const double ExpectedTotal = (double)Repeats * PerDepositL; // this IS "k*L" from the master table's recipe
	TestTrue(TEXT("canonical PersonMetres CELL keeps growing linearly, no ceiling"),
		NearlyEqualHybrid((double)Field.GetCanonical(ETrajectoryMapMode::RouteUsage)[0], ExpectedTotal,
			RelTol1e4Order, Coeff(AbsCoeff2e7, ExpectedTotal)));
	// Deliberately NOT asserted: "cell value > 255". The old ceiling was on the encoded BYTE (24 + hits),
	// which clipped after ~77 passes; the canonical cell holds person-metres (1000 deposits = 70.71 m), so
	// comparing it to 255 would be a units error. The linear k*L check above IS the no-saturation claim -
	// the old path could not have reported 1000 distinct pass counts at all.

	TArray<uint8> Buffer;
	Field.EncodeToDisplay(ETrajectoryMapMode::RouteUsage, Buffer);
	uint8 MaxRed = 0;
	for (int32 i = 0; i + FTrajectoryField::ChannelOffsetR < Buffer.Num(); i += FTrajectoryField::BytesPerPixel)
	{
		MaxRed = FMath::Max(MaxRed, Buffer[i + FTrajectoryField::ChannelOffsetR]);
	}
	TestEqual(TEXT("the single hot cell auto-exposes to exactly byte 255 (no fixed clip point)"), MaxRed,
		static_cast<uint8>(255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleClr1Test,
	"ProjectMobius.Heatmap.Trajectory.Lifecycle.T_CLR_1_ClearZeroesEverything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleClr1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);
	Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);
	Field.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 5.0f); // 5.0f not 10.0f: ruling A0-22,
	// a single 10 s segment is rejected by the delta-t gate and this cell would never be populated,
	// making every assertion about it vacuous.
	TestTrue(TEXT("sanity: something accumulated before Clear"), Field.GetTotalPersonMetres() > 0.0);

	const FIntPoint DimsBefore = Field.GetGridDims();
	const float CmPerTexelBefore = Field.GetEffectiveCmPerTexel();

	Field.Clear();

	TestTrue(TEXT("Total metres zeroed"), NearlyEqualAbs(Field.GetTotalPersonMetres(), 0.0, 0.0));
	TestTrue(TEXT("Total seconds zeroed"), NearlyEqualAbs(Field.GetTotalPersonSeconds(), 0.0, 0.0));
	TestTrue(TEXT("Dropped zeroed"), Field.GetDroppedPersonMetres() == 0.0 && Field.GetDroppedPersonSeconds() == 0.0);
	TestTrue(TEXT("Rejected zeroed"), Field.GetRejectedPersonMetres() == 0.0 && Field.GetRejectedPersonSeconds() == 0.0);
	TestTrue(TEXT("Negligible zeroed"), Field.GetNegligiblePersonMetres() == 0.0);
	TestEqual(TEXT("rejected segment count zeroed"), Field.GetRejectedSegmentCount(), 0);
	TestEqual(TEXT("stationary segment count zeroed"), Field.GetStationarySegmentCount(), 0);
	// T-CLR-1 row: FORM E, == 0.0f plus an IsFinite sweep.
	CheckCellsExact(*this, Field, TEXT("T-CLR-1 zeroed cells"),
		{ {0, 0, 0.0, 0.0}, {1, 0, 0.0, 0.0}, {2, 0, 0.0, 0.0}, {3, 4, 0.0, 0.0} });
	bool bAllFinite = true;
	for (const float V : Field.GetCanonical(ETrajectoryMapMode::RouteUsage))
	{
		bAllFinite &= FMath::IsFinite(V);
	}
	for (const float V : Field.GetCanonical(ETrajectoryMapMode::RouteExposure))
	{
		bAllFinite &= FMath::IsFinite(V);
	}
	TestTrue(TEXT("IsFinite sweep: no NaN/inf survives Clear"), bAllFinite);

	TestTrue(TEXT("grid dims preserved across Clear"),
		Field.GetGridDims().X == DimsBefore.X && Field.GetGridDims().Y == DimsBefore.Y);
	TestEqual(TEXT("effective cm/texel preserved across Clear"), Field.GetEffectiveCmPerTexel(), CmPerTexelBefore, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracleDet1Test,
	"ProjectMobius.Heatmap.Trajectory.Lifecycle.T_DET_1_Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracleDet1Test::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	auto DepositSequence = [](FTrajectoryField& Field)
	{
		Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 5), 0.4f);
		Field.DepositSegment(FVector2D(5, 5), FVector2D(25, 25), 0.5f);
		Field.DepositSegment(FVector2D(1, 2), FVector2D(121, 22), 0.6f);
		Field.DepositSegment(FVector2D(37, 42), FVector2D(37, 42), 5.0f); // 5.0f not 10.0f: ruling A0-22,
	// a single 10 s segment is rejected by the delta-t gate and this cell would never be populated,
	// making every assertion about it vacuous.
	};

	FTrajectoryField A = MakeField(1000.0, 1000.0, 10.0f);
	FTrajectoryField B = MakeField(1000.0, 1000.0, 10.0f);
	DepositSequence(A);
	DepositSequence(B);

	const TArray<float>& MetresA = A.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& MetresB = B.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& SecondsA = A.GetCanonical(ETrajectoryMapMode::RouteExposure);
	const TArray<float>& SecondsB = B.GetCanonical(ETrajectoryMapMode::RouteExposure);
	bool bIdentical = MetresA.Num() == MetresB.Num() && SecondsA.Num() == SecondsB.Num();
	for (int32 i = 0; bIdentical && i < MetresA.Num(); ++i)
	{
		bIdentical = (MetresA[i] == MetresB[i]) && (SecondsA[i] == SecondsB[i]);
	}
	TestTrue(TEXT("identical deposit sequence -> bit-identical canonical arrays"), bIdentical);

	TArray<uint8> BufferA, BufferB;
	A.EncodeToDisplay(ETrajectoryMapMode::RouteUsage, BufferA);
	B.EncodeToDisplay(ETrajectoryMapMode::RouteUsage, BufferB);
	TestTrue(TEXT("identical deposit sequence -> bit-identical encoded display buffer"),
		BufferA.Num() == BufferB.Num() && FMemory::Memcmp(BufferA.GetData(), BufferB.GetData(), BufferA.Num()) == 0);
	return true;
}

// =====================================================================================================
// REGRESSION GUARDS added by A0 from A8's adversarial review. Both cover defects that the 24 tests above
// could not have caught, because every one of them uses a square grid, an origin of (0,0), and one of
// only three kernel radii.
// =====================================================================================================

// Kernel half extent must be ceil(R - 0.5), not floor(R). The two formulas AGREE at every radius the
// oracle tabulates -- R = 1.0 (20 cm @ 10), R = 2.5 (50 cm @ 10), R = 0.4 (20 cm @ 25) -- so the bug this
// guards was invisible to the whole suite. R = 1.6 separates them: cells at |d| = 2 hold real disc area
// (2 - 0.5 = 1.5 < 1.6), giving 21 non-zero taps, where floor(R) = 1 would build only 9. Mass stays
// conserved either way (the border renormalisation absorbs the difference), so no conservation assertion
// can see it -- it shows up only as a stroke narrower than DisplayPathWidthCm asks for, i.e. a silent FR3
// violation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajKernelHalfExtentTest,
	"ProjectMobius.Heatmap.Trajectory.Kernel.HalfExtentCoversPartialRing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajKernelHalfExtentTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	// 32 cm display width at 10 cm/texel -> R = 32 / (2 * 10) = 1.6 texels.
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f, /*DisplayPathWidthCm*/ 32.0f);
	TestTrue(TEXT("R == 1.6 texels"), NearlyEqualRel(Field.GetKernelRadiusTexels(), 1.6, 1.0e-6));

	const TArray<FIntPoint>& Offsets = Field.GetKernelOffsets();
	const TArray<float>& Weights = Field.GetKernelWeights();
	TestEqual(TEXT("R = 1.6 builds 21 taps (floor(R) would build 9)"), Offsets.Num(), 21);

	// The discriminating taps: the outer ring at |d| = 2 on an axis exists only under ceil(R - 0.5).
	double OuterWeight = 0.0;
	for (int32 i = 0; i < Offsets.Num(); ++i)
	{
		if (FMath::Abs(Offsets[i].X) == 2 || FMath::Abs(Offsets[i].Y) == 2)
		{
			OuterWeight += (double)Weights[i];
		}
	}
	TestTrue(TEXT("the |d| = 2 partial-area ring carries real weight"), OuterWeight > 0.0);
	TestTrue(TEXT("kernel weights still sum to 1.0"),
		NearlyEqualAbs(SumArray(Weights), 1.0, 1.0e-6));
	// The corners at (+-2,+-2) are genuinely outside: nearest corner (1.5,1.5) is at 2.121 > 1.6.
	bool bHasDiagonalCorner = false;
	for (const FIntPoint& O : Offsets)
	{
		bHasDiagonalCorner |= (FMath::Abs(O.X) == 2 && FMath::Abs(O.Y) == 2);
	}
	TestFalse(TEXT("(+-2,+-2) is excluded - it holds no disc area at R = 1.6"), bHasDiagonalCorner);
	return true;
}

// Byte 0 is reserved for "no data". The band scheme's lowest bucket keys on a zero value, so a cell that
// actually holds mass must never encode to 0 - otherwise a lightly-used route is indistinguishable from
// ground nobody walked on. This is the Tier A form of a failure that first showed up on real data, where
// ~1700 touched texels of a 1000-agent capture were rounding away to zero.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajEncodeReservesZeroTest,
	"ProjectMobius.Heatmap.Trajectory.Lifecycle.EncodeReservesZeroForNoData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajEncodeReservesZeroTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	FTrajectoryField Field = MakeField(1000.0, 1000.0, 10.0f);

	// One heavily-used cell plus one very faint route far from it. Auto-exposure scales to the hot cell, so
	// the faint cells land far below half a byte - which is exactly the case that used to vanish.
	for (int32 i = 0; i < 500; ++i)
	{
		Field.DepositSegment(FVector2D(505, 505), FVector2D(515, 505), 0.1f);
	}
	Field.DepositSegment(FVector2D(25, 25), FVector2D(95, 25), 0.5f);

	TArray<uint8> Buffer;
	Field.EncodeToDisplay(ETrajectoryMapMode::RouteUsage, Buffer);

	const TArray<float>& Presentation = Field.GetPresentation(ETrajectoryMapMode::RouteUsage);
	const int32 NumCells = Field.GetGridDims().X * Field.GetGridDims().Y;
	int32 PositiveCells = 0;
	int32 VanishedCells = 0;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		if (Presentation[Index] > 0.0f)
		{
			++PositiveCells;
			if (Buffer[Index * FTrajectoryField::BytesPerPixel + FTrajectoryField::ChannelOffsetR] == 0)
			{
				++VanishedCells;
			}
		}
	}
	TestTrue(TEXT("the faint route did produce presentation mass"), PositiveCells > 10);
	TestEqual(TEXT("no cell holding mass encodes to byte 0 (0 means no-data, exclusively)"), VanishedCells, 0);

	// The floor must not touch genuinely empty cells: byte 0 has to still mean something.
	int32 EmptyButNonZeroByte = 0;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		if (Presentation[Index] == 0.0f
			&& Buffer[Index * FTrajectoryField::BytesPerPixel + FTrajectoryField::ChannelOffsetR] != 0)
		{
			++EmptyButNonZeroByte;
		}
	}
	TestEqual(TEXT("empty cells still encode to exactly 0"), EmptyButNonZeroByte, 0);
	return true;
}

// Non-square grid, NEGATIVE origin. Every other test in this file uses a square field at origin (0,0), so
// a transposed row stride (x and y swapped in the index), a dropped origin offset, or a sign error on
// negative world coordinates would all pass the entire suite -- sums are blind to all three. The
// construction is deliberately T-DDA-1's geometry shifted into a negative-origin frame, so the expected
// per-cell numbers are the ones the oracle already derived and verified: 0.05 / 0.10 / 0.05 metres.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajNonSquareNegativeOriginTest,
	"ProjectMobius.Heatmap.Trajectory.Resolution.NonSquareGridNegativeOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajNonSquareNegativeOriginTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	// 200 m x 40 m floor at 10 cm/texel, minimum corner at (-7000, -3000) cm.
	FTrajectoryField Field = MakeField(20000.0, 4000.0, 10.0f, 20.0f, /*OriginX*/ -7000.0, /*OriginY*/ -3000.0);
	const FIntPoint Dims = Field.GetGridDims();
	TestEqual(TEXT("grid is 2000 wide"), Dims.X, 2000);
	TestEqual(TEXT("grid is 400 tall (NOT square, and not transposed)"), Dims.Y, 400);

	// World (-6995,-2995) -> grid (0.5, 0.5); world (-6975,-2995) -> grid (2.5, 0.5). 20 cm, 0.4 s.
	Field.DepositSegment(FVector2D(-6995, -2995), FVector2D(-6975, -2995), 0.4f);
	CheckCells(*this, Field, TEXT("non-square/neg-origin"),
		{ {0, 0, 0.05, 0.1}, {1, 0, 0.1, 0.2}, {2, 0, 0.05, 0.1} });
	TestTrue(TEXT("Sum PersonMetres CELLS == L (0.20 m)"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteUsage), 0.20, RelTol1e6,
			Coeff(AbsCoeff2e7, 0.20)));

	// Nothing may leak into the row above: a transposed stride would put mass at y = 1..2 instead of x.
	const TArray<float>& Metres = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
	TestTrue(TEXT("row 1 is untouched (a transposed index would have deposited there)"),
		Metres[1 * Dims.X + 0] == 0.0f && Metres[2 * Dims.X + 0] == 0.0f);

	// A point below the origin is OUTSIDE this grid and must be dropped, not folded onto the border.
	FTrajectoryField Off = MakeField(20000.0, 4000.0, 10.0f, 20.0f, -7000.0, -3000.0);
	Off.DepositSegment(FVector2D(-9000, -5000), FVector2D(-8980, -5000), 0.4f);
	TestTrue(TEXT("a segment left of and below the origin deposits nothing"),
		SumCanonical(Off, ETrajectoryMapMode::RouteUsage) == 0.0);
	TestTrue(TEXT("and it is booked as DROPPED, not silently lost"),
		NearlyEqualHybrid(Off.GetDroppedPersonMetres(), 0.20, RelTol1e6, Coeff(AbsCoeff2e7, 0.20)));
	return true;
}

// =====================================================================================================
// T-OFFSET-2 -- THE MESH MUST SPAN EXACTLY THE HEATMAP'S WORLD EXTENT.
//
// `BuildTileBuffers` places vertex gx at `gx * CellSize` for gx = 0 .. NumTriangles-1, and gives it
// `UV = gx / (NumTriangles - 1)`, so UV 0..1 covers the WHOLE texture across a mesh spanning
// `(NumTriangles - 1) * CellSize`. The texture in turn covers the whole field extent. So unless
//
//     (NumTriangles - 1) * CellSize == MeshSize
//
// the texture is stretched across the wrong distance and the image shifts by
// `fraction_across_mesh * error` -- zero at the origin corner, worst at the far one.
//
// `GenerateSquareCellSize` divided by `NumberOfTriangles` rather than `NumberOfTriangles - 1` until
// 2026-08-05, making the mesh exactly ONE CELL short. On a 200 m carrier that is 25 cm at the far edge,
// 2.5 texels at 10 cm/texel, and it was visible on screen (A0-56 / A0-60).
//
// Nothing in the suite could see it. Every conservation criterion sums the field, and sums are
// POSITION-BLIND -- the same blind spot that left the row-orientation question open for weeks. This test
// exists so a one-character change to that divisor fails loudly instead of quietly skewing every heatmap.
//
// -----------------------------------------------------------------------------------------------------
// REWRITTEN 2026-08-07 (A0-79). IT WAS GATING A FUNCTION NOTHING CALLS.
//
// The version before this took its vertex count from `CalculateNumberOfTriangles`, which has ZERO callers
// in the shipping path -- C++ or Blueprint. The path that actually builds every heatmap is
// `GenerateMeshVerticesUVsAndTriangles` -> `ComputeHeatmapVertexGrid` -> `GenerateSquareCellSize`, and the
// two disagree: the live one TRUNCATES `MeshSize/25`, the dead one CEILS it. So the gate asserted a pairing
// between one dead function and one live one, and a regression in the live pair could not redden it.
//
// Two consequences worth stating so this does not get "restored":
//   * `TextureSize` is gone from this test. It only ever existed to satisfy the dead function's signature;
//     texture size does not enter the shipping grid at all.
//   * the old `(137, 26)` case is gone and 50 cm replaces it. It passed only BECAUSE of the ceil: trunc
//     gives 26/25 = 1 vertex on Y, which is below the two this test requires. That is not a test bug, it is
//     production behaviour, and it is pinned explicitly in the degeneracy block at the bottom instead.
//
// The `/250` formula named alongside `/25` in the open item no longer exists in production -- A0-64 made
// generation always dense, so there is no second live formula to gate here.
//
// Extents stay deliberately awkward (non-square, primes, extreme aspect) even though truncation replaced
// ceiling: those are the extents where `NumTriangles - 1` and `MeshSize/25` part company.
// =====================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajMeshSpanMatchesExtentTest,
	"ProjectMobius.Heatmap.Trajectory.Offset.MeshSpanMatchesExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajMeshSpanMatchesExtentTest::RunTest(const FString& Parameters)
{
	// Extents in cm. Square, non-square, and several that 25 does not divide evenly.
	const TArray<FVector2D> Extents = {
		FVector2D(20000.0, 20000.0),   // the 200 m capture carrier
		FVector2D(7300.0, 7300.0),     // PRD's 73 m reference floor
		FVector2D(6720.0, 5300.0),     // the technical school's real extent, non-square
		FVector2D(2000.0, 2000.0),     // small floor
		FVector2D(10000.0, 2500.0),    // extreme aspect ratio
		FVector2D(1013.0, 787.0),      // primes: 25 divides neither
		FVector2D(50.0, 50.0),         // the exact floor: trunc(50/25) = 2, the fewest verts that span
	};

	for (const FVector2D& Extent : Extents)
	{
		// The shipping pair, both taken from production rather than restated here.
		const FIntPoint NumTriangles = AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(Extent);
		const FVector2D CellSize =
			AHeatmapPixelTextureVisualizer::GenerateSquareCellSize(NumTriangles, Extent);

		// Must mirror BuildTileBuffers exactly: gx runs 0..NumTriangles-1, vertex at gx * CellSize.
		// That the tiler really does stop at NumTriangles-1 is not assumed from reading it -- A0-77
		// MEASURED a fully-kept 200x200 grid emitting 40000 verts and 79202 tris = 2 * 199^2.
		const double SpanX = static_cast<double>(NumTriangles.X - 1) * CellSize.X;
		const double SpanY = static_cast<double>(NumTriangles.Y - 1) * CellSize.Y;
		const double ErrX = SpanX - Extent.X;
		const double ErrY = SpanY - Extent.Y;

		// Reported, not asserted: the cell is ALWAYS wider than the nominal 25 cm, because dividing by
		// (N-1) spreads the extent over one fewer gap than the count suggests. 25.13 cm at 50 m, 33.3 cm
		// on a 1 m floor. `cell >= 25` is true by construction so asserting it proves nothing, and any
		// tighter bound would redden on small floors for no defect.
		AddInfo(FString::Printf(
			TEXT("extent %.0f x %.0f cm -> verts %d x %d, cell %.6f x %.6f (nominal 25), ")
			TEXT("span %.4f x %.4f, error %+.4f x %+.4f cm"),
			Extent.X, Extent.Y, NumTriangles.X, NumTriangles.Y,
			CellSize.X, CellSize.Y, SpanX, SpanY, ErrX, ErrY));

		// 0.01 cm = 0.1 mm: float accumulation noise only. A divisor mistake is a whole cell (~25 cm),
		// four orders larger, so this tolerance cannot hide one.
		TestTrue(FString::Printf(
			TEXT("mesh X span equals the extent for %.0f cm (error %+.4f cm, tolerance 0.01)"),
			Extent.X, ErrX), FMath::Abs(ErrX) < 0.01);
		TestTrue(FString::Printf(
			TEXT("mesh Y span equals the extent for %.0f cm (error %+.4f cm, tolerance 0.01)"),
			Extent.Y, ErrY), FMath::Abs(ErrY) < 0.01);

		// Guards the FIXTURE LIST, not production: every extent here must stay above the degeneracy floor,
		// or the span assertions above become meaningless for it. Add a sub-50 cm extent and this fails
		// first, with a clear reason, instead of a confusing span error further up.
		//
		// It used to be justified as covering `Max(1, n-1)` in GenerateSquareCellSize "silently returning
		// the full extent as the cell". That is no longer what it does: at N >= 2 the Max is a no-op, and
		// the sub-floor cases that DO reach it are pinned in the degeneracy block at the bottom instead.
		TestTrue(FString::Printf(TEXT("extent %.0f cm yields at least 2 vertices per axis"), Extent.X),
			NumTriangles.X >= 2 && NumTriangles.Y >= 2);
	}

	// -------------------------------------------------------------------------------------------------
	// PROVE THE GATE HAS TEETH.
	//
	// Read plainly, everything above is a tautology: `GenerateSquareCellSize` divides by (N-1) and the span
	// multiplies by (N-1), so span == extent for ANY N >= 2 and no extent can ever fail it. That is exactly
	// why it is worth having -- the thing under test is the DIVISOR, not the arithmetic -- but a reader who
	// spots the tautology and not the reason deletes the test. So run the pre-2026-08-05 form here and show
	// it fails: dividing by N instead of (N-1) shortens the mesh by one cell while its UVs still span the
	// whole texture, which is the A0-56/A0-60 defect the owner saw as a ~2-texel shift at the far corner.
	// -------------------------------------------------------------------------------------------------
	{
		const FVector2D Extent(20000.0, 20000.0);
		const FIntPoint NumTriangles = AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(Extent);
		const FVector2D CellSize =
			AHeatmapPixelTextureVisualizer::GenerateSquareCellSize(NumTriangles, Extent);

		const double LegacyCellX = Extent.X / FMath::Max(1, NumTriangles.X); // the OLD divisor: N, not N-1
		const double LegacySpanX = static_cast<double>(NumTriangles.X - 1) * LegacyCellX;
		const double LegacyErrX = LegacySpanX - Extent.X;

		AddInfo(FString::Printf(
			TEXT("non-vacuity: pre-2026-08-05 divisor on a %.0f cm carrier gives cell %.6f, span %.4f, ")
			TEXT("error %+.4f cm against a live cell of %.6f"),
			Extent.X, LegacyCellX, LegacySpanX, LegacyErrX, CellSize.X));

		// Scaled to the cell rather than hard-coded to 25 cm, so this stays honest if the density changes.
		// The error it must exceed is ~2500x the 0.01 cm tolerance the assertions above run at.
		TestTrue(TEXT("the pre-2026-08-05 divisor misses the extent by about one cell, so a regression to ")
			TEXT("it would redden the assertions above rather than slip through"),
			FMath::Abs(LegacyErrX) > 0.9 * CellSize.X);
	}

	// -------------------------------------------------------------------------------------------------
	// WHERE THE SPAN INVARIANT STOPS HOLDING, AND THE FACT THAT PRODUCTION DOES NOT SAY SO.
	//
	// GenerateMeshVerticesUVsAndTriangles' comment says /25 "stays >= 2 vertices from 50 cm up, so every
	// plausible floor is safe". True, and this pins the boundary rather than trusting the sentence. What
	// the comment does not say is what happens BELOW it, so record that here:
	//
	//   extent >= 50 cm  -> N >= 2, mesh spans the extent exactly (asserted above)
	//   25..49 cm        -> N == 1. `bValidTriangles` only rejects N == 0, so the guard PASSES, Max(1, N-1)
	//                       returns 1, the cell loop runs zero times, and the actor emits an EMPTY mesh
	//                       with no error and no user feedback. Silent.
	//   < 25 cm          -> N == 0, caught by bValidTriangles, reported through the feedback subsystem.
	//
	// The silent band needs a floor smaller than a doorway, so it is not worth code today -- but it is a
	// hole in a guard that looks total, and nothing else in the tree writes it down.
	// -------------------------------------------------------------------------------------------------
	{
		const FIntPoint AtFloor = AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(50.0, 50.0));
		TestTrue(TEXT("50 cm is exactly the smallest extent that yields 2 vertices per axis"),
			AtFloor.X == 2 && AtFloor.Y == 2);

		const FIntPoint BelowFloor =
			AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(49.99, 49.99));
		TestTrue(TEXT("just below 50 cm the grid drops to 1 vertex per axis, which spans nothing -- this is ")
			TEXT("the documented floor of the /25 density, not a defect"),
			BelowFloor.X == 1 && BelowFloor.Y == 1);

		const FIntPoint Empty = AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(24.0, 24.0));
		TestTrue(TEXT("below 25 cm the grid is 0, which bValidTriangles rejects and reports"),
			Empty.X == 0 && Empty.Y == 0);
	}

	return true;
}

// =====================================================================================================
// T-OFFSET-1 -- IS THE DEPOSITED MASS LATERALLY BIASED AGAINST THE TRUE PATH?
//
// Owner observed 2026-08-05 that the stroke width looks right but sits off-centre from the agent, to one
// side (ruling A0-56). Three stages could introduce that, and they need different fixes:
//
//   A. the FIELD          -- the DDA placing deposited mass half a texel off             <- THIS TEST
//   B. the RENDER         -- texel -> UV -> mesh mapping putting cell 0 at a texel EDGE rather than its
//                            CENTRE (`UVy = gy/(N-1)` is the suspect form). Not reachable from here.
//   C. the PRODUCER       -- the deposited point not being the agent's visual centre (entity transform
//                            vs mesh pivot). Not reachable from here either.
//
// This test settles A definitively, so a failure localises the bug and a pass EXONERATES the field and
// sends the search to B or C. That is its whole purpose: none of the conservation criteria can fail on a
// lateral shift, because depositing the right length along a shifted path conserves person-metres exactly.
//
// WHAT IS ACTUALLY MEASURED -- and the test ID oversells the stroke. Every assertion reads
// GetCanonical(...), the per-cell DDA integral, and NOT the presentation stroke. DisplayPathWidthCm sizes
// the presentation kernel only (TrajectoryField.cpp:250) and is INERT in everything below; it is still
// passed so the field is configured as it ships, and so this test keeps measuring the right thing if it
// is ever pointed at the presentation array. So the +/- half-texel bound below is CELL QUANTISATION, not
// a consequence of stroke width -- it would read the same if the stroke were 30 cm wide. The ID
// "StrokeCentroidBias" is kept unchanged so A0-57/A0-58's recorded numbers stay comparable.
//
// METHOD, and the reason it is not simply "deposit one path and look":
// a single path cannot distinguish a real bias from ordinary quantisation. Mass lands in the cell the
// path crosses, so a path anywhere inside that cell reads up to half a texel off, which is correct
// behaviour. So sweep the path across one full texel in sub-texel phases and take the MEAN signed offset.
// Symmetric quantisation averages to ~0. A half-texel convention error averages to +/-5 cm at 10 cm/texel
// and is unmissable. Max |offset| is reported separately: half a texel is the accuracy limit of
// cell-resolution deposition and not a defect, so that is asserted as a bound -- but the bound alone
// cannot tell midpoint sampling from edge sampling (5.0000 satisfies it just as 4.9750 does), so the
// exact value is pinned too. It is the one observable that would redden on a revert to the old sweep.
//
// SAMPLE THE TEXEL AT ITS MIDPOINTS, NOT FROM ITS EDGE (A0-80, fixing the sweep A0-58 measured).
// The offset is a sawtooth in the path's sub-texel position: +5 cm just after a grid line, falling to
// -5 cm just before the next. Its mean over a period is 0, but it is DISCONTINUOUS at the line, where the
// ratified lower-index rule picks the -5 end. The original sweep started at 500.0 -- an exact grid line --
// so one of its 20 samples read -5 while the other 19 summed to 0, and both axes reported a spurious
// -5/20 = -0.2500 cm. Sampling at (Phase + 0.5)/Phases straddles no discontinuity and the mean is 0.
//
// A0-58's other suggested fix -- "run Phases + 1 samples and drop the endpoint" -- does NOT work, and is
// corrected here rather than left for the next reader: BOTH ends of the period are grid lines. Trapezoid
// over p = 0..20 gives (0.5*(-5) + 0 + 0.5*(-5))/20 = -0.25, and dropping the FIRST sample instead leaves
// p = 20 sitting on 510, also a grid line, also -0.25. Only midpoint sampling steps around it.
//
// RESOLUTION -- THIS STATISTIC IS A STAIRCASE, so the tolerance is derived from the sweep, not chosen.
// For an injected bias B the reading is s * round(B/s), with s the sweep step and an exact half-step tie
// going DOWN, because the one sample that then lands on a grid line is booked to the lower-index cell. It
// can only ever report MULTIPLES OF THE STEP: a bias of s/2 or smaller reads exactly zero no matter how
// tight the assertion, so asserting tighter than s/2 is theatre. The lever is the phase count, and only
// the phase count.
//
// Phases was 20, so s = 0.5 cm. A 0.25 cm bias read exactly 0.0000 and anything above it read exactly
// 0.5000 -- against a tolerance of 0.5. The first detectable fault therefore landed on EQUALITY with the
// bound it had to break, and everything at or below 0.25 cm was invisible. 200 phases give s = 0.05 cm
// and a tolerance of s/2 = 0.025 cm = 0.25 mm; the smallest detectable fault now reads 0.0500, twice the
// bound, so detection has a factor-of-two margin instead of resting on a comparison of equals. That is
// the sensitivity A0-58 asked for. Both ends are pinned below: the injected-bias case proves the
// statistic reports a real fault at full size, and the sub-resolution case pins the floor so it cannot be
// mistaken for millimetre accuracy that is not there.
// =====================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajStrokeCentroidBiasTest,
	"ProjectMobius.Heatmap.Trajectory.Offset.StrokeCentroidBias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajStrokeCentroidBiasTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	constexpr float CmPerTexel = 10.0f;   // shipping default
	constexpr float WidthCm    = 10.0f;   // shipping default after A0-47 -- INERT here, see the header
	constexpr int32 Phases     = 200;     // sub-texel phases across one whole texel
	constexpr double HalfTexel = CmPerTexel * 0.5;
	// The sweep's own step, and the resolution of every mean it reports. Both derived from Phases so a
	// change to the phase count moves the tolerance with it instead of leaving a stale literal behind.
	constexpr double SweepStepCm     = static_cast<double>(CmPerTexel) / Phases;  // 0.05 cm
	constexpr double BiasToleranceCm = 0.5 * SweepStepCm;                         // 0.025 cm = 0.25 mm

	// Returns { mean signed offset, max abs offset } in cm for a path running along one axis.
	// BiasCm displaces the DEPOSITED path without moving the reference it is measured against. Zero for
	// the real measurement; non-zero to inject a known fault and prove the statistic can see it.
	auto MeasureAxis = [this](bool bAlongX, double BiasCm) -> TPair<double, double>
	{
		double SumOffset = 0.0;
		double MaxAbsOffset = 0.0;
		for (int32 Phase = 0; Phase < Phases; ++Phase)
		{
			// Lateral coordinate of the true path, swept across exactly one texel at the MIDPOINT of each
			// sub-interval -- never on a grid line, where the offset function is discontinuous.
			const double TrueLateral =
				500.0 + (static_cast<double>(CmPerTexel) * (static_cast<double>(Phase) + 0.5)) / Phases;
			const double DepositLateral = TrueLateral + BiasCm;
			FTrajectoryField Field = MakeField(1000.0, 1000.0, CmPerTexel, WidthCm);
			if (bAlongX)
			{
				Field.DepositSegment(FVector2D(100.0, DepositLateral), FVector2D(900.0, DepositLateral), 1.0f);
			}
			else
			{
				Field.DepositSegment(FVector2D(DepositLateral, 100.0), FVector2D(DepositLateral, 900.0), 1.0f);
			}

			const TArray<float>& Values = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
			const FIntPoint Dims = Field.GetGridDims();
			double Mass = 0.0;
			double Moment = 0.0;
			for (int32 J = 0; J < Dims.Y; ++J)
			{
				for (int32 I = 0; I < Dims.X; ++I)
				{
					const double Weight = Values[J * Dims.X + I];
					if (Weight > 0.0)
					{
						// Cell centre in cm: a cell spans [i, i+1) texels, so its centre is (i + 0.5).
						const double LateralCentre = bAlongX
							? (static_cast<double>(J) + 0.5) * CmPerTexel
							: (static_cast<double>(I) + 0.5) * CmPerTexel;
						Mass += Weight;
						Moment += Weight * LateralCentre;
					}
				}
			}
			if (Mass <= 0.0)
			{
				AddError(FString::Printf(TEXT("phase %d deposited no mass at all"), Phase));
				return TPair<double, double>(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			}
			const double Offset = (Moment / Mass) - TrueLateral;
			SumOffset += Offset;
			MaxAbsOffset = FMath::Max(MaxAbsOffset, FMath::Abs(Offset));
		}
		return TPair<double, double>(SumOffset / Phases, MaxAbsOffset);
	};

	const TPair<double, double> AlongX = MeasureAxis(/*bAlongX*/ true, 0.0);   // measures the Y offset
	const TPair<double, double> AlongY = MeasureAxis(/*bAlongX*/ false, 0.0);  // measures the X offset

	AddInfo(FString::Printf(
		TEXT("lateral offset over %d sub-texel phases at %.1f cm/texel (step %.4f cm, tolerance %.4f cm): ")
		TEXT("Y mean=%+.4f cm max=%.4f cm | X mean=%+.4f cm max=%.4f cm (half texel = %.1f cm)"),
		Phases, CmPerTexel, SweepStepCm, BiasToleranceCm,
		AlongX.Key, AlongX.Value, AlongY.Key, AlongY.Value, HalfTexel));

	// The real assertion. Half a sweep step is the finest bias the staircase can resolve at all, so it is
	// the tightest HONEST bound -- and it is 200x below the 5 cm a half-texel convention error produces.
	TestTrue(FString::Printf(TEXT("deposit is not laterally biased along Y (mean %+.4f cm, tolerance %.4f)"),
		AlongX.Key, BiasToleranceCm), FMath::Abs(AlongX.Key) < BiasToleranceCm);
	TestTrue(FString::Printf(TEXT("deposit is not laterally biased along X (mean %+.4f cm, tolerance %.4f)"),
		AlongY.Key, BiasToleranceCm), FMath::Abs(AlongY.Key) < BiasToleranceCm);

	// Quantisation bound -- the durable invariant. Exceeding half a texel means mass landed in a cell that
	// does not contain the path at all, which is a placement bug rather than a rounding limit. This one
	// survives any change to Phases or to the sampling scheme, which is why it is kept alongside the
	// exact check below rather than folded into it.
	TestTrue(FString::Printf(TEXT("worst-case Y offset within half a texel (%.4f <= %.1f)"),
		AlongX.Value, HalfTexel), AlongX.Value <= HalfTexel + KINDA_SMALL_NUMBER);
	TestTrue(FString::Printf(TEXT("worst-case X offset within half a texel (%.4f <= %.1f)"),
		AlongY.Value, HalfTexel), AlongY.Value <= HalfTexel + KINDA_SMALL_NUMBER);

	// ---- Pin that the sweep samples MIDPOINTS, which the bound above cannot see ---------------------
	// max is the only observable that separates midpoint sampling from the edge sampling this item
	// replaced -- edge sampling puts one sample exactly on a grid line and max reads a flat 5.0000, while
	// midpoints cap it half a step short at 4.9750. "<= HalfTexel" is satisfied by BOTH, so on its own it
	// would stay green through a straight revert to the sweep A0-58 measured, and the header's claim that
	// nothing touches the discontinuity would become quietly false. Assert the value, not just the bound.
	//
	// Derived from Phases, not written as 4.9750, so raising the phase count moves it instead of leaving a
	// stale literal. Nor is it test arithmetic restated: the extreme sample sits at 500.025 cm and reading
	// 4.9750 requires the FIELD to book that into cell 50 and to keep the whole 800 cm segment in one row.
	// A change to AxisIndexFromCoord, or a DDA that smeared the line across two rows, both move it.
	const double ExpectedMaxCm = HalfTexel - 0.5 * SweepStepCm;   // 4.9750 cm at 200 phases
	TestTrue(FString::Printf(
		TEXT("worst-case Y offset is half a texel less half a step (%.4f vs %.4f expected), i.e. the sweep ")
		TEXT("straddles the grid line rather than landing on it"), AlongX.Value, ExpectedMaxCm),
		FMath::Abs(AlongX.Value - ExpectedMaxCm) < 1.0e-6);
	TestTrue(FString::Printf(
		TEXT("worst-case X offset is half a texel less half a step (%.4f vs %.4f expected)"),
		AlongY.Value, ExpectedMaxCm),
		FMath::Abs(AlongY.Value - ExpectedMaxCm) < 1.0e-6);

	// ---- NON-VACUITY: the statistic reports a real fault at FULL SIZE ------------------------------
	// "Mean offset is zero" is what a symmetric quantiser gives away for free, so on its own it says
	// nothing about sensitivity and reads as a tautology worth deleting. Inject exactly the fault this
	// test was written to hunt -- a half-texel convention error, family A of A0-56 -- and require the
	// sweep to read it back at 5 cm, not at a fraction of it. A statistic that damped a real bias would
	// pass the assertions above while hiding the thing they exist to catch.
	{
		const TPair<double, double> BiasedX = MeasureAxis(/*bAlongX*/ true, HalfTexel);
		const TPair<double, double> BiasedY = MeasureAxis(/*bAlongX*/ false, HalfTexel);
		AddInfo(FString::Printf(
			TEXT("non-vacuity: a %.1f cm half-texel bias injected into the deposit reads back as ")
			TEXT("Y %+.4f cm | X %+.4f cm"), HalfTexel, BiasedX.Key, BiasedY.Key));

		TestTrue(FString::Printf(
			TEXT("an injected half-texel Y bias is reported 1:1 (%+.4f cm vs %.1f injected), so a real ")
			TEXT("one would redden the assertion above rather than average away"), BiasedX.Key, HalfTexel),
			FMath::Abs(BiasedX.Key - HalfTexel) < BiasToleranceCm);
		TestTrue(FString::Printf(
			TEXT("an injected half-texel X bias is reported 1:1 (%+.4f cm vs %.1f injected)"),
			BiasedY.Key, HalfTexel),
			FMath::Abs(BiasedY.Key - HalfTexel) < BiasToleranceCm);
	}

	// ---- THE RESOLUTION FLOOR, pinned rather than assumed ------------------------------------------
	// The staircase reads s * round(B/s), so a bias below half a step is genuinely INVISIBLE here. That
	// is a property of the method, not a defect -- but it is the one thing a reader could get wrong about
	// this gate, and the reason the tolerance must never be tightened on its own. Assert it, so the claim
	// is checked rather than believed, and so raising Phases is the only way to move it.
	//
	// The bound here is NOISE, not BiasToleranceCm. Asserting the sub-resolution reading against the same
	// 0.025 cm the main check uses would be the identical predicate written twice, and it would stay green
	// if deposition ever changed so a 0.02 cm bias LEAKED THROUGH partially -- the reading would become
	// ~0.02, the assertion would still pass, and the comment above it would be quietly false. The claim is
	// "exactly zero", so the bound has to be float noise: Moment/Mass carries ~5e-14 at a 505 cm centroid
	// and 200 phases accumulate to ~1e-11 worst case, so 1e-9 leaves four orders of headroom.
	{
		const double SubResolutionBias = 0.4 * SweepStepCm;   // 0.02 cm: below the s/2 = 0.025 cm floor
		const TPair<double, double> Blind = MeasureAxis(/*bAlongX*/ true, SubResolutionBias);
		AddInfo(FString::Printf(
			TEXT("resolution floor: a %.4f cm bias (below half a %.4f cm step) reads %+.4g cm -- this ")
			TEXT("statistic cannot resolve it, and only a higher phase count can"),
			SubResolutionBias, SweepStepCm, Blind.Key));

		TestTrue(FString::Printf(
			TEXT("a bias below half a sweep step vanishes ENTIRELY rather than reading small (%+.4g cm, ")
			TEXT("noise bound 1e-9) -- so the tolerance above is the sweep's resolution, and tightening ")
			TEXT("it without raising Phases would buy nothing"),
			Blind.Key), FMath::Abs(Blind.Key) < 1.0e-9);
	}

	// Pin the cell-centre convention outright, so a future change to AxisIndexFromCoord cannot quietly
	// move it (there is no WorldToCell; the two entry points are ContainingCell for a stationary segment
	// and the DDA for a moving one, and both index through AxisIndexFromCoord).
	// A stationary agent at an exact cell centre must land in that cell; at an exact boundary the
	// lower-index cell owns it (the ratified rule).
	{
		FTrajectoryField Field = MakeField(1000.0, 1000.0, CmPerTexel, WidthCm);
		Field.DepositSegment(FVector2D(455.0, 455.0), FVector2D(455.0, 455.0), 1.0f); // centre of cell (45,45)
		const TArray<float>& Values = Field.GetCanonical(ETrajectoryMapMode::RouteExposure);
		const FIntPoint Dims = Field.GetGridDims();
		TestTrue(TEXT("a point at the exact centre of cell (45,45) deposits into cell (45,45)"),
			Values.IsValidIndex(45 * Dims.X + 45) && Values[45 * Dims.X + 45] > 0.0f);
	}
	{
		FTrajectoryField Field = MakeField(1000.0, 1000.0, CmPerTexel, WidthCm);
		Field.DepositSegment(FVector2D(450.0, 450.0), FVector2D(450.0, 450.0), 1.0f); // exact boundary
		const TArray<float>& Values = Field.GetCanonical(ETrajectoryMapMode::RouteExposure);
		const FIntPoint Dims = Field.GetGridDims();
		TestTrue(TEXT("a point on the exact boundary is owned by the LOWER-index cell (44,44)"),
			Values.IsValidIndex(44 * Dims.X + 44) && Values[44 * Dims.X + 44] > 0.0f);
	}

	// The grid line the sweep no longer touches. Midpoint sampling deliberately steps around the
	// discontinuity, so nothing in the sweep exercises the lower-index rule any more -- and the two probes
	// above do not close that on their own: they are STATIONARY segments read out of RouteExposure, which
	// reach the grid through ContainingCell. A moving segment takes a different route (the DDA's
	// AxisIndexFromCoord with Dir == 0 on the lateral axis, TrajectoryField.cpp:119). Both call the same
	// index function today; pin the moving case explicitly rather than assume they always will.
	{
		FTrajectoryField Field = MakeField(1000.0, 1000.0, CmPerTexel, WidthCm);
		Field.DepositSegment(FVector2D(100.0, 500.0), FVector2D(900.0, 500.0), 1.0f); // dead on grid line y = 50
		const TArray<float>& Values = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
		const FIntPoint Dims = Field.GetGridDims();
		if (TestTrue(TEXT("the 1000 cm / 10 cm-per-texel fixture gives the 100x100 grid the row indices below assume"),
				Dims.X == 100 && Dims.Y == 100))
		{
			double Row49 = 0.0;
			double Row50 = 0.0;
			for (int32 I = 0; I < Dims.X; ++I)
			{
				Row49 += static_cast<double>(Values[49 * Dims.X + I]);
				Row50 += static_cast<double>(Values[50 * Dims.X + I]);
			}
			TestTrue(FString::Printf(
				TEXT("a MOVING segment running exactly along the y = 500 cm grid line books entirely into ")
				TEXT("row 49, the lower-index cell (row 49 = %.6f, row 50 = %.6f)"), Row49, Row50),
				Row49 > 0.0 && Row50 == 0.0);
		}
	}

	return true;
}

// =====================================================================================================
// THE SAMPLER-SOURCE GATE. Asserts an ASSET setting, on purpose, because that is the only place this can be
// fixed and the only place a collaborator can silently un-fix it.
//
// Both heatmap textures are created with an explicit Filter (TF_Bilinear since 2026-08-05) and TA_Clamp
// addressing, but a material only consults a texture's own sampler state when its Texture Sample node has
// Sampler Source set to "From Texture Asset". With either *_WorldGroupSettings option the node samples
// through a shared, pooled sampler and BOTH of those settings are DISCARDED - filtering comes from
// texture-group settings instead.
//
// That makes this gate the precondition for the filter meaning anything at all. A texture set TF_Bilinear
// and sampled through a shared sampler is filtered by the texture group, not by us: the C++ setting becomes
// a dead parameter, and someone reading only the code would report a smoothing fix that never shipped. It
// cuts the other way too - a FromTextureAsset material sampling a TF_Nearest texture renders blocky. The
// runtime half is gated by Mobius.InGame.TrajectoryHeatmap.Texture.FilterPerSurface; neither test can see
// the other's half. (That gate asserts a DIFFERENT filter per surface since 2026-08-10 - density bilinear,
// trajectory nearest - but this precondition applies to both equally: a shared world-group sampler
// discards whichever filter we set.)
//
// It cannot be fixed from C++ (sampler source is a property of the node, not of the texture) and it cannot
// be fixed by a script anybody has to remember to run - the asset is committed, so the asset is the fix.
// This test is what stops it regressing: regenerate a material, or open it and change the dropdown back,
// and the suite says so.
// =====================================================================================================

// =====================================================================================================
// The heatmap vertex grid: 25 cm per cell, TRUNCATED, and independent of the 3D toggle.
//
// A0-64/A0-65 made the mesh always build at the 3D-capable density because the 3D toggle is a realtime
// switch that must not regenerate geometry. That trade -- roughly 100x the quads of the old /250 path --
// is only acceptable while the toggle is genuinely free, i.e. while vertex count does not depend on it.
// Nothing asserted that. This gate is the arithmetic half; the behavioural half is
// Mobius.InGame.TrajectoryHeatmap.Mesh.VertexCountIndependentOf3DFlag, which drives the real generation
// path with the flag flipped. Neither sees the other's half: this one cannot catch a branch reintroduced
// at the call site, and that one cannot pin the rounding.
//
// Rounding is the reason this test includes extents that are NOT multiples of 25. The shipping path
// truncates; the DEAD CalculateNumberOfTriangles uses CeilToInt32. Those agree on every multiple of 25,
// so a gate built only from 5000 and 20000 -- the two extents already in the record -- would pass
// against either and pin nothing. 4999 cm separates them: trunc gives 199, ceil would give 200.
// =====================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHeatmapVertexGridFormulaTest,
	"ProjectMobius.Heatmap.Mesh.VertexGridIs25cmTruncated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHeatmapVertexGridFormulaTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		double ExtentCm;
		int32 ExpectedVertsPerAxis;
		const TCHAR* Why;
	};

	// Expected values are 25 cm per cell, truncated -- derived from the rule, not copied from an
	// observation, so a wrong observation cannot ratify itself.
	const FCase Cases[] = {
		{  5000.0, 200, TEXT("50 m fixture from the Tier B invariance tests: 200 verts/axis, 39601 quads") },
		{ 20000.0, 800, TEXT("200 m real-dataset carrier: 800 verts/axis, 638401 quads, ~1.28M tris") },
		{  4999.0, 199, TEXT("NOT a multiple of 25 -- trunc gives 199, CeilToInt32 would give 200") },
		{  5013.0, 200, TEXT("NOT a multiple of 25 -- 200.52 truncates DOWN, it does not round to 201") },
		{    50.0,   2, TEXT("smallest plausible floor: /25 still yields 2, so the span never collapses") },
		{  7300.0, 292, TEXT("the 73 m floor quoted throughout STATUS") },
	};

	for (const FCase& C : Cases)
	{
		const FIntPoint Grid =
			AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(C.ExtentCm, C.ExtentCm));

		TestEqual(*FString::Printf(TEXT("%.0f cm -> %d verts/axis (%s)"), C.ExtentCm, C.ExpectedVertsPerAxis, C.Why),
			Grid.X, C.ExpectedVertsPerAxis);
		TestEqual(*FString::Printf(TEXT("%.0f cm -> both axes agree on a square floor"), C.ExtentCm),
			Grid.Y, Grid.X);

		// >= 2 per axis is the degeneracy floor: at 1 vertex there is no span and at 0 there is no mesh.
		// This is why /25 replaced /250 -- /250 fell below 2 for anything under 500 cm.
		TestTrue(*FString::Printf(TEXT("%.0f cm yields at least 2 verts/axis (mesh span is non-degenerate)"),
			C.ExtentCm), Grid.X >= 2 && Grid.Y >= 2);
	}

	// Non-square extents must not be coupled: X must come from X only. A helper that used the larger or
	// the average would still pass every square case above.
	const FIntPoint Oblong =
		AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(10000.0, 2500.0));
	TestEqual(TEXT("oblong 100 m x 25 m -> X is 400"), Oblong.X, 400);
	TestEqual(TEXT("oblong 100 m x 25 m -> Y is 100, i.e. the axes are independent"), Oblong.Y, 100);

	// The quad count the perf discussion is actually about, stated once so the numbers quoted in STATUS
	// have a machine-checked source: quads = (verts-1) per axis.
	const FIntPoint Grid50m =
		AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(5000.0, 5000.0));
	TestEqual(TEXT("50 m floor is 39601 quads"), (Grid50m.X - 1) * (Grid50m.Y - 1), 39601);
	const FIntPoint Grid200m =
		AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(FVector2D(20000.0, 20000.0));
	TestEqual(TEXT("200 m floor is 638401 quads"), (Grid200m.X - 1) * (Grid200m.Y - 1), 638401);

	return true;
}

// =====================================================================================================
#if WITH_EDITOR
namespace TrajectorySamplerGate
{
	/** Shared body: every Texture Sample node in one material must use the texture's own sampler state. */
	static void CheckMaterialUsesOwnSampler(FAutomationTestBase& Test, const TCHAR* MaterialPath,
	                                        const TCHAR* FriendlyName)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialPath);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s loads"), FriendlyName), Material))
		{
			return;
		}

		int32 SampleCount = 0;
		int32 SharedSamplerCount = 0;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			UMaterialExpressionTextureSample* TextureExpression = Cast<UMaterialExpressionTextureSample>(Expression);
			if (!TextureExpression)
			{
				continue;
			}
			++SampleCount;
			if (TextureExpression->SamplerSource != SSM_FromTextureAsset)
			{
				++SharedSamplerCount;
				Test.AddError(FString::Printf(
					TEXT("%s in %s samples through a SHARED sampler (SamplerSource = %d), which discards the ")
					TEXT("texture's Filter and AddressX/Y. The texture-group settings then decide filtering, ")
					TEXT("so the TF_Bilinear passed at InitializeTexture is a DEAD PARAMETER for this ")
					TEXT("surface and any smoothing fix in C++ will not ship. FIX: open %s, select the ")
					TEXT("Texture Sample node, set Sampler Source to 'From Texture Asset', save, and COMMIT ")
					TEXT("the .uasset."),
					*Expression->GetName(), FriendlyName,
					static_cast<int32>(TextureExpression->SamplerSource), MaterialPath));
			}
		}

		Test.TestTrue(*FString::Printf(TEXT("%s samples at least one texture (graph has not been gutted)"),
			FriendlyName), SampleCount > 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: every texture sample honours the texture's own sampler"),
			FriendlyName), SharedSamplerCount, 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajMaterialHonoursTextureSamplerTest,
	"ProjectMobius.Heatmap.Trajectory.Material.SamplesWithTextureOwnSampler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajMaterialHonoursTextureSamplerTest::RunTest(const FString& Parameters)
{
	TrajectorySamplerGate::CheckMaterialUsesOwnSampler(*this,
		TEXT("/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_Trajectory.M_HeatmapRT_Trajectory"),
		TEXT("M_HeatmapRT_Trajectory"));
	return true;
}

// The DENSITY surfaces, added 2026-08-05 with the bilinear change. Not a speculative extension: the owner's
// pixelated density heatmap was traced to point sampling (A0-72), and the C++ fix only takes effect if
// THESE materials honour the texture's sampler - which nothing had ever checked. M_VoronoiMap is included
// because the actor binds the same texture to it (SetupDynamicTexture) and it is selected by HeatmapType,
// so a divergence there shows up only in Voronoi mode, which nobody tests by eye.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDensityMaterialHonoursTextureSamplerTest,
	"ProjectMobius.Heatmap.Density.Material.SamplesWithTextureOwnSampler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDensityMaterialHonoursTextureSamplerTest::RunTest(const FString& Parameters)
{
	TrajectorySamplerGate::CheckMaterialUsesOwnSampler(*this,
		TEXT("/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_V2.M_HeatmapRT_V2"),
		TEXT("M_HeatmapRT_V2"));
	TrajectorySamplerGate::CheckMaterialUsesOwnSampler(*this,
		TEXT("/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_VoronoiMap.M_VoronoiMap"),
		TEXT("M_VoronoiMap"));
	return true;
}

// =====================================================================================================
// THE UNLIT GATE. Same reasoning as the sampler gate above: an ASSET setting, asserted from C++, because
// the asset is the only place it can live and the only place it can be silently undone.
//
// Owner ruling A0-48 (2026-08-05): the trajectory surface is UNLIT. Under default-lit the band colour was
// shaded, exposed and tonemapped before reaching the screen -- measured, the CPU-colourised PNG held 4
// distinct colours while the rendered view held 5,124 with ZERO pixels matching a band colour exactly and
// 542 shades inside +/-8 of one blue-grey. Unlit removes the lighting and exposure stages, which is both
// the fidelity fix and a small perf win on a full-screen-ish surface.
//
// An unlit material's ONLY colour output is Emissive Color -- Base Color is ignored entirely -- so the
// two assertions below are one fact: the shading model must be Unlit AND the band custom node must reach
// Emissive. Setting the model without moving the connection renders the surface black.
//
// Base Color is intentionally left connected as well, so reverting to default-lit is a one-property flip
// rather than a rewire. That is NOT asserted here: it is a convenience, not a requirement.
//
// Regenerating this material from M_HeatmapRT_V2 (which is default-lit, shared with the density surface,
// and must stay that way) reverts both. This test is what says so.
// =====================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajMaterialIsUnlitTest,
	"ProjectMobius.Heatmap.Trajectory.Material.IsUnlitAndDrivesEmissive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajMaterialIsUnlitTest::RunTest(const FString& Parameters)
{
	const TCHAR* MaterialPath =
		TEXT("/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_Trajectory.M_HeatmapRT_Trajectory");
	UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialPath);
	if (!TestNotNull(TEXT("M_HeatmapRT_Trajectory loads"), Material))
	{
		return false;
	}

	// Read the DESERIALISED property. A binary grep of the .uasset cannot answer this: enum defaults are
	// not serialised, so a matched string may be a name-table entry rather than an assigned value. That
	// mistake produced a wrong diagnosis twice on this surface.
	// UMaterial::ShadingModel is private in 5.5 -- go through the accessor, which also resolves the
	// multi-model field rather than a single raw enum.
	const FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
	const bool bIsUnlit = ShadingModels.HasShadingModel(MSM_Unlit) && ShadingModels.CountShadingModels() == 1;
	AddInfo(FString::Printf(TEXT("shading models: count=%d, has MSM_Unlit=%s"),
		ShadingModels.CountShadingModels(),
		ShadingModels.HasShadingModel(MSM_Unlit) ? TEXT("true") : TEXT("false")));
	if (!bIsUnlit)
	{
		AddError(FString::Printf(
			TEXT("%s is not Unlit. Lit shading re-introduces the viewport/export mismatch: the bands get ")
			TEXT("shaded and tonemapped and no palette colour survives to screen. FIX: open the material, ")
			TEXT("set Shading Model to Unlit, confirm the band custom node drives Emissive Color, save, ")
			TEXT("and COMMIT the .uasset."),
			MaterialPath));
	}

	const FExpressionInput* Emissive = Material->GetExpressionInputForProperty(MP_EmissiveColor);
	const bool bEmissiveDriven = Emissive != nullptr && Emissive->Expression != nullptr;
	if (!bEmissiveDriven)
	{
		AddError(FString::Printf(
			TEXT("%s has nothing connected to Emissive Color. An Unlit material ignores Base Color, so ")
			TEXT("the surface renders BLACK. FIX: connect the band custom node to Emissive Color."),
			MaterialPath));
	}
	else
	{
		AddInfo(FString::Printf(TEXT("Emissive Color <- %s output index %d (mask=%d rgba=%d%d%d%d)"),
			*Emissive->Expression->GetName(), Emissive->OutputIndex,
			Emissive->Mask, Emissive->MaskR, Emissive->MaskG, Emissive->MaskB, Emissive->MaskA));
	}

	// A non-null expression is NOT enough. Emissive must be fed the SAME output of the SAME node that
	// Base Color was, or an Unlit surface renders BLACK while every assertion above still passes -- the
	// owner reported exactly that on 2026-08-05. `connect_material_property` was called with an empty
	// output name, which does not guarantee the output index Base Color used.
	const FExpressionInput* BaseColour = Material->GetExpressionInputForProperty(MP_BaseColor);
	if (BaseColour != nullptr && BaseColour->Expression != nullptr)
	{
		AddInfo(FString::Printf(TEXT("Base Color    <- %s output index %d (mask=%d rgba=%d%d%d%d)"),
			*BaseColour->Expression->GetName(), BaseColour->OutputIndex,
			BaseColour->Mask, BaseColour->MaskR, BaseColour->MaskG, BaseColour->MaskB, BaseColour->MaskA));
		if (bEmissiveDriven && BaseColour->Expression == Emissive->Expression
			&& BaseColour->OutputIndex != Emissive->OutputIndex)
		{
			AddError(FString::Printf(
				TEXT("%s drives Base Color from output %d but Emissive from output %d of the same node. ")
				TEXT("Under Unlit only Emissive is read, so the surface renders BLACK. FIX: connect ")
				TEXT("Emissive to output %d."),
				MaterialPath, BaseColour->OutputIndex, Emissive->OutputIndex, BaseColour->OutputIndex));
		}
	}

	return true;
}
// -----------------------------------------------------------------------------------------------------
// T-BAND-6 -- the ASSET's stored band defaults match the live crossing contract.
//
// Sits inside the WITH_EDITOR block deliberately: it needs Materials/Material.h, which this file includes
// only there, and it is an EditorContext case so it never runs in a configuration that lacks it. The
// parameter-info types arrive with it (MaterialInterface.h pulls MaterialTypes.h).
//
// THIS IS THE GAP THAT LET TWO GENERATIONS OF DRIFT SHIP. Every other band gate in this suite reads the
// MATERIAL INSTANCE -- which AHeatmapPixelTextureVisualizer overwrites at runtime from
// FHeatmapLOSBands::TrajectoryCrossings -- so all of them stayed green while M_HeatmapRT_Trajectory sat
// on the ORIGINAL seed-and-brush byte cuts (0.096078 / 0.182353 / 0.280392 / 0.433333 / 0.688235, i.e.
// bytes 24.5 / 46.5 / 71.5 / 110.5 / 175.5). It never even received the 2026-08-04 quantile set that
// replaced those, because BuildTrajectoryHeatmapMaterial.py's step_add_parameters skipped parameters
// that already existed, making a re-run a no-op. Nothing looked at the asset, so nothing noticed.
//
// The stored defaults are not what renders in-game, and that is exactly why this needs its own gate: a
// wrong default is INVISIBLE in play and shows up only in a thumbnail, a material preview, or the day
// somebody reads the asset to find out what a colour means. Discrepancies you cannot see are the ones
// that need a test.
//
// Reads GetScalarParameterDefaultValue (ENGINE_API, so no editor module dependency) rather than the
// material editor library, which lives in an editor-only module this test module does not depend on.
//
// TOLERANCE. Half a byte. Tighter would fail on decimal round-trip through the asset serialiser; looser
// would let a real band-width change through, since the tightest real separation here is ~26 bytes.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand6AssetDefaultsTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_6_AssetDefaultsMatchCrossingContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand6AssetDefaultsTest::RunTest(const FString& Parameters)
{
	const TCHAR* const MaterialPath =
		TEXT("/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_Trajectory.M_HeatmapRT_Trajectory");
	UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialPath);
	if (!TestNotNull(TEXT("M_HeatmapRT_Trajectory loads"), Material))
	{
		return false;
	}

	// The asset default has to encode the SHIPPING configuration's contract. Both inputs are read from the
	// sources the runtime itself uses — the actor's class default for the stroke width, and the field
	// config's own default for the reference — so a change to either propagates here automatically.
	// Transcribing them is what let the asset drift two generations without anything noticing.
	//
	// STROKE WIDTH, not cell size, since the 2026-08-10 decoupling: RefreshTrajectoryCrossingBands derives
	// the edges from TrajectoryDisplayPathWidthCm, so reading the cell here would gate the asset against a
	// quantity the runtime no longer uses — and would redden the moment the cell was dialled, which is
	// exactly the freedom the decoupling was for. This argument replaces the old D2b caveat: the width is
	// never raised by the grid clamp, so shipping default and runtime push cannot legitimately differ.
	//
	// D-E: the 0/1 edge is the derived ROUTE THRESHOLD, so the expectation has to derive it too — from the
	// shipping width and the shipping cell, exactly as RefreshTrajectoryCrossingBands does. Passing 0 here
	// would gate the asset against the old is-it-nonzero edge and redden the moment the threshold moved.
	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float ShippingWidthMetres = Defaults->TrajectoryDisplayPathWidthCm / 100.0f;
	const float ShippingReference = FTrajectoryFieldConfig().ReferenceUsageDensity;
	const float ShippingRouteThreshold = FTrajectoryField::DeriveRouteThresholdCrossings(
		Defaults->TrajectoryDisplayPathWidthCm, Defaults->TrajectoryWorldCmPerTexel);
	const FHeatmapLOSBands Expected = FHeatmapLOSBands::TrajectoryCrossings(
		ShippingWidthMetres, ShippingReference, ShippingRouteThreshold);

	const TCHAR* const Names[] = { TEXT("LOS_A_Band"), TEXT("LOS_B_Band"), TEXT("LOS_C_Band"),
	                               TEXT("LOS_D_Band"), TEXT("LOS_E_Band") };
	const float ExpectedEdges[] = { Expected.BandA, Expected.BandB, Expected.BandC,
	                                Expected.BandD, Expected.BandE };
	constexpr float HalfAByte = 0.5f / 255.0f;

	for (int32 Index = 0; Index < 5; ++Index)
	{
		float Stored = 0.0f;
		const FHashedMaterialParameterInfo Info{ FMaterialParameterInfo(Names[Index]) };
		if (!TestTrue(*FString::Printf(TEXT("T-BAND-6: %s exists on the asset"), Names[Index]),
			Material->GetScalarParameterDefaultValue(Info, Stored)))
		{
			continue;
		}

		if (FMath::Abs(Stored - ExpectedEdges[Index]) > HalfAByte)
		{
			AddError(FString::Printf(
				TEXT("T-BAND-6: %s default is %.6f (byte %.1f) but the crossing contract wants %.6f ")
				TEXT("(byte %.1f). The ASSET is stale — the runtime push hides this, so play looks fine ")
				TEXT("while thumbnails, previews and anyone reading the asset get a different meaning for ")
				TEXT("the same colour. FIX: set the default in the material editor, or run ")
				TEXT("MobiusPerf/BuildTrajectoryHeatmapMaterial.py. If the CONTRACT changed rather than ")
				TEXT("the asset, update FHeatmapLOSBands::TrajectoryCrossings and the asset together."),
				Names[Index], Stored, Stored * 255.0f,
				ExpectedEdges[Index], ExpectedEdges[Index] * 255.0f));
		}
	}

	// Non-vacuity: prove the comparison can actually reject. If this ever passes, the loop above is
	// comparing something to itself and every assertion in it is decorative.
	TestTrue(TEXT("T-BAND-6: the tolerance rejects the known-stale LOS_A of byte 24.5"),
		FMath::Abs((24.5f / 255.0f) - Expected.BandA) > HalfAByte);

	return true;
}

#endif // WITH_EDITOR

// =====================================================================================================
// CROSSING-COUNT BANDS (T-BAND-*)  -- added 2026-08-10
//
// These cover FHeatmapLOSBands::TrajectoryCrossings, which replaced the frozen quantile edges as the
// Route Usage band contract. The whole point of that change is that a colour on the trajectory surface
// means a COUNTABLE number of crossings, so the assertions below are about countability:
//
//   T-BAND-1  one axial crossing of a cell deposits exactly one cell-side of person-metres
//   T-BAND-2  a cell holding EXACTLY N crossings renders band N's colour, not N+-1  (the equality tie)
//   T-BAND-3  the edges move with the cell size, i.e. they are computed and not a literal table
//   T-BAND-4  a degenerate cell size still yields a strictly monotonic, in-range chain
//   T-BAND-5  the shipping width/cell pair collapses the splat to one tap, which is what makes the
//             counts literal rather than spread across a kernel
// =====================================================================================================

namespace TrajectoryBandOracle
{
	// The shipping configuration, named once. s = 0.1 m and Reference = 100 person/m give a denominator of
	// exactly 10, so an edge at (N + 0.5) crossings sits at (N + 0.5)/10.
	constexpr float ShippingCellSideMetres = 0.1f;
	constexpr float ShippingReferenceUsage = 100.0f;

	// Tight, because these are single float divisions of exactly-representable operands; 1e-6 is slack.
	constexpr float EdgeTol = 1.0e-6f;

	/**
	 * The six band colours, mirrored from the LOS_*_COLOR macros in DynamicPixelRenderingTexture.cpp.
	 *
	 * Duplicated deliberately: the macros are private to that .cpp, and pinning them here means T-BAND-2
	 * also guards the RAMP the owner specified (0 = blue, 1 = cyan, 2 = green, 3 = yellow, 4 = orange,
	 * 5+ = red). If someone re-orders or re-tints the ramp, this reddens.
	 */
	static const FLinearColor BandColours[6] = {
		FLinearColor(0.0f, 0.0f, 1.0f, 1.0f),  // A blue   - no data
		FLinearColor(0.0f, 1.0f, 1.0f, 1.0f),  // B cyan   - ~1 crossing
		FLinearColor(0.0f, 1.0f, 0.0f, 1.0f),  // C green  - ~2
		FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),  // D yellow - ~3
		FLinearColor(1.0f, 0.25f, 0.0f, 1.0f), // E orange - ~4
		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)   // F red    - 5+
	};

	static const TCHAR* BandNames[6] = { TEXT("A/blue"), TEXT("B/cyan"), TEXT("C/green"),
	                                     TEXT("D/yellow"), TEXT("E/orange"), TEXT("F/red") };

	/** Normalised red channel for a cell holding exactly N crossings: N / (s * Reference). */
	static float RedForCrossings(double Crossings, float CellSideMetres, float Reference)
	{
		return static_cast<float>(Crossings / (static_cast<double>(CellSideMetres) * Reference));
	}

	static bool EdgesStrictlyIncrease(const FHeatmapLOSBands& B)
	{
		return B.BandA < B.BandB && B.BandB < B.BandC && B.BandC < B.BandD && B.BandD < B.BandE;
	}

	static bool EdgesInRange(const FHeatmapLOSBands& B)
	{
		return B.BandA >= 0.0f && B.BandE <= 1.0f;
	}
}

// -----------------------------------------------------------------------------------------------------
// T-BAND-1 -- one axial crossing deposits exactly one cell side, and the derived edges are the half-steps.
//
// The segment runs from x = 10 cm to x = 20 cm at y = 5 cm, which is exactly the span of column 1 on a
// 10 cm grid. So column 1 receives the whole 0.1 m and no neighbour receives anything: a clean single
// crossing, which is the unit the entire band ladder is denominated in.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand1CrossingUnitTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_1_OneCrossingIsOneCellSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand1CrossingUnitTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;
	using namespace TrajectoryBandOracle;

	// 10 cm cells, and a 10 cm display width so the splat is the identity kernel (see T-BAND-5) and the
	// presentation copy is the canonical one. Both matter: the encode reads PRESENTATION.
	FTrajectoryField Field = MakeField(100.0, 100.0, 10.0f, 10.0f);

	Field.DepositSegment(FVector2D(10.0, 5.0), FVector2D(20.0, 5.0), 0.1f);

	const float CellSideMetres = Field.GetEffectiveCmPerTexel() / 100.0f;
	TestEqual(TEXT("cell side is the shipping 0.1 m"), CellSideMetres, ShippingCellSideMetres, EdgeTol);

	FIntPoint Cell;
	TestTrue(TEXT("the crossed cell resolves"), Field.WorldToCell(FVector2D(15.0, 5.0), Cell));

	// Pinned by value, not just "it resolved". The neighbour indices below are derived from this, so a
	// surprise here would read out of bounds rather than fail an assertion; and stating the expected cell
	// is what makes the segment endpoints (10 cm -> 20 cm) legible as "exactly column 1's span".
	TestTrue(TEXT("T-BAND-1: the crossed cell is column 1, row 0"), Cell == FIntPoint(1, 0));
	TestTrue(TEXT("T-BAND-1: the grid is big enough for the neighbour checks below"),
		Field.GetGridDims().X >= 3 && Field.GetGridDims().Y >= 2);

	const int32 Index = Cell.Y * Field.GetGridDims().X + Cell.X;
	const double CellMetres = (double)Field.GetCanonical(ETrajectoryMapMode::RouteUsage)[Index];

	// The load-bearing identity: person-metres in one cell divided by the cell side IS the crossing count.
	TestTrue(TEXT("T-BAND-1: one axial crossing deposits exactly one cell side of person-metres"),
		NearlyEqualHybrid(CellMetres, 0.1, RelTol1e6, Coeff(AbsCoeff2e7, 0.1)));

	const double Crossings = CellMetres / CellSideMetres;
	TestTrue(FString::Printf(TEXT("T-BAND-1: that reads as exactly 1.00 crossings (got %.9f)"), Crossings),
		NearlyEqualAbs(Crossings, 1.0, 1.0e-6));

	// Nothing leaked sideways. Asserted against the NEIGHBOURS by name rather than against the field
	// total: a sum check is conserved by construction and would pass even if a splat regression had
	// spread the crossing across a kernel, which is precisely the failure that would stop every band edge
	// below from being literal. Both neighbours must be EXACTLY zero -- untouched float32, never written.
	const TArray<float>& Metres = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
	TestEqual(TEXT("T-BAND-1: the cell BEFORE the crossed one received nothing at all"),
		(double)Metres[Cell.Y * Field.GetGridDims().X + (Cell.X - 1)], 0.0, 0.0);
	TestEqual(TEXT("T-BAND-1: the cell AFTER the crossed one received nothing at all"),
		(double)Metres[Cell.Y * Field.GetGridDims().X + (Cell.X + 1)], 0.0, 0.0);
	TestEqual(TEXT("T-BAND-1: the row ABOVE received nothing at all"),
		(double)Metres[(Cell.Y + 1) * Field.GetGridDims().X + Cell.X], 0.0, 0.0);

	TestTrue(TEXT("T-BAND-1: and the field total is that one cell"),
		NearlyEqualHybrid(SumCanonical(Field, ETrajectoryMapMode::RouteUsage), CellMetres, RelTol1e6,
			Coeff(AbsCoeff2e7, 0.1)));

	// Identity kernel, so the presentation the encode reads holds the same value the DDA wrote. Without
	// this the count would be spread over 9 taps and no band edge below would be literal.
	TestTrue(TEXT("T-BAND-1: presentation == canonical for the crossed cell (kernel is one tap)"),
		NearlyEqualAbs((double)Field.GetPresentation(ETrajectoryMapMode::RouteUsage)[Index], CellMetres, 1.0e-9));

	// And the edges themselves: (N + 0.5) / (s * Reference) = (N + 0.5) / 10.
	const FHeatmapLOSBands Bands =
		FHeatmapLOSBands::TrajectoryCrossings(ShippingCellSideMetres, ShippingReferenceUsage);

	TestEqual(TEXT("T-BAND-1: BandA is half a byte (no-data only)"), Bands.BandA, 0.5f / 255.0f, EdgeTol);
	TestEqual(TEXT("T-BAND-1: BandB edge == 1.5 crossings"), Bands.BandB, 0.150f, EdgeTol);
	TestEqual(TEXT("T-BAND-1: BandC edge == 2.5 crossings"), Bands.BandC, 0.250f, EdgeTol);
	TestEqual(TEXT("T-BAND-1: BandD edge == 3.5 crossings"), Bands.BandD, 0.350f, EdgeTol);
	TestEqual(TEXT("T-BAND-1: BandE edge == 4.5 crossings"), Bands.BandE, 0.450f, EdgeTol);

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-BAND-2 -- THE EQUALITY-TIE GUARD. This is the one that justifies the half-step edges.
//
// A cell holding EXACTLY N crossings must render band N's colour. Edges sit at N + 0.5 precisely so the
// integer case is never on a boundary; put them on the integers instead and `RVal < Band` turns into an
// exact-equality tie, dropping every whole-numbered cell one band low.
//
// NON-VACUITY: move the edges from (N + 0.5)/10 to N/10 and this reddens immediately -- 1 crossing would
// render green instead of cyan, 2 yellow instead of green, and so on down the ramp.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand2IntegerTieTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_2_ExactIntegerLandsInItsOwnBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand2IntegerTieTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryBandOracle;

	const FHeatmapLOSBands Bands =
		FHeatmapLOSBands::TrajectoryCrossings(ShippingCellSideMetres, ShippingReferenceUsage);

	// Index i == the band a cell holding exactly i crossings must take. 5 and anything above share LOS_F.
	for (int32 Crossings = 0; Crossings <= 5; ++Crossings)
	{
		const float RVal = RedForCrossings(Crossings, ShippingCellSideMetres, ShippingReferenceUsage);
		const FLinearColor Got = UDynamicPixelRenderingTexture::BandColourForRedValue(RVal, Bands);

		TestTrue(FString::Printf(
			TEXT("T-BAND-2: exactly %d crossing(s) -> RVal %.4f -> band %s, got (%.2f, %.2f, %.2f)"),
			Crossings, RVal, BandNames[Crossings], Got.R, Got.G, Got.B),
			Got.Equals(BandColours[Crossings], 1.0e-4f));
	}

	// Band F is open-ended by design: on a crossing count, "5 or more" is a true statement about 50 as
	// well as 5, so saturation is the CORRECT reading rather than a defect to normalise away.
	//
	// Swept only to 10, which is where RVal reaches 1.0 at this reference. Beyond that EncodeToDisplay
	// clamps the stored byte to 255 and every higher count arrives here as the same input, so asserting
	// on 12 or 50 would re-run the RVal == 1.0 case under a different label rather than testing anything.
	// That the encode clamps at all is T-SAT-1's claim, not this test's.
	for (int32 Crossings = 6; Crossings <= 10; ++Crossings)
	{
		const float RVal = RedForCrossings(Crossings, ShippingCellSideMetres, ShippingReferenceUsage);
		TestTrue(FString::Printf(TEXT("T-BAND-2: %d crossings -> RVal %.2f stays in band F/red"), Crossings, RVal),
			UDynamicPixelRenderingTexture::BandColourForRedValue(RVal, Bands).Equals(BandColours[5], 1.0e-4f));
	}

	// Untouched floor. EncodeToDisplay floors any POSITIVE cell to byte 1, so byte 0 is the only value
	// below BandA -- that is what reserves blue for "nobody walked here" rather than "hardly anyone did".
	TestTrue(TEXT("T-BAND-2: byte 0 is band A/blue"),
		UDynamicPixelRenderingTexture::BandColourForRedValue(0.0f, Bands).Equals(BandColours[0], 1.0e-4f));
	TestTrue(TEXT("T-BAND-2: byte 1 is already ABOVE band A, i.e. real data"),
		!UDynamicPixelRenderingTexture::BandColourForRedValue(1.0f / 255.0f, Bands)
			.Equals(BandColours[0], 1.0e-4f));

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-BAND-3 -- the edges track the cell size, which is the entire reason they are computed.
//
// NON-VACUITY: replace TrajectoryCrossings' body with a frozen table and this reddens, because every
// assertion here compares edges derived at DIFFERENT grid resolutions. It is the guard against the
// pre-2026-08-03 defect class where band meaning silently followed building size -- FTrajectoryField's
// D2b clamp can RAISE cm/texel on a large floor without any caller asking it to.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand3ScalingTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_3_EdgesTrackCellSizeAndReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand3ScalingTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryBandOracle;

	const FHeatmapLOSBands Shipping =
		FHeatmapLOSBands::TrajectoryCrossings(ShippingCellSideMetres, ShippingReferenceUsage);
	const FHeatmapLOSBands Coarse =        // 20 cm cells -- what the D2b clamp produces on a big floor
		FHeatmapLOSBands::TrajectoryCrossings(0.2f, ShippingReferenceUsage);
	const FHeatmapLOSBands Fine =          // 5 cm cells
		FHeatmapLOSBands::TrajectoryCrossings(0.05f, ShippingReferenceUsage);
	const FHeatmapLOSBands DoubleRef =     // same grid, twice the reference density
		FHeatmapLOSBands::TrajectoryCrossings(ShippingCellSideMetres, 2.0f * ShippingReferenceUsage);

	// Edge = (N + 0.5)/(s * Reference), so doubling s halves the edge and halving s doubles it.
	TestEqual(TEXT("T-BAND-3: 20 cm cells halve the BandB edge"), Coarse.BandB, 0.075f, EdgeTol);
	TestEqual(TEXT("T-BAND-3: 5 cm cells double the BandB edge"), Fine.BandB, 0.300f, EdgeTol);
	TestEqual(TEXT("T-BAND-3: doubling the reference halves the BandB edge"), DoubleRef.BandB, 0.075f, EdgeTol);

	// The product is what matters, not either factor: 20 cm at reference 100 must equal 10 cm at 200.
	TestEqual(TEXT("T-BAND-3: edges depend only on the product (s x Reference)"),
		Coarse.BandE, DoubleRef.BandE, EdgeTol);

	// Stated bluntly, because a frozen table would pass everything above by accident if it happened to
	// hold the shipping numbers: a different grid MUST give different edges.
	TestTrue(TEXT("T-BAND-3: a different cell size does not reuse the shipping edges"),
		!FMath::IsNearlyEqual(Coarse.BandB, Shipping.BandB, EdgeTol));

	// A crossing still reads as one crossing at every resolution -- that is the invariant the scaling
	// exists to preserve. At cell side s, one crossing deposits s metres and lands at 1/(s x Reference).
	for (const float CellSide : { 0.05f, 0.1f, 0.2f })
	{
		const FHeatmapLOSBands B = FHeatmapLOSBands::TrajectoryCrossings(CellSide, ShippingReferenceUsage);
		const float OneCrossing = RedForCrossings(1.0, CellSide, ShippingReferenceUsage);
		TestTrue(FString::Printf(TEXT("T-BAND-3: at %.0f cm cells, 1 crossing is band B/cyan"), CellSide * 100.0f),
			UDynamicPixelRenderingTexture::BandColourForRedValue(OneCrossing, B).Equals(BandColours[1], 1.0e-4f));
	}

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-BAND-4 -- a NON-POSITIVE grid still produces a WELL-FORMED chain.
//
// An uninitialised field reports GetEffectiveCmPerTexel() == 0, and EnsureTrajectoryFieldSized can reach
// the band call before Initialise has produced a grid. Without the guard the edges evaluate to +inf,
// clamp to 1.0, and all four collapse onto each other -- which does not crash but makes bands C..F
// unreachable, so every drawn texel would render as band B regardless of traffic.
//
// SCOPE, stated precisely: this covers the GUARD path only -- cell side <= 0, reference <= 0, non-finite.
// It does NOT cover a valid-but-tiny cell size. At s = 0.001 m the denominator is 0.1, every edge exceeds
// 1.0 and they legitimately collapse onto the clamp; that is the documented representability limit in
// TrajectoryCrossings ((s x Reference) >= 4.5 for the full ladder), not a defect, and the honest reading
// there is "this grid saturates". Do not "fix" it by nudging the edges apart -- that would invent
// separations the encode cannot represent.
//
// NON-VACUITY: delete the `Denominator <= 0` guard and the strict-monotonicity assertions below redden.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand4DegenerateTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_4_DegenerateCellSizeStaysWellFormed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand4DegenerateTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryBandOracle;

	// Zero cell side (field never initialised) and a negative one (nonsense config).
	for (const float CellSide : { 0.0f, -0.1f })
	{
		const FHeatmapLOSBands B = FHeatmapLOSBands::TrajectoryCrossings(CellSide, ShippingReferenceUsage);
		TestTrue(FString::Printf(TEXT("T-BAND-4: cell side %.2f still gives strictly increasing edges"), CellSide),
			EdgesStrictlyIncrease(B));
		TestTrue(FString::Printf(TEXT("T-BAND-4: cell side %.2f still gives in-range edges"), CellSide),
			EdgesInRange(B));
		TestEqual(FString::Printf(TEXT("T-BAND-4: cell side %.2f keeps BandA as the no-data edge"), CellSide),
			B.BandA, 0.5f / 255.0f, EdgeTol);
	}

	// Zero reference is the same failure through the other factor.
	const FHeatmapLOSBands ZeroRef = FHeatmapLOSBands::TrajectoryCrossings(ShippingCellSideMetres, 0.0f);
	TestTrue(TEXT("T-BAND-4: zero reference density still gives strictly increasing edges"),
		EdgesStrictlyIncrease(ZeroRef));

	// The healthy path must ALSO satisfy both properties, or the two assertions above would be satisfiable
	// by a function that always returned the fallback.
	const FHeatmapLOSBands Good =
		FHeatmapLOSBands::TrajectoryCrossings(ShippingCellSideMetres, ShippingReferenceUsage);
	TestTrue(TEXT("T-BAND-4: the shipping configuration is strictly increasing and in range"),
		EdgesStrictlyIncrease(Good) && EdgesInRange(Good));
	TestTrue(TEXT("T-BAND-4: the shipping configuration is NOT the degenerate fallback"),
		!FMath::IsNearlyEqual(Good.BandB, ZeroRef.BandB, EdgeTol));

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-BAND-5 -- the band edges track the STROKE WIDTH, and the cell is free to move underneath them.
//
// RENAMED 2026-08-10 from T_BAND_5_ShippingWidthIsIdentityKernel, which asserted the OPPOSITE contract:
// that TrajectoryDisplayPathWidthCm had to EQUAL TrajectoryWorldCmPerTexel so the kernel radius sat at
// exactly 0.5 texels and BuildKernel collapsed to the identity, making Presentation == Canonical. That
// lock is gone. RefreshTrajectoryCrossingBands derives the edges from the WIDTH, which compensates the
// mass-conserving splat exactly -- the cellSide cancels out of (N + 0.5)/(cellSide x Ref) x (cellSide /
// width) -- so the cell became pure sampling resolution. The old ID is deliberately NOT kept, unlike
// T-OFFSET-1's: a name asserting "identity kernel" would now assert something the shipping config
// deliberately does not do, which is worse than losing report comparability.
//
// Three properties, in the order a regression would hit them:
//
//   1. The two candidate bases are now DIFFERENT NUMBERS. Anyone "simplifying" RefreshTrajectoryCrossing-
//      Bands back to GetEffectiveCmPerTexel() moves every edge by width/cell, and this catches it. Under
//      the old locked pair that check was impossible -- the two bases were equal by construction, which
//      is precisely why the cell basis survived so long unquestioned.
//   2. The edges are INVARIANT to the cell. Sweep it across an order of magnitude, holding the width.
//   3. The shipping pair sits COMFORTABLY INSIDE a 9-tap kernel, not on a boundary. BuildKernel's half
//      extent is ceil(R - 0.5), so tap count steps at R = 0.5, 1.5, 2.5 ... and D-A snaps the effective
//      cell slightly DOWN to make the major axis divide evenly. A cell of 15 at width 45 puts R on
//      exactly 1.5, so the snap would flip it to 25 taps on most floors and 9 on the few that divide
//      exactly -- a per-building tap count. Swept over deliberately awkward extents to prove 20 cm cannot.
//
// NON-VACUITY: the width sweep at the end is asserted to MOVE the edges, so none of this can pass by
// comparing something to itself.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand5EdgesTrackWidthTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_5_BandEdgesTrackWidthNotCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand5EdgesTrackWidthTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	// Read the SHIPPING pair from the class defaults rather than hard-coding it. Hard-coded values here
	// would keep passing after a dial change while asserting nothing about what actually ships.
	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float ShipCellCm = Defaults->TrajectoryWorldCmPerTexel;
	const float ShipWidthCm = Defaults->TrajectoryDisplayPathWidthCm;
	const float Reference = FTrajectoryFieldConfig().ReferenceUsageDensity;

	// -- 1. The bases are distinguishable, so a revert to the cell basis is detectable at all.
	const FHeatmapLOSBands FromWidth = FHeatmapLOSBands::TrajectoryCrossings(ShipWidthCm / 100.0f, Reference);
	const FHeatmapLOSBands FromCell = FHeatmapLOSBands::TrajectoryCrossings(ShipCellCm / 100.0f, Reference);
	TestTrue(FString::Printf(
		TEXT("T-BAND-5: width %.1f cm and cell %.1f cm are different banding bases, so this test can fail"),
		ShipWidthCm, ShipCellCm),
		!FMath::IsNearlyEqual(FromWidth.BandB, FromCell.BandB, 1.0e-6f));

	// -- 2. Sweeping the cell must not move an edge. The width is the only input that may.
	for (const float CellCm : { 5.0f, 10.0f, ShipCellCm, 45.0f, 90.0f })
	{
		FTrajectoryField Field = MakeField(4000.0, 4000.0, CellCm, ShipWidthCm);
		const FHeatmapLOSBands Edges = FHeatmapLOSBands::TrajectoryCrossings(
			ShipWidthCm / 100.0f, Field.GetConfig().ReferenceUsageDensity);

		TestEqual(*FString::Printf(
			TEXT("T-BAND-5: band B is unchanged at a %.0f cm cell (effective %.4f)"),
			CellCm, Field.GetEffectiveCmPerTexel()), Edges.BandB, FromWidth.BandB, 1.0e-6f);
		TestEqual(*FString::Printf(TEXT("T-BAND-5: band E is unchanged at a %.0f cm cell"), CellCm),
			Edges.BandE, FromWidth.BandE, 1.0e-6f);
	}

	// -- 3. The shipping pair holds a 9-tap kernel on extents chosen to divide badly, so D-A's snap
	// cannot walk it over a boundary. 4548.9 x 3977.4 is the real floor the misalignment was measured on.
	const TArray<FVector2D> AwkwardExtentsCm = {
		FVector2D(4548.9, 3977.4), FVector2D(5000.0, 3000.0),
		FVector2D(2537.0, 2537.0), FVector2D(12345.6, 7890.1)
	};
	// ⚠️ This block used to assert "9 taps" and "R clear of the 0.5 / 1.5 boundaries". Both were written
	// for the brief 20 cm cell and are now WRONG TO ASSERT, not merely stale: the tap-count boundary was
	// the entire reason 20 cm was chosen over 15, and D-D voided that reasoning by moving the deposit onto
	// the phase table, whose footprint is ceil(R + 0.5) at every cell size. Pinning a tap count here would
	// re-freeze a decision that has been reversed on purpose. What is worth asserting is the property that
	// motivated it — the cost must not vary with BUILDING SIZE — so the count is compared across extents
	// rather than against a number.
	TArray<int32> TapCounts;
	for (const FVector2D& Ext : AwkwardExtentsCm)
	{
		FTrajectoryField Field = MakeField(Ext.X, Ext.Y, ShipCellCm, ShipWidthCm);
		const float R = Field.GetKernelRadiusTexels();

		// The PHASE table, not GetKernelOffsets(). The centred reference table drops taps whose disc area
		// happens to be zero, so its count genuinely wobbles 13..15 as D-A nudges R between extents — but
		// nothing deposits through it. The phase table stores its whole footprint and is the per-deposit
		// cost. Asserting the reference table here was measuring a number no frame pays.
		TapCounts.Add(Field.GetPhaseKernelTapCount());

		TestEqual(*FString::Printf(
			TEXT("T-BAND-5: %.1f x %.1f -- kernel radius follows width/(2*cell) (R = %.4f)"), Ext.X, Ext.Y, R),
			static_cast<double>(R),
			static_cast<double>(ShipWidthCm) / (2.0 * static_cast<double>(Field.GetEffectiveCmPerTexel())),
			1.0e-4);

		double WeightSum = 0.0;
		for (const float W : Field.GetKernelWeights())
		{
			WeightSum += static_cast<double>(W);
		}
		TestEqual(*FString::Printf(
			TEXT("T-BAND-5: the splat still conserves mass on %.1f x %.1f"), Ext.X, Ext.Y),
			WeightSum, 1.0, 1.0e-6);
	}

	int32 MinTaps = TapCounts[0];
	int32 MaxTaps = TapCounts[0];
	for (const int32 Count : TapCounts)
	{
		MinTaps = FMath::Min(MinTaps, Count);
		MaxTaps = FMath::Max(MaxTaps, Count);
	}
	TestEqual(*FString::Printf(
		TEXT("T-BAND-5: the tap count does not vary with building size (saw %d..%d across %d extents)"),
		MinTaps, MaxTaps, AwkwardExtentsCm.Num()), MinTaps, MaxTaps);

	// -- NON-VACUITY. Width is the live input: double it and the edges must halve.
	const FHeatmapLOSBands DoubleWidth =
		FHeatmapLOSBands::TrajectoryCrossings((2.0f * ShipWidthCm) / 100.0f, Reference);
	TestEqual(TEXT("T-BAND-5: doubling the stroke width halves band B, so the edges do track it"),
		DoubleWidth.BandB, 0.5f * FromWidth.BandB, 1.0e-6f);

	// The field's OWN default width is deliberately still 20 cm -- the oracle derivations and the width
	// cases in this file are written against a radius of exactly 1.0 texel and must not be re-tuned to
	// match a display preference. Asserting the contrast keeps the two apart.
	FTrajectoryField FieldDefault = MakeField(1000.0, 1000.0, 10.0f, 20.0f);
	TestEqual(TEXT("T-BAND-5: the field's own 20 cm default on a 10 cm cell is still a 3x3 splat"),
		FieldDefault.GetKernelOffsets().Num(), 9);

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-OFFSET-2 -- the field's grid spans EXACTLY the mesh extent on the major axis.
//
// THE GATE THAT WOULD HAVE CAUGHT D-A. T-OFFSET-1 settles stage A (the DDA) and says of stage B: "the
// RENDER -- texel -> UV -> mesh mapping putting cell 0 at a texel EDGE rather than its CENTRE. Not
// reachable from here." This is that stage, made reachable by testing the INVARIANT the render depends on
// instead of the render itself.
//
// BuildTileBuffers maps the mesh extent onto the FULL texture (UVx = x / ExtX). The field maps world to
// cell through a cm/texel that DimsForExtent ceil()s, so W texels used to represent W x Cm cm -- MORE
// world than the mesh spans. The two bases disagreed by that overhang and the drawn stroke was displaced
// toward the floor's minimum-XY corner, growing with distance from it: measured 18.3 cm at the min
// corner, 41.7 cm mid-floor and 68.1 cm at the far corner on the real 4548.9 x 3977.4 floor at 45 cm --
// 151% of a cell. Across 2000 random floors the median worst case was 40 cm.
//
// D-A snaps Cm to MajorExt / MajorDim, which makes the major axis exact BY CONSTRUCTION. So the whole
// defect reduces to one checkable equation, MajorDim x EffectiveCmPerTexel == MajorExt, with no world, no
// mesh component and no render target -- which is why this is a plain static case rather than the editor
// fixture the "not reachable" note assumed it would need.
//
// The MINOR axis is deliberately NOT required to be exact: its overhang IS the letterbox margin that
// TrajectoryTexelOffset centres. It is bounded here at under one cell, which is all the render needs.
//
// NON-VACUITY: the un-snapped ceil basis is recomputed alongside and asserted to MISS on at least one
// fixture, so these cannot pass by asserting an identity.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOffset2GridSpansMeshTest,
	"ProjectMobius.Heatmap.Trajectory.Offset.GridSpansMeshExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOffset2GridSpansMeshTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float RequestedCm = Defaults->TrajectoryWorldCmPerTexel;
	const float WidthCm = Defaults->TrajectoryDisplayPathWidthCm;

	// Chosen to divide badly. The first is the floor the misalignment was actually measured on; the last
	// is deliberately long and thin, where the letterbox margin is largest.
	const TArray<FVector2D> ExtentsCm = {
		FVector2D(4548.9, 3977.4),
		FVector2D(5000.0, 3000.0),
		FVector2D(2537.0, 2537.0),
		FVector2D(12345.6, 7890.1),
		FVector2D(1999.9, 407.3),
	};

	int32 UnsnappedMisses = 0;

	for (const FVector2D& Ext : ExtentsCm)
	{
		FTrajectoryField Field = MakeField(Ext.X, Ext.Y, RequestedCm, WidthCm);

		const double MajorExt = FMath::Max(Ext.X, Ext.Y);
		const FIntPoint Dims = Field.GetGridDims();
		const int32 MajorDim = FMath::Max(Dims.X, Dims.Y);
		const double Cm = static_cast<double>(Field.GetEffectiveCmPerTexel());

		// Absolute slack for the float cm/texel plus the bounded (1 + 1e-6) nudge Initialise may apply.
		// Two orders of magnitude tighter than the smallest overhang this is meant to catch.
		const double Tol = 0.05 + 1.0e-5 * MajorExt;

		const double Span = MajorDim * Cm;
		TestTrue(*FString::Printf(
			TEXT("T-OFFSET-2: %.1f x %.1f cm -- the grid spans %.4f cm on the major axis against a mesh ")
			TEXT("extent of %.4f cm (overhang %.4f cm = %.1f%% of a %.4f cm cell)"),
			Ext.X, Ext.Y, Span, MajorExt, Span - MajorExt, 100.0 * (Span - MajorExt) / Cm, Cm),
			FMath::Abs(Span - MajorExt) <= Tol);

		// The minor overhang is the letterbox and is expected, but never a whole cell: more than that
		// would mean DimsForExtent returned a dimension the extent does not need.
		const double MinorExt = FMath::Min(Ext.X, Ext.Y);
		const int32 MinorDim = FMath::Min(Dims.X, Dims.Y);
		const double MinorOverhang = (MinorDim * Cm) - MinorExt;
		TestTrue(*FString::Printf(
			TEXT("T-OFFSET-2: %.1f x %.1f cm -- minor overhang %.4f cm is inside one %.4f cm cell"),
			Ext.X, Ext.Y, MinorOverhang, Cm),
			MinorOverhang >= -Tol && MinorOverhang < Cm + Tol);

		// What the un-snapped ceil basis would have produced on this same extent.
		const int32 RawMajorDim = FMath::CeilToInt32(MajorExt / static_cast<double>(RequestedCm));
		if (FMath::Abs((RawMajorDim * static_cast<double>(RequestedCm)) - MajorExt) > Tol)
		{
			++UnsnappedMisses;
		}
	}

	TestTrue(TEXT("T-OFFSET-2: the un-snapped ceil basis misses on at least one fixture, so the span ")
		TEXT("assertions above are not identities"), UnsnappedMisses > 0);

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-BAND-7 -- the derived route threshold renders the stroke at its ACTUAL WIDTH, at every sub-cell phase.
//
// THE DEFECT. The display classifies each texel into one band, so a stroke is always a whole number of
// cells wide. The 0/1 edge used to be half a BYTE — an is-it-nonzero test — so every cell the kernel tail
// grazed painted at full band-B colour and the drawn stroke was as wide as the kernel SUPPORT rather than
// as wide as the stroke: 60 cm for a 45 cm path on 20 cm cells, swinging by a whole cell as the path slid
// across the lattice. The owner's report was simply "the big problem is how wide it is".
//
// WHAT IS ASSERTED, and why it is not a re-implementation of the derivation. This measures the OUTCOME —
// lay a straight path down at each sub-cell phase and count the lit rows — using the shipping kernel maths
// and the shipping threshold. It would still redden if DeriveRouteThresholdCrossings returned a plausible
// but wrong number, which a test that re-derived the threshold and compared could not.
//
// 45 / 15 == 3 exactly, so the shipping pair has an exact answer and the width must be CONSTANT. The test
// states that as a property of the configuration rather than hard-coding 45 cm, so dialling the cell to
// another exact divisor keeps it honest instead of reddening.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand7RouteThresholdWidthTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_7_RouteThresholdRendersTrueWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand7RouteThresholdWidthTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float WidthCm = Defaults->TrajectoryDisplayPathWidthCm;
	const float CellCm = Defaults->TrajectoryWorldCmPerTexel;
	const float Reference = FTrajectoryFieldConfig().ReferenceUsageDensity;

	const float Threshold = FTrajectoryField::DeriveRouteThresholdCrossings(WidthCm, CellCm);
	TestTrue(*FString::Printf(
		TEXT("T-BAND-8: the derived route threshold is a real crossing count, not the old half-byte test ")
		TEXT("(%.4f crossings at %.0f cm / %.0f cm)"), Threshold, WidthCm, CellCm),
		Threshold > 0.0f);

	const FHeatmapLOSBands Bands =
		FHeatmapLOSBands::TrajectoryCrossings(WidthCm / 100.0f, Reference, Threshold);
	TestTrue(TEXT("T-BAND-8: the route threshold sits strictly below the 1-crossing edge"),
		Bands.BandA < Bands.BandB);

	// Lay a straight axial path across the middle of a field and count the rows that clear the threshold.
	// Sweeping the path's lateral position across one whole cell is the point: the old behaviour was stable
	// at some offsets and a cell wider at others, so a single position could not have seen it.
	const double ExtentCm = 3000.0;
	TArray<int32> WidthsInCells;
	for (int32 Step = 0; Step < 9; ++Step)
	{
		FTrajectoryField Field = MakeField(ExtentCm, ExtentCm, CellCm, WidthCm);
		const double Cell = static_cast<double>(Field.GetEffectiveCmPerTexel());

		// Lateral offset walked across one full cell, in ninths.
		const double LateralCm = 1500.0 + (static_cast<double>(Step) / 9.0) * Cell;
		Field.DepositSegment(FVector2D(600.0, LateralCm), FVector2D(2400.0, LateralCm), 1.0f);

		const TArray<float>& Pres = Field.GetPresentation();
		const FIntPoint Dims = Field.GetGridDims();
		const double CellAreaM2 = (Cell / 100.0) * (Cell / 100.0);
		const double CrossingScale = (static_cast<double>(WidthCm) / 100.0) / CellAreaM2;

		// Column through the middle of the run, clear of both ends.
		const int32 Column = Dims.X / 2;
		int32 Lit = 0;
		for (int32 Row = 0; Row < Dims.Y; ++Row)
		{
			const double Crossings = static_cast<double>(Pres[Row * Dims.X + Column]) * CrossingScale;
			if (Crossings >= static_cast<double>(Threshold))
			{
				++Lit;
			}
		}
		WidthsInCells.Add(Lit);
	}

	int32 MinLit = WidthsInCells[0];
	int32 MaxLit = WidthsInCells[0];
	for (const int32 Lit : WidthsInCells)
	{
		MinLit = FMath::Min(MinLit, Lit);
		MaxLit = FMath::Max(MaxLit, Lit);
	}

	TestTrue(TEXT("T-BAND-8: the stroke lights at least one row at every phase"), MinLit >= 1);
	TestEqual(*FString::Printf(
		TEXT("T-BAND-8: the drawn width is CONSTANT across sub-cell phases (saw %d..%d rows)"),
		MinLit, MaxLit), MinLit, MaxLit);

	// And it is the right width. Stated as the configuration's own exact answer, so this follows the dial.
	const int32 ExpectedRows = FMath::Max(1, FMath::RoundToInt32(WidthCm / CellCm));
	TestEqual(*FString::Printf(
		TEXT("T-BAND-8: %.0f cm of stroke on %.0f cm cells draws %d rows (%.1f cm)"),
		WidthCm, CellCm, ExpectedRows, ExpectedRows * CellCm), MaxLit, ExpectedRows);

	// NON-VACUITY: the OLD half-byte edge must give a different, wider answer on at least one phase, or
	// this whole case is measuring something the threshold does not control.
	{
		// Swept over the same phases as above, not measured at one. The old edge happens to agree with the
		// new one at some sub-cell positions — that agreement IS the old behaviour's problem (its width
		// depended on where the path sat), so a single-phase probe can land on a passing case and prove
		// nothing. The first version of this check did exactly that.
		int32 WorstLitOld = 0;
		const double OldEdgeCrossings =
			(0.5 / 255.0) * ((static_cast<double>(WidthCm) / 100.0) * static_cast<double>(Reference));
		for (int32 Step = 0; Step < 9; ++Step)
		{
			FTrajectoryField Field = MakeField(ExtentCm, ExtentCm, CellCm, WidthCm);
			const double Cell = static_cast<double>(Field.GetEffectiveCmPerTexel());
			const double LateralCm = 1500.0 + (static_cast<double>(Step) / 9.0) * Cell;
			Field.DepositSegment(FVector2D(600.0, LateralCm), FVector2D(2400.0, LateralCm), 1.0f);

			const TArray<float>& Pres = Field.GetPresentation();
			const FIntPoint Dims = Field.GetGridDims();
			const double CrossingScale = (static_cast<double>(WidthCm) / 100.0)
				/ ((Cell / 100.0) * (Cell / 100.0));
			int32 LitOld = 0;
			for (int32 Row = 0; Row < Dims.Y; ++Row)
			{
				if (static_cast<double>(Pres[Row * Dims.X + Dims.X / 2]) * CrossingScale >= OldEdgeCrossings)
				{
					++LitOld;
				}
			}
			WorstLitOld = FMath::Max(WorstLitOld, LitOld);
		}
		TestTrue(*FString::Printf(
			TEXT("T-BAND-8: the old half-byte edge draws a WIDER stroke at its worst phase (%d rows ")
			TEXT("against %d), so the threshold is what is being measured"), WorstLitOld, ExpectedRows),
			WorstLitOld > ExpectedRows);
	}

	return true;
}

// -----------------------------------------------------------------------------------------------------
// T-OFFSET-3 -- the texel the field WRITES is the texel the render SAMPLES. Exactly. Every cell.
//
// This is the alignment contract itself, and the one the owner could still see failing after D-A and D-B:
// the stroke sat beside the agent, and it moved to the other axis when the floor was rotated.
//
// THE DEFECT IT GATES (D-C). HeatmapMeshUV letterboxes the MINOR axis by a REAL-valued margin of
// 0.5 * S * (1 - minorExt / majorExt) texels. The field can only write at INTEGER texels, so the
// fractional part of that margin is a permanent sub-texel offset between write and sample. On the real
// 4548.9 x 3977.4 floor the margin is 14.3224 texels and the 0.3224 put 32% OF THE FLOOR in the wrong
// texel. Because it lives on whichever axis is minor, rotating the floor moved the error to the other
// axis -- and floors whose extents divide evenly showed nothing at all, which is what made it look
// intermittent rather than structural.
//
// HOW IT IS MEASURED, and why this cannot pass vacuously:
//   * Sampled at SEVERAL FIXED SUB-CELL OFFSETS, spread across each cell and deliberately away from its
//     edges. Cell centres alone are USELESS here and the first version of this test made that mistake:
//     at a centre, floor(x/C + M) == floor(x/C) + round(M) is an ALGEBRAIC IDENTITY for any margin M, so
//     a centre-only sweep agrees whether or not the phasing is applied and proves nothing. The error
//     appears at offset f exactly when f >= 1 - frac(M) — the last 32% of each cell on the real floor.
//     The offsets are irregular so that f + frac(M) is not landing on an integer, which is the only place
//     a float tie could make an exact comparison flap.
//   * The render side calls AHeatmapPixelTextureVisualizer::HeatmapMeshUV -- the SAME function
//     BuildTileBuffers uses per vertex, not a re-derivation. A0-79 is why: a gate that owns a private copy
//     of the formula can pass while the shipping path is broken, and did, for two days.
//   * The field side calls PlanTrajectoryLatticePhase, likewise the shipping planner.
//   * Every fixture is run in BOTH orientations. An axis-symmetric bug would otherwise hide.
//   * NON-VACUITY: the same comparison is re-run with the phase removed (integer margin, no origin shift)
//     and asserted to FAIL on at least one fixture. Without that, a mapping that agreed trivially would
//     look like a pass.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOffset3LatticePhaseTest,
	"ProjectMobius.Heatmap.Trajectory.Offset.WrittenTexelIsSampledTexel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOffset3LatticePhaseTest::RunTest(const FString& Parameters)
{
	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();

	FTrajectoryFieldConfig Config;
	Config.WorldCmPerTexel = Defaults->TrajectoryWorldCmPerTexel;
	Config.DisplayPathWidthCm = Defaults->TrajectoryDisplayPathWidthCm;
	Config.MaxGridDim = Defaults->TrajectoryMaxGridDim;

	// Deliberately awkward, and each one is also run rotated 90 degrees. The first is the floor the
	// misalignment was measured on; 5000 x 3000 divides evenly and is the control that showed nothing.
	const TArray<FVector2D> BaseExtentsCm = {
		FVector2D(4548.9, 3977.4),
		FVector2D(5000.0, 3000.0),
		FVector2D(12345.6, 7890.1),
		FVector2D(1999.9, 407.3),
		FVector2D(2537.0, 2536.4),
	};

	int32 UnphasedFailures = 0;

	for (const FVector2D& Base : BaseExtentsCm)
	{
		for (int32 Rotate = 0; Rotate < 2; ++Rotate)
		{
			const FVector2D Ext = Rotate ? FVector2D(Base.Y, Base.X) : Base;

			const AHeatmapPixelTextureVisualizer::FTrajectoryLatticePhase Phase =
				AHeatmapPixelTextureVisualizer::PlanTrajectoryLatticePhase(Ext, Config);

			const int32 S = Phase.SquareSide;
			const double C = static_cast<double>(Phase.CmPerTexel);
			const FIntPoint Padded(Phase.GridDims.X + Phase.ExtraGridCells.X,
			                       Phase.GridDims.Y + Phase.ExtraGridCells.Y);
			if (!TestTrue(TEXT("T-OFFSET-3: the plan produced a usable grid"), S > 0 && C > 0.0))
			{
				continue;
			}

			// COVERAGE. The origin shift moves the lattice; the pad cells are what stop it uncovering a
			// strip of real floor. Deposits are never clamped into range, so an uncovered strip is a piece
			// of the building that silently stops accumulating.
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const double ExtCm = Axis ? Ext.Y : Ext.X;
				const double ShiftCm = Axis ? Phase.OriginShiftCm.Y : Phase.OriginShiftCm.X;
				const int32 Dim = Axis ? Padded.Y : Padded.X;
				TestTrue(*FString::Printf(
					TEXT("T-OFFSET-3: %.1f x %.1f axis %d -- the padded grid covers the mesh ")
					TEXT("(grid [%.3f, %.3f] vs mesh [0, %.3f])"),
					Ext.X, Ext.Y, Axis, ShiftCm, ShiftCm + Dim * C, ExtCm),
					ShiftCm <= UE_DOUBLE_KINDA_SMALL_NUMBER && (ShiftCm + Dim * C) >= ExtCm - UE_DOUBLE_KINDA_SMALL_NUMBER);
			}

			// The MAJOR axis is exact from D-A alone and must need no phasing at all. If this ever fires,
			// the snap regressed and T-OFFSET-2 should have caught it first.
			const bool bXMajor = Ext.X >= Ext.Y;
			TestTrue(*FString::Printf(
				TEXT("T-OFFSET-3: %.1f x %.1f -- the major axis needs no shift and no pad"), Ext.X, Ext.Y),
				FMath::IsNearlyZero(bXMajor ? Phase.OriginShiftCm.X : Phase.OriginShiftCm.Y, 1.0e-6)
				&& (bXMajor ? Phase.ExtraGridCells.X : Phase.ExtraGridCells.Y) == 0);

			// THE CONTRACT. Walk every cell centre that lands on the mesh and require the render to sample
			// the very texel the field wrote.
			int32 Mismatches = 0;
			int32 Checked = 0;
			int32 UnphasedMismatches = 0;
			FString FirstFailure;

			// Irregular on purpose — see the header note. A centre-only sweep is an identity.
			static const double SubCellOffsets[] = { 0.113, 0.317, 0.5, 0.661, 0.887 };
			const double MajorExt = FMath::Max(Ext.X, Ext.Y);
			const FIntPoint UnphasedOffset(
				FMath::RoundToInt32(0.5 * S * (1.0 - Ext.X / MajorExt)),
				FMath::RoundToInt32(0.5 * S * (1.0 - Ext.Y / MajorExt)));

			for (int32 J = 0; J < Padded.Y; ++J)
			{
				for (int32 I = 0; I < Padded.X; ++I)
				{
					for (const double FX : SubCellOffsets)
					{
						for (const double FY : SubCellOffsets)
						{
							// Offset from the MESH's minimum corner.
							const FVector2D LocalCm(Phase.OriginShiftCm.X + (I + FX) * C,
							                        Phase.OriginShiftCm.Y + (J + FY) * C);
							if (LocalCm.X < 0.0 || LocalCm.Y < 0.0
								|| LocalCm.X >= Ext.X || LocalCm.Y >= Ext.Y)
							{
								continue; // Pad cell hanging off the floor. Nothing renders there.
							}
							++Checked;

							const FVector2D UV = AHeatmapPixelTextureVisualizer::HeatmapMeshUV(LocalCm, Ext);
							const FIntPoint Sampled(
								FMath::FloorToInt32(UV.X * S), FMath::FloorToInt32(UV.Y * S));
							const FIntPoint Written(I + Phase.TexelOffset.X, J + Phase.TexelOffset.Y);

							if (Sampled != Written)
							{
								if (Mismatches == 0)
								{
									FirstFailure = FString::Printf(
										TEXT("cell (%d,%d) at sub-cell (%.3f,%.3f) -> world (%.3f,%.3f) is ")
										TEXT("written to texel (%d,%d) but sampled at (%d,%d)"),
										I, J, FX, FY, LocalCm.X, LocalCm.Y,
										Written.X, Written.Y, Sampled.X, Sampled.Y);
								}
								++Mismatches;
							}

							// The same comparison WITHOUT the phasing: integer margin, no origin shift.
							// This is what shipped before D-C and it must disagree somewhere, or the sweep
							// above is passing for reasons that have nothing to do with the fix.
							const FVector2D UnphasedLocal((I + FX) * C, (J + FY) * C);
							if (UnphasedLocal.X < Ext.X && UnphasedLocal.Y < Ext.Y)
							{
								const FVector2D UnphasedUV =
									AHeatmapPixelTextureVisualizer::HeatmapMeshUV(UnphasedLocal, Ext);
								if (FIntPoint(FMath::FloorToInt32(UnphasedUV.X * S),
								              FMath::FloorToInt32(UnphasedUV.Y * S))
									!= FIntPoint(I + UnphasedOffset.X, J + UnphasedOffset.Y))
								{
									++UnphasedMismatches;
								}
							}
						}
					}
				}
			}

			UnphasedFailures += UnphasedMismatches;

			if (Phase.bPhaseAbandoned)
			{
				// Near-square floor: the extents differ by less than one cell, so there is no room to pad
				// and the planner deliberately keeps the un-phased offset. This fixture is here to EXERCISE
				// that path, so demanding exactness of it would be asserting the fallback does not exist.
				// What is required is that it stays BOUNDED — the ideal margin is under half a texel here
				// by construction, so no cell may be more than one texel out.
				int32 WorstOut = 0;
				for (int32 J = 0; J < Padded.Y; ++J)
				{
					for (int32 I = 0; I < Padded.X; ++I)
					{
						for (const double FX : SubCellOffsets)
						{
							for (const double FY : SubCellOffsets)
							{
								const FVector2D LocalCm(Phase.OriginShiftCm.X + (I + FX) * C,
								                        Phase.OriginShiftCm.Y + (J + FY) * C);
								if (LocalCm.X < 0.0 || LocalCm.Y < 0.0
									|| LocalCm.X >= Ext.X || LocalCm.Y >= Ext.Y)
								{
									continue;
								}
								const FVector2D UV =
									AHeatmapPixelTextureVisualizer::HeatmapMeshUV(LocalCm, Ext);
								WorstOut = FMath::Max3(WorstOut,
									FMath::Abs(FMath::FloorToInt32(UV.X * S) - (I + Phase.TexelOffset.X)),
									FMath::Abs(FMath::FloorToInt32(UV.Y * S) - (J + Phase.TexelOffset.Y)));
							}
						}
					}
				}
				TestTrue(*FString::Printf(
					TEXT("T-OFFSET-3: %.1f x %.1f is the near-square FALLBACK -- error stays within one ")
					TEXT("texel (worst %d)"), Ext.X, Ext.Y, WorstOut), WorstOut <= 1);
			}
			else
			{
				TestTrue(*FString::Printf(
					TEXT("T-OFFSET-3: %.1f x %.1f -- checked %d cell centres, %d landed in the wrong texel. %s"),
					Ext.X, Ext.Y, Checked, Mismatches, Mismatches > 0 ? *FirstFailure : TEXT("")),
					Mismatches == 0);
			}
			TestTrue(*FString::Printf(TEXT("T-OFFSET-3: %.1f x %.1f -- the sweep actually visited cells"),
				Ext.X, Ext.Y), Checked > 0);
		}
	}

	// NON-VACUITY. Removing the phase must break the comparison; otherwise the assertions above hold for
	// reasons unrelated to D-C and would keep passing through a regression.
	TestTrue(TEXT("T-OFFSET-3: without the origin phasing at least one fixture lands in the wrong texel, ")
		TEXT("so this gate can actually fail"), UnphasedFailures > 0);

	return true;
}

// =====================================================================================================
// T-ORACLE-3 -- the same physical pass reads the same on any grid.
//
// THE BUG CLASS THIS GATES. Twice now, a length scale has been confused for the other one: the exposure
// ladder was derived against the cell side where its usage twin uses the stroke width (every edge 3x too
// demanding), and the representability bar for ReferenceExposureDensity was computed the same way (238.1
// where the real figure is 190.5). Both were caught by hand, months apart, by someone re-reading a
// comment. The mechanical answer is a gate that deposits ONE physical pass into fields of different cell
// size and demands the same answer -- because every one of those defects makes the answer move with the
// cell.
//
// ⚠️ WHAT IS INVARIANT IS THE WIDTH AVERAGE, NOT THE CENTRELINE, and the difference is the whole design of
// this test. The handoff that requested it (`HANDOFF_CdAndHeatmapBands_2026-08-13.md` §5a item 1) asked
// for "the same pass at s = 10 / 15 / 25 / 45 cm must yield an identical RVal". Written that way against
// the peak cell it FAILS, and correctly so: BuildKernel splats a DISC, so convolving a line with it gives
// a semicircular lateral profile whose peak depends on how many cells span the stroke. Measured on the
// shipping 45 cm stroke, the centreline reads 2.222 / 2.676 / 2.776 / 2.806 person/m at s = 45 / 25 / 15 /
// 10 cm -- a 26% spread, converging on 4/(pi*w) from below as the grid refines.
//
// The band edges are defined on the WIDTH AVERAGE (edge_N = (N + 0.5) / (width x Reference), and owner
// ruling 2026-08-14 on D5b confirmed that is the intended reading: the stroke is a fixed-width ribbon and
// a band describes the ribbon, not its hottest line). That quantity IS invariant, exactly, by mass
// conservation -- and this test asserts it across a 4.5x range of cell sizes.
//
// The cross-section sum is taken in the MIDDLE of a long straight pass. The kernel leaks along the path
// as well as across it, so a column near an end is short of mass; away from the ends what leaks out of a
// column equals what leaks in, and the sum is the canonical cell total exactly. Being a full-column sum it
// is also independent of sub-cell phase, which is why no phase sweep is needed here.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajOracle3UsageResolutionTest,
	"ProjectMobius.Heatmap.Trajectory.Oracle.T_ORACLE_3_UsageResolutionInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajOracle3UsageResolutionTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float WidthCm = Defaults->TrajectoryDisplayPathWidthCm;
	const float WidthM = WidthCm / 100.0f;
	const float Reference = FTrajectoryFieldConfig().ReferenceUsageDensity;

	// One person walking a straight line down the middle of a 10 x 4 m patch. Long enough that a column
	// sampled at the midpoint is many kernel radii from either end even at the coarsest cell.
	constexpr double ExtentXCm = 1000.0;
	constexpr double ExtentYCm = 400.0;
	constexpr double PathYCm = 200.0;
	constexpr double PathStartXCm = 100.0;
	constexpr double PathEndXCm = 900.0;

	// The exact value the ladder places "one crossing" at. Reference-free in the sense that matters: it
	// contains the stroke width and NOT the cell, which is the entire claim under test.
	const double ExpectedDensity = 1.0 / static_cast<double>(WidthM);        // person/m
	const double ExpectedRVal = ExpectedDensity / static_cast<double>(Reference);

	double FirstRVal = -1.0;
	double PeakSpreadMin = TNumericLimits<double>::Max();
	double PeakSpreadMax = -TNumericLimits<double>::Max();
	int32 Measured = 0;

	for (const float CellCm : { 10.0f, 15.0f, 25.0f, 45.0f })
	{
		FTrajectoryField Field = MakeField(ExtentXCm, ExtentYCm, CellCm, WidthCm);
		if (!TestTrue(*FString::Printf(TEXT("T-ORACLE-3: the %.0f cm field is valid"), CellCm),
			Field.IsValid()))
		{
			continue;
		}

		// Deposited as ONE segment: the DDA books per-cell person-metres along it, so the canonical mass in
		// a mid-path cell is one cell side regardless of how the segment was chopped. Duration is
		// irrelevant to Route Usage and is only here because DepositSegment needs one.
		Field.DepositSegment(FVector2D(PathStartXCm, PathYCm), FVector2D(PathEndXCm, PathYCm), 1.0f);

		const float EffectiveCellCm = Field.GetEffectiveCmPerTexel();
		const double EffectiveCellM = static_cast<double>(EffectiveCellCm) / 100.0;
		const FIntPoint Dims = Field.GetGridDims();
		const TArray<float>& Presented = Field.GetPresentation(ETrajectoryMapMode::RouteUsage);
		if (!TestTrue(*FString::Printf(TEXT("T-ORACLE-3: the %.0f cm field presented a grid"), CellCm),
			Presented.Num() >= Dims.X * Dims.Y) || !(EffectiveCellM > 0.0))
		{
			continue;
		}

		// Mid-path column, then the WHOLE column so the sum is the cross-section rather than a sample of it.
		const int32 Column = FMath::Clamp(
			FMath::FloorToInt32(((PathStartXCm + PathEndXCm) * 0.5) / EffectiveCellCm), 0, Dims.X - 1);
		double ColumnPersonMetres = 0.0;
		double PeakCell = 0.0;
		for (int32 Row = 0; Row < Dims.Y; ++Row)
		{
			const double Value = static_cast<double>(Presented[Row * Dims.X + Column]);
			ColumnPersonMetres += Value;
			PeakCell = FMath::Max(PeakCell, Value);
		}

		// Width average: the column's person-metres spread over the ribbon's area (width x one cell of
		// path), which is the density a band edge is stated in.
		const double WidthAverageDensity = ColumnPersonMetres / (static_cast<double>(WidthM) * EffectiveCellM);
		const double RVal = WidthAverageDensity / static_cast<double>(Reference);

		TestEqual(*FString::Printf(
			TEXT("T-ORACLE-3: one pass reads %.6f person/m width-averaged on a %.0f cm cell (effective ")
			TEXT("%.4f) -- the ladder places one crossing at %.6f, and this must not move with the grid"),
			WidthAverageDensity, CellCm, EffectiveCellCm, ExpectedDensity),
			RVal, ExpectedRVal, ExpectedRVal * 1.0e-3);

		if (FirstRVal < 0.0)
		{
			FirstRVal = RVal;
		}
		else
		{
			TestEqual(*FString::Printf(
				TEXT("T-ORACLE-3: the %.0f cm cell agrees with the first cell size measured"), CellCm),
				RVal, FirstRVal, FirstRVal * 1.0e-3);
		}

		// Recorded, deliberately NOT asserted invariant -- see the header. Its spread is what makes the
		// width average the right quantity to gate, and printing it keeps the reason next to the evidence.
		const double PeakDensity = PeakCell / (EffectiveCellM * EffectiveCellM);
		PeakSpreadMin = FMath::Min(PeakSpreadMin, PeakDensity);
		PeakSpreadMax = FMath::Max(PeakSpreadMax, PeakDensity);
		++Measured;
	}

	TestTrue(TEXT("T-ORACLE-3: every cell size was actually measured"), Measured == 4);

	// NON-VACUITY, and the D5b evidence in one assertion. If the CENTRELINE were also cell-invariant then
	// the distinction this test is built around would be imaginary and the width-average assertions above
	// would be passing for a reason that has nothing to do with mass conservation. It is not: the disc
	// kernel's lateral profile is semicircular, so the peak climbs toward 4/(pi*w) as the grid refines.
	TestTrue(*FString::Printf(
		TEXT("T-ORACLE-3: the CENTRELINE is NOT cell-invariant (%.4f..%.4f person/m across the sweep), ")
		TEXT("which is why the width average is the quantity the bands are defined on"),
		PeakSpreadMin, PeakSpreadMax),
		Measured == 4 && (PeakSpreadMax - PeakSpreadMin) > (PeakSpreadMax * 0.05));

	return true;
}

// =====================================================================================================
// T-BAND-8 -- the Route Exposure dwell ladder means the SECONDS the D5 ruling fixed it to mean.
//
// WHY THIS GATE EXISTS AT ALL. Until 2026-08-14 the exposure ladder (2 / 5 / 15 / 50 transit-equivalents)
// had NO test of any kind -- the T-BAND-* set covers TrajectoryCrossings only. That is how it shipped for
// a month with band F opening at 12.6 seconds of standing, i.e. with everything from thirteen seconds to
// ten minutes painting one colour, inside exactly the regime the surface exists to show. Nothing could
// have caught it, because nothing anywhere converted an edge into a quantity a human could sanity-check.
//
// THE RULING (D5) is that the edges sit at approximately 1 / 3 / 10 / 30 SECONDS of standing at the
// shipping stroke. The transit constants 4 / 12 / 40 / 120 are DERIVED from that, so this test asserts the
// seconds and lets the constants be whatever hits them -- the reverse (asserting 4/12/40/120) would be
// comparing the implementation to itself and would survive a stroke-width change that silently moved
// every second.
//
// FOUR PROPERTIES, in the order a regression would hit them:
//
//   1. SECONDS. Each edge converts to its ruled second count, within 2%. The 2% is the room the ruling
//      leaves for round transit constants: 120 transits is 30.29 s, not 30.00.
//   2. THE CONVERSION IS REAL, not algebra agreeing with itself. StandingDwellSecondsAtEdge is a closed
//      form that dropped both the cell size and the walking speed. That is re-derived here THROUGH AN
//      ACTUAL FTrajectoryField -- its real BuildKernel weights, its real effective cell -- and the two
//      routes must agree. If the closed form ever stops describing the kernel, this is what says so.
//   3. CELL-INDEPENDENCE. Sweeping the cell must not move a second. This is the property that makes the
//      figure quotable in a deck: it survives D2b's snap and any future silhouette-quality dial.
//   4. REPRESENTABILITY. Band F must stay reachable, and MinimumExposureReferenceForFullLadder must be
//      computing the bar for the ladder that actually ships. Those disagreed by construction until the
//      top step was named -- it was a literal 50 in that helper while the ladder said 120.
//
// NON-VACUITY is asserted twice: the superseded 2/5/15/50 ladder must FAIL property 1, and the cell sweep
// is paired with a width sweep that must MOVE the seconds.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajBand8ExposureDwellTest,
	"ProjectMobius.Heatmap.Trajectory.Bands.T_BAND_8_ExposureDwellLadder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajBand8ExposureDwellTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryOracle;

	// Shipping configuration read from the same sources the runtime reads, never transcribed -- the whole
	// point is that a dial change propagates into the expectation instead of reddening a literal.
	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float ShipCellCm = Defaults->TrajectoryWorldCmPerTexel;
	const float ShipWidthCm = Defaults->TrajectoryDisplayPathWidthCm;
	const float ShipWidthM = ShipWidthCm / 100.0f;
	const float Reference = FTrajectoryFieldConfig().ReferenceExposureDensity;
	constexpr float VFree = FHeatmapLOSBands::FreeWalkSpeedSFPE;

	const FHeatmapLOSBands Bands = FHeatmapLOSBands::TrajectoryTransits(ShipWidthM, Reference, VFree);
	const float Edges[4] = { Bands.BandB, Bands.BandC, Bands.BandD, Bands.BandE };

	// -- 1. The ruled seconds. Transcribed from the D5 ruling, NOT from the transit constants.
	const double RuledSeconds[4] = { 1.0, 3.0, 10.0, 30.0 };
	const TCHAR* const BandNames[4] = { TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E/F") };
	constexpr double RulingTolerance = 0.02;   // 2% -- the slack round transit constants need

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const double Seconds = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
			Edges[Index], ShipWidthM, Reference);
		const double Error = FMath::Abs(Seconds - RuledSeconds[Index]) / RuledSeconds[Index];
		TestTrue(*FString::Printf(
			TEXT("T-BAND-8: band %s opens at %.3f s of standing, ruled %.0f s (%.2f%% out, limit %.0f%%). ")
			TEXT("If the ladder was re-tuned deliberately, update the ruled seconds here AND the ")
			TEXT("calibration block on FHeatmapLOSBands::TrajectoryTransits together."),
			BandNames[Index], Seconds, RuledSeconds[Index], Error * 100.0, RulingTolerance * 100.0),
			Error <= RulingTolerance);
	}

	// NON-VACUITY (a). The superseded ladder must fail the very check that just passed, or the tolerance is
	// wide enough to accept anything and property 1 asserts nothing. 50 transits was 12.62 s against a
	// ruled 30 -- if this ever holds, someone has widened RulingTolerance past usefulness.
	{
		const double SupersededTopEdge = 50.0 / (static_cast<double>(ShipWidthM) * Reference * VFree);
		const double SupersededSeconds = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
			static_cast<float>(SupersededTopEdge), ShipWidthM, Reference);
		TestTrue(*FString::Printf(
			TEXT("T-BAND-8: the superseded 50-transit top step (%.2f s) is rejected by the same check that ")
			TEXT("accepts the shipping one, so property 1 can actually fail"), SupersededSeconds),
			FMath::Abs(SupersededSeconds - 30.0) / 30.0 > RulingTolerance);
	}

	// -- 2. The closed form re-derived through a REAL field and its REAL kernel.
	//
	// StandingDwellSecondsAtEdge claims seconds = Edge * Ref * (pi*w^2/4), with no cell size in it. The
	// independent route goes the long way round: a stationary agent's person-seconds land in the canonical
	// cell and BuildKernel spreads them, so the cell keeps only the CENTRE TAP's share; the displayed value
	// is that share divided by the cell area and the reference. Inverting,
	//
	//     seconds = Edge * Ref * CellArea / CentreTapWeight
	//
	// which contains the cell twice and must still land on the same number. Reading the weight from
	// Field.GetKernelWeights() rather than computing 1/(pi*R^2) is the point: this is the assertion that
	// fails if BuildKernel's shape rule ever changes out from under the legend.
	{
		FTrajectoryField Field = MakeField(4000.0, 2200.0, ShipCellCm, ShipWidthCm);
		const float EffectiveCellM = Field.GetEffectiveCmPerTexel() / 100.0f;
		const float RadiusTexels = Field.GetKernelRadiusTexels();

		const TArray<FIntPoint>& Offsets = Field.GetKernelOffsets();
		const TArray<float>& Weights = Field.GetKernelWeights();
		int32 CentreTap = INDEX_NONE;
		for (int32 Tap = 0; Tap < Offsets.Num(); ++Tap)
		{
			if (Offsets[Tap] == FIntPoint(0, 0))
			{
				CentreTap = Tap;
				break;
			}
		}

		if (TestTrue(TEXT("T-BAND-8: the shipping kernel has a centre tap"),
			CentreTap != INDEX_NONE && Weights.IsValidIndex(CentreTap)) && EffectiveCellM > 0.0f)
		{
			const double CentreWeight = static_cast<double>(Weights[CentreTap]);
			const double CellArea = static_cast<double>(EffectiveCellM) * EffectiveCellM;

			for (int32 Index = 0; Index < 4; ++Index)
			{
				const double ViaKernel = static_cast<double>(Edges[Index]) * Reference * CellArea
					/ CentreWeight;
				const double ViaClosedForm = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
					Edges[Index], ShipWidthM, Reference);
				TestEqual(*FString::Printf(
					TEXT("T-BAND-8: band %s -- the closed form (%.4f s) matches the real kernel's centre ")
					TEXT("tap (%.4f s, weight %.7f, effective cell %.4f m)"),
					BandNames[Index], ViaClosedForm, ViaKernel, CentreWeight, EffectiveCellM),
					ViaKernel, ViaClosedForm, ViaClosedForm * 1.0e-4);
			}

			// The phase-invariance the calibration block leans on. The centre cell's farthest corner sits
			// sqrt(2) texels from a disc centre displaced by half a cell on both axes, so a radius of at
			// least that covers the cell WHATEVER the sub-cell placement and the weight cannot depend on
			// phase. Below it, the quoted seconds become a lower bound rather than the number.
			TestTrue(*FString::Printf(
				TEXT("T-BAND-8: kernel radius %.4f texels >= sqrt(2), so the centre tap is phase-invariant ")
				TEXT("and the dwell figure is exact rather than a best case"), RadiusTexels),
				RadiusTexels >= UE_SQRT_2);
		}
	}

	// -- 3. Cell-independence, and NON-VACUITY (b): the width must move what the cell cannot.
	{
		const double ShippingTop = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
			Bands.BandE, ShipWidthM, Reference);

		for (const float CellCm : { 5.0f, 10.0f, ShipCellCm, 45.0f, 90.0f })
		{
			FTrajectoryField Field = MakeField(4000.0, 2200.0, CellCm, ShipWidthCm);
			const FHeatmapLOSBands Swept = FHeatmapLOSBands::TrajectoryTransits(
				ShipWidthM, Field.GetConfig().ReferenceExposureDensity, VFree);
			const double Seconds = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
				Swept.BandE, ShipWidthM, Reference);
			TestEqual(*FString::Printf(
				TEXT("T-BAND-8: the top band still opens at %.3f s on a %.0f cm cell (effective %.4f)"),
				ShippingTop, CellCm, Field.GetEffectiveCmPerTexel()),
				Seconds, ShippingTop, ShippingTop * 1.0e-4);
		}

		const float WiderM = ShipWidthM * 2.0f;
		const FHeatmapLOSBands Wider = FHeatmapLOSBands::TrajectoryTransits(WiderM, Reference, VFree);
		const double WiderSeconds = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
			Wider.BandE, WiderM, Reference);
		TestTrue(*FString::Printf(
			TEXT("T-BAND-8: doubling the stroke DOES move the top band (%.3f s vs %.3f s), so the cell ")
			TEXT("sweep above is not passing because nothing was connected"), WiderSeconds, ShippingTop),
			!FMath::IsNearlyEqual(WiderSeconds, ShippingTop, ShippingTop * 0.01));
	}

	// -- 4. Representability, and that the helper computing it knows which ladder ships.
	{
		TestTrue(*FString::Printf(
			TEXT("T-BAND-8: the top edge %.6f leaves band F reachable inside the [0,1] channel"),
			Bands.BandE), Bands.BandE < 1.0f);

		const float MinimumReference = FHeatmapLOSBands::MinimumExposureReferenceForFullLadder(
			ShipWidthM, VFree);
		TestTrue(*FString::Printf(
			TEXT("T-BAND-8: the shipping reference %.1f clears the %.1f the full ladder needs"),
			Reference, MinimumReference), Reference >= MinimumReference);

		// The helper and the ladder must be talking about the same top step. Recovering it from the helper
		// is what catches the two drifting apart -- which they silently would have, since the helper held a
		// literal 50 while the ladder was re-tuned to 120.
		const double RecoveredTopStep = static_cast<double>(MinimumReference) * ShipWidthM * VFree;
		const double LadderTopStep = static_cast<double>(Bands.BandE) * ShipWidthM * Reference * VFree;
		TestEqual(*FString::Printf(
			TEXT("T-BAND-8: MinimumExposureReferenceForFullLadder is sized for the %.1f-transit top step ")
			TEXT("the ladder actually uses, not a stale one"), LadderTopStep),
			RecoveredTopStep, LadderTopStep, LadderTopStep * 1.0e-4);
	}

	return true;
}

// =====================================================================================================
// T-LEGEND-1 -- the colour key prints the numbers the surface is actually banded by.
//
// WHY. Every other gate in this file asserts the CONTRACT (what an edge means) or the RENDER (what colour
// a value takes). Nothing asserted the third thing a user actually reads: the legend. That matters more
// than it sounds, because the legend is the only place the numbers appear in a form somebody can quote --
// and this surface has already shipped a key that was 20% adrift from its own pixels for three days
// without anything noticing, because nothing looked.
//
// The specific risk after the 2026-08-14 D5 work is a UNIT PAIRING: the exposure column changed from
// transit-equivalents to seconds, and the header changed with it. Those are two edits in two places. One
// line reverting either leaves a legend confidently labelling transits as seconds, which is worse than
// the bug it replaced -- it would read as authoritative.
//
// Each surface is checked by INVERTING the printed text back to the physical quantity and comparing
// against the shipped conversion, never against a table. A table here would be a fourth copy of the
// ladder and would agree with itself after the ladder moved.
// -----------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajLegend1PrintsBandedNumbersTest,
	"ProjectMobius.Heatmap.Trajectory.Legend.T_LEGEND_1_KeyPrintsTheBandedNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajLegend1PrintsBandedNumbersTest::RunTest(const FString& Parameters)
{
	// Values are printed with trailing zeros trimmed ("1", not "1.0"), so parse rather than string-match,
	// and allow half of the last printed digit.
	auto ValueOf = [](const FHeatmapLegendContents& Contents, int32 Row) -> double
	{
		return FCString::Atod(*Contents.Rows[Row].Value.ToString());
	};
	constexpr double PrintTolerance = 0.05;

	const AHeatmapPixelTextureVisualizer* Defaults = GetDefault<AHeatmapPixelTextureVisualizer>();
	const float WidthM = Defaults->TrajectoryDisplayPathWidthCm / 100.0f;
	const FTrajectoryFieldConfig Config;

	// ---- Route Exposure: the column is SECONDS, and the header says so -------------------------------
	{
		const FHeatmapLOSBands Bands = FHeatmapLOSBands::TrajectoryTransits(
			WidthM, Config.ReferenceExposureDensity, FHeatmapLOSBands::FreeWalkSpeedSFPE);
		const FHeatmapLegendContents Key = FHeatmapLegend::RouteExposure(
			Bands, WidthM, Config.ReferenceExposureDensity, FHeatmapLOSBands::FreeWalkSpeedSFPE);

		if (TestTrue(TEXT("T-LEGEND-1: the exposure key has six bands and data"),
			Key.bHasData && Key.Rows.Num() == 6))
		{
			const float Edges[5] = { Bands.BandA, Bands.BandB, Bands.BandC, Bands.BandD, Bands.BandE };
			for (int32 Row = 1; Row < 5; ++Row)
			{
				const double Expected = FHeatmapLOSBands::StandingDwellSecondsAtEdge(
					Edges[Row], WidthM, Config.ReferenceExposureDensity);
				TestEqual(*FString::Printf(
					TEXT("T-LEGEND-1: exposure row %d prints %.3f s, the standing dwell its edge means"),
					Row, Expected), ValueOf(Key, Row), Expected, PrintTolerance);
			}

			// The top row repeats the last edge behind a ">" -- an open-ended band has no upper bound to
			// print, and printing the previous edge without the qualifier would state the opposite.
			TestEqual(TEXT("T-LEGEND-1: the open top band repeats the last edge"),
				ValueOf(Key, 5),
				static_cast<double>(FHeatmapLOSBands::StandingDwellSecondsAtEdge(
					Bands.BandE, WidthM, Config.ReferenceExposureDensity)), PrintTolerance);
			TestEqual(TEXT("T-LEGEND-1: and qualifies it with '>'"), Key.Rows[5].Qualifier.ToString(),
				FString(TEXT(">")));
		}

		// THE UNIT PAIRING. The values above are seconds; the header must say seconds. This is the
		// assertion that catches the column silently reverting to transit-equivalents.
		const FString Header = Key.ValueHeader.ToString();
		TestTrue(*FString::Printf(
			TEXT("T-LEGEND-1: the exposure header names the unit the column is printed in -- got '%s', ")
			TEXT("which must carry '(s)' while the values are seconds"), *Header),
			Header.Contains(TEXT("(s)")));

		// The caveat is a disclosure obligation: the steps are a readability choice with no published
		// standard behind them, and this tooltip is the only place a reader is ever told so. Asserted by
		// PRESENCE and substance, deliberately NOT by matching the wording.
		//
		// A substring check against the sentence currently shipping would pass forever and then fail the
		// day somebody improves the phrasing — a false alarm that trains the next reader to edit the test
		// until it goes quiet, which is how a real disclosure gets deleted. What must not happen is the
		// tooltip going empty or becoming a bare restatement of the header, and a length floor catches
		// exactly that while leaving the words free.
		TestTrue(*FString::Printf(
			TEXT("T-LEGEND-1: the exposure header carries a substantive tooltip (%d chars) — this column ")
			TEXT("prints a one-stationary-person yardstick against steps that are not a published ")
			TEXT("standard, and neither fact is inferable from the number alone"),
			Key.ValueHeaderTooltip.ToString().Len()),
			Key.ValueHeaderTooltip.ToString().Len() > 200);
	}

	// ---- Route Usage: the column is PASSES ------------------------------------------------------------
	{
		const FHeatmapLOSBands Bands = FHeatmapLOSBands::TrajectoryCrossings(
			WidthM, Config.ReferenceUsageDensity);
		const FHeatmapLegendContents Key = FHeatmapLegend::RouteUsage(
			Bands, WidthM, Config.ReferenceUsageDensity);

		if (TestTrue(TEXT("T-LEGEND-1: the usage key has six bands and data"),
			Key.bHasData && Key.Rows.Num() == 6))
		{
			// Edges are half-steps -- (N + 0.5) crossings -- so the printed count is the edge with the half
			// taken back off. Inverting through the same width and reference the bands were built from.
			const float Edges[5] = { Bands.BandA, Bands.BandB, Bands.BandC, Bands.BandD, Bands.BandE };
			for (int32 Row = 1; Row < 5; ++Row)
			{
				const double Passes = FMath::RoundToDouble(
					static_cast<double>(Edges[Row]) * WidthM * Config.ReferenceUsageDensity - 0.5);
				TestEqual(*FString::Printf(TEXT("T-LEGEND-1: usage row %d prints %.0f passes"), Row, Passes),
					ValueOf(Key, Row), Passes, PrintTolerance);
			}
		}

		TestTrue(*FString::Printf(TEXT("T-LEGEND-1: the usage header reads 'passes', not the internal ")
			TEXT("'crossings' -- got '%s'"), *Key.ValueHeader.ToString()),
			Key.ValueHeader.ToString().Contains(TEXT("passes")));
	}

	// ---- Density: the column must invert back to Fruin's published boundaries -------------------------
	//
	// The one surface with a real standard behind it, so the strongest available assertion is available
	// here and nowhere else: the printed m^2/person must be Fruin's own numbers.
	{
		const FHeatmapLegendContents Key = FHeatmapLegend::Density(FHeatmapLOSBands::Density());
		const double Fruin[5] = { 3.24, 2.32, 1.39, 0.93, 0.46 };

		if (TestTrue(TEXT("T-LEGEND-1: the density key has six bands and data"),
			Key.bHasData && Key.Rows.Num() == 6))
		{
			for (int32 Row = 0; Row < 5; ++Row)
			{
				TestEqual(*FString::Printf(
					TEXT("T-LEGEND-1: density row %d prints Fruin's %.2f m2/person"), Row, Fruin[Row]),
					ValueOf(Key, Row), Fruin[Row], 0.01);
			}
			// The qualifier FLIPS relative to the trajectory surfaces: more m^2/person means EMPTIER, so
			// the open end is at the top. Getting this backwards inverts the whole key's meaning.
			TestEqual(TEXT("T-LEGEND-1: density band A is the open '>' end"),
				Key.Rows[0].Qualifier.ToString(), FString(TEXT(">")));
			TestEqual(TEXT("T-LEGEND-1: and density band F is the '<' end"),
				Key.Rows[5].Qualifier.ToString(), FString(TEXT("<")));
		}
	}

	// ---- The no-heatmap-loaded path still prints a real key ------------------------------------------
	//
	// Owner report 2026-08-14: switching surface before loading a dataset changed nothing on screen,
	// because the key returned bHasData = false and the widget drew blank. Every input to both ladders has
	// a class default, so the key a heatmap WOULD print is knowable in advance. This asserts it is printed,
	// and that it is the same key -- a preview, not a placeholder.
	{
		const FHeatmapLegendContents DefaultExposure =
			AHeatmapPixelTextureVisualizer::GetDefaultLegendContents(true, true);
		const FHeatmapLegendContents DefaultUsage =
			AHeatmapPixelTextureVisualizer::GetDefaultLegendContents(true, false);
		const FHeatmapLegendContents DefaultDensity =
			AHeatmapPixelTextureVisualizer::GetDefaultLegendContents(false, false);

		TestTrue(TEXT("T-LEGEND-1: all three surfaces print a key with no heatmap registered"),
			DefaultExposure.bHasData && DefaultUsage.bHasData && DefaultDensity.bHasData);
		TestTrue(TEXT("T-LEGEND-1: and all three fill six rows"),
			DefaultExposure.Rows.Num() == 6 && DefaultUsage.Rows.Num() == 6
			&& DefaultDensity.Rows.Num() == 6);

		// Distinct titles, or the surface switch the owner reported is still invisible even though the key
		// is now populated.
		TestTrue(TEXT("T-LEGEND-1: the three surfaces are told apart by their titles"),
			!DefaultExposure.Title.EqualTo(DefaultUsage.Title)
			&& !DefaultUsage.Title.EqualTo(DefaultDensity.Title));

		const FHeatmapLOSBands Live = FHeatmapLOSBands::TrajectoryTransits(
			WidthM, Config.ReferenceExposureDensity, FHeatmapLOSBands::FreeWalkSpeedSFPE);
		const FHeatmapLegendContents LiveExposure = FHeatmapLegend::RouteExposure(
			Live, WidthM, Config.ReferenceExposureDensity, FHeatmapLOSBands::FreeWalkSpeedSFPE);
		if (DefaultExposure.Rows.Num() == 6 && LiveExposure.Rows.Num() == 6)
		{
			TestEqual(TEXT("T-LEGEND-1: the pre-load exposure key PREVIEWS the live one rather than "
				"guessing"), ValueOf(DefaultExposure, 4), ValueOf(LiveExposure, 4), PrintTolerance);
		}
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
