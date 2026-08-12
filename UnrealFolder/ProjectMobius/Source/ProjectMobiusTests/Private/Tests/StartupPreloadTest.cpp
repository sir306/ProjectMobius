// Copyright (c) 2026 ProjectMobius contributors. Licensed under MIT.
//
// StartupPreloadTest.cpp
//
// Gates the pure, world-free half of UMobiusStartupPreloadSubsystem: the -Mobius* command-line parser
// and the per-slot path validation that decides whether a third-party launcher's argument is allowed
// anywhere near an importer.
//
// Why only that half. The gate itself (legal-notice acceptance, world HasBegunPlay, per-slot
// delegate IsBound) is timing against a live packaged startup, and its inputs - GIsEditor,
// GEngine->GetGameUserSettings(), a level-placed ARuntimeMeshBuilder - cannot be posed truthfully
// from an automation test running inside an editor process where GIsEditor is already true and the
// legal gate is unconditionally open. Faking them would gate the fake. The gate is verified by the
// packaged launch recipe in Scripts/Launch-Mobius.ps1 instead.
//
// What IS covered here is the part that is both pure and load-bearing:
//   - ParseCommandLine: independence of the three tokens (the headline requirement - any subset must
//     work), quoted paths containing spaces, and the timeout override.
//   - IsExtensionSupportedForSlot: the full accept matrix, case-insensitivity, and cross-slot
//     rejection - a .smv must not be accepted as geometry just because a launcher swapped two args.
//   - ValidatePathForSlot: blank, the "Click Browse to choose file" placeholder echoed back by a
//     launcher, wrong extension, missing file, a DIRECTORY (which passes a naive existence check and
//     then dies deep inside an importer), and a real file on disk.
//
// Run from the Session Frontend (search "ProjectMobius.Startup.Preload") or:
//   MobiusPerf\RunTests.ps1
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/MobiusStartupPreloadSubsystem.h"

namespace MobiusStartupPreloadTest
{
	/** Scratch directory under Saved/, created and removed by the fixture tests. */
	FString ScratchDir()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusStartupPreloadTest")));
	}

	/** Write a zero-content file with the given name and return its absolute path. */
	FString MakeScratchFile(const FString& FileName)
	{
		const FString FullPath = FPaths::Combine(ScratchDir(), FileName);
		IFileManager::Get().MakeDirectory(*ScratchDir(), /*Tree=*/true);
		FFileHelper::SaveStringToFile(TEXT("placeholder"), *FullPath);
		return FullPath;
	}

	/** TestEqual cannot print a bare enum; compare the underlying values instead. */
	template <typename TEnum>
	int32 AsInt(TEnum Value)
	{
		return static_cast<int32>(Value);
	}

	void RemoveScratch()
	{
		IFileManager::Get().DeleteDirectory(*ScratchDir(), /*RequireExists=*/false, /*Tree=*/true);
	}
}

// -------------------------------------------------------------------------------------------------
// Command-line parsing
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobiusPreloadParseIndependenceTest,
	"ProjectMobius.Startup.Preload.ParseIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobiusPreloadParseIndependenceTest::RunTest(const FString& /*Parameters*/)
{
	using FParsed = FMobiusPreloadCommandLine;

	// Nothing supplied.
	{
		const FParsed Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("ProjectMobius.exe -windowed -ResX=1920"));
		TestFalse(TEXT("bare command line reports no paths"), Parsed.HasAnyPath());
		TestTrue(TEXT("geometry empty"), Parsed.GeometryPath.IsEmpty());
		TestTrue(TEXT("pedestrian empty"), Parsed.PedestrianPath.IsEmpty());
		TestTrue(TEXT("brisk empty"), Parsed.BRiskPath.IsEmpty());
	}

	// Each token ALONE must work and must not contaminate the other two. This is the requirement the
	// whole feature is specified around: geometry, pedestrian and B-Risk are independent inputs.
	{
		const FParsed Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusGeometry=D:/data/build.fbx"));
		TestTrue(TEXT("geometry-only reports a path"), Parsed.HasAnyPath());
		TestEqual(TEXT("geometry-only geometry value"), Parsed.GeometryPath, FString(TEXT("D:/data/build.fbx")));
		TestTrue(TEXT("geometry-only leaves pedestrian empty"), Parsed.PedestrianPath.IsEmpty());
		TestTrue(TEXT("geometry-only leaves brisk empty"), Parsed.BRiskPath.IsEmpty());
	}
	{
		const FParsed Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusPedestrian=D:/data/peds.json"));
		TestEqual(TEXT("pedestrian-only pedestrian value"), Parsed.PedestrianPath, FString(TEXT("D:/data/peds.json")));
		TestTrue(TEXT("pedestrian-only leaves geometry empty"), Parsed.GeometryPath.IsEmpty());
		TestTrue(TEXT("pedestrian-only leaves brisk empty"), Parsed.BRiskPath.IsEmpty());
	}
	{
		const FParsed Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusBRisk=D:/data/case.smv"));
		TestEqual(TEXT("brisk-only brisk value"), Parsed.BRiskPath, FString(TEXT("D:/data/case.smv")));
		TestTrue(TEXT("brisk-only leaves geometry empty"), Parsed.GeometryPath.IsEmpty());
		TestTrue(TEXT("brisk-only leaves pedestrian empty"), Parsed.PedestrianPath.IsEmpty());
	}

	// Two of three, in an order that does not match the parse order.
	{
		const FParsed Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusBRisk=D:/data/case.smv -MobiusGeometry=D:/data/build.obj"));
		TestEqual(TEXT("pair brisk value"), Parsed.BRiskPath, FString(TEXT("D:/data/case.smv")));
		TestEqual(TEXT("pair geometry value"), Parsed.GeometryPath, FString(TEXT("D:/data/build.obj")));
		TestTrue(TEXT("pair leaves pedestrian empty"), Parsed.PedestrianPath.IsEmpty());
	}

	// All three, interleaved with unrelated engine arguments.
	{
		const FParsed Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-windowed -MobiusGeometry=D:/data/build.fbx -ResX=1920 ")
			TEXT("-MobiusPedestrian=D:/data/peds.h5 -MobiusBRisk=D:/data/case.smv -log"));
		TestEqual(TEXT("triple geometry"), Parsed.GeometryPath, FString(TEXT("D:/data/build.fbx")));
		TestEqual(TEXT("triple pedestrian"), Parsed.PedestrianPath, FString(TEXT("D:/data/peds.h5")));
		TestEqual(TEXT("triple brisk"), Parsed.BRiskPath, FString(TEXT("D:/data/case.smv")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobiusPreloadParseQuotingTest,
	"ProjectMobius.Startup.Preload.ParseQuoting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobiusPreloadParseQuotingTest::RunTest(const FString& /*Parameters*/)
{
	// Real datasets sit in folders with spaces, so a launcher must be able to quote. FParse::Value
	// consumes the quotes and returns the inner text; if that ever changes, every launcher breaks, so
	// it is asserted rather than assumed.
	{
		const FMobiusPreloadCommandLine Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusGeometry=\"D:\\Sim Data\\Tech School\\building.fbx\" -windowed"));
		TestEqual(TEXT("quoted path with spaces survives"),
			Parsed.GeometryPath, FString(TEXT("D:\\Sim Data\\Tech School\\building.fbx")));
	}

	// A quoted path followed by another quoted path: the first must not swallow the second.
	{
		const FMobiusPreloadCommandLine Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusGeometry=\"D:\\Sim Data\\a.fbx\" -MobiusPedestrian=\"D:\\Sim Data\\b.json\""));
		TestEqual(TEXT("first quoted path"), Parsed.GeometryPath, FString(TEXT("D:\\Sim Data\\a.fbx")));
		TestEqual(TEXT("second quoted path"), Parsed.PedestrianPath, FString(TEXT("D:\\Sim Data\\b.json")));
	}

	// Timeout override, and its absence.
	{
		const FMobiusPreloadCommandLine WithTimeout = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusBRisk=D:/data/case.smv -MobiusPreloadTimeout=12.5"));
		TestEqual(TEXT("timeout override parsed"), WithTimeout.ReadinessTimeoutSeconds, 12.5f, 0.001f);

		const FMobiusPreloadCommandLine NoTimeout = UMobiusStartupPreloadSubsystem::ParseCommandLine(
			TEXT("-MobiusBRisk=D:/data/case.smv"));
		TestEqual(TEXT("timeout defaults to 0 (meaning: use the cvar)"),
			NoTimeout.ReadinessTimeoutSeconds, 0.0f, 0.001f);
	}

	// A null command line must not crash the parser - Initialize() hands it FCommandLine::Get(),
	// which is never null, but RequestLoad's callers are not so constrained.
	{
		const FMobiusPreloadCommandLine Parsed = UMobiusStartupPreloadSubsystem::ParseCommandLine(nullptr);
		TestFalse(TEXT("null command line yields nothing"), Parsed.HasAnyPath());
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// Extension whitelist
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobiusPreloadExtensionMatrixTest,
	"ProjectMobius.Startup.Preload.ExtensionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobiusPreloadExtensionMatrixTest::RunTest(const FString& /*Parameters*/)
{
	using FSubsystem = UMobiusStartupPreloadSubsystem;

	// Geometry: the list UNativeFileDialogSubsystem::HandleDialogResult accepts for a mesh.
	for (const TCHAR* Ext : { TEXT("fbx"), TEXT("obj"), TEXT("udatasmith"), TEXT("ifc"), TEXT("wkt"), TEXT("h5") })
	{
		const FString Path = FString::Printf(TEXT("C:/x/model.%s"), Ext);
		TestTrue(FString::Printf(TEXT("geometry accepts .%s"), Ext),
			FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Geometry, Path));
	}

	// Pedestrian: json/h5 only. .txt is deliberately refused even though HandleDialogResult's
	// bAgentSuccess check would let it through - a launch argument is not a filtered dialog, and the
	// dialog's own filter is "*.json *.h5".
	TestTrue(TEXT("pedestrian accepts .json"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Pedestrian, TEXT("C:/x/peds.json")));
	TestTrue(TEXT("pedestrian accepts .h5"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Pedestrian, TEXT("C:/x/peds.h5")));
	TestFalse(TEXT("pedestrian refuses .txt"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Pedestrian, TEXT("C:/x/peds.txt")));

	// B-Risk: .smv only.
	TestTrue(TEXT("brisk accepts .smv"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::BRisk, TEXT("C:/x/case.smv")));

	// Case-insensitivity: launchers and Windows both hand back mixed case.
	TestTrue(TEXT("geometry accepts .FBX"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Geometry, TEXT("C:/x/model.FBX")));
	TestTrue(TEXT("brisk accepts .SMV"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::BRisk, TEXT("C:/x/case.SMV")));

	// Cross-slot rejection. Two arguments swapped by the calling application must be refused, not
	// silently pushed into the wrong importer.
	TestFalse(TEXT("geometry refuses .smv"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Geometry, TEXT("C:/x/case.smv")));
	TestFalse(TEXT("geometry refuses .json"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Geometry, TEXT("C:/x/peds.json")));
	TestFalse(TEXT("pedestrian refuses .fbx"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Pedestrian, TEXT("C:/x/model.fbx")));
	TestFalse(TEXT("brisk refuses .json"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::BRisk, TEXT("C:/x/peds.json")));

	// .h5 is the one extension shared between geometry and pedestrian, so it must pass for both and
	// still be refused by B-Risk.
	TestTrue(TEXT("h5 valid for geometry"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Geometry, TEXT("C:/x/sim.h5")));
	TestTrue(TEXT("h5 valid for pedestrian"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Pedestrian, TEXT("C:/x/sim.h5")));
	TestFalse(TEXT("h5 invalid for brisk"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::BRisk, TEXT("C:/x/sim.h5")));

	// No extension at all.
	TestFalse(TEXT("extensionless path refused"),
		FSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot::Geometry, TEXT("C:/x/model")));

	// Every slot must publish a non-empty supported-extension string, since it is shown to the user
	// in the rejection message.
	for (int32 Index = 0; Index < UMobiusStartupPreloadSubsystem::NumSlots; ++Index)
	{
		const EMobiusPreloadSlot Slot = static_cast<EMobiusPreloadSlot>(Index);
		TestFalse(FString::Printf(TEXT("slot %s publishes supported extensions"), FSubsystem::GetSlotName(Slot)),
			FSubsystem::GetSupportedExtensionsText(Slot).IsEmpty());
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// Full validation, including disk state
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobiusPreloadValidationTest,
	"ProjectMobius.Startup.Preload.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobiusPreloadValidationTest::RunTest(const FString& /*Parameters*/)
{
	using FSubsystem = UMobiusStartupPreloadSubsystem;
	using MobiusStartupPreloadTest::MakeScratchFile;
	using MobiusStartupPreloadTest::ScratchDir;
	using MobiusStartupPreloadTest::RemoveScratch;
	using MobiusStartupPreloadTest::AsInt;

	RemoveScratch();

	// Blank and whitespace-only.
	// TestEqual has no printer for a bare enum, so every comparison below goes through int32 - the
	// same pattern BRiskDataImporterTest uses for EBRiskVentKind.
	TestEqual(TEXT("empty string is EmptyPath"),
		AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, FString())),
		AsInt(EMobiusPreloadValidation::EmptyPath));
	TestEqual(TEXT("whitespace-only is EmptyPath"),
		AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, TEXT("   "))),
		AsInt(EMobiusPreloadValidation::EmptyPath));

	// The game instance's own placeholder. A launcher that reads Mobius's displayed field and passes
	// it straight back must be treated as "nothing supplied", not as a filename.
	TestEqual(TEXT("placeholder text is EmptyPath"),
		AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Pedestrian, TEXT("Click Browse to choose file"))),
		AsInt(EMobiusPreloadValidation::EmptyPath));

	// Wrong extension is reported as a type problem even when the file does not exist - a clearer
	// message than "not found" for the common case of two swapped arguments.
	TestEqual(TEXT("wrong extension beats not-found"),
		AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, TEXT("C:/does/not/exist/case.smv"))),
		AsInt(EMobiusPreloadValidation::UnsupportedExtension));

	// Right extension, nothing on disk.
	TestEqual(TEXT("missing file is NotAFile"),
		AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry,
			FPaths::Combine(ScratchDir(), TEXT("absent.fbx")))),
		AsInt(EMobiusPreloadValidation::NotAFile));

	// A real file passes, for every slot.
	{
		const FString Fbx = MakeScratchFile(TEXT("building.fbx"));
		const FString Json = MakeScratchFile(TEXT("peds.json"));
		const FString Smv = MakeScratchFile(TEXT("case.smv"));

		TestEqual(TEXT("real .fbx validates for geometry"),
			AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, Fbx)), AsInt(EMobiusPreloadValidation::Ok));
		TestEqual(TEXT("real .json validates for pedestrian"),
			AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Pedestrian, Json)), AsInt(EMobiusPreloadValidation::Ok));
		TestEqual(TEXT("real .smv validates for brisk"),
			AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::BRisk, Smv)), AsInt(EMobiusPreloadValidation::Ok));

		// Surrounding whitespace is tolerated, because a launcher building a command line by string
		// concatenation frequently leaves it behind.
		TestEqual(TEXT("padded real path validates"),
			AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, FString::Printf(TEXT("  %s "), *Fbx))),
			AsInt(EMobiusPreloadValidation::Ok));

		// Same file, wrong slot.
		TestEqual(TEXT("real .smv refused as geometry"),
			AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, Smv)),
			AsInt(EMobiusPreloadValidation::UnsupportedExtension));
	}

	// A DIRECTORY named like a valid file. This is the case a plain FileExists check lets through on
	// some platforms, and a directory handed to Assimp / the B-Risk importer fails a long way from
	// here with a far worse message.
	{
		const FString FakeDir = FPaths::Combine(ScratchDir(), TEXT("model.fbx.d"), TEXT("nested.fbx"));
		IFileManager::Get().MakeDirectory(*FakeDir, /*Tree=*/true);
		TestEqual(TEXT("directory with a valid extension is NotAFile"),
			AsInt(FSubsystem::ValidatePathForSlot(EMobiusPreloadSlot::Geometry, FakeDir)),
			AsInt(EMobiusPreloadValidation::NotAFile));
	}

	RemoveScratch();
	return true;
}

#endif // !UE_BUILD_SHIPPING
