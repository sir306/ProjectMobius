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
 *  - The trajectory surface stores cumulative passage count: a texel is seeded at
 *    TrajectoryMinimumVisibleValue on first visit and incremented per pass. Nothing ever lands between
 *    zero and the seed, so reusing the density edges paints the first several passes with the same
 *    colour as bare floor. Its band A edge must instead sit BELOW the seed, which reserves LOS_A as a
 *    genuine "no data" colour and leaves five bands for real data.
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
