// Mobius IFC++ validation harness.
//
// Two jobs:
//   1. Smoke test  - does IFC++ actually produce GEOMETRY (not just a parse) from a given .ifc,
//                    and surface every StatusCallback message so silently-dropped entities are visible.
//   2. Coordinate proof (handoff §5.6) - does the IFC -> UE conversion survive four independent
//                    verification lenses, INCLUDING the asymmetry/chirality check that volume and
//                    watertightness are both blind to.
//
// The geometry is extracted once into plain V3/Poly data, then every lens runs over that same data.
// Nothing below this line touches carve except the extraction step - deliberately, so a lens can never
// be accidentally fed a different mesh than the one the previous lens passed.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <unordered_map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <ifcpp/IFC4X3/include/IfcObjectDefinition.h>
#include <ifcpp/IFC4X3/include/IfcProduct.h>
#include <ifcpp/model/BuildingModel.h>
#include <ifcpp/reader/ReaderSTEP.h>
#include <ifcpp/geometry/GeometryConverter.h>

using namespace IFC4X3;

// ============================================================================
// Plain geometry types. No carve, no IFC++ - so the lenses are independently auditable.
// ============================================================================

struct V3
{
	double x = 0.0, y = 0.0, z = 0.0;
	double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
	double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

static inline V3   vsub(const V3& a, const V3& b)   { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline V3   vadd(const V3& a, const V3& b)   { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline V3   vmul(const V3& a, double s)      { return { a.x * s, a.y * s, a.z * s }; }
static inline double vdot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3   vcross(const V3& a, const V3& b)
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static inline double vlen(const V3& a) { return std::sqrt(vdot(a, a)); }

// One face as a world-space polygon, in the winding IFC++ handed us.
struct Poly
{
	std::vector<V3> v;
};

// One product's geometry, flattened out of the ItemShapeData tree.
struct Solid
{
	std::string guid;
	std::string cls;
	std::vector<Poly> closed;   // from ItemShapeData::m_meshsets      - volume is meaningful
	std::vector<Poly> open;     // from ItemShapeData::m_meshsets_open - volume is NOT meaningful
	size_t openEdges = 0;
	size_t meshsets = 0;
};

// ============================================================================
// THE conversion. IFC is right-handed, Z-up, metres. UE is left-handed, Z-up, centimetres.
// Conventional conversion negates Y. That is orientation-REVERSING (det < 0), which is exactly
// what changes the handedness - and it is also why triangle winding must be reversed.
// ============================================================================

struct Conv
{
	const char* name;
	double sx, sy, sz;
	bool reverseWinding;
};

// The real one. Everything else in this table is a NEGATIVE CONTROL: a plausible-looking
// conversion that must be caught by at least one lens. If a control passes every lens, the
// lens set is too weak and this harness is lying.
static const Conv kConvUE       { "IFC->UE  (x*100, -y*100, z*100) + winding reversed", 100.0, -100.0, 100.0, true  };
static const Conv kConvMirrorOnly{ "CTRL-B  mirror, winding NOT reversed",              100.0, -100.0, 100.0, false };
static const Conv kConvScaleOnly { "CTRL-A  scale only, no mirror, no winding reversal",100.0,  100.0, 100.0, false };
static const Conv kConvMirrorX   { "CTRL-D  mirror on X instead of Y + winding reversed", -100.0, 100.0, 100.0, true  };
static const Conv kConvMirrorZ   { "CTRL-E  mirror on Z instead of Y + winding reversed", 100.0, 100.0, -100.0, true  };
static const Conv kConvMirrorXYZ { "CTRL-F  mirror on all three axes + winding reversed",-100.0,-100.0,-100.0, true  };

static inline V3 applyConv(const Conv& c, const V3& p) { return { p.x * c.sx, p.y * c.sy, p.z * c.sz }; }
static inline double convDet(const Conv& c) { return c.sx * c.sy * c.sz; }

// Declared UE axis convention, INDEPENDENT of the conversion under test. This is the ground truth a
// chirality error is measured against: physical IFC +Y is physical UE -Y, so an asymmetric feature at
// the IFC +Y end of a wall MUST land at the UE -Y end. Nothing about volume or watertightness can see
// a violation of this - only an asymmetric feature can.
struct AxisMap { int axis; double sign; };
static const AxisMap kUeAxisFromIfc[3] = { { 0, +1.0 }, { 1, -1.0 }, { 2, +1.0 } };

static const double kExpectedScale = 100.0;                        // m -> cm
static const double kExpectedVolumeRatio = 100.0 * 100.0 * 100.0;  // 1e6

// ============================================================================
// Lens primitives
// ============================================================================

// Transform + optionally reverse a face polygon. Reversal is materialised rather than done with
// clever index arithmetic: this is a proof harness, and an off-by-one in a winding reversal is
// precisely the class of bug it exists to catch.
static void buildFace(const Poly& src, const Conv& c, std::vector<V3>& out)
{
	const size_t n = src.v.size();
	out.clear();
	out.reserve(n);
	if (c.reverseWinding)
	{
		for (size_t i = n; i-- > 0; ) out.push_back(applyConv(c, src.v[i]));
	}
	else
	{
		for (size_t i = 0; i < n; ++i) out.push_back(applyConv(c, src.v[i]));
	}
}

// Bounding box of a transformed face set. Declared here because the volume integrals pivot on it.
struct Aabb;
static Aabb aabbOfImpl(const std::vector<Poly>& faces, const Conv& c);

// Both integrals below are pivoted at the face set's own AABB centre rather than at the world origin.
//
// The divergence theorem is translation-invariant for a CLOSED mesh, so this changes nothing
// mathematically - but it changes everything numerically. Pivoting at the origin makes each tetra term
// O(|r|^3) in the distance from the origin, and those large terms then cancel down to the true volume.
// On the IFC4X3 test building (spans -23..+16 m) three IfcSensor solids of 4.84e-4 m^3 were computed
// from terms of order 8e3: a relative error near 4e-9, which is enough to break a 1e-9 ratio claim and
// look exactly like a coordinate bug. Pivoting locally removes that cancellation entirely.
//
// The pivot corresponds exactly under the conversion (the UE AABB centre IS the converted IFC AABB
// centre, because the conversion is a diagonal scale), so ratio claims across spaces stay valid.

// Signed volume: V = (1/6) * sum over fan triangles of dot(a, cross(b, c)).
// For a CLOSED mesh with outward-facing, consistently-wound triangles this is the true volume and is
// POSITIVE. Negative means inverted winding. Wrong magnitude means the CSG or the scale is wrong.
static double signedVolume(const std::vector<Poly>& faces, const Conv& c);
// Volume centroid, by the same tetra decomposition. Needed by the normal test (as the interior
// reference point) and by the asymmetry test (an off-centre void displaces it).
static bool volumeCentroid(const std::vector<Poly>& faces, const Conv& c, V3& outC, double& outV);

// Newell's method: area-weighted normal following the right-hand rule for the polygon's winding.
// For a polygon wound counter-clockwise as seen from outside, this points OUTWARD.
static V3 newellNormal(const std::vector<V3>& p)
{
	V3 n{};
	const size_t k = p.size();
	for (size_t i = 0; i < k; ++i)
	{
		const V3& a = p[i];
		const V3& b = p[(i + 1) % k];
		n.x += (a.y - b.y) * (a.z + b.z);
		n.y += (a.z - b.z) * (a.x + b.x);
		n.z += (a.x - b.x) * (a.y + b.y);
	}
	return n;
}

struct Aabb
{
	V3 lo{ std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	V3 hi{ -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };
	bool valid = false;
	void add(const V3& p)
	{
		valid = true;
		lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
		lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
		lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
	}
	V3 centre() const { return vmul(vadd(lo, hi), 0.5); }
	V3 size() const   { return vsub(hi, lo); }
	double boxVolume() const { const V3 s = size(); return s.x * s.y * s.z; }
};

static Aabb aabbOfImpl(const std::vector<Poly>& faces, const Conv& c)
{
	Aabb b;
	for (const Poly& f : faces)
		for (const V3& q : f.v) b.add(applyConv(c, q));
	return b;
}

static Aabb aabbOf(const std::vector<Poly>& faces, const Conv& c) { return aabbOfImpl(faces, c); }

// Definitions of the two integrals declared above. Pivot = the face set's own AABB centre.
static double signedVolume(const std::vector<Poly>& faces, const Conv& c)
{
	const Aabb box = aabbOfImpl(faces, c);
	if (!box.valid) return 0.0;
	const V3 pivot = box.centre();

	double V = 0.0;
	std::vector<V3> p;
	for (const Poly& f : faces)
	{
		buildFace(f, c, p);
		for (size_t i = 1; i + 1 < p.size(); ++i)
		{
			const V3 a = vsub(p[0], pivot), b = vsub(p[i], pivot), d = vsub(p[i + 1], pivot);
			V += vdot(a, vcross(b, d)) / 6.0;
		}
	}
	return V;
}

static bool volumeCentroid(const std::vector<Poly>& faces, const Conv& c, V3& outC, double& outV)
{
	const Aabb box = aabbOfImpl(faces, c);
	if (!box.valid) { outV = 0.0; return false; }
	const V3 pivot = box.centre();

	double V = 0.0;
	V3 acc{};
	std::vector<V3> p;
	for (const Poly& f : faces)
	{
		buildFace(f, c, p);
		for (size_t i = 1; i + 1 < p.size(); ++i)
		{
			const V3 a = vsub(p[0], pivot), b = vsub(p[i], pivot), d = vsub(p[i + 1], pivot);
			const double vt = vdot(a, vcross(b, d)) / 6.0;
			V += vt;
			acc = vadd(acc, vmul(vadd(vadd(a, b), d), vt * 0.25));
		}
	}
	outV = V;
	if (std::fabs(V) < 1e-18) return false;
	// acc/V is the centroid RELATIVE to the pivot; shift it back into the working space.
	outC = vadd(vmul(acc, 1.0 / V), pivot);
	return true;
}

// LENS 3. Every face normal must point away from the solid's interior reference point.
// Strictly valid only for a CONVEX solid, so the caller decides whether a violation is fatal:
// a wall with a window hole has interior void faces whose normals legitimately fail this test.
struct NormalResult
{
	size_t faces = 0;
	size_t violations = 0;
	double worstDot = 0.0;
};

static NormalResult normalOutwardness(const std::vector<Poly>& faces, const Conv& c, const V3& interior)
{
	NormalResult r;
	std::vector<V3> p;
	for (const Poly& f : faces)
	{
		buildFace(f, c, p);
		if (p.size() < 3) continue;
		V3 centroid{};
		for (const V3& q : p) centroid = vadd(centroid, q);
		centroid = vmul(centroid, 1.0 / static_cast<double>(p.size()));

		const V3 n = newellNormal(p);
		const double nl = vlen(n);
		if (nl < 1e-18) continue;   // degenerate sliver, no opinion

		const V3 outward = vsub(centroid, interior);
		const double ol = vlen(outward);
		if (ol < 1e-18) continue;

		const double d = vdot(n, outward) / (nl * ol);
		++r.faces;
		if (d <= 0.0) ++r.violations;
		if (r.faces == 1 || d < r.worstDot) r.worstDot = d;
	}
	return r;
}

// Is this closed set a single axis-aligned box? If so the normal test is strict, because a box is
// convex. Independent second signal: a box is 12 triangulated faces (6 quads) and fills its AABB.
static bool looksConvexBox(const std::vector<Poly>& faces, const Conv& c, double volume)
{
	const Aabb b = aabbOf(faces, c);
	if (!b.valid) return false;
	const double bv = b.boxVolume();
	if (bv <= 0.0) return false;
	return std::fabs(std::fabs(volume) - bv) <= 1e-6 * bv;
}

// ============================================================================
// Message collection. "unknown IFC entity" is the one that matters: it means the
// name-substitution table missed and the entity was dropped.
// ============================================================================

class MessageHandler
{
public:
	void slot(shared_ptr<StatusCallback::Message> m)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m) return;
		if (m->m_message_type == StatusCallback::MESSAGE_TYPE_PROGRESS_VALUE) return;

		std::string txt(m->m_message_text.begin(), m->m_message_text.end());
		if (txt.empty()) return;

		switch (m->m_message_type)
		{
		case StatusCallback::MESSAGE_TYPE_ERROR:    ++m_numErrors;   break;
		case StatusCallback::MESSAGE_TYPE_WARNING:  ++m_numWarnings; break;
		default: break;
		}

		if (txt.find("unknown") != std::string::npos || txt.find("Unknown") != std::string::npos)
		{
			++m_unknownEntity[txt];
			return;
		}
		if (m->m_message_type == StatusCallback::MESSAGE_TYPE_ERROR
			|| m->m_message_type == StatusCallback::MESSAGE_TYPE_WARNING)
		{
			++m_other[txt];
		}
	}

	void report() const
	{
		std::cout << "\n--- messages: " << m_numErrors << " errors, " << m_numWarnings << " warnings ---\n";
		std::cout << "unknown/unsupported entity messages (" << m_unknownEntity.size() << " distinct):\n";
		for (const auto& kv : m_unknownEntity)
		{
			std::cout << "   [" << kv.second << "x] " << kv.first << "\n";
		}
		std::cout << "other errors/warnings (" << m_other.size() << " distinct, showing up to 25):\n";
		size_t shown = 0;
		for (const auto& kv : m_other)
		{
			if (shown++ >= 25) { std::cout << "   ... (" << (m_other.size() - 25) << " more)\n"; break; }
			std::cout << "   [" << kv.second << "x] " << kv.first << "\n";
		}
	}

	std::mutex m_mutex;
	std::atomic<size_t> m_numErrors{0};
	std::atomic<size_t> m_numWarnings{0};
	std::map<std::string, size_t> m_unknownEntity;
	std::map<std::string, size_t> m_other;
};

// ============================================================================
// Extraction: carve -> Solid. The ONLY place carve is touched.
// ============================================================================

static void collectItem(const shared_ptr<ItemShapeData>& item, const carve::math::Matrix& xform, Solid& s)
{
	if (!item) return;

	auto walk = [&](const std::vector<shared_ptr<carve::mesh::MeshSet<3> > >& sets, bool closedSet)
	{
		for (const auto& meshset : sets)
		{
			if (!meshset) continue;
			++s.meshsets;
			for (auto mesh : meshset->meshes)
			{
				s.openEdges += mesh->open_edges.size();
				for (auto face : mesh->faces)
				{
					Poly poly;
					poly.v.reserve(face->n_edges);
					carve::mesh::Edge<3>* edge = face->edge;
					for (size_t ii = 0; ii < face->n_edges && edge; ++ii)
					{
						const carve::geom::vector<3> p = xform * edge->vert->v;
						poly.v.push_back({ p.x, p.y, p.z });
						edge = edge->next;
					}
					if (poly.v.size() >= 3)
					{
						if (closedSet) s.closed.push_back(std::move(poly));
						else           s.open.push_back(std::move(poly));
					}
				}
			}
		}
	};

	walk(item->m_meshsets, true);
	walk(item->m_meshsets_open, false);

	for (const auto& child : item->m_child_items) collectItem(child, xform, s);
}

// ============================================================================
// Reporting helpers
// ============================================================================

static const char* axisName(int a) { return a == 0 ? "X" : (a == 1 ? "Y" : "Z"); }

static bool relClose(double a, double b, double tol)
{
	const double scale = std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
	return std::fabs(a - b) <= tol * scale;
}

struct LensTally
{
	size_t pass = 0, fail = 0;
	void add(bool ok) { if (ok) ++pass; else ++fail; }
};

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: LoadFileExample <path-to.ifc>\n";
		return 2;
	}
	const std::string path = argv[1];

	MessageHandler mh;
	shared_ptr<BuildingModel> model(new BuildingModel());
	shared_ptr<ReaderSTEP> reader(new ReaderSTEP());
	reader->setMessageCallBack(std::bind(&MessageHandler::slot, &mh, std::placeholders::_1));

	std::cout << "loading: " << path << std::endl;
	reader->loadModelFromFile(path, model);

	// NOTE: getIfcSchemaVersionOfLoadedFile() is unreliable - it reports IFC4X3 for an IFC2X3 header.
	// Cosmetic IFC++ bug (handoff §8.2). Never branch on it; read the FILE_SCHEMA header yourself.
	std::cout << "schema of loaded file : " << model->getIfcSchemaVersionOfLoadedFile() << "\n"
	          << "schema after loading  : " << model->getIfcSchemaVersionCurrent() << std::endl;

	shared_ptr<GeometrySettings> settings(new GeometrySettings());
	shared_ptr<GeometryConverter> conv(new GeometryConverter(model, settings));
	conv->setMessageCallBack(std::bind(&MessageHandler::slot, &mh, std::placeholders::_1));
	conv->setCsgEps(1.5e-9);

	std::cout << "converting geometry..." << std::endl;
	conv->convertGeometry();

	// ---- extract every product once --------------------------------------------------------
	std::vector<Solid> solids;
	std::map<std::string, size_t> emptyByClass;
	std::map<std::string, size_t> okByClass;

	const std::unordered_map<std::string, shared_ptr<ProductShapeData> >& entities = conv->getShapeInputData();
	for (const auto& it : entities)
	{
		shared_ptr<ProductShapeData> shapeData = it.second;
		if (!shapeData || shapeData->m_ifc_object_definition.expired()) continue;

		shared_ptr<IfcObjectDefinition> obj(shapeData->m_ifc_object_definition);

		Solid s;
		s.cls = EntityFactory::getStringForClassID(obj->classID());
		if (obj->m_GlobalId) s.guid = obj->m_GlobalId->m_value;

		const carve::math::Matrix xform = shapeData->getTransform();
		for (const auto& geomItem : shapeData->getGeometricItems()) collectItem(geomItem, xform, s);

		if (!s.closed.empty() || !s.open.empty()) { ++okByClass[s.cls]; solids.push_back(std::move(s)); }
		else                                      { ++emptyByClass[s.cls]; }
	}

	// ---- smoke-test totals ------------------------------------------------------------------
	size_t totalFaces = 0, totalTris = 0, totalMeshsets = 0;
	Aabb worldIfc;
	for (const Solid& s : solids)
	{
		totalMeshsets += s.meshsets;
		for (const std::vector<Poly>* fs : { &s.closed, &s.open })
			for (const Poly& f : *fs)
			{
				++totalFaces;
				totalTris += (f.v.size() - 2);
				for (const V3& q : f.v) worldIfc.add(q);
			}
	}

	std::cout << "\n================ SMOKE TEST (IFC space) ================\n";
	std::cout << "products in shape map      : " << (solids.size() + [&]{ size_t n = 0; for (const auto& kv : emptyByClass) n += kv.second; return n; }()) << "\n";
	std::cout << "products WITH geometry     : " << solids.size() << "\n";
	std::cout << "meshsets                   : " << totalMeshsets << "\n";
	std::cout << "faces                      : " << totalFaces << "\n";
	std::cout << "triangles (fan estimate)   : " << totalTris << "\n";
	if (worldIfc.valid)
	{
		const V3 sz = worldIfc.size();
		std::cout << std::fixed << std::setprecision(3);
		std::cout << "world AABB min             : (" << worldIfc.lo.x << ", " << worldIfc.lo.y << ", " << worldIfc.lo.z << ")\n";
		std::cout << "world AABB max             : (" << worldIfc.hi.x << ", " << worldIfc.hi.y << ", " << worldIfc.hi.z << ")\n";
		std::cout << "world AABB size            : (" << sz.x << ", " << sz.y << ", " << sz.z << ")   <-- expect metres\n";
	}

	std::cout << "\nproducts WITH geometry, by class:\n";
	for (const auto& kv : okByClass)    std::cout << "   " << kv.second << "  " << kv.first << "\n";
	std::cout << "products with NO geometry, by class:\n";
	for (const auto& kv : emptyByClass) std::cout << "   " << kv.second << "  " << kv.first << "\n";

	// ---- per-product volumes in IFC space (the §5.2 / §5.3 anchors) --------------------------
	std::cout << "\n================ PER-PRODUCT, IFC SPACE ================\n";
	std::cout << std::fixed << std::setprecision(6);
	for (const Solid& s : solids)
	{
		if (s.cls != "IfcWall" && s.cls != "IfcWallStandardCase" && s.cls != "IfcSlab"
			&& s.cls != "IfcDoor" && s.cls != "IfcWindow" && s.cls != "IfcOpeningElement"
			&& s.cls != "IfcSpace") continue;

		const double vol = signedVolume(s.closed, Conv{ "ifc", 1.0, 1.0, 1.0, false });
		std::cout << "   [" << s.cls << "] guid=" << s.guid
		          << " faces=" << (s.closed.size() + s.open.size())
		          << " meshsets=" << s.meshsets
		          << " volume_m3=" << vol
		          << " openEdges=" << s.openEdges
		          << (s.openEdges == 0 ? "  WATERTIGHT" : "  ** NOT CLOSED **")
		          << (vol < 0.0 ? "  ** NEGATIVE => INVERTED WINDING **" : "")
		          << "\n";
	}

	// =========================================================================================
	// COORDINATE PROOF (handoff §5.6)
	// =========================================================================================

	const Conv kIdentity{ "IFC space (identity)", 1.0, 1.0, 1.0, false };

	std::cout << "\n\n########################################################################\n";
	std::cout << "#  COORDINATE PROOF - IFC (right-handed, Z-up, m) -> UE (left-handed, Z-up, cm)\n";
	std::cout << "########################################################################\n";
	std::cout << "conversion under test : " << kConvUE.name << "\n";
	std::cout << std::scientific << std::setprecision(6);
	std::cout << "determinant           : " << convDet(kConvUE)
	          << "   (expect " << -kExpectedVolumeRatio << ", i.e. NEGATIVE = orientation-reversing)\n";
	std::cout << std::fixed << std::setprecision(6);

	// ---- LENS 0: the transform matrix itself -------------------------------------------------
	const double det = convDet(kConvUE);
	const bool lens0_detSign  = det < 0.0;
	const bool lens0_detMag   = relClose(std::fabs(det), kExpectedVolumeRatio, 1e-12);
	const bool lens0_scaleMag = relClose(std::fabs(kConvUE.sx), kExpectedScale, 1e-12)
	                         && relClose(std::fabs(kConvUE.sy), kExpectedScale, 1e-12)
	                         && relClose(std::fabs(kConvUE.sz), kExpectedScale, 1e-12);
	const bool lens0_windingFix = kConvUE.reverseWinding;

	std::cout << "\n--- LENS 0: transform properties ---\n";
	std::cout << "   det < 0 (handedness actually changes)        : " << (lens0_detSign ? "PASS" : "FAIL") << "\n";
	std::cout << "   |det| == 100^3                              : " << (lens0_detMag ? "PASS" : "FAIL") << "\n";
	std::cout << "   all three axis scales are exactly 100        : " << (lens0_scaleMag ? "PASS" : "FAIL") << "\n";
	std::cout << "   winding reversal applied exactly once        : " << (lens0_windingFix ? "PASS" : "FAIL") << "\n";

	// ---- LENS 1/2/3 per product --------------------------------------------------------------
	LensTally lens1, lens2a, lens2b, lens3, lens4topo;
	std::vector<std::string> lens1Bad, lens2aBad, lens2bBad, lens3Bad, lens1Degenerate;

	std::cout << "\n--- LENS 1: volume magnitude scales by 100^3   |   LENS 2: winding sign   |   LENS 3: normals outward ---\n";
	std::cout << "  (LENS 3 is STRICT only on convex boxes; non-convex solids have legitimate interior void faces)\n\n";

	for (const Solid& s : solids)
	{
		if (s.closed.empty()) continue;

		const double vIfc      = signedVolume(s.closed, kIdentity);
		const double vMirror   = signedVolume(s.closed, kConvMirrorOnly);   // mirror, no winding fix
		const double vUe       = signedVolume(s.closed, kConvUE);           // the real conversion
		if (std::fabs(vIfc) < 1e-12) continue;

		// LENS 1 - magnitude
		// A degenerate solid - volume so small it is indistinguishable from accumulated round-off -
		// cannot support a 1e-9 relative claim about its own scaling. Report it as UNTESTABLE rather
		// than as a conversion failure: the two are completely different findings, and calling
		// round-off a coordinate bug would send session 2 hunting a phantom.
		const bool degenerate = std::fabs(vIfc) < 1e-9;
		const double ratio = std::fabs(vUe) / std::fabs(vIfc);
		const bool ok1 = relClose(ratio, kExpectedVolumeRatio, 1e-9);
		if (degenerate)
		{
			std::ostringstream os;
			os << std::scientific << std::setprecision(6)
			   << s.cls << " " << s.guid << "  V_ifc=" << vIfc << " m3  V_ue=" << vUe
			   << " cm3  ratio=" << ratio << "  (|V_ifc| < 1e-9 => below round-off floor)";
			lens1Degenerate.push_back(os.str());
		}
		else
		{
			lens1.add(ok1);
			if (!ok1)
			{
				std::ostringstream os;
				os << std::scientific << std::setprecision(6)
				   << s.cls << " " << s.guid << "  V_ifc=" << vIfc << " m3  V_ue=" << vUe
				   << " cm3  ratio=" << ratio << "  expected " << kExpectedVolumeRatio;
				lens1Bad.push_back(os.str());
			}
		}

		// LENS 2a - mirroring alone MUST make the signed volume negative. If it does not, the
		// mirror never happened and the "conversion" is a pure scale.
		const bool ok2a = (vIfc > 0.0) ? (vMirror < 0.0) : (vMirror > 0.0);
		lens2a.add(ok2a);
		if (!ok2a) lens2aBad.push_back(s.cls + " " + s.guid);

		// LENS 2b - after mirror AND winding reversal the signed volume must be POSITIVE again.
		const bool ok2b = vUe > 0.0;
		lens2b.add(ok2b);
		if (!ok2b) lens2bBad.push_back(s.cls + " " + s.guid);

		// LENS 3 - normals
		V3 cUe{}; double vTmp = 0.0;
		bool ok3 = true;
		NormalResult nr;
		if (volumeCentroid(s.closed, kConvUE, cUe, vTmp))
		{
			nr = normalOutwardness(s.closed, kConvUE, cUe);
			const bool convex = looksConvexBox(s.closed, kConvUE, vUe);
			ok3 = convex ? (nr.violations == 0) : true;   // advisory on non-convex
			if (convex) { lens3.add(ok3); if (!ok3) lens3Bad.push_back(s.cls + " " + s.guid); }
		}

		lens4topo.add(s.openEdges == 0);

		if (s.cls == "IfcWall" || s.cls == "IfcWallStandardCase" || s.cls == "IfcSlab" || s.cls == "IfcSpace")
		{
			const bool convex = looksConvexBox(s.closed, kConvUE, vUe);
			std::cout << "   [" << s.cls << "] " << s.guid << "\n"
			          << "        V_ifc      = " << vIfc << " m3\n"
			          << "        V_mirror   = " << vMirror << " cm3   (expect NEGATIVE)  " << (ok2a ? "PASS" : "FAIL") << "\n"
			          << "        V_ue       = " << vUe << " cm3   (expect POSITIVE)  " << (ok2b ? "PASS" : "FAIL") << "\n"
			          << "        ratio      = " << std::scientific << ratio << std::fixed
			          << "   (expect 1.000000e+06)  " << (ok1 ? "PASS" : "FAIL") << "\n"
			          << "        normals    = " << nr.violations << "/" << nr.faces << " inward"
			          << (convex ? "  [convex box: STRICT]" : "  [non-convex: advisory]")
			          << "   worst cos = " << nr.worstDot << "\n";
		}
	}

	// ---- LENS 4: ASYMMETRY / CHIRALITY -------------------------------------------------------
	// Volume and watertightness are BOTH mirror-invariant. A left-right flipped building passes
	// every lens above. The only thing that exposes it is an asymmetric feature: a wall whose
	// opening is off-centre. Its volume centroid is displaced AWAY from the hole, so the sign of
	// (volumeCentroid - aabbCentre) along the wall's long axis tells you which end the hole is at.
	//
	// That end must land on the physically corresponding UE end per kUeAxisFromIfc - NOT merely at
	// "some" end. Under the correct conversion, an IFC +Y feature reports at UE -Y, because UE's Y
	// axis points the opposite physical way. Under a mirror on the WRONG axis (CTRL-D) the volume,
	// the winding and the normals all still pass, and only this test fails.

	std::cout << "\n--- LENS 4: ASYMMETRY / CHIRALITY ---\n";

	// Lens 4 needs TWO complementary sub-checks, because neither alone is sufficient:
	//
	//   4a  CHIRALITY INVARIANT. A signed triple product over three physically-selected landmarks
	//       must FLIP sign, because det < 0. Catches a MISSING mirror (or an even number of them)
	//       - but not a mirror on the wrong axis, since any single mirror flips it.
	//
	//   4b  PER-AXIS FEATURE CORRESPONDENCE on X and Y. An asymmetric feature must land on the
	//       physically corresponding UE end per kUeAxisFromIfc. Catches a mirror on the WRONG axis
	//       - which 4a, volume, winding and watertightness are all blind to.
	//
	// Z is deliberately excluded from 4b: this conversion does not touch Z's sign, so a Z-asymmetric
	// feature cannot discriminate between ANY of the variants. An earlier revision of this harness
	// auto-picked Z, and every control "passed" - the harness correctly reported itself vacuous.

	// Aggregate every closed face in the model into one pseudo-solid. A whole building is reliably
	// asymmetric in both X and Y, and a mirrored BUILDING is the actual bug being hunted - not a
	// mirrored wall. Volumes of disjoint closed solids simply add, so the tetra decomposition is valid.
	std::vector<Poly> aggregate;
	for (const Solid& s : solids)
		aggregate.insert(aggregate.end(), s.closed.begin(), s.closed.end());

	bool lens4a_pass = false, lens4b_pass = false;

	// ---- 4a: chirality invariant ----
	// Landmarks are the three largest-volume products. Volume is mirror-invariant, so the SELECTION
	// itself cannot change under any variant - only the sign of the triple product can. GUID
	// tie-breaks keep it deterministic across unordered_map iteration order.
	{
		struct Landmark { double vol; std::string guid; V3 c; };
		std::vector<Landmark> lms;
		for (const Solid& s : solids)
		{
			if (s.closed.empty()) continue;
			V3 c{}; double v = 0.0;
			if (!volumeCentroid(s.closed, kIdentity, c, v)) continue;
			lms.push_back({ std::fabs(v), s.guid, c });
		}
		std::sort(lms.begin(), lms.end(), [](const Landmark& a, const Landmark& b)
		{
			if (a.vol != b.vol) return a.vol > b.vol;
			return a.guid < b.guid;
		});

		if (lms.size() < 3)
		{
			std::cout << "   4a CHIRALITY: fewer than 3 solid products - CANNOT RUN.\n";
		}
		else
		{
			V3 cAll{}; double vAll = 0.0;
			volumeCentroid(aggregate, kIdentity, cAll, vAll);

			auto chirality = [&](const Conv& c) -> double
			{
				V3 ctr{}; double vt = 0.0;
				volumeCentroid(aggregate, c, ctr, vt);
				V3 l[3];
				for (int i = 0; i < 3; ++i) l[i] = vsub(applyConv(c, lms[i].c), ctr);
				return vdot(l[0], vcross(l[1], l[2]));
			};

			const double chIfc = chirality(kIdentity);
			std::cout << "   4a CHIRALITY INVARIANT  triple product over 3 largest products\n";
			std::cout << "        landmarks: " << lms[0].guid << " (" << lms[0].vol << " m3), "
			          << lms[1].guid << " (" << lms[1].vol << " m3), "
			          << lms[2].guid << " (" << lms[2].vol << " m3)\n";
			std::cout << std::scientific << std::setprecision(6);
			std::cout << "        IFC space : " << chIfc << "\n";

			if (std::fabs(chIfc) < 1e-9)
			{
				std::cout << "        landmarks are near-coplanar - 4a CANNOT RUN.\n" << std::fixed;
			}
			else
			{
				auto report4a = [&](const Conv& c, bool expectFlip) -> bool
				{
					const double ch = chirality(c);
					const bool flipped = (ch * chIfc) < 0.0;
					const bool ok = (flipped == expectFlip);
					std::cout << "        " << std::left << std::setw(52) << c.name << std::right
					          << " " << std::setw(14) << ch
					          << (flipped ? "  FLIPPED" : "  same sign")
					          << "   " << (ok ? "PASS" : "FAIL") << "\n";
					return ok;
				};
				lens4a_pass = report4a(kConvUE, true);
				const bool a_ctrlA = report4a(kConvScaleOnly, true);   // no mirror -> must NOT flip -> FAIL
				const bool a_ctrlF = report4a(kConvMirrorXYZ, true);   // 3 mirrors -> flips, 4a blind by design
				std::cout << std::fixed << std::setprecision(6);
				std::cout << "        CTRL-A (no mirror) expected FAIL, got "
				          << (a_ctrlA ? "PASS ** 4a IS VACUOUS **" : "FAIL  (good)") << "\n";
				std::cout << "        CTRL-F (triple mirror) passes 4a by design - 4b is what catches it\n";
				(void)a_ctrlF;
				if (a_ctrlA) lens4a_pass = false;
			}
		}
	}

	// ---- 4b: per-axis feature correspondence, X and Y only ----
	//
	// The subject is chosen PER AXIS, not once for the whole test. The whole-model aggregate of a
	// typical building is often near-symmetric on one horizontal axis (this IFC2X3 file is
	// Y-symmetric to 1.5e-4 m), and a subject that is symmetric on an axis cannot detect a mirror
	// error on that axis - a control would sail through. Both X and Y must be exercised: X alone
	// cannot catch a MISSING Y mirror, and Y alone cannot catch a mirror applied to X.
	{
		struct Subject
		{
			const char* label = "";
			const std::vector<Poly>* faces = nullptr;
			double offIfc = 0.0;    // signed centroid offset from AABB centre, metres
			double norm = 0.0;      // offset / extent
		};

		// Candidate subjects: the aggregate, plus every individual solid product.
		auto measure = [&](const char* label, const std::vector<Poly>& faces, int a, Subject& out) -> bool
		{
			V3 c{}; double v = 0.0;
			if (!volumeCentroid(faces, kIdentity, c, v)) return false;
			const Aabb b = aabbOf(faces, kIdentity);
			if (!b.valid) return false;
			const double ext = b.size()[a];
			if (ext < 1e-9) return false;
			const double off = vsub(c, b.centre())[a];
			// Usability floor. The discriminator is a SIGN flip, so the offset only has to clear
			// numerical noise - but it must clear it by a wide margin or a "pass" means nothing.
			// 1e-5 m is ~7 orders above double round-off on these coordinate magnitudes.
			if (std::fabs(off) < 1e-5) return false;
			out.label = label; out.faces = &faces; out.offIfc = off; out.norm = std::fabs(off) / ext;
			return true;
		};

		Subject best[2];
		bool haveAxis[2] = { false, false };
		for (int a = 0; a < 2; ++a)
		{
			Subject cand;
			if (measure("whole-model aggregate", aggregate, a, cand))
			{
				best[a] = cand; haveAxis[a] = true;
			}
			for (const Solid& s : solids)
			{
				if (s.closed.empty()) continue;
				if (measure(s.guid.c_str(), s.closed, a, cand)
					&& (!haveAxis[a] || cand.norm > best[a].norm))
				{
					best[a] = cand; haveAxis[a] = true;
				}
			}
		}

		std::cout << "\n   4b PER-AXIS FEATURE CORRESPONDENCE  (subject chosen per axis; X and Y only,\n"
		          << "      because this conversion does not touch Z's sign and Z therefore cannot discriminate)\n";

		for (int a = 0; a < 2; ++a)
		{
			if (!haveAxis[a])
			{
				std::cout << "        IFC " << axisName(a) << ": NO ASYMMETRIC SUBJECT FOUND - axis cannot be tested\n";
				continue;
			}
			const AxisMap m = kUeAxisFromIfc[a];
			std::cout << "        IFC " << axisName(a) << ": subject " << best[a].label
			          << "  offset " << best[a].offIfc << " m (normalised " << best[a].norm << ")\n"
			          << "             material missing at IFC " << (best[a].offIfc < 0.0 ? "+" : "-") << axisName(a)
			          << " end;  declared UE convention: physical IFC +" << axisName(a)
			          << " == UE " << (m.sign > 0 ? "+" : "-") << axisName(m.axis) << "\n"
			          // Claim stated before measurement, so a pass cannot be read backwards from the result.
			          << "             CLAIM: expected UE " << axisName(m.axis) << " offset = "
			          << (best[a].offIfc * kExpectedScale * m.sign) << " cm\n";
		}

		if (!haveAxis[0] || !haveAxis[1])
		{
			std::cout << "        BOTH X AND Y ARE REQUIRED and one is missing. 4b CANNOT RUN -\n"
			          << "        do NOT record Lens 4 as a pass on this file.\n";
		}
		else
		{
			auto runVariant = [&](const Conv& c) -> bool
			{
				bool allOk = true;
				std::cout << "        " << std::left << std::setw(52) << c.name << std::right;
				for (int a = 0; a < 2; ++a)
				{
					const AxisMap m = kUeAxisFromIfc[a];
					V3 cU{}; double vU = 0.0;
					if (!volumeCentroid(*best[a].faces, c, cU, vU)) { allOk = false; continue; }
					const Aabb bU = aabbOf(*best[a].faces, c);
					const double expected = best[a].offIfc * kExpectedScale * m.sign;
					const double measured = vsub(cU, bU.centre())[m.axis];
					// Magnitude AND sign must both match: the same physical end, not merely the
					// same distance from centre. Sign is the whole point of the test.
					const bool ok = relClose(measured, expected, 1e-6) && (measured * expected > 0.0);
					allOk = allOk && ok;
					std::cout << "  " << axisName(m.axis) << "=" << measured << (ok ? "(ok)" : "(BAD)");
				}
				std::cout << "   " << (allOk ? "PASS" : "FAIL") << "\n";
				return allOk;
			};

			lens4b_pass        = runVariant(kConvUE);
			const bool b_ctrlA = runVariant(kConvScaleOnly);
			const bool b_ctrlD = runVariant(kConvMirrorX);
			const bool b_ctrlE = runVariant(kConvMirrorZ);
			const bool b_ctrlF = runVariant(kConvMirrorXYZ);

			std::cout << "\n        controls (each MUST fail, or 4b has no discriminating power):\n";
			std::cout << "          CTRL-A no mirror         : " << (b_ctrlA ? "PASS ** VACUOUS **" : "FAIL  (good)") << "\n";
			std::cout << "          CTRL-D mirror on X       : " << (b_ctrlD ? "PASS ** VACUOUS **" : "FAIL  (good)") << "\n";
			std::cout << "          CTRL-E mirror on Z       : " << (b_ctrlE ? "PASS ** VACUOUS **" : "FAIL  (good)") << "\n";
			std::cout << "          CTRL-F mirror on X,Y,Z   : " << (b_ctrlF ? "PASS ** VACUOUS **" : "FAIL  (good)") << "\n";

			if (b_ctrlA || b_ctrlD || b_ctrlE || b_ctrlF) lens4b_pass = false;
		}
	}

	// Per-product asymmetry table, for the engineer to eyeball. Not a pass/fail gate.
	std::cout << "\n   per-product normalised centroid offsets (asymmetry evidence, IFC space):\n";
	for (const Solid& s : solids)
	{
		if (s.closed.empty()) continue;
		if (s.cls != "IfcWall" && s.cls != "IfcWallStandardCase" && s.cls != "IfcSlab") continue;
		V3 c{}; double v = 0.0;
		if (!volumeCentroid(s.closed, kIdentity, c, v)) continue;
		const Aabb b = aabbOf(s.closed, kIdentity);
		const V3 off = vsub(c, b.centre());
		const V3 ext = b.size();
		std::cout << "      [" << s.cls << "] " << s.guid << "  offX=" << off.x << " offY=" << off.y
		          << " offZ=" << off.z << "   extent=(" << ext.x << ", " << ext.y << ", " << ext.z << ")\n";
	}

	const bool lens4_pass = lens4a_pass && lens4b_pass;

	// ---- negative-control sweep on volume/winding, to prove lenses 1-2 also discriminate ------
	std::cout << "\n--- NEGATIVE CONTROLS on lenses 1-3 (each must be caught by at least one lens) ---\n";
	{
		auto sweep = [&](const Conv& c)
		{
			size_t nPosVol = 0, nNegVol = 0, nBadRatio = 0, nNormViol = 0, nConvex = 0, nTot = 0;
			for (const Solid& s : solids)
			{
				if (s.closed.empty()) continue;
				const double vIfc = signedVolume(s.closed, kIdentity);
				if (std::fabs(vIfc) < 1e-12) continue;
				const double v = signedVolume(s.closed, c);
				++nTot;
				if (v > 0.0) ++nPosVol; else ++nNegVol;
				if (!relClose(std::fabs(v) / std::fabs(vIfc), kExpectedVolumeRatio, 1e-9)) ++nBadRatio;

				V3 ctr{}; double vt = 0.0;
				if (volumeCentroid(s.closed, c, ctr, vt) && looksConvexBox(s.closed, c, v))
				{
					++nConvex;
					if (normalOutwardness(s.closed, c, ctr).violations > 0) ++nNormViol;
				}
			}
			std::cout << "   " << std::left << std::setw(52) << c.name << std::right
			          << " det=" << std::scientific << std::setprecision(2) << convDet(c) << std::fixed << std::setprecision(6)
			          << "  vol+:" << nPosVol << "/" << nTot
			          << "  badRatio:" << nBadRatio
			          << "  inwardNormals(convex):" << nNormViol << "/" << nConvex << "\n";
		};
		sweep(kConvUE);
		sweep(kConvMirrorOnly);
		sweep(kConvScaleOnly);
		sweep(kConvMirrorX);
		sweep(kConvMirrorZ);
		sweep(kConvMirrorXYZ);
	}

	// ---- verdict ----------------------------------------------------------------------------
	const bool p0 = lens0_detSign && lens0_detMag && lens0_scaleMag && lens0_windingFix;
	const bool p1 = lens1.fail  == 0 && lens1.pass  > 0;
	const bool p2 = lens2a.fail == 0 && lens2b.fail == 0 && lens2b.pass > 0;
	const bool p3 = lens3.fail  == 0;
	const bool p4 = lens4_pass;

	std::cout << "\n================ COORDINATE PROOF VERDICT ================\n";
	std::cout << "LENS 0  transform properties (det<0, |det|=100^3, winding fix)  : " << (p0 ? "PASS" : "FAIL") << "\n";
	std::cout << "LENS 1  volume magnitude x100^3        : " << (p1 ? "PASS" : "FAIL")
	          << "   (" << lens1.pass << " pass, " << lens1.fail << " fail)\n";
	std::cout << "LENS 2  signed volume NEGATIVE after mirror, POSITIVE after winding reversal : "
	          << (p2 ? "PASS" : "FAIL") << "   (mirror " << lens2a.pass << "/" << (lens2a.pass + lens2a.fail)
	          << ", fixed " << lens2b.pass << "/" << (lens2b.pass + lens2b.fail) << ")\n";
	std::cout << "LENS 3  face normals outward (convex solids, strict)  : " << (p3 ? "PASS" : "FAIL")
	          << "   (" << lens3.pass << " pass, " << lens3.fail << " fail)\n";
	std::cout << "LENS 4  ASYMMETRY / chirality, with negative controls : " << (p4 ? "PASS" : "FAIL")
	          << "   (4a chirality " << (lens4a_pass ? "PASS" : "FAIL")
	          << ", 4b axis correspondence " << (lens4b_pass ? "PASS" : "FAIL") << ")\n";
	std::cout << "topology unchanged by conversion (openEdges==0)       : "
	          << (lens4topo.fail == 0 ? "PASS" : "FAIL")
	          << "   (" << lens4topo.pass << " pass, " << lens4topo.fail << " fail)\n";

	for (const auto* bad : { &lens1Bad, &lens2aBad, &lens2bBad, &lens3Bad })
	{
		for (const std::string& g : *bad) std::cout << "   FAILED: " << g << "\n";
	}
	if (!lens1Degenerate.empty())
	{
		std::cout << "\n   UNTESTABLE by Lens 1 (" << lens1Degenerate.size()
		          << " products): volume below the 1e-9 m3 round-off floor, so no 1e-9 relative ratio\n"
		          << "   claim about them is meaningful. NOT a conversion failure - but NOT evidence of\n"
		          << "   correctness either. Lenses 2-4 still cover these products.\n";
		for (const std::string& g : lens1Degenerate) std::cout << "      " << g << "\n";
	}

	mh.report();

	const bool smokePass = (totalTris > 0 && !solids.empty());
	const bool coordPass = p0 && p1 && p2 && p3 && p4;

	std::cout << "\nVERDICT smoke test       : " << (smokePass ? "GEOMETRY PRODUCED" : "NO GEOMETRY - FAIL") << "\n";
	std::cout << "VERDICT coordinate proof : " << (coordPass ? "ALL LENSES PASS" : "FAIL") << std::endl;
	return (smokePass && coordPass) ? 0 : 1;
}
