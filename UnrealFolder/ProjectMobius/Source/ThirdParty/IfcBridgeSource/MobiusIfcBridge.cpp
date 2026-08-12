// SPDX-License-Identifier: MIT
//
// MobiusIfcBridge.cpp -- implementation of the pure-C ABI declared in MobiusIfcBridge.h.
//
// This file is C++ internally (it has to be -- IFC++ is a C++ template/shared_ptr library) but
// every function actually exported across the DLL boundary is extern "C", takes/returns only POD
// and raw pointers, and is wrapped so that no exception -- IFC++'s own (BuildingException,
// UnknownEntityException), Carve's (carve::exception, which does NOT derive from std::exception --
// see the comment on that below, it is not a typo), or a plain std::exception/std::bad_alloc --
// can ever unwind past this file. No Mobius UE module enables C++ exceptions; an escaping
// exception here is a hard crash, not a caught error.
//
// The IFC++ call sequence below (BuildingModel -> ReaderSTEP::loadModelFromFile -> GeometrySettings
// -> GeometryConverter(model, settings) two-arg constructor -> setCsgEps -> convertGeometry() ->
// getShapeInputData()) is copied verbatim from the empirically verified harness at
// _CurrentHandoff/tools/ifcvalidate/ifcvalidate_main.cpp. Every signature used here was additionally
// checked against the real IFC++ headers in the cloned source tree before being used -- see NOTES.md
// for the specific files read and what, if anything, disagreed with the harness or the handoff.

#include "MobiusIfcBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
	// For MultiByteToWideChar / GetFileAttributesW / GetShortPathNameW -- the path-encoding block in
	// MobiusIfc_Load needs the wide-char file API because MSVC's ifstream(char*) is ANSI, not UTF-8.
	//
	// NOMINMAX IS LOAD-BEARING, DO NOT REMOVE IT. Without it, windef.h defines min/max as function
	// macros and Carve fails to compile with a wall of syntax errors far from this line --
	// carve/collection_types.hpp:40 and carve/mesh_impl.hpp:887 use std::numeric_limits<T>::min()
	// and ::max(), which the macros mangle into "illegal token on right side of '::'".
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
#endif

// Real IFC++ headers. Include order matches the verified harness; do not reorder or trim this
// list without re-checking that the transitive includes it relies on (GeometryConverter.h pulls
// in GeometryInputData.h, IncludeCarveHeaders.h, and IFC4X3::EntityFactory.h itself) still hold.
#include <ifcpp/IFC4X3/include/IfcObjectDefinition.h>
#include <ifcpp/IFC4X3/include/IfcProduct.h>
#include <ifcpp/model/BuildingModel.h>
#include <ifcpp/model/BuildingException.h>
#include <ifcpp/reader/ReaderSTEP.h>
#include <ifcpp/geometry/GeometryConverter.h>
// MeshOps::retriangulateMeshSetForExport + PolyInputCache3D: IFC++'s own export triangulation, the
// same pair its ConverterOSG viewer uses. NOT reachable transitively through GeometryConverter.h --
// omitting these gives "'MeshOps': is not a class or namespace name" at the call site.
#include <ifcpp/geometry/MeshOps.h>
#include <ifcpp/geometry/PolyInputCache3D.h>
// Semantic-material traversal (ABI v2): IfcObjectDefinition's inverse HasAssociations ->
// IfcRelAssociatesMaterial -> the IfcMaterialSelect branches this shim handles. Each of these is an
// explicit include because GeometryConverter.h pulls in the geometry side of the schema, not the
// material side.
#include <ifcpp/IFC4X3/include/IfcRelAssociates.h>
#include <ifcpp/IFC4X3/include/IfcRelAssociatesMaterial.h>
#include <ifcpp/IFC4X3/include/IfcMaterial.h>
#include <ifcpp/IFC4X3/include/IfcMaterialConstituent.h>
#include <ifcpp/IFC4X3/include/IfcMaterialConstituentSet.h>
#include <ifcpp/IFC4X3/include/IfcMaterialLayer.h>
#include <ifcpp/IFC4X3/include/IfcMaterialLayerSet.h>
#include <ifcpp/IFC4X3/include/IfcMaterialLayerSetUsage.h>
#include <ifcpp/IFC4X3/include/IfcMaterialList.h>
#include <ifcpp/IFC4X3/include/IfcLabel.h>
#include <ifcpp/IFC4X3/include/IfcNonNegativeLengthMeasure.h>

using namespace IFC4X3;

// ---------------------------------------------------------------------------------------------
// Compile-time layout guards. These exist so a future edit that accidentally changes
// MobiusIfcProduct's or MobiusIfcAabb's layout (reordering a field, widening a type, adding a
// member without updating the header's own size comment) fails the DLL build instead of silently
// shipping a struct whose documented 80-byte size is wrong. This is a paranoia check, not a
// correctness requirement: since the DLL and every consumer include the exact same header and are
// built for the same target (MSVC x64), the two sides can never disagree on the real layout by
// construction. It guards against drift between the header's own doc comment and its own struct.
// ---------------------------------------------------------------------------------------------
static_assert(sizeof(MobiusIfcAabb) == 24, "MobiusIfcAabb layout drifted -- update the size comment in MobiusIfcBridge.h");
static_assert(sizeof(MobiusIfcAppearance) == 24, "MobiusIfcAppearance layout drifted -- update the size comment in MobiusIfcBridge.h");
static_assert(sizeof(MobiusIfcSection) == 64, "MobiusIfcSection layout drifted -- update the size comment in MobiusIfcBridge.h");
static_assert(sizeof(MobiusIfcMaterialLayer) == 16, "MobiusIfcMaterialLayer layout drifted -- update the size comment in MobiusIfcBridge.h");
static_assert(sizeof(MobiusIfcProduct) == 88, "MobiusIfcProduct layout drifted -- update the size comment in MobiusIfcBridge.h");

namespace
{
	// ---------------------------------------------------------------------------------------------
	// Error buffer helpers. Both are safe to call with a null/zero-length buffer -- the diagnostics
	// buffer is optional for every caller (see MobiusIfc_Load's header doc).
	// ---------------------------------------------------------------------------------------------

	void ClearErrBuf(char* errBuf, int32_t errBufLen)
	{
		if (errBuf && errBufLen > 0)
		{
			errBuf[0] = '\0';
		}
	}

	void WriteErr(char* errBuf, int32_t errBufLen, const char* message)
	{
		if (!errBuf || errBufLen <= 0)
		{
			return;
		}
		if (!message)
		{
			message = "";
		}
		// snprintf always NUL-terminates when size > 0 and never writes past size -- exactly the
		// truncate-safely contract MobiusIfc_Load's header doc promises.
		std::snprintf(errBuf, static_cast<size_t>(errBufLen), "%s", message);
	}

	// =================================================================================================
	// THE coordinate conversion. Every position this DLL ever hands out goes through this function
	// and nowhere else (constraint: "coordinate conversion happens INSIDE the DLL, once, in one
	// function, so it is proven once and cannot drift" -- HANDOFF_IFC_2026-08-11.md 7.2/5.6).
	//
	// IFC is right-handed, Z-up, metres. UE is left-handed, Z-up, centimetres. The Y mirror below
	// is what makes it left-handed; a mirror always reverses triangle winding, which is why every
	// triangle this file emits has its source loop order reversed -- see EmitMeshset() below, the one
	// place triangles are assembled, and EmitTriangle(), the one place they are written out.
	//
	// The SCALE AND MIRROR AXIS below are proven (handoff 13.2/13.3): this formula passes all four
	// harness lenses on both test files -- including the asymmetry/chirality check -- while five
	// wrong-but-plausible variants (no mirror, mirror on X, mirror on Z, mirror on all three axes, and
	// mirror-without-index-reversal) are each rejected by at least one lens. The DLL's UE bounds also
	// match the harness's IFC bounds with the Y interval's min and max correctly swapped by the mirror.
	//
	// WHAT WAS *NOT* PROVEN, and got shipped wrong twice: the harness's lens 2 requires the
	// right-hand-rule signed volume to come out POSITIVE in UE space, and treats "reverse the indices"
	// as the way to achieve that. That is a right-handed MATH convention, not UE's rasterizer
	// convention, so satisfying it produced inside-out faces (owner-reported 2026-08-12). The index
	// reversal is gone; see the long note in EmitMeshset for the empirical anchor (this project's own
	// Assimp path) and the algebra. Lens 2 must NOT be used to re-justify a reversal here.
	//
	// So, before changing anything in this area, re-run all three families, because each is blind to
	// what the others catch: analytic VOLUME (scale, units, booleans), analytic SURFACE AREA
	// (triangulation -- caught the fan defect the volume lenses could not see), and an actual RENDER
	// (facing -- caught this winding defect that both of the others passed).
	// =================================================================================================
	inline void IfcToUe(double xIfcMetres, double yIfcMetres, double zIfcMetres,
	                     float& outXCm, float& outYCm, float& outZCm)
	{
		outXCm = static_cast<float>(xIfcMetres * 100.0);
		outYCm = static_cast<float>(-yIfcMetres * 100.0);
		outZCm = static_cast<float>(zIfcMetres * 100.0);
	}

	// ---------------------------------------------------------------------------------------------
	// Owns every buffer a MobiusIfcProduct's pointers point into, for one IFC product. Built once,
	// never resized after MobiusIfc_Load hands the scene back, so the raw pointers callers see
	// through MobiusIfcProduct stay valid for the scene's whole lifetime (constraint 4).
	// ---------------------------------------------------------------------------------------------
	// One draw range: geometry plus the appearance that applies to it. Sections are keyed by distinct
	// appearance within a product, so an item that shares its neighbour's colour shares its section.
	struct MobiusIfcSectionStorage
	{
		std::vector<float>   vertices; // interleaved xyz, UE space
		std::vector<float>   normals;  // interleaved xyz, UE space, parallel to vertices
		std::vector<int32_t> indices;  // triangle list, indices local to THIS section
		MobiusIfcAppearance  appearance{};
	};

	struct MobiusIfcProductStorage
	{
		// Heap buffers inside these stay put even if this vector reallocates (a moved std::vector keeps
		// its allocation), so pointers taken in the view-building pass survive. The views are still
		// built only after all emission finishes -- relying on that guarantee where it is avoidable
		// would be asking for trouble.
		std::vector<MobiusIfcSectionStorage> sections;
		std::vector<MobiusIfcSection>        sectionView;
		std::vector<MobiusIfcMaterialLayer>  layerView;
		std::vector<std::string>             layerNames; // owns the strings layerView points at
		std::string          materialName;
		std::string          guid;
		std::string          ifcClass;
		MobiusIfcAabb         aabb{};
		bool                  aabbInitialised = false;
		// Set by EmitTriangle if this product's geometry hit the int32_t count ceiling. BuildScene
		// turns it into a hard load failure -- a silently truncated mesh is worse than no mesh.
		bool                  bTruncated = false;
		// Faces IFC++'s export triangulator handed back with more than 3 corners. Expected to be 0 --
		// retriangulateMeshSetForExport emits only triangles today. Counted rather than fanned so a
		// future upstream change surfaces as a number instead of as silently overlapping geometry.
		int32_t               nonTriangleFacesDropped = 0;

		/* Zero-area (collinear T-junction) triangles refused by EmitTriangle -- see its comment.
		 * Deliberately NOT surfaced through the ABI: adding a field to MobiusIfcProduct is a layout
		 * change and would force MOBIUSIFC_ABI_VERSION 2 -> 3 and a rebuild of every consumer, for a
		 * diagnostic that is already obtainable two other ways. The drop IS observable without it:
		 * per-product triCount falls, and the load summary's rendered_tris / file_tris move with it.
		 * _CurrentHandoff/tools/ifcvalidate/ifcfacing_main.cpp reports the exact count per file. */
		int32_t               degenerateTrianglesDropped = 0;
	};

	// ---------------------------------------------------------------------------------------------
	// EmitTriangle -- appends ONE triangle, already in UE space, with a flat normal.
	//
	// This is the only place triangles are assembled. It is fed by EmitMeshset below, which gets its
	// triangles from IFC++'s own export triangulator; it is deliberately NOT fed by a hand-rolled fan
	// over a carve face loop any more. Why, measured on 2026-08-12:
	//
	//   A fan from corner 0 is only correct for a CONVEX polygon. Carve's CSG output is full of
	//   non-convex faces -- every wall face with a window or door opening cut into it -- and fanning
	//   one of those emits triangles that leave the polygon, cover the hole, and overlap each other.
	//   The wall 0rSEC$DWP81heohefO23z0 (8.4 x 0.2 x 3.5 m with three 1.2 x 1.5 m openings) came out
	//   at 131.729704 m2 of emitted surface against an analytic 56.000000 m2 -- 2.35x too much, with
	//   16 triangles under 1 cm2 and a worst aspect ratio of 4e8. In the renderer that is exactly what
	//   it sounds like: window holes filled in, black inside-out slivers, ragged wall faces.
	//
	//   The analytic VOLUME anchors (3.500000 / 4.800000 / 3.213200 m3) passed the whole time, and
	//   still do. That is not luck: the divergence-theorem sum over a fan of a planar polygon is exact
	//   regardless of convexity, because the sliver contributions cancel in signed arithmetic. Volume
	//   cannot see this class of defect at all. SURFACE AREA can, and is now the anchor that guards it
	//   (see the harness and the ProjectMobius.Ifc.Import tests).
	//
	// Vertices are deliberately NOT welded across triangles. A welded scheme would need either
	// per-vertex smoothing groups (IFC++ exposes none) or angle-threshold heuristics -- and
	// architectural BIM geometry is almost entirely hard-edged, so a wrong heuristic shows up as bad
	// shading at every corner. Every triangle gets three fresh vertex slots and one flat normal, which
	// is always exactly right. The cost is vertex count.
	//
	// The normal is computed FROM the already-converted, already-reversed corners via
	// cross(p1-p0, p2-p0) -- there is no separate "convert the normal" step that could drift out of
	// agreement with the position/winding conversion.
	// ---------------------------------------------------------------------------------------------
	void EmitTriangle(const float tri[3][3], MobiusIfcSectionStorage& section, MobiusIfcProductStorage& out)
	{
		const float e1x = tri[1][0] - tri[0][0], e1y = tri[1][1] - tri[0][1], e1z = tri[1][2] - tri[0][2];
		const float e2x = tri[2][0] - tri[0][0], e2y = tri[2][1] - tri[0][1], e2z = tri[2][2] - tri[0][2];

		// NEGATED right-hand-rule cross product, deliberately. cross(e1, e2) of a source-order loop is
		// ANTIPARALLEL to the outward direction after IfcToUe's Y mirror -- see the derivation in
		// EmitMeshset. Negating it is what makes the stored normal point out of the solid, and it is
		// consistent with what Assimp does for the same situation: MakeLeftHandedProcess mirrors the
		// stored normal's z component rather than recomputing it from the (unreversed) indices.
		//
		// If this negation is ever removed, lighting inverts while the geometry still culls correctly --
		// which looks like a material bug and is not one. The two must change together or not at all.
		float nx = -(e1y * e2z - e1z * e2y);
		float ny = -(e1z * e2x - e1x * e2z);
		float nz = -(e1x * e2y - e1y * e2x);
		// THRESHOLD CHOICE IS LOAD-BEARING -- do not "tidy" this back to 1e-8f.
		//
		// IFC++'s geometry output is NOT bit-reproducible. Measured 2026-08-12: the same IFC4X3 file
		// loaded twice IN ONE PROCESS gave 16499 then 16497 triangles, and three separate processes
		// gave 16499 / 16496 / 16499. (The IFC2X3 file happened to be stable at 3072.) Carve's boolean
		// orders work through pointer-keyed containers, so heap addresses change the accumulation
		// order and the last bits of the vertex positions with it. That is upstream of us and is not
		// something this shim can fix.
		//
		// It only matters here because a threshold turns those last bits into a COUNT. At 1e-8f the
		// cutoff sat on the top edge of the degenerate population and the count flapped by +/-3, which
		// made the automation tests flaky.
		//
		// Measured cross-length distribution (UE cm units, == 2 x area in cm^2):
		//
		//                     IFC2X3   IFC4X3
		//     exactly 0          1        0
		//     <= 1e-12           5       19     <- degenerate: collinear T-junction triples
		//     <= 1e-10          14       72
		//     <= 1e-8            0        1
		//     ----------------- EMPTY -----------------  six orders of magnitude, zero triangles
		//     <= 1e-6            0        0
		//     <= 1e-4            0        0
		//     <= 1e-2            0        0
		//     > 1e-2          3072    16499     <- real geometry
		//
		// 1e-5f is the geometric centre of that gap: 1000x above the largest degenerate triangle and
		// 1000x below the smallest real one. Well outside the noise on both sides, so the count is
		// stable. In absolute terms it discards triangles under 5e-6 cm^2 -- five thousandths of a
		// square millimetre, which cannot be a rendering decision either way.
		const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (len <= 1e-5f)
		{
			// ZERO-AREA TRIANGLE -- DROPPED, not emitted with a sentinel normal.
			//
			// These are collinear T-junction triples left by the boolean, measured 2026-08-12:
			// 20 on ISO-Test-1-2x3.ifc and 95 on ISO-Test-8-FireSmoke.ifc. Their three points are
			// distinct and far apart (edge lengths of 120 / 57.95 / 62.05 cm, summing exactly), so
			// PolyInputCache3D::addTriangleCheckDegenerate does NOT reject them -- it tests index
			// identity and edge LENGTH, never area. They arise only on walls that went through a
			// boolean: the wall with zero IfcRelVoidsElement has zero of them, walls with 1 and 3
			// voids have 3 and 8. Carve inserts the opening's edge vertices into the wall face loop
			// to keep the mesh watertight; triangulating a loop with a collinear run yields these.
			//
			// DROPPING IS SAFE AND AREA-PRESERVING, which is not obvious and is the whole reason this
			// is a drop rather than a vertex merge:
			//   - The vertex must STAY in the mesh. It is shared with the perpendicular reveal face of
			//     the opening. Deleting the VERTEX opens a T-junction crack. Deleting the TRIANGLE
			//     changes no topology at all -- the neighbouring triangles still reference the point.
			//     Those are opposite operations and only one of them is safe.
			//   - The surface is unchanged. MeshOps' 4-gon path always splits on the 0-2 diagonal, so
			//     a quad whose collinear triple is (A,B,C) or (A,C,D) yields one degenerate triangle
			//     and one that covers the ENTIRE quad. Every other arrangement of a collinear vertex
			//     produces two valid sub-triangles and reaches none of this. Measured confirmation:
			//     total surface area matched the hand-derived analytic anchors to 1e-6 WITH these
			//     triangles present, because a zero-area triangle contributes exactly zero.
			//
			// An earlier plan added diagonal selection to MeshOps' quad path so no degenerate is ever
			// produced. It was dropped: it yields TWO triangles where this yields one, for the same
			// surface, so it is strictly more output for no gain -- and it would mean forking vendored
			// third-party code.
			//
			// Why this matters at all, given a zero-area triangle rasterises to nothing: it carried a
			// (0,0,0) normal, which is an undefined shading basis in a vertex buffer. It also wasted
			// index buffer and poisoned any per-triangle analysis. It is NOT the cause of the visual
			// defect the owner reported -- that was the translucent material (HANDOFF 16.9).
			++out.degenerateTrianglesDropped;
			return;
		}
		nx /= len; ny /= len; nz /= len;

		// Guard the narrowing. The ABI hands out int32_t counts, so a product that grew past INT32_MAX
		// vertices would wrap to a negative count and the caller would walk a buffer with a garbage
		// length. Refuse to add more geometry instead: BuildScene checks bTruncated afterwards and
		// fails the whole load loudly rather than shipping a product that silently lost its tail.
		if (section.vertices.size() / 3 >= static_cast<size_t>(INT32_MAX) - 3
			|| section.indices.size() >= static_cast<size_t>(INT32_MAX) - 3)
		{
			out.bTruncated = true;
			return;
		}

		const int32_t baseIndex = static_cast<int32_t>(section.vertices.size() / 3);
		for (int c = 0; c < 3; ++c)
		{
			section.vertices.push_back(tri[c][0]);
			section.vertices.push_back(tri[c][1]);
			section.vertices.push_back(tri[c][2]);
			section.normals.push_back(nx);
			section.normals.push_back(ny);
			section.normals.push_back(nz);
			section.indices.push_back(baseIndex + c);
		}
	}

	// ---------------------------------------------------------------------------------------------

	// ---------------------------------------------------------------------------------------------
	// Appearance resolution.
	//
	// IFC++'s StylesConverter has already turned IfcStyledItem -> IfcPresentationStyleAssignment ->
	// IfcSurfaceStyle -> IfcSurfaceStyleRendering -> IfcColourRGB into StyleData by the time
	// convertGeometry() returns, so none of that parsing happens here -- this only picks which style
	// applies and flattens it into the POD the ABI hands out.
	//
	// Styles live on BOTH ItemShapeData and ProductShapeData ("styles can be attached to Products, as
	// well as to geometric items" -- GeometryInputData.h). Items win over their product, and a child
	// item with no styles of its own inherits whatever its parent resolved to, which is why
	// EmitItem threads an inherited appearance down the recursion.
	// ---------------------------------------------------------------------------------------------
	MobiusIfcAppearance NoAppearance()
	{
		MobiusIfcAppearance a{};
		a.diffuseR = a.diffuseG = a.diffuseB = 0.0f;
		a.opacity = 1.0f;
		a.specularExponent = 0.0f;
		a.bHasAppearance = 0;
		return a;
	}

	// Picks the first style carrying a usable surface colour. Curve and text styles are skipped: they
	// describe annotation, not surfaces, and letting a text colour become a wall colour would be a
	// silent wrong answer rather than a visible one.
	bool ResolveAppearance(const std::vector<shared_ptr<StyleData> >& styles, MobiusIfcAppearance& out)
	{
		for (const auto& style : styles)
		{
			if (!style)
			{
				continue;
			}
			if (style->m_apply_to_geometry_type == StyleData::GEOM_TYPE_CURVE
				|| style->m_apply_to_geometry_type == StyleData::GEOM_TYPE_TEXT)
			{
				continue;
			}

			// Prefer diffuse; fall back to ambient only if diffuse is exactly black on all channels,
			// which is how IFC++ leaves it when the source style set an ambient colour and no diffuse.
			// A genuinely black diffuse and an unset one are indistinguishable here -- accepted, because
			// treating unset-as-black would render styled products black, which is worse.
			const vec4& d = style->m_color_diffuse;
			const vec4& a = style->m_color_ambient;
			const bool bDiffuseSet = (d.r > 0.0f || d.g > 0.0f || d.b > 0.0f);
			const vec4& c = bDiffuseSet ? d : a;
			if (!bDiffuseSet && !(a.r > 0.0f || a.g > 0.0f || a.b > 0.0f))
			{
				continue; // no colour information at all in this style
			}

			out.diffuseR = static_cast<float>(c.r);
			out.diffuseG = static_cast<float>(c.g);
			out.diffuseB = static_cast<float>(c.b);

			// IFC transparency is 0 = opaque; the ABI hands out opacity because every consumer thinks
			// in opacity. Flip once, here, rather than in each consumer.
			double transparency = style->m_transparency;
			if (transparency < 0.0) { transparency = 0.0; }
			if (transparency > 1.0) { transparency = 1.0; }
			out.opacity = static_cast<float>(1.0 - transparency);

			// m_specular_exponent, NOT m_shininess -- see the header's field comment. m_shininess is
			// derived from m_specular_roughness * 128 inside StylesConverter and carries an unrelated
			// value for a file that specified an exponent, which both test files do.
			out.specularExponent = static_cast<float>(style->m_specular_exponent);
			out.bHasAppearance = 1;
			return true;
		}
		return false;
	}

	bool AppearanceEquals(const MobiusIfcAppearance& a, const MobiusIfcAppearance& b)
	{
		// Exact float comparison is correct here, not sloppy: both sides are copied from the same
		// StyleData fields, so two items sharing a style produce bit-identical values. Any epsilon would
		// be inventing a tolerance for a case that cannot arise.
		return a.bHasAppearance == b.bHasAppearance
			&& a.diffuseR == b.diffuseR && a.diffuseG == b.diffuseG && a.diffuseB == b.diffuseB
			&& a.opacity == b.opacity && a.specularExponent == b.specularExponent;
	}

	// Get-or-create the section for an appearance. Grouping by appearance rather than by item is what
	// keeps sectionCount at 1 for most products -- each section becomes a separate draw call in the
	// consumer, and the emit path there pushes one section per frame.
	MobiusIfcSectionStorage& SectionForAppearance(MobiusIfcProductStorage& out, const MobiusIfcAppearance& appearance)
	{
		for (auto& existing : out.sections)
		{
			if (AppearanceEquals(existing.appearance, appearance))
			{
				return existing;
			}
		}
		out.sections.emplace_back();
		out.sections.back().appearance = appearance;
		return out.sections.back();
	}

	// ---------------------------------------------------------------------------------------------
	// EmitMeshset -- triangulates one carve MeshSet through IFC++'s OWN export triangulator and
	// appends the result, converted to UE space, to a product's buffers.
	//
	// MeshOps::retriangulateMeshSetForExport is the routine IFC++ uses to feed its own OpenSceneGraph
	// viewer (ConverterOSG.h calls it for exactly this purpose), so it is the library's supported
	// answer to "turn a post-CSG meshset into renderable triangles", not something invented here. Per
	// face it: passes 3-gons through untouched, splits 4-gons into two triangles with a degeneracy
	// check, and hands anything larger to FaceConverter::createTriangulated3DFace, which projects the
	// loop to 2D and triangulates it properly -- handling the non-convex loops that carve's boolean
	// subtraction produces around every window and door opening. Its output is therefore ALWAYS
	// triangles.
	//
	// This replaced a hand-rolled fan over each face's edge loop. See EmitTriangle's comment for the
	// measured consequence of that fan (2.35x the correct surface area on a wall with three openings)
	// and for why the analytic volume anchors could not detect it.
	//
	// Both m_meshsets (closed/watertight) and m_meshsets_open (non-manifold leftovers, e.g. a thin
	// frame piece the CSG could not close) carry visible faces that belong in a render mesh -- "open"
	// only matters for the harness's watertightness diagnostic, not for whether a face should be drawn.
	// retriangulateMeshSetForExport is explicitly documented upstream as not requiring a closed, valid
	// mesh ("with priority of not skipping triangles"), which is what makes it correct for both.
	// ---------------------------------------------------------------------------------------------
	void EmitMeshset(const shared_ptr<carve::mesh::MeshSet<3> >& meshset, const carve::math::Matrix& xform,
	                  GeomProcessingParams& params, MobiusIfcSectionStorage& section,
	                  MobiusIfcProductStorage& out)
	{
		if (!meshset)
		{
			return;
		}

		PolyInputCache3D poly(params.epsMergePoints);
		MeshOps::retriangulateMeshSetForExport(meshset, poly, params);

		if (!poly.m_poly_data)
		{
			return;
		}

		const std::vector<carve::geom3d::Vector>& points = poly.m_poly_data->points;
		const std::vector<int>& faceIndices = poly.m_poly_data->faceIndices;

		// faceIndices is carve's PolyhedronData layout: a vertex COUNT followed by that many indices,
		// repeated. Parsed generically rather than assuming a stride of 4: if a future IFC++ version
		// ever emits a non-triangle here, this drops it and counts it instead of silently fanning it
		// (fanning is the exact defect this function exists to remove).
		size_t cursor = 0;
		while (cursor < faceIndices.size())
		{
			const int cornerCount = faceIndices[cursor];
			if (cornerCount < 3 || cursor + 1 + static_cast<size_t>(cornerCount) > faceIndices.size())
			{
				break; // malformed tail -- stop rather than read past the end
			}

			if (cornerCount == 3)
			{
				const int ia = faceIndices[cursor + 1];
				const int ib = faceIndices[cursor + 2];
				const int ic = faceIndices[cursor + 3];

				if (ia >= 0 && ib >= 0 && ic >= 0
					&& static_cast<size_t>(ia) < points.size()
					&& static_cast<size_t>(ib) < points.size()
					&& static_cast<size_t>(ic) < points.size())
				{
					// ============================================================================
					// SOURCE ORDER, NOT REVERSED. Do not "fix" this back to (a, c, b).
					//
					// This emitted (a, c, b) until 2026-08-12 and the owner reported every face
					// rendering inside-out. The reversal was there to satisfy the harness's lens 2,
					// which requires the right-hand-rule signed volume to be POSITIVE in UE space --
					// and that requirement is the bug. It enforces a right-handed MATH convention on a
					// left-handed RENDER target. Both statements are true at once: reversed indices
					// give a positive RHR volume AND render backwards.
					//
					// The convention that actually works here is established empirically by this
					// project's own Assimp path, which feeds the SAME UProceduralMeshComponent with
					// meshes that render correctly: AsyncAssimpMeshLoader.cpp passes
					// aiProcess_MakeLeftHanded and NOT aiProcess_FlipWindingOrder. Assimp's
					// MakeLeftHandedProcess (ConvertToLHProcess.cpp:147) mirrors z on positions and
					// normals and leaves face index order untouched; FlipWindingOrderProcess
					// (:321) is the separate pass that reverses indices, and Mobius never enables it.
					// So: ONE mirror, ZERO index reversals. This function does the same.
					//
					// The algebra agrees. For the linear part T = diag(100, -100, 100):
					//   cross(Ta, Tb) = det(T) * T^-T * cross(a, b)
					//                 = -1e6 * diag(1/100, -1/100, 1/100) * (nx, ny, nz)
					//                 = -1e4 * (nx, -ny, nz)
					// while the true outward direction in UE space is T applied to the normal as a
					// direction, i.e. proportional to (nx, -ny, nz). The RHR cross of the source-order
					// loop is therefore ANTIPARALLEL to outward -- which is why EmitTriangle stores the
					// negated cross as its normal. See its comment.
					// ============================================================================
					const int order[3] = { ia, ib, ic };
					float tri[3][3];
					for (int c = 0; c < 3; ++c)
					{
						const carve::geom::vector<3> p = xform * points[static_cast<size_t>(order[c])];
						IfcToUe(p.x, p.y, p.z, tri[c][0], tri[c][1], tri[c][2]);

						if (!out.aabbInitialised)
						{
							out.aabb.minX = out.aabb.maxX = tri[c][0];
							out.aabb.minY = out.aabb.maxY = tri[c][1];
							out.aabb.minZ = out.aabb.maxZ = tri[c][2];
							out.aabbInitialised = true;
						}
						else
						{
							out.aabb.minX = std::min(out.aabb.minX, tri[c][0]); out.aabb.maxX = std::max(out.aabb.maxX, tri[c][0]);
							out.aabb.minY = std::min(out.aabb.minY, tri[c][1]); out.aabb.maxY = std::max(out.aabb.maxY, tri[c][1]);
							out.aabb.minZ = std::min(out.aabb.minZ, tri[c][2]); out.aabb.maxZ = std::max(out.aabb.maxZ, tri[c][2]);
						}
					}

					EmitTriangle(tri, section, out);
				}
			}
			else
			{
				++out.nonTriangleFacesDropped;
			}

			cursor += 1 + static_cast<size_t>(cornerCount);
		}
	}

	// Recurses through an ItemShapeData's meshsets and child items, emitting every meshset it finds
	// into the section matching that item's resolved appearance.
	//
	// InheritedAppearance is what this item's parent resolved to (the product's own styles at the top
	// level). An item's own styles win; an item with none keeps what it inherited. That mirrors how IFC
	// presentation styles actually cascade, and it is why a window's glazing can be clear while its
	// frame stays brown even though only one of them carries an explicit style.
	void EmitItem(const shared_ptr<ItemShapeData>& item, const carve::math::Matrix& xform,
	               GeomProcessingParams& params, const MobiusIfcAppearance& inheritedAppearance,
	               MobiusIfcProductStorage& out)
	{
		if (!item)
		{
			return;
		}

		MobiusIfcAppearance appearance = inheritedAppearance;
		ResolveAppearance(item->getStyles(), appearance); // leaves `appearance` untouched if it finds nothing

		if (!item->m_meshsets.empty() || !item->m_meshsets_open.empty())
		{
			MobiusIfcSectionStorage& section = SectionForAppearance(out, appearance);
			for (const auto& meshset : item->m_meshsets)
			{
				EmitMeshset(meshset, xform, params, section, out);
			}
			for (const auto& meshset : item->m_meshsets_open)
			{
				EmitMeshset(meshset, xform, params, section, out);
			}
		}

		for (const auto& child : item->m_child_items)
		{
			EmitItem(child, xform, params, appearance, out);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Semantic materials -- a DIFFERENT channel from appearance, and the one that makes an IFC import
	// useful beyond looking right: a named material with a thickness is a fire-load / thermal input for
	// the B-RISK side of Mobius.
	//
	// Reached through the inverse relationship IfcObjectDefinition::m_HasAssociations_inverse ->
	// IfcRelAssociatesMaterial::m_RelatingMaterial, whose type is the IfcMaterialSelect SELECT. Each
	// concrete branch is handled explicitly; an unhandled branch leaves the product with no layers,
	// which is honest, rather than guessing.
	//
	// Thickness comes out of the file in the file's own length unit, so it is scaled by
	// UnitConverter's length-in-metres factor and then to centimetres, matching every other length in
	// this ABI. Getting this wrong would be a factor-of-1000 error hiding in metadata rather than in
	// geometry, where no volume or area anchor would ever catch it.
	// ---------------------------------------------------------------------------------------------
	void AddLayer(MobiusIfcProductStorage& out, const shared_ptr<IfcMaterial>& material,
	               const shared_ptr<IfcNonNegativeLengthMeasure>& thickness, double lengthToCm)
	{
		std::string name;
		if (material && material->m_Name)
		{
			name = material->m_Name->m_value;
		}

		float thicknessCm = 0.0f;
		if (thickness)
		{
			thicknessCm = static_cast<float>(thickness->m_value * lengthToCm);
		}

		out.layerNames.push_back(name);
		MobiusIfcMaterialLayer layer{};
		layer.name = nullptr; // filled in the view pass, once layerNames can no longer reallocate
		layer.thicknessCm = thicknessCm;
		layer.reserved0 = 0;
		out.layerView.push_back(layer);
	}

	void CollectMaterials(const shared_ptr<IfcObjectDefinition>& objDef, double lengthToCm,
	                       MobiusIfcProductStorage& out)
	{
		if (!objDef)
		{
			return;
		}

		for (const auto& assocWeak : objDef->m_HasAssociations_inverse)
		{
			shared_ptr<IfcRelAssociates> assoc = assocWeak.lock();
			if (!assoc)
			{
				continue;
			}
			shared_ptr<IfcRelAssociatesMaterial> matAssoc = dynamic_pointer_cast<IfcRelAssociatesMaterial>(assoc);
			if (!matAssoc || !matAssoc->m_RelatingMaterial)
			{
				continue;
			}

			const shared_ptr<IfcMaterialSelect>& select = matAssoc->m_RelatingMaterial;

			// A layer set, directly or through a usage. Layer order is the IFC declaration order, which
			// is outermost-first, and is preserved.
			shared_ptr<IfcMaterialLayerSet> layerSet = dynamic_pointer_cast<IfcMaterialLayerSet>(select);
			if (!layerSet)
			{
				if (shared_ptr<IfcMaterialLayerSetUsage> usage = dynamic_pointer_cast<IfcMaterialLayerSetUsage>(select))
				{
					layerSet = usage->m_ForLayerSet;
				}
			}
			if (layerSet)
			{
				if (out.materialName.empty() && layerSet->m_LayerSetName)
				{
					out.materialName = layerSet->m_LayerSetName->m_value;
				}
				for (const auto& layer : layerSet->m_MaterialLayers)
				{
					if (!layer)
					{
						continue;
					}
					AddLayer(out, layer->m_Material, layer->m_LayerThickness, lengthToCm);
					if (out.materialName.empty() && layer->m_Material && layer->m_Material->m_Name)
					{
						out.materialName = layer->m_Material->m_Name->m_value;
					}
				}
				continue;
			}

			// A single material.
			if (shared_ptr<IfcMaterial> material = dynamic_pointer_cast<IfcMaterial>(select))
			{
				AddLayer(out, material, nullptr, lengthToCm);
				if (out.materialName.empty() && material->m_Name)
				{
					out.materialName = material->m_Name->m_value;
				}
				continue;
			}

			// A plain list of materials -- named, unlayered, so no thicknesses.
			if (shared_ptr<IfcMaterialList> list = dynamic_pointer_cast<IfcMaterialList>(select))
			{
				for (const auto& material : list->m_Materials)
				{
					AddLayer(out, material, nullptr, lengthToCm);
					if (out.materialName.empty() && material && material->m_Name)
					{
						out.materialName = material->m_Name->m_value;
					}
				}
				continue;
			}

			// IFC4's constituent set -- the 4X3 test file uses 160 of these. Constituents name a material
			// per part of the element (e.g. frame vs glazing) and carry no thickness.
			if (shared_ptr<IfcMaterialConstituentSet> constituentSet = dynamic_pointer_cast<IfcMaterialConstituentSet>(select))
			{
				if (out.materialName.empty() && constituentSet->m_Name)
				{
					out.materialName = constituentSet->m_Name->m_value;
				}
				for (const auto& constituent : constituentSet->m_MaterialConstituents)
				{
					if (!constituent)
					{
						continue;
					}
					AddLayer(out, constituent->m_Material, nullptr, lengthToCm);
					if (out.materialName.empty() && constituent->m_Material && constituent->m_Material->m_Name)
					{
						out.materialName = constituent->m_Material->m_Name->m_value;
					}
				}
				continue;
			}

			// Anything else (IfcMaterialProfileSet and friends): left alone deliberately. No test file
			// exercises them, and inventing a mapping that has never been run against real data would be
			// worse than reporting nothing.
		}
	}

	// Walks every entry in the GeometryConverter's shape map and builds the scene's owned storage,
	// then a second pass builds the POD view that MobiusIfc_GetProducts() hands out. The two
	// passes are deliberately separate: storage must be FULLY built and never touched again before
	// any pointer into it is taken, otherwise a std::vector reallocation mid-build would silently
	// invalidate a pointer already handed to a caller. reserve()-then-fill-in-place below is what
	// guarantees that.
	void BuildScene(GeometryConverter& conv, std::vector<MobiusIfcProductStorage>& storage,
	                 std::vector<MobiusIfcProduct>& view, int32_t& productsWithoutGeometryCount)
	{
		const std::unordered_map<std::string, shared_ptr<ProductShapeData> >& entities = conv.getShapeInputData();

		// Triangulation parameters, built ONCE from the same GeometrySettings the GeometryConverter
		// used, so the epsilons the triangulator merges points with are the ones the geometry was
		// built with rather than a second, independently-chosen set. dumpMeshes=false: the debug-dump
		// path inside MeshOps writes files and is compiled out of Release anyway.
		GeomProcessingParams triangulationParams(conv.getGeomSettings(), false);

		// File length unit -> centimetres, for IfcMaterialLayer thicknesses. Taken from IFC++'s own
		// UnitConverter so it agrees with the scaling the geometry already went through; a hardcoded
		// 0.1 (mm->cm) would be right for both test files and wrong for a metres-unit file, and it would
		// be wrong in metadata where no volume or area anchor could catch it.
		double lengthToCm = 100.0;
		if (const shared_ptr<BuildingModel>& model = conv.getBuildingModel())
		{
			if (const shared_ptr<UnitConverter>& units = model->getUnitConverter())
			{
				lengthToCm = units->getLengthInMeterFactor() * 100.0;
			}
		}

		storage.reserve(entities.size()); // upper bound; some entries below are skipped (no
		                                   // object definition, or zero geometry), so the final
		                                   // count is <= this reservation -- never more, so no
		                                   // reallocation can happen during the fill loop.

		for (const auto& kv : entities)
		{
			const shared_ptr<ProductShapeData>& shapeData = kv.second;
			if (!shapeData || shapeData->m_ifc_object_definition.expired())
			{
				continue;
			}

			shared_ptr<IfcObjectDefinition> obj(shapeData->m_ifc_object_definition);
			if (!obj)
			{
				continue;
			}

			MobiusIfcProductStorage entry;
			entry.guid = shapeData->m_entity_guid;
			if (entry.guid.empty() && obj->m_GlobalId)
			{
				entry.guid = obj->m_GlobalId->m_value;
			}
			// EntityFactory::getStringForClassID's return-storage-duration guarantee is not
			// documented in the header and was not verified against its implementation -- copy
			// the string into scene-owned storage rather than hand out IFC++'s raw pointer, so
			// this API's lifetime contract depends only on this file's own storage, never on an
			// unverified assumption about IFC++ internals. Same reasoning applies to guid above,
			// though m_GlobalId->m_value is already a plain std::string we own a copy of.
			const char* classNameRaw = EntityFactory::getStringForClassID(obj->classID());
			entry.ifcClass = classNameRaw ? classNameRaw : "";

			// Product-level styles are the fallback every geometric item inherits.
			MobiusIfcAppearance productAppearance = NoAppearance();
			ResolveAppearance(shapeData->getStyles(), productAppearance);

			// Semantic materials come from the IFC entity graph, not from the geometry, so this runs
			// regardless of whether the product produced any triangles.
			CollectMaterials(obj, lengthToCm, entry);

			const carve::math::Matrix xform = shapeData->getTransform();
			for (const auto& geomItem : shapeData->getGeometricItems())
			{
				EmitItem(geomItem, xform, triangulationParams, productAppearance, entry);
			}

			if (entry.bTruncated)
			{
				// Hit the int32_t vertex/index ceiling. Refuse the whole load: the caller's ABI
				// cannot express this product's size, and handing back a partial mesh would look
				// like valid geometry. Throwing here is safe -- MobiusIfc_Load's stage-2 try/catch
				// is the immediate caller, so this converts to MOBIUSIFC_ERR_GEOMETRY_FAILED and
				// never escapes the DLL.
				throw std::runtime_error("product geometry exceeds the int32_t vertex/index limit");
			}

			// Drop sections that ended up empty (an item whose meshsets all produced zero triangles
			// still created a section). A zero-triangle section would otherwise reach the consumer as a
			// draw call with nothing in it.
			entry.sections.erase(
				std::remove_if(entry.sections.begin(), entry.sections.end(),
					[](const MobiusIfcSectionStorage& s) { return s.indices.empty(); }),
				entry.sections.end());

			if (entry.sections.empty())
			{
				// Normal for IfcProject/IfcSite/IfcBuilding/IfcBuildingStorey and *Type/*Style
				// definitions (HANDOFF_IFC_2026-08-11.md 5.1) -- not an allowlist decision, not
				// an error, just "IFC++ gave this entity no shape at all".
				++productsWithoutGeometryCount;
				continue;
			}

			storage.push_back(std::move(entry));
		}

		// Second pass: storage is now final and will never be resized again, so every pointer
		// taken below is permanently valid for the scene's lifetime. The per-product sectionView and
		// layerView vectors are built here too, for the same reason -- their contents point into
		// buffers that must already be final.
		view.reserve(storage.size());
		for (auto& s : storage)
		{
			s.sectionView.clear();
			s.sectionView.reserve(s.sections.size());

			int32_t totalVerts = 0;
			int32_t totalIndices = 0;
			for (auto& sec : s.sections)
			{
				MobiusIfcSection view_section{};
				view_section.vertices   = sec.vertices.data();
				view_section.normals    = sec.normals.data();
				view_section.indices    = sec.indices.data();
				view_section.vertCount  = static_cast<int32_t>(sec.vertices.size() / 3);
				view_section.indexCount = static_cast<int32_t>(sec.indices.size());
				view_section.triCount   = static_cast<int32_t>(sec.indices.size() / 3);
				view_section.reserved0  = 0;
				view_section.appearance = sec.appearance;
				s.sectionView.push_back(view_section);

				totalVerts   += view_section.vertCount;
				totalIndices += view_section.indexCount;
			}

			// layerView entries were pushed during CollectMaterials with a null name; bind them to their
			// owning strings now that layerNames is final and cannot reallocate underneath them.
			for (size_t i = 0; i < s.layerView.size() && i < s.layerNames.size(); ++i)
			{
				s.layerView[i].name = s.layerNames[i].c_str();
			}

			MobiusIfcProduct p{};
			p.sections     = s.sectionView.data();
			p.layers       = s.layerView.empty() ? nullptr : s.layerView.data();
			p.guid         = s.guid.c_str();
			p.ifcClass     = s.ifcClass.c_str();
			p.materialName = s.materialName.c_str();
			p.sectionCount = static_cast<int32_t>(s.sectionView.size());
			p.layerCount   = static_cast<int32_t>(s.layerView.size());
			p.vertCount    = totalVerts;
			p.indexCount   = totalIndices;
			p.triCount     = totalIndices / 3;
			p.bRenderable  = (p.triCount > 0) ? 1 : 0;
			p.aabb         = s.aabb;
			view.push_back(p);
		}
	}

} // anonymous namespace

// ---------------------------------------------------------------------------------------------
// The scene's real definition. Opaque to every consumer of MobiusIfcBridge.h; concrete here
// because this is the only translation unit that ever constructs, reads the internals of, or
// destroys one.
// ---------------------------------------------------------------------------------------------
struct MobiusIfcScene
{
	std::vector<MobiusIfcProductStorage> storage; // owns all the memory
	std::vector<MobiusIfcProduct>        view;    // POD pointers into `storage` -- what callers see
	int32_t productsWithoutGeometryCount = 0;
};

// =================================================================================================
// Exported functions. Every one of these is wrapped so nothing can escape (constraint 2) --
// see the individual try/catch blocks below. carve::exception is caught explicitly and
// separately from std::exception because it does NOT derive from std::exception (verified by
// reading external/Carve/src/include/carve/carve.hpp -- `struct exception` there is a
// freestanding type with no base class at all); a plain `catch (const std::exception&)` would
// silently miss it and fall through to `catch (...)`, which still would not crash but would
// lose the exception's own message via e.str().
// =================================================================================================

extern "C" {

MOBIUSIFC_API uint32_t MobiusIfc_GetAbiVersion(void)
{
	return MOBIUSIFC_ABI_VERSION;
}

MOBIUSIFC_API const char* MobiusIfc_ErrorString(int32_t resultCode)
{
	switch (resultCode)
	{
		case MOBIUSIFC_OK:                  return "OK";
		case MOBIUSIFC_ERR_INVALID_ARGUMENT: return "invalid argument (null pointer, empty path, or out-of-range value)";
		case MOBIUSIFC_ERR_FILE_NOT_FOUND:   return "file not found or not readable";
		case MOBIUSIFC_ERR_PARSE_FAILED:     return "IFC STEP parse failed (malformed file or unrecoverable reader exception)";
		case MOBIUSIFC_ERR_GEOMETRY_FAILED:  return "geometry/CSG conversion failed";
		case MOBIUSIFC_ERR_OUT_OF_MEMORY:    return "out of memory";
		case MOBIUSIFC_ERR_ABI_MISMATCH:     return "caller/DLL ABI version mismatch (caller-detected only -- the DLL itself never returns this code)";
		case MOBIUSIFC_ERR_UNKNOWN:          return "unknown/unclassified error";
		default:                             return "unrecognized MobiusIfcResult code";
	}
}

// The real body. Called only through the MobiusIfc_Load wrapper below, which supplies the outer
// catch(...) -- so this function is allowed to have statements that can throw anywhere, including
// before its own first try block. `static` keeps it out of the DLL's export table; only the
// wrapper is exported.
static int32_t MobiusIfc_LoadImpl(const char* utf8Path, MobiusIfcScene** outScene, char* errBuf, int32_t errBufLen)
{
	ClearErrBuf(errBuf, errBufLen);
	if (outScene)
	{
		*outScene = nullptr;
	}

	if (!utf8Path || utf8Path[0] == '\0' || !outScene)
	{
		WriteErr(errBuf, errBufLen, "MobiusIfc_Load: utf8Path and outScene must both be non-null, and utf8Path must not be empty");
		return static_cast<int32_t>(MOBIUSIFC_ERR_INVALID_ARGUMENT);
	}

	// ---- Path encoding. Read this before "simplifying" it. ----
	//
	// The header promises utf8Path is UTF-8. On MSVC, std::ifstream's char* constructor decodes the
	// path with the process ANSI codepage, NOT UTF-8 -- so a genuinely non-ASCII UTF-8 path (an
	// accented, CJK or Cyrillic building name) mis-decodes into a false "file not found", or in the
	// worst case opens a DIFFERENT existing file whose ANSI name matches the mojibake bytes.
	//
	// IFC++ HAS THE SAME BUG AND WE CANNOT FIX IT FROM HERE: ReaderSTEP::loadModelFromFile takes a
	// std::string and opens it with infile.open(filePathRead.c_str(), ...) (ReaderSTEP.cpp:136).
	// (The bundled `nowide` library exists precisely to solve this and ifcpp never uses it -- see
	// HANDOFF_IFC_2026-08-11.md 13.9 for the two-line vendored patch that would fix it properly.)
	//
	// So the strategy here is: probe correctly ourselves via the wide-char API, and for a non-ASCII
	// path hand IFC++ the 8.3 SHORT path, which is pure ASCII and therefore survives any codepage.
	// If short-name generation is unavailable (8.3 can be disabled per-volume), fail with a specific,
	// honest error instead of letting IFC++ report a misleading one.
	std::string pathForReader(utf8Path);
	{
		bool isAscii = true;
		for (const char* p = utf8Path; *p; ++p)
		{
			if (static_cast<unsigned char>(*p) > 0x7F) { isAscii = false; break; }
		}

#if defined(_WIN32)
		const int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, nullptr, 0);
		if (wideLen <= 0)
		{
			WriteErr(errBuf, errBufLen, "path is not valid UTF-8");
			return static_cast<int32_t>(MOBIUSIFC_ERR_INVALID_ARGUMENT);
		}
		std::wstring widePath(static_cast<size_t>(wideLen), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, &widePath[0], wideLen);
		widePath.resize(static_cast<size_t>(wideLen) - 1);   // drop the NUL MultiByteToWideChar added

		const DWORD attrs = GetFileAttributesW(widePath.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			WriteErr(errBuf, errBufLen, "file not found or not readable");
			return static_cast<int32_t>(MOBIUSIFC_ERR_FILE_NOT_FOUND);
		}

		if (!isAscii)
		{
			const DWORD shortLen = GetShortPathNameW(widePath.c_str(), nullptr, 0);
			bool shortOk = false;
			if (shortLen > 0)
			{
				std::wstring shortPath(static_cast<size_t>(shortLen), L'\0');
				if (GetShortPathNameW(widePath.c_str(), &shortPath[0], shortLen) > 0)
				{
					shortPath.resize(static_cast<size_t>(shortLen) - 1);
					// The short path is only useful if it really is ASCII -- GetShortPathNameW
					// returns the long name unchanged on a volume with 8.3 disabled.
					bool shortIsAscii = true;
					for (wchar_t wc : shortPath) { if (wc > 0x7F) { shortIsAscii = false; break; } }
					if (shortIsAscii)
					{
						pathForReader.assign(shortPath.begin(), shortPath.end());
						shortOk = true;
					}
				}
			}
			if (!shortOk)
			{
				WriteErr(errBuf, errBufLen,
				         "path contains non-ASCII characters and no 8.3 short name is available; "
				         "IFC++ cannot open it (move the file to an ASCII-only path)");
				return static_cast<int32_t>(MOBIUSIFC_ERR_INVALID_ARGUMENT);
			}
		}
#else
		(void)isAscii;
		std::ifstream probe(utf8Path, std::ios::binary);
		if (!probe.good())
		{
			WriteErr(errBuf, errBufLen, "file not found or not readable");
			return static_cast<int32_t>(MOBIUSIFC_ERR_FILE_NOT_FOUND);
		}
#endif
	}

	// A callback that does nothing is still a callback: StatusCallback::messageCallback() (see
	// ifcpp/model/StatusCallback.h) writes to std::wcout under #ifdef _DEBUG whenever no message
	// callback has been installed. Constraint 5 bans iostream output in every path, unconditionally
	// -- not just in configurations that happen not to define _DEBUG -- so a callback is installed
	// on every StatusCallback-derived object below before it can do any work, even though this
	// bridge does not currently surface the messages it swallows.
	const StatusCallback::MessageCallbackType swallow = [](shared_ptr<StatusCallback::Message>) {};

	shared_ptr<BuildingModel> model;

	// ---- Stage 1: STEP parse ----
	try
	{
		model = std::make_shared<BuildingModel>();
		shared_ptr<ReaderSTEP> reader = std::make_shared<ReaderSTEP>();
		model->setMessageCallBack(swallow);
		reader->setMessageCallBack(swallow);

		// pathForReader, not utf8Path -- see the path-encoding block above. For an ASCII path these
		// are the same string; for a non-ASCII one this is the 8.3 short name, which is the only
		// form IFC++'s ANSI-codepage ifstream::open can be relied on to resolve.
		reader->loadModelFromFile(pathForReader, model);

		// IFC++ does NOT throw for a file it cannot parse at all. ReaderSTEP::loadModelFromFile
		// (ReaderSTEP.cpp:125-131) reports an unsupported extension via messageCallback and then
		// just `return`s, leaving a freshly-constructed, completely empty BuildingModel. Its
		// readData/readEntityArguments paths behave the same way for malformed lines: they
		// accumulate diagnostics and call messageCallback, never throw. So without this check a
		// non-IFC file (verified with C:/Windows/System32/kernel32.dll) returns MOBIUSIFC_OK with an
		// empty-but-valid-looking scene.
		//
		// The entity count is the right discriminator, not the product count: a legitimately
		// geometry-free IFC file still contains IfcProject/IfcSite/IfcBuildingStorey entities, so
		// this rejects garbage without breaking the header's documented
		// "zero renderable products is not a failure" contract.
		if (model->getMapIfcEntities().empty())
		{
			WriteErr(errBuf, errBufLen,
			         "parsed zero IFC entities - not an IFC file, an unsupported file type, or unparseable content");
			return static_cast<int32_t>(MOBIUSIFC_ERR_PARSE_FAILED);
		}
	}
	catch (const carve::exception& e)
	{
		WriteErr(errBuf, errBufLen, e.str().c_str());
		return static_cast<int32_t>(MOBIUSIFC_ERR_PARSE_FAILED);
	}
	catch (const std::bad_alloc&)
	{
		WriteErr(errBuf, errBufLen, "out of memory while parsing IFC file");
		return static_cast<int32_t>(MOBIUSIFC_ERR_OUT_OF_MEMORY);
	}
	catch (const std::exception& e)
	{
		WriteErr(errBuf, errBufLen, e.what());
		return static_cast<int32_t>(MOBIUSIFC_ERR_PARSE_FAILED);
	}
	catch (...)
	{
		WriteErr(errBuf, errBufLen, "unknown exception while parsing IFC file");
		return static_cast<int32_t>(MOBIUSIFC_ERR_UNKNOWN);
	}

	// ---- Stage 2: geometry conversion + export ----
	std::unique_ptr<MobiusIfcScene> scene;
	try
	{
		shared_ptr<GeometrySettings> settings = std::make_shared<GeometrySettings>();
		shared_ptr<GeometryConverter> conv = std::make_shared<GeometryConverter>(model, settings);
		conv->setMessageCallBack(swallow);
		// Matches the verified harness exactly -- do not change without re-running the handoff's
		// Level 2 analytic volume checks (HANDOFF_IFC_2026-08-11.md 5.2).
		conv->setCsgEps(1.5e-9);

		conv->convertGeometry();

		scene = std::make_unique<MobiusIfcScene>();
		BuildScene(*conv, scene->storage, scene->view, scene->productsWithoutGeometryCount);
	}
	catch (const carve::exception& e)
	{
		WriteErr(errBuf, errBufLen, e.str().c_str());
		return static_cast<int32_t>(MOBIUSIFC_ERR_GEOMETRY_FAILED);
	}
	catch (const std::bad_alloc&)
	{
		WriteErr(errBuf, errBufLen, "out of memory while converting IFC geometry");
		return static_cast<int32_t>(MOBIUSIFC_ERR_OUT_OF_MEMORY);
	}
	catch (const std::exception& e)
	{
		WriteErr(errBuf, errBufLen, e.what());
		return static_cast<int32_t>(MOBIUSIFC_ERR_GEOMETRY_FAILED);
	}
	catch (...)
	{
		WriteErr(errBuf, errBufLen, "unknown exception while converting IFC geometry");
		return static_cast<int32_t>(MOBIUSIFC_ERR_UNKNOWN);
	}

	*outScene = scene.release();
	return static_cast<int32_t>(MOBIUSIFC_OK);
}

// The exported entry point: nothing but an outer guard around MobiusIfc_LoadImpl.
//
// Why this wrapper exists rather than relying on the two inner try blocks: several statements in the
// impl necessarily sit outside them (the path-encoding block builds a std::wstring, the message
// callback is a std::function, and both can throw std::bad_alloc). Under MSVC's default /EHsc, /EHc
// tells the optimiser that an `extern "C"` function never throws, so an exception escaping one is
// documented as UNPREDICTABLE rather than merely uncaught. This wrapper makes "nothing escapes"
// true by construction instead of by inspecting every statement in a 200-line function.
//
// NOTE (open, owner decision -- see HANDOFF_IFC_2026-08-11.md 13.9): catch(...) under /EHsc does NOT
// catch SEH exceptions, so an access violation inside Carve on malformed geometry still kills the
// process. Containing that needs an explicit __try/__except around convertGeometry() filtered to
// exclude C++ exception code 0xE06D7363. Deliberately NOT done here: swallowing an access violation
// continues execution after possible memory corruption, which is a trade-off the owner should make.
MOBIUSIFC_API int32_t MobiusIfc_Load(const char* utf8Path, MobiusIfcScene** outScene, char* errBuf, int32_t errBufLen)
{
	try
	{
		return MobiusIfc_LoadImpl(utf8Path, outScene, errBuf, errBufLen);
	}
	catch (const std::bad_alloc&)
	{
		if (outScene) { *outScene = nullptr; }
		WriteErr(errBuf, errBufLen, "out of memory");
		return static_cast<int32_t>(MOBIUSIFC_ERR_OUT_OF_MEMORY);
	}
	catch (...)
	{
		if (outScene) { *outScene = nullptr; }
		WriteErr(errBuf, errBufLen, "unknown exception in MobiusIfc_Load");
		return static_cast<int32_t>(MOBIUSIFC_ERR_UNKNOWN);
	}
}

MOBIUSIFC_API void MobiusIfc_Free(MobiusIfcScene* scene)
{
	// Belt-and-braces per constraint 2 ("EVERY exported function body is wrapped"): none of
	// std::vector<T>'s or std::string's destructors are expected to throw, so this should never
	// actually catch anything, but the rule is unconditional and this call has no errBuf to
	// report through even if it did.
	try
	{
		delete scene;
	}
	catch (...)
	{
	}
}

MOBIUSIFC_API int32_t MobiusIfc_GetProductCount(const MobiusIfcScene* scene)
{
	// try/catch(...) on a body that provably cannot throw (size()/data()/empty() are all
	// noexcept, no allocation). Kept anyway so constraint 2 -- EVERY exported function body is
	// wrapped -- is true by inspection of the function, not by reasoning about its contents. A
	// future edit that adds an allocation here then cannot silently reopen an escape path.
	try
	{
		if (!scene)
		{
			return 0;
		}
		return static_cast<int32_t>(scene->view.size());
	}
	catch (...)
	{
		return 0;
	}
}

MOBIUSIFC_API int32_t MobiusIfc_GetProducts(const MobiusIfcScene* scene, const MobiusIfcProduct** outProducts, int32_t* outCount)
{
	if (!outProducts || !outCount)
	{
		return static_cast<int32_t>(MOBIUSIFC_ERR_INVALID_ARGUMENT);
	}

	if (!scene)
	{
		*outProducts = nullptr;
		*outCount = 0;
		return static_cast<int32_t>(MOBIUSIFC_OK);
	}

	try
	{
		*outProducts = scene->view.empty() ? nullptr : scene->view.data();
		*outCount = static_cast<int32_t>(scene->view.size());
		return static_cast<int32_t>(MOBIUSIFC_OK);
	}
	catch (...)
	{
		*outProducts = nullptr;
		*outCount = 0;
		return static_cast<int32_t>(MOBIUSIFC_ERR_UNKNOWN);
	}
}

MOBIUSIFC_API int32_t MobiusIfc_GetProductsWithoutGeometryCount(const MobiusIfcScene* scene)
{
	if (!scene)
	{
		return 0;
	}
	return scene->productsWithoutGeometryCount;
}

} // extern "C"
