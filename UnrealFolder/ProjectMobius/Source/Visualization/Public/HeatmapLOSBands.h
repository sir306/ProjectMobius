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
	 * WHY THIS IS COMPUTED RATHER THAN A TABLE OF LITERALS. The edges depend on the product of cell size
	 * and reference. Freezing them would make a colour mean a different number of crossings the moment
	 * TrajectoryWorldCmPerTexel changed - or, worse, the moment FTrajectoryField's D2b clamp silently
	 * RAISED cm/texel to honour MaxGridDim on a large floor, which no caller asks for and nothing would
	 * flag. That is exactly the defect class removed on 2026-08-03, where band meaning tracked building
	 * size. Pass the EFFECTIVE cell size (GetEffectiveCmPerTexel() / 100), never the requested one.
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
	 * @param CellSideMetres         Effective cell side in metres. Must be finite and > 0.
	 * @param ReferenceUsageDensity  The person/m that EncodeToDisplay maps to byte 255, i.e.
	 *                               FTrajectoryFieldConfig::ReferenceUsageDensity.
	 */
	static FHeatmapLOSBands TrajectoryCrossings(float CellSideMetres, float ReferenceUsageDensity)
	{
		FHeatmapLOSBands Bands;

		// Reserved for "no data". EncodeToDisplay floors any positive cell to byte 1, so byte 0 - and only
		// byte 0 - falls below half a byte. Independent of the grid, so it is set before the guard.
		Bands.BandA = 0.5f / 255.0f;

		const float Denominator = CellSideMetres * ReferenceUsageDensity;
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
		Bands.BandB = FMath::Clamp(1.5f / Denominator, Bands.BandA, 1.0f); // ~1 crossing
		Bands.BandC = FMath::Clamp(2.5f / Denominator, Bands.BandB, 1.0f); // ~2
		Bands.BandD = FMath::Clamp(3.5f / Denominator, Bands.BandC, 1.0f); // ~3
		Bands.BandE = FMath::Clamp(4.5f / Denominator, Bands.BandD, 1.0f); // ~4; at/above this is LOS_F, 5+

		return Bands;
	}

	/**
	 * The same treatment for Route Exposure, normalised against ReferenceExposureDensity
	 * (200 person*s/m^2). Exposure needs its OWN set: it is a different quantity with a different
	 * reference, so reusing the Usage edges would mis-band it by the ratio of the two references.
	 *
	 *   BandA..BandB  <= 12.95 person*s/m^2  p50
	 *   BandB..BandC  <= 30.62               p75
	 *   BandC..BandD  <= 62.74               p90
	 *   BandD..BandE  <= 126.74              p97
	 *
	 * Same capture and method as Trajectory(), person_seconds column. PROVISIONAL under D9.
	 */
	static FHeatmapLOSBands TrajectoryExposure()
	{
		FHeatmapLOSBands Bands;
		Bands.BandA = 0.5f / 255.0f; // no-data threshold, as above
		Bands.BandB = 0.0648f;       //  12.95 person*s/m^2 (p50)
		Bands.BandC = 0.1531f;       //  30.62 person*s/m^2 (p75)
		Bands.BandD = 0.3137f;       //  62.74 person*s/m^2 (p90)
		Bands.BandE = 0.6337f;       // 126.74 person*s/m^2 (p97)
		return Bands;
	}
};
