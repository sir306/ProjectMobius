// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

#include "BRiskDataImporter.h"
#include "BRisk/BRiskDataSubsystem.h"
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
		return WriteTextFile(OutSmvPath, MakeSmv())
			&& WriteTextFile(OutCsvPath, MakeZoneCsv());
	}
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
	TestEqual(TEXT("Room X extent should be 2400 cm"), Bounds.GetSize().X, 2400.0);
	TestEqual(TEXT("Room Y extent should be 550 cm"), Bounds.GetSize().Y, 550.0);
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

	bool bFoundOffsetVentEdge = false;
	for (const FVector& Vertex : Vertices)
	{
		if (FMath::IsNearlyEqual(Vertex.X, 2400.0)
			&& FMath::IsNearlyEqual(Vertex.Y, 80.0)
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
		0.00045,
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
		4.45,
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
	TestEqual(TEXT("Lower-layer temperature should be preserved in state"),
		MidState.LowerTemperatureC,
		80.0f);

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

#endif
