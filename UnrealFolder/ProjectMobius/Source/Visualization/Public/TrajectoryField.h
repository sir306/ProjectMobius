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

#pragma once

// Deliberately plain C++: no UObject, no world, no RHI. The whole numeric contract of the trajectory
// surface has to be constructible and assertable inside a headless EditorContext automation test, so
// nothing in here may reach for a UWorld, a texture resource or the render thread. The single reflected
// type is the mode enum, which exists only so the visualizer can expose it as a UPROPERTY.
//
// Kept free of the OpenCV includes that DynamicPixelRenderingTexture.h pulls in.

#include "CoreMinimal.h"

#include "TrajectoryField.generated.h"

/**
 * Which quantity the trajectory surface presents.
 *
 * Both are accumulated simultaneously and unconditionally by DepositSegment - switching mode never
 * re-walks any segment and never touches either canonical array. Only the presentation (kernel-splatted)
 * copy is mode-specific.
 */
UENUM(BlueprintType)
enum class ETrajectoryMapMode : uint8
{
	/** Cumulative person-metres travelled through each cell. Divide by cell area for person/m. */
	RouteUsage    UMETA(DisplayName = "Route Usage"),

	/** Cumulative person-seconds spent in each cell. Divide by cell area for person*s/m^2. */
	RouteExposure UMETA(DisplayName = "Route Exposure")
};

/**
 * Sizing and validity policy for FTrajectoryField.
 *
 * Deliberately NOT a USTRUCT: nothing here needs Blueprint or details-panel exposure yet, and every
 * reflected member added to a Public header of this module propagates UHT surface to every downstream
 * consumer. Promote it if and when the visualizer actually needs it editable.
 */
struct FTrajectoryFieldConfig
{
	/**
	 * D2 - the source of cross-floor path-width consistency. One texel is this many world centimetres
	 * on EVERY floor; the grid dimensions vary instead. This is the defect being removed: the old path
	 * pinned the texture to 1024x1024 and let cm/texel float with building size, so one person's
	 * trajectory was 5.9 cm wide on a small floor and 73 cm wide on a large one.
	 */
	float WorldCmPerTexel = 10.0f;

	/**
	 * Presentation only. Feeds the splat kernel radius; it must not change any canonical value, and the
	 * invariant Sum(Presentation) == Sum(Canonical) is what proves it does not.
	 */
	float DisplayPathWidthCm = 20.0f;

	/**
	 * D2b - hard ceiling on either grid axis. When honouring WorldCmPerTexel would exceed it, cm/texel
	 * is RAISED (coarser cells) rather than the grid being stretched to a fixed size. Clamped to
	 * [1, 8192] on Initialise; 8192 already implies ~805 MiB across the three float arrays.
	 */
	int32 MaxGridDim = 2048;

	/**
	 * Display density that maps to byte 255, per mode. THIS IS WHAT MAKES A BYTE MEAN SOMETHING.
	 *
	 * The encode used to auto-expose against the current maximum cell, so a byte meant "this fraction of
	 * the brightest cell in THIS capture" - which moves between captures and between buildings. Band edges
	 * expressed against it therefore could not transfer, which is the same defect this rebuild removed
	 * from the stored value, surviving at the display layer. With a fixed reference, byte 255 means a
	 * stated physical density and nothing else, so bands are comparable across captures and a saturated
	 * texel is a real statement ("at or above the reference") rather than an artefact of the frame.
	 *
	 * Defaults are the p99 of the first real TechSchool export, rounded: 99.79 person/m -> 100, and
	 * 194.91 person*s/m^2 -> 200. That puts the busiest 1% of occupied cells at or above full scale.
	 * Cells above the reference clamp to 255 - deliberately: the canonical float field keeps rising and
	 * is what analysis reads (AC9), while the display is allowed to top out.
	 */
	float ReferenceUsageDensity = 100.0f;    // person/m
	float ReferenceExposureDensity = 200.0f; // person*s/m^2

	/**
	 * Legacy per-capture auto-exposure, opt-in. Useful for eyeballing a sparse capture where everything
	 * would otherwise sit in the bottom band; never use it for anything that will be compared to another
	 * capture, or for fitting band edges.
	 */
	bool bAutoExposeDisplay = false;

	/**
	 * Teleport rejection. 2000 cm/s = 20 m/s. Set from human performance rather than from egress gait:
	 * the gate exists to catch discontinuities (agent recycling, dataset swap, floor change), which land
	 * in the thousands of cm/s, and a threshold set just above the fastest human (10.44 m/s over 100 m)
	 * leaves no headroom for a legitimate segment sampled across a frame hitch. Anything under 20 m/s is
	 * accepted; painting a real discontinuity draws a false corridor straight across the building.
	 */
	float MaxPlausibleSpeedCmPerSec = 2000.0f;

	/**
	 * Second, independent teleport gate. Speed alone cannot catch a timeline scrub: skip 100 s of sim
	 * time while an agent legitimately moves 50 m and the implied speed is 0.5 m/s, which passes the
	 * speed gate and paints a straight line across the building. Delta-t is the tell.
	 *
	 * 5.0 s, not 1.0 s. DeltaSeconds is PER-FRAME sim time, so at 8x playback it is 8/fps - 0.27 s at
	 * 30 fps but 1.6 s across a 5 fps hitch, which a 1.0 s cap would reject. The rejected mass would then
	 * vanish and present as a DDA conservation failure rather than as a gate misfire. 5.0 s tolerates 8x
	 * down to 1.6 fps and still catches the scrub case: 100 s at 0.5 m/s passes the speed gate but is
	 * 100 s >> 5 s.
	 *
	 * Together the two gates bound the longest paintable segment at
	 * MaxPlausibleSpeedCmPerSec * MaxPlausibleDeltaSeconds = 100 m.
	 */
	float MaxPlausibleDeltaSeconds = 5.0f;
};

/**
 * A fixed-world-scale 2D scalar field for crowd trajectories.
 *
 * Two canonical accumulators per cell, both float32:
 *   PersonMetres  - person-metres of path length that passed through the cell
 *   PersonSeconds - person-seconds of occupancy attributable to the cell
 *
 * A segment is distributed across the cells it actually crosses by exact 2D DDA (Amanatides-Woo), in
 * proportion to the parametric interval spent in each. It is NOT stamped as a disc at either endpoint
 * and it is NOT seeded to a minimum visible value; both of those were defects of the previous path.
 *
 * ==================== CONVENTIONS (all deterministic, all deliberate) ====================
 *
 * GRID SPACE. g = (WorldCm - OriginCm) / EffectiveCmPerTexel. Cell (i,j) covers g.x in [i, i+1) and
 * g.y in [j, j+1). The grid domain is the CLOSED box [0,W] x [0,H]; a point exactly on the far edge is
 * inside.
 *
 * BOUNDARY OWNERSHIP (segment running exactly ALONG a grid line). The lower-index cell owns it. A
 * segment collinear with g.x == k is deposited into column k-1, not column k. At k == 0 there is no
 * lower cell, so column 0 owns the grid's own minimum edge. This is the one place the addressing is not
 * plain floor(), and it falls out of a single shared rule rather than a special case: the entering cell
 * index on an axis is floor(p) when the direction on that axis is positive, and ceil(p)-1 otherwise
 * (zero direction included), clamped into range.
 *
 * CORNER CROSSING (both axis crossings coincide exactly). Advance BOTH axes in one step - a true
 * diagonal transition. The two off-diagonal cells at that lattice point receive nothing at all rather
 * than a zero-extent visit. For (0,0) -> (2,2) on a unit grid this yields exactly
 * {(0,0): 0.5, (1,1): 0.5}; cells (1,0) and (0,1) are never touched.
 *
 * REVERSAL. Both rules above are direction-symmetric, so B->A produces bit-identical per-cell values
 * to A->B up to the order of the float adds.
 *
 * CLIPPING (D7). The parametric interval is clipped against the grid box FIRST. Coordinates are never
 * clamped into range - clamping is the current bug, it welds every off-floor agent onto the border row.
 * The retained interval is deposited; the complement is added to the dropped-mass counters.
 *
 * FOUR-BUCKET AUDIT IDENTITY. For every segment offered to DepositSegment:
 *
 *   Sum(LengthMetres) == TotalPersonMetres + DroppedPersonMetres + RejectedPersonMetres
 *                        + NegligiblePersonMetres
 *   Sum(DeltaSeconds) == TotalPersonSeconds + DroppedPersonSeconds + RejectedPersonSeconds
 *
 * Dropped means geometrically outside the grid. Rejected means refused by a validity gate (teleport,
 * non-positive or oversized delta-t, field not initialised). Negligible is the sub-threshold length
 * discarded by the stationary rule. A rejection is NEVER folded into dropped - conflating them is how a
 * conservation test ends up disagreeing for a reason that has nothing to do with the geometry.
 * Non-finite input is the sole exception: its length is not a number, so it is counted but contributes
 * to no mass bucket.
 *
 * The running totals are double even though the cells are float32. They exist so the conservation tests
 * have a reference that is not itself subject to per-cell accumulation error.
 */
class VISUALIZATION_API FTrajectoryField
{
public:
	/** Bytes per encoded pixel, and the channel order of PF_B8G8R8A8. */
	static constexpr int32 BytesPerPixel = 4;
	static constexpr int32 ChannelOffsetB = 0;
	static constexpr int32 ChannelOffsetG = 1;
	static constexpr int32 ChannelOffsetR = 2;
	static constexpr int32 ChannelOffsetA = 3;

	/** A segment at or below this length in metres is treated as a stationary agent (spec: 1e-4 m). */
	static constexpr double StationaryLengthMetres = 1.0e-4;

	/** Floor on cm/texel, purely to keep the grid arithmetic finite if a caller passes zero. */
	static constexpr float MinCmPerTexel = 1.0e-3f;

	/** Absolute ceiling on MaxGridDim, to keep W*H inside int32 and the allocation sane. */
	static constexpr int32 AbsoluteMaxGridDim = 8192;

	/**
	 * Sizes the grid from the floor's world extent and allocates. Also resets every accumulator and
	 * counter - a re-initialise on floor change that kept stale mass would be a silent bug.
	 *
	 * @param FloorExtentCm  Size of the floor in world cm. GridDims = ceil(Extent / cm-per-texel), NPOT
	 *                       allowed (no mips, TA_Clamp, uncompressed).
	 * @param FloorOriginCm  World-cm coordinate of the grid's MINIMUM corner, i.e. of cell (0,0)'s min
	 *                       corner. Not the centre. Getting this wrong shifts the whole field silently.
	 * @param InConfig       Policy. WorldCmPerTexel may be RAISED to honour MaxGridDim (D2b); read the
	 *                       result back with GetEffectiveCmPerTexel().
	 */
	void Initialise(const FVector2D& FloorExtentCm, const FVector2D& FloorOriginCm,
	                const FTrajectoryFieldConfig& InConfig);

	/**
	 * The one hot path. Start/End in world cm, already floor-local or projected by the caller (this
	 * class knows nothing about floors, storeys or Z). Serial by design - the density path
	 * parallel-writes shared texels and is already a race; do not copy that pattern here.
	 *
	 * Contains no logging of any kind, deliberately: at ~17k agents a single UE_LOG here invalidates
	 * every latency measurement in the app.
	 */
	void DepositSegment(const FVector2D& StartCm, const FVector2D& EndCm, float DeltaSeconds);

	/**
	 * Writes the selected mode's presentation field into the red channel of a BGRA8 buffer sized
	 * W*H*4. B and G are zero, A is 255.
	 *
	 * Normalisation is linear auto-exposure against the maximum cell DENSITY (cell value divided by
	 * cell area, so the scale is a physically meaningful bytes-per-(person/m) figure for the export
	 * metadata). Byte = round(Density * Scale), clamped to [0,255]; the maximum cell therefore encodes
	 * to exactly 255. There is deliberately no minimum-visible seed - that hack is what made the
	 * trajectory surface's LOS band A meaningless.
	 *
	 * Read the applied scale back with GetLastEncodeScale() / GetLastEncodeMaxDensity().
	 *
	 * const, but it may lazily rebuild the mutable presentation cache for a newly selected mode.
	 */
	void EncodeToDisplay(ETrajectoryMapMode Mode, TArray<uint8>& OutBGRA8) const;

	/** D8 - zeroes all three cell arrays and every accumulator/counter. Keeps dims, config and mode. */
	void Clear();

	/** True once Initialise has produced a grid with at least one cell on each axis. */
	bool IsValid() const { return GridDims.X > 0 && GridDims.Y > 0; }

	FIntPoint GetGridDims() const { return GridDims; }

	/** May be coarser than the requested WorldCmPerTexel after the D2b clamp. */
	float GetEffectiveCmPerTexel() const { return EffectiveCmPerTexel; }

	float GetCellAreaSquareMetres() const { return CellAreaSquareMetres; }

	/** The clamped config actually in force (WorldCmPerTexel here is the REQUESTED value, not effective). */
	const FTrajectoryFieldConfig& GetConfig() const { return Config; }

	double GetTotalPersonMetres() const { return TotalPersonMetres; }
	double GetTotalPersonSeconds() const { return TotalPersonSeconds; }

	/** D7 - mass whose geometry fell outside the grid. */
	double GetDroppedPersonMetres() const { return DroppedPersonMetres; }
	double GetDroppedPersonSeconds() const { return DroppedPersonSeconds; }

	/** Mass refused by a validity gate. Never folded into the dropped counters. */
	double GetRejectedPersonMetres() const { return RejectedPersonMetres; }
	double GetRejectedPersonSeconds() const { return RejectedPersonSeconds; }

	/** Sub-threshold path length discarded by the stationary rule (the seconds are still deposited). */
	double GetNegligiblePersonMetres() const { return NegligiblePersonMetres; }

	int32 GetRejectedSegmentCount() const { return RejectedSegmentCount; }
	int32 GetRejectedNonFiniteCount() const { return RejectedNonFiniteCount; }
	int32 GetRejectedNonPositiveDeltaCount() const { return RejectedNonPositiveDeltaCount; }
	int32 GetRejectedDeltaTooLargeCount() const { return RejectedDeltaTooLargeCount; }
	int32 GetRejectedTeleportCount() const { return RejectedTeleportCount; }
	int32 GetRejectedNotInitialisedCount() const { return RejectedNotInitialisedCount; }

	/** Segments handled by the stationary rule rather than the DDA. */
	int32 GetStationarySegmentCount() const { return StationarySegmentCount; }

	/** Segments whose retained interval was empty (entirely, or measure-zero, outside the grid). */
	int32 GetFullyClippedSegmentCount() const { return FullyClippedSegmentCount; }

	/**
	 * Cell containing a world-cm point, or false if the point is outside the grid's closed box.
	 *
	 * Uses the SAME lower-index-owns rule the DDA deposits with, which is the whole reason this is public:
	 * any caller that needs "which cell is this world point in" must not re-derive it with a bare
	 * FloorToInt. Plain floor() disagrees in two places - a point exactly on a grid line resolves one cell
	 * too high, and the grid's far edge is rejected instead of belonging to the last cell - so a caller
	 * using floor() targets a different cell than the one the deposition wrote to, on exactly the boundary
	 * coordinates a test is most likely to pick.
	 */
	bool WorldToCell(const FVector2D& WorldCm, FIntPoint& OutCell) const { return ContainingCell(WorldCm, OutCell); }

	/** Row-major, index = Y * GridDims.X + X. Never altered by a mode switch or a width change. */
	const TArray<float>& GetCanonical(ETrajectoryMapMode Mode) const;

	/** Presentation for the currently selected mode; rebuilt first if a mode switch is outstanding. */
	const TArray<float>& GetPresentation() const;

	/** Presentation for an explicit mode; selects it (as a cache) and rebuilds if needed. */
	const TArray<float>& GetPresentation(ETrajectoryMapMode Mode) const;

	/** Selects the mode the incremental splat maintains. Marks the presentation for rebuild if changed. */
	void SetPresentationMode(ETrajectoryMapMode Mode);
	ETrajectoryMapMode GetPresentationMode() const { return PresentationMode; }

	/**
	 * Changes the display path width, rebuilds the kernel and marks the presentation for rebuild. The
	 * canonical arrays are NOT touched - that is the whole point of the split, and it is what makes the
	 * width a display control rather than a re-import. Rebuilding the kernel here is also why the splat
	 * never rebuilds it per segment (the previous brush code did, ~17k identical rebuilds per second).
	 */
	void SetDisplayPathWidthCm(float WidthCm);

	/** DisplayPathWidthCm / (2 * EffectiveCmPerTexel). 1.0 at defaults. */
	float GetKernelRadiusTexels() const { return KernelRadiusTexels; }

	/** Integer texel offsets of the splat kernel, and their weights. Weights sum to 1. */
	const TArray<FIntPoint>& GetKernelOffsets() const { return KernelOffsets; }
	const TArray<float>& GetKernelWeights() const { return KernelWeights; }

	/** Bytes per unit density applied by the last EncodeToDisplay, and the maximum density present. */
	float GetLastEncodeScale() const { return LastEncodeScale; }
	float GetLastEncodeMaxDensity() const { return LastEncodeMaxDensity; }

	/**
	 * The density that mapped to byte 255 in the last encode. Under the default fixed reference this is
	 * the configured reference; under auto-exposure it is whatever the frame's maximum happened to be,
	 * which is exactly why the two must be distinguishable from the outside.
	 */
	float GetLastEncodeReferenceDensity() const { return LastEncodeReferenceDensity; }
	bool WasLastEncodeAutoExposed() const { return bLastEncodeWasAutoExposed; }

	/** Cells that hit 255 in the last encode - i.e. at or above the reference density. */
	int32 GetLastEncodeSaturatedCells() const { return LastEncodeSaturatedCells; }

private:
	/**
	 * Entering-cell index on one axis. Positive direction takes floor(); zero and negative directions
	 * take ceil(p)-1, which is what makes the lower-index cell own a grid line the segment runs along or
	 * departs from. Clamped into [0, N-1]; after clipping the clamp is only ever reached by the
	 * legitimate g == 0 boundary case.
	 */
	static int32 AxisIndexFromCoord(double G, double Dir, int32 N);

	/** Liang-Barsky slab clip of [TMin,TMax] against [0,N] on one axis. False = no overlap at all. */
	static bool ClipAxis(double G0, double D, int32 N, double& TMin, double& TMax);

	/** Grid dims for an extent at a given cm/texel. */
	static FIntPoint DimsForExtent(double ExtentXCm, double ExtentYCm, float CmPerTexel);

	/** Cell containing a world point, using the zero-direction (lower-index-owns) rule. */
	bool ContainingCell(const FVector2D& WorldCm, FIntPoint& OutCell) const;

	/** Adds to both canonical arrays and splats the active mode's share into the presentation. */
	void DepositCell(int32 I, int32 J, double AddPersonMetres, double AddPersonSeconds);

	/** Distributes Value over the kernel about (I,J), renormalising over in-bounds taps at the edge. */
	void SplatInto(TArray<float>& Target, int32 I, int32 J, double Value) const;

	/** Rebuilds the offset/weight table. Called on Initialise only - never per segment. */
	void BuildKernel();

	/** Full presentation rebuild from a canonical array. */
	void RebuildPresentation(ETrajectoryMapMode Mode) const;

	/** Rebuilds only if the mode changed or the cache is dirty. */
	void EnsurePresentation(ETrajectoryMapMode Mode) const;

	/** Books a segment into the rejected buckets. */
	void RecordRejection(double LengthMetres, double DeltaSeconds, bool bAccountMass);

	FTrajectoryFieldConfig Config;

	/** World cm of cell (0,0)'s minimum corner. */
	FVector2D OriginCm = FVector2D::ZeroVector;

	FIntPoint GridDims = FIntPoint::ZeroValue;
	float EffectiveCmPerTexel = 0.0f;
	double InvEffectiveCmPerTexel = 0.0;
	float CellAreaSquareMetres = 0.0f;

	/** Canonical, float32, row-major. Untouched by mode or display-width changes. */
	TArray<float> PersonMetres;
	TArray<float> PersonSeconds;

	/** Splat kernel, built once. */
	TArray<FIntPoint> KernelOffsets;
	TArray<float> KernelWeights;
	int32 KernelHalfExtent = 0;
	int32 KernelCentreIndex = INDEX_NONE;
	float KernelRadiusTexels = 0.0f;

	/**
	 * Presentation cache. Maintained incrementally inside the DDA walk for PresentationMode, and
	 * rebuilt wholesale from canonical when the mode changes. Mutable because EncodeToDisplay is const
	 * and the rebuild is a pure cache fill: the splat is linear, so rebuilding gives the same values the
	 * incremental path would have, up to float rounding.
	 */
	mutable TArray<float> Presentation;
	mutable ETrajectoryMapMode PresentationMode = ETrajectoryMapMode::RouteUsage;

	/**
	 * Set by TWO independent triggers, not one: a pending mode change AND a kernel rebuild from
	 * SetDisplayPathWidthCm. EnsurePresentation only inspects the mode, so anything that invalidates the
	 * splat WITHOUT changing the mode must raise this flag or the width change will not take effect.
	 */
	mutable bool bPresentationDirty = false;
	mutable float LastEncodeScale = 0.0f;
	mutable float LastEncodeMaxDensity = 0.0f;
	mutable float LastEncodeReferenceDensity = 0.0f;
	mutable bool bLastEncodeWasAutoExposed = false;
	mutable int32 LastEncodeSaturatedCells = 0;

	double TotalPersonMetres = 0.0;
	double TotalPersonSeconds = 0.0;
	double DroppedPersonMetres = 0.0;
	double DroppedPersonSeconds = 0.0;
	double RejectedPersonMetres = 0.0;
	double RejectedPersonSeconds = 0.0;
	double NegligiblePersonMetres = 0.0;

	int32 RejectedSegmentCount = 0;
	int32 RejectedNonFiniteCount = 0;
	int32 RejectedNonPositiveDeltaCount = 0;
	int32 RejectedDeltaTooLargeCount = 0;
	int32 RejectedTeleportCount = 0;
	int32 RejectedNotInitialisedCount = 0;
	int32 StationarySegmentCount = 0;
	int32 FullyClippedSegmentCount = 0;
};
