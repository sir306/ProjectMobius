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

	OriginCm = FloorOriginCm;

	const double ExtX = FMath::Max(0.0, static_cast<double>(FloorExtentCm.X));
	const double ExtY = FMath::Max(0.0, static_cast<double>(FloorExtentCm.Y));

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
	LastEncodeScale = 0.0f;
	LastEncodeMaxDensity = 0.0f;

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
				SplatInto(Presentation, I, J, static_cast<double>(Value));
			}
		}
	}
}

void FTrajectoryField::SplatInto(TArray<float>& Target, int32 I, int32 J, double Value) const
{
	const int32 Width = GridDims.X;
	const int32 Height = GridDims.Y;
	const int32 NumTaps = KernelOffsets.Num();

	// Fast path: the whole footprint is interior, so the precomputed weights apply verbatim.
	if (I - KernelHalfExtent >= 0 && I + KernelHalfExtent < Width &&
	    J - KernelHalfExtent >= 0 && J + KernelHalfExtent < Height)
	{
		for (int32 Tap = 0; Tap < NumTaps; ++Tap)
		{
			const FIntPoint& Offset = KernelOffsets[Tap];
			Target[(J + Offset.Y) * Width + (I + Offset.X)] +=
				static_cast<float>(Value * static_cast<double>(KernelWeights[Tap]));
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
		const FIntPoint& Offset = KernelOffsets[Tap];
		const int32 X = I + Offset.X;
		const int32 Y = J + Offset.Y;
		if (X >= 0 && X < Width && Y >= 0 && Y < Height)
		{
			InBoundsWeight += static_cast<double>(KernelWeights[Tap]);
		}
	}
	if (InBoundsWeight <= 0.0)
	{
		return;
	}

	const double Renormalise = 1.0 / InBoundsWeight;
	for (int32 Tap = 0; Tap < NumTaps; ++Tap)
	{
		const FIntPoint& Offset = KernelOffsets[Tap];
		const int32 X = I + Offset.X;
		const int32 Y = J + Offset.Y;
		if (X >= 0 && X < Width && Y >= 0 && Y < Height)
		{
			Target[Y * Width + X] +=
				static_cast<float>(Value * static_cast<double>(KernelWeights[Tap]) * Renormalise);
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

void FTrajectoryField::DepositCell(int32 I, int32 J, double AddPersonMetres, double AddPersonSeconds)
{
	const int32 Index = J * GridDims.X + I;
	PersonMetres[Index] += static_cast<float>(AddPersonMetres);
	PersonSeconds[Index] += static_cast<float>(AddPersonSeconds);

	const double PresentationValue = (PresentationMode == ETrajectoryMapMode::RouteUsage)
		? AddPersonMetres
		: AddPersonSeconds;
	if (PresentationValue != 0.0)
	{
		SplatInto(Presentation, I, J, PresentationValue);
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
	if (!IsUsable2D(StartCm) || !IsUsable2D(EndCm) || !IsUsable(static_cast<double>(DeltaSeconds)))
	{
		// Length is not a number, so this contributes to no mass bucket - only to the counters.
		++RejectedNonFiniteCount;
		RecordRejection(0.0, 0.0, false);
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
				SplatInto(Presentation, Cell.X, Cell.Y, Delta);
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
			DepositCell(I, J, LengthMetres * Fraction, Delta * Fraction);
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

	OutBGRA8.Reset();
	if (NumCells <= 0 || Presentation.Num() < NumCells)
	{
		LastEncodeScale = 0.0f;
		LastEncodeMaxDensity = 0.0f;
		return;
	}

	OutBGRA8.SetNumZeroed(NumCells * BytesPerPixel);

	EnsurePresentation(Mode);

	// Canonical cells hold raw person-metres / person-seconds. Dividing by cell area turns them into the
	// documented display units (person/m and person*s/m^2). Because the area is uniform this cannot
	// change a single encoded byte - it is done so the scale recorded for the export metadata is in
	// bytes per physical unit rather than bytes per raw cell total.
	const double InvCellArea = (CellAreaSquareMetres > 0.0f) ? (1.0 / static_cast<double>(CellAreaSquareMetres)) : 0.0;

	float MaxRaw = 0.0f;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		MaxRaw = FMath::Max(MaxRaw, Presentation[Index]);
	}

	const double MaxDensity = static_cast<double>(MaxRaw) * InvCellArea;
	LastEncodeMaxDensity = static_cast<float>(MaxDensity);

	// Linear auto-exposure. No minimum-visible seed: the old seed-then-increment scheme is exactly why
	// the trajectory surface's first LOS band meant nothing.
	const double Scale = (MaxDensity > 0.0) ? (255.0 / MaxDensity) : 0.0;
	LastEncodeScale = static_cast<float>(Scale);

	// An empty field still has to produce an opaque buffer, so alpha is written on both branches.
	uint8* RESTRICT Bytes = OutBGRA8.GetData();
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		int32 Quantised = 0;
		if (Scale > 0.0)
		{
			const double Density = static_cast<double>(Presentation[Index]) * InvCellArea;
			Quantised = FMath::Clamp(static_cast<int32>(FMath::RoundToDouble(Density * Scale)), 0, 255);
		}

		uint8* RESTRICT Pixel = Bytes + Index * BytesPerPixel;
		Pixel[ChannelOffsetB] = 0;
		Pixel[ChannelOffsetG] = 0;
		Pixel[ChannelOffsetR] = static_cast<uint8>(Quantised);
		Pixel[ChannelOffsetA] = 255;
	}
}
