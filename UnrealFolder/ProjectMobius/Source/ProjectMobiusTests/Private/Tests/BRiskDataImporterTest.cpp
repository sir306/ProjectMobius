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
#include "MobiusTestDataRoots.h"

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

	/**
	 * Is Point covered by any mesh triangle lying in the same wall plane and facing the same way?
	 *
	 * This is what "there is a hole here" actually means, and it cannot be answered by counting
	 * vertices: cutting an opening changes the triangle count for lots of reasons, and a count that
	 * moved tells you nothing about WHERE the geometry went. Restricted to co-planar, co-facing
	 * triangles so the opposite wall of the same room, and the reveal quads running through the
	 * wall, cannot answer for a wall they are not part of.
	 */
	bool IsWallPointCovered(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const FVector& Point,
		const FVector& Outward,
		double PlaneToleranceCm = 0.1)
	{
		// The wall's own 2D frame: drop the axis its normal runs along.
		int32 NormalAxis = 0;
		for (int32 Axis = 1; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Outward[Axis]) > FMath::Abs(Outward[NormalAxis]))
			{
				NormalAxis = Axis;
			}
		}
		const int32 AxisU = (NormalAxis + 1) % 3;
		const int32 AxisV = (NormalAxis + 2) % 3;
		const FVector2D Query(Point[AxisU], Point[AxisV]);

		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			if (FVector::DotProduct(Normals[Triangles[Index]], Outward) < 0.999)
			{
				continue;
			}

			const FVector& A = Vertices[Triangles[Index]];
			const FVector& B = Vertices[Triangles[Index + 1]];
			const FVector& C = Vertices[Triangles[Index + 2]];
			if (FMath::Abs(A[NormalAxis] - Point[NormalAxis]) > PlaneToleranceCm
				|| FMath::Abs(B[NormalAxis] - Point[NormalAxis]) > PlaneToleranceCm
				|| FMath::Abs(C[NormalAxis] - Point[NormalAxis]) > PlaneToleranceCm)
			{
				continue;
			}

			const FVector2D P0(A[AxisU], A[AxisV]);
			const FVector2D P1(B[AxisU], B[AxisV]);
			const FVector2D P2(C[AxisU], C[AxisV]);
			const auto Side = [](const FVector2D& From, const FVector2D& To, const FVector2D& Test)
			{
				return (To.X - From.X) * (Test.Y - From.Y) - (To.Y - From.Y) * (Test.X - From.X);
			};
			const double S0 = Side(P0, P1, Query);
			const double S1 = Side(P1, P2, Query);
			const double S2 = Side(P2, P0, Query);
			if ((S0 >= 0.0 && S1 >= 0.0 && S2 >= 0.0) || (S0 <= 0.0 && S1 <= 0.0 && S2 <= 0.0))
			{
				return true;
			}
		}
		return false;
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

	// --- A room WITHOUT a footprint must honour the scenario frame ---------------------------
	//
	// The rectangle path used to convert with ToUnrealBox regardless of the Frame it was passed,
	// so in a Revit-frame scenario a polygon-less room came out rotated 90 degrees about the world
	// origin from every room that did have a footprint. Every other test here happens to feed rooms
	// that have polygons, or passes SmokeviewSwap, so none of them could see it.
	//
	// Same room, same numbers, both frames - only the frame differs, so a single expectation cannot
	// be satisfied by both and the assertion cannot be quietly weakened into agreement.
	{
		FBRiskRoomGeometry RectangleOnly = MakeLobby14Room();
		RectangleOnly.FootprintPolygon.Reset();
		RectangleOnly.bHasFootprintExtents = false;
		const TArray<FBRiskRoomGeometry> Rooms = { RectangleOnly };

		// Revit: (x, -y, z). Origin x 3.2505 + size x 5.0 -> X [325.05, 825.05];
		// origin y -19.0189 + size y 2.786 -> negated and ordered -> Y [1623.29, 1901.89].
		if (TestTrue(TEXT("Rectangle-only room should build in the Revit frame"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, NoVents, 100.0f, BRiskCoord::ERoomFrame::Revit, Vertices, Triangles, Normals, &Error)))
		{
			const FBox Bounds(Vertices);
			TestEqual(TEXT("Revit frame min X"), Bounds.Min.X, 325.05, 0.01);
			TestEqual(TEXT("Revit frame max X"), Bounds.Max.X, 825.05, 0.01);
			TestEqual(TEXT("Revit frame min Y"), Bounds.Min.Y, 1623.29, 0.01);
			TestEqual(TEXT("Revit frame max Y"), Bounds.Max.Y, 1901.89, 0.01);
		}

		// Legacy X<->Y swap, byte-identical to the old ToUnrealBox call. Pinning this is the point:
		// the fix must not disturb a scenario that has no Zones-data.json at all.
		if (TestTrue(TEXT("Rectangle-only room should build in the legacy frame"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, NoVents, 100.0f, BRiskCoord::ERoomFrame::SmokeviewSwap, Vertices, Triangles, Normals, &Error)))
		{
			const FBox Bounds(Vertices);
			TestEqual(TEXT("Legacy frame min X"), Bounds.Min.X, -1901.89, 0.01);
			TestEqual(TEXT("Legacy frame max X"), Bounds.Max.X, -1623.29, 0.01);
			TestEqual(TEXT("Legacy frame min Y"), Bounds.Min.Y, 325.05, 0.01);
			TestEqual(TEXT("Legacy frame max Y"), Bounds.Max.Y, 825.05, 0.01);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskRoomMeshOpeningsTest,
	"ProjectMobius.BRisk.Geometry.RoomMeshOpenings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * Cutting the real door/window openings out of the polygon room shell.
 *
 * What makes this possible is the Zones-data.json opening CENTRE, not hostThickness: the centre is
 * a real position in the room's own frame, where (face, offset) are coordinates in B-Risk's
 * area/perimeter-equivalent rectangle and cannot be mapped onto a polygon at all. hostThickness
 * only supplies the DEPTH, which is what lines the hole - so the two are tested separately, and an
 * export without a thickness must still get its hole.
 *
 * The shapes asserted here are the ones the 12-room model actually produces, and they are the ones
 * a triangulator-with-holes handles worst: an opening spanning its wall corner to corner (a notch,
 * whose "hole" vertices lie on the outer boundary), and openings that overlap each other.
 */
bool FBRiskRoomMeshOpeningsTest::RunTest(const FString& Parameters)
{
	constexpr float Scale = 100.0f;
	constexpr BRiskCoord::ERoomFrame Frame = BRiskCoord::ERoomFrame::Revit;

	// A 5 x 5 m room at the origin, 3 m tall. Under FootprintToUnreal (Y negated) the ring comes
	// out counter-clockwise spanning UE X 0..500, Y 0..500.
	const auto MakeSquareRoom = [](int32 RoomId, double OriginX) -> FBRiskRoomGeometry
	{
		FBRiskRoomGeometry Room;
		Room.RoomId = RoomId;
		Room.Label = FString::Printf(TEXT("square %d"), RoomId);
		Room.Origin = FVector(OriginX, -5.0, 0.0);
		Room.Size = FVector(5.0, 5.0, 3.0);
		Room.FootprintPolygon = {
			FVector2D(OriginX,       -5.0),
			FVector2D(OriginX + 5.0, -5.0),
			FVector2D(OriginX + 5.0,  0.0),
			FVector2D(OriginX,        0.0),
		};
		return Room;
	};

	// An opening on the room's UE -Y wall (the y = 0 edge), centred at X = CentreX metres and
	// standing off half a wall thickness OUTSIDE the room, exactly as the add-in writes them.
	const auto MakeOpening = [](
		int32 VentId, EBRiskVentKind Kind, int32 FromRoom, int32 ToRoom,
		double CentreX, double WidthM, double ThicknessM) -> FBRiskVentGeometry
	{
		FBRiskVentGeometry Vent;
		Vent.VentId = VentId;
		Vent.Kind = Kind;
		Vent.FromRoomId = FromRoom;
		Vent.ToRoomId = ToRoom;
		Vent.bHasPlacement = true;
		Vent.CentreMetres = FVector(CentreX, ThicknessM * 0.5, 1.0);
		Vent.PhysicalWidth = WidthM;
		Vent.PhysicalHeight = 2.0;
		Vent.Width = WidthM * 0.5;   // modelled, as B-Risk halves a door leaf
		Vent.Height = 2.0;
		Vent.SillHeight = 0.0;
		Vent.HostThicknessMetres = ThicknessM;
		return Vent;
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	FString Error;

	// The wall the openings are cut into: UE y = 0, facing -Y.
	const FVector WallOutward = -FVector::YAxisVector;

	// --- No openings: the shell must be exactly what it was before holes existed --------------
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		const TArray<FBRiskVentGeometry> NoVents;
		if (TestTrue(TEXT("A room with no openings should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, NoVents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			// Two caps of two triangles each, plus one two-triangle panel per wall. Pinned exactly:
			// the decomposition must collapse back to a single panel when there is nothing to cut,
			// or every scenario without a Zones-data.json silently gains geometry.
			TestEqual(TEXT("Uncut square room is 12 triangles"), Triangles.Num(), 36);
			TestEqual(TEXT("Uncut square room is 36 vertices"), Vertices.Num(), 36);
			TestTrue(TEXT("The wall is solid where a door would go"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 100.0), WallOutward));
		}
	}

	// --- One door mid-wall --------------------------------------------------------------------
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		const TArray<FBRiskVentGeometry> Vents = {
			MakeOpening(1, EBRiskVentKind::Door, 1, 99, 2.5, 1.0, 0.2) };

		if (TestTrue(TEXT("A room with a door should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			// The PHYSICAL width, not the modelled one. Cutting Vent.Width would leave a 50 cm hole
			// under a 100 cm marker - the same half-a-door-leaf error the marker itself used to make.
			TestFalse(TEXT("The door centre is open"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 100.0), WallOutward));
			TestFalse(TEXT("The door is a full 100 cm wide, not the modelled 50"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(203.0, 0.0, 100.0), WallOutward));
			TestTrue(TEXT("The wall beside the door survives"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(195.0, 0.0, 100.0), WallOutward));
			TestTrue(TEXT("The wall above the door survives"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 250.0), WallOutward));
			TestTrue(TEXT("The other three walls are untouched"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 500.0, 100.0), FVector::YAxisVector));

			// The lining, which is the only part hostThickness pays for. It runs from the polygon -
			// the room's INNER face - outward through the wall body, so the jamb is a surface at
			// y = 0..-20 that faces along the opening.
			TestTrue(TEXT("The door is lined to the host wall's depth"),
				IsWallPointCovered(Vertices, Triangles, Normals,
					FVector(200.0, -5.0, 100.0), FVector::XAxisVector));
			TestTrue(TEXT("The lining has a head"),
				IsWallPointCovered(Vertices, Triangles, Normals,
					FVector(250.0, -5.0, 200.0), -FVector::ZAxisVector));

			FBox Bounds(Vertices);
			TestEqual(TEXT("The lining reaches the outer face and no further"), Bounds.Min.Y, -20.0, 1.0e-6);
		}
	}

	// --- No hostThickness: the hole is still cut, just unlined ---------------------------------
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		TArray<FBRiskVentGeometry> Vents = { MakeOpening(1, EBRiskVentKind::Door, 1, 99, 2.5, 1.0, 0.2) };
		// A pre-v2 export: a real centre, no wall thickness anywhere in the data.
		Vents[0].HostThicknessMetres = 0.0;

		if (TestTrue(TEXT("A pre-v2 opening should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			TestFalse(TEXT("The door is still cut without a declared thickness"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 100.0), WallOutward));
			const FBox Bounds(Vertices);
			TestEqual(TEXT("Nothing is lined, so no geometry leaves the footprint"), Bounds.Min.Y, 0.0, 1.0e-6);
		}
	}

	// --- A door spanning its wall corner to corner ---------------------------------------------
	//
	// The corridor's 100 cm doorway stubs do exactly this: the opening runs 0.00..100.00 of a 100 cm
	// edge. There is no wall left either side, so the "hole" touches the outer boundary and is a
	// notch, not a hole - the case an ear-clipper produces self-intersecting output for.
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		const TArray<FBRiskVentGeometry> Vents = {
			MakeOpening(1, EBRiskVentKind::Door, 1, 99, 2.5, 5.0, 0.2) };

		if (TestTrue(TEXT("A wall-wide door should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			TestFalse(TEXT("The full-width door is open at one end"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(5.0, 0.0, 100.0), WallOutward));
			TestFalse(TEXT("The full-width door is open at the other"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(495.0, 0.0, 100.0), WallOutward));
			TestTrue(TEXT("Only the band above it is left"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 250.0), WallOutward));

			for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
			{
				const FVector& A = Vertices[Triangles[Index]];
				const FVector& B = Vertices[Triangles[Index + 1]];
				const FVector& C = Vertices[Triangles[Index + 2]];
				TestTrue(TEXT("No degenerate triangle is emitted"),
					FVector::CrossProduct(B - A, C - A).Size() > 1.0e-3);
			}
		}
	}

	// --- Overlapping openings union ------------------------------------------------------------
	//
	// 15 of the 18 leakage vents in the 12-room model sit wholly inside a door's span. Turning them
	// on must not produce a hole inside a hole, or geometry stacked on the boundary between them.
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		const TArray<FBRiskVentGeometry> Vents = {
			MakeOpening(1, EBRiskVentKind::Door, 1, 99, 2.5, 1.0, 0.2),
			MakeOpening(2, EBRiskVentKind::Door, 1, 99, 2.8, 1.0, 0.2) };

		if (TestTrue(TEXT("Overlapping openings should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			TestFalse(TEXT("The union is open across the overlap"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(275.0, 0.0, 100.0), WallOutward));
			TestFalse(TEXT("The union is open at the far end"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(325.0, 0.0, 100.0), WallOutward));
			TestTrue(TEXT("The wall outside the union survives"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(340.0, 0.0, 100.0), WallOutward));
		}
	}

	// --- Leakage is not an aperture -------------------------------------------------------------
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		const TArray<FBRiskVentGeometry> Vents = {
			MakeOpening(1, EBRiskVentKind::Leakage, 1, 99, 2.5, 0.01, 0.2) };

		if (TestTrue(TEXT("A leakage-only room should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			TestEqual(TEXT("A leakage vent leaves the wall exactly as it found it"), Triangles.Num(), 36);
			TestTrue(TEXT("The wall is solid where the leakage path is"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 100.0), WallOutward));
		}

		if (TestTrue(TEXT("A leakage-only room should build with cutting on"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error, /*bCutLeakageOpenings*/ true)))
		{
			TestFalse(TEXT("Turning leakage on does cut the slit"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 100.0), WallOutward));
		}
	}

	// --- An opening nowhere near a wall of this room --------------------------------------------
	//
	// Nearest-edge always returns SOME edge. Vent 32 in the 12-room model is the live case: its
	// centre is 10 cm from room 1's wall as declared and 20 cm from room 2's, because the add-in
	// derives wall-leakage from the room boundary rather than the wall centreline.
	{
		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0) };
		TArray<FBRiskVentGeometry> Vents = { MakeOpening(1, EBRiskVentKind::Door, 1, 99, 2.5, 1.0, 0.2) };
		Vents[0].CentreMetres.Y = 1.0; // a metre outside a room whose walls are 200 mm

		if (TestTrue(TEXT("A stray opening should still build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			TestEqual(TEXT("An opening that far off any wall is not cut"), Triangles.Num(), 36);
		}
	}

	// --- A shared wall is cut from both sides, and lined once -----------------------------------
	//
	// One opening record describes both rooms. Cut it into only its roomA and a door reads as an
	// opening from one side and a solid wall from the other; line it from both and the two tunnels
	// occupy the same wall body as coincident, co-facing quads.
	{
		// Two rooms 20 cm apart across the y = 0 / y = -20 gap: room 1 spans UE Y 0..500 and room 2
		// UE Y -520..-20, so the shared wall body is exactly the declared 200 mm.
		FBRiskRoomGeometry Far = MakeSquareRoom(2, 0.0);
		Far.Origin = FVector(0.0, -0.2, 0.0);
		Far.Size = FVector(5.0, 5.0, 3.0);
		Far.FootprintPolygon = {
			FVector2D(0.0, 0.2),
			FVector2D(5.0, 0.2),
			FVector2D(5.0, 5.2),
			FVector2D(0.0, 5.2),
		};

		const TArray<FBRiskRoomGeometry> Rooms = { MakeSquareRoom(1, 0.0), Far };
		const TArray<FBRiskVentGeometry> Vents = {
			MakeOpening(1, EBRiskVentKind::Door, 1, 2, 2.5, 1.0, 0.2) };

		if (TestTrue(TEXT("A shared-wall opening should build"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				Rooms, Vents, Scale, Frame, Vertices, Triangles, Normals, &Error)))
		{
			TestFalse(TEXT("The near room's wall is open"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, 0.0, 100.0), WallOutward));
			TestFalse(TEXT("The far room's wall is open at the same place"),
				IsWallPointCovered(Vertices, Triangles, Normals, FVector(250.0, -20.0, 100.0), FVector::YAxisVector));

			// One lining, not two. Both rooms' tunnels would occupy the same 20 cm of wall.
			int32 JambTriangles = 0;
			for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
			{
				const FVector& A = Vertices[Triangles[Index]];
				if (Normals[Triangles[Index]].Equals(FVector::XAxisVector, 1.0e-3)
					&& FMath::IsNearlyEqual(A.X, 200.0, 0.1))
				{
					++JambTriangles;
				}
			}
			TestEqual(TEXT("The shared opening is lined exactly once"), JambTriangles, 2);
		}
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

	// Height must split the same way as width. This fixture omits modelledHeight, so Height falls
	// back to the real one - the case that must NOT silently leave Height meaning something
	// different from Width, since their product is the zone CSV's HVENT.
	TestEqual(TEXT("Door PhysicalHeight should be the real height"), Door.PhysicalHeight, 2.134, 1.0e-9);
	TestEqual(TEXT("Height falls back to the real height when modelledHeight is absent"),
		Door.Height, 2.134, 1.0e-9);

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
	// No hostThickness in this fixture, so the caller's fallback depth is used.
	TestEqual(TEXT("Door depth falls back when hostThickness is absent"),
		SizeCm.Y, static_cast<double>(ThicknessCm), 1.0e-3);
	TestEqual(TEXT("Door height"), SizeCm.Z, 213.4, 1.0e-3);

	// The window sits on "spur east", which runs along Y - the other axis. If the axis were
	// hard-coded or taken from the bounding box this would come out identical to the door.
	if (TestTrue(TEXT("Window should place from its centre"),
		ABRiskHazardVisualizer::ComputeVentSlab(
			Window, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm, CentreCm, SizeCm)))
	{
		TestEqual(TEXT("Window X should be the centre, untouched"), CentreCm.X, -766.35, 1.0e-3);
		TestEqual(TEXT("Window should run along Y"), SizeCm.Y, 105.0, 1.0e-3);
		TestEqual(TEXT("Window depth falls back when hostThickness is absent"),
			SizeCm.X, static_cast<double>(ThicknessCm), 1.0e-3);

		// hostThickness (v2 export) becomes the drawn depth, replacing the caller's constant. Kept as
		// a mutation of an opening that already placed, so only the depth differs.
		FBRiskVentGeometry ThickWindow = Window;
		ThickWindow.HostThicknessMetres = 0.2;
		FVector ThickCentreCm = FVector::ZeroVector;
		FVector ThickSizeCm = FVector::ZeroVector;
		if (TestTrue(TEXT("Window with a host thickness should place"),
			ABRiskHazardVisualizer::ComputeVentSlab(
				ThickWindow, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm, ThickCentreCm, ThickSizeCm)))
		{
			TestEqual(TEXT("Depth should be the host wall thickness, not the fallback"),
				ThickSizeCm.X, 20.0, 1.0e-3);
			TestEqual(TEXT("Host thickness must not disturb the opening width"),
				ThickSizeCm.Y, 105.0, 1.0e-3);
			TestEqual(TEXT("Host thickness must not move the centre"),
				ThickCentreCm.X, CentreCm.X, 1.0e-6);
		}

		// A modelled height smaller than the real one must NOT shrink the drawn opening - the same
		// split that keeps a 0.9 m door from drawing at its modelled 0.45 m.
		FBRiskVentGeometry ModelledShortWindow = Window;
		ModelledShortWindow.Height = 0.5;          // what B-Risk simulated
		ModelledShortWindow.PhysicalHeight = 1.35; // what is really there
		FVector ShortCentreCm = FVector::ZeroVector;
		FVector ShortSizeCm = FVector::ZeroVector;
		if (TestTrue(TEXT("Window with a reduced modelled height should place"),
			ABRiskHazardVisualizer::ComputeVentSlab(
				ModelledShortWindow, &Room, nullptr, Scale, Data.RoomFrame, ThicknessCm,
				ShortCentreCm, ShortSizeCm)))
		{
			TestEqual(TEXT("Drawn height should be the real height, not the modelled one"),
				ShortSizeCm.Z, 135.0, 1.0e-3);
		}
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
	// Roots come from MobiusTestDataRoots.h. Absolute drive paths used to live here; they worked on
	// one machine and published its layout. Set MOBIUS_INTERNAL_DATA if your copy is elsewhere.
	const TArray<FString> InternalRoots = MobiusTestData::GetInternalDataRoots();
	FString RealSmv;
	// Newest export first: v2 added openings[].hostThickness, v1 dropped normal. Any of them
	// exercises placement; only v2 exercises the real host-wall depth.
	const TCHAR* InternalFolders[] = { TEXT("12-room-test-v2"), TEXT("12-room-test-vents_v1"), TEXT("12-room-test-vents") };
	for (const TCHAR* Folder : InternalFolders)
	{
		for (const FString& Root : InternalRoots)
		{
			const FString Candidate = FPaths::Combine(
				FString(Root), FString(Folder), TEXT("basemodel_default"), TEXT("basemodel_default.smv"));
			if (FPaths::FileExists(Candidate))
			{
				RealSmv = Candidate;
				break;
			}
		}
		if (!RealSmv.IsEmpty())
		{
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
	// (wall normal axis, wall coordinate) -> the outward sign every door on that wall reported.
	TMap<TPair<int32, double>, TArray<double>> DoorWallOutward;

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
		FVector MarkerOutward = FVector::ZeroVector;
		if (!ABRiskHazardVisualizer::ComputeVentSlab(
			Vent, VentRoom, nullptr, Scale, RealData.RoomFrame, ThicknessCm, MarkerCm, MarkerSizeCm,
			&MarkerNormalAxis, &MarkerOutward))
		{
			continue;
		}
		++Placed;
		Centres.Add(MarkerCm);

		// Group by the wall each opening landed on, to check flow directions across the set below.
		if (Vent.Kind == EBRiskVentKind::Door && MarkerNormalAxis >= 0 && MarkerNormalAxis <= 1)
		{
			const double WallCoord = FMath::RoundToDouble(MarkerCm[MarkerNormalAxis] * 10.0) / 10.0;
			DoorWallOutward.FindOrAdd(TPair<int32, double>(MarkerNormalAxis, WallCoord))
				.Add(MarkerOutward[MarkerNormalAxis]);
		}

		// Every marker must be thin along the WALL normal. 18 of these 34 are leakage paths narrower
		// than the 8 cm slab, so an outline built on "thinnest axis" would lie across the wall.
		if (MarkerNormalAxis != 0 && MarkerNormalAxis != 1)
		{
			AddError(FString::Printf(TEXT("Vent %d reported normal axis %d."), Vent.VentId, MarkerNormalAxis));
		}
		else
		{
			// Depth through the wall: the host wall's real thickness when the export supplies one
			// (v2 onward), otherwise the caller's fallback. Asserting the RESOLVED value keeps this
			// honest across all three export generations rather than pinning one constant.
			const double ExpectedDepthCm = (Vent.HostThicknessMetres > 0.0)
				? Vent.HostThicknessMetres * Scale
				: static_cast<double>(ThicknessCm);
			if (!FMath::IsNearlyEqual(MarkerSizeCm[MarkerNormalAxis], ExpectedDepthCm, 1.0e-3))
			{
				AddError(FString::Printf(
					TEXT("Vent %d is %.2f cm deep along its reported normal axis, expected %.2f cm."),
					Vent.VentId, MarkerSizeCm[MarkerNormalAxis], ExpectedDepthCm));
			}
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

		// hostThickness/2 IS the measured stand-off from the room polygon - 0.100 m against a
		// declared 0.200 m wall across the whole model - so when the export supplies the field the
		// two must corroborate each other rather than drift apart. Leakage is excluded: the add-in
		// derives wall-leakage vents from the room boundary, so those legitimately sit at 0.
		if (Vent.HostThicknessMetres > 0.0 && Vent.Kind != EBRiskVentKind::Leakage
			&& !FMath::IsNearlyEqual(NearestCm, Vent.HostThicknessMetres * Scale * 0.5, 0.5))
		{
			AddError(FString::Printf(
				TEXT("Vent %d stands %.2f cm off the wall but declares a %.2f cm host wall."),
				Vent.VentId, NearestCm, Vent.HostThicknessMetres * Scale));
		}
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

	// Owner-reported: every door along the corridor showed its flow pointing the same way. The
	// outward direction was decided by "is the opening beyond the room's bounding-box centre", and
	// Corridor 15's spur drags that centre to UE Y 13.92 while the leg occupies 16.02..17.02 - so
	// both long walls answered +Y. Two facts pin it: every door on ONE wall must agree with itself,
	// and the corridor's two opposing walls must disagree with each other.
	TArray<double> CorridorLongWallSigns;
	for (const TPair<TPair<int32, double>, TArray<double>>& Wall : DoorWallOutward)
	{
		const TArray<double>& Signs = Wall.Value;
		for (const double Sign : Signs)
		{
			TestEqual(FString::Printf(TEXT("doors on the wall at axis %d / %.1f cm all face the same way"),
				Wall.Key.Key, Wall.Key.Value), Sign, Signs[0], 1.0e-9);
		}

		// The two long corridor walls: normal along Y, six doors each.
		if (Wall.Key.Key == 1 && Signs.Num() == 6)
		{
			CorridorLongWallSigns.Add(Signs[0]);
		}
	}

	if (TestEqual(TEXT("the corridor's two six-door walls should both be found"),
		CorridorLongWallSigns.Num(), 2))
	{
		TestNotEqual(TEXT("facing walls of the corridor must point in OPPOSITE directions"),
			CorridorLongWallSigns[0], CorridorLongWallSigns[1]);
	}

	// --- Every marker must sit inside its own hole ---------------------------------------------
	//
	// The hole and the marker are sized from the same four numbers, so this is the free check that
	// falls out of building both: if a marker overhangs its opening, one of the two is wrong. It has
	// force only because both go through BRiskCoord::ResolveOpeningEdge rather than each carrying a
	// copy of the nearest-edge search.
	{
		TArray<FVector> MeshVertices;
		TArray<int32> MeshTriangles;
		TArray<FVector> MeshNormals;
		FString MeshError;
		if (TestTrue(TEXT("The 12-room scenario should build a mesh"),
			UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
				RealData.Rooms, RealData.Vents, Scale, RealData.RoomFrame,
				MeshVertices, MeshTriangles, MeshNormals, &MeshError)))
		{
			int32 ExpectedCuts = 0;
			int32 MarkersOutsideTheirHole = 0;

			for (const FBRiskVentGeometry& Vent : RealData.Vents)
			{
				if (!Vent.bHasPlacement || Vent.Kind == EBRiskVentKind::Leakage)
				{
					continue;
				}

				const int32 RoomIds[] = { Vent.FromRoomId, Vent.ToRoomId };
				for (const int32 RoomId : RoomIds)
				{
					const FBRiskRoomGeometry* HoleRoom = RealData.Rooms.FindByPredicate(
						[RoomId](const FBRiskRoomGeometry& Candidate) { return Candidate.RoomId == RoomId; });
					if (!HoleRoom)
					{
						continue;
					}

					const BRiskCoord::FRoomFootprintCm Footprint =
						BRiskCoord::MakeRoomFootprint(*HoleRoom, Scale, RealData.RoomFrame);
					const double WidthCm = (Vent.PhysicalWidth > 0.0 ? Vent.PhysicalWidth : Vent.Width) * Scale;
					const FVector OpeningCentreCm = BRiskCoord::FootprintToUnreal(Vent.CentreMetres, Scale);

					BRiskCoord::FOpeningEdgePlacement Placement;
					if (!BRiskCoord::ResolveOpeningEdge(
						Footprint.Polygon, FVector2D(OpeningCentreCm.X, OpeningCentreCm.Y), WidthCm, Placement))
					{
						continue;
					}
					// The same bound the mesh applies, through the same function rather than a
					// second copy of the formula. An inlined copy here silently dropped the
					// no-thickness branch, so on a pre-v2 fixture it would have expected zero cuts
					// from code that cut all of them correctly.
					if (Placement.DistanceCm
						> BRiskCoord::MaxOpeningStandoffCm(Vent.HostThicknessMetres, Scale))
					{
						continue;
					}
					++ExpectedCuts;

					// Where the marker meets this room's wall: the centre dropped onto the wall
					// plane, at the marker's own mid-height.
					const FVector2D& EdgeStart = Footprint.Polygon[Placement.EdgeIndex];
					const FVector2D& EdgeEnd =
						Footprint.Polygon[(Placement.EdgeIndex + 1) % Footprint.Polygon.Num()];
					const FVector2D AlongUnit = (EdgeEnd - EdgeStart).GetSafeNormal();
					const FVector2D OnWall = EdgeStart + AlongUnit * Placement.AlongCm;
					const FVector Outward(AlongUnit.Y, -AlongUnit.X, 0.0);
					const double DrawHeight = (Vent.PhysicalHeight > 0.0) ? Vent.PhysicalHeight : Vent.Height;
					const double MidZ = (HoleRoom->Origin.Z + Vent.SillHeight + DrawHeight * 0.5) * Scale;

					if (IsWallPointCovered(MeshVertices, MeshTriangles, MeshNormals,
						FVector(OnWall.X, OnWall.Y, MidZ), Outward))
					{
						++MarkersOutsideTheirHole;
						AddError(FString::Printf(
							TEXT("Vent %d's marker sits against solid wall in room %d - no hole was cut for it."),
							Vent.VentId, RoomId));
					}
				}
			}

			// 15 doors + 1 window, of which only the Lobby/Corridor door has a second room with a
			// footprint, so it is cut from both sides.
			TestEqual(TEXT("17 openings should be cut across the two rooms with footprints"),
				ExpectedCuts, 17);
			TestEqual(TEXT("Every marker sits inside its own hole"), MarkersOutsideTheirHole, 0);
			// Say it out loud: a fixture that is not on this machine makes the whole block above
			// vanish, and a test that checked nothing still reports success.
			AddInfo(FString::Printf(
				TEXT("%d openings cut into the real 12-room mesh, %d markers outside their own hole"),
				ExpectedCuts, MarkersOutsideTheirHole));
		}
		else
		{
			AddError(FString::Printf(TEXT("Room mesh build error: %s"), *MeshError));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskVentScheduleTest,
	"ProjectMobius.BRisk.Importer.VentSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * B-Risk's vent open/close schedule, read from its own vents.xml.
 *
 * Sourced there rather than from the Revit add-in's openTimeS/closeTimeS - which measure identical
 * on all 34 openings - because vents.xml ships with every B-Risk run that has vents, so scheduling
 * also reaches .smv-only scenarios and there is one code path instead of two.
 *
 * The join is the part that can silently go wrong, so most of this test is about refusing to guess.
 *
 * Also covers the per-vent DISCHARGE COEFFICIENT, because it is carried by the same <Vent> record
 * and applied by the same join - splitting it into its own test would duplicate the fixture lookup
 * and add a second skip branch. The numeric consequence (cd is a pure multiplier on flow) is
 * asserted separately, in ProjectMobius.BRisk.Hazard.VentFlow.
 */
bool FBRiskVentScheduleTest::RunTest(const FString& Parameters)
{
	// --- What open/close MEAN, before any file is involved -----------------------------------
	{
		FBRiskVentGeometry Door;
		Door.bHasSchedule = true;
		Door.OpenTimeSeconds = 10.0;
		Door.CloseTimeSeconds = 60.0;
		TestFalse(TEXT("A door that opens at 10 s is SHUT at the start"), Door.IsOpenAtTime(0.0));
		TestFalse(TEXT("...still shut a moment before it opens"), Door.IsOpenAtTime(9.99));
		TestTrue(TEXT("...open exactly on its open time"), Door.IsOpenAtTime(10.0));
		TestTrue(TEXT("...open in between"), Door.IsOpenAtTime(30.0));
		TestFalse(TEXT("...shut again exactly on its close time"), Door.IsOpenAtTime(60.0));
		TestFalse(TEXT("...and stays shut"), Door.IsOpenAtTime(600.0));

		// B-Risk writes 0/0 for an opening it never changes. That is a real schedule meaning
		// "always open", not a missing one - every leakage path in the 12-room export carries it.
		FBRiskVentGeometry Always;
		Always.bHasSchedule = true;
		TestTrue(TEXT("0/0 is a schedule meaning always open, at t=0"), Always.IsOpenAtTime(0.0));
		TestTrue(TEXT("0/0 is still open late in the run"), Always.IsOpenAtTime(600.0));

		// An opening we could not match must behave exactly as it did before schedules existed.
		FBRiskVentGeometry Unmatched;
		TestFalse(TEXT("An unmatched vent has no schedule"), Unmatched.bHasSchedule);
		TestTrue(TEXT("...and is treated as permanently open"), Unmatched.IsOpenAtTime(0.0));
		TestTrue(TEXT("...at every time"), Unmatched.IsOpenAtTime(1.0e6));

		// closetime 0 with a real opentime is B-Risk for "opens and never shuts", not a
		// zero-length window that would leave the opening shut for the whole run.
		FBRiskVentGeometry OpensOnly;
		OpensOnly.bHasSchedule = true;
		OpensOnly.OpenTimeSeconds = 10.0;
		OpensOnly.CloseTimeSeconds = 0.0;
		TestFalse(TEXT("Opens-and-never-shuts is shut before its open time"), OpensOnly.IsOpenAtTime(5.0));
		TestTrue(TEXT("...and open forever after"), OpensOnly.IsOpenAtTime(1.0e6));
	}

	// --- A .smv-only scenario: joined on the room pair, because there is no ventId ------------
	{
		const FString TestDir = MakeBRiskTestDir();
		const FString SmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
		if (!TestTrue(TEXT("SMV should be written"), WriteTextFile(SmvPath, MakeSmv())))
		{
			return false;
		}
		// MakeSmv's single VENTGEOM is fromRoom 1, toRoom 2, sill 0.
		WriteTextFile(FPaths::Combine(TestDir, TEXT("vents.xml")),
			TEXT("<Vents><Vent><id>1</id><fromroom>1</fromroom><toroom>2</toroom>")
			TEXT("<sillheight>0</sillheight><opentime>15</opentime><closetime>45</closetime>")
			TEXT("<cd>0.9</cd></Vent></Vents>"));

		FBRiskScenarioData Data;
		FString Error;
		if (TestTrue(TEXT("A .smv-only scenario with vents.xml should import"),
			FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error))
			&& TestEqual(TEXT("One vent"), Data.Vents.Num(), 1))
		{
			TestTrue(TEXT("The room pair alone identified it"), Data.Vents[0].bHasSchedule);
			TestEqual(TEXT("Open time from vents.xml"), Data.Vents[0].OpenTimeSeconds, 15.0, 1.0e-9);
			TestEqual(TEXT("Close time from vents.xml"), Data.Vents[0].CloseTimeSeconds, 45.0, 1.0e-9);
			TestTrue(TEXT("Shut at the start"), !Data.Vents[0].IsOpenAtTime(0.0));

			// The discharge coefficient rides the same join. A value that is neither B-Risk's
			// default nor 1.0 is used deliberately, so a test that passes cannot be passing
			// because the field happened to keep its default.
			TestEqual(TEXT("Discharge coefficient from vents.xml"),
				Data.Vents[0].DischargeCoefficient, 0.9, 1.0e-9);
		}
		IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	}

	// --- A missing or nonsense <cd> falls back to B-Risk's default, never to zero ---------------
	//
	// The failure this guards is specific and silent: the XML helper's own default is 0.0, and Cd
	// multiplies every slab of flux, so reading a missing tag straight through would zero all flow
	// through the opening and still report a successful import.
	{
		const auto ImportWithCdTag = [&](const TCHAR* CdTag) -> double
		{
			const FString TestDir = MakeBRiskTestDir();
			const FString SmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
			WriteTextFile(SmvPath, MakeSmv());
			WriteTextFile(FPaths::Combine(TestDir, TEXT("vents.xml")),
				FString(TEXT("<Vents><Vent><id>1</id><fromroom>1</fromroom><toroom>2</toroom>"))
				+ TEXT("<sillheight>0</sillheight><opentime>15</opentime><closetime>45</closetime>")
				+ CdTag + TEXT("</Vent></Vents>"));

			FBRiskScenarioData Data;
			FString Error;
			const bool bOk = FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)
				&& Data.Vents.Num() == 1;
			const double Cd = bOk ? Data.Vents[0].DischargeCoefficient : -1.0;
			IFileManager::Get().DeleteDirectory(*TestDir, false, true);
			return Cd;
		};

		TestEqual(TEXT("A vents.xml record with no <cd> gets B-Risk's default, not 0"),
			ImportWithCdTag(TEXT("")), BRiskDefaultDischargeCoefficient, 1.0e-9);
		TestEqual(TEXT("<cd>0</cd> is rejected as out of range, not used"),
			ImportWithCdTag(TEXT("<cd>0</cd>")), BRiskDefaultDischargeCoefficient, 1.0e-9);
		TestEqual(TEXT("a negative <cd> is rejected"),
			ImportWithCdTag(TEXT("<cd>-0.5</cd>")), BRiskDefaultDischargeCoefficient, 1.0e-9);
		TestEqual(TEXT("<cd> above 1 is rejected - it is a fraction of the geometric area"),
			ImportWithCdTag(TEXT("<cd>1.4</cd>")), BRiskDefaultDischargeCoefficient, 1.0e-9);
		TestEqual(TEXT("<cd>1</cd> is in range and IS used - this is the leakage-path case"),
			ImportWithCdTag(TEXT("<cd>1</cd>")), 1.0, 1.0e-9);
	}

	// --- Ambiguity is refused, not guessed ----------------------------------------------------
	//
	// This is the case that matters. The .smv's VENTGEOM order is NOT the vents.xml id order -
	// measured, positional (fromroom,toroom) agrees 20/34 in the 12-room export and 0/3 in
	// 3_RoomFire - so falling back to "same index" would confidently shut the wrong doors.
	{
		const FString TestDir = MakeBRiskTestDir();
		const FString SmvPath = FPaths::Combine(TestDir, TEXT("basemodel_testBox.smv"));
		WriteTextFile(SmvPath,
			TEXT("ROOM   1\n 2.4000E+001 5.5000E+000 2.6000E+000\n 0.0000E+000 0.0000E+000 0.0000E+000\n")
			TEXT("VENTGEOM\n1 2 4 1.0 0.0 0.0 2.0\n")
			TEXT("VENTGEOM\n1 2 4 1.0 3.0 0.0 2.0\n")
			TEXT("ZONE\nbasemodel_testBox_zone.csv\n"));
		// Two records that fit both vents equally well. Neither may be applied.
		WriteTextFile(FPaths::Combine(TestDir, TEXT("vents.xml")),
			TEXT("<Vents>")
			TEXT("<Vent><id>1</id><fromroom>1</fromroom><toroom>2</toroom><sillheight>0</sillheight>")
			TEXT("<opentime>15</opentime><closetime>45</closetime></Vent>")
			TEXT("<Vent><id>2</id><fromroom>1</fromroom><toroom>2</toroom><sillheight>0</sillheight>")
			TEXT("<opentime>20</opentime><closetime>50</closetime></Vent>")
			TEXT("</Vents>"));

		FBRiskScenarioData Data;
		FString Error;
		if (FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &Error)
			&& TestEqual(TEXT("Two vents"), Data.Vents.Num(), 2))
		{
			TestFalse(TEXT("An ambiguous vent gets no schedule rather than the wrong one"),
				Data.Vents[0].bHasSchedule);
			TestFalse(TEXT("...both of them"), Data.Vents[1].bHasSchedule);
			TestTrue(TEXT("...so both stay permanently open, exactly as before schedules existed"),
				Data.Vents[0].IsOpenAtTime(0.0) && Data.Vents[1].IsOpenAtTime(0.0));
		}
		IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	}

	// --- The real export: joined exactly, by the ventId the add-in already recorded -----------
	// Roots come from MobiusTestDataRoots.h. Absolute drive paths used to live here; they worked on
	// one machine and published its layout. Set MOBIUS_INTERNAL_DATA if your copy is elsewhere.
	const TArray<FString> InternalRoots = MobiusTestData::GetInternalDataRoots();
	FString RealSmv;
	for (const TCHAR* Folder : { TEXT("12-room-test-v2"), TEXT("12-room-test-vents_v1"), TEXT("12-room-test-vents") })
	{
		for (const FString& Root : InternalRoots)
		{
			const FString Candidate = FPaths::Combine(
				FString(Root), FString(Folder), TEXT("basemodel_default"), TEXT("basemodel_default.smv"));
			if (FPaths::FileExists(Candidate) && FPaths::FileExists(
				FPaths::Combine(FString(Root), FString(Folder), TEXT("basemodel_default"), TEXT("vents.xml"))))
			{
				RealSmv = Candidate;
				break;
			}
		}
		if (!RealSmv.IsEmpty())
		{
			break;
		}
	}
	if (RealSmv.IsEmpty())
	{
		AddInfo(TEXT("real-dataset schedule check SKIPPED: no 12-room fixture with a vents.xml on this machine"));
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

	// Assert per KIND rather than by raw counts. It is what the numbers actually mean, and it does
	// not go stale the moment the model is re-exported with a different schedule.
	//
	// Measured on this export: the 15 DOORS carry 10/60, so they are shut at the start, open during
	// the fire and shut again. The single WINDOW carries 0/0, and so does every leakage path -
	// nobody opens or shuts them, which is what "no scheduled change" means. The window sitting with
	// the leakage rather than with the doors is correct, not an anomaly: it is an opening that is
	// simply open for the whole run.
	int32 Scheduled = 0;
	int32 Doors = 0;
	int32 NeverChanging = 0;
	int32 AutoOpening = 0;
	for (const FBRiskVentGeometry& Vent : RealData.Vents)
	{
		Scheduled += Vent.bHasSchedule ? 1 : 0;

		// An auto-opening vent is shut for the whole run, so it is neither "never changing" (which
		// means permanently OPEN below) nor a door. Counted and asserted on its own.
		if (Vent.bAutoOpenVent)
		{
			++AutoOpening;
			TestFalse(FString::Printf(TEXT("auto-opening vent %d is shut at the start"), Vent.VentId),
				Vent.IsOpenAtTime(0.0));
			TestFalse(FString::Printf(TEXT("auto-opening vent %d is still shut mid-fire"), Vent.VentId),
				Vent.IsOpenAtTime(300.0));
			TestFalse(FString::Printf(TEXT("auto-opening vent %d never opens on a clock"), Vent.VentId),
				Vent.IsOpenAtTime(1.0e6));
			continue;
		}

		const bool bNeverChanges = Vent.IsOpenAtTime(0.0) && Vent.IsOpenAtTime(1.0e6);
		NeverChanging += bNeverChanges ? 1 : 0;

		if (Vent.Kind == EBRiskVentKind::Door)
		{
			++Doors;
			TestFalse(FString::Printf(TEXT("door %d is shut before the fire starts"), Vent.VentId),
				Vent.IsOpenAtTime(0.0));
			TestTrue(FString::Printf(TEXT("door %d is open during the fire"), Vent.VentId),
				Vent.IsOpenAtTime(30.0));
			TestFalse(FString::Printf(TEXT("door %d is shut again afterwards"), Vent.VentId),
				Vent.IsOpenAtTime(100.0));
		}
		else
		{
			TestTrue(FString::Printf(TEXT("opening %d (a %s) is never shut"),
				Vent.VentId, Vent.Kind == EBRiskVentKind::Leakage ? TEXT("leakage path") : TEXT("window")),
				bNeverChanges);
		}
	}

	// Every opening matched: the add-in's ventId IS the vents.xml <id>, so nothing here falls back
	// to the room-pair join, let alone to an index.
	//
	// The 19/18 split changed on 2026-08-07 and the reason matters: the WINDOW used to be counted as
	// "never changes" because its times are 0/0, which read as permanently open. It carries
	// autoopenvent=True, so it is really shut for the whole run - confirmed against B-Risk's own
	// wallventflows.txt, where it is absent from all 61 timesteps. See the flow-log test below.
	TestEqual(TEXT("All 34 openings should get a schedule"), Scheduled, RealData.Vents.Num());
	TestEqual(TEXT("15 doors are the ones that move"), Doors, 15);
	TestEqual(TEXT("1 opening opens on a trigger, not a clock"), AutoOpening, 1);
	TestEqual(TEXT("the other 18 openings never change"), NeverChanging, 18);
	AddInfo(FString::Printf(
		TEXT("%d of %d openings scheduled; %d doors open and shut, %d auto-opening (shown shut), %d never change"),
		Scheduled, RealData.Vents.Num(), Doors, AutoOpening, NeverChanging));

	// --- Discharge coefficient, on the real export ---------------------------------------------
	//
	// The regression this pins is a hardcoded Cd = 0.68 in ComputeWallVentFlow, which ignored the
	// per-vent <cd> that B-Risk writes and its users edit.
	//
	// Do NOT reduce this to "leakage paths carry 1.0" - that reading is wrong and the file says so.
	// This export has TWO different things called leakage: 15 CLOSED-DOOR leakage paths, which are
	// the gap around a shut door and carry 0.68 like the door itself, and 3 WALL leakage paths
	// (ids 32/33/34, "wall leakage #1->#2 / #1->exterior / #2->exterior"), which carry 1.0 because
	// a wall-leakage width is already a calibrated effective area. Asserting per Kind would pass
	// for the wrong reason or fail for a correct file; the ids are the honest key here.
	//
	// Re-derive any of this from the fixture rather than trusting this comment:
	//   grep -o '<cd>[^<]*</cd>' vents.xml | sort | uniq -c        -> 31 x 0.68, 3 x 1
	TArray<int32> NonDefaultCdVentIds;
	for (const FBRiskVentGeometry& Vent : RealData.Vents)
	{
		TestTrue(FString::Printf(TEXT("vent %d has a physical discharge coefficient in (0, 1]"), Vent.VentId),
			Vent.DischargeCoefficient > 0.0 && Vent.DischargeCoefficient <= 1.0);

		if (!FMath::IsNearlyEqual(
			Vent.DischargeCoefficient, BRiskDefaultDischargeCoefficient, 1.0e-9))
		{
			NonDefaultCdVentIds.Add(Vent.VentId);
		}
	}
	NonDefaultCdVentIds.Sort();

	// The load-bearing assertion. If someone re-hardcodes Cd, every vent lands on the default and
	// this list empties - which is the only symptom the old defect ever had.
	TestEqual(TEXT("3 openings carry a discharge coefficient that is NOT B-Risk's default"),
		NonDefaultCdVentIds.Num(), 3);
	if (NonDefaultCdVentIds.Num() == 3)
	{
		TestEqual(TEXT("...and they are the three wall-leakage paths, ids 32/33/34"),
			FString::Printf(TEXT("%d,%d,%d"),
				NonDefaultCdVentIds[0], NonDefaultCdVentIds[1], NonDefaultCdVentIds[2]),
			FString(TEXT("32,33,34")));
		for (const int32 VentId : NonDefaultCdVentIds)
		{
			const FBRiskVentGeometry* Wall = RealData.Vents.FindByPredicate(
				[VentId](const FBRiskVentGeometry& V) { return V.VentId == VentId; });
			if (Wall)
			{
				TestEqual(FString::Printf(TEXT("wall-leakage vent %d carries cd 1.0"), VentId),
					Wall->DischargeCoefficient, 1.0, 1.0e-9);
			}
		}
	}
	AddInfo(FString::Printf(
		TEXT("discharge coefficients: %d of %d openings at B-Risk's default %g; %d read from <cd> as non-default"),
		RealData.Vents.Num() - NonDefaultCdVentIds.Num(), RealData.Vents.Num(),
		BRiskDefaultDischargeCoefficient, NonDefaultCdVentIds.Num()));

	return true;
}

// --- Our open/close state vs B-Risk's OWN flow log ----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskVentStateVsFlowLogTest,
	"ProjectMobius.BRisk.Importer.VentStateVsBRiskFlowLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskVentStateVsFlowLogTest::RunTest(const FString& Parameters)
{
	// B-Risk can be asked to write wallventflows.txt, and it is the ONLY output anywhere that
	// reveals whether a vent is open: a shut vent simply does not appear at that timestep. Nothing
	// else does - output1.xml has no vent element, the run log has no vent lines, and the zone CSV's
	// HVENT columns are static nominal geometry (width * (head - sill)) that never vary.
	//
	// So this is the one place our IsOpenAtTime can be checked against ground truth instead of
	// against our own reading of the manual. It caught the real defect: the window (vent 27) carries
	// 0/0 times, which we read as "permanently open", while B-Risk had it shut for the entire run.
	// Roots come from MobiusTestDataRoots.h. Absolute drive paths used to live here; they worked on
	// one machine and published its layout. Set MOBIUS_INTERNAL_DATA if your copy is elsewhere.
	const TArray<FString> InternalRoots = MobiusTestData::GetInternalDataRoots();
	FString SmvPath;
	FString FlowLogPath;
	for (const TCHAR* Folder : { TEXT("12-room-test-v2"), TEXT("12-room-test-vents_v1"), TEXT("12-room-test-vents") })
	{
		for (const FString& Root : InternalRoots)
		{
			const FString Dir = FPaths::Combine(FString(Root), FString(Folder), TEXT("basemodel_default"));
			const FString Smv = FPaths::Combine(Dir, TEXT("basemodel_default.smv"));
			const FString Log = FPaths::Combine(Dir, TEXT("wallventflows.txt"));
			if (FPaths::FileExists(Smv) && FPaths::FileExists(Log))
			{
				SmvPath = Smv;
				FlowLogPath = Log;
				break;
			}
		}
		if (!SmvPath.IsEmpty())
		{
			break;
		}
	}
	if (SmvPath.IsEmpty())
	{
		// Genuinely optional: wallventflows.txt only exists when the modeller ticked the box.
		AddInfo(TEXT("flow-log cross-check SKIPPED: no 12-room fixture with a wallventflows.txt on this machine"));
		return true;
	}

	FBRiskScenarioData Data;
	FString ImportError;
	if (!TestTrue(TEXT("The export should import"),
		FBRiskDataImporter::ImportScenarioFromSmv(SmvPath, Data, &ImportError)))
	{
		AddError(FString::Printf(TEXT("Import error: %s"), *ImportError));
		return false;
	}

	TArray<FString> Lines;
	if (!TestTrue(TEXT("The flow log should read"), FFileHelper::LoadFileToStringArray(Lines, *FlowLogPath)))
	{
		return false;
	}

	// Presence set keyed by (time, fromRoom, toRoom, ventNumberWithinThatPair). Only the header line
	// of each record carries those; the continuation lines are extra elevation bands of the same vent
	// and are distinguished by having far fewer tokens.
	TSet<FString> Present;
	TSet<int32> LoggedTimes;
	TMap<FIntPoint, int32> LogPairHighest;
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.TrimStartAndEnd().ParseIntoArrayWS(Tok);
		if (Tok.Num() < 8)
		{
			continue;
		}
		bool bHeader = true;
		for (int32 k = 0; k < 5; ++k)
		{
			if (Tok[k].Contains(TEXT(".")) || !Tok[k].IsNumeric())
			{
				bHeader = false;
				break;
			}
		}
		if (!bHeader)
		{
			continue;
		}
		const int32 T = FCString::Atoi(*Tok[0]);
		LoggedTimes.Add(T);
		Present.Add(FString::Printf(TEXT("%d|%s|%s|%s"), T, *Tok[1], *Tok[2], *Tok[3]));

		// Highest vent number the log ever uses for each pair. B-Risk numbers 1..N over ALL vents on
		// the pair including ones that never open, so this is N and we can check our own count against
		// it - the one cheap structural check that catches a numbering drift head-on.
		int32& HighestForPair = LogPairHighest.FindOrAdd(
			FIntPoint(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2])));
		HighestForPair = FMath::Max(HighestForPair, FCString::Atoi(*Tok[3]));
	}

	if (!TestTrue(TEXT("The flow log should contain timesteps"), LoggedTimes.Num() > 0))
	{
		return false;
	}

	// B-Risk numbers vents per room PAIR, starting at 1, in the same order the .smv lists them - and it
	// keeps the number of a vent that never opens, so a closed vent leaves a HOLE in the sequence. That
	// hole is what identifies the window: rooms 1/3 log #1, #2 and #4.
	//
	// The pair is UNORDERED in the log. B-Risk always prints it low room first, whichever way vents.xml
	// declares it, so a vent whose fromroom exceeds its toroom is logged reversed. In the 12-room export
	// vents 28 and 29 are declared 2->1 while the log calls that pair 1->2; keying on the raw declared
	// order gives them a private counter that matches nothing, and it displaces vent 32 (declared 1->2)
	// onto vent 28's number. Those three vents alone produced 119 false disagreements. Normalise both the
	// counter and the lookup to (min,max) and the cross-check runs clean at every one of the 2025
	// vent/time samples.
	auto NormalisedPair = [](const FBRiskVentGeometry& V)
	{
		return FIntPoint(FMath::Min(V.FromRoomId, V.ToRoomId), FMath::Max(V.FromRoomId, V.ToRoomId));
	};

	TMap<FIntPoint, int32> PairCounter;
	TArray<int32> VentNumber;
	VentNumber.Reserve(Data.Vents.Num());
	for (const FBRiskVentGeometry& Vent : Data.Vents)
	{
		int32& Next = PairCounter.FindOrAdd(NormalisedPair(Vent));
		VentNumber.Add(++Next);
	}

	// Structural check on the numbering itself, run BEFORE the state comparison so a drift reports as
	// "the oracle is broken" rather than as hundreds of state disagreements - which is exactly how the
	// reversed-pair defect above first presented. Our count of vents on a pair must equal the highest
	// number the log uses for it: 12-room-test-v2 gives rooms 1/2 -> 3, rooms 1/3 -> 4, rooms 2/3 -> 27.
	for (const TPair<FIntPoint, int32>& Ours : PairCounter)
	{
		const int32* Logged = LogPairHighest.Find(Ours.Key);
		if (!TestNotNull(
			*FString::Printf(TEXT("The flow log knows about the room pair %d/%d that we place vents on"),
				Ours.Key.X, Ours.Key.Y),
			Logged))
		{
			continue;
		}
		TestEqual(
			*FString::Printf(TEXT("Vent count on room pair %d/%d matches B-Risk's numbering"),
				Ours.Key.X, Ours.Key.Y),
			Ours.Value, *Logged);
	}

	TArray<int32> SortedTimes = LoggedTimes.Array();
	SortedTimes.Sort();

	int32 Checked = 0;
	int32 Mismatches = 0;
	int32 SkippedAtTransition = 0;
	FString FirstMismatch;
	for (int32 VentIndex = 0; VentIndex < Data.Vents.Num(); ++VentIndex)
	{
		const FBRiskVentGeometry& Vent = Data.Vents[VentIndex];
		for (const int32 T : SortedTimes)
		{
			// The two transition samples are excluded on purpose. B-Risk ramps an opening over two
			// seconds rather than switching it (SR282 §4.6.2), and it reports a door from the sample
			// AFTER its open time up to and including its close time. Those are reporting-convention
			// edges, not state disagreements, and asserting them would encode B-Risk's logging
			// quirks rather than whether we know the vent is open.
			//
			// Do NOT narrow this to schedules with a positive open time. A permanently-open leakage
			// path carries opentime = closetime = 0, so this same rule also drops it at t = 0 - and
			// that is load-bearing, because B-Risk's t = 0 sample is an initialisation record that
			// lists ONE vent (1156 records over 61 steps, of which t = 0 contributes a single zero-flow
			// row). Comparing against it would fail 17 permanently-open vents that are not shut, merely
			// not yet flowing. Of the 49 exclusions, 30 are door transitions and 19 are this.
			const double Td = static_cast<double>(T);
			if (Vent.bHasSchedule
				&& (FMath::IsNearlyEqual(Td, Vent.OpenTimeSeconds, 0.5)
					|| FMath::IsNearlyEqual(Td, Vent.CloseTimeSeconds, 0.5)))
			{
				++SkippedAtTransition;
				continue;
			}

			const FIntPoint Pair = NormalisedPair(Vent);
			const bool bWeSayOpen = Vent.IsOpenAtTime(Td);
			const bool bBRiskSaysOpen = Present.Contains(FString::Printf(
				TEXT("%d|%d|%d|%d"), T, Pair.X, Pair.Y, VentNumber[VentIndex]));
			++Checked;
			if (bWeSayOpen != bBRiskSaysOpen)
			{
				++Mismatches;
				if (FirstMismatch.IsEmpty())
				{
					FirstMismatch = FString::Printf(
						TEXT("vent id %d (%d->%d, #%d) at t=%d: we say %s, B-Risk says %s"),
						Vent.VentId, Vent.FromRoomId, Vent.ToRoomId, VentNumber[VentIndex], T,
						bWeSayOpen ? TEXT("OPEN") : TEXT("SHUT"),
						bBRiskSaysOpen ? TEXT("OPEN") : TEXT("SHUT"));
				}
			}
		}
	}

	if (Mismatches > 0)
	{
		AddError(FString::Printf(TEXT("First disagreement: %s"), *FirstMismatch));
	}
	TestEqual(TEXT("Our open/shut state agrees with B-Risk's own flow log at every timestep"),
		Mismatches, 0);

	// Guard the guard: if the pair numbering ever stopped lining up, every vent would read as shut
	// and Mismatches could only be zero by us also calling everything shut. Assert that the log
	// really did place a healthy number of vents as open.
	TestTrue(TEXT("The cross-check actually compared something"), Checked > 500);
	TestTrue(TEXT("The log shows plenty of OPEN vents (numbering lines up)"), Present.Num() > 500);
	AddInfo(FString::Printf(
		TEXT("Cross-checked %d vent/time pairs against wallventflows.txt across %d timesteps: %d disagreements ")
		TEXT("(%d transition samples excluded, %d open records in the log)"),
		Checked, SortedTimes.Num(), Mismatches, SkippedAtTransition, Present.Num()));

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
	// written at authoring time, but nothing has produced it. Seen for real in the private
	// 12-room-test-vents export (see MobiusTestDataRoots.h for where those live).
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
	// Roots come from MobiusTestDataRoots.h. Absolute drive paths used to live here; they worked on
	// one machine and published its layout. Set MOBIUS_INTERNAL_DATA if your copy is elsewhere.
	const TArray<FString> InternalRoots = MobiusTestData::GetInternalDataRoots();
	FString RealSmv;
	// This test needs an export that has NOT been simulated, so select on that property rather than
	// on a folder name - the exports get re-run, and "12-room-test-v2" has results while its
	// predecessor does not. Picking by name is how this test started passing against the wrong data.
	const TCHAR* InternalFolders[] = { TEXT("12-room-test-vents"), TEXT("12-room-test-vents_v1"), TEXT("12-room-test-v2") };
	for (const TCHAR* Folder : InternalFolders)
	{
		for (const FString& Root : InternalRoots)
		{
			const FString Candidate = FPaths::Combine(
				FString(Root), FString(Folder), TEXT("basemodel_default"), TEXT("basemodel_default.smv"));
			const FString SiblingCsv = FPaths::Combine(
				FString(Root), FString(Folder), TEXT("basemodel_default"), TEXT("basemodel_default_zone.csv"));
			if (FPaths::FileExists(Candidate) && !FPaths::FileExists(SiblingCsv))
			{
				RealSmv = Candidate;
				break;
			}
		}
		if (!RealSmv.IsEmpty())
		{
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

// --- Orientation of the panel that fills a SHUT opening ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBRiskVentClosedPanelOrientationTest,
	"ProjectMobius.BRisk.Hazard.VentClosedPanelOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBRiskVentClosedPanelOrientationTest::RunTest(const FString& Parameters)
{
	// This test exists because the first version of the closed-opening panel shipped rotated 90
	// degrees and NOTHING in this file could see it: every other geometry assertion here reads mesh
	// triangles, and the panel is a UStaticMeshComponent whose transform never reaches a vertex
	// buffer. It was found by looking at the screen. ABRiskHazardVisualizer::ClosedPanelRotation was
	// extracted purely so the invariant could be stated here instead.
	//
	// The invariant is the FULL axis assignment, not just the facing direction. Asserting only
	// "local +Z lands on the wall normal" would still pass for MakeFromZX, which faces the panel
	// correctly while silently swapping the axes SetRelativeScale3D feeds width and height into - a
	// door that is 2.0 m wide and 0.8 m tall. So all three axes are checked, and they are checked
	// against the two scale terms by name.
	auto CheckNormal = [this](const TCHAR* Label, const FVector& Outward)
	{
		const FRotator Rotation = ABRiskHazardVisualizer::ClosedPanelRotation(Outward);
		const FVector LocalX = Rotation.RotateVector(FVector::XAxisVector);
		const FVector LocalY = Rotation.RotateVector(FVector::YAxisVector);
		const FVector LocalZ = Rotation.RotateVector(FVector::ZAxisVector);

		// /Engine/BasicShapes/Plane is 100x100 cm lying in local XY with its surface normal on local
		// +Z, so the SURFACE faces the way local +Z points. This is the assertion that fails on the
		// original MakeFromXY, which pointed the surface along the wall instead of through it.
		TestEqual(*FString::Printf(TEXT("%s: surface (local +Z) faces along the wall normal"), Label),
			FVector::DotProduct(LocalZ, Outward.GetSafeNormal()), 1.0, 1.0e-4);

		// Scaled by the opening HEIGHT (SizeCm.Z), so it must be world up - not merely perpendicular
		// to the normal, which the along-wall axis also is.
		TestEqual(*FString::Printf(TEXT("%s: height axis (local +Y) is world up"), Label),
			FVector::DotProduct(LocalY, FVector::UpVector), 1.0, 1.0e-4);

		// Scaled by the opening WIDTH, so it must lie in the wall: horizontal, and perpendicular to
		// the normal. Sign is deliberately not asserted - the plane is symmetric about its centre and
		// the material is two-sided, so a 180-degree flip along the wall is not observable.
		TestEqual(*FString::Printf(TEXT("%s: width axis (local +X) is horizontal"), Label),
			LocalX.Z, 0.0, 1.0e-4);
		TestEqual(*FString::Printf(TEXT("%s: width axis (local +X) lies in the wall"), Label),
			FVector::DotProduct(LocalX, Outward.GetSafeNormal()), 0.0, 1.0e-4);
	};

	// The four axis-aligned walls ComputeVentSlab actually produces today...
	CheckNormal(TEXT("+X wall"), FVector(1.0, 0.0, 0.0));
	CheckNormal(TEXT("-X wall"), FVector(-1.0, 0.0, 0.0));
	CheckNormal(TEXT("+Y wall"), FVector(0.0, 1.0, 0.0));
	CheckNormal(TEXT("-Y wall"), FVector(0.0, -1.0, 0.0));

	// ...plus a diagonal, so a rotation that happens to satisfy the axis-aligned cases by symmetry
	// cannot pass. NOTE: a green diagonal here says the panel FACES correctly, not that it is the
	// right size - the caller reads its width off an axis-aligned slab extent, which over-measures
	// on a diagonal wall. See the handoff; that is a separate open item.
	CheckNormal(TEXT("diagonal wall"), FVector(0.6, 0.8, 0.0));

	AddInfo(TEXT("Closed-opening panel orientation checked on 4 axis-aligned walls + 1 diagonal."));
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

	// --- Discharge coefficient is read from the vent, and is a pure multiplier ------------------
	//
	// This is the check that the <cd> fix is actually wired through, and it is exact rather than
	// qualitative. Cd multiplies every slab's flux and appears nowhere in the pressure profile, so
	// two otherwise identical vents must produce mass flows in exactly the ratio of their Cd, with
	// an IDENTICAL neutral plane. That second half matters: if Cd ever leaked into the pressure
	// integral the ratio could still look right while the flow split moved.
	//
	// Concretely, this is the 12-room export's wall-leakage case. Those three openings carry
	// <cd>1</cd> (a leakage width is already a calibrated effective area, so a second contraction
	// correction would double-count), and the old hardcoded 0.68 ran them at 68 % of B-Risk's own
	// flow for the whole period after the doors shut at 60 s.
	TestEqual(TEXT("a vent defaults to B-Risk's own discharge coefficient"),
		Vent.DischargeCoefficient, BRiskDefaultDischargeCoefficient, 1.0e-12);

	FBRiskVentGeometry Leakage = Vent;
	Leakage.DischargeCoefficient = 1.0; // what vents.xml carries for ids 32/33/34
	const FBRiskVentFlow LeakFlow = UBRiskDataSubsystem::ComputeWallVentFlow(Hot, Cool, Leakage);

	TestTrue(TEXT("a cd=1.0 opening flows MORE than the same opening at 0.68"),
		LeakFlow.MassFlowOutKgs > Flow.MassFlowOutKgs);
	TestEqual(TEXT("out-stream scales exactly with cd"),
		Flow.MassFlowOutKgs / LeakFlow.MassFlowOutKgs,
		BRiskDefaultDischargeCoefficient, 1.0e-9);
	TestEqual(TEXT("in-stream scales exactly with cd"),
		Flow.MassFlowInKgs / LeakFlow.MassFlowInKgs,
		BRiskDefaultDischargeCoefficient, 1.0e-9);
	TestEqual(TEXT("cd does not move the neutral plane - it is not part of the pressure profile"),
		LeakFlow.NeutralPlaneHeightM, Flow.NeutralPlaneHeightM, 1.0e-12);
	TestEqual(TEXT("cd does not change stream temperature - it cancels in the mass-weighted mean"),
		LeakFlow.OutTemperatureC, Flow.OutTemperatureC, 1.0e-9);

	// An out-of-range cd on a vent built in code (not parsed) must not reach the flow maths. The
	// importer already range-checks, but this function is public and takes any FBRiskVentGeometry.
	FBRiskVentGeometry Nonsense = Vent;
	Nonsense.DischargeCoefficient = 0.0;
	const FBRiskVentFlow NonsenseFlow = UBRiskDataSubsystem::ComputeWallVentFlow(Hot, Cool, Nonsense);
	TestEqual(TEXT("cd=0 falls back to the default rather than silently zeroing all flow"),
		NonsenseFlow.MassFlowOutKgs, Flow.MassFlowOutKgs, 1.0e-12);
	Nonsense.DischargeCoefficient = 7.5;
	const FBRiskVentFlow AbsurdFlow = UBRiskDataSubsystem::ComputeWallVentFlow(Hot, Cool, Nonsense);
	TestEqual(TEXT("an absurd cd falls back to the default too"),
		AbsurdFlow.MassFlowOutKgs, Flow.MassFlowOutKgs, 1.0e-12);

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
