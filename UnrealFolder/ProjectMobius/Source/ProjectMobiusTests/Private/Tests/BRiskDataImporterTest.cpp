// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

#include "BRiskDataImporter.h"
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

	/** Zones-data.json carrying the true L-shaped footprint for room 1, wound clockwise. */
	FString MakeZonesDataJson()
	{
		// Deliberately CW (negative shoelace) so the importer's winding normalisation is
		// exercised rather than assumed - the exporter guarantees no particular winding.
		return
			TEXT("{\n")
			TEXT("  \"format\": \"simulex-zones-data\",\n")
			TEXT("  \"version\": 1,\n")
			TEXT("  \"source\": { \"coordinateSystem\": \"revit-internal\", \"iteration\": 1 },\n")
			TEXT("  \"spaces\": [\n")
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
			TEXT("    }\n")
			TEXT("  ]\n")
			TEXT("}\n");
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
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, Vertices, Triangles, Normals, &Error));
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
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, Vertices, Triangles, Normals, &Error));
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
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, Vertices, Triangles, Normals, &Error));
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
		UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(Rooms, Vents, 100.0f, Vertices, Triangles, Normals, &Error));

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

	const FString MissingCsvDir = MakeBRiskTestDir();
	const FString MissingCsvSmv = FPaths::Combine(MissingCsvDir, TEXT("missing_csv.smv"));
	TestTrue(TEXT("Missing-CSV SMV should be written"), WriteTextFile(MissingCsvSmv, MakeSmv()));
	TestFalse(TEXT("Missing referenced CSV should fail"),
		FBRiskDataImporter::ImportScenarioFromSmv(MissingCsvSmv, Data, &Error));

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
		ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 2, 2, 0.0), &Room1, &Room2, Scale, Thickness, Center, Size));
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
		ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 3, 3, 0.0), &Room1, &Room3, Scale, Thickness, Center, Size));
	TestEqual(TEXT("vent 1->3 on +X wall"), Center.X, 800.0, 0.01);
	TestEqual(TEXT("vent 1->3 thin in X"), Size.X, ThicknessCm, 0.01);
	TestEqual(TEXT("vent 1->3 spans Y by width"), Size.Y, 240.0, 0.01);

	// Vent 1->4: exterior (no such room) -> CFAST face id 4 = B-Risk -X, which maps to the
	// Unreal -Y wall under the swap. Offset (0.8 m) runs along Unreal X.
	TestTrue(TEXT("vent 1->exterior resolves"),
		ABRiskHazardVisualizer::ComputeVentSlab(MakeVent(1, 4, 4, 0.8), &Room1, nullptr, Scale, Thickness, Center, Size));
	TestEqual(TEXT("exterior vent on -Y wall"), Center.Y, 0.0, 0.01);
	TestEqual(TEXT("exterior vent offset applied on X"), Center.X, 200.0, 0.01); // (80 + 320)/2

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

#endif
