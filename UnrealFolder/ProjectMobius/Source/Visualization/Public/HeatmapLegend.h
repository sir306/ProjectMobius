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

// Deliberately does NOT include TrajectoryField.h just to name ETrajectoryMapMode. This header is
// reachable from the widget module, and TrajectoryField.h drags the whole field implementation surface
// with it. Three named factories cost less than one enum parameter here; the CALLER already knows which
// surface it is displaying, because it is the thing that switched it.

#include "CoreMinimal.h"

#include "HeatmapLOSBands.h"

#include "HeatmapLegend.generated.h"

/**
 * One printed row of the heatmap key: the band letter, an optional comparison glyph, and the value.
 *
 * The glyph is a SEPARATE field rather than baked into Value because it is not decoration - it flips
 * with the surface. On the density key the top row reads "> 3.24" and the bottom "< 0.46", because more
 * m^2/person is emptier. On the trajectory keys the top row is "no data" and it is the BOTTOM row that
 * carries ">", because more crossings is busier. A widget that concatenated its own glyph would have to
 * know which surface it was drawing, which is exactly the knowledge this type exists to supply.
 */
USTRUCT(BlueprintType)
struct VISUALIZATION_API FHeatmapLegendRow
{
	GENERATED_BODY()

	/** "A" .. "F". Supplied rather than assumed so the row order cannot be reversed by accident. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText Band;

	/** ">", "<", "~", or empty. Print it to the LEFT of Value. Never empty on the first and last rows. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText Qualifier;

	/** Already formatted for display - do not re-round it. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText Value;

	/**
	 * What this band MEANS, in words - "queueing", "about one person passed".
	 *
	 * Bind it to the row's ToolTipText. A bare number in an unfamiliar unit is the thing that made the
	 * trajectory keys unreadable: "5" answers nothing on its own, and the unit header cannot carry a
	 * per-band meaning. Short enough to sit in a row if the layout ever gains a column for it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText Description;
};

/**
 * Everything the colour-band key needs to describe the surface currently on screen.
 *
 * Rows are ALWAYS six, A..F, in that order, whenever bHasData is true. The widget can therefore bind
 * positionally without a length check.
 */
USTRUCT(BlueprintType)
struct VISUALIZATION_API FHeatmapLegendContents
{
	GENERATED_BODY()

	/** Replaces "Level of service". */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText Title;

	/** Bind to the title's ToolTipText. One sentence on what the surface measures. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText TitleTooltip;

	/** The left column header. "Band" on every surface - supplied so the widget has one source, not two. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText BandHeader;

	/** Replaces "m^2/person". Carries the UNIT, which is what actually changes between surfaces. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText ValueHeader;

	/**
	 * Bind to the value header's ToolTipText. Defines the unit, and carries the honesty caveats that will
	 * not fit in a column heading - the axial under-count on Route Usage, and the fact that the Route
	 * Exposure steps are an uncalibrated readability choice rather than a standard.
	 *
	 * These caveats are documented in FHeatmapLOSBands and nowhere a user can see. A stakeholder reading
	 * the surface off a screen is exactly the person they are about; this is the only place they reach them.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	FText ValueHeaderTooltip;

	/** Six rows, A..F, when bHasData. Empty otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	TArray<FHeatmapLegendRow> Rows;

	/**
	 * False when the surface has no honest banding to describe - see FHeatmapLOSBands::Unbanded().
	 *
	 * ⚠️ The widget MUST branch on this rather than printing Rows regardless. An unbanded surface renders
	 * EMPTY on purpose, and inverting its edges yields six plausible-looking identical numbers. A key that
	 * printed them would restore precisely the failure Unbanded() exists to make visible: an authoritative
	 * legend over a blank map. Hide the rows, or show "no data".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Heatmap|Legend")
	bool bHasData = false;
};

/**
 * Builds the printed key for a heatmap surface by INVERTING its live band edges back into the unit the
 * surface is measured in.
 *
 * WHY INVERT RATHER THAN TABULATE. The edges are the thing the material and the PNG colouriser actually
 * compare against, and on the trajectory surfaces they are computed from the stroke width and the
 * reference density rather than stored. A hand-written table of "3.24 / 2.32 / ..." would be a second
 * copy that agrees with the render only until somebody dials a reference - which is the defect shape that
 * has already produced two separate heatmap bugs (the 3x-too-demanding exposure edges, and a legend that
 * sat 20% off the pixels for three days). Inverting means the key cannot disagree with the surface: if
 * the edges move, the printed numbers move with them.
 *
 * The inverse of each surface's encode, in one place:
 *
 *   density   normalised = (1 / value) / FruinMaxDensityPersonsPerSqM   =>  value = 1 / (edge * Max)
 *   usage     normalised = crossings / (width * Ref)                    =>  crossings = edge * width * Ref
 *   exposure  normalised = transits / (width * Ref * v_free)            =>  transits  = edge * width * Ref * v
 *
 * Each factory takes the SAME arguments the matching FHeatmapLOSBands factory was handed. Pass anything
 * else and the key describes a configuration that is not on screen.
 */
struct VISUALIZATION_API FHeatmapLegend
{
	/**
	 * Fruin Level of Service, printed in m^2/person.
	 *
	 * Reads LOW to HIGH density down the table, so band A is the EMPTIEST and carries ">" while band F is
	 * the most crowded and carries "<". Both extreme rows are open-ended, which is why F reuses band E's
	 * number rather than inventing a floor.
	 *
	 * ⚠️ Pass the bands the DENSITY surface is actually using. Today the density colouriser compares
	 * against the LOS_*_BAND macros in DynamicPixelRenderingTexture.cpp rather than against a live
	 * FHeatmapLOSBands, and those macros hold the same five numbers as FHeatmapLOSBands::Density(). The two
	 * are equal by duplication, not by construction. Nothing gates that, so if the macros are ever edited
	 * this key goes quietly stale - the one place in this file where inverting does not buy immunity.
	 */
	static FHeatmapLegendContents Density(const FHeatmapLOSBands& Bands);

	/**
	 * Route Usage, printed as a countable crossing count.
	 *
	 * The stored edges sit at N + 0.5 so a cell holding exactly N crossings cannot land on an equality tie
	 * (see FHeatmapLOSBands::TrajectoryCrossings). Those half-steps are a comparison device, not a meaning,
	 * so the printed value is the integer each band CONTAINS - the half is taken back off here rather than
	 * shown to a stakeholder.
	 *
	 * Band A is the route threshold, below which a cell is not on the route at all, so it prints 0 and the
	 * count is deliberately not shown - the threshold is a fraction of a crossing and printing it would
	 * invite the reader to treat it as a level.
	 *
	 * The header says "crossings", never "exactly N": the divisor is the AXIAL chord, so isotropic
	 * open-floor traffic under-reads by about 21.5%. One-sided - it never reads high.
	 *
	 * @param Bands                  The live TrajectoryLOSBands, as pushed to the material.
	 * @param StrokeWidthMetres      TrajectoryDisplayPathWidthCm / 100, NOT the cell side.
	 * @param ReferenceUsageDensity  FTrajectoryFieldConfig::ReferenceUsageDensity.
	 */
	static FHeatmapLegendContents RouteUsage(const FHeatmapLOSBands& Bands, float StrokeWidthMetres,
	                                         float ReferenceUsageDensity);

	/**
	 * Route Exposure, printed in transit-equivalents.
	 *
	 * Unlike the crossing ladder these edges are UPPER BOUNDS and are printed as such: band B is "up to 2
	 * transits", band F is "more than 50". The geometric spacing is deliberate - a standing agent racks up
	 * hundreds of transits while a crossing is one.
	 *
	 * ⚠️ THE LADDER IS A READABILITY CHOICE WITH NO PUBLISHED STANDARD, and owner decision D5 has not been
	 * made. Only the anchor is cited (v_free, Nelson & MacLennan, SFPE). Whatever the key ends up saying
	 * about this surface must not imply otherwise.
	 *
	 * @param Bands                     The live TrajectoryExposureLOSBands, as pushed to the material.
	 * @param StrokeWidthMetres         TrajectoryDisplayPathWidthCm / 100, NOT the cell side.
	 * @param ReferenceExposureDensity  FTrajectoryFieldConfig::ReferenceExposureDensity.
	 * @param FreeWalkSpeed             The same v_free the bands were built with.
	 */
	static FHeatmapLegendContents RouteExposure(const FHeatmapLOSBands& Bands, float StrokeWidthMetres,
	                                            float ReferenceExposureDensity, float FreeWalkSpeed);
};
