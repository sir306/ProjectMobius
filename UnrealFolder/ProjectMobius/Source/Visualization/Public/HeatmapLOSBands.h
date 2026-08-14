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

// Deliberately kept free of the OpenCV includes that DynamicPixelRenderingTexture.h pulls in: this
// header is included by MobiusCore's public actor header, so anything added here propagates to every
// consumer of that module's public surface.

#include "CoreMinimal.h"

#include "HeatmapLOSBands.generated.h"

/**
 * The persons/m^2 that the DENSITY surface encodes as a full red channel, i.e. the divisor every edge
 * below is normalised by. 2.1739 is 1 / 0.46, the reciprocal of Fruin's LOS_F boundary in m^2/person.
 *
 * Named 2026-08-14 because it had existed only inside comments here and beside the LOS_*_BAND macros in
 * DynamicPixelRenderingTexture.cpp, while anything wanting to convert an edge back into m^2/person had to
 * retype it. That is the "constant whose justification lives in a drifting comment" shape that has already
 * cost this surface two defects. FHeatmapLegend::Density is the first consumer.
 *
 * Namespace scope rather than a static member of FHeatmapLOSBands on purpose: that struct is dll-exported,
 * and a static constexpr member of an exported struct is ODR-used the moment anything binds it to a const&,
 * which is the one MSVC/dllimport combination that fails to link. Same reasoning as
 * BRiskDefaultDischargeCoefficient.
 */
inline constexpr float FruinMaxDensityPersonsPerSqM = 2.1739f;

/**
 * The TOP step of the Route Exposure transit ladder — the band E/F edge in transit-equivalents.
 *
 * Named because it is used in two places that must not disagree: FHeatmapLOSBands::TrajectoryTransits, which
 * sets the edge, and MinimumExposureReferenceForFullLadder, which computes the smallest reference at which
 * that edge still fits the [0,1] display channel. Those were a literal 50 in both until the 2026-08-14 D5
 * re-tune, and changing one without the other reports the representability bar for a ladder that no longer
 * exists. Namespace scope for the same dll-export reason as FruinMaxDensityPersonsPerSqM above.
 *
 * 120 transits = 30.29 s of standing at the shipping 45 cm stroke. See TrajectoryTransits for the ruling.
 */
inline constexpr float TransitLadderTopStep = 120.0f;

/**
 * Upper edges of the five lower Level of Service colour bands, expressed in normalised red-channel
 * units (stored byte / 255). A value falls in the first band whose edge it is strictly below; anything
 * at or above BandE takes the sixth (LOS_F) colour.
 *
 * These edges exist as data rather than compile-time constants because the two heatmap surfaces measure
 * different quantities on the same uint8 channel:
 *
 *  - The density surface stores Fruin density normalised against a 2.1739 persons/m^2 maximum, so its
 *    edges are the Fruin LOS boundaries and band A legitimately means "almost nobody here".
 *  - The trajectory surface stores cumulative person-metres, encoded against a FIXED reference density,
 *    and an untouched cell encodes to byte 0 exactly. Its band A edge is therefore half a byte: it exists
 *    only to reserve LOS_A as a genuine "no data" colour, leaving five bands for real traffic. Reusing
 *    the density edges here would paint the first several crossings as bare floor.
 *
 * Whatever a surface uses here must match the band scalars pushed to its material, or the exported PNG
 * stops mirroring the in-world render.
 */
USTRUCT(BlueprintType)
struct VISUALIZATION_API FHeatmapLOSBands
{
	GENERATED_BODY()

	/** Upper edge of LOS_A. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|LOS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BandA = 0.1419f; // 1/3.24 = 0.3086 -> 0.3086/2.1739

	/** Upper edge of LOS_B. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|LOS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BandB = 0.1983f; // 1/2.32 = 0.4310 -> 0.4310/2.1739

	/** Upper edge of LOS_C. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|LOS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BandC = 0.3309f; // 1/1.39 = 0.7194 -> 0.7194/2.1739

	/** Upper edge of LOS_D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|LOS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BandD = 0.4946f; // 1/0.93 = 1.0753 -> 1.0753/2.1739

	/** Upper edge of LOS_E. At or above this the texel takes LOS_F. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|LOS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BandE = 1.0f; // 2.1739/2.1739

	/** Fruin density edges — the historical constants, and the defaults above. */
	static FHeatmapLOSBands Density()
	{
		return FHeatmapLOSBands();
	}

	/**
	 * SUPERSEDED AS THE LIVE CONTRACT 2026-08-10 by TrajectoryCrossings(), which computes edges that mean
	 * a countable number of crossings instead of quantiles of one particular capture. This function is
	 * retained for two reasons and should not be deleted:
	 *
	 *   1. It is the UPROPERTY initialiser for AHeatmapPixelTextureVisualizer::TrajectoryLOSBands. A
	 *      default runs at CONSTRUCTION, before the field is sized, so it cannot know the cell size and
	 *      therefore cannot be a crossing-count set. EnsureTrajectoryFieldSized overwrites it with
	 *      TrajectoryCrossings() as soon as the grid exists; until then this is a placeholder that merely
	 *      has to be monotonic and in range.
	 *   2. TrajectoryCrossings() falls back to it when handed a degenerate cell size.
	 *
	 * The quantile rationale below is preserved as provenance. It is NO LONGER what the surface renders.
	 *
	 * Route-intensity edges for the trajectory surface, REFIT 2026-08-04 against the first real canonical
	 * export. Values are normalised 0..1 against the encode's fixed reference density
	 * (FTrajectoryFieldConfig::ReferenceUsageDensity, 100 person/m), so edge x means "x * 100 person/m".
	 *
	 *   below BandA   byte 0 only        NO DATA - ground nobody walked on
	 *   BandA..BandB  <= 10.89 person/m  p50 of occupied cells
	 *   BandB..BandC  <= 21.72           p75
	 *   BandC..BandD  <= 39.18           p90
	 *   BandD..BandE  <= 66.61           p97
	 *   above BandE   > 66.61            the busiest 3%
	 *
	 * WHAT CHANGED, AND WHY THE OLD SET RENDERED WRONG. The previous edges were
	 * [24.5, 46.5, 71.5, 110.5, 175.5] / 255, byte values calibrated by replaying a 30 s capture of the
	 * seed-and-brush rasteriser that this rebuild DELETED (a lone crossing measured at a median 13 hits).
	 * TEST_PLAN section 7 lists exactly that table among the coverage that dies with the brush. Two
	 * consequences were visible on screen:
	 *
	 *  1. BandA sat at byte 24.5 because the old path SEEDED every touched texel at byte 25. With no seed
	 *     and byte 0 now reserved for no-data (EncodeToDisplay floors a positive cell to 1), every texel
	 *     from byte 1 to 24 was painted as EMPTY GROUND. BandA is therefore now half a byte: the only
	 *     value below it is 0.
	 *  2. The remaining edges were fitted to hit counts, a quantity that no longer exists, so they bore no
	 *     relation to person-metres.
	 *
	 * Provenance for the numbers: percentiles of occupied cells in
	 * Saved/Heatmap/Heatmap_RealDatasetCapture_Trajectory_20260804_212520.csv (TechSchool 1000-agent
	 * baseline, 28,822 occupied cells), via MobiusPerf/analysis/band_fit.py's QUANTILE proposal. Chosen
	 * over its log-spaced alternative because the smallest positive cell is a presentation kernel tap five
	 * decades below the median, which makes plain log-spacing put 98% of cells in the top two bands.
	 * PROVISIONAL under D9 - one building, one dataset, 30 s.
	 *
	 * These values are duplicated as the defaults of the LOS_*_Band scalar parameters on
	 * M_HeatmapRT_Trajectory (built by MobiusPerf/BuildTrajectoryHeatmapMaterial.py). The two must
	 * agree; AHeatmapPixelTextureVisualizer::ApplyTrajectoryLOSBands is what keeps them in step at
	 * runtime.
	 */
	static FHeatmapLOSBands Trajectory()
	{
		FHeatmapLOSBands Bands;
		Bands.BandA = 0.5f / 255.0f; // 0.001961 - no-data threshold; only byte 0 falls below it
		Bands.BandB = 0.1089f;       // 10.89 person/m   (p50)
		Bands.BandC = 0.2172f;       // 21.72 person/m   (p75)
		Bands.BandD = 0.3918f;       // 39.18 person/m   (p90)
		Bands.BandE = 0.6661f;       // 66.61 person/m   (p97)
		return Bands;
	}

	/**
	 * CROSSING-COUNT edges for Route Usage. This is the live band contract for the trajectory surface as
	 * of 2026-08-10, and the reason a colour on that surface can be explained to a stakeholder at all:
	 * band B means "about one person walked here", band F means "five or more did".
	 *
	 * ONE CROSSING IS ONE CELL SIDE. An agent walking straight across a cell deposits exactly `s`
	 * person-metres, `s` being the cell side in metres, so Crossings = PersonMetres / s. EncodeToDisplay
	 * normalises a cell's DENSITY (value / cell area) against a fixed reference, so C crossings encode to
	 *
	 *     RVal = (C * s) / s^2 / Reference = C / (s * Reference)
	 *
	 * which is why every edge divides by the single product (CellSideMetres * ReferenceUsageDensity).
	 *
	 * EDGES SIT BETWEEN THE INTEGERS, AT N + 0.5. An edge placed ON an integer puts a cell holding
	 * exactly N crossings on an exact-equality tie against the `RVal < Band` comparison chain, dropping it
	 * into the neighbouring band. Half-step edges make that tie unreachable. Same defect class as the
	 * A0-80 staircase statistic.
	 *
	 * WHY THIS IS COMPUTED RATHER THAN A TABLE OF LITERALS. The edges depend on the product of the scale
	 * argument and the reference. Freezing them would make a colour mean a different number of crossings
	 * the moment either moved, which is exactly the defect class removed on 2026-08-03, where band meaning
	 * tracked building size.
	 *
	 * WHAT TO PASS, AND THIS CHANGED ON 2026-08-10. The live caller
	 * (AHeatmapPixelTextureVisualizer::RefreshTrajectoryCrossingBands) passes the STROKE WIDTH in metres,
	 * NOT the cell size. The encode reads the PRESENTATION array, and BuildKernel's splat is
	 * mass-conserving, so a stroke wider than one cell divides a crossing's mass by cellSide / width;
	 * folding that in cancels the cell side out of the edge entirely. The width is therefore the physical
	 * quantity a count refers to - "person-passes through a width-wide corridor" - and the cell is free to
	 * be dialled for silhouette quality without moving a colour. Passing the effective cell size instead
	 * was correct only while the two were locked equal, and reverting to it now would rescale every edge
	 * by width/cell; T-BAND-5 gates that. The parameter keeps its CellSideMetres name because the algebra
	 * below is unchanged and the two arguments are dimensionally identical.
	 *
	 * REPRESENTABILITY. The top edge is 4.5 / (s * Reference), so the full ladder fits the [0,1] channel
	 * only when (s * Reference) >= 4.5. At the shipping configuration - s = 0.1 m, Reference = 100
	 * person/m - the product is 10 and the edges land at 0.15 / 0.25 / 0.35 / 0.45, i.e. bytes 38 / 64 /
	 * 89 / 115, comfortably apart. A COARSER grid is always safe. A much finer one saturates early (at
	 * s = 0.01 m a single crossing already reads full scale) and would need a proportionally larger
	 * reference. Edges are clamped against their predecessor so even a degenerate configuration yields a
	 * monotonic, well-formed chain rather than an unreachable band.
	 *
	 * HOW ACCURATE THE COUNT IS. `s` is the chord of an AXIAL crossing. A path crossing at an angle
	 * traverses a different chord, and for isotropic (randomly oriented) traffic the mean chord of a
	 * square is Cauchy's pi*A/P = pi*s/4 ~= 0.785 s - so an open-floor crossing reads about 0.785 instead
	 * of 1.0, an UNDER-count of 21.5%. The error is one-sided; it never reads high. Real egress flow is
	 * channelised by walls and so close to axial, where the count is exact, which is why the divisor stays
	 * `s` and the bias is DOCUMENTED rather than corrected: a blanket 4/pi correction would over-count
	 * every corridor, and corridors are where the traffic is. Label the output "approximately N
	 * crossings", never "exactly N"; the quantity is an axial-equivalent crossing count.
	 *
	 * @param StrokeWidthMetres      The DISPLAY STROKE WIDTH in metres, NOT the cell side. Renamed
	 *                               2026-08-11: it was called CellSideMetres, the shipping call site has
	 *                               passed TrajectoryDisplayPathWidthCm since the kernel was decoupled from
	 *                               the grid, and the stale name is what led the Route Exposure twin below
	 *                               to be wired with the cell side and come out 3x too demanding. The width
	 *                               is correct because EncodeToDisplay reads the splat-DILUTED presentation,
	 *                               not the canonical cell: the dilution is (s / w), so s cancels and the
	 *                               surviving length scale is w. Must be finite and > 0.
	 * @param ReferenceUsageDensity  The person/m that EncodeToDisplay maps to byte 255, i.e.
	 *                               FTrajectoryFieldConfig::ReferenceUsageDensity.
	 */
	static FHeatmapLOSBands TrajectoryCrossings(float StrokeWidthMetres, float ReferenceUsageDensity,
	                                            float RouteThresholdCrossings = 0.0f)
	{
		FHeatmapLOSBands Bands;

		// Reserved for "no data". EncodeToDisplay floors any positive cell to byte 1, so byte 0 - and only
		// byte 0 - falls below half a byte. Independent of the grid, so it is set before the guard.
		Bands.BandA = 0.5f / 255.0f;

		const float Denominator = StrokeWidthMetres * ReferenceUsageDensity;
		if (!FMath::IsFinite(Denominator) || Denominator <= 0.0f)
		{
			// Arithmetic cannot recover this: with no valid cell size there is no crossing to count. Fall
			// back to the pre-2026-08-10 quantile edges, which are at least monotonic and in range. A
			// caller landing here is pushing bands for a field that is not sized yet.
			return Trajectory();
		}

		// Each edge is clamped against the PREVIOUS one rather than merely against zero. The comparison
		// chain is evaluated in order, so a non-monotonic set would make a band unreachable rather than
		// merely misplaced. Written out rather than looped: four lines is fewer than the loop that would
		// replace them, and the numerators are the half-steps the derivation above names.
		// D-E — the 0/1 edge is the ROUTE THRESHOLD, not an is-it-nonzero test.
		//
		// Every other edge here is a half-step on the crossing scale. This one was not: it was half a BYTE,
		// which only ever asked "is this cell zero". So a cell holding a sliver of kernel spill painted the
		// same colour as one holding a full crossing, and the drawn stroke was as wide as the kernel's
		// support rather than as wide as the stroke — 60 cm for a 45 cm path on 20 cm cells, swinging by a
		// whole cell as the path slid across the lattice.
		//
		// The replacement is DERIVED from the kernel that draws the stroke, not fitted to one configuration
		// — see FTrajectoryField::DeriveRouteThresholdCrossings. A caller that passes 0 keeps the old
		// is-it-nonzero behaviour, which is what the degenerate and legacy paths want.
		//
		// Floored at half a byte so byte 0, and only byte 0, is guaranteed to read as "no data" whatever
		// the threshold works out to. Clamped under BandB so the chain cannot invert.
		if (RouteThresholdCrossings > 0.0f)
		{
			Bands.BandA = FMath::Max(Bands.BandA, RouteThresholdCrossings / Denominator);
		}

		Bands.BandB = FMath::Clamp(1.5f / Denominator, Bands.BandA, 1.0f); // ~1 crossing
		Bands.BandC = FMath::Clamp(2.5f / Denominator, Bands.BandB, 1.0f); // ~2
		Bands.BandD = FMath::Clamp(3.5f / Denominator, Bands.BandC, 1.0f); // ~3
		Bands.BandE = FMath::Clamp(4.5f / Denominator, Bands.BandD, 1.0f); // ~4; at/above this is LOS_F, 5+

		return Bands;
	}

	/**
	 * Free walking speed on the level, metres/second. NAMED, not a literal, because which one is correct
	 * is set by JURISDICTION and not by preference — see TrajectoryTransits.
	 *
	 * SFPE: Nelson & MacLennan, SFPE Handbook, "Emergency Movement", S = k(1 - 0.266 D), k = 1.40 m/s level.
	 * The Mobius audience is NZ fire engineering under C/VM2, which expects the SFPE hydraulic model, and
	 * the tenability pipeline already imports from the same handbook.
	 */
	static constexpr float FreeWalkSpeedSFPE = 1.40f;

	/**
	 * The EU / DACH alternative, kept documented beside the shipping value so the anchor can be swapped if
	 * the audience ever changes. Weidmann (1993), Transporttechnik der Fussgaenger, ETH Zuerich:
	 * v = v_free * [1 - exp(-1.913 * (1/rho - 1/5.4))], v_free = 1.34 m/s. Entrenched in the EU via RiMEA.
	 * Swapping to it moves t0 and the person-second column by about 4%. IMO rejects both.
	 */
	static constexpr float FreeWalkSpeedWeidmann = 1.34f;

	/**
	 * Route Exposure banded in TRANSIT-EQUIVALENTS (SPEC_TrajectorySurfaces §5.2).
	 *
	 * A cell accrues person-seconds whenever anyone is inside it, moving or not, so raw seconds are not
	 * comparable between captures of different length or between cells of different size. The anchor that
	 * fixes that is the time ONE person at free walking speed takes to cross ONE cell:
	 *
	 *     t0       = CellSideMetres / v_free
	 *     Transits = PersonSeconds / t0
	 *
	 * One transit reads as "one person crossed this cell at normal walking pace". Two is either two people
	 * or one person at half speed — the same countable UX as crossings, anchored on a published free speed.
	 *
	 * ⚠️ **THE ANCHOR IS PRINCIPLED; THE STEPS ARE A CHOICE, AND THE UI MUST SAY SO.** Crossings work
	 * because a crossing is a discrete countable event. Person-seconds is a product of count x time and no
	 * physical principle fixes the count, so the STEPS below are a readability choice and there is no
	 * published standard for this surface. What changed on 2026-08-14 (owner ruling D5) is that the choice
	 * is now made against a STATED PHYSICAL TARGET instead of by feel — see the calibration block below.
	 * Still say "relative, not thresholds" in the legend; do not say "uncalibrated".
	 *
	 *   A  0         nobody was ever here      D  <= 40    noticeably delayed   (~10 s)
	 *   B  <= 4      free-flow pass-through    E  <= 120   queueing             (~30 s)
	 *   C  <= 12     light use / slight slow   F  >  120   stationary / blocked
	 *
	 * CALIBRATION — WHAT THE STEPS WERE CHOSEN AGAINST (D5, 2026-08-14). The readable physical quantity is
	 * how long ONE STATIONARY person must stand on a spot to reach an edge. That conversion is exact,
	 * needs no cell size and no walking speed, and is StandingDwellSecondsAtEdge() below:
	 *
	 *     seconds = NormalisedEdge * ReferenceExposureDensity * (pi * w^2 / 4)
	 *
	 * because a stationary agent deposits its person-seconds into a DISC of diameter w — area pi*w^2/4 —
	 * and the display value is a density. Equivalently, on the transit scale, seconds = T * pi*w / (4*v).
	 * At the shipping 45 cm stroke and Ref = 240 the ladder therefore reads:
	 *
	 *     T =   4  ->  1.01 s        T =  40  ->  10.10 s
	 *     T =  12  ->  3.03 s        T = 120  ->  30.29 s
	 *
	 * i.e. approximately 1 / 3 / 10 / 30 seconds of standing, which is the ruling. The step ratios stay
	 * roughly x3 for the same reason as before: a standing agent racks up transits far faster than a
	 * crossing accrues.
	 *
	 * WHY THE PREVIOUS 2 / 5 / 15 / 50 WAS RE-TUNED, from the pixels rather than from taste. Under the same
	 * conversion its edges were 0.50 / 1.26 / 3.79 / 12.62 s. Band F therefore opened at 12.6 seconds, so
	 * everything from thirteen seconds to ten minutes painted the SAME colour — inside exactly the queueing
	 * regime this surface exists to show. At the other end, band B closed at half a second, so an ordinary
	 * walking pause already left "free-flow". Both ends were compressed into the range nobody is asking
	 * about. Byte separation moved the same way: the old edges quantised to bytes 3 / 8 / 25 / 84, where a
	 * one-byte rounding is 17% of the B edge; the new ones land at 7 / 20 / 67 / 202.
	 *
	 * THE DISC IS NOT AN APPROXIMATION AT THE SHIPPING CONFIGURATION, which is what makes the seconds
	 * figure quotable. BuildKernel's disc-coverage rule gives the centre tap weight
	 * Area(disc intersect centre cell) / (pi*R^2) with R = w/(2s) = 1.5 texels. The centre cell's farthest corner
	 * sits sqrt(2) = 1.4142 texels from the disc centre for ANY sub-cell phase up to half a cell, and
	 * 1.4142 < 1.5, so the cell is FULLY covered and the weight is exactly 1/(pi*R^2) = 0.1414711 whatever
	 * the sub-cell placement. The dwell figure is therefore phase-invariant, not a best case.
	 * T_BAND_8_ExposureDwellLadder gates both halves of that.
	 *
	 * DERIVATION OF THE EDGE. CORRECTED 2026-08-11 — the version below used to stop at the canonical cell
	 * and concluded edge(T) = T / (s * Ref * v_free), which made every edge (w / s) too demanding: 3x at
	 * the shipping 45 cm stroke on 15 cm cells. EncodeToDisplay does not read the canonical array. It reads
	 * the PRESENTATION, which the mass-conserving splat has already diluted by (s / w) — the same dilution
	 * Route Usage gets, because it is the same kernel. Carrying that through:
	 *
	 *     RVal     = Presented / (s^2 * Ref),  Presented = P * (s / w)  =>  RVal = P / (s * w * Ref)
	 *     Transits = P * v_free / s                                    =>  P    = T * s / v_free
	 *     edge(T)  = T / (w * Ref * v_free)
	 *
	 * The cell side CANCELS. t0's per-cell dependence is exactly undone by the per-cell dilution, so the
	 * surviving length scale is the STROKE WIDTH — the same scale TrajectoryCrossings uses, which is also
	 * what makes the two ladders comparable to one another.
	 *
	 * REPRESENTABILITY — BINDING AGAIN AFTER THE D5 RE-TUNE, and it is what bounds any future one. The top
	 * edge fits the [0,1] channel only when w * Ref * v_free >= the top step. At w = 0.45 and v_free = 1.40
	 * that ceiling is 151.2 transits = 38.17 s of standing, so the new top step of 120 (30.29 s) sits at
	 * RVal 0.7937 with headroom, but a ladder asking for a 60-second top band would NOT fit and would need
	 * Ref >= 378. Raising the reference is a SEPARATE decision — it rescales every byte on the surface —
	 * so do not smuggle one in to buy a rounder top number.
	 *
	 * The old note here said the constraint was gone. That was true of the OLD ladder only: with a top step
	 * of 50 the bar was Ref >= 79.4, cleared by anything. Under the cell-side denominator that predated
	 * 2026-08-11 the same test read Ref >= 238.1, which is the only reason
	 * FTrajectoryFieldConfig::ReferenceExposureDensity was ever raised 200 -> 240. It stays at 240 — now
	 * because 200 would put the ceiling at 31.8 s and leave the 30.29 s top edge with almost none.
	 *
	 * @param StrokeWidthMetres The DISPLAY STROKE WIDTH, not the cell side — see the derivation above.
	 *                          Passing the cell side is the defect this rename exists to prevent.
	 * @param ReferenceExposureDensity  person*s/m^2 that EncodeToDisplay maps to byte 255.
	 * @param FreeWalkSpeed     v_free. Pass FreeWalkSpeedSFPE unless the audience is EU.
	 * @param RouteThresholdTransits  D-E route threshold in transits; 0 keeps the is-it-nonzero edge.
	 */
	static FHeatmapLOSBands TrajectoryTransits(float StrokeWidthMetres, float ReferenceExposureDensity,
	                                           float FreeWalkSpeed = FreeWalkSpeedSFPE,
	                                           float RouteThresholdTransits = 0.0f)
	{
		FHeatmapLOSBands Bands;
		Bands.BandA = 0.5f / 255.0f;

		const float Denominator = StrokeWidthMetres * ReferenceExposureDensity * FreeWalkSpeed;
		if (!FMath::IsFinite(Denominator) || Denominator <= 0.0f)
		{
			// No valid stroke width or anchor means there is no transit to count, so there is no honest
			// banding to return. Render nothing rather than a plausible scale — a caller landing here is
			// banding an unsized field, and the previous behaviour (a frozen quantile set built against a
			// reference that has since changed) made that look like real data.
			return Unbanded();
		}

		// Same reasoning as TrajectoryCrossings: the 0/1 edge is the ROUTE THRESHOLD, not an is-it-nonzero
		// test, or every cell the splat kernel grazes paints at full band-B colour and the drawn stroke is
		// as wide as the kernel support instead of as wide as the stroke.
		if (RouteThresholdTransits > 0.0f)
		{
			Bands.BandA = FMath::Max(Bands.BandA, RouteThresholdTransits / Denominator);
		}

		// Geometric, and clamped against the previous edge so a degenerate configuration yields an
		// unreachable-free monotonic chain rather than a scrambled one.
		//
		// D5, 2026-08-14: 2 / 5 / 15 / 50 -> 4 / 12 / 40 / 120. These are the transit values that put the
		// edges at approximately 1 / 3 / 10 / 30 seconds of standing at the shipping stroke; the seconds are
		// the ruled quantity and these are derived from them, not the other way round. Re-tuning means
		// picking new SECONDS and converting with StandingDwellSecondsAtEdge below — and checking the top
		// step still clears the representability ceiling in the doc comment.
		Bands.BandB = FMath::Clamp(4.0f / Denominator, Bands.BandA, 1.0f);
		Bands.BandC = FMath::Clamp(12.0f / Denominator, Bands.BandB, 1.0f);
		Bands.BandD = FMath::Clamp(40.0f / Denominator, Bands.BandC, 1.0f);
		Bands.BandE = FMath::Clamp(TransitLadderTopStep / Denominator, Bands.BandD, 1.0f);

		return Bands;
	}

	/**
	 * The smallest ReferenceExposureDensity at which TrajectoryTransits' full ladder fits the [0,1]
	 * channel, i.e. band F stays reachable. Exposed so a gate can assert the shipping default clears it
	 * rather than transcribing a number that goes stale the moment the cell is dialled.
	 */
	static float MinimumExposureReferenceForFullLadder(float StrokeWidthMetres,
	                                                   float FreeWalkSpeed = FreeWalkSpeedSFPE)
	{
		// Takes the SAME length scale as TrajectoryTransits, and must keep doing so — a gate that fed this
		// the cell side while the ladder used the width would compute a threshold for a configuration that
		// does not exist and pass while band F was unreachable. That is precisely the failure it exists to
		// catch. At w = 0.45 and v_free = 1.40 this returns 190.5.
		//
		// The top step is NAMED rather than repeated as a literal. It was a literal 50 until 2026-08-14, and
		// the D5 re-tune to 120 would have left this function computing the bar for a ladder that no longer
		// exists — reporting 79.4 while band F actually needed 190.5, i.e. passing a configuration where the
		// top band is unreachable. That is the exact failure mode the comment above claims it catches.
		const float Divisor = StrokeWidthMetres * FreeWalkSpeed;
		return (Divisor > 0.0f) ? (TransitLadderTopStep / Divisor) : 0.0f;
	}

	/**
	 * Person-seconds ONE STATIONARY agent must accrue on a spot for that cell to reach `NormalisedEdge`.
	 * The physical reading of an exposure band, and the quantity the D5 ruling picked the ladder against.
	 *
	 *     seconds = NormalisedEdge * ReferenceExposureDensity * (pi * w^2 / 4)
	 *
	 * DERIVATION, and why neither the cell size nor the walking speed appears. The display value is a
	 * DENSITY (person*s/m^2) once normalised out by the reference, and a stationary agent's person-seconds
	 * land in a disc of diameter w — the splat kernel is a disc of radius w/2 — whose area is pi*w^2/4. So
	 * the cell's density is (seconds / disc area) and inverting gives the line above. The cell size drops
	 * out because the kernel's centre weight 1/(pi*R^2) carries an s^2 that the per-cell area divides back
	 * out; v_free drops out because nobody is walking. Cross-check on the transit scale, where both do
	 * appear and then cancel: seconds = T * pi*w / (4*v_free) = T * 0.25245 s at the shipping configuration.
	 *
	 * ⚠️ HONEST ONLY FOR ONE STATIONARY PERSON. A cell reading 10 s could equally be two people standing
	 * for five, or forty walking passes. Person-seconds cannot distinguish those and neither can this. Any
	 * UI printing these MUST carry that qualifier — see FHeatmapLegend::RouteExposure.
	 *
	 * EXACT, NOT APPROXIMATE, AT THE SHIPPING CONFIGURATION. See the phase-invariance argument on
	 * TrajectoryTransits: with R = 1.5 texels the centre cell is fully covered at every sub-cell phase, so
	 * the centre weight is exactly 1/(pi*R^2). Below R = sqrt(2) = 1.4142 texels — i.e. a stroke narrower
	 * than 2*sqrt(2) cells — the disc stops covering the centre cell at half-cell phase, the true weight
	 * becomes phase-dependent and drops below the flat-disc value, and this then returns a LOWER bound on
	 * the seconds. Flagged rather than corrected: the shipping 45 cm on 15 cm cells gives R = 1.5 and is not
	 * near that boundary, and a phase-averaged figure would be less quotable, not more.
	 *
	 * @param NormalisedEdge   A band edge in [0,1], i.e. one of FHeatmapLOSBands' members.
	 * @param StrokeWidthMetres  The DISPLAY STROKE WIDTH, same scale as everywhere else on this struct.
	 * @param ReferenceExposureDensity  person*s/m^2 that EncodeToDisplay maps to byte 255.
	 */
	static float StandingDwellSecondsAtEdge(float NormalisedEdge, float StrokeWidthMetres,
	                                        float ReferenceExposureDensity)
	{
		const float DiscAreaSquareMetres = PI * StrokeWidthMetres * StrokeWidthMetres * 0.25f;
		return NormalisedEdge * ReferenceExposureDensity * DiscAreaSquareMetres;
	}

	/**
	 * "No valid banding" — every edge at 1.0, so every cell reads as LOS_A (the no-data colour) and the
	 * surface renders EMPTY. Use where a band set is structurally required but cannot honestly be derived.
	 *
	 * This replaces a frozen quantile set (`TrajectoryExposure()`, deleted 2026-08-13) whose four edges
	 * were arithmetically 200-based — 0.0648/0.1531/0.3137/0.6337 are 12.95/30.62/62.74/126.74 divided by
	 * **200** — while ReferenceExposureDensity has been **240** since 2026-08-10. Decoded against the live
	 * reference those edges meant 15.55/36.7/75.3/152.1, so the legend was 20 % adrift from the pixels. It
	 * was reachable two ways: as the fallback in TrajectoryTransits, and as the member initialiser for
	 * AHeatmapPixelTextureVisualizer::TrajectoryExposureLOSBands, i.e. the value in force until the field
	 * is first sized.
	 *
	 * An empty surface is the deliberate choice over a plausible one. The failure this guards is a map
	 * that looks authoritative and is silently mis-scaled; a blank surface is wrong in a way somebody
	 * notices. Do NOT "improve" this by substituting a nice-looking default — that is the deleted bug.
	 *
	 * (Only a fully saturated cell, normalised exactly 1.0, takes LOS_F, since bands are "first edge the
	 * value is strictly below". That is intentional: saturation is still worth seeing.)
	 */
	static FHeatmapLOSBands Unbanded()
	{
		FHeatmapLOSBands Bands;
		Bands.BandA = 1.0f;
		Bands.BandB = 1.0f;
		Bands.BandC = 1.0f;
		Bands.BandD = 1.0f;
		Bands.BandE = 1.0f;
		return Bands;
	}
};
