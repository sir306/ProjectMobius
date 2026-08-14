// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

// ============================================================================================
// End-to-end .ifc load in a REAL GAME WORLD, through the production path.
//
// ProjectMobius.Ifc.Import.* (IfcImportTest.cpp) tests FMobiusIfcMeshLoader directly: right
// triangles, right volumes, right classes dropped. It says nothing about the WIRING -- whether an
// .ifc chosen in the app actually reaches a UProceduralMeshComponent. That wiring is exactly what was
// broken: RuntimeMeshBuilder routed .ifc into the DatasmithRuntime branch, built an import options
// struct, and returned without calling LoadFile. Seven places advertised .ifc support and nothing
// ever drew. A loader-level test would have passed against that bug all day.
//
// So these tests drive the same two calls the Browse button makes (SetSimulationMeshFilePath +
// SetSimulationMeshFileName on the game instance) and then assert on the ProcMesh component:
//   - sections exist, and their triangle total equals what the IFC loader reported it emitted
//   - the per-section IFC provenance array is index-parallel to the sections actually created
//   - bIsDatasmithAsset is FALSE (i.e. the Datasmith branch really was left behind)
//   - LastIfcLoadStats.SourceSchema is the schema read from the file header, proving the IFC path ran
//   - the aggregate section bounds match the harness's world AABB, converted to UE centimetres
//
// The bounds assertion is the "does it render correctly" check that does not need eyes: 9.4 x 6.4 x
// 4.05 m in IFC space must arrive as 940 x 640 x 405 cm in UE space. A missing x100, a metres/
// millimetres slip, or geometry collapsing to a point all fail here.
//
// The screenshots are the check that DOES need eyes -- specifically "no solid block fills a door, a
// window or a room", which is what a working renderable-class allowlist looks like. They are written
// unlit and wireframe with fog off, because the level's exponential height fog washes a lit shot of a
// 9 m building into a grey smudge (measured: the first version of this test produced exactly that and
// proved nothing). Unlit removes the lighting variable; wireframe makes an opening unmistakable.
//
// Run: MobiusPerf\RunTests.ps1 -InGame -Rendered
//      (-Rendered is required for the screenshots; they self-skip under the default -nullrhi)
// ============================================================================================

#if !UE_BUILD_SHIPPING

#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "Camera/CameraActor.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Ifc/MobiusIfcRenderableClasses.h" // bMobiusIfcRenderAnnotationClasses (owner policy constant)
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h" // FScreenshotRequest lives here in 5.5, not in a Misc/ScreenshotRequest.h
#include "MobiusTestDataRoots.h"

namespace IfcInGame
{
	/** Shared between the load command and the screenshot command that follows it. */
	struct FIfcInGameState
	{
		/** Absolute path of the fixture under test. */
		FString FixturePath;

		/** Short tag used in screenshot filenames, e.g. "Ifc2x3". */
		FString Tag;

		/** Bounds the load actually produced, in UE cm. Invalid if the load failed. */
		FBox Bounds = FBox(ForceInit);
	};

	using FIfcStatePtr = TSharedPtr<FIfcInGameState, ESPMode::ThreadSafe>;

	static UWorld* GetActiveGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	static ARuntimeMeshBuilder* FindMeshBuilder(UWorld* World)
	{
		return World ? Cast<ARuntimeMeshBuilder>(
			UGameplayStatics::GetActorOfClass(World, ARuntimeMeshBuilder::StaticClass())) : nullptr;
	}

	/** Aggregate local bounds + triangle/section totals across every emitted ProcMesh section. */
	static bool GetMeshTotals(ARuntimeMeshBuilder* Builder, FBox& OutBounds, int32& OutSections, int32& OutTris)
	{
		OutBounds = FBox(ForceInit);
		OutSections = 0;
		OutTris = 0;

		if (!Builder || !Builder->MobiusProceduralMeshComponent)
		{
			return false;
		}

		OutSections = Builder->MobiusProceduralMeshComponent->GetNumSections();
		for (int32 i = 0; i < OutSections; ++i)
		{
			if (const FProcMeshSection* Section = Builder->MobiusProceduralMeshComponent->GetProcMeshSection(i))
			{
				OutBounds += Section->SectionLocalBox;
				OutTris += Section->ProcIndexBuffer.Num() / 3;
			}
		}
		return OutSections > 0 && OutBounds.IsValid != 0;
	}

	/** The IFC2X3 conformance file, committed to the repo (repo root is two levels above ProjectDir). */
	static FString Ifc2x3FixturePath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("../.."), TEXT("TestData"), TEXT("ISO-Test-1-2x3.ifc")));
	}

	/**
	 * The IFC4X3_ADD2 export, which lives in Mobius_InternalData OUTSIDE the repo (large models are not
	 * committed). Returns empty when it is not on this machine, and the caller skips loudly.
	 */
	static FString Ifc4x3FixturePath()
	{
		const FString Relative = FPaths::Combine(
			TEXT("12 RoomTest"), TEXT("Exported-model"), TEXT("ISO-Test-8-FireSmoke.ifc"));

		// Roots come from MobiusTestDataRoots.h. This used to list absolute drive paths, which
		// worked on one machine and published its layout. Set MOBIUS_INTERNAL_DATA if your copy
		// of the private datasets lives somewhere the relative roots do not cover.
		return MobiusTestData::FindInternalFixture(Relative);
	}

	/**
	 * Kicks the production load and waits for the building to be fully emitted.
	 *
	 * Deliberately the same two setters TrajectoryHeatmapInGameTest's FLoadPlanCommand uses -- that is
	 * what ULoadMeshWidget calls after a Browse selection, and going through
	 * IProjectMobiusInterface::UpdateMobiusGameInstanceMeshDataFile is not available to a test (the
	 * interface method is not static).
	 */
	class FLoadIfcCommand : public IAutomationLatentCommand
	{
	public:
		FLoadIfcCommand(FAutomationTestBase& InTest, FIfcStatePtr InState, const FString& InExpectedSchema,
		                int32 InExpectedFileTriangles, int32 InExpectedRenderedProducts,
		                int32 InExpectedRoomVolumes, double InTimeoutSeconds)
			: Test(InTest), State(InState), ExpectedSchema(InExpectedSchema)
			, ExpectedFileTriangles(InExpectedFileTriangles), ExpectedRenderedProducts(InExpectedRenderedProducts)
			, ExpectedRoomVolumes(InExpectedRoomVolumes), TimeoutSeconds(InTimeoutSeconds) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("no game world - run with -game (MobiusPerf\\RunTests.ps1 -InGame)"));
				return true;
			}

			if (!bKicked)
			{
				UProjectMobiusGameInstance* GameInstance = World->GetGameInstance<UProjectMobiusGameInstance>();
				if (!GameInstance)
				{
					Test.AddError(TEXT("no UProjectMobiusGameInstance - cannot load the IFC file"));
					return true;
				}

				GameInstance->SetSimulationMeshFilePath(State->FixturePath);
				GameInstance->SetSimulationMeshFileName(FPaths::GetCleanFilename(State->FixturePath));
				Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
				bKicked = true;
				return false;
			}

			FBox Bounds(ForceInit);
			int32 Sections = 0;
			int32 Tris = 0;
			ARuntimeMeshBuilder* Builder = FindMeshBuilder(World);

			// Wait for the STAGGERED EMIT to finish, not just for the first section to appear.
			// ARuntimeMeshBuilder pushes SectionsEmittedPerTick sections per frame, so "sections > 0"
			// is true one frame in with a single small product present. IsMeshBeingBuilt() stays true
			// from AsyncUpdateMesh through FinalizeMeshEmit and is the only correct gate. Measured on
			// 2026-08-12: polling on "sections > 0" reported 1 section / 88 tris / 135 x 4 x 50 cm out
			// of a 37-section / 3008-triangle / 940 x 640 x 405 cm building.
			const bool bEmitFinished = Builder && !Builder->IsMeshBeingBuilt();

			if (bEmitFinished && GetMeshTotals(Builder, Bounds, Sections, Tris) && Tris > 0)
			{
				const FMobiusIfcLoadStats& Stats = Builder->GetLastIfcLoadStats();

				Test.AddInfo(FString::Printf(
					TEXT("%s loaded into ProcMesh: sections=%d tris=%d bounds=%.0f x %.0f x %.0f cm"),
					*State->Tag, Sections, Tris,
					Bounds.GetSize().X, Bounds.GetSize().Y, Bounds.GetSize().Z));
				Test.AddInfo(FString::Printf(TEXT("loader stats: %s"), *Stats.FilterSummary));

				// ---- The wiring assertions -----------------------------------------------------
				Test.TestEqual(TEXT("schema recorded from the file header (proves the IFC path ran, not Datasmith)"),
				               Stats.SourceSchema, ExpectedSchema);
				Test.TestFalse(TEXT("bIsDatasmithAsset is false for .ifc"), Builder->IsDatasmithAsset());
				// A BAND, not an equality, and deliberately so. IFC++'s tessellation is not
				// bit-reproducible: the triangle COUNT it produces is stable (16591 for the IFC4X3
				// file over five runs), but how many of those come out geometrically degenerate is
				// not (92..95), because carve orders its boolean through pointer-keyed containers.
				// Since 2026-08-12 EmitTriangle drops the degenerate ones, so the shipped count
				// inherits that jitter. No threshold value fixes it -- verified by moving the cutoff
				// three orders of magnitude with no effect. Full measurement in IfcImportTest.cpp and
				// HANDOFF_IFC_2026-08-11.md 16.11. ExpectedFileTriangles is the TOP of the band.
				// The actual value goes in the message: TestTrue prints only "expected true", which on a
				// numeric band is undiagnosable. Losing that cost a whole re-run on 2026-08-12.
				Test.TestTrue(FString::Printf(
					TEXT("triangles in the source file within the tessellation band [%d,%d] (actual %d)"),
					ExpectedFileTriangles - 4, ExpectedFileTriangles, Stats.TotalTriangles),
					Stats.TotalTriangles >= ExpectedFileTriangles - 4
					&& Stats.TotalTriangles <= ExpectedFileTriangles);
				Test.TestEqual(TEXT("renderable products after the allowlist"),
				               Stats.RenderedProducts, ExpectedRenderedProducts);
				Test.TestEqual(TEXT("ProcMesh triangles equal the triangles the IFC loader emitted"),
				               Tris, Stats.RenderedTriangles);
				Test.TestEqual(TEXT("per-section IFC provenance is parallel to the emitted sections"),
				               Builder->GetIfcSectionInfo().Num(), Sections);
				Test.TestEqual(TEXT("IfcSpace room volumes captured for the B-RISK path"),
				               Stats.RoomVolumes.Num(), ExpectedRoomVolumes);
				Test.TestTrue(TEXT("some triangles were dropped by the allowlist (openings/spaces are volume-only)"),
				              Stats.RenderedTriangles < Stats.TotalTriangles);

				bool bAllTagged = true;
				for (const FMobiusIfcSectionInfo& Info : Builder->GetIfcSectionInfo())
				{
					if (Info.Guid.IsEmpty() || Info.IfcClass.IsEmpty())
					{
						bAllTagged = false;
						break;
					}
				}
				Test.TestTrue(TEXT("every section carries a non-empty IFC GUID and class"), bAllTagged);

				State->Bounds = Bounds;
				return true;
			}

			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(
					TEXT("timed out after %.0f s waiting for %s to reach the RuntimeMeshBuilder's procedural ")
					TEXT("mesh. Sections=%d tris=%d building=%d. Check that an ARuntimeMeshBuilder exists in the ")
					TEXT("level, that MobiusIfcBridge.dll is staged beside the executable, and that ")
					TEXT("ContinueLoadAfterPurge still routes .ifc to AsyncUpdateMesh rather than the Datasmith anchor."),
					TimeoutSeconds, *State->FixturePath, Sections, Tris,
					Builder ? static_cast<int32>(Builder->IsMeshBeingBuilt()) : -1));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		FIfcStatePtr State;
		FString ExpectedSchema;
		int32 ExpectedFileTriangles;
		int32 ExpectedRenderedProducts;
		int32 ExpectedRoomVolumes;
		double TimeoutSeconds;
		bool bKicked = false;
		double Deadline = 0.0;
	};

	/**
	 * Asserts the world-space size of the loaded building, in UE centimetres.
	 *
	 * Separate command so the load command can stay fixture-agnostic: the IFC2X3 file takes an exact
	 * check (its dropped openings are strictly inside the walls they were cut from, so the hull cannot
	 * move), while the IFC4X3 file drops 17 IfcSensor and 14 IfcSpace products whose positions inside
	 * the model are not known here -- an exact hull assertion there would be asserting a guess.
	 */
	class FCheckBoundsCommand : public IAutomationLatentCommand
	{
	public:
		FCheckBoundsCommand(FAutomationTestBase& InTest, FIfcStatePtr InState, FVector InExpectedCm, double InToleranceCm)
			: Test(InTest), State(InState), ExpectedCm(InExpectedCm), ToleranceCm(InToleranceCm) {}

		virtual bool Update() override
		{
			if (State->Bounds.IsValid == 0)
			{
				// The load command already reported why; a second error for one cause reads like two bugs.
				return true;
			}
			const FVector Size = State->Bounds.GetSize();
			Test.TestNearlyEqual(TEXT("bounds X cm"), Size.X, ExpectedCm.X, ToleranceCm);
			Test.TestNearlyEqual(TEXT("bounds Y cm"), Size.Y, ExpectedCm.Y, ToleranceCm);
			Test.TestNearlyEqual(TEXT("bounds Z cm"), Size.Z, ExpectedCm.Z, ToleranceCm);
			return true;
		}

	private:
		FAutomationTestBase& Test;
		FIfcStatePtr State;
		FVector ExpectedCm;
		double ToleranceCm;
	};

	/**
	 * Cycles the four building material styles and checks each one actually re-parents the emitted
	 * sections onto the intended MI_RuntimeMeshBuilder* instance.
	 *
	 * This exists because the style->instance mapping is the kind of thing that looks obviously right
	 * and silently isn't: it was chosen from asset NAMES, and names lie. The names were then verified
	 * against the instances' own base_property_overrides (Opaque=BLEND_OPAQUE, Masked=BLEND_MASKED,
	 * Translucent=BLEND_TRANSLUCENT, TranslucentClearcoat=BLEND_TRANSLUCENT), and this test pins the
	 * wiring so a renamed or moved asset fails here instead of in someone's viewport.
	 *
	 * "TranslucentClearcoat" is now a name only: the instance carries MSM_DEFAULT_LIT, because clear
	 * coat is deferred and a translucent surface is forward shaded. The asset keeps its name so the
	 * mapping above stays greppable -- the shading model is not asserted here.
	 */
	class FCheckMaterialStylesCommand : public IAutomationLatentCommand
	{
	public:
		FCheckMaterialStylesCommand(FAutomationTestBase& InTest, FIfcStatePtr InState)
			: Test(InTest), State(InState) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			ARuntimeMeshBuilder* Builder = FindMeshBuilder(World);
			if (!Builder || !Builder->MobiusProceduralMeshComponent || State->Bounds.IsValid == 0)
			{
				return true; // the load command already reported why
			}

			struct FStyleCase
			{
				EMobiusBuildingMaterialStyle Style;
				const TCHAR* ExpectedParent;
				const TCHAR* What;
			};
			const FStyleCase Cases[] =
			{
				{ EMobiusBuildingMaterialStyle::OriginalColours,            TEXT("MI_RuntimeMeshBuilderOpaque"),               TEXT("original colours") },
				{ EMobiusBuildingMaterialStyle::OriginalColoursCutOut,      TEXT("MI_RuntimeMeshBuilderMasked"),               TEXT("original colours, cut out") },
				{ EMobiusBuildingMaterialStyle::TransparentWhite,           TEXT("MI_RuntimeMeshBuilderTranslucent"),          TEXT("transparent white") },
				{ EMobiusBuildingMaterialStyle::OriginalColoursTransparent, TEXT("MI_RuntimeMeshBuilderTranslucentClearcoat"), TEXT("original colours, transparent") },
			};

			for (const FStyleCase& Case : Cases)
			{
				Builder->SetBuildingMaterialStyle(Case.Style);
				Test.TestEqual(FString::Printf(TEXT("style '%s' is recorded"), Case.What),
				               static_cast<int32>(Builder->GetBuildingMaterialStyle()), static_cast<int32>(Case.Style));

				UMaterialInterface* Applied = Builder->MobiusProceduralMeshComponent->GetMaterial(0);
				UMaterialInstanceDynamic* AsMid = Cast<UMaterialInstanceDynamic>(Applied);
				if (!Test.TestNotNull(FString::Printf(TEXT("style '%s' applied a MID to section 0"), Case.What), AsMid))
				{
					continue;
				}

				UMaterialInterface* Parent = AsMid->Parent;
				if (Test.TestNotNull(FString::Printf(TEXT("style '%s' MID has a parent"), Case.What), Parent))
				{
					Test.TestEqual(FString::Printf(TEXT("style '%s' parent instance"), Case.What),
					               Parent->GetName(), FString(Case.ExpectedParent));
				}

				// The source colour must survive a style switch. Section 0 of the IFC2X3 building is a
				// styled product, so "Use Modified Colour" has to come back as 1 for every style -- if
				// the per-section colour cache were lost, this would read 0 and the building would go
				// plain white on the first mode change.
				float UseModified = -1.0f;
				if (AsMid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Use Modified Colour")), UseModified))
				{
					Test.TestEqual(FString::Printf(TEXT("style '%s' kept section 0's source colour enabled"), Case.What),
					               UseModified, 1.0f);
				}
			}

			// ---- The check that catches a shared material being stamped over the per-section ones ----
			//
			// Asserting "a MID exists with Use Modified Colour = 1" is not enough: that was true while
			// the building rendered as one flat tan, because SetMaterialOnMesh was overwriting every
			// section with a single shared MID after OnMeshBuilt fired. What distinguishes correct from
			// broken is that DIFFERENT sections carry DIFFERENT colours. This file's building has wall
			// grey, frame white and sash tan, so a correct application has at least three distinct
			// NewColour values across its 56 sections.
			Builder->SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle::OriginalColours);
			{
				TArray<FLinearColor> Distinct;
				const int32 NumSections = Builder->MobiusProceduralMeshComponent->GetNumSections();
				for (int32 i = 0; i < NumSections; ++i)
				{
					UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(
						Builder->MobiusProceduralMeshComponent->GetMaterial(i));
					FLinearColor Colour = FLinearColor::White;
					if (Mid && Mid->GetVectorParameterValue(FMaterialParameterInfo(TEXT("NewColour")), Colour))
					{
						const bool bAlready = Distinct.ContainsByPredicate(
							[&Colour](const FLinearColor& Existing) { return Existing.Equals(Colour, 1.0e-4f); });
						if (!bAlready)
						{
							Distinct.Add(Colour);
						}
					}
				}
				Test.AddInfo(FString::Printf(TEXT("distinct section colours applied: %d across %d sections"),
				                             Distinct.Num(), NumSections));
				Test.TestTrue(FString::Printf(TEXT("sections carry MORE THAN ONE distinct colour (got %d) -- ")
				                              TEXT("one colour means a shared material overwrote the per-section MIDs"),
				                              Distinct.Num()),
				              Distinct.Num() >= 3);
			}

			// -----------------------------------------------------------------------------------------
			// The base MATERIAL must actually declare the colour parameters.
			//
			// Reading NewColour back off a MID proves nothing: UMaterialInstanceDynamic accepts and stores
			// ANY parameter name, so SetVectorParameterValue("NewColour", ...) succeeds and reads back
			// correctly even when no material in the chain has such a parameter -- nothing renders. That is
			// exactly what happened on 2026-08-12: an unsaved edit to M_RuntimeMaster meant the parameters
			// did not exist on disk, while a MID-readback test reported "8 distinct colours" and passed.
			//
			// Asking the base UMaterial for its declared parameter list cannot be satisfied that way.
			// -----------------------------------------------------------------------------------------
			{
				UMaterialInterface* SectionMat = Builder->MobiusProceduralMeshComponent->GetMaterial(0);
				UMaterial* BaseMat = SectionMat ? SectionMat->GetMaterial() : nullptr;
				Test.TestNotNull(TEXT("section 0 resolves to a base UMaterial"), BaseMat);
				if (BaseMat)
				{
					TArray<FMaterialParameterInfo> VectorInfos, ScalarInfos;
					TArray<FGuid> VectorIds, ScalarIds;
					BaseMat->GetAllVectorParameterInfo(VectorInfos, VectorIds);
					BaseMat->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);

					const bool bHasNewColour = VectorInfos.ContainsByPredicate(
						[](const FMaterialParameterInfo& I) { return I.Name == FName(TEXT("NewColour")); });
					const bool bHasUseModified = ScalarInfos.ContainsByPredicate(
						[](const FMaterialParameterInfo& I) { return I.Name == FName(TEXT("Use Modified Colour")); });

					Test.TestTrue(*FString::Printf(
						              TEXT("base material '%s' DECLARES vector parameter 'NewColour' -- without ")
						              TEXT("it, per-section MID writes are stored and render nothing"),
						              *BaseMat->GetName()),
					              bHasNewColour);
					Test.TestTrue(*FString::Printf(
						              TEXT("base material '%s' DECLARES scalar parameter 'Use Modified Colour'"),
						              *BaseMat->GetName()),
					              bHasUseModified);
				}
			}

			// -----------------------------------------------------------------------------------------
			// Simulate WBP_SetBuildingMat's OnMeshBuilt response EXACTLY, and repeatedly.
			//
			// The previous version of this block called
			//     Builder->UpdateMeshMaterial(Builder->GetMobiusMaterialInstanceDynamic())
			// i.e. handed the SAME MID straight back. That passes while the real thing is broken, because
			// the widget does not do that: it calls CreateDynamicMaterialInstance, which builds a NEW MID
			// parented on the component's CURRENT material -- which by then is a section MID this class
			// created. Feeding that back in stacked a MID level per interaction, and once the chain closed
			// on itself UMaterialInstance::SetParentInternal refused it and left Parent NULL.
			//
			// So: build the MID the way Blueprint does, three times over, and assert the invariants that
			// distinguish correct from broken -- no MID parented on a MID, no null parent, and a stable
			// MID population (no doubling up).
			// -----------------------------------------------------------------------------------------
			const int32 NumSections = Builder->MobiusProceduralMeshComponent->GetNumSections();
			int32 PreviousMidCount = -1;

			for (int32 Pass = 1; Pass <= 3; ++Pass)
			{
				// == CreateDynamicMaterialInstance(0) on the procedural mesh component.
				UMaterialInterface* CurrentOnSection0 = Builder->MobiusProceduralMeshComponent->GetMaterial(0);
				UMaterialInstanceDynamic* WidgetMid =
					UMaterialInstanceDynamic::Create(CurrentOnSection0, Builder);
				Test.TestNotNull(*FString::Printf(TEXT("pass %d: widget-style MID created"), Pass), WidgetMid);
				if (!WidgetMid)
				{
					return true;
				}

				Builder->UpdateMeshMaterial(WidgetMid);

				TArray<FLinearColor> Distinct;
				TSet<UMaterialInstanceDynamic*> LiveMids;
				int32 NestedParents = 0;
				int32 NullParents   = 0;

				for (int32 i = 0; i < NumSections; ++i)
				{
					UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(
						Builder->MobiusProceduralMeshComponent->GetMaterial(i));
					if (!Mid)
					{
						continue;
					}
					LiveMids.Add(Mid);

					if (Mid->Parent == nullptr)
					{
						++NullParents;
					}
					else if (Mid->Parent->IsA<UMaterialInstanceDynamic>())
					{
						++NestedParents;
					}

					FLinearColor Colour = FLinearColor::White;
					if (Mid->GetVectorParameterValue(FMaterialParameterInfo(TEXT("NewColour")), Colour))
					{
						if (!Distinct.ContainsByPredicate(
							[&Colour](const FLinearColor& Existing) { return Existing.Equals(Colour, 1.0e-4f); }))
						{
							Distinct.Add(Colour);
						}
					}
				}

				Test.TestEqual(*FString::Printf(
					               TEXT("pass %d: NO section MID is parented on another MID (nested MIDs stack ")
					               TEXT("per widget interaction until the parent is refused)"), Pass),
				               NestedParents, 0);

				Test.TestEqual(*FString::Printf(
					               TEXT("pass %d: NO section MID has a null parent (SetParentInternal refuses a ")
					               TEXT("cyclic parent and leaves it null, which renders as nothing)"), Pass),
				               NullParents, 0);

				Test.TestTrue(*FString::Printf(
					              TEXT("pass %d: per-section colours survive the widget's OnMeshBuilt path ")
					              TEXT("(got %d distinct)"), Pass, Distinct.Num()),
				              Distinct.Num() >= 3);

				// Population must be stable across passes: a growing set is the "doubling up" symptom.
				if (PreviousMidCount >= 0)
				{
					Test.TestEqual(*FString::Printf(
						               TEXT("pass %d: section MID count is stable across repeated widget calls ")
						               TEXT("(was %d)"), Pass, PreviousMidCount),
					               LiveMids.Num(), PreviousMidCount);
				}
				PreviousMidCount = LiveMids.Num();

				Test.AddInfo(FString::Printf(
					TEXT("pass %d: %d distinct colours, %d live section MIDs, parent '%s'"),
					Pass, Distinct.Num(), LiveMids.Num(),
					LiveMids.Num() > 0 && (*LiveMids.CreateConstIterator())->Parent
						? *(*LiveMids.CreateConstIterator())->Parent->GetName()
						: TEXT("<none>")));
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
		FIfcStatePtr State;
	};

	/**
	 * Points a camera at the loaded building and writes two screenshots -- unlit and wireframe -- as
	 * human-inspectable evidence that the geometry draws and that no solid block fills a door, a window
	 * or a room.
	 *
	 * Self-skips when the run has no renderer: FApp::CanEverRender() is false under -nullrhi, which is
	 * the default for MobiusPerf\RunTests.ps1 -- pass -Rendered to get a real RHI. A skipped screenshot
	 * is reported, not silent, because "no PNG appeared" and "the PNG was fine" must not look alike.
	 */
	class FScreenshotBuildingCommand : public IAutomationLatentCommand
	{
	public:
		FScreenshotBuildingCommand(FAutomationTestBase& InTest, FIfcStatePtr InState)
			: Test(InTest), State(InState) {}

		virtual bool Update() override
		{
			if (!FApp::CanEverRender())
			{
				Test.AddInfo(TEXT("screenshots SKIPPED: no renderer in this run (-nullrhi). Re-run with -Rendered."));
				return true;
			}

			UWorld* World = GetActiveGameWorld();
			if (!World || State->Bounds.IsValid == 0)
			{
				Test.AddInfo(TEXT("screenshots SKIPPED: no world or no building bounds (the load assertions say why)."));
				return true;
			}

			APlayerController* PC = World->GetFirstPlayerController();
			if (!PC)
			{
				Test.AddInfo(TEXT("screenshots SKIPPED: no player controller to set a view target on."));
				return true;
			}

			if (!bStaged)
			{
				// The level's exponential height fog turns a lit shot of a 9 m building into a grey
				// smudge at any distance that fits it in frame -- the first version of this test produced
				// exactly that. Unlit removes lighting and fog from the question entirely; the geometry
				// is what is under test, not the level's atmosphere.
				//
				// ShowFlag.Wireframe 0 is EXPLICIT and load-bearing: show flags live on the viewport, not
				// on the test, so they persist across tests in one -game process. The first version of
				// this file left wireframe on at the end of the IFC2X3 test and the IFC4X3 test's
				// "solid" shot came out wireframe. Every flag this command depends on is set every time.
				PC->ConsoleCommand(TEXT("ShowFlag.Fog 0"), true);
				PC->ConsoleCommand(TEXT("ShowFlag.AtmosphericFog 0"), true);
				PC->ConsoleCommand(TEXT("ShowFlag.VolumetricFog 0"), true);
				PC->ConsoleCommand(TEXT("ShowFlag.Lighting 0"), true);
				PC->ConsoleCommand(TEXT("ShowFlag.Wireframe 0"), true);

				// Three-quarter view from outside, framed on the centre and scaled to the model so this
				// works for any fixture. 0.9x the largest span is close enough that a 1.2 m window hole
				// is a few dozen pixels rather than a smudge.
				const FVector Centre = State->Bounds.GetCenter();
				const double Span = State->Bounds.GetSize().GetMax();
				const FVector CamLoc = Centre + FVector(-0.9 * Span, -0.9 * Span, 0.45 * Span);

				ACameraActor* Cam = World->SpawnActor<ACameraActor>(CamLoc,
					FRotationMatrix::MakeFromX(Centre - CamLoc).Rotator());
				if (!Cam)
				{
					Test.AddInfo(TEXT("screenshots SKIPPED: could not spawn a camera actor."));
					return true;
				}
				PC->SetViewTarget(Cam);

				bStaged = true;
				FramesWaited = 0;
				return false;
			}

			// Let the view target blend and the show flags apply before the first capture.
			if (FramesWaited < 10)
			{
				++FramesWaited;
				return false;
			}

			if (ShotIndex >= 3)
			{
				return true;
			}

			if (!bRequested)
			{
				// Shot 0 unlit solid, shot 1 wireframe, shot 2 LIT.
				//
				// Wireframe settles "is a door hole a hole, or is there an IfcOpeningElement block in
				// it". It needs the 10-frame settle: capturing one frame after setting the flag produced
				// a black frame carrying only the tiled "your scene contains a skydome mesh" warning.
				//
				// The LIT shot exists because neither of the other two can see FACE ORIENTATION: the
				// building material is translucent and renders two-sided, so an inside-out mesh looks
				// identical unlit and in wireframe. That is how a full index reversal shipped and was
				// only caught by the owner looking at a lit view (2026-08-12). The authoritative
				// regression test for facing is now numeric — every product's RHR signed volume must be
				// negative, asserted in both IFC tests — and this capture is the human-legible companion
				// to it, not the gate.
				if (ShotIndex == 1)
				{
					PC->ConsoleCommand(TEXT("ShowFlag.Wireframe 1"), true);
					if (++FlagSettleFrames < 10)
					{
						return false;
					}
				}
				else if (ShotIndex == 2)
				{
					PC->ConsoleCommand(TEXT("ShowFlag.Wireframe 0"), true);
					PC->ConsoleCommand(TEXT("ShowFlag.Lighting 1"), true);
					if (++FlagSettleFrames < 10)
					{
						return false;
					}
				}

				const TCHAR* ShotName = TEXT("Solid");
				if (ShotIndex == 1) { ShotName = TEXT("Wireframe"); }
				else if (ShotIndex == 2) { ShotName = TEXT("Lit"); }

				CurrentPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("IfcInGame"),
					FString::Printf(TEXT("%s_%s.png"), *State->Tag, ShotName));
				FScreenshotRequest::RequestScreenshot(CurrentPath, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
				bRequested = true;
				FramesWaited = 0;
				return false;
			}

			// The request is serviced at the end of a frame and written asynchronously; give it a few
			// frames before deciding it did not happen.
			if (++FramesWaited < 30)
			{
				return false;
			}

			if (FPaths::FileExists(CurrentPath))
			{
				Test.AddInfo(FString::Printf(TEXT("screenshot written: %s"), *CurrentPath));
			}
			else
			{
				Test.AddInfo(FString::Printf(
					TEXT("screenshot NOT written (requested %s) - not failing the test on it; the numeric ")
					TEXT("assertions above are the real gate."), *CurrentPath));
			}

			++ShotIndex;
			bRequested = false;
			FlagSettleFrames = 0;
			FramesWaited = 10; // already staged; no need to re-wait for the view target
			return ShotIndex >= 3;
		}

	private:
		FAutomationTestBase& Test;
		FIfcStatePtr State;
		bool bStaged = false;
		bool bRequested = false;
		int32 FramesWaited = 0;
		int32 FlagSettleFrames = 0;
		int32 ShotIndex = 0;
		FString CurrentPath;
	};
}

// =================================================================================================
// IFC2X3 -- in the repo, so a missing fixture is an error, not a skip. Exact bounds assertion.
// =================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIfcInGameIfc2x3Test,
	"Mobius.InGame.Ifc.Ifc2x3LoadsIntoProceduralMesh",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FIfcInGameIfc2x3Test::RunTest(const FString& /*Parameters*/)
{
	// KNOWN FAILURE UNDER -Rendered, ENGINE-SIDE, NOT A MOBIUS DEFECT. Documented rather than
	// suppressed, the same way Mobius.InGame.TrajectoryHeatmap.T_PIX_2 is.
	//
	// The wireframe capture trips UE's own RDG validation from AddEditorPrimitivePass
	// (PostProcessCompositeEditorPrimitives.cpp:478):
	//   Ensure condition failed: Resource->bProduced || Resource->bExternal || Resource->bQueuedForUpload
	//   Pass Composite <w>x<h> MSAA=4 has a read dependency on Composite.PrimitivesDepth, but it was
	//   never written to.
	//
	// EVERY ASSERTION IN THIS TEST PASSES WHEN IT FIRES. The run is marked failed only because an
	// ensure counts as an error. The same log carries the proof: "sections=56 tris=2988",
	// "8 distinct colours, 56 live section MIDs, parent 'MI_RuntimeMeshBuilderOpaque'", and all three
	// screenshots (Solid / Wireframe / Lit) written.
	//
	// It appeared on 2026-08-12 when the loader began applying Original Colours at load
	// (RuntimeMeshBuilder.cpp). An opaque building takes a different branch through the editor-primitive
	// composite than a fully translucent one, so that change REACHES this engine bug rather than causing
	// it -- the ensure is raised entirely inside engine code with no Mobius frame on the stack.
	//
	// AddExpectedError was tried on 2026-08-12 and REVERTED: the ensure emits several separate error
	// lines ("=== Handled ensure: ===", the condition, "Stack:", blanks), so suppressing it needs either
	// a broad pattern that would hide real failures, or per-line expectations that break the moment the
	// engine reworks the message. Worse, the ensure does NOT fire on the IFC4X3 test, so an expectation
	// added there fails with "expected ... did not occur" and turns a passing test red.
	//
	// If this needs to go green: run without -Rendered (the screenshots self-skip), or take the
	// wireframe capture without MSAA.
	const FString Path = IfcInGame::Ifc2x3FixturePath();
	if (!FPaths::FileExists(Path))
	{
		// Tracked in git: absence is a broken checkout, and "skipped" would report that as a pass.
		AddError(FString::Printf(TEXT("IFC2X3 fixture missing: %s"), *Path));
		return false;
	}

	IfcInGame::FIfcStatePtr State = MakeShared<IfcInGame::FIfcInGameState, ESPMode::ThreadSafe>();
	State->FixturePath = Path;
	State->Tag = TEXT("Ifc2x3");

	// 44 products with geometry, 3072 triangles (was 3092 before the 2026-08-12 zero-area drop);
	// minus 7 IfcOpeningElement = 37 renderable products.
	// No IfcSpace in this file. Harness world AABB (9.400, 6.400, 4.050) m -> (940, 640, 405) cm.
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FLoadIfcCommand(*this, State, TEXT("IFC2X3"), 3072, 37, 0, 120.0));
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FCheckBoundsCommand(*this, State, FVector(940.0, 640.0, 405.0), 1.0));
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FCheckMaterialStylesCommand(*this, State));
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FScreenshotBuildingCommand(*this, State));
	return true;
}

// =================================================================================================
// IFC4X3_ADD2 -- 205 products, 36 openings, 14 IfcSpace, 17 IfcSensor. Lives outside the repo, so it
// skips loudly when absent. This is the file whose rooms would be 14 opaque blocks without the
// allowlist, which is what the wireframe screenshot is for.
// =================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIfcInGameIfc4x3Test,
	"Mobius.InGame.Ifc.Ifc4x3LoadsIntoProceduralMesh",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FIfcInGameIfc4x3Test::RunTest(const FString& /*Parameters*/)
{
	const FString Path = IfcInGame::Ifc4x3FixturePath();
	if (Path.IsEmpty())
	{
		AddInfo(TEXT("SKIPPED: Mobius_InternalData\\12 RoomTest\\Exported-model\\ISO-Test-8-FireSmoke.ifc is not ")
		        TEXT("on this machine. That fixture is deliberately outside the public repo (large models are not ")
		        TEXT("committed), so this test cannot run from a bare checkout."));
		return true;
	}

	IfcInGame::FIfcStatePtr State = MakeShared<IfcInGame::FIfcInGameState, ESPMode::ThreadSafe>();
	State->FixturePath = Path;
	State->Tag = TEXT("Ifc4x3");

	// 205 products / 16499 triangles; minus 36 IfcOpeningElement + 14 IfcSpace + 17 IfcSensor +
	// 1 IfcGeographicElement (annotations off by owner policy) = 137 renderable products.
	// 16499, down from 16591 on 2026-08-12: 92 zero-area collinear T-junction triangles are now
	// dropped at emit rather than shipped with a (0,0,0) normal. See IfcImportTest.cpp.
	const int32 ExpectedRenderable = MobiusIfc::bMobiusIfcRenderAnnotationClasses ? 155 : 137;
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FLoadIfcCommand(
		*this, State, TEXT("IFC4X3_ADD2"), 16499, ExpectedRenderable, 14, 180.0));

	// The RENDERED hull is 2178.6 x 1237.2 x 470.5 cm, NOT the 4000 x 2200 x 480.5 cm whole-file AABB
	// the harness reports. That is not a shortfall — it is attributed, measured per class through the
	// DLL on 2026-08-12:
	//     ALL products            4000.0 x 2200.0 x 480.5
	//     RENDER set (137)        2178.6 x 1237.2 x 470.5
	//     IfcGeographicElement    4000.0 x 2200.0 x  30.0   <- ONE product, a site/ground plate
	//     IfcSpace (14)           2078.6 x 1137.2 x 400.0   <- rooms, inside the building, as expected
	//     IfcSensor (17)          1880.5 x  619.3 x   9.0
	// The single IfcGeographicElement IS the 40 x 22 m extent: it is a 0.3 m thick ground plate, and it
	// is an Annotation class, which owner policy currently leaves OFF. So flipping
	// bMobiusIfcRenderAnnotationClasses does not merely add 18 small markers — it adds a 40 x 22 m
	// ground plate and quadruples the building's footprint. Asserted both ways so that flip is a
	// deliberate, visible change rather than a surprise.
	const FVector ExpectedBoundsCm = MobiusIfc::bMobiusIfcRenderAnnotationClasses
		                                 ? FVector(4000.0, 2200.0, 480.5)
		                                 : FVector(2178.6, 1237.2, 470.5);
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FCheckBoundsCommand(*this, State, ExpectedBoundsCm, 1.0));
	ADD_LATENT_AUTOMATION_COMMAND(IfcInGame::FScreenshotBuildingCommand(*this, State));
	return true;
}

#endif // !UE_BUILD_SHIPPING
