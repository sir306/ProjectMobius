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
	static constexpr int32 BrushRadius = 1;                 // TrajectoryLineBrushRadius (3x3)

	// --- LOS band edges, mirrored from the material custom node and the C++ macros -------------------
	static constexpr float LOS_A = 0.1419f;
	static constexpr float LOS_D = 0.4946f;

	// --- Derived byte expectations -------------------------------------------------------------------
	/** The seed written to a previously untouched texel: (uint8)(0.10 * 255) == (uint8)25.5 == 25. */
	static constexpr uint8 ExpectedSeedByte = 25;

	/** Repeat increment: (uint8)(0.1478 * 0.05 * 255) == (uint8)1.884 == 1. NOT 2, and not 0.00739. */
	static constexpr uint8 ExpectedIncrementByte = 1;

	/**
	 * A straight run stamps the 3x3 brush at every Bresenham step, so an interior centreline texel is
	 * covered by the steps at X-1, X and X+1 — three hits. The first seeds, the other two increment.
	 */
	static constexpr int32 HitsPerPassInterior = 3;

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

	/** The band a stored byte lands in, using the material's own `RVal < BAND` comparison order. */
	static bool IsInLowestBand(uint8 StoredByte)
	{
		return (static_cast<float>(StoredByte) / 255.0f) < LOS_A;
	}
}

// -------------------------------------------------------------------------------------------------
// 1. A single traversal must stay in the lowest (light blue) band.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectorySinglePassStaysInLowestBandTest,
	"ProjectMobius.Heatmap.Trajectory.SinglePassStaysInLowestBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrajectorySinglePassStaysInLowestBandTest::RunTest(const FString& Parameters)
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
	TestTrue(TEXT("A single traversal stays inside LOS_A (light blue)"), IsInLowestBand(Interior));

	// The 3x3 brush means the two neighbouring rows accumulate identically, not as a falloff.
	TestEqual(TEXT("Brush row above matches the centreline"), Texture->GetRawPixelRed(InteriorX, RouteY - 1), Interior);
	TestEqual(TEXT("Brush row below matches the centreline"), Texture->GetRawPixelRed(InteriorX, RouteY + 1), Interior);
	TestEqual(TEXT("Outside the brush stays untouched"), Texture->GetRawPixelRed(InteriorX, RouteY + 2), static_cast<uint8>(0));

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
	int32 FirstPassOutsideLowestBand = INDEX_NONE;

	// 40 passes comfortably spans LOS_A through LOS_D without approaching the 255 ceiling.
	for (int32 Pass = 1; Pass <= 40; ++Pass)
	{
		DrawOnePass(Texture);
		const int32 Stored = Texture->GetRawPixelRed(InteriorX, RouteY);

		TestEqual(*FString::Printf(TEXT("Interior byte after pass %d"), Pass),
			Stored, ExpectedInteriorByte(Pass));
		TestTrue(*FString::Printf(TEXT("Pass %d is strictly greater than pass %d"), Pass, Pass - 1),
			Stored > PreviousByte);

		if (FirstPassOutsideLowestBand == INDEX_NONE && !IsInLowestBand(static_cast<uint8>(Stored)))
		{
			FirstPassOutsideLowestBand = Pass;
		}
		PreviousByte = Stored;
	}

	// byte 36 == 0.14118 is still below LOS_A (0.1419); byte 39 == 0.15294 is not.
	TestEqual(TEXT("LOS_A is left on the fifth traversal"), FirstPassOutsideLowestBand, 5);

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
// 4. Dwell renders as a uniform 3x3 plateau, not a peak — a measurement artefact of the box brush
//    that any per-square-metre claim has to divide out.
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
	// stamps one 3x3 box and breaks immediately: one hit for all nine texels, per sample.
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

	// Every texel in the 3x3 footprint must be identical — the defining property of a plateau.
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			TestEqual(*FString::Printf(TEXT("Plateau texel (%d,%d) matches the centre"), OffsetX, OffsetY),
				Texture->GetRawPixelRed(CentreX + OffsetX, CentreY + OffsetY), Centre);
		}
	}

	TestEqual(TEXT("Immediately outside the footprint is untouched"),
		Texture->GetRawPixelRed(CentreX + 2, CentreY), static_cast<uint8>(0));

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

	int32 FirstPassAtOrAboveLosD = INDEX_NONE;
	int32 FirstSaturatedPass = INDEX_NONE;

	for (int32 Pass = 1; Pass <= 120; ++Pass)
	{
		DrawOnePass(Texture);
		const uint8 Stored = Texture->GetRawPixelRed(InteriorX, RouteY);

		if (FirstPassAtOrAboveLosD == INDEX_NONE && (static_cast<float>(Stored) / 255.0f) >= LOS_D)
		{
			FirstPassAtOrAboveLosD = Pass;
		}
		if (FirstSaturatedPass == INDEX_NONE && Stored == 255)
		{
			FirstSaturatedPass = Pass;
			break;
		}
	}

	// 27 + (n-1)*3 >= 127  =>  n == 35.
	TestEqual(TEXT("The top band is reached after 35 traversals"), FirstPassAtOrAboveLosD, 35);
	// 27 + (n-1)*3 >= 255  =>  n == 77.
	TestEqual(TEXT("The channel saturates after 77 traversals"), FirstSaturatedPass, 77);

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
