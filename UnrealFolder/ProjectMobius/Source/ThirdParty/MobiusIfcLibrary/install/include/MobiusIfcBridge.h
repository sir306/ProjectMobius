/* SPDX-License-Identifier: MIT
 *
 * MobiusIfcBridge -- pure-C ABI over IFC++ (IfcPlusPlus, MIT) + Carve (MIT) for Project Mobius.
 *
 * THIS HEADER IS THE ENTIRE CONTRACT. It is included both by MobiusIfcBridge.cpp (which also
 * includes the real IFC++/Carve C++ headers) and, unmodified, by UE C++ modules that must NEVER
 * see a std:: type, a Carve type, or a C++ exception from this boundary. Concretely:
 *
 *   - extern "C" linkage only. No C++ name mangling, no overloads, no default arguments.
 *   - POD structs and raw pointers only. No std::vector/string/shared_ptr, no Carve types,
 *     no virtual functions, no exceptions -- this header must compile as plain C (C99, for
 *     <stdint.h>) as well as C++, and must compile inside a UE module built with
 *     bEnableExceptions = false and no RTTI.
 *   - int32_t everywhere an integer crosses the boundary. Never bool (its size/ABI is not
 *     guaranteed identical between a C caller, C++17 MSVC, and a hypothetical future non-MSVC
 *     consumer of this same header).
 *   - Every function is documented with who owns what it returns and how long that memory
 *     stays valid. Read those comments before calling anything -- the two load-bearing rules
 *     are (1) nothing returned by this API is ever freed by the caller except by calling
 *     MobiusIfc_Free on the MobiusIfcScene* that produced it, and (2) every pointer a
 *     MobiusIfcProduct exposes stays valid until that same MobiusIfc_Free call, not just for
 *     the duration of the call that returned it.
 *
 * Coordinate space: every position and normal this API hands out is already in UE space --
 * left-handed, Z-up, centimetres. The IFC-space-to-UE-space conversion (right-handed Z-up metres ->
 * left-handed Z-up centimetres, one axis mirrored) happens exactly once, inside the DLL, in
 * MobiusIfcBridge.cpp's IfcToUe(). Callers never see IFC-space numbers.
 *
 * Triangle index order is the SOURCE loop order -- NOT reversed. See the `indices` field comment for
 * why, and for the consequence that a closed product's right-hand-rule signed volume is negative in
 * this space by design.
 */

#ifndef MOBIUS_IFC_BRIDGE_H
#define MOBIUS_IFC_BRIDGE_H

#include <stdint.h>

/* ---------------------------------------------------------------------------------------------
 * Export macro.
 *
 * Building the DLL itself must define MOBIUSIFC_BUILD_DLL (the CMakeLists.txt in this directory
 * does this for the MobiusIfcBridge target only). Every consumer -- including the UE
 * MobiusIfcLibrary.Build.cs staged alongside this header -- must leave MOBIUSIFC_BUILD_DLL
 * undefined, so MOBIUSIFC_API resolves to dllimport and matches the import lib it links against.
 * Do not define MOBIUSIFC_BUILD_DLL from a consuming module; MobiusIfcLibrary.Build.cs already
 * has a comment recording this same rule from the other side of the boundary.
 * ------------------------------------------------------------------------------------------- */
#if defined(_WIN32)
	#if defined(MOBIUSIFC_BUILD_DLL)
		#define MOBIUSIFC_API __declspec(dllexport)
	#else
		#define MOBIUSIFC_API __declspec(dllimport)
	#endif
#elif defined(__GNUC__) || defined(__clang__)
	#if defined(MOBIUSIFC_BUILD_DLL)
		#define MOBIUSIFC_API __attribute__((visibility("default")))
	#else
		#define MOBIUSIFC_API
	#endif
#else
	#define MOBIUSIFC_API
#endif

/* ---------------------------------------------------------------------------------------------
 * ABI version guard.
 *
 * MOBIUSIFC_ABI_VERSION is a compile-time constant baked into whichever copy of this header a
 * translation unit includes. MobiusIfc_GetAbiVersion() returns the constant the DLL itself was
 * BUILT against. A caller should compare the two BEFORE calling anything else:
 *
 *     if (MobiusIfc_GetAbiVersion() != MOBIUSIFC_ABI_VERSION) { refuse to use this DLL; }
 *
 * This project has previously shipped a stale DLL and gotten a false-green test result out of
 * it (see CLAUDE.md / auto-memory project_mcpbridge_bin_builds and the reference-mobius-stale-dll
 * note) -- a header/DLL mismatch here must be loud and immediate, not a silent struct-layout
 * mismatch discovered by a crash three calls later. Bump this value on ANY change to a struct
 * layout, a function signature, or an enum value in this file, however small.
 * ------------------------------------------------------------------------------------------- */
/* v2 (2026-08-12): appearance and material data added. A product is now a LIST OF SECTIONS, one per
 * distinct appearance, instead of one flat vertex buffer -- IFC attaches styles per geometric item, so
 * a window with a brown frame and clear glass genuinely needs two draw ranges. MobiusIfcProduct's
 * layout changed (flat vertices/normals/indices removed, sections/layers/materialName added), which is
 * exactly the struct-layout change this version guard exists for. */
#define MOBIUSIFC_ABI_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================================================
 * Result codes
 * ===============================================================================================*/

/* Every MobiusIfc_* function that can fail returns one of these, widened to int32_t at the
 * function boundary (see header preamble: no bare `enum` return type is used across the ABI,
 * because C leaves an enum's underlying integer size implementation-defined -- only the named
 * constants are meant to be portable, not the enum type itself). */
typedef enum MobiusIfcResult
{
	MOBIUSIFC_OK                      = 0, /* success */
	MOBIUSIFC_ERR_INVALID_ARGUMENT    = 1, /* null pointer, empty string, or out-of-range value passed in */
	MOBIUSIFC_ERR_FILE_NOT_FOUND      = 2, /* path does not exist or is not readable */
	MOBIUSIFC_ERR_PARSE_FAILED        = 3, /* STEP/IFC parse failed (malformed file, unsupported schema, reader exception) */
	MOBIUSIFC_ERR_GEOMETRY_FAILED     = 4, /* geometry/CSG conversion threw (carve::exception or std::exception) */
	MOBIUSIFC_ERR_OUT_OF_MEMORY       = 5, /* std::bad_alloc caught while parsing or converting */
	MOBIUSIFC_ERR_ABI_MISMATCH        = 6, /* CALLER-DETECTED ONLY: MobiusIfc_GetAbiVersion() != caller's MOBIUSIFC_ABI_VERSION.
	                                         The DLL itself never returns this code -- it cannot know what header the
	                                         caller compiled against. This constant exists purely so the caller has a
	                                         named value to log/return after doing that comparison itself. */
	MOBIUSIFC_ERR_UNKNOWN             = 7  /* caught `...` with no further diagnosis possible */
} MobiusIfcResult;

/* Returns a static, non-null, English diagnostic string for any MobiusIfcResult value (or any
 * int32_t at all -- unrecognized codes get a generic "unrecognized" string rather than undefined
 * behaviour). The returned pointer is to static storage: valid for the life of the process, never
 * freed, safe to call before any MobiusIfcScene exists. */
MOBIUSIFC_API const char* MobiusIfc_ErrorString(int32_t resultCode);

/* Returns the MOBIUSIFC_ABI_VERSION this DLL was compiled with. See "ABI version guard" above --
 * call this and compare against your own compiled-in MOBIUSIFC_ABI_VERSION before calling
 * anything else in this header. Never fails, never allocates. */
MOBIUSIFC_API uint32_t MobiusIfc_GetAbiVersion(void);

/* =================================================================================================
 * Scene handle
 * ===============================================================================================*/

/* Opaque. The real definition lives only in MobiusIfcBridge.cpp. Never dereference this, never
 * take its sizeof(), never construct one yourself -- the only way to get a valid MobiusIfcScene*
 * is a successful MobiusIfc_Load() call, and the only way to release one is MobiusIfc_Free(). */
typedef struct MobiusIfcScene MobiusIfcScene;

/* ---------------------------------------------------------------------------------------------
 * MobiusIfc_Load
 *
 * Parses the IFC file at utf8Path (STEP physical file, any schema IFC++ recognises -- see
 * HANDOFF_IFC_2026-08-11.md Level 4 for what has and has not been exercised) and converts its
 * geometry. Never throws past this boundary: every IFC++/Carve exception (BuildingException,
 * UnknownEntityException, carve::exception, and anything else derived or not derived from
 * std::exception) is caught inside this function and converted to a MobiusIfcResult.
 *
 * Parameters:
 *   utf8Path   - UTF-8, NUL-terminated path to the .ifc file. Must not be null or empty.
 *   outScene   - Must not be null. On success, *outScene receives a new scene handle that must
 *                eventually be released with MobiusIfc_Free(). On any failure, *outScene is set
 *                to NULL -- callers do not need to (and must not) free anything after a failed
 *                Load.
 *   errBuf     - Optional. If non-null and errBufLen > 0, a NUL-terminated, UTF-8, English
 *                diagnostic message is written here (truncated to fit, always NUL-terminated).
 *                Only meaningful when the return value is non-zero; on success this buffer's
 *                first byte is cleared to '\0' and nothing further is written to it. Pass NULL
 *                (with any errBufLen) if you don't want diagnostics.
 *   errBufLen  - Size of errBuf in bytes, including room for the terminator. Ignored if errBuf
 *                is null.
 *
 * Returns a MobiusIfcResult (widened to int32_t). MOBIUSIFC_OK means *outScene is valid and
 * ready to query. Note that a structurally valid IFC file that happens to contain zero
 * renderable products (e.g. a file with only spatial containers) is NOT a failure -- it returns
 * MOBIUSIFC_OK with MobiusIfc_GetProductCount() == 0. Check the count explicitly if you need to
 * distinguish that case.
 *
 * Threading: IFC++'s thread-safety has not been independently verified by this project (see
 * HANDOFF_IFC_2026-08-11.md 7.4) -- do not call MobiusIfc_Load concurrently with itself or with
 * any other MobiusIfc_* call against a scene that is still being built. A single scene, once
 * returned, is safe to read from multiple threads (nothing in this API mutates a scene after
 * MobiusIfc_Load returns).
 * ------------------------------------------------------------------------------------------- */
MOBIUSIFC_API int32_t MobiusIfc_Load(const char* utf8Path, MobiusIfcScene** outScene,
                                      char* errBuf, int32_t errBufLen);

/* Releases a scene and every buffer any MobiusIfcProduct from it ever pointed to. Safe to call
 * with scene == NULL (no-op). After this call, every pointer previously obtained via
 * MobiusIfc_GetProducts() for this scene is dangling -- do not keep them around. Never throws. */
MOBIUSIFC_API void MobiusIfc_Free(MobiusIfcScene* scene);

/* =================================================================================================
 * Product data
 * ===============================================================================================*/

/* Axis-aligned bounding box, UE space (centimetres). 24 bytes, 4-byte aligned, no padding. */
typedef struct MobiusIfcAabb
{
	float minX, minY, minZ;
	float maxX, maxY, maxZ;
} MobiusIfcAabb;

/* =================================================================================================
 * Appearance -- the VISUAL material, from IfcStyledItem -> IfcSurfaceStyle -> IfcSurfaceStyleRendering
 * -> IfcColourRGB, as parsed by IFC++'s StylesConverter into its StyleData.
 *
 * This is deliberately a small fixed set of channels rather than a general material model: IFC's
 * presentation styles can express far more (textures, reflectance methods, two-sided styles), and a
 * consumer that wanted all of it would be better served by reading the IFC itself. What is here is
 * what a runtime viewer needs to make a building look like the source rather than like one flat colour.
 *
 * 24 bytes, 4-byte aligned, no padding.
 * ===============================================================================================*/
typedef struct MobiusIfcAppearance
{
	/* Diffuse colour, 0..1 per channel, straight from IfcColourRGB (which is defined as normalised
	 * ratios). NOT gamma-corrected or otherwise transformed -- IFC does not say what colour space it
	 * is in, so converting would be inventing information. Meaningless when bHasAppearance == 0. */
	float diffuseR, diffuseG, diffuseB;

	/* 1 = fully opaque, 0 = fully transparent. This is 1 - IfcSurfaceStyleRendering's Transparency,
	 * inverted here because every renderer this feeds thinks in opacity and doing the flip once, at
	 * the boundary, is one less place to get it backwards. */
	float opacity;

	/* IfcSurfaceStyleRendering's SpecularHighlight when it is an IfcSpecularExponent, else 0.
	 *
	 * This reads IFC++'s StyleData::m_specular_exponent, NOT its m_shininess. That distinction was
	 * measured, not assumed: m_shininess is computed inside StylesConverter as
	 * m_specular_roughness * 128, so for a file that specifies an exponent rather than a roughness --
	 * which both Mobius test files do, e.g. IFCSPECULAREXPONENT(64.) -- m_shininess carries a value
	 * unrelated to anything in the source (it read 1.0 while the file said 64). An earlier version of
	 * this field read m_shininess and this comment claimed it was the exponent; it was wrong, and it was
	 * caught only because the harness REPORTED this value instead of asserting a guessed mapping.
	 *
	 * Passed through unscaled: it is a Phong exponent, and mapping it onto a PBR roughness is a decision
	 * for whoever owns the material, not for this ABI. 0 means the style gave no specular highlight, or
	 * gave one as a roughness this field does not carry. */
	float specularExponent;

	/* 0 means NO style was found for this section -- neither on its geometric item nor inherited from
	 * its product. The other fields are then undefined and the caller MUST fall back to its own
	 * material rather than rendering black. Do not treat "no appearance" as "black": plenty of real
	 * IFC files style only some of their products. int32_t, never bool, per this header's convention. */
	int32_t bHasAppearance;
} MobiusIfcAppearance;

/* =================================================================================================
 * One draw range of a product: a triangle list plus the appearance that applies to it.
 *
 * WHY PRODUCTS ARE SPLIT INTO SECTIONS (new in ABI v2): IFC attaches styles to individual geometric
 * items, not to whole products. A window is one product whose frame and glazing are separate items
 * with different IfcSurfaceStyles, so a single flat buffer per product cannot express it. Sections are
 * grouped by DISTINCT APPEARANCE within a product, not one-per-item: an item that shares its
 * neighbour's colour shares its section, which keeps the count at 1 for the overwhelming majority of
 * products and matters because the consumer turns each section into a separate draw call.
 *
 * Layout: 3 pointers (24) + 4 int32_t (16) + MobiusIfcAppearance (24) = 64 bytes, no padding.
 * ===============================================================================================*/
typedef struct MobiusIfcSection
{
	/* Interleaved xyz positions, UE space (cm). Length = vertCount * 3 floats. Never NULL when
	 * vertCount > 0. Vertices are NOT welded -- every triangle owns its own three vertex slots so its
	 * flat normal can never be wrong from averaging with a neighbouring face at a different angle.
	 * This costs vertex count; it buys correctness with zero smoothing-group bookkeeping. */
	const float* vertices;

	/* Interleaved xyz unit normals, parallel to `vertices`. Each is the flat face normal of the
	 * triangle that corner belongs to, derived from that same triangle's already-converted positions,
	 * so a normal cannot disagree with the geometry it came from. A degenerate/zero-area source
	 * triangle yields an exact (0,0,0) sentinel rather than NaN -- treat it as "renormalize or
	 * discard". Never NULL when vertCount > 0. */
	const float* normals;

	/* Triangle list, indices local to THIS SECTION (0-based, range [0, vertCount)). Never NULL when
	 * indexCount > 0.
	 *
	 * WINDING, corrected 2026-08-12 -- this comment previously claimed "CCW as seen from outside,
	 * already reversed", and both the claim and the reversal were wrong. Faces rendered inside-out.
	 * The order handed out is the SOURCE loop order with exactly one axis mirror applied to the
	 * positions and NO index reversal, which is the convention that renders correctly in
	 * UProceduralMeshComponent -- established from this project's own Assimp path, which passes
	 * aiProcess_MakeLeftHanded (mirrors an axis, leaves indices alone) and never
	 * aiProcess_FlipWindingOrder.
	 *
	 * Consequence for anyone validating this data: the right-hand-rule signed volume of a closed
	 * product is NEGATIVE in this space, and that is CORRECT. A positive RHR signed volume means
	 * someone reintroduced an index reversal and the mesh is inside-out. Compare magnitudes against
	 * analytic volumes, and assert the sign as negative. */
	const int32_t* indices;

	int32_t vertCount;  /* vertices, not floats -- multiply by 3 for the buffer lengths */
	int32_t indexCount; /* indices, not triangles */
	int32_t triCount;   /* == indexCount / 3, precomputed */
	int32_t reserved0;  /* explicit, so the 64-byte size is a stated fact rather than a padding accident */

	MobiusIfcAppearance appearance;
} MobiusIfcSection;

/* =================================================================================================
 * One layer of a product's SEMANTIC material -- IfcMaterialLayerSet / IfcMaterialConstituentSet /
 * IfcMaterialList, reached through IfcRelAssociatesMaterial.
 *
 * This is a different channel from appearance and is NOT about rendering. It is what makes an IFC
 * import useful to the B-RISK side of Mobius: a named material with a thickness is a fire-load and
 * thermal input, e.g. "Concrete, 200 mm". A colour cannot tell you that.
 *
 * 16 bytes.
 * ===============================================================================================*/
typedef struct MobiusIfcMaterialLayer
{
	/* IfcMaterial's Name, UTF-8, NUL-terminated, never NULL ("" if the entity carried no name). */
	const char* name;

	/* Layer thickness in CENTIMETRES, unit-converted from the file's own length unit via IFC++'s
	 * UnitConverter -- cm to match every other length in this ABI. 0 when the source carried no
	 * thickness, which is normal for IfcMaterialList and IfcMaterialConstituentSet entries: those
	 * name materials without layering them, so 0 means "unknown", never "zero-thickness". */
	float thicknessCm;

	int32_t reserved0;
} MobiusIfcMaterialLayer;

/* One IFC product (an IfcWall, IfcDoor, IfcSpace, ... -- one entry per GUID that produced at
 * least one triangle; entities IFC++ gave zero geometry, such as IfcProject/IfcBuildingStorey/
 * *Type/*Style definitions, do not appear here at all -- see MobiusIfc_GetProductsWithoutGeometryCount).
 *
 * Ownership and lifetime: every pointer in this struct, and every pointer inside the sections and
 * layers it points to, is owned by the MobiusIfcScene that produced it (via MobiusIfc_GetProducts) and
 * stays valid until that scene's MobiusIfc_Free() call. The caller never frees any of it individually.
 *
 * Layout (MSVC x64, the only configuration this project builds for): 5 pointers (40) + 6 int32_t (24)
 * + MobiusIfcAabb (24) = 88 bytes, no padding. MobiusIfcBridge.cpp static_asserts this exact size, so
 * an edit that silently changes the layout fails to compile instead of shipping a struct whose
 * documented size is a lie.
 *
 * Rendering policy is NOT baked into this struct beyond the bRenderable flag described below --
 * see ifcClass for how a caller builds its own allowlist. */
typedef struct MobiusIfcProduct
{
	/* --- 8-byte-aligned pointers first --- */

	/* sectionCount entries. Never NULL when sectionCount > 0. Geometry lives HERE, not on the product:
	 * see MobiusIfcSection for why a product is a list of draw ranges rather than one buffer. */
	const MobiusIfcSection* sections;

	/* layerCount entries, outermost-first in the order the IFC layer set declares them. NULL when
	 * layerCount == 0, which is normal -- plenty of products carry no material association at all. */
	const MobiusIfcMaterialLayer* layers;

	/* IFC IfcGloballyUniqueId (22-character base64 GUID per the IFC spec), UTF-8, NUL-terminated.
	 * Never NULL -- an empty string "" only if the source entity somehow had no GlobalId, which
	 * should not happen for any IfcRoot-derived entity but is defended against rather than
	 * assumed impossible. */
	const char* guid;

	/* IFC entity class name exactly as IFC++'s EntityFactory names it, e.g.
	 * "IfcWallStandardCase", "IfcSpace", "IfcOpeningElement", "IfcWindow". UTF-8, NUL-terminated,
	 * never NULL. This is the ONLY signal this API gives you to decide what to render: the DLL
	 * deliberately does not filter by class (see handoff HANDOFF_IFC_2026-08-11.md 8.1 -- an
	 * allowlist belongs to the caller, because a denylist baked into the DLL would silently
	 * admit every new IFC class a future file introduces). In particular: IfcOpeningElement and
	 * IfcSpace both carry real, watertight, solid geometry here (void volumes and room volumes
	 * respectively) and WILL appear in this array with bRenderable = 1 -- they are not filtered
	 * out. Render them and you get solid blocks filling every door, window, and room. Build your
	 * allowlist against this field before handing a section's buffers to a mesh builder. */
	const char* ifcClass;

	/* Primary SEMANTIC material name for the product -- the IfcMaterial's Name, or the layer set's
	 * LayerSetName when the association is a layer set, or the first constituent's material name.
	 * UTF-8, NUL-terminated, never NULL ("" when the product has no material association).
	 *
	 * This is metadata, NOT the thing to render with; `sections[i].appearance` is what to render with.
	 * A product can legitimately have a material name and no appearance, or an appearance and no
	 * material name -- the two channels are independent in IFC and are kept independent here. Use
	 * `layers` when you need the full set with thicknesses rather than just the headline name. */
	const char* materialName;

	/* --- 4-byte members --- */

	int32_t sectionCount; /* draw ranges in `sections`; >= 1 for any product that produced geometry */
	int32_t layerCount;   /* entries in `layers`; 0 is normal and means "no material association" */

	int32_t vertCount;    /* TOTAL vertices summed across sections -- for sanity checks and budgeting,
	                       * not for indexing: each section's indices are local to that section */
	int32_t indexCount;   /* TOTAL indices summed across sections */
	int32_t triCount;     /* TOTAL triangles summed across sections (== indexCount / 3) */

	/* Nonzero iff triCount > 0. This is a PURELY GEOMETRIC signal ("did IFC++ give this product
	 * any triangles at all"), not a semantic/allowlist decision -- it exists so a caller can skip
	 * empty or degenerate products cheaply without re-deriving "vertCount == 0" itself. It says
	 * nothing about whether the class in `ifcClass` belongs in a render mesh (an IfcSpace with
	 * geometry has bRenderable == 1 and still needs to be excluded by the caller's allowlist).
	 * int32_t per this project's C-ABI convention: never a bool across this boundary. */
	int32_t bRenderable;

	/* --- trailing 24-byte POD struct, 4-byte aligned --- */
	MobiusIfcAabb aabb; /* UE space (cm), axis-aligned bounding box of this product's geometry */
} MobiusIfcProduct;

/* Returns the number of entries MobiusIfc_GetProducts() would return, without needing a scratch
 * pointer. Returns 0 for a NULL scene (not an error -- there is no errBuf on this call to explain
 * why, so NULL and "genuinely empty" are treated identically on purpose). */
MOBIUSIFC_API int32_t MobiusIfc_GetProductCount(const MobiusIfcScene* scene);

/* ---------------------------------------------------------------------------------------------
 * MobiusIfc_GetProducts
 *
 * Returns every product in the scene as ONE contiguous, cache-friendly array in a single call
 * (chosen over a per-index MobiusIfc_GetProduct(scene, i, ...) accessor -- see NOTES.md "struct
 * array vs per-index" for the reasoning: one call, one array to walk when building UE mesh
 * sections, no N-call overhead for an N-product IFC file that can run into the hundreds).
 *
 * *outProducts is set to a pointer to the scene's internal array (owned by the scene; valid
 * until MobiusIfc_Free) and *outCount to its length. Both are set to NULL/0 if scene is NULL --
 * that is not an error (see MobiusIfc_GetProductCount for the same reasoning). It IS an error
 * (MOBIUSIFC_ERR_INVALID_ARGUMENT) to pass outProducts or outCount as NULL, since those are
 * output parameters the caller must supply a writable location for.
 * ------------------------------------------------------------------------------------------- */
MOBIUSIFC_API int32_t MobiusIfc_GetProducts(const MobiusIfcScene* scene,
                                             const MobiusIfcProduct** outProducts, int32_t* outCount);

/* Number of IFC entities in the source file that IFC++ produced literally zero triangles for
 * (IfcProject, IfcSite, IfcBuilding, IfcBuildingStorey, *Type/*Style definitions, and similar --
 * see HANDOFF_IFC_2026-08-11.md 5.1). This is NOT related to the allowlist/ifcClass filtering
 * described above -- it is purely "IFC++ itself gave this entity no shape", counted before any
 * caller-side rendering policy is applied. Useful as a sanity check against the harness's own
 * "products WITH/WITHOUT geometry" split. Returns 0 for a NULL scene. */
MOBIUSIFC_API int32_t MobiusIfc_GetProductsWithoutGeometryCount(const MobiusIfcScene* scene);

#ifdef __cplusplus
}
#endif

#endif /* MOBIUS_IFC_BRIDGE_H */
