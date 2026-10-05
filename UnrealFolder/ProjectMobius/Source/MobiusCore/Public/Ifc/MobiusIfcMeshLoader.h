// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

// ============================================================================================
// Runtime IFC import for Project Mobius.
//
// This is the UE-side half of the IFC path. The other half is MobiusIfcBridge.dll -- our own
// extern "C" shim over IFC++ (IfcPlusPlus, MIT) + Carve (MIT), built from
// Source\ThirdParty\IfcBridgeSource\ and consumed through the MobiusIfcLibrary external module.
// Full evidence trail: _CurrentHandoff\HANDOFF_IFC_2026-08-11.md.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO
//   - It does not include MobiusIfcBridge.h. The C ABI stays private to MobiusIfcMeshLoader.cpp so
//     that no other MobiusCore translation unit can start depending on the DLL's header (and so a
//     module that consumes this loader needs no include path into the external module).
//   - It does not go through DatasmithRuntime. DatasmithRuntime cannot translate IFC in a packaged
//     build -- its CAD backend (HOOPS/TechSoft) lives in Engine\Restricted\NotForLicensees and is
//     absent from a licensee install. That is the whole reason this loader exists; see the handoff
//     section 1.
//
// Coordinate space: MobiusIfcBridge.dll hands out vertices and normals ALREADY in UE space --
// left-handed, Z-up, centimetres, winding reversed to stay CCW-outward after the mirror. The
// conversion is proven in the standalone harness AND in the shipping DLL (four independent lenses
// pass on both test files; five wrong-but-plausible negative controls all fail -- handoff 13.2).
// Nothing in this file re-scales, re-mirrors or re-winds anything, and nothing here should ever
// start doing so: a second conversion is how the proof silently stops applying.
// ============================================================================================

#include "CoreMinimal.h"

/** Per-submesh buffer struct the whole procedural-mesh path already speaks (AsyncAssimpMeshLoader.h). */
struct FAssimpSubmeshBuffers;

/**
 * An IfcSpace room volume. IfcSpace carries real, watertight, solid geometry -- render it and every
 * room becomes an opaque block -- so it is excluded from render meshes by the allowlist. It is
 * captured here instead because it is the native room-polygon source for the B-RISK work
 * (agent->room point-in-polygon, floor separation), which currently derives rooms elsewhere.
 *
 * Bounds only, for now. Extracting the actual floor polygon (project the solid's lowest horizontal
 * face to 2D, weld, order the loop) is deliberately NOT done here yet -- it is a separate piece of
 * work with its own correctness bar, and an AABB per space is enough to prove the door is open and
 * to cross-check counts against the existing B-RISK room set. See the handoff section 9.
 */
struct FMobiusIfcRoomVolume
{
	/** IfcGloballyUniqueId (22-char base64) of the IfcSpace entity. */
	FString Guid;

	/** Axis-aligned bounds in UE space (cm). */
	FBox Bounds = FBox(ForceInit);

	/** Triangles the space's solid was made of. Non-zero for every space IFC++ gave geometry. */
	int32 TriangleCount = 0;
};

/**
 * Visual material for one mesh section, as authored in the source file.
 *
 * Filled by BOTH import paths: from IFC's IfcSurfaceStyleRendering/IfcColourRGB, and from assimp's
 * aiMaterial for fbx/obj. That is the point of putting it here rather than in either loader -- one
 * struct, one consumer, so a colour behaves the same whichever format it came from.
 *
 * bHasMaterial == false means the source said nothing about appearance, which is common and is NOT the
 * same as "black": the consumer must keep its own material in that case.
 */
struct FMobiusMeshMaterial
{
	/** Source's material/style name, for display and filtering. Empty when unnamed. */
	FString Name;

	/** Diffuse/base colour, 0..1 per channel, as authored. Alpha carries opacity (1 = opaque). */
	FLinearColor BaseColour = FLinearColor::White;

	/** Phong specular exponent where the source gave one, else 0. Not a PBR roughness. */
	float SpecularExponent = 0.0f;

	/** False when the source carried no appearance at all -- do not render this as black. */
	bool bHasMaterial = false;
};

/** IFC provenance of one emitted mesh section. Parallel array to the emitted section list. */
struct FMobiusIfcSectionInfo
{
	/** IfcGloballyUniqueId of the product this section's triangles came from. */
	FString Guid;

	/** IFC entity class name exactly as IFC++ reports it, e.g. "IfcWallStandardCase". */
	FString IfcClass;

	/** Semantic material name (IfcMaterial / layer set), independent of the visual appearance. */
	FString MaterialName;

	/** The appearance this section was drawn with, or bHasMaterial == false if the source had none. */
	FMobiusMeshMaterial Material;
};

/**
 * One layer of a product's semantic material -- IfcMaterialLayerSet / IfcMaterialConstituentSet.
 *
 * This is the fire-load / thermal channel, not the rendering one: "Concrete, 200 mm" is an input to
 * B-RISK, which a colour cannot provide. ThicknessCm is 0 when the source named a material without
 * layering it (IfcMaterialList, IfcMaterialConstituentSet), meaning "unknown", never "zero".
 */
struct FMobiusIfcMaterialLayer
{
	FString Name;
	float ThicknessCm = 0.0f;
};

/** Per-product semantic material record, keyed by the product's IFC GUID. */
struct FMobiusIfcProductMaterial
{
	FString Guid;
	FString IfcClass;

	/** Headline name: the IfcMaterial's name, or the layer set's LayerSetName. */
	FString MaterialName;

	/** Full layer set, outermost-first, with thicknesses where the source gave them. */
	TArray<FMobiusIfcMaterialLayer> Layers;
};

/**
 * Everything a load produced that is not vertex data. Owned by the caller; filled by LoadIfcFile.
 * The counts exist to be asserted against: the two test files are pinned in the handoff
 * (sections 5.1, 5.4, 13.4) and in ProjectMobiusTests.
 */
struct FMobiusIfcLoadStats
{
	/**
	 * Schema string read out of the file's own FILE_SCHEMA(('...')) header, e.g. "IFC2X3" or
	 * "IFC4X3_ADD2". Read directly from the text, NOT from IFC++'s
	 * getIfcSchemaVersionOfLoadedFile(), which reports IFC4X3 for an IFC2X3 file (a cosmetic upstream
	 * bug -- handoff 8.2). Recorded for diagnostics only: nothing branches on it.
	 */
	FString SourceSchema;

	/** Products IFC++ produced at least one triangle for, before any render filtering. */
	int32 ProductsWithGeometry = 0;

	/** Entities IFC++ produced zero triangles for (IfcProject, IfcSite, *Type / *Style, ...). */
	int32 ProductsWithoutGeometry = 0;

	/** Products that survived the allowlist and produced at least one submesh. */
	int32 RenderedProducts = 0;

	/**
	 * Submeshes emitted. >= RenderedProducts, because a product is split into one submesh per distinct
	 * appearance — a window's frame and glazing are separate draw ranges. Worth watching: each becomes
	 * its own ProcMesh section, and the emit pump pushes one section per frame.
	 */
	int32 RenderedSections = 0;

	/** Triangles across the emitted submeshes only. */
	int32 RenderedTriangles = 0;

	/** Triangles across every product with geometry, filtered or not. Matches the harness total. */
	int32 TotalTriangles = 0;

	/** Products dropped because their index data was out of range (should always be zero). */
	int32 MalformedProducts = 0;

	/** Every IfcSpace with geometry, kept for the B-RISK room work. Never rendered. */
	TArray<FMobiusIfcRoomVolume> RoomVolumes;

	/**
	 * Semantic material record for every product that named one or carried layers — INCLUDING products
	 * the render allowlist drops, because an IfcSpace's or an opening's material is still data. This is
	 * the fire-load / thermal channel for B-RISK, not the rendering one.
	 */
	TArray<FMobiusIfcProductMaterial> ProductMaterials;

	/** FMobiusIfcClassStats::Summarize() -- the ONE line the caller logs after the load. */
	FString FilterSummary;
};

/**
 * Loads an .ifc file into the same per-submesh buffers the Assimp path produces, so the rest of the
 * mesh pipeline (SplitSubmeshByTriCap -> staggered CreateMeshSection_LinearColor emit) is shared
 * verbatim rather than duplicated.
 *
 * Threading: safe to call from a worker thread and intended to be (FAssimpMeshLoaderRunnable calls
 * it from its Run()). NOT safe to call concurrently with itself -- IFC++'s thread-safety is
 * unverified (handoff 7.4), so one load at a time. Nothing here touches a UObject or the game
 * thread; the caller marshals the finished buffers back itself.
 *
 * No UE_LOG in any per-product path (project rule). Diagnostics come back through FMobiusIfcLoadStats
 * and OutError for the caller to log once.
 */
class MOBIUSCORE_API FMobiusIfcMeshLoader
{
public:
	/**
	 * Parse + convert one IFC file.
	 *
	 * @param PathToIfc    Absolute path to the .ifc file.
	 * @param OutSubmeshes Reset then filled: one entry per RENDERED product, vertices/normals in UE
	 *                     space (cm), submesh-local indices, SourceGuid/SourceIfcClass populated.
	 *                     UV is left empty, exactly as the Assimp path leaves it (see the .cpp).
	 * @param OutStats     Reset then filled. Valid even when the return value is false, as far as the
	 *                     failure got (SourceSchema is populated before the DLL is called at all).
	 * @param OutError     Human-readable failure reason. Empty on success.
	 * @return true only if the file parsed, converted, and produced at least one renderable submesh.
	 */
	static bool LoadIfcFile(const FString& PathToIfc, TArray<FAssimpSubmeshBuffers>& OutSubmeshes,
	                        FMobiusIfcLoadStats& OutStats, FString& OutError);

	/**
	 * Reads the STEP header's FILE_SCHEMA(('...')) token straight out of the file text.
	 *
	 * Why this exists rather than trusting the library: IFC++'s getIfcSchemaVersionOfLoadedFile()
	 * returns IFC4X3 for a file whose header says IFC2X3 (handoff 8.2), so it must never be branched
	 * on. This also doubles as a cheap "is this actually an IFC file" gate BEFORE the DLL is asked to
	 * parse it -- MobiusIfc_Load's own zero-entity check catches binary garbage, but this catches it
	 * earlier and says which schema was found.
	 *
	 * @return false if the file could not be read or contains no FILE_SCHEMA token naming an IFC schema.
	 */
	static bool ReadFileSchema(const FString& PathToIfc, FString& OutSchema, FString& OutError);

	/**
	 * Explicitly loads MobiusIfcBridge.dll and caches the handle for the process.
	 *
	 * MobiusIfcLibrary.Build.cs uses PublicDelayLoadDLLs, so MSVC's delay-load thunk would load the
	 * DLL by itself on the first MobiusIfc_* call -- but if it is not findable at that moment the
	 * thunk raises a Win32 SEH exception (ERROR_MOD_NOT_FOUND) at the call site, which is not a C++
	 * exception and is not catchable in a module built with bEnableExceptions = false. Loading it
	 * explicitly turns "missing DLL" into one clean error string instead of a crash on first use.
	 * This mirrors what the existing UE4_Assimp integration does.
	 *
	 * Called from FMobiusCoreModule::StartupModule (so the failure is visible at startup) and again,
	 * idempotently, at the top of LoadIfcFile (so a load never depends on startup ordering).
	 * Thread-safe; the first caller wins and every later caller gets the cached result.
	 */
	static bool EnsureBridgeLoaded(FString& OutError);
};
