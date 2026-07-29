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
	 * Occupancy edges for the trajectory surface, calibrated against the byte one agent crossing a texel
	 * actually leaves. Each edge sits on a half-byte so no stored value lands exactly on a comparison
	 * boundary.
	 *
	 *   BandA  bytes   0-24   no data (nothing can land here but an untouched texel)
	 *   BandB  bytes  25-46   ~1 crossing
	 *   BandC  bytes  47-71   ~2 crossings
	 *   BandD  bytes  72-110  ~3 crossings
	 *   BandE  bytes 111-175  ~4-5 crossings
	 *   LOS_F  bytes 176+     6+ crossings
	 *
	 * BandA is structural: it is the fix for empty floor and first-visit sharing one colour, and must
	 * stay below the seed. The rest are calibration and can be retuned; they are parameters for exactly
	 * that reason.
	 *
	 * DO NOT derive these from the calibration test's straight-line model. That model draws one
	 * DrawLineWithMinimumRed call per "pass" and predicts 3 hits, byte 27. The real pipeline emits one
	 * segment per agent per flush (~0.1 s) and Bresenham-walks each with a 3x3 brush, so an agent
	 * crossing a single texel is stamped by several consecutive flushes. Replaying a real 30 s
	 * ground-floor capture (530,008 drawn segments, replay reproduces the stored byte on 99.6 % of
	 * texels) measured a lone crossing at a median 13 hits -- byte 37, with p10 28 and p90 49. An
	 * earlier edge set built on the 3-hit assumption put a typical single crossing in the *third* band,
	 * which is what made one quick walk-through render as a green streak.
	 *
	 * The quantity is occupancy, not a pass count: a slow crossing deposits as much as several fast
	 * ones, so the per-crossing distributions overlap by design.
	 *
	 * These values are duplicated as the defaults of the LOS_*_Band scalar parameters on
	 * M_HeatmapRT_Trajectory (built by MobiusPerf/BuildTrajectoryHeatmapMaterial.py). The two must
	 * agree; AHeatmapPixelTextureVisualizer::ApplyTrajectoryLOSBands is what keeps them in step at
	 * runtime.
	 */
	static FHeatmapLOSBands Trajectory()
	{
		FHeatmapLOSBands Bands;
		Bands.BandA = 24.5f / 255.0f;  // 0.096078
		Bands.BandB = 46.5f / 255.0f;  // 0.182353
		Bands.BandC = 71.5f / 255.0f;  // 0.280392
		Bands.BandD = 110.5f / 255.0f; // 0.433333
		Bands.BandE = 175.5f / 255.0f; // 0.688235
		return Bands;
	}
};
