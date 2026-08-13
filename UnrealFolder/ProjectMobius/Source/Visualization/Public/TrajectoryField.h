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

	/**
	 * 240 since 2026-08-10, up from the capture-derived 200.
	 *
	 * ⚠️ CORRECTED 2026-08-13 — this value is NOT derived, and the derivation that used to sit here was
	 * wrong. It read: "the top edge fits the [0,1] display channel only when CellSide * Reference *
	 * v_free >= 50 — at the shipping 15 cm cell and SFPE's 1.40 m/s that needs >= 238.1", which is what
	 * 240 was picked to clear.
	 *
	 * The length in that inequality is the DISPLAY STROKE WIDTH, not the cell side. That is exactly the
	 * confusion acb392a9 fixed in FHeatmapLOSBands (both factories took a parameter named CellSideMetres
	 * while TrajectoryTransits needed the width, which made the wrong argument look right at the call
	 * site), and this comment was the last surviving instance of it. At the shipping 45 cm width the real
	 * bar is 50 / (0.45 * 1.40) = 79.4, not 238.1 — the two differ by exactly the 45/15 = 3x that caused
	 * the original defect.
	 *
	 * What remains TRUE: below the bar, the top edge clamps to 1.0 and band F becomes UNREACHABLE, so
	 * queueing and blocked render identically and nothing complains, because the clamp keeps the chain
	 * monotonic. Distinguishing those two is the entire purpose of the surface.
	 *
	 * So this is a FREE PRESENTATION CHOICE anywhere above ~79.4, not a forced value. It is a brightness
	 * dial: the displayed value is density / Reference, so 240 renders every cell ~17 % dimmer than the
	 * capture-derived 200 would. Open owner decision — do not treat the current number as load-bearing.
	 *
	 * Read FHeatmapLOSBands::MinimumExposureReferenceForFullLadder for the bar rather than transcribing
	 * any number from this comment; it takes the same length scale as TrajectoryTransits by construction.
	 */
	float ReferenceExposureDensity = 240.0f; // person*s/m^2

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

	/**
	 * D-C — extra grid cells appended per axis AFTER the extent-derived dimensions are resolved. Zero for
	 * every caller except the trajectory visualizer, and it is not a quality or resolution knob.
	 *
	 * WHY IT EXISTS. The render's texel lattice and this field's cell lattice must be the SAME lattice or
	 * the drawn stroke sits beside the agent. The mesh UV letterboxes the minor axis by a REAL-valued
	 * margin of 0.5 * S * (1 - minorExtent / majorExtent) texels, while the field can only write at
	 * integer texels; the difference is a permanent sub-texel phase error that put 32% of the real test
	 * floor in the wrong texel, on whichever axis happened to be minor — which is why it looked
	 * orientation-dependent. The visualizer removes it by shifting this field's ORIGIN by the fractional
	 * remainder, which re-phases the lattice to match the render exactly.
	 *
	 * That shift is up to half a cell in EITHER direction, so the grid can then fall short of the mesh by
	 * up to half a cell at one edge. One extra cell on the shifted axis covers it. Deposits are never
	 * clamped into range in this codebase — an off-grid sample is dropped and reported — so without the
	 * pad a strip of real floor would silently stop accumulating.
	 *
	 * Applied additively to the resolved dims. It does NOT feed ResolveGrid, so it cannot perturb
	 * EffectiveCmPerTexel, the major-axis snap, or which axis is major.
	 */
	FIntPoint ExtraGridCells = FIntPoint::ZeroValue;
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
	 * The grid-sizing decision, on its own, with no allocation and no object state — D2b's cm/texel raise
	 * followed by D-A's major-axis snap. Initialise() is its only in-class caller and does exactly this
	 * before it allocates, so the two can never disagree.
	 *
	 * PUBLIC because the render side has to know the outcome BEFORE the field exists. The visualizer needs
	 * EffectiveCmPerTexel and the grid dims to work out the letterbox phase it must cancel, and it has to
	 * apply that as an origin shift passed INTO Initialise — a chicken-and-egg that would otherwise force
	 * two Initialise calls and two allocations on every floor change. It is also what lets a test assert
	 * the sizing contract without spawning a world.
	 *
	 * Does NOT apply Config.ExtraGridCells: that is additive padding on top of this answer, deliberately
	 * outside it so padding can never perturb cm/texel, the snap, or which axis is major.
	 *
	 * @param ExtentXCm/ExtentYCm  Floor extent. Negatives are treated as zero.
	 * @param InConfig             Read for WorldCmPerTexel and MaxGridDim only.
	 * @param OutCmPerTexel        The EFFECTIVE cm/texel: >= MinCmPerTexel, and exactly
	 *                             majorExtent / majorDim so the major axis divides evenly.
	 * @param OutDims              Grid dimensions in cells, before ExtraGridCells.
	 */
	static void ResolveGrid(double ExtentXCm, double ExtentYCm, const FTrajectoryFieldConfig& InConfig,
	                        float& OutCmPerTexel, FIntPoint& OutDims);

	/**
	 * D-E — the ROUTE THRESHOLD in crossings: below this a cell is not part of the drawn stroke.
	 *
	 * WHAT IT FIXES. The display classifies each texel into one band, so a stroke's width can only ever be
	 * a whole number of cells — and with the old threshold (half a byte, i.e. "is it nonzero") EVERY cell
	 * the kernel tail touched painted at full band-B colour. A 45 cm stroke on 20 cm cells rendered 60 cm
	 * wide, and the width swung by a whole cell as the path slid across the lattice, because a sliver of
	 * kernel spill counts the same as a direct hit.
	 *
	 * WHY IT IS DERIVED AND NOT A CONSTANT. There IS a threshold that renders exactly 45 cm at every
	 * sub-cell phase — but only for one (width, cell, kernel) triple, and it is a fitted number with no
	 * meaning if any of the three moves. Owner ruling #3 on the trajectory spec is "derive, don't fit".
	 * So this reads the kernel's own lateral marginal profile and places the threshold in the gap between
	 * the rows that belong to the stroke and the first row that does not:
	 *
	 *     crossings(row, phase) = marginal(row, phase) * (width / cell)
	 *
	 * That identity is exact and reference-free — the cell area and ReferenceUsageDensity cancel — so the
	 * threshold depends only on the kernel geometry it is protecting.
	 *
	 * The target row count is round(width / cell), which is the honest nearest whole-cell rendering of the
	 * stroke. The window is [max over phases of the first EXCLUDED row, min over phases of the last
	 * INCLUDED row]; the midpoint is returned. When the window is empty — no threshold can hold the width
	 * constant at this width/cell pair, which is the case at 45/20 — the best available compromise is
	 * returned instead and the caller gets a stable, if not exact, width.
	 *
	 * IT IS NOT AN ARBITRARY RULE, and this is the check that says so: at width == cell — the old shipping
	 * pair, where the kernel collapses to the identity — it returns exactly 0.5 crossings, which is the
	 * (N + 0.5) half-step every other band edge already uses. So it GENERALISES the ladder rule to a spread
	 * kernel and reduces to it when there is no spread. The 0/1 edge was the one rung that had been left
	 * off that scale (it was half a BYTE), and this puts it back on.
	 *
	 * Worked values, cross-checked against an independent sweep of the rendered width:
	 *   45 / 15 -> window 0.277..0.383, returns 0.330  (3 rows, exactly 45 cm at every phase)
	 *   45 / 20 -> window empty,        returns 0.510  (2 rows = 40 cm, stable but not exact)
	 *   45 / 45 -> window 0.429..0.571, returns 0.500  (1 row, the identity case above)
	 *
	 * @return Threshold in CROSSINGS. Convert to an RVal band edge by dividing by (widthMetres * Reference).
	 */
	static float DeriveRouteThresholdCrossings(float DisplayPathWidthCm, float CmPerTexel);

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

	/**
	 * Integer texel offsets of the CELL-CENTRED splat kernel, and their weights. Weights sum to 1.
	 *
	 * ⚠️ This is the analytic REFERENCE table, not what the deposit path splats — that is the D-D phase
	 * table, and its footprint is one ring wider. Reading these to reason about per-deposit COST is the
	 * A0-79 mistake in miniature: it answers a question about a table nothing writes through. Use
	 * GetPhaseKernelTapCount() for cost.
	 */
	const TArray<FIntPoint>& GetKernelOffsets() const { return KernelOffsets; }
	const TArray<float>& GetKernelWeights() const { return KernelWeights; }

	/**
	 * Taps per deposit on the SHIPPING path — the D-D phase table's footprint. Unlike the centred table
	 * above this holds every cell in the footprint including the ones a given phase weights at zero, so it
	 * is deterministic in R alone and does NOT drift with the extent. That is the property worth gating:
	 * per-deposit cost must not vary with building size.
	 */
	int32 GetPhaseKernelTapCount() const { return PhaseKernelTapCount; }

	/**
	 * D-F — the half-open cell rectangle touched since the last ClearDirtyRect(), or an empty rect when
	 * nothing has moved.
	 *
	 * WHY. The texture WRITES were already incremental (the actor diffs against TrajectoryPreviousRed), but
	 * the work in front of them was not: every refresh allocated and zeroed a full NumCells*4 buffer, swept
	 * the grid for the maximum, encoded every cell, and then swept it again to find the handful that
	 * changed. Three full-grid walks at ~10 Hz, and the cost is a function of FLOOR AREA rather than of how
	 * many agents moved — a stadium with three people in it paid the same as a full one. At a 4096 grid
	 * that is ~50 M cell visits per refresh.
	 *
	 * Agents occupy a tiny fraction of a large floor per tick, so bounding the work to what actually
	 * changed turns O(area) into O(active area) — and an idle field costs nothing at all, because an empty
	 * rect lets the whole refresh be skipped.
	 */
	FIntRect GetDirtyRect() const { return DirtyRect; }

	/**
	 * Called by the display path once it has consumed the region. Const, and DirtyRect is mutable, for the
	 * same reason the presentation cache is: the refresh path is const and this is bookkeeping about a
	 * cache, not about the measured field.
	 */
	void ClearDirtyRect() const { DirtyRect = FIntRect(0, 0, 0, 0); }

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

	/**
	 * Adds to both canonical arrays and splats the active mode's share into the presentation.
	 *
	 * SubCellX/Y are the deposit's position INSIDE cell (I,J), each in [-0.5, +0.5] measured from the cell
	 * CENTRE. The canonical arrays ignore them — those are cell-exact by DDA and are what analysis reads.
	 * The presentation splat uses them so the drawn stroke's centroid lands on the real path rather than
	 * snapping to the nearest cell centre. See BuildKernel's phase table (D-D).
	 */
	void DepositCell(int32 I, int32 J, double AddPersonMetres, double AddPersonSeconds,
	                 double SubCellX, double SubCellY);

	/**
	 * Distributes Value over the kernel about (I,J), renormalising over in-bounds taps at the edge.
	 * SubCellX/Y select the phase bin; (0,0) reproduces a cell-centred stamp.
	 */
	void SplatInto(TArray<float>& Target, int32 I, int32 J, double Value,
	               double SubCellX, double SubCellY) const;

	/** Builds the D-D sub-cell phase table. Called by BuildKernel; never per segment. */
	void BuildPhaseKernel(double Radius);

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

	/**
	 * Splat kernel, built once. This is the CELL-CENTRED table: the analytic reference the oracle tests are
	 * written against, and the limit the phase table below reproduces at phase (0,0). It is no longer what
	 * the deposit path uses.
	 */
	TArray<FIntPoint> KernelOffsets;
	TArray<float> KernelWeights;
	int32 KernelHalfExtent = 0;
	int32 KernelCentreIndex = INDEX_NONE;
	float KernelRadiusTexels = 0.0f;

	/**
	 * D-D — the SUB-CELL PHASE TABLE, and what the deposit path actually splats.
	 *
	 * THE DEFECT IT REMOVES. The DDA knows exactly where inside a cell a segment ran, but the old
	 * DepositCell took only the integer index, so every stamp was centred on a CELL CENTRE. The drawn
	 * stroke's centroid was therefore quantised to the lattice — up to half a cell (10 cm at the shipping
	 * 20 cm) from the agent, in either direction. That is a rendering error, not a measurement one: the
	 * canonical accumulators were always right, the picture was not. It is what remained visible after the
	 * D-A/D-B/D-C alignment work, and no amount of lattice alignment can remove it, because it is not a
	 * lattice offset — it is the loss of the position WITHIN a cell.
	 *
	 * HOW. BuildKernel evaluates the same analytic disc-area coverage at KernelPhaseBinsPerAxis^2 sub-cell
	 * offsets instead of once at the centre, and the deposit picks the bin nearest its true position. The
	 * per-deposit cost is one splat exactly as before — the phase is a table lookup, not extra work — and
	 * every phase normalises to 1.0, so Sum(Presentation) == Sum(Canonical) still holds bin by bin.
	 *
	 * WHAT IT COSTS. The footprint grows: a disc offset by up to half a cell reaches one ring further, so
	 * the half extent is ceil(R + 0.5) rather than ceil(R - 0.5) — 25 taps instead of 9 at the shipping
	 * R = 1.125. Memory is nothing (64 phases x 25 floats).
	 *
	 * RESIDUAL. Placement is quantised to a bin, so the centroid error falls from half a cell to half a
	 * bin — cell / (2 * Bins), i.e. 10 cm -> 1.1 cm at 9 bins on a 20 cm cell. Raise the bin count if that
	 * ever matters; it costs only build time and memory, and nothing per deposit.
	 *
	 * All phases share PhaseKernelOffsets; PhaseKernelWeights is Bins^2 blocks of PhaseKernelTapCount,
	 * indexed (BinY * Bins + BinX) * TapCount + Tap.
	 *
	 * ⚠️ MUST BE ODD. Bins are sampled at their CENTRES, so an odd count puts one bin at phase exactly
	 * (0,0) and a cell-centred deposit then reproduces the centred table BIT FOR BIT — which is what keeps
	 * the hand-derived oracle weights (T-CONV-3, Kernel.BorderTapsRenormalise) and the identity-kernel
	 * contract (T-BAND-1, T-WIDTH-2) valid. An even count has no zero phase: 8 bins rendered a centred
	 * deposit at (+0.0625, +0.0625) and reddened all four of those gates. The outer ring is exactly zero
	 * at phase (0,0) and is skipped by the splat, so the wider footprint costs nothing there either.
	 */
	static constexpr int32 KernelPhaseBinsPerAxis = 9;
	TArray<FIntPoint> PhaseKernelOffsets;
	TArray<float> PhaseKernelWeights;
	int32 PhaseKernelTapCount = 0;

	/** D-F dirty region, half-open [Min, Max). Empty means nothing changed since the last encode. */
	mutable FIntRect DirtyRect = FIntRect(0, 0, 0, 0);

	/** Expands DirtyRect to cover the splat footprint centred on (I,J), clipped to the grid. */
	void MarkCellDirty(int32 I, int32 J) const;

	/** Whole grid dirty — after Initialise, Clear, or a presentation rebuild. */
	void MarkAllDirty() const { DirtyRect = FIntRect(0, 0, GridDims.X, GridDims.Y); }

	/**
	 * Running maximum of the presentation array, so EncodeToDisplay need not sweep the grid for it.
	 * MONOTONIC AND THEREFORE EXACT between clears: deposits only ever ADD to a cell, so a maximum taken
	 * over the dirty region and folded into the previous maximum equals a full-grid maximum. Reset
	 * wherever the presentation can go DOWN — Clear, Initialise, and a mode rebuild.
	 */
	mutable float RunningMaxPresentation = 0.0f;

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
