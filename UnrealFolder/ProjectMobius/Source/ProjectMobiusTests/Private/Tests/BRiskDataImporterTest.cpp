// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

#include "BRiskDataImporter.h"
#include "BRisk/AgentTenabilityTimeline.h" // FRoomVolume / ResolveRoomIndexAtLocation (attribution measurement)
#include "BRisk/BRiskDataSubsystem.h"
#include "BRisk/BRiskEgressSubsystem.h"
#include "BRisk/BRiskHazardVisualizer.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	FString MakeBRiskTestDir()
	{
		const FString TestDir = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Automation"),
			TEXT("BRiskImporter"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*TestDir, true);
		return TestDir;
	}

	bool WriteTextFile(const FString& Path, const FString& Contents)
	{
		return FFileHelper::SaveStringToFile(Contents, *Path);
	}

	FString MakeSmv(const FString& ZoneCsvName = TEXT("basemodel_testBox_zone.csv"))
	{
		return FString::Printf(
			TEXT("ROOM   1\n")
			TEXT(" 2.4000E+001 5.5000E+000 2.6000E+000\n")
			TEXT(" 0.0000E+000 0.0000E+000 0.0000E+000\n")
			TEXT("LABEL\n")
			TEXT(" 0 0 0\n")
			TEXT("room\n")
			TEXT("FIRE\n")
			TEXT("1 1.0 2.0 0.0\n")
			TEXT("VENTGEOM\n")
			TEXT("1 2 4 2.4 0.8 0.0 2.4\n")
			TEXT("ZONE\n")
			TEXT("%s\n"),
			*ZoneCsvName);
	}

	FString MakeZoneCsv()
	{
		FString Csv =
			TEXT("s,C,C,m,Pa,1 / m,1 / m,kW,m,m,m ^ 2,m ^ 2,\n")
			TEXT("Time,ULT_1,LLT_1,HGT_1,PRS_1,ULOD_1,LLOD_1,HRR_1,FLHGT_1,FBASE_1,FAREA_1,HVENT_1,\n");

		for (int32 Index = 0; Index <= 60; ++Index)
		{
			const int32 Time = Index * 10;
			Csv += FString::Printf(
				TEXT("%d,%d,%d,2.5,0,0.1,0.2,%d,0.0,0.0,1.0,2.0,\n"),
				Time,
				24 + Index,
				23 + Index,
				100 + Index);
		}

		return Csv;
	}

	bool WriteScenario(const FString& TestDir, FString& OutSmvPath, FString& OutCsvPath)
	{
		OutSmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
		OutCsvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox_zone.csv"));
		const FString SprinklersPath = FPaths::Combine(TestDir, TEXT("sprinklers.xml"));
		const FString SprinklersXml =
			TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
			TEXT("<Sprinklers>\n")
			TEXT("  <Sprinkler>\n")
			TEXT("    <sprid>1</sprid>\n")
			TEXT("    <room>1</room>\n")
			TEXT("    <sprx>1.0</sprx>\n")
			TEXT("    <spry>2.0</spry>\n")
			TEXT("    <responsetime>86</responsetime>\n")
			TEXT("    <sdistribution><varname>sprr</varname><value>3.25</value></sdistribution>\n")
			TEXT("    <sdistribution><varname>sprdensity</varname><value>4.2</value></sdistribution>\n")
			TEXT("    <sdistribution><varname>sprz</varname><value>0.025</value></sdistribution>\n")
			TEXT("    <sdistribution><varname>acttemp</varname><value>57</value></sdistribution>\n")
			TEXT("  </Sprinkler>\n")
			TEXT("</Sprinklers>\n");
		return WriteTextFile(OutSmvPath, MakeSmv())
			&& WriteTextFile(OutCsvPath, MakeZoneCsv())
			&& WriteTextFile(SprinklersPath, SprinklersXml);
	}

	/**
	 * An SMV whose single room is an L-shaped corridor, exercising the case B-Risk cannot
	 * represent: the equivalent rectangle (area+perimeter preserving, SR282 eq. 1-2) is
	 * 22.986 x 1.0 while the true footprint spans 17.8 x 6.186. Vent has a non-zero sill so
	 * the head-vs-opening-height decode is covered too: head token 2.25, sill 0.9 -> 1.35.
	 */
	FString MakeLShapedSmv()
	{
		return
			TEXT("ROOM   1\n")
			TEXT(" 2.2986E+001 1.0000E+000 4.0000E+000\n")
			TEXT("-1.4750E+001-1.7019E+001 0.0000E+000\n")
			TEXT("LABEL\n")
			TEXT(" 0 0 0\n")
			TEXT("Corridor 15\n")
			TEXT("VENTGEOM\n")
			TEXT("1 2 3 1.05 0.0 0.9 2.25\n")
			TEXT("ZONE\n")
			TEXT("basemodel_testBox_zone.csv\n");
	}

	/**
	 * The single spaces[] entry: room 1 as the true L-shaped corridor, wound clockwise.
	 *
	 * Deliberately CW (negative shoelace) so the importer's winding normalisation is exercised
	 * rather than assumed - the exporter guarantees no particular winding.
	 *
	 * Edges, in the order written, for anything placing openings against them:
	 *   spur west  x = -8.7635, y -16.0189..-10.8329, along Y, 5.186 m
	 *   spur north y = -10.8329, x -8.7635..-7.7635,  along X, 1.000 m
	 *   spur east  x = -7.7635, y -10.8329..-16.0189, along Y, 5.186 m
	 *   leg north  y = -16.0189, x -7.7635..3.0505,   along X, 10.814 m
	 *   leg east   x =  3.0505, y -16.0189..-17.0189, along Y, 1.000 m
	 *   leg south  y = -17.0189, x 3.0505..-14.7495,  along X, 17.800 m
	 *   leg west   x = -14.7495, y -17.0189..-16.0189, along Y, 1.000 m
	 *   stub north y = -16.0189, x -14.7495..-8.7635, along X, 5.986 m
	 */
	FString MakeZonesDataSpaceObject()
	{
		return
			TEXT("    {\n")
			TEXT("      \"roomNumber\": 1,\n")
			TEXT("      \"name\": \"Corridor 15\",\n")
			TEXT("      \"floorElevation\": 0,\n")
			TEXT("      \"height\": 4,\n")
			TEXT("      \"tenability\": { \"odLimitPerM\": 0.26 },\n")
			TEXT("      \"polygons\": [ { \"vertices\": [\n")
			TEXT("        [-8.7635, -16.0189], [-8.7635, -10.8329], [-7.7635, -10.8329],\n")
			TEXT("        [-7.7635, -16.0189], [3.0505, -16.0189], [3.0505, -17.0189],\n")
			TEXT("        [-14.7495, -17.0189], [-14.7495, -16.0189]\n")
			TEXT("      ] } ]\n")
			TEXT("    }\n");
	}

	/** Zones-data.json carrying the true L-shaped footprint for room 1, and no openings[]. */
	FString MakeZonesDataJson()
	{
		return FString::Printf(
			TEXT("{\n")
			TEXT("  \"format\": \"simulex-zones-data\",\n")
			TEXT("  \"version\": 1,\n")
			TEXT("  \"source\": { \"coordinateSystem\": \"revit-internal\", \"iteration\": 1 },\n")
			TEXT("  \"spaces\": [\n%s  ]\n")
			TEXT("}\n"),
			*MakeZonesDataSpaceObject());
	}

	bool WriteLShapedScenario(const FString& TestDir, FString& OutSmvPath, bool bIncludeZonesJson)
	{
		OutSmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
		const FString CsvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox_zone.csv"));
		bool bOk = WriteTextFile(OutSmvPath, MakeLShapedSmv()) && WriteTextFile(CsvPath, MakeZoneCsv());
		if (bIncludeZonesJson)
		{
			bOk = bOk && WriteTextFile(FPaths::Combine(TestDir, TEXT("Zones-data.json")), MakeZonesDataJson());
		}
		return bOk;
	}

	double SignedAreaOf(const TArray<FVector2D>& Ring)
	{
		double Twice = 0.0;
		for (int32 i = 0, n = Ring.Num(); i < n; ++i)
		{
			Twice += (Ring[i].X * Ring[(i + 1) % n].Y) - (Ring[(i + 1) % n].X * Ring[i].Y);
		}
		return Twice * 0.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskFortranGluedNegativesTest,
	"ProjectMobius.BRisk.Importer.FortranGluedNegatives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskFortranGluedNegativesTest::RunTest(const FString& Parameters)
{
	// B-Risk writes .smv coordinates fixed-width, so a negative value's sign eats the
	// separating space: "-1.4750E+001-1.7019E+001 0.0000E+000" is THREE values, not two.
	// Whitespace splitting saw two and rejected the whole ROOM block, silently importing
	// rooms=0. Both shipped datasets have all-positive origins at (0,0), which is why this
	// stayed hidden until a model with real-world coordinates arrived.
	const FString TestDir = MakeBRiskTestDir();
	FString SmvPath;
	if (!TestTrue(TEXT("Scenario with glued negative origin should be written"),
		WriteLShapedScenario(TestDir, SmvPath, false)))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("Scenario should import"), FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	if (!TestEqual(TEXT("ROOM block with glued negatives must still parse"), Data.Rooms.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Origin X from glued token"), Data.Rooms[0].Origin.X, -14.750, 1.0e-3);
	TestEqual(TEXT("Origin Y from glued token"), Data.Rooms[0].Origin.Y, -17.019, 1.0e-3);
	TestEqual(TEXT("Origin Z"), Data.Rooms[0].Origin.Z, 0.0, 1.0e-6);
	TestEqual(TEXT("Size X"), Data.Rooms[0].Size.X, 22.986, 1.0e-3);
	TestEqual(TEXT("Size Y"), Data.Rooms[0].Size.Y, 1.0, 1.0e-6);
	TestEqual(TEXT("Size Z"), Data.Rooms[0].Size.Z, 4.0, 1.0e-6);

	IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskVentHeadHeightTest,
	"ProjectMobius.BRisk.Importer.VentHeadHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskVentHeadHeightTest::RunTest(const FString& Parameters)
{
	const FString TestDir = MakeBRiskTestDir();
	FString SmvPath;
	if (!TestTrue(TEXT("L-shaped scenario should be written"), WriteLShapedScenario(TestDir, SmvPath, false)))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("Scenario should import"), FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	if (!TestEqual(TEXT("Expected one vent"), Data.Vents.Num(), 1))
	{
		return false;
	}

	// VENTGEOM token[6] is the head height (2.25), token[5] the sill (0.9). The opening is
	// the difference. Getting this wrong overstates the vent area by the sill on every
	// window - here 1.05 x 2.25 = 2.36 m2 instead of the true 1.05 x 1.35 = 1.4175 m2.
	TestEqual(TEXT("Sill height should be the raw token"), Data.Vents[0].SillHeight, 0.9, 1.0e-6);
	TestEqual(TEXT("Opening height should be head minus sill"), Data.Vents[0].Height, 1.35, 1.0e-6);
	TestEqual(TEXT("Vent area should match B-Risk HVENT"), Data.Vents[0].Width * Data.Vents[0].Height, 1.4175, 1.0e-6);

	IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskZonesDataFootprintTest,
	"ProjectMobius.BRisk.Importer.ZonesDataFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskZonesDataFootprintTest::RunTest(const FString& Parameters)
{
	const FString TestDir = MakeBRiskTestDir();
	FString SmvPath;
	if (!TestTrue(TEXT("L-shaped scenario with zones JSON should be written"),
		WriteLShapedScenario(TestDir, SmvPath, true)))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("Scenario should import"), FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	if (!TestEqual(TEXT("Expected one room"), Data.Rooms.Num(), 1))
	{
		return false;
	}

	const FBRiskRoomGeometry& Room = Data.Rooms[0];
	if (!TestEqual(TEXT("Footprint should have 8 vertices"), Room.FootprintPolygon.Num(), 8))
	{
		return false;
	}

	// The JSON ring is authored clockwise; the importer must normalise it to CCW so
	// triangulation and point-in-polygon downstream get one convention.
	TestTrue(TEXT("Footprint winding should be normalised to CCW"), SignedAreaOf(Room.FootprintPolygon) > 0.0);

	// Area is the whole point: the equivalent rectangle and the true polygon agree on area
	// (that is how B-Risk derives it) but not on shape. Bounding box is ~4.8x the area.
	TestEqual(TEXT("Footprint area should match the B-Risk equivalent rectangle"),
		SignedAreaOf(Room.FootprintPolygon), 22.986, 1.0e-2);

	FBox2D Bounds(ForceInit);
	for (const FVector2D& V : Room.FootprintPolygon)
	{
		Bounds += V;
	}
	TestEqual(TEXT("Footprint should span the real 17.8 m X extent"), Bounds.GetSize().X, 17.8, 1.0e-3);
	TestEqual(TEXT("Footprint should span the real 6.186 m Y extent"), Bounds.GetSize().Y, 6.186, 1.0e-3);

	TestEqual(TEXT("Floor elevation should come from the JSON"), Room.FootprintFloorElevationM, 0.0, 1.0e-6);
	TestEqual(TEXT("Height should come from the JSON"), Room.FootprintHeightM, 4.0, 1.0e-6);
	// Distinguishes "the JSON said 0" from "the JSON said nothing", which matters because the
	// mesh builder only cross-checks these against the .smv when they were actually supplied.
	TestTrue(TEXT("Both JSON extents should be flagged as present"), Room.bHasFootprintExtents);
	TestTrue(TEXT("odLimitPerM should be captured"), Room.bHasOdLimitPerM);
	TestEqual(TEXT("odLimitPerM value"), Room.OdLimitPerM, 0.26, 1.0e-6);

	TestTrue(TEXT("Zones-data.json should be recorded as a referenced file"),
		Data.ReferencedFiles.ContainsByPredicate([](const FString& Path)
		{
			return Path.EndsWith(TEXT("Zones-data.json"));
		}));

	IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskZonesDataAbsentTest,
	"ProjectMobius.BRisk.Importer.ZonesDataAbsent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskZonesDataAbsentTest::RunTest(const FString& Parameters)
{
	const FString TestDir = MakeBRiskTestDir();
	FString SmvPath;
	if (!TestTrue(TEXT("L-shaped scenario without zones JSON should be written"),
		WriteLShapedScenario(TestDir, SmvPath, false)))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("Scenario should still import"), FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	// No JSON is a supported case: the add-in only emits it when the custom Revit plugin
	// was used. Rooms must fall back to the Origin/Size rectangle, not fail the import.
	if (!TestEqual(TEXT("Expected one room"), Data.Rooms.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Footprint should be empty without Zones-data.json"), Data.Rooms[0].FootprintPolygon.Num(), 0);
	TestFalse(TEXT("odLimitPerM should be absent"), Data.Rooms[0].bHasOdLimitPerM);
	TestEqual(TEXT("Legacy equivalent-rectangle size should be untouched"), Data.Rooms[0].Size.X, 22.986, 1.0e-3);

	IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskDataImporterSuccessTest,
	"ProjectMobius.BRisk.Importer.Success",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskDataImporterSuccessTest::RunTest(const FString& Parameters)
{
	const FString TestDir = MakeBRiskTestDir();
	FString SmvPath;
	FString CsvPath;
	if (!TestTrue(TEXT("Test SMV and CSV files should be written"), WriteScenario(TestDir, SmvPath, CsvPath)))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("Valid B-Risk scenario should import"),
		FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("Expected one room"), Data.Rooms.Num(), 1);
	TestEqual(TEXT("Expected room label"), Data.Rooms[0].Label, FString(TEXT("room")));
	TestEqual(TEXT("Expected one fire source"), Data.Fires.Num(), 1);
	TestEqual(TEXT("Expected one sprinkler"), Data.Sprinklers.Num(), 1);
	TestEqual(TEXT("Expected one vent"), Data.Vents.Num(), 1);
	TestEqual(TEXT("Expected one zone table"), Data.ZoneTables.Num(), 1);
	TestEqual(TEXT("Expected 61 time samples"), Data.ZoneTables[0].TimeSeconds.Num(), 61);
	TestEqual(TEXT("Expected 11 non-Time series"), Data.ZoneTables[0].Series.Num(), 11);

	const auto HasSeries = [&Data](const TCHAR* Name)
	{
		for (const FBRiskSeries& Series : Data.ZoneTables[0].Series)
		{
			if (Series.Name.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};

	TestTrue(TEXT("Expected ULT_1 series"), HasSeries(TEXT("ULT_1")));
	TestTrue(TEXT("Expected HGT_1 series"), HasSeries(TEXT("HGT_1")));
	TestTrue(TEXT("Expected HRR_1 series"), HasSeries(TEXT("HRR_1")));
	TestEqual(TEXT("Expected sprinkler response time"), Data.Sprinklers[0].ActivationTimeSeconds, 86.0);
	TestEqual(TEXT("Expected sprinkler radius"), Data.Sprinklers[0].SprayRadius, 3.25);

	return true;
}

// NOTE: FBRiskEgressHealthRewindHistoryTest (ProjectMobius.BRisk.EgressHealth.RewindHistory) was
// removed in the tenability-timeline v2 refactor (Task 4): it exercised
// UBRiskEgressSubsystem::RecordAgentHealth/RestoreAgentHealth and FAgentEgressHealthHistorySample,
// which are deleted (runtime-dead — tenability is now a precomputed per-agent timeline queried via
// FAgentTenabilityTimeline::DoseAt, with no rewind-history recording/restoring step). See
// BRiskTenabilityTest.cpp's FBRiskTenabilityFailureProjectionTest for the successor coverage of the
// failure-lock display contract this test partially exercised.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskIndependentLoadDefaultsTest,
	"ProjectMobius.BRisk.Loading.IndependentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskIndependentLoadDefaultsTest::RunTest(const FString& Parameters)
{
	const UBRiskDataSubsystem* DataSubsystem = NewObject<UBRiskDataSubsystem>();
	TestNotNull(TEXT("B-Risk data subsystem test object should be created"), DataSubsystem);
	if (!DataSubsystem)
	{
		return false;
	}

	TestFalse(
		TEXT("B-Risk loads should not replace the existing runtime building mesh by default"),
		DataSubsystem->GetAutoGenerateRoomGeometryOnLoad());
	TestFalse(
		TEXT("B-Risk loads should not replace agent playback duration or timestep by default"),
		DataSubsystem->GetConfigureSharedPlaybackOnLoad());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskRoomMeshDataTest,
	"ProjectMobius.BRisk.Geometry.RoomMeshData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskRoomMeshDataTest::RunTest(const FString& Parameters)
{
	TArray<FBRiskRoomGeometry> Rooms;
	TArray<FBRiskVentGeometry> Vents;
	FBRiskRoomGeometry& Room = Rooms.AddDefaulted_GetRef();
	Room.RoomId = 1;
	Room.Label = TEXT("room");
	Room.Origin = FVector::ZeroVector;
	Room.Size = FVector(24.0, 5.5, 2.6);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	FString Error;

	TestTrue(TEXT("Room geometry should build"),
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, BRiskCoord::ERoomFrame::SmokeviewSwap, Vertices, Triangles, Normals, &Error));
	TestEqual(TEXT("One single-sided box should produce twenty-four vertices"), Vertices.Num(), 24);
	TestEqual(TEXT("One single-sided box should produce thirty-six triangle indices"), Triangles.Num(), 36);
	TestEqual(TEXT("Normals should match vertex count"), Normals.Num(), Vertices.Num());
	TestTrue(TEXT("Floor normal should face down"), Normals.Num() > 0 && Normals[0].Equals(FVector::DownVector));
	TestTrue(TEXT("Roof normal should face up"), Normals.Num() > 4 && Normals[4].Equals(FVector::UpVector));

	const FBox Bounds(Vertices);
	TestTrue(TEXT("Room bounds should be valid"), Bounds.IsValid != 0);
	// BRiskCoord swaps X<->Y: B-Risk size (24 width, 5.5 depth) -> Unreal (Y=2400, X=550).
	TestEqual(TEXT("Room X extent should be 550 cm"), Bounds.GetSize().X, 550.0);
	TestEqual(TEXT("Room Y extent should be 2400 cm"), Bounds.GetSize().Y, 2400.0);
	TestEqual(TEXT("Room Z extent should be 260 cm"), Bounds.GetSize().Z, 260.0);

	const FBRiskRoomGeometry RoomCopy = Room;
	Rooms.Add(RoomCopy);
	TestTrue(TEXT("Multiple rooms should build"),
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, BRiskCoord::ERoomFrame::SmokeviewSwap, Vertices, Triangles, Normals, &Error));
	TestEqual(TEXT("Two single-sided boxes should produce forty-eight vertices"), Vertices.Num(), 48);
	TestEqual(TEXT("Two single-sided boxes should produce seventy-two triangle indices"), Triangles.Num(), 72);

	Rooms.SetNum(1);
	FBRiskVentGeometry& Vent = Vents.AddDefaulted_GetRef();
	Vent.FromRoomId = 1;
	Vent.ToRoomId = 2;
	Vent.Face = 4;
	Vent.Width = 2.4;
	Vent.Offset = 0.8;
	Vent.SillHeight = 0.0;
	Vent.Height = 2.4;
	TestTrue(TEXT("Room geometry with vent should build"),
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, BRiskCoord::ERoomFrame::SmokeviewSwap, Vertices, Triangles, Normals, &Error));
	TestEqual(TEXT("One floor-sill vented single-sided room should produce thirty-two vertices"), Vertices.Num(), 32);
	TestEqual(TEXT("One floor-sill vented single-sided room should produce forty-eight triangle indices"), Triangles.Num(), 48);

	// Under the BRiskCoord X<->Y swap (commit 3f293c7b), B-Risk face 4 (-X) is the UE -Y wall
	// (Y = 0) spanning X in [0, 550]. Offset 0.8 m puts the opening edge at X = 80 cm; head
	// height 2.4 m puts the top corner at Z = 240 cm -> the head-strip vertex is (80, 0, 240).
	// (The pre-swap test expected (2400, 80, 240); the bounds assertions above pin the new
	// convention.)
	bool bFoundOffsetVentEdge = false;
	for (const FVector& Vertex : Vertices)
	{
		if (FMath::IsNearlyEqual(Vertex.X, 80.0)
			&& FMath::IsNearlyEqual(Vertex.Y, 0.0)
			&& FMath::IsNearlyEqual(Vertex.Z, 240.0))
		{
			bFoundOffsetVentEdge = true;
			break;
		}
	}
	TestTrue(TEXT("Vent offset should be interpreted as the opening edge"), bFoundOffsetVentEdge);

	Rooms.Reset();
	TestFalse(TEXT("Empty room list should fail"),
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, BRiskCoord::ERoomFrame::SmokeviewSwap, Vertices, Triangles, Normals, &Error));

	return true;
}

namespace
{
	/**
	 * Real footprints from the 12 RoomTest Zones-data.json, verbatim. Room 1 is a plain
	 * rectangle; room 2 is the L-shaped corridor whose equivalent rectangle (22.986 x 1.0 m)
	 * bears no resemblance to its actual shape. Both rings are counter-clockwise in the source
	 * frame, so the Y negation turns them clockwise and the builder must re-wind them.
	 */
	FBRiskRoomGeometry MakeLobby14Room()
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = 1;
		Room.Label = TEXT("Lobby 14");
		Room.Origin = FVector(3.2505, -19.0189, 0.0);
		Room.Size = FVector(5.0, 2.786, 4.0);
		Room.FootprintFloorElevationM = 0.0;
		Room.FootprintHeightM = 4.0;
		Room.bHasFootprintExtents = true;
		Room.FootprintPolygon = {
			FVector2D(6.0365, -14.0189),
			FVector2D(3.2505, -14.0189),
			FVector2D(3.2505, -19.0189),
			FVector2D(6.0365, -19.0189),
		};
		return Room;
	}

	FBRiskRoomGeometry MakeCorridor15Room()
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = 2;
		Room.Label = TEXT("Corridor 15");
		Room.Origin = FVector(-14.7495, -17.0189, 0.0);
		Room.Size = FVector(22.986, 1.0, 4.0);
		Room.FootprintFloorElevationM = 0.0;
		Room.FootprintHeightM = 4.0;
		Room.bHasFootprintExtents = true;
		Room.FootprintPolygon = {
			FVector2D(-14.7495, -16.0189),
			FVector2D(-14.7495, -17.0189),
			FVector2D(3.0505,   -17.0189),
			FVector2D(3.0505,   -16.0189),
			FVector2D(-7.7635,  -16.0189),
			FVector2D(-7.7635,  -10.8329),
			FVector2D(-8.7635,  -10.8329),
			FVector2D(-8.7635,  -16.0189),
		};
		return Room;
	}

	/** Total area of every triangle whose normal faces straight down, i.e. the floor cap. */
	double FloorCapArea(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			if (!Normals[Triangles[Index]].Equals(FVector::DownVector))
			{
				continue;
			}

			const FVector& A = Vertices[Triangles[Index]];
			const FVector& B = Vertices[Triangles[Index + 1]];
			const FVector& C = Vertices[Triangles[Index + 2]];
			Area += 0.5 * FVector::CrossProduct(B - A, C - A).Size();
		}
		return Area;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskFootprintMaskTest,
	"ProjectMobius.BRisk.Geometry.FootprintMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskFootprintMaskTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 256;
	const double TexelCount = static_cast<double>(Resolution) * Resolution;

	const auto CoverageOf = [](const TArray<uint8>& Mask)
	{
		int32 Inside = 0;
		for (uint8 Texel : Mask)
		{
			Inside += (Texel != 0) ? 1 : 0;
		}
		return static_cast<double>(Inside);
	};

	TArray<uint8> Mask;

	// --- Rectangle fills its own bounding box completely --------------------------------------
	{
		const BRiskCoord::FRoomFootprintCm Footprint =
			BRiskCoord::MakeRoomFootprint(MakeLobby14Room(), 100.0f, BRiskCoord::ERoomFrame::Revit);
		BRiskCoord::RasteriseFootprintMask(Footprint, Resolution, Mask);

		TestEqual(TEXT("Mask should be Resolution squared"), Mask.Num(), Resolution * Resolution);
		TestEqual(TEXT("A rectangular room should cover its whole bbox"),
			CoverageOf(Mask) / TexelCount, 1.0, 1.0e-6);
	}

	// --- L-shaped corridor covers exactly its area fraction -----------------------------------
	//
	// This is the assertion that catches a wrong UV mapping or a flipped row order: the corridor's
	// true area is 22.986 m2 inside a 17.8 x 6.186 m bounding box, so coverage must be
	// 22.986 / 110.1108 = 0.2088. A mask that filled the bbox, or was transposed, would not.
	{
		const BRiskCoord::FRoomFootprintCm Footprint =
			BRiskCoord::MakeRoomFootprint(MakeCorridor15Room(), 100.0f, BRiskCoord::ERoomFrame::Revit);
		BRiskCoord::RasteriseFootprintMask(Footprint, Resolution, Mask);

		const double BBoxArea = Footprint.Bounds.GetSize().X * Footprint.Bounds.GetSize().Y;
		const double Expected = 229860.0 / BBoxArea;
		TestEqual(TEXT("L-shaped coverage should match its area fraction"),
			CoverageOf(Mask) / TexelCount, Expected, 0.005);

		// Row 0 is the bbox minimum Y, and which end of the corridor that is depends on the Y
		// NEGATION in FootprintToUnreal - not on the authored Revit values. Reasoning in Revit Y
		// here gets it exactly backwards, so spell the transform out:
		//
		//   long run   Revit Y [-17.0189, -16.0189] -> UE Y [16.0189, 17.0189]  = bbox MAXIMUM
		//   1 m stub   Revit Y [-16.0189, -10.8329] -> UE Y [10.8329, 16.0189]
		//   bbox       UE Y [10.8329, 17.0189], size 6.186 m
		//
		// So the long 17.8 m run hugs the bbox TOP, and row 0 crosses only the 1 m stub:
		// 1.0 / 17.8 = 5.6% of the row. Asserting both ends pins the row order in the one
		// direction that matters, because a vertical flip swaps these two numbers.
		int32 BottomRowInside = 0;
		int32 TopRowInside = 0;
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			BottomRowInside += (Mask[Column] != 0) ? 1 : 0;
			TopRowInside += (Mask[(Resolution - 1) * Resolution + Column] != 0) ? 1 : 0;
		}
		// ~5.6% (the stub only), and non-zero: an empty row would mean the stub was lost entirely.
		TestTrue(TEXT("Bottom mask row should cross the stub only"),
			BottomRowInside > 0 && BottomRowInside < Resolution / 8);
		// ~100%: the full length of the long run.
		TestTrue(TEXT("Top mask row should be almost fully inside"),
			TopRowInside > Resolution * 9 / 10);
	}

	// --- No polygon: all inside, so consumers need no branch ----------------------------------
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = 1;
		Room.Origin = FVector::ZeroVector;
		Room.Size = FVector(24.0, 5.5, 2.6);

		const BRiskCoord::FRoomFootprintCm Footprint = BRiskCoord::MakeRoomFootprint(Room, 100.0f, BRiskCoord::ERoomFrame::Revit);
		BRiskCoord::RasteriseFootprintMask(Footprint, Resolution, Mask);

		TestEqual(TEXT("A room with no footprint should mask nothing out"),
			CoverageOf(Mask) / TexelCount, 1.0, 1.0e-6);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskRoomLocalAxesTest,
	"ProjectMobius.BRisk.Geometry.RoomLocalAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskRoomLocalAxesTest::RunTest(const FString& Parameters)
{
	// B-Risk measures in-room positions against the equivalent rectangle, whose local X runs along
	// `L` - the LARGER root, not an oriented axis. Lobby 14's long side runs along world Y, so its
	// rectangle (5.0 x 2.786) is the real footprint (2.786 x 5.0) transposed, and every in-room
	// offset is transposed with it.

	// --- Transposed room: the real sprinkler and fire from 12 RoomTest ------------------------
	{
		const FBRiskRoomGeometry Room = MakeLobby14Room();
		BRiskCoord::ERoomLocalAxes Axes = BRiskCoord::ERoomLocalAxes::Unverified;

		// sprinklers.xml: sprx 2.8464, spry 1.3876, sprz 0.025 -> local Z = Size.Z - sprz.
		const FVector SprinklerWorld =
			BRiskCoord::RoomLocalToWorld(Room, FVector(2.8464, 1.3876, 3.975), Axes);
		TestTrue(TEXT("Lobby 14 should be detected as transposed"),
			Axes == BRiskCoord::ERoomLocalAxes::Transposed);

		const FVector SprinklerCm = BRiskCoord::FootprintToUnreal(SprinklerWorld, 100.0f);
		// Used as authored this lands at X = 609.69, which is 6 cm OUTSIDE the room's 603.65 edge.
		TestEqual(TEXT("Sprinkler X should be corrected to 463.81 cm"), SprinklerCm.X, 463.81, 0.01);
		TestEqual(TEXT("Sprinkler Y should be corrected to 1617.25 cm"), SprinklerCm.Y, 1617.25, 0.01);

		const BRiskCoord::FRoomFootprintCm Footprint = BRiskCoord::MakeRoomFootprint(Room, 100.0f, BRiskCoord::ERoomFrame::Revit);
		TestTrue(TEXT("Corrected sprinkler should be inside the room in X"),
			SprinklerCm.X >= Footprint.Bounds.Min.X && SprinklerCm.X <= Footprint.Bounds.Max.X);
		TestTrue(TEXT("Corrected sprinkler should be inside the room in Y"),
			SprinklerCm.Y >= Footprint.Bounds.Min.Y && SprinklerCm.Y <= Footprint.Bounds.Max.Y);
		// A sprinkler head centred across the room's short axis is the physically sensible answer,
		// and is the independent signal that the swap is right rather than merely in-bounds.
		TestEqual(TEXT("Corrected sprinkler should sit on the room's X centre"),
			SprinklerCm.X, Footprint.Bounds.GetCenter().X, 1.0);

		// .smv FIRE block for room 1: 2.65, 1.543, 0.30.
		const FVector FireWorld = BRiskCoord::RoomLocalToWorld(Room, FVector(2.65, 1.543, 0.30), Axes);
		const FVector FireCm = BRiskCoord::FootprintToUnreal(FireWorld, 100.0f);
		TestEqual(TEXT("Fire X should be corrected to 479.35 cm"), FireCm.X, 479.35, 0.01);
		TestEqual(TEXT("Fire Y should be corrected to 1636.89 cm"), FireCm.Y, 1636.89, 0.01);
		// 15 cm off dead centre both ways, versus 1.257 m off-centre in a 2.786 m room if untouched.
		TestEqual(TEXT("Corrected fire should sit 15 cm off the room centre in X"),
			FireCm.X - Footprint.Bounds.GetCenter().X, 15.0, 0.5);
	}

	// --- Aligned room: no swap --------------------------------------------------------------
	{
		FBRiskRoomGeometry Room = MakeLobby14Room();
		// Swap the equivalent rectangle so it matches the footprint as-is.
		Room.Size = FVector(2.786, 5.0, 4.0);

		BRiskCoord::ERoomLocalAxes Axes = BRiskCoord::ERoomLocalAxes::Unverified;
		const FVector World = BRiskCoord::RoomLocalToWorld(Room, FVector(1.0, 2.0, 0.5), Axes);

		TestTrue(TEXT("A matching rectangle should report Aligned"),
			Axes == BRiskCoord::ERoomLocalAxes::Aligned);
		TestEqual(TEXT("Aligned room should not swap X"), World.X, Room.Origin.X + 1.0, 1.0e-6);
		TestEqual(TEXT("Aligned room should not swap Y"), World.Y, Room.Origin.Y + 2.0, 1.0e-6);
	}

	// --- Non-rectangular room: unmappable, offsets left alone ---------------------------------
	{
		const FBRiskRoomGeometry Room = MakeCorridor15Room();
		BRiskCoord::ERoomLocalAxes Axes = BRiskCoord::ERoomLocalAxes::Unverified;
		const FVector World = BRiskCoord::RoomLocalToWorld(Room, FVector(1.0, 2.0, 0.5), Axes);

		// Equivalent rectangle 22.986 x 1.0 against a 17.8 x 6.186 footprint matches neither way,
		// so B-Risk's local frame genuinely cannot be recovered. Say so rather than guess.
		TestTrue(TEXT("The L-shaped corridor should report Unmappable"),
			Axes == BRiskCoord::ERoomLocalAxes::Unmappable);
		TestEqual(TEXT("Unmappable room should use the offset as authored"),
			World.X, Room.Origin.X + 1.0, 1.0e-6);
	}

	// --- No footprint at all: unverified, offsets left alone ----------------------------------
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = 9;
		Room.Origin = FVector(1.0, 2.0, 0.0);
		Room.Size = FVector(24.0, 5.5, 2.6);

		BRiskCoord::ERoomLocalAxes Axes = BRiskCoord::ERoomLocalAxes::Transposed;
		const FVector World = BRiskCoord::RoomLocalToWorld(Room, FVector(3.0, 4.0, 0.0), Axes);

		TestTrue(TEXT("A room with no JSON should report Unverified"),
			Axes == BRiskCoord::ERoomLocalAxes::Unverified);
		TestEqual(TEXT("Unverified room should use the offset as authored"), World.X, 4.0, 1.0e-6);
		TestEqual(TEXT("Unverified room should not swap Y"), World.Y, 6.0, 1.0e-6);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskRoomFootprintAccessorTest,
	"ProjectMobius.BRisk.Geometry.RoomFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskRoomFootprintAccessorTest::RunTest(const FString& Parameters)
{
	// MakeRoomFootprint is the single answer to "where is this room", shared by the smoke
	// volumes, the egress bounds that drive agent->room, and the generated mesh. If this drifts,
	// those three drift apart silently, so both branches are pinned here.

	// --- Polygon branch ---------------------------------------------------------------------
	{
		const BRiskCoord::FRoomFootprintCm Footprint =
			BRiskCoord::MakeRoomFootprint(MakeLobby14Room(), 100.0f, BRiskCoord::ERoomFrame::Revit);

		TestTrue(TEXT("Lobby 14 should resolve from its polygon"), Footprint.bFromPolygon);
		TestEqual(TEXT("Ring should keep all four vertices"), Footprint.Polygon.Num(), 4);
		// The source ring is CCW in B-Risk space; the Y negation flips it, so the accessor must
		// re-wind. A negative area here means that normalisation was skipped or done too early.
		TestTrue(TEXT("Ring should be counter-clockwise in UE space"),
			BRiskCoord::SignedRingArea(Footprint.Polygon) > 0.0);
		TestEqual(TEXT("Ring area should be 13.93 m2"),
			BRiskCoord::SignedRingArea(Footprint.Polygon), 139300.0, 1.0);

		TestEqual(TEXT("Polygon bounds min X"), Footprint.Bounds.Min.X, 325.05, 0.01);
		TestEqual(TEXT("Polygon bounds max X"), Footprint.Bounds.Max.X, 603.65, 0.01);
		TestEqual(TEXT("Polygon bounds min Y"), Footprint.Bounds.Min.Y, 1401.89, 0.01);
		TestEqual(TEXT("Polygon bounds max Y"), Footprint.Bounds.Max.Y, 1901.89, 0.01);
		TestEqual(TEXT("Polygon bounds min Z"), Footprint.Bounds.Min.Z, 0.0, 0.01);
		TestEqual(TEXT("Polygon bounds max Z from the .smv height"), Footprint.Bounds.Max.Z, 400.0, 0.01);
	}

	// --- Rectangle fallback, at the origin ----------------------------------------------------
	//
	// The InGameTenabilityScrub fixture depends on exactly these numbers: B-Risk dims 24 x 5.5 x 2.6
	// at origin (0,0,0) must give Min=(0,-550,0) Max=(2400,0,260). Under the old X<->Y swap it was
	// Min=(0,0,0) Max=(550,2400,260) — the transposed, unnegated form.
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = 1;
		Room.Origin = FVector::ZeroVector;
		Room.Size = FVector(24.0, 5.5, 2.6);

		const BRiskCoord::FRoomFootprintCm Footprint = BRiskCoord::MakeRoomFootprint(Room, 100.0f, BRiskCoord::ERoomFrame::Revit);

		TestFalse(TEXT("A room with no JSON should not claim a polygon"), Footprint.bFromPolygon);
		TestEqual(TEXT("Fallback should carry no ring"), Footprint.Polygon.Num(), 0);
		TestEqual(TEXT("Fallback min X"), Footprint.Bounds.Min.X, 0.0, 0.01);
		TestEqual(TEXT("Fallback max X"), Footprint.Bounds.Max.X, 2400.0, 0.01);
		TestEqual(TEXT("Fallback min Y"), Footprint.Bounds.Min.Y, -550.0, 0.01);
		TestEqual(TEXT("Fallback max Y"), Footprint.Bounds.Max.Y, 0.0, 0.01);
		TestEqual(TEXT("Fallback max Z"), Footprint.Bounds.Max.Z, 260.0, 0.01);
	}

	// --- Rectangle fallback, offset -----------------------------------------------------------
	//
	// At the origin the negation is invisible. Offset the room so it is not: room 1's real .smv
	// origin has a negative absy, so the min corner maps to the Y MAXIMUM. An FBox built from the
	// corner pair in order would come out inverted, which is why the accessor accumulates points.
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = 1;
		Room.Origin = FVector(3.2505, -19.0189, 0.0);
		Room.Size = FVector(5.0, 2.786, 4.0);

		const BRiskCoord::FRoomFootprintCm Footprint = BRiskCoord::MakeRoomFootprint(Room, 100.0f, BRiskCoord::ERoomFrame::Revit);

		TestTrue(TEXT("Offset fallback bounds should be valid"), Footprint.Bounds.IsValid != 0);
		TestEqual(TEXT("Offset fallback min X"), Footprint.Bounds.Min.X, 325.05, 0.01);
		TestEqual(TEXT("Offset fallback max X"), Footprint.Bounds.Max.X, 825.05, 0.01);
		TestEqual(TEXT("Offset fallback min Y"), Footprint.Bounds.Min.Y, 1623.29, 0.01);
		TestEqual(TEXT("Offset fallback max Y"), Footprint.Bounds.Max.Y, 1901.89, 0.01);
	}

	// --- Degenerate ring falls back rather than returning nothing -----------------------------
	//
	// Deliberately different from BuildRoomMeshDataFromRooms, which hard-fails on this: a mesh can
	// refuse to draw, but the smoke volume and the egress bounds must still get a box.
	{
		FBRiskRoomGeometry Room = MakeLobby14Room();
		Room.FootprintPolygon = {
			FVector2D(1.0, 1.0),
			FVector2D(1.0, 1.0),
			FVector2D(1.0, 1.0),
		};

		const BRiskCoord::FRoomFootprintCm Footprint = BRiskCoord::MakeRoomFootprint(Room, 100.0f, BRiskCoord::ERoomFrame::Revit);

		TestFalse(TEXT("Degenerate ring should not report a polygon"), Footprint.bFromPolygon);
		TestEqual(TEXT("Degenerate ring should be discarded"), Footprint.Polygon.Num(), 0);
		TestTrue(TEXT("Degenerate ring should still yield rectangle bounds"), Footprint.Bounds.IsValid != 0);
		TestEqual(TEXT("Degenerate fallback min X"), Footprint.Bounds.Min.X, 325.05, 0.01);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskRoomMeshFootprintTest,
	"ProjectMobius.BRisk.Geometry.RoomMeshFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskRoomMeshFootprintTest::RunTest(const FString& Parameters)
{
	const TArray<FBRiskVentGeometry> NoVents;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	FString Error;

	// --- Rectangle footprint: pins the coordinate transform -------------------------------
	//
	// This is the assertion that would catch the wrong frame. Room 1 sits at absx 3.2505,
	// absy -19.0189 in the .smv. Under BRiskCoord::FootprintToUnreal (Y negated) it must land
	// at X [325.05, 603.65], Y [1401.89, 1901.89]; under the legacy X<->Y swap it would land at
	// X [-1901.89, -1623.30] instead. The expected numbers come from the Datasmith door actors,
	// an artifact independent of both the .smv and the JSON.
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeLobby14Room() };

		TestTrue(TEXT("Rectangular footprint room should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, NoVents, 100.0f, BRiskCoord::ERoomFrame::Revit, Vertices, Triangles, Normals, &Error));

		// A simple N-gon prism is 2*(N-2) cap triangles + 2*N wall triangles, emitted unindexed.
		TestEqual(TEXT("A 4-gon prism should produce twelve triangles"), Triangles.Num() / 3, 12);
		TestEqual(TEXT("Every triangle should carry its own vertices"), Vertices.Num(), Triangles.Num());
		TestEqual(TEXT("Normals should match vertex count"), Normals.Num(), Vertices.Num());

		const FBox Bounds(Vertices);
		TestEqual(TEXT("Footprint min X should be 325.05 cm"), Bounds.Min.X, 325.05, 0.01);
		TestEqual(TEXT("Footprint max X should be 603.65 cm"), Bounds.Max.X, 603.65, 0.01);
		TestEqual(TEXT("Footprint min Y should be 1401.89 cm"), Bounds.Min.Y, 1401.89, 0.01);
		TestEqual(TEXT("Footprint max Y should be 1901.89 cm"), Bounds.Max.Y, 1901.89, 0.01);
		TestEqual(TEXT("Footprint min Z should be 0 cm"), Bounds.Min.Z, 0.0, 0.01);
		TestEqual(TEXT("Footprint max Z should come from the .smv height, 400 cm"), Bounds.Max.Z, 400.0, 0.01);

		// 2.786 x 5.0 m = 13.93 m^2. A triangulation that dropped or double-covered a triangle
		// would still produce a correct bounding box, so check the covered area too.
		TestEqual(TEXT("Floor cap area should equal the polygon area"),
			FloorCapArea(Vertices, Triangles, Normals), 139300.0, 1.0);

		int32 FloorTriangles = 0;
		int32 CeilingTriangles = 0;
		int32 WallTriangles = 0;
		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			const FVector& Normal = Normals[Triangles[Index]];
			FloorTriangles += Normal.Equals(FVector::DownVector) ? 1 : 0;
			CeilingTriangles += Normal.Equals(FVector::UpVector) ? 1 : 0;
			WallTriangles += FMath::IsNearlyZero(Normal.Z, 1e-4) ? 1 : 0;
		}
		TestEqual(TEXT("Floor cap should be two triangles"), FloorTriangles, 2);
		TestEqual(TEXT("Ceiling cap should be two triangles"), CeilingTriangles, 2);
		TestEqual(TEXT("Walls should be eight triangles"), WallTriangles, 8);

		// Outward-facing shell, matching the legacy box path. The rectangle is convex, so every
		// wall normal must point away from the footprint centre.
		const FVector Centre = Bounds.GetCenter();
		bool bAllWallsFaceOutward = true;
		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			const FVector& Normal = Normals[Triangles[Index]];
			if (!FMath::IsNearlyZero(Normal.Z, 1e-4))
			{
				continue;
			}

			const FVector ToFace = Vertices[Triangles[Index]] - Centre;
			bAllWallsFaceOutward &= FVector::DotProduct(Normal, ToFace) > 0.0;
		}
		TestTrue(TEXT("Wall normals should face out of the room"), bAllWallsFaceOutward);
	}

	// --- L-shaped footprint: the case the equivalent rectangle cannot express ---------------
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeCorridor15Room() };

		TestTrue(TEXT("L-shaped footprint room should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, NoVents, 100.0f, BRiskCoord::ERoomFrame::Revit, Vertices, Triangles, Normals, &Error));
		TestEqual(TEXT("An 8-gon prism should produce twenty-eight triangles"), Triangles.Num() / 3, 28);

		const FBox Bounds(Vertices);
		TestEqual(TEXT("Corridor min X should be -1474.95 cm"), Bounds.Min.X, -1474.95, 0.01);
		TestEqual(TEXT("Corridor max X should be 305.05 cm"), Bounds.Max.X, 305.05, 0.01);
		TestEqual(TEXT("Corridor min Y should be 1083.29 cm"), Bounds.Min.Y, 1083.29, 0.01);
		TestEqual(TEXT("Corridor max Y should be 1701.89 cm"), Bounds.Max.Y, 1701.89, 0.01);

		// 22.986 m^2 — the true L-shaped area, not the 22.986 x 1.0 m equivalent rectangle's
		// bounding box (17.8 x 6.186 m = 110 m^2).
		TestEqual(TEXT("Floor cap area should equal the L-shaped polygon area"),
			FloorCapArea(Vertices, Triangles, Normals), 229860.0, 1.0);
	}

	// --- Mixed scenario: footprint room plus a legacy rectangle room ------------------------
	{
		FBRiskRoomGeometry RectangleRoom;
		RectangleRoom.RoomId = 3;
		RectangleRoom.Label = TEXT("No footprint");
		RectangleRoom.Origin = FVector::ZeroVector;
		RectangleRoom.Size = FVector(24.0, 5.5, 2.6);

		const TArray<FBRiskRoomGeometry> Rooms = { MakeLobby14Room(), RectangleRoom };

		// Registered at Warning, not via AddExpectedError: that one registers the pattern at
		// Error verbosity, which would not intercept this message.
		AddExpectedMessagePlain(TEXT("mixes coordinate frames"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1);
		TestTrue(TEXT("Mixed footprint/rectangle scenario should still build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, NoVents, 100.0f, BRiskCoord::ERoomFrame::Revit, Vertices, Triangles, Normals, &Error));
		// 12 polygon triangles + the box path's 12 (six quads).
		TestEqual(TEXT("Mixed scenario should produce twenty-four triangles"), Triangles.Num() / 3, 24);
	}

	// --- Degenerate footprint fails loudly rather than silently falling back ----------------
	{
		FBRiskRoomGeometry DegenerateRoom = MakeLobby14Room();
		DegenerateRoom.FootprintPolygon = {
			FVector2D(1.0, 1.0),
			FVector2D(1.0, 1.0),
			FVector2D(1.0, 1.0),
		};

		const TArray<FBRiskRoomGeometry> Rooms = { DegenerateRoom };
		Error.Reset();
		TestFalse(TEXT("Degenerate footprint should fail"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, NoVents, 100.0f, BRiskCoord::ERoomFrame::Revit, Vertices, Triangles, Normals, &Error));
		TestTrue(TEXT("Degenerate footprint should name the room"), Error.Contains(TEXT("Lobby 14")));
		TestEqual(TEXT("Failed build should emit no geometry"), Vertices.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskSmokeMappingTest,
	"ProjectMobius.BRisk.Smoke.Mapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskSmokeMappingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Full layer height should leave the room clear"),
		UBRiskDataSubsystem::ComputeRoomSmokeScalar(2.6, 2.6),
		1.0f);
	TestEqual(TEXT("Half layer height should half-fill the room"),
		UBRiskDataSubsystem::ComputeRoomSmokeScalar(1.3, 2.6),
		0.5f);
	TestEqual(TEXT("Zero layer height should fully smoke the room"),
		UBRiskDataSubsystem::ComputeRoomSmokeScalar(0.0, 2.6),
		0.0f);
	TestEqual(TEXT("Invalid room height should fail clear"),
		UBRiskDataSubsystem::ComputeRoomSmokeScalar(1.0, 0.0),
		1.0f);

	const FBRiskSmokeVisualState ClearState = UBRiskDataSubsystem::ComputeSmokeVisualState(
		2.59974,
		2.6,
		0.0,
		100.0f,
		0.00045,
		0.0002,
		24.0,
		23.0);
	TestTrue(TEXT("Near-full layer height should be nearly clear"),
		FMath::IsNearlyEqual(ClearState.RoomSmoke, 0.9999f, 0.0002f));
	TestTrue(TEXT("Tiny ULOD_1 should produce near-zero density"),
		ClearState.SmokeDensity < 0.001f);
	TestEqual(TEXT("Ambient upper temperature should produce no heat tint"),
		ClearState.SmokeHeat,
		0.0f);

	const FBRiskSmokeVisualState MidState = UBRiskDataSubsystem::ComputeSmokeVisualState(
		1.3883,
		2.6,
		0.0,
		100.0f,
		4.45,
		0.2,
		222.0,
		80.0);
	TestTrue(TEXT("100s layer height should map to expected RoomSmoke"),
		FMath::IsNearlyEqual(MidState.RoomSmoke, 0.534f, 0.001f));
	TestTrue(TEXT("High ULOD_1 should produce high visual density"),
		MidState.SmokeDensity > 0.78f);
	TestTrue(TEXT("Upper temperature near 222C should produce near-full heat tint"),
		FMath::IsNearlyEqual(MidState.SmokeHeat, 0.99f, 0.001f));
	TestEqual(TEXT("Upper optical density should be preserved in state"),
		MidState.UpperOpticalDensity,
		4.45f);
	TestEqual(TEXT("Lower optical density should be preserved in state"),
		MidState.LowerOpticalDensity,
		0.2f);
	TestTrue(TEXT("Upper optical density should convert to extinction per cm"),
		FMath::IsNearlyEqual(MidState.UpperExtinctionPerCm, 0.102465f, 0.000001f));
	TestTrue(TEXT("Lower optical density should convert to extinction per cm"),
		FMath::IsNearlyEqual(MidState.LowerExtinctionPerCm, 0.00460517f, 0.000001f));
	TestEqual(TEXT("Lower-layer temperature should be preserved in state"),
		MidState.LowerTemperatureC,
		80.0f);

	const FBRiskSmokeVisualState MissingLowerState = UBRiskDataSubsystem::ComputeSmokeVisualState(
		1.3883,
		2.6,
		0.0,
		100.0f,
		4.45,
		0.0,
		222.0,
		80.0);
	TestEqual(TEXT("Missing LLOD_1 fallback should produce zero lower extinction"),
		MissingLowerState.LowerExtinctionPerCm,
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskDataImporterFailureTest,
	"ProjectMobius.BRisk.Importer.Failures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskDataImporterFailureTest::RunTest(const FString& Parameters)
{
	FBRiskScenarioData Data;
	FString Error;

	TestFalse(TEXT("Missing SMV should fail"),
		FBRiskDataImporter::ImportScenarioFromSmv(
			FPaths::Combine(MakeBRiskTestDir(), TEXT("missing.smv")), Data, &Error));

	const FString WrongExtDir = MakeBRiskTestDir();
	const FString WrongExtPath = FPaths::Combine(WrongExtDir, TEXT("scenario.txt"));
	TestTrue(TEXT("Wrong extension file should be written"), WriteTextFile(WrongExtPath, MakeSmv()));
	TestFalse(TEXT("Wrong extension should fail"),
		FBRiskDataImporter::ImportScenarioFromSmv(WrongExtPath, Data, &Error));

	const FString NoZoneDir = MakeBRiskTestDir();
	const FString NoZoneSmv = FPaths::Combine(NoZoneDir, TEXT("no_zone.smv"));
	TestTrue(TEXT("No-ZONE SMV should be written"), WriteTextFile(NoZoneSmv, TEXT("ROOM 1\n1 1 1\n0 0 0\n")));
	TestFalse(TEXT("SMV without ZONE should fail"),
		FBRiskDataImporter::ImportScenarioFromSmv(NoZoneSmv, Data, &Error));

	// A referenced-but-ABSENT zone CSV used to be asserted here as a failure. It is now a
	// supported geometry-only import - see ProjectMobius.BRisk.Importer.GeometryOnlyWhenResultsMissing.
	// The two cases below keep the other half of that contract: a CSV that is PRESENT and does not
	// parse must still fail, so a corrupted run cannot pass itself off as one that never ran.

	const FString MissingTimeDir = MakeBRiskTestDir();
	const FString MissingTimeSmv = FPaths::Combine(MissingTimeDir, TEXT("missing_time.smv"));
	const FString MissingTimeCsv = FPaths::Combine(MissingTimeDir, TEXT("basemodel_testBox_zone.csv"));
	TestTrue(TEXT("Missing-Time SMV should be written"), WriteTextFile(MissingTimeSmv, MakeSmv()));
	TestTrue(TEXT("Missing-Time CSV should be written"),
		WriteTextFile(MissingTimeCsv, TEXT("s,C\nULT_1,HRR_1\n24,100\n")));
	TestFalse(TEXT("CSV missing Time should fail"),
		FBRiskDataImporter::ImportScenarioFromSmv(MissingTimeSmv, Data, &Error));

	const FString MalformedDir = MakeBRiskTestDir();
	const FString MalformedSmv = FPaths::Combine(MalformedDir, TEXT("malformed.smv"));
	const FString MalformedCsv = FPaths::Combine(MalformedDir, TEXT("basemodel_testBox_zone.csv"));
	TestTrue(TEXT("Malformed SMV should be written"), WriteTextFile(MalformedSmv, MakeSmv()));
	TestTrue(TEXT("Malformed CSV should be written"),
		WriteTextFile(MalformedCsv, TEXT("s,C\nTime,ULT_1\n0,not-a-number\n")));
	TestFalse(TEXT("Malformed numeric cell should fail"),
		FBRiskDataImporter::ImportScenarioFromSmv(MalformedSmv, Data, &Error));

	return true;
}

// --- Openings[]: real vent placement from the Revit add-in --------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskOpeningsPlacementTest,
	"ProjectMobius.BRisk.Importer.OpeningsPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskOpeningsPlacementTest::RunTest(const FString& Parameters)
{
	// One L-shaped room, and a Zones-data.json carrying BOTH the footprint and an openings array.
	// The .smv deliberately disagrees: its single VENTGEOM sits on face 3 at offset 0 - the shape
	// that stacks every vent on one bounding-box wall. openings[] must win.
	const FString TestDir = MakeBRiskTestDir();
	const FString SmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
	const FString CsvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox_zone.csv"));
	const FString JsonPath = FPaths::Combine(TestDir, TEXT("Zones-data.json"));

	// Three openings on the L described by MakeZonesDataSpaceObject, each on the wall CENTRELINE -
	// 0.1 m outside the footprint polygon, exactly as the real add-in emits them:
	//   door + leakage on "leg south" (y = -17.0189, runs along X) -> centre y = -17.1189
	//   window on "spur east"         (x =  -7.7635, runs along Y) -> centre x =  -7.6635
	// The two axes are both covered on purpose: a hard-coded or bounding-box-derived axis would
	// place the window identically to the door and still pass a single-axis test.
	const FString ZonesJson = FString::Printf(
		TEXT("{\n")
		TEXT("  \"format\": \"simulex-zones-data\", \"version\": 1,\n")
		TEXT("  \"source\": { \"coordinateSystem\": \"revit-internal\", \"iteration\": 1 },\n")
		TEXT("  \"spaces\": [\n%s  ],\n")
		TEXT("  \"openings\": [\n")
		TEXT("    { \"ventId\": 1, \"type\": \"door\", \"roomA\": 1, \"exterior\": true,\n")
		TEXT("      \"centre\": [-10.75, -17.1189, 1.067], \"normal\": [0, -1],\n")
		TEXT("      \"width\": 0.9, \"modelledWidth\": 0.45, \"height\": 2.134, \"sillHeight\": 0.0,\n")
		TEXT("      \"openTimeS\": 10, \"closeTimeS\": 60 },\n")
		TEXT("    { \"ventId\": 2, \"type\": \"leakage\", \"roomA\": 1, \"exterior\": true,\n")
		TEXT("      \"leakageOf\": \"door\",\n")
		TEXT("      \"centre\": [-10.2, -17.1189, 1.067], \"normal\": [0, -1],\n")
		TEXT("      \"width\": 0.01, \"modelledWidth\": 0.01, \"height\": 2.134, \"sillHeight\": 0.0,\n")
		TEXT("      \"openTimeS\": 0, \"closeTimeS\": 0 },\n")
		TEXT("    { \"ventId\": 3, \"type\": \"window\", \"roomA\": 1, \"exterior\": true,\n")
		TEXT("      \"centre\": [-7.6635, -13.5, 1.575], \"normal\": [1, 0],\n")
		TEXT("      \"width\": 1.05, \"modelledWidth\": 1.05, \"height\": 1.35, \"sillHeight\": 0.9,\n")
		TEXT("      \"openTimeS\": 0, \"closeTimeS\": 0 }\n")
		TEXT("  ]\n")
		TEXT("}\n"),
		*MakeZonesDataSpaceObject());

	if (!TestTrue(TEXT("Openings scenario should be written"),
		WriteTextFile(SmvPath, MakeLShapedSmv())
			&& WriteTextFile(CsvPath, MakeZoneCsv())
			&& WriteTextFile(JsonPath, ZonesJson)))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("Scenario with openings should import"),
		FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	// openings[] REPLACES the VENTGEOM list rather than merging with it. The .smv declares one vent;
	// three come back, all from the JSON.
	if (!TestEqual(TEXT("Openings should replace the VENTGEOM vents"), Data.Vents.Num(), 3))
	{
		return false;
	}

	const FBRiskVentGeometry& Door = Data.Vents[0];
	const FBRiskVentGeometry& Leak = Data.Vents[1];
	const FBRiskVentGeometry& Window = Data.Vents[2];

	TestEqual(TEXT("Door kind"), static_cast<int32>(Door.Kind), static_cast<int32>(EBRiskVentKind::Door));
	TestEqual(TEXT("Leakage kind"), static_cast<int32>(Leak.Kind), static_cast<int32>(EBRiskVentKind::Leakage));
	TestEqual(TEXT("Window kind"), static_cast<int32>(Window.Kind), static_cast<int32>(EBRiskVentKind::Window));
	TestTrue(TEXT("Placement should be flagged"), Door.bHasPlacement && Leak.bHasPlacement && Window.bHasPlacement);

	// Width keeps meaning "what B-Risk simulated" so flow-area consumers are untouched by the JSON
	// appearing; PhysicalWidth is the real leaf. Getting these the wrong way round draws every door
	// at half size, which looks plausible and is wrong.
	TestEqual(TEXT("Door Width should stay the MODELLED width"), Door.Width, 0.45, 1.0e-9);
	TestEqual(TEXT("Door PhysicalWidth should be the real width"), Door.PhysicalWidth, 0.9, 1.0e-9);

	// Exterior openings must keep landing on B-Risk's outside-room id (rooms + 1) so FindRoomById
	// still returns null for them and adjacency behaves exactly as it did.
	TestEqual(TEXT("Exterior vent should point at the outside room id"), Door.ToRoomId, Data.Rooms.Num() + 1);
	TestTrue(TEXT("Exterior flag should survive"), Door.bExterior);
	TestEqual(TEXT("Open time should survive"), Door.OpenTimeSeconds, 10.0, 1.0e-9);
	TestEqual(TEXT("Close time should survive"), Door.CloseTimeSeconds, 60.0, 1.0e-9);

	// Face and Offset are cleared, not left stale: a future reader must not be able to treat them as
	// a fallback, because both are coordinates in B-Risk's equivalent rectangle.
	TestEqual(TEXT("Face should be cleared once a centre exists"), Door.Face, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Offset should be cleared once a centre exists"), Door.Offset, 0.0, 1.0e-9);

	// --- Placement: the centre is used verbatim and the polygon supplies only the axis -----------
	constexpr float Scale = 100.0f;
	constexpr float ThicknessCm = 8.0f;
	const FBRiskRoomGeometry& Room = Data.Rooms[0];

	FVector CentreCm = FVector::ZeroVector;
	FVector SizeCm = FVector::ZeroVector;
	if (!TestTrue(TEXT("Door should place from its centre"),
		ABRiskHazardVisualizer::ComputeVentSlab(
			Door, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm, CentreCm, SizeCm)))
	{
		return false;
	}

	// Verbatim: FootprintToUnreal is (x, -y, z) * Scale, so -10.75, -17.1189 -> -1075, +1711.89 cm.
	// NOT projected onto the polygon - the 0.1 m stand-off is half a wall thickness and is the only
	// position a shared wall's two rooms agree on.
	TestEqual(TEXT("Door X should be the centre, untouched"), CentreCm.X, -1075.0, 1.0e-3);
	TestEqual(TEXT("Door Y should be the centre, untouched"), CentreCm.Y, 1711.89, 1.0e-3);
	TestEqual(TEXT("Door Z should come from sill and height, not centre z"), CentreCm.Z, 106.7, 1.0e-3);

	// "leg south" runs along X, so the opening runs along X and the slab is thin in Y.
	TestEqual(TEXT("Door should run along X at its real width"), SizeCm.X, 90.0, 1.0e-3);
	TestEqual(TEXT("Door should be thin across the wall"), SizeCm.Y, static_cast<double>(ThicknessCm), 1.0e-3);
	TestEqual(TEXT("Door height"), SizeCm.Z, 213.4, 1.0e-3);

	// The window sits on "spur east", which runs along Y - the other axis. If the axis were
	// hard-coded or taken from the bounding box this would come out identical to the door.
	if (TestTrue(TEXT("Window should place from its centre"),
		ABRiskHazardVisualizer::ComputeVentSlab(
			Window, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm, CentreCm, SizeCm)))
	{
		TestEqual(TEXT("Window X should be the centre, untouched"), CentreCm.X, -766.35, 1.0e-3);
		TestEqual(TEXT("Window should run along Y"), SizeCm.Y, 105.0, 1.0e-3);
		TestEqual(TEXT("Window should be thin across the wall"), SizeCm.X, static_cast<double>(ThicknessCm), 1.0e-3);
		// Sill 0.9 head 2.25 -> centre 1.575 m. Proves Z ignores the JSON centre z entirely.
		TestEqual(TEXT("Window Z from sill+height"), CentreCm.Z, 157.5, 1.0e-3);
	}

	// Two vents 0.55 m apart must NOT land on top of each other - the failure the zero .smv offset
	// caused, where 26 markers collapsed onto six positions.
	FVector LeakCentreCm = FVector::ZeroVector;
	FVector LeakSizeCm = FVector::ZeroVector;
	int32 LeakNormalAxis = INDEX_NONE;
	if (TestTrue(TEXT("Leakage vent should place from its centre"),
		ABRiskHazardVisualizer::ComputeVentSlab(
			Leak, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm, LeakCentreCm, LeakSizeCm,
			&LeakNormalAxis)))
	{
		TestEqual(TEXT("Leakage should sit 0.55 m from the door"), LeakCentreCm.X, -1020.0, 1.0e-3);
		TestEqual(TEXT("Leakage keeps its hairline width"), LeakSizeCm.X, 1.0, 1.0e-3);

		// The reported wall normal must be the WALL's, not the box's thinnest axis. This opening is
		// 1 cm wide in an 8 cm slab, so "thinnest axis" would answer X and build the outline in the
		// wrong plane - a door-sized ghost lying across the wall. It has to answer Y, the same as
		// the door beside it on the same wall.
		TestEqual(TEXT("Leakage normal axis must be the wall's, not the thinnest axis"), LeakNormalAxis, 1);
		TestTrue(TEXT("Leakage is narrower than the slab is thick - the case that broke it"),
			LeakSizeCm.X < LeakSizeCm.Y);
	}

	int32 DoorNormalAxis = INDEX_NONE;
	if (ABRiskHazardVisualizer::ComputeVentSlab(
		Door, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm, CentreCm, SizeCm, &DoorNormalAxis))
	{
		TestEqual(TEXT("Door on the same wall reports the same normal axis"), DoorNormalAxis, LeakNormalAxis);
	}

	IFileManager::Get().DeleteDirectory(*TestDir, false, true);

	// --- The real 34-opening export ---------------------------------------------------------
	//
	// The synthetic fixture above pins the arithmetic. It cannot catch the two failures the owner
	// actually saw, because both are properties of the whole SET: markers landing off the building
	// (bounding-box walls), and 26 markers collapsing onto six positions (every .smv offset is 0).
	// Assert those directly. Skips when the fixture is not on this machine.
	const TCHAR* InternalRoots[] =
	{
		TEXT("D:/NickWork/Mobius_InternalData"),
		TEXT("E:/00_Work/Mobius_InternalData"),
		TEXT("F:/Mobius_InternalData"),
	};
	FString RealSmv;
	for (const TCHAR* Root : InternalRoots)
	{
		const FString Candidate = FPaths::Combine(
			FString(Root), TEXT("12-room-test-vents"), TEXT("basemodel_default"), TEXT("basemodel_default.smv"));
		if (FPaths::FileExists(Candidate))
		{
			RealSmv = Candidate;
			break;
		}
	}
	if (RealSmv.IsEmpty())
	{
		AddInfo(TEXT("real-dataset placement check SKIPPED: 12-room-test-vents fixture not present"));
		return true;
	}

	FBRiskScenarioData RealData;
	FString RealError;
	if (!TestTrue(TEXT("The 34-opening export should import"),
		FBRiskDataImporter::ImportScenarioFromSmv(RealSmv, RealData, &RealError)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *RealError));
		return false;
	}
	TestEqual(TEXT("All 34 openings should be present"), RealData.Vents.Num(), 34);

	int32 Placed = 0;
	int32 OffWall = 0;
	double WorstStandOffCm = 0.0;
	TArray<FVector> Centres;

	for (int32 VentIndex = 0; VentIndex < RealData.Vents.Num(); ++VentIndex)
	{
		const FBRiskVentGeometry& Vent = RealData.Vents[VentIndex];
		const FBRiskRoomGeometry* VentRoom = RealData.Rooms.FindByPredicate(
			[&Vent](const FBRiskRoomGeometry& Candidate) { return Candidate.RoomId == Vent.FromRoomId; });
		if (!VentRoom)
		{
			continue;
		}

		FVector MarkerCm = FVector::ZeroVector;
		FVector MarkerSizeCm = FVector::ZeroVector;
		int32 MarkerNormalAxis = INDEX_NONE;
		if (!ABRiskHazardVisualizer::ComputeVentSlab(
			Vent, VentRoom, nullptr, Scale, RealData.RoomFrame, ThicknessCm, MarkerCm, MarkerSizeCm,
			&MarkerNormalAxis))
		{
			continue;
		}
		++Placed;
		Centres.Add(MarkerCm);

		// Every marker must be thin along the WALL normal. 18 of these 34 are leakage paths narrower
		// than the 8 cm slab, so an outline built on "thinnest axis" would lie across the wall.
		if (MarkerNormalAxis != 0 && MarkerNormalAxis != 1)
		{
			AddError(FString::Printf(TEXT("Vent %d reported normal axis %d."), Vent.VentId, MarkerNormalAxis));
		}
		else if (!FMath::IsNearlyEqual(MarkerSizeCm[MarkerNormalAxis], static_cast<double>(ThicknessCm), 1.0e-3))
		{
			AddError(FString::Printf(
				TEXT("Vent %d is %.2f cm along its reported normal axis, expected the %.2f cm slab."),
				Vent.VentId, MarkerSizeCm[MarkerNormalAxis], ThicknessCm));
		}

		// On a real wall: the centre is the wall centreline, so it stands off the footprint polygon
		// by half a wall thickness - a measured, constant 10 cm here. Allow 15 cm and no more; the
		// bounding-box placement this replaces put markers metres out into open space.
		const BRiskCoord::FRoomFootprintCm VentRoomFootprint =
			BRiskCoord::MakeRoomFootprint(*VentRoom, Scale, RealData.RoomFrame);
		const TArray<FVector2D>& Ring = VentRoomFootprint.Polygon;
		double NearestCm = TNumericLimits<double>::Max();
		for (int32 EdgeIndex = 0; EdgeIndex < Ring.Num(); ++EdgeIndex)
		{
			const FVector2D& A = Ring[EdgeIndex];
			const FVector2D& B = Ring[(EdgeIndex + 1) % Ring.Num()];
			const FVector2D Along = B - A;
			const double LengthSq = Along.SizeSquared();
			if (LengthSq <= 0.0)
			{
				continue;
			}
			const FVector2D Point(MarkerCm.X, MarkerCm.Y);
			const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Along) / LengthSq, 0.0, 1.0);
			NearestCm = FMath::Min(NearestCm, FVector2D::Distance(Point, A + Along * T));
		}
		WorstStandOffCm = FMath::Max(WorstStandOffCm, NearestCm);
		if (NearestCm > 15.0)
		{
			++OffWall;
			AddError(FString::Printf(
				TEXT("Vent %d (room %d) sits %.1f cm from the nearest wall of its own room."),
				Vent.VentId, Vent.FromRoomId, NearestCm));
		}
	}

	// Face 5 used to drop 8 of the 34, including the only interior door. With a centre per opening
	// there is no face left to be unhandled, so every one must place.
	TestEqual(TEXT("Every opening should place - no face-id drops left"), Placed, 34);
	TestEqual(TEXT("No opening should sit off its own room's walls"), OffWall, 0);
	AddInfo(FString::Printf(TEXT("worst wall stand-off %.2f cm across %d placed openings"),
		WorstStandOffCm, Placed));

	// The stacking check. 26 markers previously collapsed onto six positions because every .smv
	// offset is 0; distinct openings must now occupy distinct places.
	int32 Coincident = 0;
	for (int32 i = 0; i < Centres.Num(); ++i)
	{
		for (int32 j = i + 1; j < Centres.Num(); ++j)
		{
			if (Centres[i].Equals(Centres[j], 1.0))
			{
				++Coincident;
			}
		}
	}
	TestEqual(TEXT("No two openings should render at the same point"), Coincident, 0);

	return true;
}

// --- Geometry-only import when the model has not been simulated yet -----------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskGeometryOnlyWhenResultsMissingTest,
	"ProjectMobius.BRisk.Importer.GeometryOnlyWhenResultsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskGeometryOnlyWhenResultsMissingTest::RunTest(const FString& Parameters)
{
	// A B-Risk model exported but not yet run: the .smv names its results file because the .smv is
	// written at authoring time, but nothing has produced it. Real instance of this shape:
	// D:\NickWork\Mobius_InternalData\12-room-test-vents\basemodel_default.
	const FString TestDir = MakeBRiskTestDir();
	const FString SmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
	if (!TestTrue(TEXT("SMV without results should be written"), WriteTextFile(SmvPath, MakeSmv())))
	{
		return false;
	}

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("A model with no results should still import"),
		FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	// The whole point of degrading: the building survives. Asserting each of these separately
	// because dropping any one of them would still leave a "successful" import that shows nothing.
	TestEqual(TEXT("Rooms should survive a missing zone CSV"), Data.Rooms.Num(), 1);
	TestEqual(TEXT("Fires should survive a missing zone CSV"), Data.Fires.Num(), 1);
	TestEqual(TEXT("Vents should survive a missing zone CSV"), Data.Vents.Num(), 1);

	TestFalse(TEXT("bHasResultsData should be false"), Data.bHasResultsData);
	TestEqual(TEXT("No zone tables should have been produced"), Data.ZoneTables.Num(), 0);

	// The prompt is built from these paths, so an empty list would give the user a dialog that
	// names nothing to go and fix.
	const FString ExpectedCsv = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(TestDir, TEXT("basemodel_testBox_zone.csv")));
	const FString ExpectedOutputXml = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(TestDir, TEXT("output1.xml")));
	TestTrue(TEXT("Missing zone CSV should be listed"), Data.MissingResultFiles.Contains(ExpectedCsv));
	TestTrue(TEXT("Missing output1.xml should be listed"), Data.MissingResultFiles.Contains(ExpectedOutputXml));

	// The boundary that makes the degrade safe: a results file that EXISTS and carries no samples is
	// a broken run, not an unrun model, and must still stop the import. Without this, the natural
	// "make empty results degrade too" follow-up would let a corrupted B-Risk run present as a
	// building nobody has simulated - the one confusion this whole change exists to prevent.
	const FString EmptyDir = MakeBRiskTestDir();
	const FString EmptySmv = FPaths::Combine(EmptyDir, TEXT("basemodel_testBox.smv"));
	const FString EmptyCsv = FPaths::Combine(EmptyDir, TEXT("basemodel_testBox_zone.csv"));
	if (TestTrue(TEXT("Header-only scenario should be written"),
		WriteTextFile(EmptySmv, MakeSmv()) && WriteTextFile(EmptyCsv, TEXT("s,C\nTime,ULT_1\n"))))
	{
		FBRiskScenarioData EmptyData;
		FString EmptyError;
		TestFalse(TEXT("A present zone CSV with no data rows should still fail"),
			FBRiskDataImporter::ImportScenarioFromSmv(EmptySmv, EmptyData, &EmptyError));
	}
	IFileManager::Get().DeleteDirectory(*EmptyDir, false, true);
	IFileManager::Get().DeleteDirectory(*TestDir, false, true);

	// The synthetic .smv above is one block of each kind. The real export is 34 VENTGEOM blocks, two
	// ROOMs, LABEL pairs and a THCP the parser does not handle - so it exercises the degrade on a
	// shape the fixture cannot reproduce. Skips when the fixture is not on this machine, matching
	// Hdf5ImportMatrixTest / MobiusTimingTests; the drive letter moved between boxes, so try both.
	const TCHAR* InternalRoots[] =
	{
		TEXT("D:/NickWork/Mobius_InternalData"),
		TEXT("E:/00_Work/Mobius_InternalData"),
		TEXT("F:/Mobius_InternalData"),
	};
	FString RealSmv;
	for (const TCHAR* Root : InternalRoots)
	{
		const FString Candidate = FPaths::Combine(
			FString(Root), TEXT("12-room-test-vents"), TEXT("basemodel_default"), TEXT("basemodel_default.smv"));
		if (FPaths::FileExists(Candidate))
		{
			RealSmv = Candidate;
			break;
		}
	}

	if (RealSmv.IsEmpty())
	{
		AddInfo(TEXT("real-dataset check SKIPPED: 12-room-test-vents fixture not present on this machine"));
		return true;
	}

	FBRiskScenarioData RealData;
	FString RealError;
	if (!TestTrue(TEXT("The unsimulated 12-room export should import"),
		FBRiskDataImporter::ImportScenarioFromSmv(RealSmv, RealData, &RealError)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *RealError));
		return false;
	}
	TestFalse(TEXT("The unsimulated export should report no results"), RealData.bHasResultsData);
	TestEqual(TEXT("Both modelled spaces should survive"), RealData.Rooms.Num(), 2);
	TestEqual(TEXT("All 34 VENTGEOM records should survive"), RealData.Vents.Num(), 34);
	TestTrue(TEXT("Both footprints should still be applied"),
		RealData.Rooms[0].FootprintPolygon.Num() >= 3 && RealData.Rooms[1].FootprintPolygon.Num() >= 3);
	TestTrue(TEXT("The missing zone CSV should be named"),
		RealData.MissingResultFiles.ContainsByPredicate([](const FString& Path)
			{ return Path.EndsWith(TEXT("basemodel_default_zone.csv")); }));

	return true;
}

// --- Multi-room: per-room columns in a single zone table ----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskMultiRoomZoneImportTest,
	"ProjectMobius.BRisk.Importer.MultiRoomZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskMultiRoomZoneImportTest::RunTest(const FString& Parameters)
{
	// Three rooms laid out like b-risk-Testdata\3_RoomFire; fire only in room 1.
	const FString Smv =
		TEXT("ROOM   1\n 8.0 8.0 4.0\n 0.0 0.0 0.0\n")
		TEXT("ROOM   2\n 8.0 8.0 4.0\n 8.0 0.0 0.0\n")
		TEXT("ROOM   3\n 8.0 8.0 4.0\n 0.0 8.0 0.0\n")
		TEXT("FIRE\n 1 3.15 2.15 0.3\n")
		TEXT("ZONE\n3_RoomFire_zone.csv\n");

	// Distinct per-room ULOD so we can prove room 2/3 are not aliased to room 1.
	FString Csv =
		TEXT("s,C,C,m,1 / m,C,C,m,1 / m,C,C,m,1 / m,kW,\n")
		TEXT("Time,ULT_1,LLT_1,HGT_1,ULOD_1,ULT_2,LLT_2,HGT_2,ULOD_2,ULT_3,LLT_3,HGT_3,ULOD_3,HRR_1,\n")
		TEXT("0,24,24,4.0,0.5,24,24,4.0,1.5,24,24,4.0,2.5,0,\n")
		TEXT("600,200,150,1.2,0.5,180,140,1.6,1.5,160,130,2.0,2.5,1000,\n");

	const FString Dir = MakeBRiskTestDir();
	const FString SmvPath = FPaths::Combine(Dir, TEXT("3_RoomFire.smv"));
	TestTrue(TEXT("multi-room smv written"), WriteTextFile(SmvPath, Smv));
	TestTrue(TEXT("multi-room csv written"),
		WriteTextFile(FPaths::Combine(Dir, TEXT("3_RoomFire_zone.csv")), Csv));

	FBRiskScenarioData Data;
	FString Error;
	if (!TestTrue(TEXT("multi-room scenario imports"),
		FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *Error));
		return false;
	}

	TestEqual(TEXT("three rooms parsed"), Data.Rooms.Num(), 3);
	// B-Risk packs all rooms into ONE zone table with per-room suffixed columns.
	TestEqual(TEXT("single shared zone table"), Data.ZoneTables.Num(), 1);

	auto FindSeries = [&Data](const TCHAR* Name) -> const FBRiskSeries*
	{
		if (Data.ZoneTables.Num() == 0)
		{
			return nullptr;
		}
		return Data.ZoneTables[0].Series.FindByPredicate(
			[Name](const FBRiskSeries& S) { return S.Name.Equals(Name, ESearchCase::IgnoreCase); });
	};

	const FBRiskSeries* Ulod1 = FindSeries(TEXT("ULOD_1"));
	const FBRiskSeries* Ulod2 = FindSeries(TEXT("ULOD_2"));
	const FBRiskSeries* Ulod3 = FindSeries(TEXT("ULOD_3"));
	TestNotNull(TEXT("ULOD_1 present"), Ulod1);
	TestNotNull(TEXT("ULOD_2 present"), Ulod2);
	TestNotNull(TEXT("ULOD_3 present"), Ulod3);
	if (Ulod1 && Ulod2 && Ulod3)
	{
		// Each room must carry its own value, not room 1's, at every sample.
		TestEqual(TEXT("room 1 ULOD"), Ulod1->Values[0], 0.5);
		TestEqual(TEXT("room 2 ULOD distinct from room 1"), Ulod2->Values[0], 1.5);
		TestEqual(TEXT("room 3 ULOD distinct from room 1"), Ulod3->Values[0], 2.5);
	}

	// Fire channel uses the fire-object suffix (_1), present once.
	TestNotNull(TEXT("HRR_1 present"), FindSeries(TEXT("HRR_1")));
	return true;
}

// --- Vent slabs land on the wall shared with the connected room ---------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskVentWallPlacementTest,
	"ProjectMobius.BRisk.Hazard.VentWallPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskVentWallPlacementTest::RunTest(const FString& Parameters)
{
	// 3_RoomFire layout: room 1 at origin, room 2 east (+X), room 3 north (+Y).
	auto MakeRoom = [](int32 Id, const FVector& Origin)
	{
		FBRiskRoomGeometry R;
		R.RoomId = Id;
		R.Origin = Origin;
		R.Size = FVector(8.0, 8.0, 4.0);
		return R;
	};
	const FBRiskRoomGeometry Room1 = MakeRoom(1, FVector(0, 0, 0));
	const FBRiskRoomGeometry Room2 = MakeRoom(2, FVector(8, 0, 0));
	const FBRiskRoomGeometry Room3 = MakeRoom(3, FVector(0, 8, 0));

	auto MakeVent = [](int32 From, int32 To, int32 Face, double Offset)
	{
		FBRiskVentGeometry V;
		V.FromRoomId = From;
		V.ToRoomId = To;
		V.Face = Face;
		V.Width = 2.4;
		V.Offset = Offset;
		V.SillHeight = 0.0;
		V.Height = 2.0;
		return V;
	};

	const float Scale = 100.0f;
	const float Thickness = 8.0f;
	FVector Center, Size;

	// After the B-Risk -> Unreal X<->Y swap (BRiskCoord): room 2 (B-Risk east/+X) is at
	// Unreal +Y, and room 3 (B-Risk north/+Y) is at Unreal +X.

	// Vent 1->2: room 2 is adjacent on Unreal +Y, so the slab is on room 1's +Y wall
	// (y = 800 cm), thin in Y, spanning X.
	TestTrue(TEXT("vent 1->2 resolves"),
		ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 2, 2, 0.0), &Room1, &Room2, Scale, BRiskCoord::ERoomFrame::SmokeviewSwap, Thickness, Center, Size));
	// FVector components are double in UE5.5; compare against double literals so
	// TestEqual binds to the (double,double,double) overload unambiguously.
	const double ThicknessCm = static_cast<double>(Thickness);
	TestEqual(TEXT("vent 1->2 on +Y wall"), Center.Y, 800.0, 0.01);
	TestEqual(TEXT("vent 1->2 thin in Y"), Size.Y, ThicknessCm, 0.01);
	TestEqual(TEXT("vent 1->2 spans X by width"), Size.X, 240.0, 0.01);
	TestEqual(TEXT("vent 1->2 floor-to-2m centre"), Center.Z, 100.0, 0.01);

	// Vent 1->3: room 3 is adjacent on Unreal +X, so the slab is on room 1's +X wall
	// (x = 800 cm), thin in X, spanning Y.
	TestTrue(TEXT("vent 1->3 resolves"),
		ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 3, 3, 0.0), &Room1, &Room3, Scale, BRiskCoord::ERoomFrame::SmokeviewSwap, Thickness, Center, Size));
	TestEqual(TEXT("vent 1->3 on +X wall"), Center.X, 800.0, 0.01);
	TestEqual(TEXT("vent 1->3 thin in X"), Size.X, ThicknessCm, 0.01);
	TestEqual(TEXT("vent 1->3 spans Y by width"), Size.Y, 240.0, 0.01);

	// Vent 1->4: exterior (no such room) -> CFAST face id 4 = B-Risk -X, which maps to the
	// Unreal -Y wall under the swap. Offset (0.8 m) runs along Unreal X.
	TestTrue(TEXT("vent 1->exterior resolves"),
		ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 4, 4, 0.8), &Room1, nullptr, Scale, BRiskCoord::ERoomFrame::SmokeviewSwap, Thickness, Center, Size));
	TestEqual(TEXT("exterior vent on -Y wall"), Center.Y, 0.0, 0.01);
	TestEqual(TEXT("exterior vent offset applied on X"), Center.X, 200.0, 0.01); // (80 + 320)/2

	// --- Same three vents under the Revit frame -------------------------------------------------
	//
	// Everything above pins the LEGACY swap and must never move; this block covers the frame a
	// Zones-data.json scenario actually uses. Under FootprintToUnreal (UE X = B-Risk X,
	// UE Y = -B-Risk Y) the same rooms land at:
	//   room 1  X [0, 800]    Y [-800, 0]
	//   room 2  X [800, 1600] Y [-800, 0]      (B-Risk +X -> UE +X, unchanged)
	//   room 3  X [0, 800]    Y [-1600, -800]  (B-Risk +Y -> UE -Y, NEGATED)
	// so the two interior vents swap which axis they sit on relative to the legacy case.
	{
		const BRiskCoord::ERoomFrame Revit = BRiskCoord::ERoomFrame::Revit;

		// Room 2 is adjacent on UE +X now, not +Y.
		TestTrue(TEXT("revit vent 1->2 resolves"),
			ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 2, 2, 0.0), &Room1, &Room2, Scale, Revit, Thickness, Center, Size));
		TestEqual(TEXT("revit vent 1->2 on +X wall"), Center.X, 800.0, 0.01);
		TestEqual(TEXT("revit vent 1->2 thin in X"), Size.X, ThicknessCm, 0.01);
		TestEqual(TEXT("revit vent 1->2 spans Y by width"), Size.Y, 240.0, 0.01);

		// Room 3 is adjacent on UE -Y, the direction the negation reverses.
		TestTrue(TEXT("revit vent 1->3 resolves"),
			ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 3, 3, 0.0), &Room1, &Room3, Scale, Revit, Thickness, Center, Size));
		TestEqual(TEXT("revit vent 1->3 on -Y wall"), Center.Y, -800.0, 0.01);
		TestEqual(TEXT("revit vent 1->3 thin in Y"), Size.Y, ThicknessCm, 0.01);
		TestEqual(TEXT("revit vent 1->3 spans X by width"), Size.X, 240.0, 0.01);

		// The one that catches a wrong offset DIRECTION rather than a wrong wall. Face 4 = B-Risk
		// -X, which is still UE -X here (not -Y as under the swap). The opening runs along Y, the
		// negated axis, so the 0.8 m offset is measured DOWN from the box maximum:
		//   OpenEnd = 0 - 80 = -80, OpenStart = -80 - 240 = -320, centre = -200.
		// Measuring from the minimum instead would give +200 - a mirrored vent that looks
		// perfectly plausible until the offset is non-zero, which is exactly why this asserts it.
		TestTrue(TEXT("revit vent 1->exterior resolves"),
			ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 4, 4, 0.8), &Room1, nullptr, Scale, Revit, Thickness, Center, Size));
		TestEqual(TEXT("revit exterior vent on -X wall"), Center.X, 0.0, 0.01);
		TestEqual(TEXT("revit exterior vent offset measured from max Y"), Center.Y, -200.0, 0.01);
		TestEqual(TEXT("revit exterior vent spans Y by width"), Size.Y, 240.0, 0.01);
	}

	return true;
}

// --- Derived Smokeview-style wall-vent flow (no data oracle; verify qualitative physics) ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskVentFlowTest,
	"ProjectMobius.BRisk.Hazard.VentFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskVentFlowTest::RunTest(const FString& Parameters)
{
	FBRiskVentGeometry Vent;
	Vent.FromRoomId = 1;
	Vent.ToRoomId = 2;
	Vent.Width = 0.8;
	Vent.SillHeight = 0.0;
	Vent.Height = 2.0;

	// Fire room (hot buoyant upper layer, slightly low floor pressure) -> cool room. Expect the
	// classic doorway pattern: hot gas OUT the top, cool air IN the bottom, neutral plane between.
	FBRiskVentSideState Hot;
	Hot.FloorZM = 0.0;
	Hot.UpperTempC = 400.0;
	Hot.LowerTempC = 30.0;
	Hot.LayerHeightM = 1.0;
	Hot.PressurePa = -2.0;

	FBRiskVentSideState Cool;
	Cool.FloorZM = 0.0;
	Cool.UpperTempC = 20.0;
	Cool.LowerTempC = 20.0;
	Cool.LayerHeightM = 2.0;
	Cool.PressurePa = 0.0;

	const FBRiskVentFlow Flow = UBRiskDataSubsystem::ComputeWallVentFlow(Hot, Cool, Vent);
	TestTrue(TEXT("flow present"), Flow.bHasFlow);
	TestTrue(TEXT("hot gas flows out the top"), Flow.MassFlowOutKgs > 0.0);
	TestTrue(TEXT("cool air flows in the bottom"), Flow.MassFlowInKgs > 0.0);
	TestTrue(TEXT("out stream hotter than in stream"), Flow.OutTemperatureC > Flow.InTemperatureC);
	TestTrue(TEXT("neutral plane lies within the opening"),
		Flow.NeutralPlaneHeightM > 0.0 && Flow.NeutralPlaneHeightM < 2.0);

	// Closed opening (zero width) produces no flow.
	FBRiskVentGeometry Closed = Vent;
	Closed.Width = 0.0;
	const FBRiskVentFlow NoFlow = UBRiskDataSubsystem::ComputeWallVentFlow(Hot, Cool, Closed);
	TestFalse(TEXT("closed vent has no flow"), NoFlow.bHasFlow);

	// Identical rooms at equal pressure: no pressure difference, so no flow either way.
	const FBRiskVentFlow Still = UBRiskDataSubsystem::ComputeWallVentFlow(Cool, Cool, Vent);
	TestTrue(TEXT("identical rooms produce negligible flow"),
		(Still.MassFlowOutKgs + Still.MassFlowInKgs) < 1.0e-3);

	return true;
}

// --- Suffix mapping the renderer + egress cache rely on -----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskRoomIdSuffixTest,
	"ProjectMobius.BRisk.Egress.RoomIdSuffix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskRoomIdSuffixTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("ULOD_1 -> 1"), UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT("ULOD_1")), 1);
	TestEqual(TEXT("HGT_2 -> 2"), UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT("HGT_2")), 2);
	TestEqual(TEXT("HRR_10 -> 10"), UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT("HRR_10")), 10);
	TestEqual(TEXT("leading/trailing space tolerated"),
		UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT(" ULOD_3 ")), 3);
	TestEqual(TEXT("no suffix -> INDEX_NONE"),
		UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT("Time")), INDEX_NONE);
	TestEqual(TEXT("bare channel -> INDEX_NONE"),
		UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT("ULOD")), INDEX_NONE);
	TestEqual(TEXT("non-numeric suffix -> INDEX_NONE"),
		UBRiskEgressSubsystem::ExtractRoomIdSuffix(TEXT("FED_GAS")), INDEX_NONE);
	return true;
}

namespace
{
	/**
	 * The pre-footprint resolution rule, kept verbatim as the measurement baseline: smallest
	 * bounding-box volume among the boxes containing the point, no polygon test. This is what
	 * ResolveRoomIndexAtLocation did before footprint containment, and comparing against it is what
	 * turns "the fix works" into a number.
	 */
	int32 ResolveByBoundingBoxOnly(
		const TArray<UE::Mobius::Tenability::FRoomVolume>& Volumes,
		const FVector& Location)
	{
		int32 BestIndex = INDEX_NONE;
		double BestVolume = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			const FBox& Bounds = Volumes[Index].Bounds;
			if (!Bounds.IsInsideOrOn(Location) || Bounds.GetVolume() >= BestVolume)
			{
				continue;
			}
			BestVolume = Bounds.GetVolume();
			BestIndex = Index;
		}
		return BestIndex;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskFootprintAttributionTest,
	"ProjectMobius.BRisk.Geometry.FootprintAttribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * How much agent -> room attribution actually changes when containment honours the footprint,
 * measured on the REAL 12 RoomTest geometry rather than a synthetic shape.
 *
 * Lobby 14 is a true rectangle, so its attribution must not move at all - that is the regression
 * gate for every scenario without a Zones-data.json, which is all of them until now. Corridor 15 is
 * the L-shaped corridor whose bounding box is 17.8 x 6.186 m (110.11 m2) around a room of just
 * 22.986 m2, so a bbox-only rule claimed roughly five times the floor it should. This test pins
 * that ratio, and pins the failure mode that would matter most if the two frames ever disagreed:
 * a polygon in the wrong frame would reject every point inside its own bounding box and silently
 * strip every agent of its room.
 */
bool FBRiskFootprintAttributionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Mobius::Tenability;

	constexpr float Scale = 100.0f; // cm per metre
	// Room 1 (rectangle) and room 2 (L-shaped corridor) exactly as Zones-data.json declares them.
	const TArray<FBRiskRoomGeometry> Rooms = { MakeLobby14Room(), MakeCorridor15Room() };
	constexpr int32 LobbyIndex = 0;
	constexpr int32 CorridorIndex = 1;

	TArray<FRoomVolume> Volumes;
	FBox SweepBounds(ForceInit);
	for (const FBRiskRoomGeometry& Room : Rooms)
	{
		const BRiskCoord::FRoomFootprintCm Footprint =
			BRiskCoord::MakeRoomFootprint(Room, Scale, BRiskCoord::ERoomFrame::Revit);
		Volumes.Add(MakeRoomVolume(Footprint.Bounds, Footprint.Polygon));
		SweepBounds += Footprint.Bounds;
	}

	TestEqual(TEXT("both rooms produced a footprint polygon"), Volumes.Num(), 2);

	// --- Frame agreement: the guard against the catastrophic failure mode ------------------
	// Bounds and polygon come out of one MakeRoomFootprint call, so the ring must span exactly its
	// own bounding box in XY. If a future change converted one through ToUnreal and the other
	// through FootprintToUnreal (they differ by 90 degrees), containment would reject every point
	// and every agent would resolve to INDEX_NONE with no other symptom.
	for (int32 Index = 0; Index < Volumes.Num(); ++Index)
	{
		const FRoomVolume& Volume = Volumes[Index];
		double RingMinX = TNumericLimits<double>::Max(), RingMaxX = -TNumericLimits<double>::Max();
		double RingMinY = TNumericLimits<double>::Max(), RingMaxY = -TNumericLimits<double>::Max();
		for (const FVector2D& Vertex : Volume.FootprintPolygonCm)
		{
			RingMinX = FMath::Min(RingMinX, Vertex.X);
			RingMaxX = FMath::Max(RingMaxX, Vertex.X);
			RingMinY = FMath::Min(RingMinY, Vertex.Y);
			RingMaxY = FMath::Max(RingMaxY, Vertex.Y);
		}
		TestTrue(*FString::Printf(TEXT("room %d: ring spans its own bounding box in XY"), Index),
			FMath::IsNearlyEqual(RingMinX, Volume.Bounds.Min.X, 0.01)
			&& FMath::IsNearlyEqual(RingMinY, Volume.Bounds.Min.Y, 0.01)
			&& FMath::IsNearlyEqual(RingMaxX, Volume.Bounds.Max.X, 0.01)
			&& FMath::IsNearlyEqual(RingMaxY, Volume.Bounds.Max.Y, 0.01));
	}

	// --- Sweep both rules over the whole scenario at breathing height ---------------------
	constexpr int32 SweepResolution = 400;
	const double BreathingZ = SweepBounds.Min.Z + 160.0; // ~1.6 m, inside the 4 m rooms
	const FVector2D SweepSize(
		SweepBounds.Max.X - SweepBounds.Min.X, SweepBounds.Max.Y - SweepBounds.Min.Y);

	TArray<int32> BoxRuleHits, FootprintRuleHits;
	BoxRuleHits.SetNumZeroed(Volumes.Num());
	FootprintRuleHits.SetNumZeroed(Volumes.Num());
	int32 MovedToAnotherRoom = 0;
	int32 LostToUnmodelledSpace = 0;
	int32 InsideARingButUnattributed = 0;

	for (int32 Row = 0; Row < SweepResolution; ++Row)
	{
		const double Y = SweepBounds.Min.Y + ((Row + 0.5) / SweepResolution) * SweepSize.Y;
		for (int32 Column = 0; Column < SweepResolution; ++Column)
		{
			const double X = SweepBounds.Min.X + ((Column + 0.5) / SweepResolution) * SweepSize.X;
			const FVector Point(X, Y, BreathingZ);

			const int32 BoxRoom = ResolveByBoundingBoxOnly(Volumes, Point);
			const int32 FootprintRoom = ResolveRoomIndexAtLocation(Volumes, Point, INDEX_NONE);

			if (Volumes.IsValidIndex(BoxRoom))
			{
				++BoxRuleHits[BoxRoom];
			}
			if (Volumes.IsValidIndex(FootprintRoom))
			{
				++FootprintRuleHits[FootprintRoom];
			}
			else if (Volumes.IsValidIndex(BoxRoom))
			{
				++LostToUnmodelledSpace;
			}
			if (Volumes.IsValidIndex(BoxRoom) && Volumes.IsValidIndex(FootprintRoom)
				&& BoxRoom != FootprintRoom)
			{
				++MovedToAnotherRoom;
			}

			// Self-consistency: a point inside a room's own ring must resolve to SOME room. If this
			// ever fires, containment is rejecting space the footprint says is interior.
			for (const FRoomVolume& Volume : Volumes)
			{
				if (Volume.Bounds.IsInsideOrOn(Point)
					&& BRiskCoord::IsPointInRing(Volume.FootprintPolygonCm, FVector2D(X, Y))
					&& !Volumes.IsValidIndex(FootprintRoom))
				{
					++InsideARingButUnattributed;
				}
			}
		}
	}

	TestEqual(TEXT("no point inside a footprint ring is left unattributed"),
		InsideARingButUnattributed, 0);

	// Lobby 14 IS its bounding box, so honouring the footprint changes nothing for it. This is the
	// number that says rectangle-only scenarios are untouched.
	TestEqual(TEXT("Lobby 14 (a true rectangle) keeps every point it had"),
		FootprintRuleHits[LobbyIndex], BoxRuleHits[LobbyIndex]);

	// Corridor 15's real floor is 22.986 m2 inside a 110.11 m2 bounding box. The sweep should
	// recover that ratio to within grid discretisation.
	const double ExpectedCorridorShare = 22.986 / (17.8 * 6.186);
	const double MeasuredCorridorShare = BoxRuleHits[CorridorIndex] > 0
		? static_cast<double>(FootprintRuleHits[CorridorIndex]) / BoxRuleHits[CorridorIndex]
		: 0.0;
	AddInfo(FString::Printf(
		TEXT("Corridor 15: bbox rule claimed %d sweep points, footprint rule claims %d ")
		TEXT("(%.4f of them; geometry predicts %.4f). Scenario-wide: %d points moved to a ")
		TEXT("different room, %d fell into space B-Risk modelled no zone for."),
		BoxRuleHits[CorridorIndex], FootprintRuleHits[CorridorIndex],
		MeasuredCorridorShare, ExpectedCorridorShare,
		MovedToAnotherRoom, LostToUnmodelledSpace));
	TestTrue(*FString::Printf(
			TEXT("Corridor 15 footprint share %.4f matches its area ratio %.4f"),
			MeasuredCorridorShare, ExpectedCorridorShare),
		FMath::IsNearlyEqual(MeasuredCorridorShare, ExpectedCorridorShare, 0.01));

	// The corridor's bounding box over-claims by ~5x, which is the whole reason for this change.
	TestTrue(TEXT("the bbox rule over-claimed Corridor 15 by more than 4x"),
		FootprintRuleHits[CorridorIndex] * 4 < BoxRuleHits[CorridorIndex]);

	// In THIS two-room fixture nothing is modelled in the corridor's notch, so every over-claimed
	// point becomes INDEX_NONE rather than moving to another room. In the full 12-room scenario most
	// of that space belongs to rooms 3-12 and those points move instead; the semantics are the same
	// either way - a point is attributed to the room whose footprint contains it, or to none.
	TestEqual(TEXT("no point silently moves between these two non-overlapping rooms"),
		MovedToAnotherRoom, 0);
	TestEqual(TEXT("every over-claimed corridor point becomes unattributed, not reassigned"),
		LostToUnmodelledSpace, BoxRuleHits[CorridorIndex] - FootprintRuleHits[CorridorIndex]);

	return true;
}

#endif
