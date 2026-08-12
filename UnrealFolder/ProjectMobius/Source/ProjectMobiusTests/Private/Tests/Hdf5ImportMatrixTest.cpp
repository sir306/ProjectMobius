// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// Hdf5ImportMatrixTest.cpp
//
// PRD 02 task T4 — the HDF5 half of the file-loading matrix (the JSON half is
// JsonParserParityTest.cpp). Minimal Mobius-format and Juelich-format .h5 fixtures are generated
// IN-TEST through the vendored HDF5 C API (deterministic, and keeps binary blobs out of the
// repo), then driven through the real FMobiusAgentDataImporter entry points:
//   - format detection (Mobius vs Juelich vs garbage)
//   - full-fixture import: entities/samples/metadata land intact
//   - minimal Mobius fixture (no rotation/speed/mode fields): presence flags come back false,
//     mode falls back to "walk"
//   - Juelich import: trajectory conversion produces the expected entity/sample counts
//   - error paths for every format: clean false + OutError, no crash
//   - OPTIONAL local-only large-file smoke (skips cleanly when the F: fixture is absent)
//
// Run: MobiusPerf\RunTests.ps1 -Filter "ProjectMobius.SimData."
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MobiusAgentDataImporter.h"

#include "hdf5.h" // via UeHdf5Library (same API the production reader uses)
#include "MobiusTestDataRoots.h"

namespace
{
	FString Hdf5TestDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusHdf5MatrixTest"));
	}

	// Fixture record layouts. Standard-layout structs + offsetof drive the compound types, same
	// technique as the production reader's positional decode (entities are read by member INDEX,
	// so member order here IS the contract).
	struct FFixtureEntity
	{
		int32 Id;
		char Name[32];
		float SimTimeS;
		float MaxSpeed;
		char MPlane[32];
		int32 Map;
	};

	struct FFixtureSample
	{
		int32 TimestepIdx;
		int32 EntityId;
		float PosX;
		float PosY;
		float PosZ;
		float Rotation;
		float Speed;
		char Mode[16];
	};

	/** Mobius sample without rotation/speed/mode — exercises the presence-flag detection. */
	struct FFixtureSampleMinimal
	{
		int32 TimestepIdx;
		int32 EntityId;
		float PosX;
		float PosY;
		float PosZ;
	};

	struct FFixtureTrajectory
	{
		int64 Id;
		int64 Frame;
		double X;
		double Y;
		double Z;
	};

	hid_t MakeFixedString(size_t Size)
	{
		const hid_t Type = H5Tcopy(H5T_C_S1);
		H5Tset_size(Type, Size);
		H5Tset_strpad(Type, H5T_STR_NULLTERM);
		return Type;
	}

	void WriteScalarAttr(hid_t Loc, const char* Name, hid_t Type, const void* Value)
	{
		const hid_t Space = H5Screate(H5S_SCALAR);
		const hid_t Attr = H5Acreate2(Loc, Name, Type, Space, H5P_DEFAULT, H5P_DEFAULT);
		H5Awrite(Attr, Type, Value);
		H5Aclose(Attr);
		H5Sclose(Space);
	}

	void WriteStringAttr(hid_t Loc, const char* Name, const char* Value)
	{
		const hid_t Type = MakeFixedString(FCStringAnsi::Strlen(Value) + 1);
		WriteScalarAttr(Loc, Name, Type, Value);
		H5Tclose(Type);
	}

	template <typename T>
	void WriteDataset(hid_t File, const char* Path, hid_t Type, const TArray<T>& Rows)
	{
		hsize_t Dims[1] = { static_cast<hsize_t>(Rows.Num()) };
		const hid_t Space = H5Screate_simple(1, Dims, nullptr);
		const hid_t Dataset = H5Dcreate2(File, Path, Type, Space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Dwrite(Dataset, Type, H5S_ALL, H5S_ALL, H5P_DEFAULT, Rows.GetData());
		H5Dclose(Dataset);
		H5Sclose(Space);
	}

	hid_t MakeEntityType()
	{
		const hid_t StrType = MakeFixedString(32);
		const hid_t Type = H5Tcreate(H5T_COMPOUND, sizeof(FFixtureEntity));
		H5Tinsert(Type, "id", offsetof(FFixtureEntity, Id), H5T_NATIVE_INT32);
		H5Tinsert(Type, "name", offsetof(FFixtureEntity, Name), StrType);
		H5Tinsert(Type, "sim_time_s", offsetof(FFixtureEntity, SimTimeS), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "max_speed", offsetof(FFixtureEntity, MaxSpeed), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "m_plane", offsetof(FFixtureEntity, MPlane), StrType);
		H5Tinsert(Type, "map", offsetof(FFixtureEntity, Map), H5T_NATIVE_INT32);
		H5Tclose(StrType);
		return Type;
	}

	/** Common Mobius skeleton: /metadata attrs + /entities + /simulation/{timesteps,
	 *  samples_per_timestep}; the caller adds /simulation/samples (full or minimal). */
	hid_t BeginMobiusFixture(const FString& Path, const TArray<int32>& SamplesPerTimestep)
	{
		FTCHARToUTF8 PathUtf8(*Path);
		const hid_t File = H5Fcreate(PathUtf8.Get(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
		if (File < 0)
		{
			return File;
		}

		const hid_t Meta = H5Gcreate2(File, "/metadata", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		const double Duration = 0.2;
		const double SamplingRate = 0.1;
		const int64 MaxNumEntities = 2;
		const hbool_t True = 1;
		WriteScalarAttr(Meta, "duration", H5T_NATIVE_DOUBLE, &Duration);
		WriteScalarAttr(Meta, "sampling_rate", H5T_NATIVE_DOUBLE, &SamplingRate);
		WriteScalarAttr(Meta, "max_num_entities", H5T_NATIVE_INT64, &MaxNumEntities);
		WriteScalarAttr(Meta, "is_si", H5T_NATIVE_HBOOL, &True);
		WriteScalarAttr(Meta, "is_deg", H5T_NATIVE_HBOOL, &True);
		H5Gclose(Meta);

		TArray<FFixtureEntity> Entities;
		Entities.SetNumZeroed(2);
		Entities[0].Id = 1; FCStringAnsi::Strncpy(Entities[0].Name, "h5_agent_one", 32);
		Entities[0].SimTimeS = 0.2f; Entities[0].MaxSpeed = 1.5f;
		FCStringAnsi::Strncpy(Entities[0].MPlane, "floor_0", 32); Entities[0].Map = 2;
		Entities[1].Id = 2; FCStringAnsi::Strncpy(Entities[1].Name, "h5_agent_two", 32);
		Entities[1].SimTimeS = 0.1f; Entities[1].MaxSpeed = 2.0f;
		FCStringAnsi::Strncpy(Entities[1].MPlane, "floor_1", 32); Entities[1].Map = 3;
		const hid_t EntityType = MakeEntityType();
		WriteDataset(File, "/entities", EntityType, Entities);
		H5Tclose(EntityType);

		H5Gclose(H5Gcreate2(File, "/simulation", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT));
		TArray<float> Timesteps;
		for (int32 i = 0; i < SamplesPerTimestep.Num(); ++i)
		{
			Timesteps.Add(0.1f * i);
		}
		WriteDataset(File, "/simulation/timesteps", H5T_NATIVE_FLOAT, Timesteps);
		WriteDataset(File, "/simulation/samples_per_timestep", H5T_NATIVE_INT32, SamplesPerTimestep);
		return File;
	}

	bool WriteMobiusFixtureFull(const FString& Path)
	{
		const hid_t File = BeginMobiusFixture(Path, { 2, 2, 1 });
		if (File < 0)
		{
			return false;
		}

		TArray<FFixtureSample> Samples;
		Samples.SetNumZeroed(5);
		auto Set = [&Samples](int32 i, int32 Ts, int32 Entity, float X, float Y, float Z, float Rot, float Speed, const char* Mode)
		{
			Samples[i].TimestepIdx = Ts; Samples[i].EntityId = Entity;
			Samples[i].PosX = X; Samples[i].PosY = Y; Samples[i].PosZ = Z;
			Samples[i].Rotation = Rot; Samples[i].Speed = Speed;
			FCStringAnsi::Strncpy(Samples[i].Mode, Mode, 16);
		};
		Set(0, 0, 1, 0.5f, 1.0f, 0.0f, 90.0f, 1.25f, "walk");
		Set(1, 0, 2, -1.5f, 2.0f, 0.0f, 45.0f, 0.75f, "run");
		Set(2, 1, 1, 0.6f, 1.1f, 0.0f, 91.0f, 1.30f, "walk");
		Set(3, 1, 2, -1.4f, 2.1f, 0.0f, 46.0f, 0.80f, "run");
		Set(4, 2, 1, 0.7f, 1.2f, 0.0f, 92.0f, 1.35f, "walk");

		const hid_t StrType = MakeFixedString(16);
		const hid_t Type = H5Tcreate(H5T_COMPOUND, sizeof(FFixtureSample));
		H5Tinsert(Type, "timestep_idx", offsetof(FFixtureSample, TimestepIdx), H5T_NATIVE_INT32);
		H5Tinsert(Type, "entity_id", offsetof(FFixtureSample, EntityId), H5T_NATIVE_INT32);
		H5Tinsert(Type, "position_x", offsetof(FFixtureSample, PosX), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "position_y", offsetof(FFixtureSample, PosY), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "position_z", offsetof(FFixtureSample, PosZ), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "rotation", offsetof(FFixtureSample, Rotation), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "speed", offsetof(FFixtureSample, Speed), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "mode", offsetof(FFixtureSample, Mode), StrType);
		WriteDataset(File, "/simulation/samples", Type, Samples);
		H5Tclose(Type);
		H5Tclose(StrType);
		H5Fclose(File);
		return true;
	}

	bool WriteMobiusFixtureMinimal(const FString& Path)
	{
		const hid_t File = BeginMobiusFixture(Path, { 1, 1 });
		if (File < 0)
		{
			return false;
		}

		TArray<FFixtureSampleMinimal> Samples;
		Samples.SetNumZeroed(2);
		Samples[0] = { 0, 1, 0.5f, 1.0f, 0.0f };
		Samples[1] = { 1, 1, 0.6f, 1.1f, 0.0f };

		const hid_t Type = H5Tcreate(H5T_COMPOUND, sizeof(FFixtureSampleMinimal));
		H5Tinsert(Type, "timestep_idx", offsetof(FFixtureSampleMinimal, TimestepIdx), H5T_NATIVE_INT32);
		H5Tinsert(Type, "entity_id", offsetof(FFixtureSampleMinimal, EntityId), H5T_NATIVE_INT32);
		H5Tinsert(Type, "position_x", offsetof(FFixtureSampleMinimal, PosX), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "position_y", offsetof(FFixtureSampleMinimal, PosY), H5T_NATIVE_FLOAT);
		H5Tinsert(Type, "position_z", offsetof(FFixtureSampleMinimal, PosZ), H5T_NATIVE_FLOAT);
		WriteDataset(File, "/simulation/samples", Type, Samples);
		H5Tclose(Type);
		H5Fclose(File);
		return true;
	}

	bool WriteJuelichFixture(const FString& Path)
	{
		FTCHARToUTF8 PathUtf8(*Path);
		const hid_t File = H5Fcreate(PathUtf8.Get(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
		if (File < 0)
		{
			return false;
		}

		WriteStringAttr(File, "wkt_geometry", "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))");
		const double Fps = 25.0;
		WriteScalarAttr(File, "fps", H5T_NATIVE_DOUBLE, &Fps);
		WriteStringAttr(File, "run_name", "matrix_fixture");

		// 2 agents x 3 frames.
		TArray<FFixtureTrajectory> Records;
		for (int64 Frame = 0; Frame < 3; ++Frame)
		{
			Records.Add({ 1, Frame, 0.5 + 0.1 * Frame, 1.0, 0.0 });
			Records.Add({ 2, Frame, 2.5 - 0.1 * Frame, 3.0, 0.0 });
		}

		const hid_t Type = H5Tcreate(H5T_COMPOUND, sizeof(FFixtureTrajectory));
		H5Tinsert(Type, "id", offsetof(FFixtureTrajectory, Id), H5T_NATIVE_INT64);
		H5Tinsert(Type, "frame", offsetof(FFixtureTrajectory, Frame), H5T_NATIVE_INT64);
		H5Tinsert(Type, "x", offsetof(FFixtureTrajectory, X), H5T_NATIVE_DOUBLE);
		H5Tinsert(Type, "y", offsetof(FFixtureTrajectory, Y), H5T_NATIVE_DOUBLE);
		H5Tinsert(Type, "z", offsetof(FFixtureTrajectory, Z), H5T_NATIVE_DOUBLE);
		WriteDataset(File, "/trajectory", Type, Records);
		H5Tclose(Type);
		H5Fclose(File);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHdf5ImportMatrixTest,
	"ProjectMobius.SimData.Hdf5ImportMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHdf5ImportMatrixTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Dir = Hdf5TestDir();
	FileManager.MakeDirectory(*Dir, /*Tree*/ true);
	H5open();

	// ---------- Mobius full fixture ----------
	const FString MobiusPath = FPaths::Combine(Dir, TEXT("MobiusFull.h5"));
	TestTrue(TEXT("mobius fixture written"), WriteMobiusFixtureFull(MobiusPath));
	TestEqual(TEXT("mobius format detected"),
		static_cast<int32>(FMobiusAgentDataImporter::DetectFileFormat(MobiusPath)),
		static_cast<int32>(EMobiusAgentFileFormat::MobiusHdf5));

	FMobiusAgentSimulationData MobiusData;
	FString Error;
	TestTrue(FString::Printf(TEXT("mobius import (%s)"), *Error),
		FMobiusAgentDataImporter::ImportAgentFile(MobiusPath, MobiusData, &Error));
	TestEqual(TEXT("mobius SourceFormat"), static_cast<int32>(MobiusData.SourceFormat), static_cast<int32>(EMobiusAgentFileFormat::MobiusHdf5));
	TestEqual(TEXT("mobius entity count"), MobiusData.Entities.Num(), 2);
	TestEqual(TEXT("mobius sample count"), MobiusData.Samples.Num(), 5);
	TestTrue(TEXT("mobius rotation flag"), MobiusData.Metadata.bHasRotationData);
	TestTrue(TEXT("mobius speed flag"), MobiusData.Metadata.bHasSpeedData);
	TestEqual(TEXT("mobius MaxNumEntities"), MobiusData.Metadata.MaxNumEntities, 2);
	TestEqual(TEXT("mobius duration"), MobiusData.Metadata.Duration, 0.2f, 1e-4f);
	TestEqual(TEXT("mobius sampling rate"), MobiusData.Metadata.SamplingRate, 0.1f, 1e-4f);
	if (MobiusData.Entities.Num() == 2)
	{
		TestEqual(TEXT("mobius entity[0].Id"), MobiusData.Entities[0].Id, 1);
		TestEqual(TEXT("mobius entity[0].Name"), MobiusData.Entities[0].Name, FString(TEXT("h5_agent_one")));
		TestEqual(TEXT("mobius entity[0].MPlane"), MobiusData.Entities[0].MPlane, FString(TEXT("floor_0")));
		TestEqual(TEXT("mobius entity[0].Map"), MobiusData.Entities[0].Map, 2);
		TestEqual(TEXT("mobius entity[1].Id"), MobiusData.Entities[1].Id, 2);
	}
	if (MobiusData.Samples.Num() == 5)
	{
		TestEqual(TEXT("mobius sample[0].EntityId"), MobiusData.Samples[0].EntityId, 1);
		TestEqual(TEXT("mobius sample[0].Rotation"), MobiusData.Samples[0].Rotation, 90.0f, 1e-4f);
		TestEqual(TEXT("mobius sample[0].Speed"), MobiusData.Samples[0].Speed, 1.25f, 1e-4f);
		TestEqual(TEXT("mobius sample[0].Mode"), MobiusData.Samples[0].Mode, FString(TEXT("walk")));
		TestEqual(TEXT("mobius sample[1].Mode"), MobiusData.Samples[1].Mode, FString(TEXT("run")));
		TestEqual(TEXT("mobius sample[4].TimestepIndex"), MobiusData.Samples[4].TimestepIndex, 2);
	}

	// ---------- Mobius minimal fixture (no rotation/speed/mode members) ----------
	const FString MinimalPath = FPaths::Combine(Dir, TEXT("MobiusMinimal.h5"));
	TestTrue(TEXT("minimal fixture written"), WriteMobiusFixtureMinimal(MinimalPath));

	FMobiusAgentSimulationData MinimalData;
	TestTrue(TEXT("minimal import"), FMobiusAgentDataImporter::ImportAgentFile(MinimalPath, MinimalData, &Error));
	TestFalse(TEXT("minimal rotation flag off (must be derived post-load)"), MinimalData.Metadata.bHasRotationData);
	TestFalse(TEXT("minimal speed flag off (must be derived post-load)"), MinimalData.Metadata.bHasSpeedData);
	TestEqual(TEXT("minimal sample count"), MinimalData.Samples.Num(), 2);
	if (MinimalData.Samples.Num() == 2)
	{
		TestEqual(TEXT("minimal mode falls back to walk"), MinimalData.Samples[0].Mode, FString(TEXT("walk")));
		TestEqual(TEXT("minimal rotation zeroed"), MinimalData.Samples[0].Rotation, 0.0f, 1e-6f);
	}

	// ---------- Juelich fixture ----------
	const FString JuelichPath = FPaths::Combine(Dir, TEXT("Juelich.h5"));
	TestTrue(TEXT("juelich fixture written"), WriteJuelichFixture(JuelichPath));
	TestEqual(TEXT("juelich format detected"),
		static_cast<int32>(FMobiusAgentDataImporter::DetectFileFormat(JuelichPath)),
		static_cast<int32>(EMobiusAgentFileFormat::JuelichHdf5));

	FMobiusAgentSimulationData JuelichData;
	TestTrue(FString::Printf(TEXT("juelich import (%s)"), *Error),
		FMobiusAgentDataImporter::ImportAgentFile(JuelichPath, JuelichData, &Error));
	TestEqual(TEXT("juelich SourceFormat"), static_cast<int32>(JuelichData.SourceFormat), static_cast<int32>(EMobiusAgentFileFormat::JuelichHdf5));
	TestEqual(TEXT("juelich entities from unique ids"), JuelichData.Entities.Num(), 2);
	TestEqual(TEXT("juelich samples from trajectory rows"), JuelichData.Samples.Num(), 6);
	TestFalse(TEXT("juelich rotation flag off"), JuelichData.Metadata.bHasRotationData);
	TestFalse(TEXT("juelich speed flag off"), JuelichData.Metadata.bHasSpeedData);

	// ---------- Error paths ----------
	FMobiusAgentSimulationData Unused;
	FString PathError;
	TestFalse(TEXT("missing file fails"),
		FMobiusAgentDataImporter::ImportAgentFile(FPaths::Combine(Dir, TEXT("DoesNotExist.h5")), Unused, &PathError));
	TestFalse(TEXT("missing file error populated"), PathError.IsEmpty());

	const FString GarbageH5 = FPaths::Combine(Dir, TEXT("Garbage.h5"));
	FFileHelper::SaveStringToFile(TEXT("this is not an hdf5 file"), *GarbageH5);
	// The reader legitimately logs an Error for the unreadable file — expected, not a failure.
	AddExpectedError(TEXT("Failed to open HDF5 file"), EAutomationExpectedErrorFlags::Contains, /*Occurrences*/ 1);
	TestEqual(TEXT("garbage .h5 detected as Unknown"),
		static_cast<int32>(FMobiusAgentDataImporter::DetectFileFormat(GarbageH5)),
		static_cast<int32>(EMobiusAgentFileFormat::Unknown));
	TestFalse(TEXT("garbage .h5 import fails cleanly"),
		FMobiusAgentDataImporter::ImportAgentFile(GarbageH5, Unused, &PathError));

	const FString WrongExt = FPaths::Combine(Dir, TEXT("WrongExtension.txt"));
	FFileHelper::SaveStringToFile(TEXT("{}"), *WrongExt);
	TestFalse(TEXT("unsupported extension fails cleanly"),
		FMobiusAgentDataImporter::ImportAgentFile(WrongExt, Unused, &PathError));

	const FString EmptyJson = FPaths::Combine(Dir, TEXT("Empty.json"));
	FFileHelper::SaveStringToFile(TEXT(""), *EmptyJson);
	AddExpectedMessage(TEXT("simdjson JSON parse failed"), EAutomationExpectedMessageFlags::Contains, /*Occurrences*/ 1, /*IsRegex*/ false);
	TestFalse(TEXT("empty .json fails cleanly"),
		FMobiusAgentDataImporter::ImportAgentFile(EmptyJson, Unused, &PathError));

	// ---------- Local-only large-file smoke (skips when absent) ----------
	// Private fixture, resolved via MobiusTestDataRoots.h rather than a hardcoded drive letter.
	const FString LargeFixture = FPaths::Combine(
		TEXT("TechSchoolTest"), TEXT("TechnicalSchool5000DefaultExits.json"));
	const FString LargeFile = MobiusTestData::FindInternalFixture(LargeFixture);
	if (!LargeFile.IsEmpty() && FileManager.FileExists(*LargeFile))
	{
		FMobiusAgentSimulationData LargeData;
		TestTrue(TEXT("large-file smoke import"), FMobiusAgentDataImporter::ImportAgentFile(LargeFile, LargeData, &Error));
		TestTrue(TEXT("large-file smoke entities"), LargeData.Entities.Num() > 0);
		TestTrue(TEXT("large-file smoke samples"), LargeData.Samples.Num() > 0);
	}
	else
	{
		AddInfo(MobiusTestData::DescribeMissingFixture(LargeFixture));
	}

	FileManager.DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

#endif // !UE_BUILD_SHIPPING
