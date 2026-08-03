// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryHeatmapCalibrationTest.cpp
//
// Locks down the trajectory heatmap's ACCUMULATION ARITHMETIC against the uint8 pixel buffer it
// actually writes to. The band calibration was previously reasoned about in linear float space, which
// is not where accumulation happens: every write goes through
//
//     #define COLOR_TO_BYTE(color) (uint8)(color * 255)      // DynamicPixelRenderingTexture.cpp
//
// a C-style cast, so it TRUNCATES. The intended 0.00739 repeat increment becomes (uint8)1.884 == 1,
// i.e. 1/255 == 0.00392 — barely half the documented value. These tests pin the real byte sequence so
// any future retune is measured against observed behaviour rather than float intent.
//
// Tier A: drives UDynamicPixelRenderingTexture directly. No world, no MASS, no agent import, no
// building geometry. Every number here is a closed-form consequence of the draw call, so this suite is
// deterministic and fast; the in-game pipeline is covered separately by Mobius.InGame.TrajectoryHeatmap.
//
// Run: UnrealEditor ProjectMobius.uproject -ExecCmds="Automation RunTests ProjectMobius.Heatmap.Trajectory" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "DynamicPixelRenderingTexture.h"

namespace TrajectoryCalibration
{
	// --- Values mirrored from AHeatmapPixelTextureVisualizer's defaults -------------------------------
	// Kept as literals rather than read from the actor so a silent default change fails a test instead
	// of quietly moving the expectations with it.
	static constexpr float AgentRed = 0.1478f;              // AgentColorValue.R
	static constexpr float SampleWeight = 0.05f;            // TrajectorySampleWeight
	static constexpr float MinimumVisible = 0.10f;          // TrajectoryMinimumVisibleValue
	// Footprint radius in TEXELS. At runtime this is not a constant: AHeatmapPixelTextureVisualizer
	// derives it per floor from TrajectoryCircleRadius (20 cm) via UVScale, so it was 3 on the 73 m floor
	// the calibration capture came from and reaches double digits on a small one. One is used here only
	// because it keeps the closed-form byte arithmetic below readable; BrushScalesWithRadius covers the
	// rest of the range.
	static constexpr int32 BrushRadius = 1;

	// --- LOS band edges ------------------------------------------------------------------------------
	// Two sets, because the two heatmap surfaces measure different quantities on the same uint8 channel.
	//
	// Density edges are Fruin's, normalised against 2.1739 persons/m^2. They are what M_HeatmapRT_V2 and
	// FHeatmapLOSBands::Density() use, and they are WRONG for trajectory: DensityLOS_A (byte 36.2) sits
	// above the first-visit seed (byte 25), so bare floor and the first four passes all render LOS_A.
	// A measured 30 s ground-floor capture had 23.8 % of its touched texels land there, indistinguishable
	// from untouched floor. That is the bug these tests now pin against, not a property to preserve.
	static constexpr float DensityLOS_A = 0.1419f;
	static constexpr float DensityLOS_D = 0.4946f;

	// Trajectory edges, mirrored from FHeatmapLOSBands::Trajectory() and from the scalar parameters on
	// M_HeatmapRT_Trajectory. TrajectoryLOS_A is placed below the seed so LOS_A means "no data".
	//
	// IMPORTANT: these edges are NOT derived from the straight-line model this file uses elsewhere. That
	// model gives an interior texel 3 hits per "pass" (byte 27), but it draws one line call per pass,
	// whereas the running pipeline emits one segment per agent per flush and stamps the disc along
	// each -- so several consecutive flushes hit the same texel as one agent walks across it. Replaying
	// a real 30 s capture measured a lone crossing at a median 13 hits, byte 37. Calibrating the edges
	// off the 3-hit figure is what previously made a single quick crossing render three bands up.
	//
	// STALE AS OF 2026-08-03. Those edges were fitted against a brush of a fixed 1-texel radius. The brush
	// is now sized in world centimetres, which on that same capture resolves to radius 3 and moves a lone
	// crossing from byte 37 to a replayed 46 — close enough to the 46.5 LOS_B edge that a slow crossing
	// crosses it. Owner ruling 2026-08-03: recapture, then refit. Until that lands these edges are known
	// to be slightly tight rather than measured, and the tests below assert arithmetic, not band fit.
	static constexpr float TrajectoryLOS_A = 24.5f / 255.0f;
	static constexpr float TrajectoryLOS_B = 46.5f / 255.0f;
	static constexpr float TrajectoryLOS_D = 110.5f / 255.0f;
	static constexpr float TrajectoryLOS_E = 175.5f / 255.0f;

	/** Median byte a single agent crossing leaves, measured from the 20260728_165413 capture replay. */
	static constexpr uint8 MeasuredLoneCrossingByte = 37;
	/** p90 of that same distribution — a slow single crossing. */
	static constexpr uint8 MeasuredSlowCrossingByte = 49;

	// --- Derived byte expectations -------------------------------------------------------------------
	/** The seed written to a previously untouched texel: (uint8)(0.10 * 255) == (uint8)25.5 == 25. */
	static constexpr uint8 ExpectedSeedByte = 25;

	/** Repeat increment: (uint8)(0.1478 * 0.05 * 255) == (uint8)1.884 == 1. NOT 2, and not 0.00739. */
	static constexpr uint8 ExpectedIncrementByte = 1;

	/**
	 * A straight run stamps the brush at every Bresenham step, so an interior CENTRELINE texel is covered
	 * by the steps at X-1, X and X+1 — three hits. The first seeds, the other two increment.
	 *
	 * Centreline only. Off-centre rows of a disc see fewer steps (the chord through the disc is shorter),
	 * which is why the neighbouring row holds the bare seed at radius 1.
	 */
	static constexpr int32 HitsPerPassInterior = 2 * BrushRadius + 1;

	static constexpr int32 TextureSize = 64;

	/** Texel row the horizontal test routes are drawn along. */
	static constexpr int32 RouteY = 32;
	/** Start/end X of the test route. Interior assertions must stay clear of both ends. */
	static constexpr int32 RouteStartX = 10;
	static constexpr int32 RouteEndX = 50;
	/** An interior texel: far enough from both ends to receive the full three hits per pass. */
	static constexpr int32 InteriorX = 30;

	static FLinearColor MakePathColor(float Weight = SampleWeight)
	{
		// Mirrors PathColor = AgentColorValue * TrajectorySampleWeight. Note FLinearColor::operator*
		// scales alpha too, so A becomes 0.05 — harmless, the material samples only .r.
		return FLinearColor(AgentRed, 0.0f, 0.0f, 1.0f) * Weight;
	}

	static UDynamicPixelRenderingTexture* MakeTexture()
	{
		UDynamicPixelRenderingTexture* Texture = NewObject<UDynamicPixelRenderingTexture>();
		Texture->InitializeTexture(TextureSize, TextureSize, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		Texture->ClearTexture();
		return Texture;
	}

	/** One straight horizontal traversal of the route, exactly as the rasteriser would draw it. */
	static void DrawOnePass(UDynamicPixelRenderingTexture* Texture, float Weight = SampleWeight)
	{
		Texture->DrawLineWithMinimumRed(RouteStartX, RouteEndX, RouteY, RouteY,
			MakePathColor(Weight), MinimumVisible, BrushRadius);
	}

	/** Expected interior byte after N identical passes: seed + 2 on the first, then 3 per pass after. */
	static int32 ExpectedInteriorByte(int32 PassCount)
	{
		if (PassCount <= 0)
		{
			return 0;
		}
		const int32 FirstPass = ExpectedSeedByte + (HitsPerPassInterior - 1) * ExpectedIncrementByte;
		return FirstPass + (PassCount - 1) * HitsPerPassInterior * ExpectedIncrementByte;
	}

	/** True if the stored byte renders in the lowest band under the DENSITY edges. */
	static bool IsInLowestDensityBand(uint8 StoredByte)
	{
		return (static_cast<float>(StoredByte) / 255.0f) < DensityLOS_A;
	}

	/**
	 * True if the stored byte renders in the lowest band under the TRAJECTORY edges — which, by
	 * construction, means the texel was never visited.
	 */
	static bool ReadsAsNoData(uint8 StoredByte)
	{
		return (static_cast<float>(StoredByte) / 255.0f) < TrajectoryLOS_A;
	}
}

// -------------------------------------------------------------------------------------------------
// 1. A single traversal must be distinguishable from bare floor.
//
//    This test used to assert the opposite framing — "a single traversal stays inside LOS_A" — which
//    was arithmetically true and visually useless, because under the density edges LOS_A is also the
//    colour of an untouched texel. Passing meant "renders invisible". The assertion below is the one
//    that has to hold for the surface to be readable at all.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectorySinglePassIsDistinguishableTest,
	"ProjectMobius.Heatmap.Trajectory.SinglePassIsDistinguishable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectorySinglePassIsDistinguishableTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	UDynamicPixelRenderingTexture* Texture = MakeTexture();
	TestEqual(TEXT("A cleared texel reads zero"), Texture->GetRawPixelRed(InteriorX, RouteY), static_cast<uint8>(0));

	DrawOnePass(Texture);

	const uint8 Interior = Texture->GetRawPixelRed(InteriorX, RouteY);

	// 25 + 1 + 1 == 27 == 0.1059. The handoff previously claimed ~0.115, which was the float result.
	TestEqual(TEXT("One pass leaves an interior texel at the truncated byte value"),
		Interior, static_cast<uint8>(ExpectedInteriorByte(1)));
	TestEqual(TEXT("That byte is 27"), Interior, static_cast<uint8>(27));

	// The headline property: one pass must not read as empty floor.
	TestFalse(TEXT("A single traversal does not render as no-data"), ReadsAsNoData(Interior));
	TestTrue(TEXT("An untouched texel does render as no-data"), ReadsAsNoData(static_cast<uint8>(0)));

	// And the regression that motivated the retune: under the density edges it did.
	TestTrue(TEXT("Under the density edges the same byte was indistinguishable from bare floor"),
		IsInLowestDensityBand(Interior));

	// The disc brush DOES fall off across the path's width, unlike the square brush it replaced. At R=1 a
	// neighbouring row is covered only by the single centre directly beside it, so it holds the bare seed
	// while the centreline has taken three hits. This is brush geometry, not data: a rim texel is within
	// footprint range for a shorter stretch of the walk than a centreline texel is.
	//
	// It costs one texel of fringe on a busy route and nothing at all on a light one (seed and centreline
	// both sit in the lowest data band until a route is walked repeatedly). Pinned here so the fringe is a
	// known quantity rather than something rediscovered off a screenshot.
	TestEqual(TEXT("A rim row holds the first-visit seed only"),
		Texture->GetRawPixelRed(InteriorX, RouteY - 1), ExpectedSeedByte);
	TestEqual(TEXT("Both rim rows behave identically"),
		Texture->GetRawPixelRed(InteriorX, RouteY + 1), Texture->GetRawPixelRed(InteriorX, RouteY - 1));
	TestTrue(TEXT("The centreline still reads hotter than the rim"),
		Interior > Texture->GetRawPixelRed(InteriorX, RouteY - 1));
	TestEqual(TEXT("Outside the brush stays untouched"), Texture->GetRawPixelRed(InteriorX, RouteY + 2), static_cast<uint8>(0));

	return true;
}

// -------------------------------------------------------------------------------------------------
// 1b. The band edges themselves: nothing a draw can produce may collide with the no-data colour, and
//     the colouriser must agree with the edges it was handed.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryNoDataBandIsReservedTest,
	"ProjectMobius.Heatmap.Trajectory.NoDataBandIsReserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryNoDataBandIsReservedTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	const FHeatmapLOSBands Bands = FHeatmapLOSBands::Trajectory();

	TestEqual(TEXT("BandA matches the constant this suite mirrors"), Bands.BandA, TrajectoryLOS_A);
	TestTrue(TEXT("BandA sits below the first-visit seed, so no drawn texel can fall in it"),
		(static_cast<float>(ExpectedSeedByte) / 255.0f) >= Bands.BandA);
	TestTrue(TEXT("Edges ascend"), Bands.BandA < Bands.BandB && Bands.BandB < Bands.BandC
		&& Bands.BandC < Bands.BandD && Bands.BandD < Bands.BandE);
	TestTrue(TEXT("The top edge leaves room for a sixth band below saturation"), Bands.BandE < 1.0f);

	// Every byte a draw can leave behind must colourise to something other than the no-data colour.
	const FLinearColor NoDataColour = UDynamicPixelRenderingTexture::BandColourForRedValue(0.0f, Bands);
	for (int32 Byte = ExpectedSeedByte; Byte <= 255; ++Byte)
	{
		const FLinearColor Colour = UDynamicPixelRenderingTexture::BandColourForRedValue(
			static_cast<float>(Byte) / 255.0f, Bands);
		if (Colour.Equals(NoDataColour))
		{
			AddError(FString::Printf(TEXT("Byte %d colourises to the no-data colour"), Byte));
			break;
		}
	}

	// Half-byte edges mean no reachable stored value can sit exactly on a comparison boundary.
	const float Edges[] = { Bands.BandA, Bands.BandB, Bands.BandC, Bands.BandD, Bands.BandE };
	for (const float Edge : Edges)
	{
		const float ScaledEdge = Edge * 255.0f;
		TestTrue(*FString::Printf(TEXT("Edge %.6f does not land on a whole byte"), Edge),
			!FMath::IsNearlyEqual(ScaledEdge, FMath::RoundToFloat(ScaledEdge), 0.01f));
	}

	// The property a viewer actually judges the map by: one person walking through once must read as the
	// lowest DATA band, not as a busy route. Both figures are measured from a real capture replay, not
	// derived from this file's straight-line model — see the note on the constants.
	const FLinearColor LowestDataColour = UDynamicPixelRenderingTexture::BandColourForRedValue(
		static_cast<float>(ExpectedSeedByte) / 255.0f, Bands);

	const FLinearColor LoneColour = UDynamicPixelRenderingTexture::BandColourForRedValue(
		static_cast<float>(MeasuredLoneCrossingByte) / 255.0f, Bands);
	TestTrue(*FString::Printf(TEXT("A typical single crossing (byte %d) renders in the lowest data band"),
			MeasuredLoneCrossingByte),
		LoneColour.Equals(LowestDataColour));
	TestFalse(TEXT("...and is not mistaken for no data"), LoneColour.Equals(NoDataColour));

	// A SLOW single crossing deposits as much as a couple of fast ones and may sit one band higher. That
	// is the surface behaving correctly — it measures occupancy — so the bound here is "at most one band
	// up", not "same band". Two bands up would mean the edges have drifted low again.
	const FLinearColor SlowColour = UDynamicPixelRenderingTexture::BandColourForRedValue(
		static_cast<float>(MeasuredSlowCrossingByte) / 255.0f, Bands);
	const FLinearColor SecondDataColour = UDynamicPixelRenderingTexture::BandColourForRedValue(
		(Bands.BandB + Bands.BandC) * 0.5f, Bands);
	TestTrue(*FString::Printf(TEXT("A slow single crossing (byte %d) is at most one band above the lowest"),
			MeasuredSlowCrossingByte),
		SlowColour.Equals(LowestDataColour) || SlowColour.Equals(SecondDataColour));

	return true;
}

// -------------------------------------------------------------------------------------------------
// 2. Repeat traversal must climb monotonically, +3 bytes per pass, leaving LOS_A on the fifth.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryRepeatPassesClimbMonotonicallyTest,
	"ProjectMobius.Heatmap.Trajectory.RepeatPassesClimbMonotonically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryRepeatPassesClimbMonotonicallyTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	UDynamicPixelRenderingTexture* Texture = MakeTexture();

	int32 PreviousByte = -1;
	int32 FirstPassOutsideLowestDensityBand = INDEX_NONE;
	int32 FirstVisiblePassUnderTrajectoryBands = INDEX_NONE;

	// 40 passes comfortably spans the band set without approaching the 255 ceiling.
	for (int32 Pass = 1; Pass <= 40; ++Pass)
	{
		DrawOnePass(Texture);
		const int32 Stored = Texture->GetRawPixelRed(InteriorX, RouteY);

		TestEqual(*FString::Printf(TEXT("Interior byte after pass %d"), Pass),
			Stored, ExpectedInteriorByte(Pass));
		TestTrue(*FString::Printf(TEXT("Pass %d is strictly greater than pass %d"), Pass, Pass - 1),
			Stored > PreviousByte);

		if (FirstPassOutsideLowestDensityBand == INDEX_NONE && !IsInLowestDensityBand(static_cast<uint8>(Stored)))
		{
			FirstPassOutsideLowestDensityBand = Pass;
		}
		if (FirstVisiblePassUnderTrajectoryBands == INDEX_NONE && !ReadsAsNoData(static_cast<uint8>(Stored)))
		{
			FirstVisiblePassUnderTrajectoryBands = Pass;
		}
		PreviousByte = Stored;
	}

	// byte 36 == 0.14118 is still below DensityLOS_A (0.1419); byte 39 == 0.15294 is not. Four passes of
	// real movement rendering as bare floor is the defect the trajectory edges exist to remove.
	TestEqual(TEXT("Under the density edges the lowest band is not left until the fifth traversal"),
		FirstPassOutsideLowestDensityBand, 5);
	TestEqual(TEXT("Under the trajectory edges the very first traversal is already visible"),
		FirstVisiblePassUnderTrajectoryBands, 1);

	return true;
}

// -------------------------------------------------------------------------------------------------
// 3. The truncation dead zone: an increment below 1/255 silently stops accumulating entirely.
//    This is the trap for anyone retuning TrajectorySampleWeight downward.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryIncrementDeadZoneTest,
	"ProjectMobius.Heatmap.Trajectory.IncrementDeadZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryIncrementDeadZoneTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	// The floor: the increment must reach 1/255 after truncation or it contributes nothing.
	const float DeadZoneFloorWeight = (1.0f / 255.0f) / AgentRed; // ~0.02654

	// Current shipping configuration must sit above the floor — with very little headroom.
	TestTrue(TEXT("The shipping sample weight is above the truncation floor"), SampleWeight > DeadZoneFloorWeight);
	TestTrue(TEXT("...but by less than a factor of two, so a modest retune would break it"),
		SampleWeight < DeadZoneFloorWeight * 2.0f);

	// Just below the floor, repeat traversal must be provably inert.
	const float DeadWeight = DeadZoneFloorWeight * 0.9f;
	TestTrue(TEXT("The chosen dead weight truncates to a zero increment"),
		static_cast<uint8>(AgentRed * DeadWeight * 255.0f) == 0);

	UDynamicPixelRenderingTexture* Texture = MakeTexture();
	DrawOnePass(Texture, DeadWeight);
	const uint8 AfterFirst = Texture->GetRawPixelRed(InteriorX, RouteY);

	// The seed still lands — MinimumVisible is applied with FMath::Max, not the increment path.
	TestEqual(TEXT("First visit is still seeded"), AfterFirst, ExpectedSeedByte);

	for (int32 Pass = 0; Pass < 25; ++Pass)
	{
		DrawOnePass(Texture, DeadWeight);
	}

	TestEqual(TEXT("25 further traversals below the floor add absolutely nothing"),
		Texture->GetRawPixelRed(InteriorX, RouteY), ExpectedSeedByte);

	return true;
}

// -------------------------------------------------------------------------------------------------
// 4. Dwell renders as a uniform plateau over the whole footprint, not a peak — a measurement artefact
//    of the brush that any per-square-metre claim has to divide out.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryDwellFormsUniformPlateauTest,
	"ProjectMobius.Heatmap.Trajectory.DwellFormsUniformPlateau",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryDwellFormsUniformPlateauTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	UDynamicPixelRenderingTexture* Texture = MakeTexture();

	// A dwelling agent submits many samples whose start and end round to the same texel. The loop then
	// stamps the disc once and breaks immediately: one hit for every texel in the footprint, per sample.
	// Because the walk never advances, no texel is favoured, so a standing agent is the one case where
	// the footprint IS a flat plateau — the falloff a moving agent leaves comes from the walk, not the brush.
	constexpr int32 DwellSamples = 12;
	constexpr int32 CentreX = 20;
	constexpr int32 CentreY = 20;
	for (int32 Sample = 0; Sample < DwellSamples; ++Sample)
	{
		Texture->DrawLineWithMinimumRed(CentreX, CentreX, CentreY, CentreY,
			MakePathColor(), MinimumVisible, BrushRadius);
	}

	const uint8 Centre = Texture->GetRawPixelRed(CentreX, CentreY);
	TestEqual(TEXT("Dwell accumulates one increment per sample after the seed"),
		Centre, static_cast<uint8>(ExpectedSeedByte + (DwellSamples - 1) * ExpectedIncrementByte));

	// Every texel inside the disc must match the centre, and every texel outside it must be untouched.
	// The corners are the assertion that matters: a square brush would have covered them, and a square
	// footprint is what made a route render R*sqrt(2) wide on its diagonals.
	for (int32 OffsetY = -BrushRadius; OffsetY <= BrushRadius; ++OffsetY)
	{
		for (int32 OffsetX = -BrushRadius; OffsetX <= BrushRadius; ++OffsetX)
		{
			const bool bInsideDisc = (OffsetX * OffsetX + OffsetY * OffsetY) <= (BrushRadius * BrushRadius);
			const uint8 Stored = Texture->GetRawPixelRed(CentreX + OffsetX, CentreY + OffsetY);
			TestEqual(*FString::Printf(TEXT("Footprint texel (%d,%d) %s"), OffsetX, OffsetY,
					bInsideDisc ? TEXT("matches the centre") : TEXT("is outside the disc and untouched")),
				Stored, bInsideDisc ? Centre : static_cast<uint8>(0));
		}
	}

	TestEqual(TEXT("Immediately outside the footprint is untouched"),
		Texture->GetRawPixelRed(CentreX + BrushRadius + 1, CentreY), static_cast<uint8>(0));

	return true;
}

// -------------------------------------------------------------------------------------------------
// 4b. The brush is sized from world centimetres, not texels. AHeatmapPixelTextureVisualizer does the
//     conversion (TrajectoryCircleRadius * min(UVScale)), but the property that makes it worth doing
//     lives here: footprint area must actually track the radius it is handed, so that a coarser texel
//     grid can be compensated for by asking for more texels.
//
//     This is the regression guard for the defect that motivated the change — a radius fixed in texels
//     rendered the same 40 cm route 5.9 cm wide on a 20 m floor and 73 cm wide on a 250 m one, because
//     a texel is max(MeshSize.X, MeshSize.Y)/1024 across and nothing scaled with it.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryBrushScalesWithRadiusTest,
	"ProjectMobius.Heatmap.Trajectory.BrushScalesWithRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryBrushScalesWithRadiusTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	constexpr int32 CentreX = 30;
	constexpr int32 CentreY = 30;

	int32 PreviousWidth = 0;
	for (int32 Radius = 0; Radius <= 5; ++Radius)
	{
		UDynamicPixelRenderingTexture* Texture = MakeTexture();
		Texture->DrawLineWithMinimumRed(CentreX, CentreX, CentreY, CentreY,
			MakePathColor(), MinimumVisible, Radius);

		// Measure the footprint the way a viewer reads it: how wide is the mark, in texels.
		int32 Width = 0;
		for (int32 OffsetX = -8; OffsetX <= 8; ++OffsetX)
		{
			if (Texture->GetRawPixelRed(CentreX + OffsetX, CentreY) > 0)
			{
				++Width;
			}
		}

		TestEqual(*FString::Printf(TEXT("Radius %d spans %d texels across"), Radius, 2 * Radius + 1),
			Width, 2 * Radius + 1);
		TestTrue(*FString::Printf(TEXT("Radius %d is wider than radius %d"), Radius, Radius - 1),
			Width > PreviousWidth);
		PreviousWidth = Width;

		// A disc, not a square: the corner of the bounding box must stay clear for any radius above zero.
		if (Radius > 0)
		{
			TestEqual(*FString::Printf(TEXT("Radius %d leaves its bounding-box corner untouched"), Radius),
				Texture->GetRawPixelRed(CentreX + Radius, CentreY + Radius), static_cast<uint8>(0));
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// 5. Overlapping routes: a shared texel must carry the sum of both routes' hits, while the
//    non-overlapping arms stay at single-pass value. This is the "route concentration" signal.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryOverlappingRoutesSumTest,
	"ProjectMobius.Heatmap.Trajectory.OverlappingRoutesSum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryOverlappingRoutesSumTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	UDynamicPixelRenderingTexture* Texture = MakeTexture();

	// Horizontal route along RouteY, and a vertical route crossing it at InteriorX.
	Texture->DrawLineWithMinimumRed(RouteStartX, RouteEndX, RouteY, RouteY,
		MakePathColor(), MinimumVisible, BrushRadius);
	Texture->DrawLineWithMinimumRed(InteriorX, InteriorX, RouteY - 12, RouteY + 12,
		MakePathColor(), MinimumVisible, BrushRadius);

	const uint8 Crossing = Texture->GetRawPixelRed(InteriorX, RouteY);
	const uint8 HorizontalOnly = Texture->GetRawPixelRed(RouteStartX + 6, RouteY);
	const uint8 VerticalOnly = Texture->GetRawPixelRed(InteriorX, RouteY - 8);

	TestEqual(TEXT("An arm touched by one route only sits at the single-pass value"),
		HorizontalOnly, static_cast<uint8>(ExpectedInteriorByte(1)));
	TestEqual(TEXT("Both arms accumulate identically"), VerticalOnly, HorizontalOnly);

	// The crossing is seeded once and then incremented by every remaining hit from both routes.
	TestEqual(TEXT("The crossing carries both routes' hits"),
		Crossing, static_cast<uint8>(ExpectedSeedByte + (2 * HitsPerPassInterior - 1) * ExpectedIncrementByte));
	TestTrue(TEXT("The crossing reads hotter than either arm"), Crossing > HorizontalOnly);

	return true;
}

// -------------------------------------------------------------------------------------------------
// 6. Full dynamic range. Documents how few traversals the layer can actually distinguish before it
//    saturates — the headline constraint on using this as a quantitative surface.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryDynamicRangeTest,
	"ProjectMobius.Heatmap.Trajectory.DynamicRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryDynamicRangeTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryCalibration;

	UDynamicPixelRenderingTexture* Texture = MakeTexture();

	int32 FirstPassAtOrAboveDensityLosD = INDEX_NONE;
	int32 FirstPassInTrajectoryTopBand = INDEX_NONE;
	int32 FirstSaturatedPass = INDEX_NONE;

	for (int32 Pass = 1; Pass <= 120; ++Pass)
	{
		DrawOnePass(Texture);
		const uint8 Stored = Texture->GetRawPixelRed(InteriorX, RouteY);
		const float Normalised = static_cast<float>(Stored) / 255.0f;

		if (FirstPassAtOrAboveDensityLosD == INDEX_NONE && Normalised >= DensityLOS_D)
		{
			FirstPassAtOrAboveDensityLosD = Pass;
		}
		if (FirstPassInTrajectoryTopBand == INDEX_NONE && Normalised >= TrajectoryLOS_E)
		{
			FirstPassInTrajectoryTopBand = Pass;
		}
		if (FirstSaturatedPass == INDEX_NONE && Stored == 255)
		{
			FirstSaturatedPass = Pass;
			break;
		}
	}

	// 27 + (n-1)*3 >= 127  =>  n == 35.
	TestEqual(TEXT("Under the density edges the top band is reached after 35 traversals"),
		FirstPassAtOrAboveDensityLosD, 35);
	// 27 + (n-1)*3 >= 175.5  =>  n == 51, in this file's straight-line units. Note those units are NOT
	// agent crossings: replaying a real capture put a lone crossing at ~13 hits against this model's 3,
	// so the top band is reached after roughly 6 real crossings, not 51.
	TestEqual(TEXT("Under the trajectory edges the top band is reached after 51 straight-line passes"),
		FirstPassInTrajectoryTopBand, 51);
	// 27 + (n-1)*3 >= 255  =>  n == 77. Retuning edges cannot move this: the ceiling is the increment's,
	// not the palette's. In real terms that is only ~9-10 agent crossings — the measured capture had
	// 18 % of its 5-crossing texels and 61 % of its 10-crossing texels already pinned at 255.
	TestEqual(TEXT("The channel saturates after 77 straight-line passes"), FirstSaturatedPass, 77);

	// Saturation must clamp, never wrap — AddSaturated is what guarantees this.
	for (int32 Pass = 0; Pass < 5; ++Pass)
	{
		DrawOnePass(Texture);
	}
	TestEqual(TEXT("Further traversals clamp at 255 rather than wrapping"),
		Texture->GetRawPixelRed(InteriorX, RouteY), static_cast<uint8>(255));

	return true;
}

#endif // !UE_BUILD_SHIPPING
