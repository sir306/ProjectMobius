/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "TrajectoryField.h"

// No logging anywhere in this translation unit, by policy: DepositSegment runs per agent per frame at
// crowd scale and the encode path runs per flush. A single UE_LOG or on-screen message in either
// invalidates the latency numbers this whole rebuild is being measured by.

namespace TrajectoryFieldPrivate
{
	/** Larger than any parametric t the walk can produce, so an axis with zero direction never crosses. */
	static constexpr double NeverCrosses = 1.0e300;

	/** Anything at or beyond this magnitude is not a usable world coordinate. */
	static constexpr double FiniteBound = 1.0e300;

	/**
	 * Magnitude bound rather than FMath::IsFinite: a bound comparison is false for both infinity and NaN
	 * (all NaN comparisons are false) and survives /fp:fast, whereas the usual self-subtraction trick
	 * does not. World coordinates this large are garbage regardless.
	 */
	FORCEINLINE bool IsUsable(double A)
	{
		return FMath::Abs(A) < FiniteBound;
	}

	FORCEINLINE bool IsUsable2D(const FVector2D& V)
	{
		return IsUsable(static_cast<double>(V.X)) && IsUsable(static_cast<double>(V.Y));
	}

	/**
	 * Antiderivative for the disc-area kernel: G(u) = integral of sqrt(R^2 - s^2) ds from 0 to u
	 *                                               = 0.5 * ( u*sqrt(R^2 - u^2) + R^2*asin(u/R) ).
	 * Argument is clamped into [0, R] so a caller can pass an unbounded rectangle edge.
	 */
	double DiscArcIntegral(double U, double R)
	{
		if (R <= 0.0)
		{
			return 0.0;
		}

		const double Clamped = FMath::Clamp(U, 0.0, R);
		const double Inner = FMath::Max(0.0, R * R - Clamped * Clamped);
		return 0.5 * (Clamped * FMath::Sqrt(Inner) + R * R * FMath::Asin(FMath::Clamp(Clamped / R, -1.0, 1.0)));
	}

	/**
	 * Area of { 0 <= u <= X, 0 <= v <= Y, u^2 + v^2 <= R^2 } - the first-quadrant corner primitive. X and
	 * Y are magnitudes. Below u* = sqrt(R^2 - Y^2) the rectangle is wholly inside the disc, so that part
	 * is exact rather than integrated; past it the arc is the upper bound.
	 */
	double DiscCornerArea(double X, double Y, double R)
	{
		if (R <= 0.0)
		{
			return 0.0;
		}

		const double Xc = FMath::Clamp(X, 0.0, R);
		const double Yc = FMath::Clamp(Y, 0.0, R);
		if (Xc <= 0.0 || Yc <= 0.0)
		{
			return 0.0;
		}

		const double UStar = FMath::Sqrt(FMath::Max(0.0, R * R - Yc * Yc));
		if (Xc <= UStar)
		{
			return Xc * Yc;
		}

		return UStar * Yc + (DiscArcIntegral(Xc, R) - DiscArcIntegral(UStar, R));
	}

	/**
	 * The corner primitive extended to signed arguments by the disc's own two-fold mirror symmetry, so a
	 * rectangle straddling either axis needs no case analysis: the usual 2D inclusion-exclusion of four
	 * corner terms then works over the whole plane.
	 */
	double DiscSignedCorner(double X, double Y, double R)
	{
		const double Magnitude = DiscCornerArea(FMath::Abs(X), FMath::Abs(Y), R);
		const double Sign = ((X < 0.0) ? -1.0 : 1.0) * ((Y < 0.0) ? -1.0 : 1.0);
		return Sign * Magnitude;
	}

	/** Area of the disc of radius R centred at the origin intersected with [X0,X1] x [Y0,Y1]. */
	double DiscAreaInRect(double X0, double X1, double Y0, double Y1, double R)
	{
		const double Area = DiscSignedCorner(X1, Y1, R) - DiscSignedCorner(X0, Y1, R)
		                  - DiscSignedCorner(X1, Y0, R) + DiscSignedCorner(X0, Y0, R);
		return FMath::Max(0.0, Area);
	}
}

int32 FTrajectoryField::AxisIndexFromCoord(double G, double Dir, int32 N)
{
	const double Floored = FMath::FloorToDouble(G);

	// Positive direction: the cell being entered is the one whose half-open span [i, i+1) contains G,
	// i.e. floor(G). Zero or negative direction: the cell being entered (or occupied, for Dir == 0) is
	// the one on the LOWER side of a grid line, i.e. ceil(G) - 1. The two agree unless G is integral.
	int32 Index;
	if (Dir > 0.0)
	{
		Index = static_cast<int32>(Floored);
	}
	else
	{
		Index = (G == Floored) ? (static_cast<int32>(Floored) - 1) : static_cast<int32>(Floored);
	}

	return FMath::Clamp(Index, 0, N - 1);
}

bool FTrajectoryField::ClipAxis(double G0, double D, int32 N, double& TMin, double& TMax)
{
	const double Hi = static_cast<double>(N);

	if (D == 0.0)
	{
		// Parallel to this axis' slab: either always inside it or never. Bounds are CLOSED, so a segment
		// running exactly along g == 0 or g == N survives (the lower-index-owns rule then places it).
		return (G0 >= 0.0) && (G0 <= Hi);
	}

	double T1 = (0.0 - G0) / D;
	double T2 = (Hi - G0) / D;
	if (T1 > T2)
	{
		Swap(T1, T2);
	}

	TMin = FMath::Max(TMin, T1);
	TMax = FMath::Min(TMax, T2);
	return TMin <= TMax;
}

FIntPoint FTrajectoryField::DimsForExtent(double ExtentXCm, double ExtentYCm, float CmPerTexel)
{
	const double Cm = static_cast<double>(CmPerTexel);
	if (Cm <= 0.0)
	{
		return FIntPoint::ZeroValue;
	}

	// NPOT is fine: no mips, TA_Clamp, uncompressed format. Never stretch to a fixed dimension.
	// The clamp is only so an absurd extent cannot overflow the int32 cast into a negative dimension,
	// which would then silently pass the MaxGridDim test below.
	const double Ceiling = 1.0e9;
	const double DimX = FMath::Clamp(FMath::CeilToDouble(FMath::Max(0.0, ExtentXCm) / Cm), 0.0, Ceiling);
	const double DimY = FMath::Clamp(FMath::CeilToDouble(FMath::Max(0.0, ExtentYCm) / Cm), 0.0, Ceiling);
	return FIntPoint(static_cast<int32>(DimX), static_cast<int32>(DimY));
}

void FTrajectoryField::Initialise(const FVector2D& FloorExtentCm, const FVector2D& FloorOriginCm,
                                 const FTrajectoryFieldConfig& InConfig)
{
	Config = InConfig;
	Config.WorldCmPerTexel = FMath::Max(Config.WorldCmPerTexel, MinCmPerTexel);
	Config.DisplayPathWidthCm = FMath::Max(Config.DisplayPathWidthCm, 0.0f);
	Config.MaxGridDim = FMath::Clamp(Config.MaxGridDim, 1, AbsoluteMaxGridDim);
	Config.MaxPlausibleSpeedCmPerSec = FMath::Max(Config.MaxPlausibleSpeedCmPerSec, 0.0f);
	Config.MaxPlausibleDeltaSeconds = FMath::Max(Config.MaxPlausibleDeltaSeconds, 0.0f);
	// A non-positive reference is treated as "auto-expose" by the encode rather than producing an
	// infinity, so clamping at zero is enough here.
	Config.ReferenceUsageDensity = FMath::Max(Config.ReferenceUsageDensity, 0.0f);
	Config.ReferenceExposureDensity = FMath::Max(Config.ReferenceExposureDensity, 0.0f);

	OriginCm = FloorOriginCm;

	const double ExtX = FMath::Max(0.0, static_cast<double>(FloorExtentCm.X));
	const double ExtY = FMath::Max(0.0, static_cast<double>(FloorExtentCm.Y));

	float Cm = Config.WorldCmPerTexel;
	FIntPoint Dims = FIntPoint::ZeroValue;
	ResolveGrid(ExtX, ExtY, Config, Cm, Dims);

	// D-C: additive padding, applied only here so it cannot reach ResolveGrid's decision. The visualizer
	// asks for one extra cell on whichever axis it re-phases, because the origin shift it applies can
	// leave the grid up to half a cell short of the mesh at one edge and this codebase drops off-grid
	// deposits rather than clamping them onto the border row.
	Dims.X = FMath::Max(0, Dims.X + FMath::Max(0, Config.ExtraGridCells.X));
	Dims.Y = FMath::Max(0, Dims.Y + FMath::Max(0, Config.ExtraGridCells.Y));

	GridDims = Dims;
	EffectiveCmPerTexel = Cm;
	InvEffectiveCmPerTexel = (Cm > 0.0f) ? (1.0 / static_cast<double>(Cm)) : 0.0;

	const float CellSideMetres = Cm * 0.01f;
	CellAreaSquareMetres = CellSideMetres * CellSideMetres;

	const int32 NumCells = GridDims.X * GridDims.Y;
	PersonMetres.Reset();
	PersonSeconds.Reset();
	Presentation.Reset();
	if (NumCells > 0)
	{
		PersonMetres.SetNumZeroed(NumCells);
		PersonSeconds.SetNumZeroed(NumCells);
		Presentation.SetNumZeroed(NumCells);
	}

	BuildKernel();

	// A re-initialise on floor change that kept stale mass would be a silent bug.
	Clear();
}

void FTrajectoryField::ResolveGrid(double ExtentXCm, double ExtentYCm, const FTrajectoryFieldConfig& InConfig,
                                   float& OutCmPerTexel, FIntPoint& OutDims)
{
	const double ExtX = FMath::Max(0.0, ExtentXCm);
	const double ExtY = FMath::Max(0.0, ExtentYCm);

	FTrajectoryFieldConfig Config = InConfig;
	Config.WorldCmPerTexel = FMath::Max(Config.WorldCmPerTexel, MinCmPerTexel);
	Config.MaxGridDim = FMath::Clamp(Config.MaxGridDim, 1, AbsoluteMaxGridDim);

	// D2 then D2b: honour cm/texel if it fits, otherwise RAISE it to the smallest value that does. The
	// division is done in float because the reported EffectiveCmPerTexel is float and the grid must be
	// consistent with the value the caller can read back. The bounded nudge loop only fires if the exact
	// quotient rounds such that ceil() still overshoots by one - at 25000 cm / 2048 it does not
	// (3125/256 is binary-exact), but a non-round extent can.
	float Cm = Config.WorldCmPerTexel;
	FIntPoint Dims = DimsForExtent(ExtX, ExtY, Cm);
	if (FMath::Max(Dims.X, Dims.Y) > Config.MaxGridDim)
	{
		const double MaxExtent = FMath::Max(ExtX, ExtY);
		Cm = static_cast<float>(MaxExtent / static_cast<double>(Config.MaxGridDim));
		Cm = FMath::Max(Cm, MinCmPerTexel);
		Dims = DimsForExtent(ExtX, ExtY, Cm);

		for (int32 Guard = 0; Guard < 16 && FMath::Max(Dims.X, Dims.Y) > Config.MaxGridDim; ++Guard)
		{
			Cm *= (1.0f + 1.0e-6f);
			Dims = DimsForExtent(ExtX, ExtY, Cm);
		}
	}

	// D-A / 2026-08-10 — SNAP cm/texel so the MAJOR axis divides its extent EVENLY.
	//
	// DimsForExtent ceil()s, so W texels represent W*Cm cm of world while the mesh spans only ExtX. The
	// render maps mesh extent onto the FULL texture (BuildTileBuffers letterboxes on the world-extent
	// ratio ExtY/ExtX) while the texel offset is computed from the CEIL'D grid-dims ratio H/W. Two
	// different numbers, so the drawn stroke is displaced toward the floor's minimum-XY corner by an error
	// that GROWS with distance from it. Measured on the real 4548.9 x 3977.4 cm floor at 45 cm/texel:
	// 18.3 cm at the min corner, 41.7 cm mid-floor, 68.1 cm at the far corner = 151% of a cell. Across
	// 2000 random floors the median worst case is 40 cm and 37% exceed a full cell, so this is structural
	// rather than a bad-luck extent. It is what "the path lands beside the agent, not under it" was.
	//
	// Dividing the major extent by its already-ceil'd dimension makes that axis exact by construction and
	// removes the scale term outright. The MINOR axis may gain one texel from the slightly smaller Cm;
	// that is only letterbox margin, which the offset rounding at the call site bounds to half a texel.
	// It can never overtake the major axis: MinorExt <= MajorExt and ceil() is monotonic, so
	// ceil(MinorExt/Cm) <= ceil(MajorExt/Cm) == MajorDim.
	//
	// Cm falling BELOW the requested value is the whole point, so this runs after the MaxGridDim raise
	// above and is deliberately not re-clamped against Config.WorldCmPerTexel. It only ever decreases Cm,
	// so the D2b ceiling cannot be breached except by float rounding, which the guard below absorbs.
	const double MajorExt = FMath::Max(ExtX, ExtY);
	const int32 MajorDim = FMath::Max(Dims.X, Dims.Y);
	if (MajorDim > 0 && MajorExt > 0.0)
	{
		const float SnappedCm = static_cast<float>(MajorExt / static_cast<double>(MajorDim));
		// Below the floor the snap would have to be clamped back UP, which would re-grow the grid it was
		// meant to leave alone. A sub-micron cell is degenerate anyway; leave Cm as it was.
		if (SnappedCm >= MinCmPerTexel)
		{
			Cm = SnappedCm;
			Dims = DimsForExtent(ExtX, ExtY, Cm);

			// The double -> float cast can land a hair below the exact quotient, which turns ceil() into
			// MajorDim + 1. Same bounded nudge as the D2b block above, and for the same reason.
			for (int32 Guard = 0; Guard < 16 && FMath::Max(Dims.X, Dims.Y) > MajorDim; ++Guard)
			{
				Cm *= (1.0f + 1.0e-6f);
				Dims = DimsForExtent(ExtX, ExtY, Cm);
			}
		}
	}

	OutCmPerTexel = Cm;
	OutDims = Dims;
}

void FTrajectoryField::MarkCellDirty(int32 I, int32 J) const
{
	const int32 Half = (PhaseKernelOffsets.Num() > 0) ? PhaseKernelOffsets.Last().X : 0;
	const int32 X0 = FMath::Clamp(I - Half, 0, GridDims.X);
	const int32 Y0 = FMath::Clamp(J - Half, 0, GridDims.Y);
	const int32 X1 = FMath::Clamp(I + Half + 1, 0, GridDims.X);
	const int32 Y1 = FMath::Clamp(J + Half + 1, 0, GridDims.Y);

	if (DirtyRect.Min.X >= DirtyRect.Max.X || DirtyRect.Min.Y >= DirtyRect.Max.Y)
	{
		DirtyRect = FIntRect(X0, Y0, X1, Y1);
		return;
	}
	// A bounding box, not a tile set. Deliberate: a crowd's deposits are spatially coherent within a tick,
	// and a box costs four compares per deposit against a tile set's hashing. If a capture ever has two
	// crowds at opposite corners the box degenerates to the full grid, which is exactly today's cost and
	// so cannot be a regression.
	DirtyRect.Min.X = FMath::Min(DirtyRect.Min.X, X0);
	DirtyRect.Min.Y = FMath::Min(DirtyRect.Min.Y, Y0);
	DirtyRect.Max.X = FMath::Max(DirtyRect.Max.X, X1);
	DirtyRect.Max.Y = FMath::Max(DirtyRect.Max.Y, Y1);
}

float FTrajectoryField::DeriveRouteThresholdCrossings(float DisplayPathWidthCm, float CmPerTexel)
{
	// Legacy behaviour for a degenerate configuration: "anything nonzero is on the route". Half a byte is
	// the smallest positive an encoded cell can hold, so this is the is-it-zero test the threshold
	// replaces — the right answer when there is no kernel to reason about.
	constexpr float NoDataOnlyCrossings = 0.0f;
	if (!(DisplayPathWidthCm > 0.0f) || !(CmPerTexel > 0.0f))
	{
		return NoDataOnlyCrossings;
	}

	const double Radius = static_cast<double>(DisplayPathWidthCm) / (2.0 * static_cast<double>(CmPerTexel));
	const double WidthInCells = static_cast<double>(DisplayPathWidthCm) / static_cast<double>(CmPerTexel);

	// The nearest honest whole-cell rendering of the stroke. At 45/15 this is 3 and the width comes out
	// exactly right; at 45/20 it is 2, i.e. 40 cm — wrong by 5 cm but STABLE, which the 40-or-60 flicker
	// was not.
	const int32 TargetRows = FMath::Max(1, FMath::RoundToInt32(WidthInCells));

	// Same footprint rule as BuildPhaseKernel: a disc pushed half a cell off centre reaches one ring more.
	const int32 HalfExtent = FMath::Max(1, static_cast<int32>(FMath::CeilToDouble(Radius + 0.5)));
	constexpr int32 Bins = KernelPhaseBinsPerAxis;

	// Window: everything the stroke must KEEP has to sit above everything it must DROP, at every phase.
	double WorstDropped = 0.0;          // highest value that must NOT light
	double WorstKept = TNumericLimits<double>::Max(); // lowest value that MUST light

	TArray<double> RowCrossings;
	RowCrossings.Reserve(2 * HalfExtent + 1);

	for (int32 Bin = 0; Bin < Bins; ++Bin)
	{
		// Only the ACROSS-path phase matters: the marginal sums along the path, so the along-path phase
		// cancels. One sweep of Bins instead of Bins^2.
		const double Phase = (static_cast<double>(Bin) + 0.5) / Bins - 0.5;

		RowCrossings.Reset();
		double RawTotal = 0.0;
		for (int32 DY = -HalfExtent; DY <= HalfExtent; ++DY)
		{
			// Marginal of the disc in this row: the full-width strip, which is exactly the sum over the
			// row's cells of the per-cell areas BuildPhaseKernel computes.
			const double Area = TrajectoryFieldPrivate::DiscAreaInRect(
				-Radius - 1.0, Radius + 1.0,
				static_cast<double>(DY) - 0.5 - Phase, static_cast<double>(DY) + 0.5 - Phase, Radius);
			RowCrossings.Add(Area);
			RawTotal += Area;
		}
		if (!(RawTotal > 0.0))
		{
			continue;
		}

		// Normalise the marginal to 1.0 (as the kernel is), then convert to crossings. The cell area and
		// the reference density cancel out of that conversion, which is why this needs neither.
		for (double& Value : RowCrossings)
		{
			Value = (Value / RawTotal) * WidthInCells;
		}

		// Descending, so element [TargetRows - 1] is the dimmest row that must light and [TargetRows] is
		// the brightest that must not.
		RowCrossings.Sort([](const double& A, const double& B) { return A > B; });

		WorstKept = FMath::Min(WorstKept, RowCrossings[FMath::Min(TargetRows - 1, RowCrossings.Num() - 1)]);
		if (TargetRows < RowCrossings.Num())
		{
			WorstDropped = FMath::Max(WorstDropped, RowCrossings[TargetRows]);
		}
	}

	if (WorstKept == TNumericLimits<double>::Max())
	{
		return NoDataOnlyCrossings;
	}

	if (WorstDropped < WorstKept)
	{
		// A real window exists: the width is constant at TargetRows cells across every phase. Midpoint,
		// so float noise in either bound cannot tip a row across.
		return static_cast<float>(0.5 * (WorstDropped + WorstKept));
	}

	// No window — the kept and dropped populations overlap, so SOME phase must render a row wide or narrow
	// whatever we choose. Sit on the dimmest kept row: that keeps the stroke from ever being narrower than
	// TargetRows, and lets it widen only on the phases that genuinely cannot be resolved. Widening is the
	// safer failure for a route surface, since a missing cell reads as "nobody walked here".
	return static_cast<float>(WorstKept);
}

void FTrajectoryField::BuildKernel()
{
	KernelOffsets.Reset();
	KernelWeights.Reset();
	KernelCentreIndex = INDEX_NONE;

	KernelRadiusTexels = (EffectiveCmPerTexel > 0.0f)
		? (Config.DisplayPathWidthCm / (2.0f * EffectiveCmPerTexel))
		: 0.0f;
	KernelRadiusTexels = FMath::Max(0.0f, KernelRadiusTexels);

	// ---------------------------------------------------------------------------------------------
	// SHAPE RULE - normalised disc AREA COVERAGE (PRD D3 / section 4.3 "normalised disc/capsule
	// coverage"). The weight of a neighbour is the fraction of the disc's area that falls inside that
	// neighbour's cell:
	//
	//     weight(dx,dy) = Area( disc(R) intersect cell(dx,dy) ) / (pi * R^2)
	//
	// Computed analytically (TrajectoryFieldPrivate::DiscAreaInRect), not sampled, so the weights are
	// reproducible by hand to full double precision and the raw areas sum to pi*R^2 by construction.
	//
	// This replaces an earlier centre-MEMBERSHIP rule (dx^2 + dy^2 <= R^2, all members weighted equally),
	// which at R = 1.0 yields 5 equal taps instead of these 9. Both rules normalise to 1.0, so
	// Sum(Presentation) == Sum(Canonical) - i.e. AC3 - holds under either and could not discriminate
	// them; the independently hand-derived oracle could. Coverage is an area measure and is what
	// "stamping a disc of DisplayPathWidthCm along the path" actually produces, so it is the rule.
	//
	// Reference values at R = 1.0 texel (the default: 20 cm width at 10 cm/texel), each exact:
	//     centre  1/pi                        = 0.318309886
	//     edge   (pi/6 + sqrt3/4 - 1/2)/pi    = 0.145343947   x4
	//     corner (pi/12 - sqrt3/4 + 1/4)/pi   = 0.025078581   x4
	// and R <= 0.5 texel collapses the table to the identity kernel exactly.
	// ---------------------------------------------------------------------------------------------
	const double Radius = static_cast<double>(KernelRadiusTexels);

	// Cell (dx,dy) spans [dx-0.5, dx+0.5], so it can only hold disc area when dx - 0.5 < R, i.e.
	// dx < R + 0.5. The half extent is therefore ceil(R - 0.5), NOT floor(R): at R = 1.6 floor() would
	// crop the outermost partial ring. Mass would still be conserved (the renormalisation below absorbs
	// it) but the rendered stroke would be narrower than DisplayPathWidthCm asks for, which is a silent
	// violation of FR3 rather than a visible failure.
	KernelHalfExtent = FMath::Max(0, static_cast<int32>(FMath::CeilToDouble(Radius - 0.5)));

	// Never let the kernel span more than the grid; the edge renormalisation assumes at least the centre
	// tap is in bounds, which it always is, but an absurd width on a tiny grid would waste the whole walk.
	const int32 MaxHalf = FMath::Max(0, FMath::Min(GridDims.X, GridDims.Y) / 2);
	KernelHalfExtent = FMath::Clamp(KernelHalfExtent, 0, MaxHalf);

	// Areas are collected in double and normalised once, rather than stored as float and scaled in place:
	// two float roundings per weight would eat most of the 1e-6 relative tolerance the oracle weights are
	// stated to.
	TArray<double> TapAreas;
	double RawSum = 0.0;
	for (int32 DY = -KernelHalfExtent; DY <= KernelHalfExtent; ++DY)
	{
		for (int32 DX = -KernelHalfExtent; DX <= KernelHalfExtent; ++DX)
		{
			const double Area = TrajectoryFieldPrivate::DiscAreaInRect(
				static_cast<double>(DX) - 0.5, static_cast<double>(DX) + 0.5,
				static_cast<double>(DY) - 0.5, static_cast<double>(DY) + 0.5, Radius);

			// Strictly greater than zero: a cell the disc only touches at a single point (dx = 3 at
			// R = 2.5) contributes no area and must not become a zero-weight tap the splat loop pays for.
			if (Area > 0.0)
			{
				if (DX == 0 && DY == 0)
				{
					KernelCentreIndex = KernelOffsets.Num();
				}
				KernelOffsets.Add(FIntPoint(DX, DY));
				KernelWeights.Add(0.0f);
				TapAreas.Add(Area);
				RawSum += Area;
			}
		}
	}

	// R == 0 (DisplayPathWidthCm == 0) is the only case that produces no tap at all, since a
	// zero-radius disc has no area. Fall back to the identity kernel, which is the correct limit:
	// presentation then equals canonical.
	if (RawSum <= 0.0 || KernelCentreIndex == INDEX_NONE)
	{
		KernelOffsets.Reset();
		KernelWeights.Reset();
		KernelOffsets.Add(FIntPoint(0, 0));
		KernelWeights.Add(1.0f);
		KernelCentreIndex = 0;
		return;
	}

	const double InvRawSum = 1.0 / RawSum;
	for (int32 Tap = 0; Tap < KernelWeights.Num(); ++Tap)
	{
		KernelWeights[Tap] = static_cast<float>(TapAreas[Tap] * InvRawSum);
	}

	// Fold the quantisation residual into the centre tap so the table sums to 1.0 to within a single
	// float ULP. This is the guard against the kernel silently multiplying or losing mass: the invariant
	// Sum(Presentation) == Sum(Canonical) is only assertable because of it.
	double WeightSum = 0.0;
	for (const float Weight : KernelWeights)
	{
		WeightSum += static_cast<double>(Weight);
	}
	KernelWeights[KernelCentreIndex] += static_cast<float>(1.0 - WeightSum);

	BuildPhaseKernel(Radius);
}

void FTrajectoryField::BuildPhaseKernel(double Radius)
{
	// D-D — the same analytic disc-area coverage as above, evaluated at every sub-cell phase instead of
	// only at the cell centre. See the PhaseKernel* declarations for why this exists at all.
	PhaseKernelOffsets.Reset();
	PhaseKernelWeights.Reset();
	PhaseKernelTapCount = 0;

	constexpr int32 Bins = KernelPhaseBinsPerAxis;

	// A disc displaced by up to half a cell reaches one ring further than a centred one: cell dx holds
	// area when dx - 0.5 < R + 0.5, i.e. dx < R + 1. Half extent ceil(R + 0.5), against the centred
	// table's ceil(R - 0.5). Sizing to the CENTRED extent instead would clip the leading edge of every
	// off-centre phase — mass-conserving because of the renormalise below, so it would not fail any
	// conservation gate, but the stroke would narrow as it moved off centre. A moving-width stroke is
	// exactly the kind of fault that reads as "the renderer is fine, the data is odd".
	int32 HalfExtent = FMath::Max(0, static_cast<int32>(FMath::CeilToDouble(Radius + 0.5)));
	const int32 MaxHalf = FMath::Max(0, FMath::Min(GridDims.X, GridDims.Y) / 2);
	HalfExtent = FMath::Clamp(HalfExtent, 0, MaxHalf);

	for (int32 DY = -HalfExtent; DY <= HalfExtent; ++DY)
	{
		for (int32 DX = -HalfExtent; DX <= HalfExtent; ++DX)
		{
			PhaseKernelOffsets.Add(FIntPoint(DX, DY));
		}
	}
	PhaseKernelTapCount = PhaseKernelOffsets.Num();

	// R == 0 has no area at any phase. Identity kernel is the correct limit, as for the centred table.
	if (Radius <= 0.0 || PhaseKernelTapCount == 0)
	{
		PhaseKernelOffsets.Reset();
		PhaseKernelOffsets.Add(FIntPoint(0, 0));
		PhaseKernelTapCount = 1;
		PhaseKernelWeights.Init(1.0f, Bins * Bins);
		return;
	}

	// The footprint is shared across phases and every tap is stored, including the zeros an outer-ring
	// cell has at phases that lean away from it. Storing a per-phase compacted list would save a handful
	// of multiply-adds and cost a per-deposit indirection plus a second offsets table; at 25 taps that
	// trade is not worth the second way for the two tables to disagree.
	PhaseKernelWeights.SetNumZeroed(Bins * Bins * PhaseKernelTapCount);

	for (int32 BinY = 0; BinY < Bins; ++BinY)
	{
		for (int32 BinX = 0; BinX < Bins; ++BinX)
		{
			// Bin centres, so the worst placement error is half a bin rather than a whole one, and bin
			// (Bins/2) is NOT exactly zero — deliberate. Sampling bin EDGES would put one phase exactly on
			// the cell centre and make a cell-centred deposit a special case that behaves differently from
			// its neighbours by a whole bin.
			const double PhaseX = (static_cast<double>(BinX) + 0.5) / Bins - 0.5;
			const double PhaseY = (static_cast<double>(BinY) + 0.5) / Bins - 0.5;

			const int32 Base = (BinY * Bins + BinX) * PhaseKernelTapCount;

			double RawSum = 0.0;
			for (int32 Tap = 0; Tap < PhaseKernelTapCount; ++Tap)
			{
				const FIntPoint& Offset = PhaseKernelOffsets[Tap];
				// Shift the RECT by -Phase rather than the disc by +Phase: DiscAreaInRect centres the disc
				// at the origin, and the two are the same integral.
				const double Area = TrajectoryFieldPrivate::DiscAreaInRect(
					static_cast<double>(Offset.X) - 0.5 - PhaseX, static_cast<double>(Offset.X) + 0.5 - PhaseX,
					static_cast<double>(Offset.Y) - 0.5 - PhaseY, static_cast<double>(Offset.Y) + 0.5 - PhaseY,
					Radius);
				PhaseKernelWeights[Base + Tap] = static_cast<float>(Area);
				RawSum += Area;
			}

			if (RawSum <= 0.0)
			{
				// Cannot happen for R > 0 with this footprint, but a zero row would silently delete mass
				// rather than misplace it, so fall back to the centre tap instead of dividing by zero.
				const int32 Centre = PhaseKernelTapCount / 2;
				FMemory::Memzero(&PhaseKernelWeights[Base], PhaseKernelTapCount * sizeof(float));
				PhaseKernelWeights[Base + Centre] = 1.0f;
				continue;
			}

			// Normalise, then fold the residual into the LARGEST tap. The centred table folds into the
			// centre tap, which is safe there because the centre always dominates; off centre it may not,
			// and adding a residual to a near-zero outer tap is how a faint halo appears at the stroke's
			// edge.
			const double InvRawSum = 1.0 / RawSum;
			double Sum = 0.0;
			int32 LargestTap = 0;
			for (int32 Tap = 0; Tap < PhaseKernelTapCount; ++Tap)
			{
				const float W = static_cast<float>(static_cast<double>(PhaseKernelWeights[Base + Tap]) * InvRawSum);
				PhaseKernelWeights[Base + Tap] = W;
				Sum += static_cast<double>(W);
				if (W > PhaseKernelWeights[Base + LargestTap])
				{
					LargestTap = Tap;
				}
			}
			PhaseKernelWeights[Base + LargestTap] += static_cast<float>(1.0 - Sum);
		}
	}
}

void FTrajectoryField::Clear()
{
	if (PersonMetres.Num() > 0)
	{
		FMemory::Memzero(PersonMetres.GetData(), PersonMetres.Num() * sizeof(float));
	}
	if (PersonSeconds.Num() > 0)
	{
		FMemory::Memzero(PersonSeconds.GetData(), PersonSeconds.Num() * sizeof(float));
	}
	if (Presentation.Num() > 0)
	{
		FMemory::Memzero(Presentation.GetData(), Presentation.Num() * sizeof(float));
	}

	// Canonical and presentation are both empty, so the cache is consistent - not dirty.
	bPresentationDirty = false;
	// D-F: a clear is the one thing that LOWERS the presentation, which is exactly what the monotonic
	// running maximum cannot survive. Reset it and repaint everything.
	RunningMaxPresentation = 0.0f;
	MarkAllDirty();
	LastEncodeScale = 0.0f;
	LastEncodeMaxDensity = 0.0f;
	LastEncodeReferenceDensity = 0.0f;
	bLastEncodeWasAutoExposed = false;
	LastEncodeSaturatedCells = 0;

	TotalPersonMetres = 0.0;
	TotalPersonSeconds = 0.0;
	DroppedPersonMetres = 0.0;
	DroppedPersonSeconds = 0.0;
	RejectedPersonMetres = 0.0;
	RejectedPersonSeconds = 0.0;
	NegligiblePersonMetres = 0.0;

	RejectedSegmentCount = 0;
	RejectedNonFiniteCount = 0;
	RejectedNonPositiveDeltaCount = 0;
	RejectedDeltaTooLargeCount = 0;
	RejectedTeleportCount = 0;
	RejectedNotInitialisedCount = 0;
	StationarySegmentCount = 0;
	FullyClippedSegmentCount = 0;
}

void FTrajectoryField::SetPresentationMode(ETrajectoryMapMode Mode)
{
	if (Mode != PresentationMode)
	{
		PresentationMode = Mode;
		bPresentationDirty = true;
	}
}

void FTrajectoryField::SetDisplayPathWidthCm(float WidthCm)
{
	const float Clamped = FMath::Max(0.0f, WidthCm);
	if (Clamped == Config.DisplayPathWidthCm)
	{
		return;
	}

	Config.DisplayPathWidthCm = Clamped;
	BuildKernel();

	// Canonical is untouched; only the splatted copy has to be redone.
	bPresentationDirty = true;
}

const TArray<float>& FTrajectoryField::GetCanonical(ETrajectoryMapMode Mode) const
{
	return (Mode == ETrajectoryMapMode::RouteUsage) ? PersonMetres : PersonSeconds;
}

const TArray<float>& FTrajectoryField::GetPresentation() const
{
	// Routed through the same lazy rebuild as EncodeToDisplay: reading this without encoding first must
	// not hand back the previous mode's splat.
	EnsurePresentation(PresentationMode);
	return Presentation;
}

const TArray<float>& FTrajectoryField::GetPresentation(ETrajectoryMapMode Mode) const
{
	EnsurePresentation(Mode);
	return Presentation;
}

void FTrajectoryField::EnsurePresentation(ETrajectoryMapMode Mode) const
{
	if (bPresentationDirty || Mode != PresentationMode)
	{
		PresentationMode = Mode;
		RebuildPresentation(Mode);
	}
}

void FTrajectoryField::RebuildPresentation(ETrajectoryMapMode Mode) const
{
	bPresentationDirty = false;

	if (Presentation.Num() <= 0)
	{
		return;
	}

	FMemory::Memzero(Presentation.GetData(), Presentation.Num() * sizeof(float));

	// D-F: a rebuild replaces every presentation value and can LOWER any of them — the two modes have
	// entirely different magnitudes — which is precisely what a MONOTONIC running maximum cannot survive.
	// Reset it and repaint the whole grid. Const because the presentation cache is a mutable cache; both
	// members are mutable for the same reason.
	RunningMaxPresentation = 0.0f;
	MarkAllDirty();

	const TArray<float>& Source = GetCanonical(Mode);
	const int32 Width = GridDims.X;
	const int32 Height = GridDims.Y;

	// The splat is linear in the deposited value and its edge renormalisation depends only on the cell
	// index, so splatting the accumulated per-cell total here gives the same field the incremental path
	// inside the DDA would have produced, up to float rounding.
	for (int32 J = 0; J < Height; ++J)
	{
		const int32 RowBase = J * Width;
		for (int32 I = 0; I < Width; ++I)
		{
			const float Value = Source[RowBase + I];
			if (Value != 0.0f)
			{
				// 🚩 D-D LIMITATION, and the one place sub-cell placement cannot be recovered. A rebuild
				// reads the CANONICAL arrays, which are cell-resolution by design — the sub-cell position
				// each deposit carried is not stored anywhere, so this can only stamp on the cell centre.
				//
				// Consequence: the incremental picture (sub-cell placed) and a rebuilt one (cell-centred)
				// are not identical, and a mode switch rebuilds. Expect the surface to shift by up to half
				// a cell when toggling Route Usage <-> Route Exposure.
				// T_SEM_3_ModeSwitchInvariant is the gate that will say so.
				//
				// The fix, if that shift is unacceptable, is two more float arrays holding the mass-weighted
				// sub-cell centroid per cell, so a rebuild can restore placement exactly. Deliberately NOT
				// done on a guess: it is +67% field memory, which at the 8192 grid ceiling is hundreds of
				// megabytes, to remove an artefact only visible on a mode toggle. Owner call.
				SplatInto(Presentation, I, J, static_cast<double>(Value), 0.0, 0.0);
			}
		}
	}
}

void FTrajectoryField::SplatInto(TArray<float>& Target, int32 I, int32 J, double Value,
                                 double SubCellX, double SubCellY) const
{
	const int32 Width = GridDims.X;
	const int32 Height = GridDims.Y;

	// D-D: select the sub-cell phase. SubCell is measured from the cell CENTRE and lives in [-0.5, +0.5];
	// a caller that has no sub-cell information passes (0,0) and lands on the middle bin, which is the
	// cell-centred stamp this used to do unconditionally.
	constexpr int32 Bins = KernelPhaseBinsPerAxis;
	const int32 BinX = FMath::Clamp(
		static_cast<int32>(FMath::FloorToDouble((SubCellX + 0.5) * Bins)), 0, Bins - 1);
	const int32 BinY = FMath::Clamp(
		static_cast<int32>(FMath::FloorToDouble((SubCellY + 0.5) * Bins)), 0, Bins - 1);

	const int32 NumTaps = PhaseKernelTapCount;
	if (NumTaps <= 0)
	{
		return;
	}
	const float* const Weights = &PhaseKernelWeights[(BinY * Bins + BinX) * NumTaps];
	const FIntPoint* const Offsets = PhaseKernelOffsets.GetData();

	// The phase footprint is one ring wider than the centred one, so it needs its own bounds test —
	// reusing KernelHalfExtent here would take the fast path while writing outside the array.
	const int32 HalfExtent = PhaseKernelOffsets.Last().X;

	// Fast path: the whole footprint is interior, so the precomputed weights apply verbatim.
	if (I - HalfExtent >= 0 && I + HalfExtent < Width &&
	    J - HalfExtent >= 0 && J + HalfExtent < Height)
	{
		for (int32 Tap = 0; Tap < NumTaps; ++Tap)
		{
			const float W = Weights[Tap];
			if (W == 0.0f)
			{
				continue; // Outer-ring taps are zero at phases leaning away from them.
			}
			const FIntPoint& Offset = Offsets[Tap];
			Target[(J + Offset.Y) * Width + (I + Offset.X)] +=
				static_cast<float>(Value * static_cast<double>(W));
		}
		return;
	}

	// Edge: renormalise over the in-bounds taps so the kernel neither loses mass off the border nor
	// piles it onto the border row. Discarding the out-of-bounds taps instead would break the
	// Sum(Presentation) == Sum(Canonical) invariant for any dataset that touches an edge, which is most
	// of them. The centre tap is always in bounds, so the divisor is never zero.
	double InBoundsWeight = 0.0;
	for (int32 Tap = 0; Tap < NumTaps; ++Tap)
	{
		const FIntPoint& Offset = Offsets[Tap];
		const int32 X = I + Offset.X;
		const int32 Y = J + Offset.Y;
		if (X >= 0 && X < Width && Y >= 0 && Y < Height)
		{
			InBoundsWeight += static_cast<double>(Weights[Tap]);
		}
	}
	if (InBoundsWeight <= 0.0)
	{
		return;
	}

	const double Renormalise = 1.0 / InBoundsWeight;
	for (int32 Tap = 0; Tap < NumTaps; ++Tap)
	{
		const FIntPoint& Offset = Offsets[Tap];
		const int32 X = I + Offset.X;
		const int32 Y = J + Offset.Y;
		if (X >= 0 && X < Width && Y >= 0 && Y < Height)
		{
			Target[Y * Width + X] +=
				static_cast<float>(Value * static_cast<double>(Weights[Tap]) * Renormalise);
		}
	}
}

bool FTrajectoryField::ContainingCell(const FVector2D& WorldCm, FIntPoint& OutCell) const
{
	const double GX = (static_cast<double>(WorldCm.X) - static_cast<double>(OriginCm.X)) * InvEffectiveCmPerTexel;
	const double GY = (static_cast<double>(WorldCm.Y) - static_cast<double>(OriginCm.Y)) * InvEffectiveCmPerTexel;

	if (GX < 0.0 || GX > static_cast<double>(GridDims.X) ||
	    GY < 0.0 || GY > static_cast<double>(GridDims.Y))
	{
		return false;
	}

	// Zero direction on both axes: the lower-index cell owns a grid line, matching the DDA.
	OutCell.X = AxisIndexFromCoord(GX, 0.0, GridDims.X);
	OutCell.Y = AxisIndexFromCoord(GY, 0.0, GridDims.Y);
	return true;
}

void FTrajectoryField::DepositCell(int32 I, int32 J, double AddPersonMetres, double AddPersonSeconds,
                                   double SubCellX, double SubCellY)
{
	// CANONICAL IS CELL-EXACT AND STAYS THAT WAY. The sub-cell position deliberately does not reach these
	// two arrays: they are the measurement, the DDA already apportioned this segment's length and time to
	// this cell exactly, and smearing them across neighbours would make the analysis quantity depend on a
	// display kernel. Only the PRESENTATION is placed sub-cell.
	const int32 Index = J * GridDims.X + I;
	PersonMetres[Index] += static_cast<float>(AddPersonMetres);
	PersonSeconds[Index] += static_cast<float>(AddPersonSeconds);

	// D-F: mark BEFORE the splat, and mark the whole footprint rather than the cell — the kernel writes a
	// ring of neighbours, and a dirty rect that covered only the centre would leave the outermost ring of
	// every stroke un-uploaded. That is a stale-texture bug the conservation gates cannot see, because the
	// field itself would be perfectly correct.
	MarkCellDirty(I, J);

	const double PresentationValue = (PresentationMode == ETrajectoryMapMode::RouteUsage)
		? AddPersonMetres
		: AddPersonSeconds;
	if (PresentationValue != 0.0)
	{
		SplatInto(Presentation, I, J, PresentationValue, SubCellX, SubCellY);
	}
}

void FTrajectoryField::RecordRejection(double LengthMetres, double DeltaSeconds, bool bAccountMass)
{
	++RejectedSegmentCount;
	if (bAccountMass)
	{
		// Signed, so the four-bucket identity closes exactly even for a negative delta-t.
		RejectedPersonMetres += LengthMetres;
		RejectedPersonSeconds += DeltaSeconds;
	}
}

void FTrajectoryField::DepositSegment(const FVector2D& StartCm, const FVector2D& EndCm, float DeltaSeconds)
{
	using namespace TrajectoryFieldPrivate;

	// ---- validity gates ------------------------------------------------------------------------
	const bool bEndpointsUsable = IsUsable2D(StartCm) && IsUsable2D(EndCm);
	if (!bEndpointsUsable || !IsUsable(static_cast<double>(DeltaSeconds)))
	{
		++RejectedNonFiniteCount;

		// A0 fix (A8 finding): when the ENDPOINTS are finite and only Delta-t is not, the length is a
		// perfectly good number and must still be booked, or a real offered metre lands in no bucket at all
		// and the four-bucket identity silently fails to close. Only a non-finite endpoint makes the length
		// itself meaningless, and that is the one case that legitimately contributes to no mass bucket.
		if (bEndpointsUsable)
		{
			RecordRejection(FVector2D::Distance(StartCm, EndCm) * 0.01, 0.0, true);
		}
		else
		{
			RecordRejection(0.0, 0.0, false);
		}
		return;
	}

	const double LengthCm = FVector2D::Distance(StartCm, EndCm);
	const double LengthMetres = LengthCm * 0.01;
	const double Delta = static_cast<double>(DeltaSeconds);

	if (!IsValid())
	{
		++RejectedNotInitialisedCount;
		RecordRejection(LengthMetres, Delta, true);
		return;
	}

	if (!(Delta > 0.0))
	{
		++RejectedNonPositiveDeltaCount;
		RecordRejection(LengthMetres, Delta, true);
		return;
	}

	if (Delta > static_cast<double>(Config.MaxPlausibleDeltaSeconds))
	{
		// Timeline scrub: the implied speed can be perfectly ordinary while the segment spans the
		// building. Delta-t is the only tell.
		++RejectedDeltaTooLargeCount;
		RecordRejection(LengthMetres, Delta, true);
		return;
	}

	// Multiply rather than divide: Delta is already known positive, and this keeps a zero max speed
	// meaning "reject everything that moves" instead of producing an infinity.
	if (LengthCm > static_cast<double>(Config.MaxPlausibleSpeedCmPerSec) * Delta)
	{
		++RejectedTeleportCount;
		RecordRejection(LengthMetres, Delta, true);
		return;
	}

	// A mode switch may be outstanding; the incremental splat below must not add to a stale field.
	if (bPresentationDirty)
	{
		RebuildPresentation(PresentationMode);
	}

	// ---- stationary agent ----------------------------------------------------------------------
	// Required behaviour, not an edge case: Route Exposure exists to show queues, and a queue is made
	// entirely of segments the old path threw away as degenerate.
	if (LengthMetres <= StationaryLengthMetres)
	{
		++StationarySegmentCount;
		NegligiblePersonMetres += LengthMetres;

		// Midpoint, not Start: the containing cell must be the same whichever way the segment is given,
		// and a 0.01 cm segment can straddle a cell boundary.
		const FVector2D MidCm = (StartCm + EndCm) * 0.5;

		FIntPoint Cell;
		if (ContainingCell(MidCm, Cell))
		{
			TotalPersonSeconds += Delta;
			// PersonMetres contribution is zero by contract, so RouteUsage gains nothing here.
			PersonSeconds[Cell.Y * GridDims.X + Cell.X] += static_cast<float>(Delta);
			if (PresentationMode == ETrajectoryMapMode::RouteExposure)
			{
				// D-D: a standing agent has a position too, and it is the one place sub-cell placement
				// matters MOST — a queue is a cluster of stationary agents, and rounding each to its cell
				// centre is what turns a queue into a chequerboard.
				const double GMidX = (static_cast<double>(MidCm.X) - static_cast<double>(OriginCm.X))
					* InvEffectiveCmPerTexel;
				const double GMidY = (static_cast<double>(MidCm.Y) - static_cast<double>(OriginCm.Y))
					* InvEffectiveCmPerTexel;
				SplatInto(Presentation, Cell.X, Cell.Y, Delta,
					GMidX - (static_cast<double>(Cell.X) + 0.5),
					GMidY - (static_cast<double>(Cell.Y) + 0.5));
			}
		}
		else
		{
			DroppedPersonSeconds += Delta;
		}
		return;
	}

	// ---- to grid space -------------------------------------------------------------------------
	const double G0X = (static_cast<double>(StartCm.X) - static_cast<double>(OriginCm.X)) * InvEffectiveCmPerTexel;
	const double G0Y = (static_cast<double>(StartCm.Y) - static_cast<double>(OriginCm.Y)) * InvEffectiveCmPerTexel;
	const double G1X = (static_cast<double>(EndCm.X) - static_cast<double>(OriginCm.X)) * InvEffectiveCmPerTexel;
	const double G1Y = (static_cast<double>(EndCm.Y) - static_cast<double>(OriginCm.Y)) * InvEffectiveCmPerTexel;
	const double DirX = G1X - G0X;
	const double DirY = G1Y - G0Y;

	// ---- clip, never clamp (D7) ----------------------------------------------------------------
	double TMin = 0.0;
	double TMax = 1.0;
	const bool bOverlaps = ClipAxis(G0X, DirX, GridDims.X, TMin, TMax)
	                    && ClipAxis(G0Y, DirY, GridDims.Y, TMin, TMax);

	const double Retained = bOverlaps ? (TMax - TMin) : 0.0;
	if (!(Retained > 0.0))
	{
		// No overlap, or a measure-zero graze of a corner or edge. All of it is dropped mass.
		++FullyClippedSegmentCount;
		DroppedPersonMetres += LengthMetres;
		DroppedPersonSeconds += Delta;
		return;
	}

	// Booked once, from the clip result, so the four-bucket identity is exact in double regardless of
	// how the per-cell float adds round.
	TotalPersonMetres += LengthMetres * Retained;
	TotalPersonSeconds += Delta * Retained;
	DroppedPersonMetres += LengthMetres * (1.0 - Retained);
	DroppedPersonSeconds += Delta * (1.0 - Retained);

	// ---- 2D DDA (Amanatides-Woo) ---------------------------------------------------------------
	const double EntryX = G0X + TMin * DirX;
	const double EntryY = G0Y + TMin * DirY;

	int32 I = AxisIndexFromCoord(EntryX, DirX, GridDims.X);
	int32 J = AxisIndexFromCoord(EntryY, DirY, GridDims.Y);

	const int32 StepX = (DirX > 0.0) ? 1 : ((DirX < 0.0) ? -1 : 0);
	const int32 StepY = (DirY > 0.0) ? 1 : ((DirY < 0.0) ? -1 : 0);
	const double InvDirX = (DirX != 0.0) ? (1.0 / DirX) : 0.0;
	const double InvDirY = (DirY != 0.0) ? (1.0 / DirY) : 0.0;

	const int32 Width = GridDims.X;
	const int32 Height = GridDims.Y;

	// A clipped segment can cross at most W + H cell boundaries. The cap only exists so no floating
	// point pathology can spin here forever.
	const int32 MaxSteps = Width + Height + 4;

	double TCurrent = TMin;
	double AccumulatedFraction = 0.0;

	for (int32 Step = 0; Step < MaxSteps; ++Step)
	{
		// Each crossing parameter is recomputed FRESH from the integer boundary coordinate rather than
		// accumulated as tMax += tDelta. Accumulation is where a long shallow segment across ~15 cells
		// drifts, and drift here silently violates Sum(frac) == 1.
		double TNextX = NeverCrosses;
		if (StepX != 0)
		{
			const double BoundaryX = static_cast<double>((StepX > 0) ? (I + 1) : I);
			TNextX = (BoundaryX - G0X) * InvDirX;
		}

		double TNextY = NeverCrosses;
		if (StepY != 0)
		{
			const double BoundaryY = static_cast<double>((StepY > 0) ? (J + 1) : J);
			TNextY = (BoundaryY - G0Y) * InvDirY;
		}

		const double TCross = FMath::Min(TNextX, TNextY);
		const bool bLastCell = !(TCross < TMax);

		double Fraction;
		if (bLastCell)
		{
			// Close the interval by REMAINDER, not by the crossing value. Sterbenz makes
			// Acc + (Retained - Acc) == Retained exactly, which is what makes Sum(frac) == 1 hold bit
			// for bit instead of one ULP short after a dozen cells.
			Fraction = FMath::Max(0.0, Retained - AccumulatedFraction);
		}
		else
		{
			Fraction = TCross - TCurrent;
			AccumulatedFraction += Fraction;
		}

		if (Fraction > 0.0)
		{
			// D-D — hand the deposit's position WITHIN this cell to the splat. The DDA has always known
			// it: the segment occupies [TCurrent, TCurrent + Fraction] of the parametric range, so its
			// midpoint in grid space is exact. Discarding it and stamping on the cell centre is what
			// quantised the drawn stroke to the lattice and put it up to half a cell off the agent.
			//
			// Midpoint rather than entry point: a segment that crosses most of a cell should draw where
			// its mass actually is, and the midpoint is the centroid of a uniform-density chord.
			const double TMid = TCurrent + 0.5 * Fraction;
			const double SubCellX = (G0X + TMid * DirX) - (static_cast<double>(I) + 0.5);
			const double SubCellY = (G0Y + TMid * DirY) - (static_cast<double>(J) + 0.5);

			DepositCell(I, J, LengthMetres * Fraction, Delta * Fraction, SubCellX, SubCellY);
		}

		if (bLastCell)
		{
			break;
		}

		if (TNextX < TNextY)
		{
			I += StepX;
		}
		else if (TNextY < TNextX)
		{
			J += StepY;
		}
		else
		{
			// Exact corner crossing: advance both axes in one step. The two off-diagonal cells at this
			// lattice point are never visited, so no zero-extent deposit is produced and Sum(frac) is
			// untouched. (0,0) -> (2,2) therefore yields {(0,0): 0.5, (1,1): 0.5} and nothing else.
			I += StepX;
			J += StepY;
		}

		TCurrent = TCross;

		// Unreachable after a correct clip; kept so a degenerate input can never index out of range.
		if (I < 0 || I >= Width || J < 0 || J >= Height)
		{
			break;
		}
	}
}

void FTrajectoryField::EncodeToDisplay(ETrajectoryMapMode Mode, TArray<uint8>& OutBGRA8) const
{
	const int32 NumCells = GridDims.X * GridDims.Y;

	if (NumCells <= 0 || Presentation.Num() < NumCells)
	{
		OutBGRA8.Reset();
		LastEncodeScale = 0.0f;
		LastEncodeMaxDensity = 0.0f;
		return;
	}

	// D-F: grow but never Reset(). Reset()+SetNumZeroed() memset several megabytes on EVERY refresh, and
	// every byte of it was about to be overwritten or skipped. The buffer is the actor's persistent scratch.
	if (OutBGRA8.Num() != NumCells * BytesPerPixel)
	{
		OutBGRA8.Reset();
		OutBGRA8.SetNumZeroed(NumCells * BytesPerPixel);
	}

	EnsurePresentation(Mode);

	// Canonical cells hold raw person-metres / person-seconds. Dividing by cell area turns them into the
	// documented display units (person/m and person*s/m^2). Because the area is uniform this cannot
	// change a single encoded byte - it is done so the scale recorded for the export metadata is in
	// bytes per physical unit rather than bytes per raw cell total.
	const double InvCellArea = (CellAreaSquareMetres > 0.0f) ? (1.0 / static_cast<double>(CellAreaSquareMetres)) : 0.0;

	// D-F: fold the DIRTY region's maximum into the running one instead of sweeping the grid.
	//
	// This is exact, not an approximation, and the reason is narrow: deposits only ever ADD to a cell, so
	// the presentation is monotonically non-decreasing between clears and a maximum can never go stale by
	// missing a DECREASE. Every path that can lower a cell — Clear, Initialise, a mode rebuild — resets
	// RunningMaxPresentation and marks the whole grid dirty, so the next encode re-establishes it.
	{
		const FIntRect Scan = (DirtyRect.Min.X < DirtyRect.Max.X && DirtyRect.Min.Y < DirtyRect.Max.Y)
			? DirtyRect
			: FIntRect(0, 0, 0, 0);
		for (int32 Y = Scan.Min.Y; Y < Scan.Max.Y; ++Y)
		{
			const int32 RowBase = Y * GridDims.X;
			for (int32 X = Scan.Min.X; X < Scan.Max.X; ++X)
			{
				RunningMaxPresentation = FMath::Max(RunningMaxPresentation, Presentation[RowBase + X]);
			}
		}
	}
	const float MaxRaw = RunningMaxPresentation;

	const double MaxDensity = static_cast<double>(MaxRaw) * InvCellArea;
	LastEncodeMaxDensity = static_cast<float>(MaxDensity);

	// FIXED REFERENCE normalisation by default; auto-exposure only on request.
	//
	// Auto-exposure makes byte 255 mean "the brightest cell in this capture", so every band edge silently
	// re-scales per capture and none of them transfer between buildings - the display-layer survival of the
	// very defect this rebuild removed from the stored value. A fixed reference makes a byte a stated
	// physical density, which is the precondition for band edges meaning anything at all.
	const double Reference = static_cast<double>((Mode == ETrajectoryMapMode::RouteUsage)
		? Config.ReferenceUsageDensity
		: Config.ReferenceExposureDensity);

	const bool bAutoExpose = Config.bAutoExposeDisplay || !(Reference > 0.0);
	const double Scale = bAutoExpose
		? ((MaxDensity > 0.0) ? (255.0 / MaxDensity) : 0.0)
		: (255.0 / Reference);

	LastEncodeScale = static_cast<float>(Scale);
	LastEncodeReferenceDensity = static_cast<float>(bAutoExpose ? MaxDensity : Reference);
	bLastEncodeWasAutoExposed = bAutoExpose;
	LastEncodeSaturatedCells = 0;

	// An empty field still has to produce an opaque buffer, so alpha is written on both branches.
	uint8* RESTRICT Bytes = OutBGRA8.GetData();
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		int32 Quantised = 0;
		if (Scale > 0.0)
		{
			const double Density = static_cast<double>(Presentation[Index]) * InvCellArea;
			Quantised = FMath::Clamp(static_cast<int32>(FMath::RoundToDouble(Density * Scale)), 0, 255);

			// Byte 0 is RESERVED for "no data" (the band scheme's lowest bucket keys on it), so a cell that
			// genuinely holds mass must never encode to 0. Without this floor, round() sends every cell
			// fainter than half a byte to 0, and a real but lightly-used route becomes indistinguishable
			// from ground nobody walked on - on a 1000-agent capture that was ~1700 texels.
			//
			// This is NOT the old minimum-visible seed returning. That seed added a constant (byte 25) to
			// the accumulator itself, which is what made the first band meaningless. This touches only the
			// display quantisation, never a canonical or presentation value, and it lifts a cell by at most
			// one byte - and only a cell that is already strictly positive.
			if (Quantised == 0 && Presentation[Index] > 0.0f)
			{
				Quantised = 1;
			}
			if (Quantised >= 255)
			{
				// Counted, not hidden: under a fixed reference, saturation is a real statement about the
				// data ("at or above the reference density"), and how much of the field is making it is
				// the number that tells you whether the reference is set sensibly.
				++LastEncodeSaturatedCells;
			}
		}

		uint8* RESTRICT Pixel = Bytes + Index * BytesPerPixel;
		Pixel[ChannelOffsetB] = 0;
		Pixel[ChannelOffsetG] = 0;
		Pixel[ChannelOffsetR] = static_cast<uint8>(Quantised);
		Pixel[ChannelOffsetA] = 255;
	}
}
