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

	// Sub-texel identity: 25 cm/texel, 20 cm display width -> R = 0.4 texel < 0.5 -> identity kernel.
	FTrajectoryField SubTexel = MakeField(1000.0, 1000.0, 25.0f, /*DisplayPathWidthCm*/ 20.0f);
	SubTexel.DepositSegment(FVector2D(302, 303), FVector2D(307, 308), 0.1f);
	const TArray<float>& SubCanon = SubTexel.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& SubPres = SubTexel.GetPresentation();
	bool bIdentity = SubCanon.Num() == SubPres.Num();
	for (int32 i = 0; bIdentity && i < SubCanon.Num(); ++i)
	{
		bIdentity = (SubCanon[i] == SubPres[i]);
	}
	TestTrue(TEXT("sub-texel radius: Presentation == Canonical bitwise (identity kernel)"), bIdentity);
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

#endif // !UE_BUILD_SHIPPING
