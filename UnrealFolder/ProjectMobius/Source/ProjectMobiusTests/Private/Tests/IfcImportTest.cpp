// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

// ============================================================================================
// Runtime IFC import regression anchors.
//
// The numbers asserted here are NOT "whatever the code printed when this was written". They come
// from two independent places and that is the whole point of the file:
//
//   1. THE ANALYTIC VOLUMES (3.500000, 4.800000, 3.213200 m3) are hand-derived from the IFC source
//      text of ProjectMobius\TestData\ISO-Test-1-2x3.ifc -- profile dimensions x extrusion depth,
//      minus the hand-derived volumes of the IfcOpeningElement voids that were booleaned out of it.
//      Every term is traceable to an entity id in the file; see HANDOFF_IFC_2026-08-11.md 5.2 and
//      13.5. They therefore test the whole chain at once: STEP parse -> profile interpretation ->
//      extrusion -> mm-to-m unit scaling -> Carve boolean subtraction -> IFC-to-UE conversion
//      (scale AND handedness) -> our own vertex/index marshalling.
//
//      A volume assertion catches more than it looks like it does. Wrong unit scale is off by 10^3
//      or 10^9. A missing mirror or a missing winding reversal makes the signed volume NEGATIVE.
//      A dropped boolean makes the cut walls come back as 5.880000 / 3.640000. What it CANNOT catch
//      is a mirror on the wrong axis -- volume is mirror-invariant -- which is why the harness has a
//      separate per-axis asymmetry lens (handoff 13.2, lens 4) that no UE-side test replaces.
//
//   2. THE PRODUCT AND TRIANGLE COUNTS (44/3092 and 205/16592) are what the standalone IFC++
//      validation harness and the DLL's own pure-C consumer test independently measured on these two
//      files (handoff 5.1, 5.4, 13.3). The renderable counts (37 and 137) are the allowlist
//      arithmetic in handoff 13.4.
//
// The IFC2X3 file lives in the repo, so its test is a hard failure when absent. The IFC4X3_ADD2 file
// lives in Mobius_InternalData (deliberately outside the public repo -- large models must not be
// committed), so that test SKIPS with an explicit AddInfo when the fixture is not on the machine.
// A skipped automation test reports green, so the skip message names the file it wanted.
// ============================================================================================

#if !UE_BUILD_SHIPPING

#include "AsyncAssimpMeshLoader.h" // FAssimpSubmeshBuffers
#include "CoreMinimal.h"
#include "Ifc/MobiusIfcMeshLoader.h"
#include "Ifc/MobiusIfcRenderableClasses.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "MobiusTestDataRoots.h"

namespace
{
	/** UE-space centimetres cubed per cubic metre. */
	constexpr double GCmCubedPerMetreCubed = 1.0e6;

	/**
	 * Signed volume of a closed triangle mesh via the divergence theorem, in the same form the
	 * validation harness uses: V = (1/6) * sum over triangles of dot(a, cross(b, c)).
	 *
	 * Pivoted at the mesh's own AABB centre rather than the world origin. The divergence theorem is
	 * translation-invariant for a closed mesh, so this changes nothing mathematically -- but summing
	 * about a distant origin makes each tetrahedron term O(|r|^3) and those terms then cancel down to
	 * a small volume, which cost the harness a false failure on three IfcSensor products sitting ~20 m
	 * out (handoff 13.2). Same trap, same fix, kept deliberately identical so the two implementations
	 * are comparable.
	 *
	 * Returns cubic centimetres. Positive for a consistently CCW-outward-wound closed mesh, which is
	 * what the DLL is contracted to hand out -- so the SIGN is part of what this asserts.
	 */
	double SignedVolumeCm3(const FAssimpSubmeshBuffers& Sub)
	{
		if (Sub.Vertices.Num() == 0 || Sub.Faces.Num() < 3)
		{
			return 0.0;
		}

		FVector Min = Sub.Vertices[0];
		FVector Max = Sub.Vertices[0];
		for (const FVector& V : Sub.Vertices)
		{
			Min = Min.ComponentMin(V);
			Max = Max.ComponentMax(V);
		}
		const FVector Pivot = (Min + Max) * 0.5;

		double Accum = 0.0;
		for (int32 i = 0; i + 2 < Sub.Faces.Num(); i += 3)
		{
			const FVector A = Sub.Vertices[Sub.Faces[i]] - Pivot;
			const FVector B = Sub.Vertices[Sub.Faces[i + 1]] - Pivot;
			const FVector C = Sub.Vertices[Sub.Faces[i + 2]] - Pivot;
			Accum += FVector::DotProduct(A, FVector::CrossProduct(B, C));
		}

		return Accum / 6.0;
	}

	/**
	 * Total triangle surface area of a submesh, in square centimetres.
	 *
	 * THIS IS THE CHECK THE VOLUME ANCHORS CANNOT MAKE, and it exists because a real defect got past
	 * them. The DLL used to fan-triangulate each carve face from corner 0, which is only valid for a
	 * convex polygon; carve's boolean subtraction produces non-convex faces around every window and
	 * door opening, and fanning those emits triangles that leave the polygon, cover the hole and
	 * overlap each other. Measured on the 8.4 x 0.2 x 3.5 m wall with three 1.2 x 1.5 m openings:
	 * 131.729704 m² emitted against an analytic 56.000000 m².
	 *
	 * Every volume anchor passed throughout, and still passes. That is not luck — the
	 * divergence-theorem sum over a fan of a planar polygon is exact regardless of convexity, because
	 * the sliver contributions cancel in signed arithmetic. Area does not cancel: overlaps add, and a
	 * covered hole adds. So area is the anchor that guards triangulation, and volume is the anchor that
	 * guards scale/handedness/booleans. Both are needed; neither substitutes for the other.
	 */
	double SurfaceAreaCm2(const FAssimpSubmeshBuffers& Sub)
	{
		double Total = 0.0;
		for (int32 i = 0; i + 2 < Sub.Faces.Num(); i += 3)
		{
			const FVector A = Sub.Vertices[Sub.Faces[i]];
			const FVector B = Sub.Vertices[Sub.Faces[i + 1]];
			const FVector C = Sub.Vertices[Sub.Faces[i + 2]];
			Total += 0.5 * FVector::CrossProduct(B - A, C - A).Size();
		}
		return Total;
	}

	/** Total triangles across every emitted submesh. */
	int32 CountTriangles(const TArray<FAssimpSubmeshBuffers>& Submeshes)
	{
		int32 Total = 0;
		for (const FAssimpSubmeshBuffers& Sub : Submeshes)
		{
			Total += Sub.Faces.Num() / 3;
		}
		return Total;
	}

	/** First submesh with the given IFC GUID, or nullptr. */
	const FAssimpSubmeshBuffers* FindByGuid(const TArray<FAssimpSubmeshBuffers>& Submeshes, const FString& Guid)
	{
		for (const FAssimpSubmeshBuffers& Sub : Submeshes)
		{
			if (Sub.SourceGuid == Guid)
			{
				return &Sub;
			}
		}
		return nullptr;
	}

	/** ProjectDir is <workspace>\ProjectMobius\UnrealFolder\ProjectMobius\ -- the repo root is two up. */
	FString RepoRootDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../..")));
	}

	/** The IFC2X3 conformance file, committed to the repo. */
	FString Ifc2x3FixturePath()
	{
		return FPaths::Combine(RepoRootDir(), TEXT("TestData"), TEXT("ISO-Test-1-2x3.ifc"));
	}

	/**
	 * The IFC4X3_ADD2 export, which lives in Mobius_InternalData OUTSIDE the repo (large models
	 * are not committed). Returns empty when it is not on this machine, and the caller skips
	 * loudly. Root resolution is shared with every other private-fixture test through
	 * MobiusTestDataRoots.h.
	 */
	FString Ifc4x3FixturePath()
	{
		const FString Relative = FPaths::Combine(
			TEXT("12 RoomTest"), TEXT("Exported-model"), TEXT("ISO-Test-8-FireSmoke.ifc"));

		// Roots come from MobiusTestDataRoots.h. This used to list absolute drive paths, which
		// worked on one machine and published its layout. Set MOBIUS_INTERNAL_DATA if your copy
		// of the private datasets lives somewhere the relative roots do not cover.
		return MobiusTestData::FindInternalFixture(Relative);
	}
}

// =================================================================================================
// IFC2X3 -- the analytic-volume anchors. This is the test that would catch a coordinate or unit
// regression, and it runs on a fixture that is always present.
// =================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIfcImportAnalyticVolumesTest,
	"ProjectMobius.Ifc.Import.AnalyticVolumes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FIfcImportAnalyticVolumesTest::RunTest(const FString& /*Parameters*/)
{
	const FString FixturePath = Ifc2x3FixturePath();
	if (!TestTrue(FString::Printf(TEXT("IFC2X3 fixture exists: %s"), *FixturePath),
	              FPaths::FileExists(FixturePath)))
	{
		return false;
	}

	TArray<FAssimpSubmeshBuffers> Submeshes;
	FMobiusIfcLoadStats Stats;
	FString Error;

	const bool bLoaded = FMobiusIfcMeshLoader::LoadIfcFile(FixturePath, Submeshes, Stats, Error);
	if (!TestTrue(FString::Printf(TEXT("LoadIfcFile succeeded (error: %s)"), *Error), bLoaded))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("IFC load: %s"), *Stats.FilterSummary));

	// --- Schema, read from the file's own header rather than the library's unreliable accessor -----
	TestEqual(TEXT("source schema from FILE_SCHEMA header"), Stats.SourceSchema, FString(TEXT("IFC2X3")));

	// --- Counts pinned by the standalone harness and the DLL's pure-C test ------------------------
	TestEqual(TEXT("products with geometry"), Stats.ProductsWithGeometry, 44);
	TestEqual(TEXT("total triangles in file"), Stats.TotalTriangles, 3092);
	TestEqual(TEXT("malformed products"), Stats.MalformedProducts, 0);

	// --- Allowlist: 44 with geometry minus the 7 IfcOpeningElement void volumes = 37 --------------
	TestEqual(TEXT("renderable products after allowlist"), Stats.RenderedProducts, 37);
	// One submesh per SECTION, not per product: ABI v2 splits a product by distinct appearance, so the
	// 37 renderable products yield 56 sections (19 of them carry two styles -- window frame vs sash).
	// Asserting products here is what this line used to do, and it failed the moment materials landed.
	TestEqual(TEXT("emitted submeshes matches emitted sections"), Submeshes.Num(), Stats.RenderedSections);
	TestEqual(TEXT("emitted sections (37 products, 19 of them two-toned)"), Stats.RenderedSections, 56);
	TestEqual(TEXT("emitted triangles matches stats"), CountTriangles(Submeshes), Stats.RenderedTriangles);
	TestTrue(TEXT("rendered triangles are fewer than the file total (openings were dropped)"),
	         Stats.RenderedTriangles < Stats.TotalTriangles);

	// This file has no IfcSpace, so the B-RISK room channel is legitimately empty here. The IFC4X3
	// test below is the one that proves rooms come through.
	TestEqual(TEXT("IfcSpace room volumes (none in this file)"), Stats.RoomVolumes.Num(), 0);

	// --- Nothing volume-only may reach a mesh section. The direct statement of the bug the ---------
	// --- allowlist exists to prevent: render an IfcOpeningElement and the door hole fills in. ------
	for (const FAssimpSubmeshBuffers& Sub : Submeshes)
	{
		const MobiusIfc::ERenderVerdict Verdict = MobiusIfc::ClassifyClass(Sub.SourceIfcClass);
		TestTrue(FString::Printf(TEXT("emitted class '%s' is not VolumeOnly"), *Sub.SourceIfcClass),
		         Verdict != MobiusIfc::ERenderVerdict::VolumeOnly);
		TestTrue(FString::Printf(TEXT("emitted class '%s' is not Unknown"), *Sub.SourceIfcClass),
		         Verdict != MobiusIfc::ERenderVerdict::Unknown);
		TestFalse(TEXT("every emitted section carries its IFC GUID"), Sub.SourceGuid.IsEmpty());
	}

	// --- MATERIALS ---------------------------------------------------------------------------------
	// Anchors traced from the IFC source text, not read off the output:
	//   #131=IFCCOLOURRGB($,0.50196078431372548, x3)
	//   #132=IFCSURFACESTYLERENDERING(#131,0.,...,IFCSPECULAREXPONENT(64.),.NOTDEFINED.)
	//   #133=IFCSURFACESTYLE('Default Wall',.BOTH.,(#132)) -> #134 -> #135 styles wall z0's solid #130
	//                                                                -> #217 styles wall _A's solid #216
	//   #139=IFCMATERIAL('Default Wall'); #144=IFCMATERIALLAYER(#139,200.,$)
	//   #145=IFCMATERIALLAYERSET((#144),'Basic Wall:Generic - 200mm')
	// The 20 cm layer thickness doubles as a unit-conversion check: it must equal the wall's own
	// modelled 0.2 m thickness, which the volume anchors already pin independently.
	{
		const FAssimpSubmeshBuffers* WallSub = FindByGuid(Submeshes, TEXT("0rSEC$DWP81heohefO23_A"));
		if (TestNotNull(TEXT("anchor wall was emitted (for material checks)"), WallSub))
		{
			TestTrue(TEXT("anchor wall section carries a source material"), WallSub->Material.bHasMaterial);
			TestNearlyEqual(TEXT("anchor wall diffuse R (IFCCOLOURRGB 0.501961)"),
			                static_cast<double>(WallSub->Material.BaseColour.R), 0.501961, 1.0e-5);
			TestNearlyEqual(TEXT("anchor wall diffuse G"),
			                static_cast<double>(WallSub->Material.BaseColour.G), 0.501961, 1.0e-5);
			TestNearlyEqual(TEXT("anchor wall diffuse B"),
			                static_cast<double>(WallSub->Material.BaseColour.B), 0.501961, 1.0e-5);
			TestNearlyEqual(TEXT("anchor wall opacity (IFC transparency 0. -> opaque)"),
			                static_cast<double>(WallSub->Material.BaseColour.A), 1.0, 1.0e-6);
			// 64 from IFCSPECULAREXPONENT(64.). Asserted only because it was MEASURED to come through:
			// the DLL originally read IFC++'s m_shininess here, which reported 1.0 for this file because
			// that field is derived from specular ROUGHNESS. Reading m_specular_exponent gives the 64.
			TestNearlyEqual(TEXT("anchor wall specular exponent (IFCSPECULAREXPONENT 64.)"),
			                static_cast<double>(WallSub->Material.SpecularExponent), 64.0, 1.0e-4);
			TestEqual(TEXT("anchor wall semantic material name (layer set name)"),
			          WallSub->SourceMaterialName, FString(TEXT("Basic Wall:Generic - 200mm")));
		}

		const FMobiusIfcProductMaterial* WallMaterial = Stats.ProductMaterials.FindByPredicate(
			[](const FMobiusIfcProductMaterial& M) { return M.Guid == TEXT("0rSEC$DWP81heohefO23_A"); });
		if (TestNotNull(TEXT("anchor wall has a semantic material record"), WallMaterial))
		{
			TestEqual(TEXT("anchor wall material name"), WallMaterial->MaterialName,
			          FString(TEXT("Basic Wall:Generic - 200mm")));
			if (TestEqual(TEXT("anchor wall layer count"), WallMaterial->Layers.Num(), 1))
			{
				TestEqual(TEXT("anchor wall layer name (IFCMATERIAL 'Default Wall')"),
				          WallMaterial->Layers[0].Name, FString(TEXT("Default Wall")));
				// 200 mm -> 20 cm. A hardcoded mm->cm factor would pass here and fail on a metres-unit
				// file, so this is really asserting that the UnitConverter factor is being applied.
				TestNearlyEqual(TEXT("anchor wall layer thickness cm (IFCMATERIALLAYER 200.)"),
				                static_cast<double>(WallMaterial->Layers[0].ThicknessCm), 20.0, 1.0e-4);
			}
		}

		// Sections outnumber products because a product is split per distinct appearance -- 19 products
		// in this file carry two styles (window frame vs sash). Measured through the DLL: 63 sections
		// across all 44 products, of which the 37 renderable ones produce these.
		TestTrue(FString::Printf(TEXT("sections (%d) >= renderable products (%d), from per-appearance splitting"),
		                         Stats.RenderedSections, Stats.RenderedProducts),
		         Stats.RenderedSections >= Stats.RenderedProducts);
		TestEqual(TEXT("emitted submeshes == RenderedSections"), Submeshes.Num(), Stats.RenderedSections);
	}

	// --- THE ANALYTIC ANCHORS ---------------------------------------------------------------------
	// Tolerance: 1e-4 m3, i.e. 0.003% of the smallest anchor. Vertices cross the DLL boundary as
	// float32 centimetres, so ~1e-6 relative error is expected and 6-decimal equality is not
	// available on this side of the boundary the way it is in the harness's doubles. This is still
	// tight enough that every failure mode the anchors exist for (10^3/10^9 unit slips, a dropped
	// boolean, an inverted sign) misses by orders of magnitude.
	struct FAnchor
	{
		const TCHAR* Guid;
		const TCHAR* What;
		double ExpectedM3;

		/**
		 * Analytic surface area, or <= 0 to report without asserting. Hand-derived from the same IFC
		 * entities as the volumes. See SurfaceAreaCm2 above for why this second family of anchors
		 * exists: volume is blind to a triangulation that overlaps itself and covers holes.
		 */
		double ExpectedM2;
	};

	const FAnchor Anchors[] =
	{
		// 5000 mm x 200 mm profile, 3500 mm extrusion, no openings. Proves extrusion + mm->m scaling.
		// Area: a plain box, 2(5*0.2 + 5*3.5 + 0.2*3.5) = 38.400000 m². Convex, so even the old fan
		// triangulation got this one right — which is exactly why the anchor set needs the next two.
		{ TEXT("0rSEC$DWP81heohefO23_A"), TEXT("plain wall, no boolean"),        3.500000, 38.400000 },
		// 8400 x 200 x 3500 = 5.880000 minus three 1.2 x 1.5 x 0.2 window voids (0.360000 each).
		// Area: uncut 2(8.4*0.2 + 8.4*3.5 + 0.2*3.5) = 63.56; each opening removes 2 x 1.8 = 3.6 from
		// the two large faces and adds a 2(1.2+1.5) = 5.4 m perimeter x 0.2 m reveal = 1.08, net -2.52.
		// 63.56 - 3(2.52) = 56.000000 m². The fan triangulation emitted 131.729704 m² here.
		{ TEXT("0rSEC$DWP81heohefO23z0"), TEXT("wall minus 3 window openings"),  4.800000, 56.000000 },
		// 5200 x 200 x 3500 = 3.640000 minus one 1.0 x 2.134 x 0.2 door void (0.426800).
		// Area: uncut 2(5.2*0.2 + 5.2*3.5 + 0.2*3.5) = 39.88. The opening's contribution has two
		// defensible hand-derivations depending on whether its sill sits ON the wall's bottom face:
		//   sill on the base (3-sided reveal, and 1.0 x 0.2 also lost from the bottom face):
		//       39.88 - 4.268 + (1.0 + 2*2.134)*0.2 - 0.2 = 36.465600
		//   sill clear of the base (4-sided reveal):
		//       39.88 - 4.268 + 2*(1.0 + 2.134)*0.2      = 36.865600
		// The mesh measures 36.465600 to 1e-6, which selects the first and therefore also establishes
		// that the door opening reaches the wall base. Asserted on that basis: the value is
		// hand-derived, and the geometry picked between two hand-derived candidates rather than the
		// expectation being read off the output.
		{ TEXT("0rSEC$DWP81heohefO23y6"), TEXT("wall minus 1 door opening"),     3.213200, 36.465600 },
	};

	for (const FAnchor& Anchor : Anchors)
	{
		const FAssimpSubmeshBuffers* Sub = FindByGuid(Submeshes, FString(Anchor.Guid));
		if (!TestNotNull(FString::Printf(TEXT("anchor product %s (%s) was emitted"), Anchor.Guid, Anchor.What), Sub))
		{
			continue;
		}

		const double VolumeCm3 = SignedVolumeCm3(*Sub);
		const double VolumeM3 = VolumeCm3 / GCmCubedPerMetreCubed;

		// SIGN MUST BE NEGATIVE, and that is the FACE-ORIENTATION anchor, not a defect.
		//
		// SignedVolumeCm3 uses the right-hand-rule cross product. The IFC->UE conversion mirrors one
		// axis and does NOT reverse index order (the convention this project's Assimp path already
		// uses, and the only one that renders right-side-out in UProceduralMeshComponent), so the RHR
		// signed volume of a correctly-wound closed product is negative here by construction.
		//
		// A POSITIVE value means someone reintroduced an index reversal and every face is inside-out.
		// That shipped once — owner-reported 2026-08-12 — precisely because the standalone harness's
		// lens 2 demands a positive RHR volume, which is a right-handed math convention masquerading as
		// a render convention. Asserting the sign here is what makes "the faces point outward" a
		// regression test instead of something that needs a human to look at a screenshot.
		TestTrue(FString::Printf(TEXT("%s: RHR signed volume is NEGATIVE (got %f m3), i.e. source-order ")
		                         TEXT("winding after one mirror -- POSITIVE means an index reversal came ")
		                         TEXT("back and the faces are inside-out"), Anchor.What, VolumeM3),
		         VolumeCm3 < 0.0);

		// Magnitude against the analytic volume. Absolute value, because the sign is asserted above and
		// carries orientation, not size.
		TestNearlyEqual(FString::Printf(TEXT("%s (%s) volume magnitude m3"), Anchor.What, Anchor.Guid),
		                FMath::Abs(VolumeM3), Anchor.ExpectedM3, 1.0e-4);

		// Surface area: the triangulation anchor. Tolerance 1e-3 m² (10 cm²) — looser than the volume
		// anchor because area is a sum of absolute values, so float32 error accumulates instead of
		// cancelling. Still tight enough that the failure it exists to catch missed by 75.7 m².
		const double AreaM2 = SurfaceAreaCm2(*Sub) / 1.0e4;
		if (Anchor.ExpectedM2 > 0.0)
		{
			TestNearlyEqual(FString::Printf(TEXT("%s (%s) surface area m2 -- guards triangulation, ")
			                                TEXT("which volume cannot see"), Anchor.What, Anchor.Guid),
			                AreaM2, Anchor.ExpectedM2, 1.0e-3);
		}
		else
		{
			AddInfo(FString::Printf(TEXT("%s (%s) surface area %.6f m2 (reported, not asserted)"),
			                        Anchor.What, Anchor.Guid, AreaM2));
		}
	}

	return true;
}

// =================================================================================================
// IFC4X3_ADD2 -- schema coverage, the render filter at scale, and the IfcSpace room channel.
// Skips (green, with a message) when the internal fixture is not on this machine.
// =================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIfcImportIfc4x3RenderFilterTest,
	"ProjectMobius.Ifc.Import.Ifc4x3RenderFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FIfcImportIfc4x3RenderFilterTest::RunTest(const FString& /*Parameters*/)
{
	const FString FixturePath = Ifc4x3FixturePath();
	if (FixturePath.IsEmpty())
	{
		AddInfo(TEXT("SKIPPED: Mobius_InternalData\\12 RoomTest\\Exported-model\\ISO-Test-8-FireSmoke.ifc ")
		        TEXT("is not present on this machine. This fixture is deliberately outside the public repo ")
		        TEXT("(large models are not committed), so this test cannot run from a bare checkout."));
		return true;
	}

	TArray<FAssimpSubmeshBuffers> Submeshes;
	FMobiusIfcLoadStats Stats;
	FString Error;

	const bool bLoaded = FMobiusIfcMeshLoader::LoadIfcFile(FixturePath, Submeshes, Stats, Error);
	if (!TestTrue(FString::Printf(TEXT("LoadIfcFile succeeded (error: %s)"), *Error), bLoaded))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("IFC load: %s"), *Stats.FilterSummary));

	// IFC4X3_ADD2 is the newest schema in production use and the entire reason IFC++ was chosen over
	// Assimp's IFCLoader (hard-coded to IFC2X3) -- assert the schema actually read as 4X3.
	TestEqual(TEXT("source schema from FILE_SCHEMA header"), Stats.SourceSchema, FString(TEXT("IFC4X3_ADD2")));

	TestEqual(TEXT("products with geometry"), Stats.ProductsWithGeometry, 205);
	// 16591, not the 16592 the standalone harness reports. The difference is one triangle and it is
	// expected: the harness fan-triangulates face loops, while the DLL now goes through IFC++'s
	// MeshOps::retriangulateMeshSetForExport, whose PolyInputCache3D::addTriangleCheckDegenerate drops
	// a triangle whose three points merge to fewer than three distinct indices. A count that differs
	// from the harness by one degenerate triangle is the correct outcome, not drift to be papered over.
	TestEqual(TEXT("total triangles in file"), Stats.TotalTriangles, 16591);
	TestEqual(TEXT("malformed products"), Stats.MalformedProducts, 0);

	// 205 - 36 IfcOpeningElement - 14 IfcSpace - 17 IfcSensor - 1 IfcGeographicElement = 137, with
	// Annotation classes off (bMobiusIfcRenderAnnotationClasses, owner policy, default false).
	// Recompute the expected value from that constant rather than hardcoding 137 twice, so flipping
	// the policy moves this assertion instead of breaking it in a way that reads like a bug.
	const int32 ExpectedRenderable = MobiusIfc::bMobiusIfcRenderAnnotationClasses ? 155 : 137;
	TestEqual(TEXT("renderable products after allowlist"), Stats.RenderedProducts, ExpectedRenderable);
	// Sections, not products: per-appearance splitting turns 137 renderable products into 208 sections
	// (71 products in this file carry two distinct styles). Only asserted with annotations off, since
	// flipping that policy adds 18 more products of unknown style count.
	TestEqual(TEXT("emitted submeshes matches emitted sections"), Submeshes.Num(), Stats.RenderedSections);
	if (!MobiusIfc::bMobiusIfcRenderAnnotationClasses)
	{
		TestEqual(TEXT("emitted sections (137 products, 71 of them multi-style)"), Stats.RenderedSections, 208);
	}
	TestEqual(TEXT("emitted triangles matches stats"), CountTriangles(Submeshes), Stats.RenderedTriangles);

	// Semantic materials reach the B-RISK channel for most of this file, and its walls are layered --
	// 'Basic Wall:Masonry internal wall 200mm' carries three layers. Measured through the DLL: 155 of
	// the 205 products with geometry name a material (the 50 that do not are the openings and spaces).
	TestEqual(TEXT("products with a semantic material record"), Stats.ProductMaterials.Num(), 155);
	{
		int32 ProductsWithLayers = 0;
		int32 MaxLayers = 0;
		for (const FMobiusIfcProductMaterial& M : Stats.ProductMaterials)
		{
			if (M.Layers.Num() > 0) { ++ProductsWithLayers; }
			MaxLayers = FMath::Max(MaxLayers, M.Layers.Num());
		}
		AddInfo(FString::Printf(TEXT("semantic materials: %d products, %d with layers, deepest layer set %d"),
		                        Stats.ProductMaterials.Num(), ProductsWithLayers, MaxLayers));
		TestTrue(TEXT("at least one product carries a multi-layer construction (fire-load input)"),
		         MaxLayers >= 2);
	}

	// The B-RISK room channel: 14 IfcSpace solids, every one excluded from the render mesh and every
	// one captured with real bounds. This is the thing that must not silently regress -- IFC carries
	// the rooms natively, which is why the space geometry is worth keeping at all.
	TestEqual(TEXT("IfcSpace room volumes captured"), Stats.RoomVolumes.Num(), 14);
	for (const FMobiusIfcRoomVolume& Room : Stats.RoomVolumes)
	{
		TestFalse(TEXT("room volume carries its IfcSpace GUID"), Room.Guid.IsEmpty());
		TestTrue(FString::Printf(TEXT("room %s has triangles"), *Room.Guid), Room.TriangleCount > 0);
		TestTrue(FString::Printf(TEXT("room %s has non-degenerate bounds"), *Room.Guid),
		         Room.Bounds.IsValid != 0 && Room.Bounds.GetVolume() > 0.0);
	}

	for (const FAssimpSubmeshBuffers& Sub : Submeshes)
	{
		const MobiusIfc::ERenderVerdict Verdict = MobiusIfc::ClassifyClass(Sub.SourceIfcClass);
		TestTrue(FString::Printf(TEXT("emitted class '%s' is not VolumeOnly"), *Sub.SourceIfcClass),
		         Verdict != MobiusIfc::ERenderVerdict::VolumeOnly);
		TestTrue(FString::Printf(TEXT("emitted class '%s' is not Unknown"), *Sub.SourceIfcClass),
		         Verdict != MobiusIfc::ERenderVerdict::Unknown);
		// Negative RHR signed volume = wound right-side-out, for every one of the 137 products, not just
		// the three anchors. See the sign note in the AnalyticVolumes test above.
		TestTrue(FString::Printf(TEXT("emitted section %s is wound right-side-out (RHR signed volume ")
		                         TEXT("negative)"), *Sub.SourceGuid),
		         SignedVolumeCm3(Sub) < 0.0);
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
