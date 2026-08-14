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

#include "HeatmapLegend.h"

#define LOCTEXT_NAMESPACE "HeatmapLegend"

namespace
{
	/** "A" .. "F", in the order the key prints them. */
	const TCHAR* const BandLetters[6] = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F") };

	/**
	 * The five stored edges as an array, so the inversions below can loop instead of repeating themselves
	 * five times with the one transposed letter that a reader never spots.
	 */
	void EdgesOf(const FHeatmapLOSBands& Bands, double(&Out)[5])
	{
		Out[0] = Bands.BandA;
		Out[1] = Bands.BandB;
		Out[2] = Bands.BandC;
		Out[3] = Bands.BandD;
		Out[4] = Bands.BandE;
	}

	/**
	 * Unbanded() is "every edge at 1.0", and it is the ONLY band set that means "do not draw a key". Detect
	 * it by its shape rather than by re-deriving whatever produced it: the callers that return it are in
	 * two different factories and a third could be added, but the value is the contract.
	 */
	bool IsUnbanded(const FHeatmapLOSBands& Bands)
	{
		double Edges[5];
		EdgesOf(Bands, Edges);
		for (const double Edge : Edges)
		{
			if (!FMath::IsNearlyEqual(Edge, 1.0, UE_DOUBLE_KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		return true;
	}

	/** Fixed decimals, then trailing zeros and a bare point trimmed - "2" reads better than "2.00". */
	FText FormatValue(double Value, int32 Decimals)
	{
		FString Text = FString::Printf(TEXT("%.*f"), Decimals, Value);
		if (Decimals > 0 && Text.Contains(TEXT(".")))
		{
			while (Text.EndsWith(TEXT("0")))
			{
				Text.LeftChopInline(1);
			}
			// Only after the zeros are gone, or "20.0" would keep its point.
			Text.RemoveFromEnd(TEXT("."));
		}
		return FText::FromString(Text);
	}

	FHeatmapLegendRow MakeRow(int32 Index, const TCHAR* Qualifier, double Value, int32 Decimals,
		const FText& Description)
	{
		FHeatmapLegendRow Row;
		Row.Band = FText::FromString(BandLetters[Index]);
		Row.Qualifier = FText::FromString(Qualifier);
		Row.Value = FormatValue(Value, Decimals);
		Row.Description = Description;
		return Row;
	}

	/** Shared by every surface, and deliberately NOT localised differently per surface. */
	FText BandColumnHeader()
	{
		return LOCTEXT("BandHeader", "Band");
	}

	/**
	 * A key with no rows, but with its headings intact. Returned only when the surface genuinely cannot be
	 * banded - NOT when a heatmap merely has not been built yet, which
	 * AHeatmapPixelTextureVisualizer::GetDefaultLegendContents answers with real numbers instead.
	 */
	FHeatmapLegendContents NoData(const FText& Title, const FText& TitleTooltip, const FText& ValueHeader,
		const FText& ValueHeaderTooltip)
	{
		FHeatmapLegendContents Contents;
		Contents.Title = Title;
		Contents.TitleTooltip = TitleTooltip;
		Contents.BandHeader = BandColumnHeader();
		Contents.ValueHeader = ValueHeader;
		Contents.ValueHeaderTooltip = ValueHeaderTooltip;
		Contents.bHasData = false;
		return Contents;
	}
}

FHeatmapLegendContents FHeatmapLegend::Density(const FHeatmapLOSBands& Bands)
{
	// "m^2/person" with a real superscript two. Built from its codepoint rather than typed into the source:
	// a non-ASCII character in a narrow string literal survives the compiler but renders as tofu in Slate,
	// which is a defect that only shows up on screen. See memory reference-ue-nonascii-literal-mojibake.
	const FString SquaredHeader = FString::Printf(TEXT("m%c/person"), static_cast<TCHAR>(0x00B2));

	const FText Title = LOCTEXT("DensityTitle", "Level of service");
	const FText TitleTooltip = LOCTEXT("DensityTitleTip",
		"How crowded the floor is at each moment. Blue is emptiest, red is most crowded.");
	const FText ValueHeader = FText::FromString(SquaredHeader);
	const FText ValueHeaderTooltip = LOCTEXT("DensityHeaderTip",
		"Square metres of floor available per person. LARGER IS EMPTIER, so the scale runs the opposite way "
		"to the other two surfaces: band A is the roomiest and band F the most crowded.\n\n"
		"The band boundaries are the Fruin Level of Service walkway boundaries.");

	if (IsUnbanded(Bands))
	{
		return NoData(Title, TitleTooltip, ValueHeader, ValueHeaderTooltip);
	}

	double Edges[5];
	EdgesOf(Bands, Edges);

	FHeatmapLegendContents Contents;
	Contents.Title = Title;
	Contents.TitleTooltip = TitleTooltip;
	Contents.BandHeader = BandColumnHeader();
	Contents.ValueHeader = ValueHeader;
	Contents.ValueHeaderTooltip = ValueHeaderTooltip;
	Contents.bHasData = true;
	Contents.Rows.Reserve(6);

	// Plain descriptions of what each band LOOKS like on the floor. Written here rather than quoted: the
	// boundaries are Fruin's, the wording is ours, and under the owner's sourcing ruling nothing may read as
	// a quotation from a source that has not been checked.
	const FText Descriptions[6] = {
		LOCTEXT("DensityA", "Free circulation - people choose their own speed and path"),
		LOCTEXT("DensityB", "Near-free flow, occasional need to avoid someone"),
		LOCTEXT("DensityC", "Speed restricted, passing needs deliberate effort"),
		LOCTEXT("DensityD", "Movement restricted, frequent conflicts"),
		LOCTEXT("DensityE", "Shuffling, everyone moves at the crowd's speed"),
		LOCTEXT("DensityF", "Standstill - contact unavoidable, movement only in surges"),
	};

	// value = 1 / (edge * Max). Guarded because an edge of 0 is representable in the UPROPERTY's clamp and
	// would divide by zero here; it cannot happen with any shipping set, which is exactly why an unguarded
	// division would sit undetected until the one day somebody typed a 0 into the details panel.
	auto SquareMetresPerPerson = [](double Edge) -> double
	{
		const double Density = Edge * static_cast<double>(FruinMaxDensityPersonsPerSqM);
		return (Density > 0.0) ? (1.0 / Density) : 0.0;
	};

	// A is open-ended UPWARD - emptier than its own edge - and F open-ended downward, so F prints band E's
	// number under a "<" rather than a floor of its own. Reading down the table is increasing crowding,
	// which is the opposite direction to both trajectory surfaces.
	Contents.Rows.Add(MakeRow(0, TEXT(">"), SquareMetresPerPerson(Edges[0]), 2, Descriptions[0]));
	for (int32 Index = 1; Index < 5; ++Index)
	{
		Contents.Rows.Add(MakeRow(Index, TEXT(""), SquareMetresPerPerson(Edges[Index]), 2, Descriptions[Index]));
	}
	Contents.Rows.Add(MakeRow(5, TEXT("<"), SquareMetresPerPerson(Edges[4]), 2, Descriptions[5]));

	return Contents;
}

FHeatmapLegendContents FHeatmapLegend::RouteUsage(const FHeatmapLOSBands& Bands, float StrokeWidthMetres,
                                                  float ReferenceUsageDensity)
{
	const FText Title = LOCTEXT("UsageTitle", "Route usage");
	const FText TitleTooltip = LOCTEXT("UsageTitleTip",
		"Which parts of the floor were walked, and how heavily. Accumulates over the whole run.");

	// "passes", not "crossings". A crossing is the internal quantity - one cell side of person-metres - and
	// it means nothing to a reader. A PASS is the same event described as the reader experiences it:
	// somebody walked through here once. The word also survives the caveat below, which "people" would not:
	// two passes may be one person twice.
	const FText ValueHeader = LOCTEXT("UsageHeader", "passes");
	const FText ValueHeaderTooltip = LOCTEXT("UsageHeaderTip",
		"About how many times somebody walked through this spot, over the width of a normal walking path.\n\n"
		"One pass is one person crossing once, so two passes may be two people or one person twice.\n\n"
		"Counted along the direction of travel. Traffic crossing open floor at an angle therefore reads LOW "
		"- by up to about a fifth. The error is one-sided: this never reads high. In corridors, where the "
		"traffic is, it is exact.");

	const double Denominator = static_cast<double>(StrokeWidthMetres) * static_cast<double>(ReferenceUsageDensity);
	if (IsUnbanded(Bands) || !(Denominator > 0.0) || !FMath::IsFinite(Denominator))
	{
		return NoData(Title, TitleTooltip, ValueHeader, ValueHeaderTooltip);
	}

	double Edges[5];
	EdgesOf(Bands, Edges);

	FHeatmapLegendContents Contents;
	Contents.Title = Title;
	Contents.TitleTooltip = TitleTooltip;
	Contents.BandHeader = BandColumnHeader();
	Contents.ValueHeader = ValueHeader;
	Contents.ValueHeaderTooltip = ValueHeaderTooltip;
	Contents.bHasData = true;
	Contents.Rows.Reserve(6);

	// Take the half-step back off. The edges are at N + 0.5 purely so a cell holding exactly N cannot tie
	// against the "RVal < Band" chain; the number a reader wants is the N the band contains. Derived rather
	// than transcribed so a re-spaced ladder still prints the right integers.
	auto PassesInBand = [Denominator](double Edge) -> double
	{
		return FMath::RoundToDouble((Edge * Denominator) - 0.5);
	};

	// Descriptions are BUILT from the numbers, not written beside them - a hand-written "about two people"
	// next to a re-spaced ladder is a caption that lies. Only the two open-ended ends are phrased specially.
	auto Describe = [](double Passes) -> FText
	{
		return FText::Format(LOCTEXT("UsageBandFmt", "Roughly {0} pass(es) through here"),
			FText::AsNumber(static_cast<int32>(Passes)));
	};

	// Band A is the route threshold - a FRACTION of a pass, below which the cell is not on the route at all.
	// Printing that fraction would read as a level. It prints 0, which is what the band means.
	Contents.Rows.Add(MakeRow(0, TEXT(""), 0.0, 0,
		LOCTEXT("UsageA", "Not part of a walked route - nobody, or only a trace of one, came through")));
	for (int32 Index = 1; Index < 5; ++Index)
	{
		Contents.Rows.Add(MakeRow(Index, TEXT("~"), PassesInBand(Edges[Index]), 0,
			Describe(PassesInBand(Edges[Index]))));
	}
	// Open-ended at the top: at or above band E's edge is "one more than E, or more", printed as "> 4" so
	// the number shown is still one the ladder actually contains.
	Contents.Rows.Add(MakeRow(5, TEXT(">"), PassesInBand(Edges[4]), 0,
		FText::Format(LOCTEXT("UsageFFmt", "More than {0} passes - a main route"),
			FText::AsNumber(static_cast<int32>(PassesInBand(Edges[4]))))));

	return Contents;
}

FHeatmapLegendContents FHeatmapLegend::RouteExposure(const FHeatmapLOSBands& Bands, float StrokeWidthMetres,
                                                     float ReferenceExposureDensity, float FreeWalkSpeed)
{
	const FText Title = LOCTEXT("ExposureTitle", "Route exposure");
	const FText TitleTooltip = LOCTEXT("ExposureTitleTip",
		"How long people spent on each part of the floor - not just whether they crossed it. This is where "
		"waiting and queueing show up, which Route usage cannot see.");

	// SECONDS, not transit-equivalents, since the D5 ruling of 2026-08-14.
	//
	// The internal unit is the transit-equivalent, and this column used to print it: the ladder read
	// 2 / 5 / 15 / 50 and the header said "dwell". Nobody can act on that. It was defensible only while the
	// steps were unexplained - the moment D5 fixed them AGAINST a dwell target in seconds, printing the
	// intermediate unit hid the very calibration that makes the numbers quotable.
	//
	// The old comment here claimed seconds "cannot" be used because the quantity is normalised. That reason
	// was wrong, not merely cautious: FHeatmapLOSBands::StandingDwellSecondsAtEdge inverts the normalisation
	// exactly, and the result depends on neither the capture length nor the grid - only on the stroke width
	// and the reference, both of which are passed in here and so track any change. What the seconds DO
	// depend on is the assumption of ONE STATIONARY person, and that is a labelling obligation rather than
	// an obstacle. It is discharged in the tooltip below.
	const FText ValueHeader = LOCTEXT("ExposureHeader", "dwell (s)");
	const FText ValueHeaderTooltip = LOCTEXT("ExposureHeaderTip",
		"How long people spent here, in seconds. Read it as: one person standing still on this spot for "
		"this long would colour it this band.\n\n"
		"One person is the yardstick, not a claim about what happened. The underlying measure is "
		"person-seconds, so a cell reading 10 s could be one person standing for ten, two standing for "
		"five, or a stream of people walking through without stopping. It cannot tell those apart - what "
		"it does tell you is how heavily that patch of floor was occupied in total.\n\n"
		"CAUTION: unlike the Level of service scale, these band steps are a readability choice, not a "
		"published standard. They are set so the bands land near 1, 3, 10 and 30 seconds of standing, "
		"which is a deliberate choice about what is worth seeing rather than a measured threshold. Read "
		"the colours as relative - busier versus quieter - not as pass/fail lines.");

	// FreeWalkSpeed no longer enters the printed value - the seconds conversion needs only the width and the
	// reference (see StandingDwellSecondsAtEdge). It stays in the guard because a caller handing over a
	// nonsense speed has handed over a nonsense band set too, and the honest response to that is NoData.
	const double Denominator = static_cast<double>(StrokeWidthMetres)
		* static_cast<double>(ReferenceExposureDensity) * static_cast<double>(FreeWalkSpeed);
	if (IsUnbanded(Bands) || !(Denominator > 0.0) || !FMath::IsFinite(Denominator))
	{
		return NoData(Title, TitleTooltip, ValueHeader, ValueHeaderTooltip);
	}

	double Edges[5];
	EdgesOf(Bands, Edges);

	FHeatmapLegendContents Contents;
	Contents.Title = Title;
	Contents.TitleTooltip = TitleTooltip;
	Contents.BandHeader = BandColumnHeader();
	Contents.ValueHeader = ValueHeader;
	Contents.ValueHeaderTooltip = ValueHeaderTooltip;
	Contents.bHasData = true;
	Contents.Rows.Reserve(6);

	// These edges are UPPER BOUNDS, not counts, so unlike the pass ladder there is no half-step to undo.
	// One decimal: at the shipping configuration this prints 1.0 / 3.0 / 10.1 / 30.3, and the ladder was
	// chosen to make those read as 1 / 3 / 10 / 30. A re-referenced configuration will not print clean, and
	// rounding harder to force it would misdescribe where the colour actually changes.
	//
	// INVERTED from the live edges rather than tabled, like every other column on this surface. A hardcoded
	// "1 / 3 / 10 / 30" would agree with the render only until somebody dialled the stroke width or the
	// reference - which is the defect shape that already put this legend 20% off the pixels for three days.
	auto DwellSecondsAtEdge = [StrokeWidthMetres, ReferenceExposureDensity](double Edge) -> double
	{
		return static_cast<double>(FHeatmapLOSBands::StandingDwellSecondsAtEdge(
			static_cast<float>(Edge), StrokeWidthMetres, ReferenceExposureDensity));
	};

	// Qualitative, and safe against a re-tuned ladder because each phrase describes the RANK rather than the
	// number: whatever the steps become, B is still the free-flowing end and E is still the congested one.
	// Deliberately NOT built from the seconds the way the pass descriptions are built from the counts - the
	// seconds are already in the value column, and repeating them would just be a second place to drift.
	// Wording follows the band meanings already recorded on FHeatmapLOSBands::TrajectoryTransits.
	const FText Descriptions[6] = {
		LOCTEXT("ExposureA", "Nobody was ever here"),
		LOCTEXT("ExposureB", "Free-flow - walked straight through without slowing"),
		LOCTEXT("ExposureC", "Light use, slight slow-down"),
		LOCTEXT("ExposureD", "Noticeably delayed"),
		LOCTEXT("ExposureE", "Queueing"),
		LOCTEXT("ExposureF", "Stationary or blocked for a sustained period"),
	};

	Contents.Rows.Add(MakeRow(0, TEXT(""), 0.0, 0, Descriptions[0]));
	for (int32 Index = 1; Index < 5; ++Index)
	{
		Contents.Rows.Add(MakeRow(Index, TEXT(""), DwellSecondsAtEdge(Edges[Index]), 1, Descriptions[Index]));
	}
	Contents.Rows.Add(MakeRow(5, TEXT(">"), DwellSecondsAtEdge(Edges[4]), 1, Descriptions[5]));

	return Contents;
}

#undef LOCTEXT_NAMESPACE
