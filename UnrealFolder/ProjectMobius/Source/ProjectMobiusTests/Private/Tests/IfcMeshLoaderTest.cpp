// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#if !UE_BUILD_SHIPPING

// IFC import is wired for Win64 (MobiusIfcBridge.dll) and macOS (libMobiusIfcBridge.dylib); on any
// other platform MobiusCore compiles the loader as a stub (MOBIUS_WITH_IFC_BRIDGE=0) that returns a
// "not available" error, so there is nothing to assert. Compile the test only where the bridge exists.
#if PLATFORM_WINDOWS || PLATFORM_MAC

#include "Ifc/MobiusIfcMeshLoader.h"
#include "AsyncAssimpMeshLoader.h" // FAssimpSubmeshBuffers (complete type for the out-array)
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
	/** The committed sample under <Project>/UnitTestSampleData/Ifc/. */
	FString SampleIfcPath()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("UnitTestSampleData"),
			TEXT("Ifc"), TEXT("WallWithOpening_IFC4.ifc"));
	}
}

// End-to-end guard for the IFC path: proves the shim shared library loads, the ABI matches, and a
// real IFC4 file parses + converts to renderable geometry. This is the automated form of the manual
// dylib harness used when bringing IFC up on macOS; it now guards Win64 and macOS against regressions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusIfcMeshLoaderLoadsSampleTest,
	"ProjectMobius.Ifc.MeshLoader.LoadsSampleFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMobiusIfcMeshLoaderLoadsSampleTest::RunTest(const FString&)
{
	const FString Path = SampleIfcPath();

	// The sample ships in the repo; without it the test asserts nothing, so fail loudly rather than
	// silently pass.
	if (!TestTrue(FString::Printf(TEXT("sample IFC exists at %s"), *Path), FPaths::FileExists(Path)))
	{
		return false;
	}

	// Schema is read straight from the STEP header text (never from IFC++'s own reporter -- see the
	// ReadFileSchema comment).
	FString Schema, SchemaError;
	TestTrue(TEXT("ReadFileSchema succeeds"), FMobiusIfcMeshLoader::ReadFileSchema(Path, Schema, SchemaError));
	TestEqual(TEXT("header schema is IFC4"), Schema, FString(TEXT("IFC4")));

	// The bridge shared library must load and match the ABI this build was compiled against.
	FString BridgeError;
	if (!TestTrue(FString::Printf(TEXT("EnsureBridgeLoaded succeeds (%s)"), *BridgeError),
			FMobiusIfcMeshLoader::EnsureBridgeLoaded(BridgeError)))
	{
		return false;
	}

	// Full parse + geometry conversion through the shim.
	TArray<FAssimpSubmeshBuffers> Submeshes;
	FMobiusIfcLoadStats Stats;
	FString Error;
	const bool bLoaded = FMobiusIfcMeshLoader::LoadIfcFile(Path, Submeshes, Stats, Error);
	if (!TestTrue(FString::Printf(TEXT("LoadIfcFile succeeds (%s)"), *Error), bLoaded))
	{
		return false;
	}

	// The wall survives the render allowlist and carries triangles. (The IfcOpeningElement volume is
	// dropped by the allowlist but still counted in Stats.TotalTriangles.)
	TestTrue(TEXT("at least one renderable submesh"), Submeshes.Num() >= 1);
	TestTrue(TEXT("rendered triangles > 0"), Stats.RenderedTriangles > 0);
	TestTrue(TEXT("total triangles include the opening volume too"),
		Stats.TotalTriangles >= Stats.RenderedTriangles);
	TestEqual(TEXT("stats record the source schema"), Stats.SourceSchema, FString(TEXT("IFC4")));

	return true;
}

#endif // PLATFORM_WINDOWS || PLATFORM_MAC

#endif // !UE_BUILD_SHIPPING
